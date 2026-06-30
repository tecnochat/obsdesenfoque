// =============================================================================
// settings.h — Configuración del filtro AI Background Blur
//
// Define todos los parámetros configurables del filtro, sus valores por
// defecto, y utilidades para serialización con OBS Data.
//
// Separación estricta: esta clase solo maneja configuración, no tiene
// dependencias de render ni de IA.
// =============================================================================

#pragma once

#include <cstdint>
#include <string>

// ── Enumeraciones ───────────────────────────────────────────────────────────

/// Calidad de la segmentación. Controla la resolución interna del modelo.
enum class QualityLevel : uint8_t {
    Low    = 0,  ///< 256×256 — máxima velocidad
    Medium = 1,  ///< 384×384 — balance
    High   = 2,  ///< 512×512 — buena calidad
    Ultra  = 3   ///< 1024×1024 — calidad máxima (modelos grandes)
};

/// Modelo de segmentación seleccionable
enum class AIModelType : uint8_t {
    MediaPipe     = 0,  ///< MediaPipe Selfie Segmentation (rápido, ligero)
    MODNet        = 1,  ///< MODNet (balance calidad/velocidad)
    RobustVideoMatting = 2  ///< RVM (mejor calidad, más pesado)
};

/// Dispositivo de procesamiento para la inferencia
enum class ProcessingDevice : uint8_t {
    CPU      = 0,  ///< CPU (fallback, lento)
    DirectML = 1,  ///< DirectML (Windows, cualquier GPU)
    CUDA     = 2,  ///< CUDA (NVIDIA solamente)
    Auto     = 3   ///< Auto-detect: CUDA > DirectML > CPU
};

// ── Constantes ──────────────────────────────────────────────────────────────

/// Límites de los parámetros
struct ParamLimits {
    static constexpr int BlurStrengthMin  = 0;
    static constexpr int BlurStrengthMax  = 100;
    static constexpr int BlurStrengthDefault = 70;

    static constexpr int EdgeSoftnessMin  = 0;
    static constexpr int EdgeSoftnessMax  = 100;
    static constexpr int EdgeSoftnessDefault = 50;

    static constexpr int MaskThresholdMin  = 0;
    static constexpr int MaskThresholdMax  = 100;
    static constexpr int MaskThresholdDefault = 50;

    static constexpr int BlurRadiusMin  = 1;
    static constexpr int BlurRadiusMax  = 50;
    static constexpr int BlurRadiusDefault = 15;

    static constexpr QualityLevel QualityDefault = QualityLevel::Medium;
    static constexpr AIModelType AIModelDefault  = AIModelType::MediaPipe;
    static constexpr ProcessingDevice ProcessingDeviceDefault = ProcessingDevice::Auto;

    static constexpr bool ShowMaskDefault        = false;
    static constexpr bool HighQualityEdgesDefault = true;
    static constexpr bool EnableGPUBlurDefault    = true;
};

// ── Estructura de configuración ─────────────────────────────────────────────

/// Contiene todos los parámetros del filtro en un solo struct.
/// Se pasa por valor (es pequeño) o const-ref.
struct FilterSettings {
    // Controles básicos
    int  blur_strength    = ParamLimits::BlurStrengthDefault;
    int  edge_softness    = ParamLimits::EdgeSoftnessDefault;
    int  mask_threshold   = ParamLimits::MaskThresholdDefault;
    int  blur_radius      = ParamLimits::BlurRadiusDefault;

    // Calidad
    QualityLevel    quality     = ParamLimits::QualityDefault;
    AIModelType     ai_model    = ParamLimits::AIModelDefault;
    ProcessingDevice processing_device = ParamLimits::ProcessingDeviceDefault;

    // Toggles
    bool show_mask           = ParamLimits::ShowMaskDefault;
    bool high_quality_edges  = ParamLimits::HighQualityEdgesDefault;
    bool enable_gpu_blur     = ParamLimits::EnableGPUBlurDefault;

    // ── Métodos de ayuda ────────────────────────────────────────────────

    /// Normaliza blur_strength a un factor [0.0, 1.0] para shaders
    float BlurStrengthFactor() const noexcept;

    /// Normaliza edge_softness a [0.0, 1.0] para shaders
    float EdgeSoftnessFactor() const noexcept;

    /// Normaliza mask_threshold a [0.0, 1.0] para shaders
    float MaskThresholdFactor() const noexcept;

    /// Obtiene el blur_radius como valor flotante (para shaders)
    float BlurRadiusFloat() const noexcept;

    /// Resolución interna del modelo según calidad
    uint32_t ModelInputSize() const noexcept;

    /// Restaura todos los valores por defecto
    void ResetToDefaults() noexcept;
};

// ── Utilidades de serialización OBS ─────────────────────────────────────────

namespace SettingsIO {

/// Guarda la configuración en OBS Data (obs_data_t*)
/// Se pasa obs_data_t* como void* para evitar incluir obs-data.h aquí.
void Save(FilterSettings& settings, void* obs_data);

/// Carga la configuración desde OBS Data
void Load(FilterSettings& settings, const void* obs_data);

/// Obtiene una representación textual (para debugging)
std::string ToString(const FilterSettings& settings);

} // namespace SettingsIO
