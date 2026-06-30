// =============================================================================
// background_filter.cpp — Implementación del filtro OBS
//
// Conecta OBS → SegmentationEngine → BlurRenderer → salida OBS.
// =============================================================================

#include "background_filter.h"
#include "utils.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>

#include <algorithm>
#include <cstring>

// =============================================================================
// Información del filtro para OBS
// =============================================================================

static obs_source_info g_background_filter_info = {};

struct obs_source_info* GetBackgroundFilterInfo() noexcept {
    if (g_background_filter_info.id == nullptr) {
        g_background_filter_info.id             = "ai_background_blur";
        g_background_filter_info.type           = OBS_SOURCE_TYPE_FILTER;
        g_background_filter_info.output_flags   = OBS_SOURCE_VIDEO;

        g_background_filter_info.get_name       = BackgroundFilter::GetName;
        g_background_filter_info.get_defaults   = BackgroundFilter::GetDefaults;
        g_background_filter_info.get_properties = [](void* data) {
            return BackgroundFilter::GetProperties(data);
        };

        g_background_filter_info.create         = BackgroundFilter::OBSCreate;
        g_background_filter_info.destroy        = BackgroundFilter::OBSDestroy;
        g_background_filter_info.update         = BackgroundFilter::OBSUpdate;

        g_background_filter_info.video_tick     = BackgroundFilter::OBSVideoTick;
        g_background_filter_info.video_render   = BackgroundFilter::OBSVideoRender;
        g_background_filter_info.get_width      = BackgroundFilter::OBSGetWidth;
        g_background_filter_info.get_height     = BackgroundFilter::OBSGetHeight;

        // Icono para la UI de OBS
        g_background_filter_info.icon_type = OBS_ICON_TYPE_FILTER;
    }
    return &g_background_filter_info;
}

// =============================================================================
// BackgroundFilter
// =============================================================================

BackgroundFilter::BackgroundFilter(obs_source* source) noexcept
    : source_(source)
{
    LOG_INFO("[BackgroundFilter] Creado (source=%p)", (void*)source);
}

BackgroundFilter::~BackgroundFilter() noexcept {
    Shutdown();
    LOG_INFO("[BackgroundFilter] Destruido");
}

// ── Inicialización ──────────────────────────────────────────────────────────

bool BackgroundFilter::Initialize() noexcept {
    if (initialized_) return true;

    LOG_INFO("[BackgroundFilter] Inicializando...");

    // 1. Inicializar motor de segmentación
    engine_ = std::make_unique<SegmentationEngine>();
    if (!engine_->Initialize()) {
        LOG_ERROR("[BackgroundFilter] Falló inicializar SegmentationEngine");
        return false;
    }

    // 2. Cargar modelo de IA
    if (!LoadAIModel()) {
        LOG_WARN("[BackgroundFilter] Modelo IA no cargado, "
                 "se cargará cuando haya frame disponible");
    }

    // 3. Inicializar renderizador GPU
    renderer_ = std::make_unique<BlurRenderer>();

    // Buscar directorio de shaders (relativo al plugin)
    std::string shaders_dir;
    const char* module_path = obs_get_module_path("ai_background_blur");
    if (module_path) {
        shaders_dir = std::string(module_path) + "/shaders";
    } else {
        shaders_dir = "shaders";
    }

    if (!renderer_->Initialize(shaders_dir)) {
        LOG_ERROR("[BackgroundFilter] Falló inicializar BlurRenderer "
                  "desde %s", shaders_dir.c_str());
        return false;
    }

    initialized_ = true;
    LOG_INFO("[BackgroundFilter] Inicialización completa");
    return true;
}

void BackgroundFilter::Shutdown() noexcept {
    if (!initialized_) return;

    renderer_.reset();
    engine_.reset();

    mask_buffer_1_.clear();
    mask_buffer_2_.clear();
    frame_copy_buffer_.clear();

    front_mask_.store(nullptr, std::memory_order_relaxed);
    back_mask_.store(nullptr, std::memory_order_relaxed);
    mask_valid_.store(false, std::memory_order_relaxed);

    initialized_ = false;
    LOG_INFO("[BackgroundFilter] Shutdown completo");
}

// ── Carga del modelo IA ─────────────────────────────────────────────────────

