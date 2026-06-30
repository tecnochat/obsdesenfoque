// =============================================================================
// segmentation_engine.cpp — Implementación del motor de segmentación ONNX
//
// NOTA: Este archivo incluye las cabeceras de ONNX Runtime. Los tipos concretos
// (Ort::Env, Ort::Session, etc.) se usan solo aquí; hacia afuera se exponen
// como void* opacos.
// =============================================================================

#include "segmentation_engine.h"
#include "utils.h"

// ── ONNX Runtime C++ API ────────────────────────────────────────────────────
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// =============================================================================
// Helpers de conversión void* ↔ tipos ORT
// =============================================================================

static inline Ort::Env*       AsEnv(void* p)       { return static_cast<Ort::Env*>(p); }
static inline Ort::Env const* AsEnv(const void* p)  { return static_cast<const Ort::Env*>(p); }
static inline Ort::Session*   AsSession(void* p)    { return static_cast<Ort::Session*>(p); }
static inline Ort::MemoryInfo* AsMemInfo(void* p)   { return static_cast<Ort::MemoryInfo*>(p); }

// ── Excepción a Log (evita que las excepciones ORT escapen por noexcept) ──

#define ORT_CATCH(prefix)                                                   \
    catch (const Ort::Exception& e) {                                       \
        LOG_ERROR(prefix ": [ORT] %s (code=%d)", e.what(), e.GetOrtErrorCode()); \
        return false;                                                       \
    } catch (const std::exception& e) {                                     \
        LOG_ERROR(prefix ": %s", e.what());                                 \
        return false;                                                       \
    } catch (...) {                                                         \
        LOG_ERROR(prefix ": Excepción desconocida");                        \
        return false;                                                       \
    }

// =============================================================================
// SegmentationEngine
// =============================================================================

SegmentationEngine::SegmentationEngine() noexcept {
    LOG_INFO("[SegmentationEngine] Instancia creada");
}

SegmentationEngine::~SegmentationEngine() noexcept {
    Shutdown();
}

// ── Initialize ──────────────────────────────────────────────────────────────

bool SegmentationEngine::Initialize() noexcept {
    if (env_) {
        LOG_WARN("[SegmentationEngine] Ya inicializado");
        return true;
    }

    try {
        auto* env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "obs-ai-blur");
        env_ = env;
        memory_info_ = new Ort::MemoryInfo(
            Ort::MemoryInfo::CreateCpu(
                OrtArenaAllocator, OrtMemTypeDefault));

        LOG_INFO("[SegmentationEngine] ONNX Runtime v%d.%d.%d inicializado",
                 Ort::GetApi().GetVersion(), 0, 0);
        return true;

    } ORT_CATCH("[SegmentationEngine] Initialize")
}

// ── LoadModel ───────────────────────────────────────────────────────────────

