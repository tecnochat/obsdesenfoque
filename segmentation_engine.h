// =============================================================================
// segmentation_engine.h — Motor de segmentación por IA (ONNX Runtime)
//
// Clase independiente del filtro OBS que encapsula:
//   - Carga de modelos ONNX (MediaPipe, MODNet, RVM)
//   - Inferencia mediante ONNX Runtime
//   - Múltiples execution providers (CPU, DirectML, CUDA, Auto)
//   - Generación de máscara binaria con post-procesamiento
//
// Toda la inferencia se ejecuta en un hilo separado.
// El hilo de renderizado de OBS nunca se bloquea.
// =============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "settings.h"  // Para QualityLevel, AIModelType, ProcessingDevice

// ═════════════════════════════════════════════════════════════════════════════
// SegmentationEngine
// ═════════════════════════════════════════════════════════════════════════════

class SegmentationEngine {
public:
    SegmentationEngine() noexcept;
    ~SegmentationEngine() noexcept;

    // No copiable ni movible
    SegmentationEngine(const SegmentationEngine&) = delete;
    SegmentationEngine& operator=(const SegmentationEngine&) = delete;

    // ── Ciclo de vida ───────────────────────────────────────────────────

    /// Inicializa el motor: crea entorno ONNX Runtime
    bool Initialize() noexcept;

    /// Carga un modelo ONNX desde el sistema de archivos.
    bool LoadModel(const std::string& model_path,
                   ProcessingDevice device = ProcessingDevice::Auto) noexcept;

    /// Descarga el modelo actual y libera recursos
    void Shutdown() noexcept;

    /// Verifica si el motor está listo para inferencia
    bool IsReady() const noexcept { return session_ != nullptr; }

    // ── Inferencia ──────────────────────────────────────────────────────

    /// Ejecuta inferencia síncrona (llama desde hilo secundario).
    bool RunInference(const uint8_t* input_data,
                      uint32_t width, uint32_t height,
                      float* output_mask,
                      QualityLevel quality) noexcept;

    /// Callback para inferencia asíncrona
    using InferenceCallback = std::function<void(bool success)>;

    /// Programa inferencia asíncrona (no bloquea al llamador).
    void RunInferenceAsync(const uint8_t* input_data,
                           uint32_t width, uint32_t height,
                           float* output_mask,
                           QualityLevel quality,
                           InferenceCallback callback) noexcept;

    // ── Información del modelo ──────────────────────────────────────────

    void GetModelInputSize(uint32_t& out_width,
                           uint32_t& out_height) const noexcept;

    std::string GetModelName() const noexcept { return model_name_; }

    // ── Métricas ────────────────────────────────────────────────────────

    double   LastInferenceTimeMs() const noexcept { return last_inference_ms_.load(); }
    uint64_t TotalFramesProcessed() const noexcept { return frames_processed_.load(); }

private:
    // Pre-procesamiento
    bool Preprocess(const uint8_t* input_data,
                    uint32_t input_w, uint32_t input_h,
                    uint32_t model_w, uint32_t model_h,
                    std::vector<float>& output_tensor) noexcept;

    // Post-procesamiento
    bool Postprocess(const float* model_output,
                     uint32_t model_w, uint32_t model_h,
                     uint32_t output_w, uint32_t output_h,
                     float* output_mask) noexcept;

    static float Sigmoid(float x) noexcept;

    // ─── Pimpl: ocultamos los tipos de ONNX Runtime ──────────────────────
    // Usamos void* para no exponer las cabeceras de ORT en este header.
    // En el .cpp se castean a los tipos reales (Ort::Session*, etc.)

    void* env_         = nullptr;  // Ort::Env*
    void* session_     = nullptr;  // Ort::Session*
    void* memory_info_ = nullptr;  // Ort::MemoryInfo*

    // ── Metadatos del modelo ────────────────────────────────────────────

    std::string        model_name_;
    std::string        model_path_;
    bool               model_loaded_ = false;

    std::string        input_name_;
    std::string        output_name_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;

    int64_t model_channels_ = 3;
    int64_t model_height_   = 256;
    int64_t model_width_    = 256;

    // ── Hilo asíncrono ──────────────────────────────────────────────────

    std::thread        worker_thread_;
    std::mutex         work_mutex_;
    std::atomic<bool>  worker_running_{false};
    std::atomic<bool>  worker_busy_{false};

    // ── Métricas ────────────────────────────────────────────────────────

    std::atomic<double>  last_inference_ms_{0.0};
    std::atomic<uint64_t> frames_processed_{0};
};

// ═════════════════════════════════════════════════════════════════════════════
// ModelPathUtils
// ═════════════════════════════════════════════════════════════════════════════

namespace ModelPathUtils {

std::string GetModelsDirectory() noexcept;
std::string GetModelPath(AIModelType model) noexcept;
std::string GetModelDisplayName(AIModelType model) noexcept;
bool        ModelFileExists(AIModelType model) noexcept;

} // namespace ModelPathUtils