bool BackgroundFilter::LoadAIModel() noexcept {
    if (!engine_) return false;

    auto model_type = settings_.ai_model;
    std::string model_path = ModelPathUtils::GetModelPath(model_type);

    if (!ModelPathUtils::ModelFileExists(model_type)) {
        LOG_WARN("[BackgroundFilter] Modelo no encontrado: %s",
                 model_path.c_str());

        // Intentar con otros modelos como fallback
        for (int m = 0; m < 3; m++) {
            auto alt = static_cast<AIModelType>(m);
            if (ModelPathUtils::ModelFileExists(alt)) {
                model_path = ModelPathUtils::GetModelPath(alt);
                model_type = alt;
                LOG_INFO("[BackgroundFilter] Fallback a modelo: %s",
                         model_path.c_str());
                break;
            }
        }
    }

    return engine_->LoadModel(model_path, settings_.processing_device);
}

// ── Callbacks estáticos ─────────────────────────────────────────────────────

const char* BackgroundFilter::GetName() noexcept {
    return obs_module_text("AI Background Blur");
}

void BackgroundFilter::GetDefaults(obs_data* settings) noexcept {
    // Valores por defecto (coinciden con ParamLimits en settings.h)
    obs_data_set_default_int(settings, "blur_strength",
                             ParamLimits::BlurStrengthDefault);
    obs_data_set_default_int(settings, "edge_softness",
                             ParamLimits::EdgeSoftnessDefault);
    obs_data_set_default_int(settings, "mask_threshold",
                             ParamLimits::MaskThresholdDefault);
    obs_data_set_default_int(settings, "blur_radius",
                             ParamLimits::BlurRadiusDefault);

    obs_data_set_default_int(settings, "quality",
                             static_cast<int>(ParamLimits::QualityDefault));
    obs_data_set_default_int(settings, "ai_model",
                             static_cast<int>(ParamLimits::AIModelDefault));
    obs_data_set_default_int(settings, "processing_device",
                             static_cast<int>(ParamLimits::ProcessingDeviceDefault));

    obs_data_set_default_bool(settings, "show_mask",
                              ParamLimits::ShowMaskDefault);
    obs_data_set_default_bool(settings, "high_quality_edges",
                              ParamLimits::HighQualityEdgesDefault);
    obs_data_set_default_bool(settings, "enable_gpu_blur",
                              ParamLimits::EnableGPUBlurDefault);
}