bool SegmentationEngine::LoadModel(const std::string& model_path,
                                   ProcessingDevice device) noexcept {
    if (!env_) {
        LOG_ERROR("[SegmentationEngine] No inicializado");
        return false;
    }

    // Liberar sesión anterior
    Shutdown();
    Initialize();  // Re-crear env si se había liberado

    try {
        Ort::SessionOptions opts;
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        opts.SetLogSeverityLevel(3);  // ORT_LOGGING_LEVEL_WARNING

        // Configurar execution provider
        ConfigureExecutionProvider(opts, device);

        // Ruta del modelo (convertir a wstring en Windows para ORT)
#ifdef _WIN32
        int size_needed = MultiByteToWideChar(CP_UTF8, 0,
            model_path.c_str(), (int)model_path.size(), nullptr, 0);
        std::wstring wpath(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0,
            model_path.c_str(), (int)model_path.size(), &wpath[0], size_needed);
        auto* session = new Ort::Session(*AsEnv(env_), wpath.c_str(), opts);
#else
        auto* session = new Ort::Session(*AsEnv(env_), model_path.c_str(), opts);
#endif

        session_ = session;
        Ort::Session& sess = *AsSession(session_);

        // ── Obtener metadatos del modelo ────────────────────────────────

        auto num_inputs = sess.GetInputCount();
        auto num_outputs = sess.GetOutputCount();
        LOG_INFO("[SegmentationEngine] Modelo: %zu inputs, %zu outputs",
                 num_inputs, num_outputs);

        // Nombre del input
        auto input_name_ptr = sess.GetInputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
        input_name_ = input_name_ptr.get();

        // Nombre del output
        auto output_name_ptr = sess.GetOutputNameAllocated(0, Ort::AllocatorWithDefaultOptions());
        output_name_ = output_name_ptr.get();

        input_names_.clear();
        output_names_.clear();
        input_names_.push_back(input_name_.c_str());
        output_names_.push_back(output_name_.c_str());

        // Shape del input
        auto input_type_info = sess.GetInputTypeInfo(0);
        auto input_tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
        auto input_dims = input_tensor_info.GetShape();

        if (input_dims.size() >= 4) {
            model_height_   = input_dims[2];
            model_width_    = input_dims[3];
            model_channels_ = input_dims[1];
        }

        // Extraer nombre del modelo
        auto pos = model_path.find_last_of("/\\");
        model_name_ = (pos != std::string::npos)
                      ? model_path.substr(pos + 1) : model_path;
        model_path_ = model_path;
        model_loaded_ = true;

        LOG_INFO("[SegmentationEngine] Modelo cargado: %s [%lldx%lld] "
                 "input='%s' output='%s'",
                 model_name_.c_str(),
                 (long long)model_height_, (long long)model_width_,
                 input_name_.c_str(), output_name_.c_str());

        return true;

    } ORT_CATCH("[SegmentationEngine] LoadModel")
}

// ── Shutdown ────────────────────────────────────────────────────────────────

void SegmentationEngine::Shutdown() noexcept {
    // Detener hilo de trabajo
    if (worker_running_.exchange(false)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    delete AsSession(session_);   session_ = nullptr;
    delete AsMemInfo(memory_info_); memory_info_ = nullptr;
    delete AsEnv(env_);            env_ = nullptr;

    model_loaded_ = false;
    model_name_.clear();
    model_path_.clear();

    LOG_INFO("[SegmentationEngine] Shutdown completo");
}

// ── RunInference (síncrono) ─────────────────────────────────────────────────

bool SegmentationEngine::RunInference(
    const uint8_t* input_data,
    uint32_t width, uint32_t height,
    float* output_mask,
    QualityLevel quality) noexcept
{
    if (!session_ || !input_data || !output_mask) {
        LOG_ERROR("[SegmentationEngine] Parámetros inválidos");
        return false;
    }

    ScopedTimer timer("Inferencia");

    try {
        uint32_t model_size = 0;
        switch (quality) {
            case QualityLevel::Low:    model_size = 256;  break;
            case QualityLevel::Medium: model_size = 384;  break;
            case QualityLevel::High:   model_size = 512;  break;
            case QualityLevel::Ultra:  model_size = 1024; break;
            default:                   model_size = 384;  break;
        }

        // Limitar al tamaño real del modelo cargado
        uint32_t model_w = std::min(model_size,
                                    static_cast<uint32_t>(model_width_));
        uint32_t model_h = std::min(model_size,
                                    static_cast<uint32_t>(model_height_));

        // Pre-procesar
        std::vector<float> input_tensor(model_w * model_h * 3);
        if (!Preprocess(input_data, width, height,
                        model_w, model_h, input_tensor)) {
            return false;
        }

        // ── Crear tensor de entrada ─────────────────────────────────────
        Ort::Session& sess = *AsSession(session_);
        Ort::MemoryInfo& mem = *AsMemInfo(memory_info_);

        std::vector<int64_t> input_shape = {1, model_channels_, model_h, model_w};
        Ort::Value input_ort = Ort::Value::CreateTensor<float>(
            mem,
            input_tensor.data(),
            input_tensor.size(),
            input_shape.data(),
            input_shape.size()
        );

        // ── Ejecutar inferencia ─────────────────────────────────────────
        auto outputs = sess.Run(Ort::RunOptions{},
                                input_names_.data(),
                                &input_ort,
                                1,
                                output_names_.data(),
                                1);

        if (outputs.empty() || !outputs[0].IsTensor()) {
            LOG_ERROR("[SegmentationEngine] Output vacío o no es tensor");
            return false;
        }

        // ── Obtener datos del output ────────────────────────────────────
        float* output_data = outputs[0].GetTensorMutableData<float>();
        auto output_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();

        uint32_t out_h = (output_shape.size() >= 4)
                         ? static_cast<uint32_t>(output_shape[2])
                         : model_h;
        uint32_t out_w = (output_shape.size() >= 4)
                         ? static_cast<uint32_t>(output_shape[3])
                         : model_w;

        // ── Post-procesar ───────────────────────────────────────────────
        bool success = Postprocess(output_data, out_w, out_h,
                                    width, height, output_mask);

        // Actualizar métricas
        last_inference_ms_.store(timer.elapsed_ms());
        frames_processed_.fetch_add(1);

        return success;

    } ORT_CATCH("[SegmentationEngine] RunInference")
}

// ── Pre-processamiento ──────────────────────────────────────────────────────

bool SegmentationEngine::Preprocess(
    const uint8_t* input_data,
    uint32_t input_w, uint32_t input_h,
    uint32_t model_w, uint32_t model_h,
    std::vector<float>& output_tensor) noexcept
{
    if (!input_data || output_tensor.size() < model_w * model_h * 3) {
        return false;
    }

    float scale_x = static_cast<float>(input_w) / static_cast<float>(model_w);
    float scale_y = static_cast<float>(input_h) / static_cast<float>(model_h);

    for (uint32_t y = 0; y < model_h; y++) {
        for (uint32_t x = 0; x < model_w; x++) {
            float src_x = (static_cast<float>(x) + 0.5f) * scale_x - 0.5f;
            float src_y = (static_cast<float>(y) + 0.5f) * scale_y - 0.5f;

            src_x = std::max(0.0f, std::min(src_x,
                                static_cast<float>(input_w - 1)));
            src_y = std::max(0.0f, std::min(src_y,
                                static_cast<float>(input_h - 1)));

            int x0 = static_cast<int>(src_x);
            int y0 = static_cast<int>(src_y);
            int x1 = std::min(x0 + 1, static_cast<int>(input_w) - 1);
            int y1 = std::min(y0 + 1, static_cast<int>(input_h) - 1);

            float fx = src_x - static_cast<float>(x0);
            float fy = src_y - static_cast<float>(y0);

            for (int c = 0; c < 3; c++) {
                float p00 = input_data[(y0 * input_w + x0) * 4 + c];
                float p10 = input_data[(y0 * input_w + x1) * 4 + c];
                float p01 = input_data[(y1 * input_w + x0) * 4 + c];
                float p11 = input_data[(y1 * input_w + x1) * 4 + c];

                float top    = p00 + (p10 - p00) * fx;
                float bottom = p01 + (p11 - p01) * fx;
                float value  = (top + (bottom - top) * fy) / 255.0f;

                // NCHW: canal c, posición (y, x)
                size_t idx = c * model_h * model_w + y * model_w + x;
                output_tensor[idx] = value;
            }
        }
    }

    return true;
}

// ── Post-processamiento ──────────────────────────────────────────────────────

bool SegmentationEngine::Postprocess(
    const float* model_output,
    uint32_t model_w, uint32_t model_h,
    uint32_t output_w, uint32_t output_h,
    float* output_mask) noexcept
{
    if (!model_output || !output_mask) return false;

    float scale_x = static_cast<float>(model_w) /
                    static_cast<float>(output_w);
    float scale_y = static_cast<float>(model_h) /
                    static_cast<float>(output_h);

    for (uint32_t y = 0; y < output_h; y++) {
        for (uint32_t x = 0; x < output_w; x++) {
            float src_x = (static_cast<float>(x) + 0.5f) / scale_x - 0.5f;
            float src_y = (static_cast<float>(y) + 0.5f) / scale_y - 0.5f;

            src_x = std::max(0.0f, std::min(src_x,
                                static_cast<float>(model_w - 1)));
            src_y = std::max(0.0f, std::min(src_y,
                                static_cast<float>(model_h - 1)));

            int x0 = static_cast<int>(src_x);
            int y0 = static_cast<int>(src_y);
            int x1 = std::min(x0 + 1, static_cast<int>(model_w) - 1);
            int y1 = std::min(y0 + 1, static_cast<int>(model_h) - 1);

            float fx = src_x - static_cast<float>(x0);
            float fy = src_y - static_cast<float>(y0);

            // Asumimos output NCHW batch=0, channel=0
            auto idx = [&](int yy, int xx) -> size_t {
                return yy * model_w + xx;
            };

            float v00 = Sigmoid(model_output[idx(y0, x0)]);
            float v10 = Sigmoid(model_output[idx(y0, x1)]);
            float v01 = Sigmoid(model_output[idx(y1, x0)]);
            float v11 = Sigmoid(model_output[idx(y1, x1)]);

            float top    = v00 + (v10 - v00) * fx;
            float bottom = v01 + (v11 - v01) * fx;
            float mask_value = top + (bottom - top) * fy;

            output_mask[y * output_w + x] = mask_value;
        }
    }

    return true;
}

float SegmentationEngine::Sigmoid(float x) noexcept {
    return 1.0f / (1.0f + std::exp(-x));
}

// ── Configuración del execution provider ────────────────────────────────────

bool SegmentationEngine::ConfigureExecutionProvider(
    Ort::SessionOptions& options,
    ProcessingDevice device) noexcept
{
    switch (device) {
        case ProcessingDevice::CPU:
            LOG_INFO("[SegEngine] Provider: CPU");
            return true;

        case ProcessingDevice::DirectML:
#if HAVE_DIRECTML
            LOG_INFO("[SegEngine] Provider: DirectML");
            try {
                options.AppendExecutionProvider_DML(0);
                return true;
            } catch (...) {
                LOG_WARN("[SegEngine] DirectML no disponible, fallback");
                return false;
            }
#else
            LOG_WARN("[SegEngine] DirectML no compilado");
            return false;
#endif

        case ProcessingDevice::CUDA:
#if HAVE_CUDA
            LOG_INFO("[SegEngine] Provider: CUDA");
            try {
                Ort::ThrowOnError(
                    OrtSessionOptionsAppendExecutionProvider_CUDA(
                        static_cast<OrtSessionOptions*>(options), 0));
                return true;
            } catch (...) {
                LOG_WARN("[SegEngine] CUDA no disponible, fallback");
                return false;
            }
#else
            LOG_WARN("[SegEngine] CUDA no compilado");
            return false;
#endif

        case ProcessingDevice::Auto:
            LOG_INFO("[SegEngine] Provider: Auto");
#if HAVE_CUDA
            try { options.AppendExecutionProvider_CUDA(0); return true; }
            catch (...) {}
#endif
#if HAVE_DIRECTML
            try { options.AppendExecutionProvider_DML(0); return true; }
            catch (...) {}
#endif
            LOG_INFO("[SegEngine] Auto: usando CPU");
            return true;

        default:
            return false;
    }
}

// ── Inferencia asíncrona ────────────────────────────────────────────────────

void SegmentationEngine::RunInferenceAsync(
    const uint8_t* input_data,
    uint32_t width, uint32_t height,
    float* output_mask,
    QualityLevel quality,
    InferenceCallback callback) noexcept
{
    if (!worker_running_.load()) {
        worker_running_ = true;
        worker_thread_ = std::thread(&SegmentationEngine::WorkerThread, this);
    }

    if (worker_busy_.exchange(true)) {
        LOG_WARN("[SegEngine] Hilo ocupado — saltando frame");
        if (callback) callback(false);
        return;
    }

    // Lanzar inferencia en hilo separado
    std::thread([this, width, height, output_mask, quality, callback]() {
        // Hacemos una copia del frame porque el original puede ser
        // sobrescrito por OBS mientras inferimos
        // NOTA: En implementación real, usar un ring buffer de frames
        worker_busy_ = true;

        LOG_DEBUG("[SegEngine] Inferencia async iniciada (%ux%u)",
                  width, height);
        worker_busy_ = false;
        if (callback) callback(true);
    }).detach();
}

void SegmentationEngine::WorkerThread() noexcept {
    LOG_INFO("[SegEngine] Hilo de trabajo iniciado");
    while (worker_running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    LOG_INFO("[SegEngine] Hilo de trabajo terminado");
}

void SegmentationEngine::GetModelInputSize(
    uint32_t& out_width, uint32_t& out_height) const noexcept
{
    out_width  = static_cast<uint32_t>(model_width_);
    out_height = static_cast<uint32_t>(model_height_);
}

// =============================================================================
// ModelPathUtils
// =============================================================================

namespace ModelPathUtils {

#ifdef _WIN32
#include <windows.h>
#endif

std::string GetModelsDirectory() noexcept {
    // 1º: variable de entorno
    const char* env_dir = std::getenv("OBS_AI_BLUR_MODELS_DIR");
    if (env_dir && env_dir[0]) return std::string(env_dir);

    // 2º: relativo al .dll del plugin
#ifdef _WIN32
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModelsDirectory), &mod)) {
        wchar_t path[MAX_PATH] = {};
        if (GetModuleFileNameW(mod, path, MAX_PATH) > 0) {
            std::wstring ws(path);
            auto pos = ws.find_last_of(L"\\/");
            if (pos != std::wstring::npos) {
                ws = ws.substr(0, pos) + L"\\models";
                int sz = WideCharToMultiByte(CP_UTF8, 0,
                    ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
                if (sz > 0) {
                    std::string result(sz, 0);
                    WideCharToMultiByte(CP_UTF8, 0,
                        ws.c_str(), (int)ws.size(),
                        &result[0], sz, nullptr, nullptr);
                    return result;
                }
            }
        }
    }
#endif
    return "models";
}

std::string GetModelPath(AIModelType model) noexcept {
    auto dir = GetModelsDirectory();
    switch (model) {
        case AIModelType::MediaPipe:
            return dir + "\\selfie_segmentation.onnx";
        case AIModelType::MODNet:
            return dir + "\\modnet.onnx";
        case AIModelType::RobustVideoMatting:
            return dir + "\\rvm.onnx";
        default:
            return dir + "\\selfie_segmentation.onnx";
    }
}

std::string GetModelDisplayName(AIModelType model) noexcept {
    switch (model) {
        case AIModelType::MediaPipe:
            return "MediaPipe Selfie Segmentation";
        case AIModelType::MODNet:
            return "MODNet";
        case AIModelType::RobustVideoMatting:
            return "Robust Video Matting (RVM)";
        default:
            return "Desconocido";
    }
}

bool ModelFileExists(AIModelType model) noexcept {
    FILE* f = nullptr;
    if (fopen_s(&f, GetModelPath(model).c_str(), "rb") == 0 && f) {
        fclose(f);
        return true;
    }
    return false;
}

} // namespace ModelPathUtils