obs_properties_t* BackgroundFilter::GetProperties(void* filter_data) noexcept {
    obs_properties_t* props = obs_properties_create();

    // ── Control de Blur ────────────────────────────────────────────────
    obs_properties_add_int_slider(props, "blur_strength",
        obs_module_text("Blur Strength"),
        ParamLimits::BlurStrengthMin, ParamLimits::BlurStrengthMax, 1);
    obs_properties_add_int_slider(props, "edge_softness",
        obs_module_text("Edge Softness"),
        ParamLimits::EdgeSoftnessMin, ParamLimits::EdgeSoftnessMax, 1);
    obs_properties_add_int_slider(props, "mask_threshold",
        obs_module_text("Mask Threshold"),
        ParamLimits::MaskThresholdMin, ParamLimits::MaskThresholdMax, 1);
    obs_properties_add_int_slider(props, "blur_radius",
        obs_module_text("Blur Radius"),
        ParamLimits::BlurRadiusMin, ParamLimits::BlurRadiusMax, 1);

    // ── Separador ──────────────────────────────────────────────────────
    obs_properties_add_text(props, "sep_quality", nullptr, OBS_TEXT_INFO);

    // ── Calidad ─────────────────────────────────────────────────────────
    obs_property_t* quality_list = obs_properties_add_list(props, "quality",
        obs_module_text("Quality"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(quality_list, obs_module_text("Low"),    0);
    obs_property_list_add_int(quality_list, obs_module_text("Medium"), 1);
    obs_property_list_add_int(quality_list, obs_module_text("High"),   2);
    obs_property_list_add_int(quality_list, obs_module_text("Ultra"),  3);

    // ── Modelo IA ───────────────────────────────────────────────────────
    obs_property_t* model_list = obs_properties_add_list(props, "ai_model",
        obs_module_text("AI Model"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(model_list,
        ModelPathUtils::GetModelDisplayName(AIModelType::MediaPipe).c_str(),
        static_cast<int>(AIModelType::MediaPipe));
    obs_property_list_add_int(model_list,
        ModelPathUtils::GetModelDisplayName(AIModelType::MODNet).c_str(),
        static_cast<int>(AIModelType::MODNet));
    obs_property_list_add_int(model_list,
        ModelPathUtils::GetModelDisplayName(AIModelType::RobustVideoMatting).c_str(),
        static_cast<int>(AIModelType::RobustVideoMatting));

    // ── Dispositivo de procesamiento ────────────────────────────────────
    obs_property_t* device_list = obs_properties_add_list(props, "processing_device",
        obs_module_text("Processing Device"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(device_list, obs_module_text("CPU"),      0);
    obs_property_list_add_int(device_list, obs_module_text("DirectML"), 1);
    obs_property_list_add_int(device_list, obs_module_text("CUDA"),     2);
    obs_property_list_add_int(device_list, obs_module_text("Auto"),     3);

    // ── Toggles ─────────────────────────────────────────────────────────
    obs_properties_add_bool(props, "show_mask",
        obs_module_text("Show Mask"));
    obs_properties_add_bool(props, "high_quality_edges",
        obs_module_text("High Quality Edges"));
    obs_properties_add_bool(props, "enable_gpu_blur",
        obs_module_text("Enable GPU Blur"));

    // ── Botón de reset ──────────────────────────────────────────────────
    obs_properties_add_button(props, "reset_defaults",
        obs_module_text("Reset Defaults"),
        [](obs_properties_t* /*props*/, obs_property_t* /*prop*/,
           void* data) {
            auto* filter = static_cast<BackgroundFilter*>(data);
            if (filter) {
                filter->GetMutableSettings().ResetToDefaults();
                LOG_INFO("[BackgroundFilter] Valores restaurados a default");
            }
            return true;
        }
    );

    return props;
}

void BackgroundFilter::Update(obs_data* settings) noexcept {
    std::lock_guard<std::mutex> lock(settings_mutex_);

    SettingsIO::Load(settings_, settings);
    LOG_DEBUG("[BackgroundFilter] Configuración actualizada: %s",
              SettingsIO::ToString(settings_).c_str());

    // Si cambió el modelo o dispositivo, recargar
    // Nota: esta verificación es simplificada; en una implementación
    // completa compararíamos con el modelo cargado actualmente.
    LoadAIModel();
}

void BackgroundFilter::VideoTick(float /*seconds*/) noexcept {
    // Se llama cada frame en el render thread.
    // Aquí podríamos actualizar métricas o gestionar timeouts.
}

void BackgroundFilter::VideoRender() noexcept {
    if (!initialized_ || !renderer_ || !renderer_->IsReady()) {
        obs_source_skip_video_filter(source_);
        return;
    }

    obs_source* parent = obs_filter_get_parent(source_);
    if (!parent) {
        obs_source_skip_video_filter(source_);
        return;
    }

    gs_effect* default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    if (!default_effect) {
        obs_source_skip_video_filter(source_);
        return;
    }

    // ── Iniciar procesamiento del filtro ─────────────────────────────────
    // Sin OBS_ALLOW_DIRECT_RENDERING para que obs_source_process_filter_end
    // no sobreescriba nuestra textura de salida con la textura temporal del parent.
    obs_source_process_filter_begin(source_, GS_RGBA, 0);

    uint32_t width = obs_source_get_base_width(source_);
    uint32_t height = obs_source_get_base_height(source_);
    if (width == 0 || height == 0) {
        obs_source_process_filter_end(source_, default_effect, 0, 0);
        return;
    }

    // ── Obtener textura de entrada ───────────────────────────────────────
    gs_texture* input_texture = obs_source_process_filter_get_texture(source_);

    if (input_texture) {
        // Obtener máscara más reciente (thread-safe)
        float* current_mask = front_mask_.load(std::memory_order_acquire);

        // Ejecutar pipeline completo de blur GPU
        {
            std::lock_guard<std::mutex> lock(settings_mutex_);
            renderer_->Render(input_texture,
                              current_mask,
                              nullptr,
                              width, height,
                              settings_);
        }

        // ── Obtener textura de salida y presentar ────────────────────────
        gs_texture* output = renderer_->GetOutputTexture();
        if (output) {
            // Configurar la textura de salida en el effect por defecto
            gs_eparam_t* image_param = gs_effect_get_param_by_name(
                default_effect, "image");
            gs_effect_set_texture(image_param, output);

            // end() dibuja usando gs_draw_sprite(nullptr, ...) con el effect
            // que pasamos. Como seteamos "image" a nuestra textura de salida,
            // el effect por defecto la usará.
            obs_source_process_filter_end(source_, default_effect,
                                          width, height);
            return;
        }
    }

    // Fallback: dibujar el frame original sin procesar
    obs_source_process_filter_end(source_, default_effect, 0, 0);
}

// ── Render estático de OBS ─────────────────────────────────────────────────

void BackgroundFilter::OBSVideoRender(void* data, gs_effect* /*effect*/) noexcept {
    auto* filter = static_cast<BackgroundFilter*>(data);
    if (filter) {
        filter->VideoRender();
    }
}

// ── Lógica de inferencia ────────────────────────────────────────────────────

void BackgroundFilter::RunInferenceOnFrame(
    const uint8_t* frame_data,
    uint32_t width, uint32_t height) noexcept
{
    if (!engine_ || !engine_->IsReady() || !frame_data) return;

    // Obtener buffer de máscara back
    if (mask_buffer_1_.size() != width * height) {
        mask_buffer_1_.resize(width * height);
        mask_buffer_2_.resize(width * height);
    }

    float* back_buffer = (back_mask_.load(std::memory_order_relaxed)
                          == mask_buffer_1_.data())
                         ? mask_buffer_2_.data()
                         : mask_buffer_1_.data();

    // Ejecutar inferencia
    bool success = false;
    {
        std::lock_guard<std::mutex> lock(settings_mutex_);
        success = engine_->RunInference(
            frame_data, width, height, back_buffer, settings_.quality);
    }

    if (success) {
        // Swap buffers: el back pasa a ser front
        std::lock_guard<std::mutex> lock(mask_swap_mutex_);
        front_mask_.store(back_buffer, std::memory_order_release);
        back_mask_.store((back_buffer == mask_buffer_1_.data())
                         ? mask_buffer_2_.data()
                         : mask_buffer_1_.data(),
                         std::memory_order_relaxed);
        mask_valid_.store(true, std::memory_order_release);
        mask_width_ = width;
        mask_height_ = height;
    }
}

// ── Callbacks estáticos de OBS ──────────────────────────────────────────────

void* BackgroundFilter::OBSCreate(obs_data* settings,
                                  obs_source* source) noexcept {
    auto* filter = new BackgroundFilter(source);
    if (!filter->Initialize()) {
        LOG_ERROR("[OBSCreate] Falló inicialización del filtro");
        // Aun así devolvemos el objeto; OBS lo maneja
    }
    filter->Update(settings);
    return filter;
}

void BackgroundFilter::OBSDestroy(void* data) noexcept {
    auto* filter = static_cast<BackgroundFilter*>(data);
    if (filter) {
        // Shutdown se llama en el destructor
        delete filter;
    }
}

void BackgroundFilter::OBSUpdate(void* data, obs_data* settings) noexcept {
    auto* filter = static_cast<BackgroundFilter*>(data);
    if (filter) {
        filter->Update(settings);
    }
}

void BackgroundFilter::OBSVideoTick(void* data, float seconds) noexcept {
    auto* filter = static_cast<BackgroundFilter*>(data);
    if (filter) {
        filter->VideoTick(seconds);
    }
}

uint32_t BackgroundFilter::OBSGetWidth(void* data) noexcept {
    auto* filter = static_cast<BackgroundFilter*>(data);
    if (filter && filter->GetSource()) {
        return obs_source_get_base_width(filter->GetSource());
    }
    return 0;
}

uint32_t BackgroundFilter::OBSGetHeight(void* data) noexcept {
    auto* filter = static_cast<BackgroundFilter*>(data);
    if (filter && filter->GetSource()) {
        return obs_source_get_base_height(filter->GetSource());
    }
    return 0;
}
