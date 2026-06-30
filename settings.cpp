// =============================================================================
// settings.cpp — Implementación de la configuración
// =============================================================================

#include "settings.h"
#include "utils.h"

#include <cstdio>
#include <string>

// ── FilterSettings ──────────────────────────────────────────────────────────

float FilterSettings::BlurStrengthFactor() const noexcept {
    return static_cast<float>(blur_strength) / 100.0f;
}

float FilterSettings::EdgeSoftnessFactor() const noexcept {
    return static_cast<float>(edge_softness) / 100.0f;
}

float FilterSettings::MaskThresholdFactor() const noexcept {
    return static_cast<float>(mask_threshold) / 100.0f;
}

float FilterSettings::BlurRadiusFloat() const noexcept {
    return static_cast<float>(blur_radius);
}

uint32_t FilterSettings::ModelInputSize() const noexcept {
    switch (quality) {
        case QualityLevel::Low:    return 256;
        case QualityLevel::Medium: return 384;
        case QualityLevel::High:   return 512;
        case QualityLevel::Ultra:  return 1024;
        default:                   return 384;
    }
}

void FilterSettings::ResetToDefaults() noexcept {
    blur_strength    = ParamLimits::BlurStrengthDefault;
    edge_softness    = ParamLimits::EdgeSoftnessDefault;
    mask_threshold   = ParamLimits::MaskThresholdDefault;
    blur_radius      = ParamLimits::BlurRadiusDefault;

    quality           = ParamLimits::QualityDefault;
    ai_model          = ParamLimits::AIModelDefault;
    processing_device = ParamLimits::ProcessingDeviceDefault;

    show_mask          = ParamLimits::ShowMaskDefault;
    high_quality_edges = ParamLimits::HighQualityEdgesDefault;
    enable_gpu_blur    = ParamLimits::EnableGPUBlurDefault;
}

// ── SettingsIO ──────────────────────────────────────────────────────────────

namespace SettingsIO {

// NOTA: Estas funciones dependen de obs-data.h de OBS. Como settings.h
// promete no incluir cabeceras de OBS, la inclusión va aquí en .cpp.
#include <obs-data.h>

void Save(FilterSettings& settings, void* obs_data) {
    if (!obs_data) return;
    auto* data = static_cast<obs_data_t*>(obs_data);

    obs_data_set_int(data, "blur_strength",    settings.blur_strength);
    obs_data_set_int(data, "edge_softness",    settings.edge_softness);
    obs_data_set_int(data, "mask_threshold",   settings.mask_threshold);
    obs_data_set_int(data, "blur_radius",      settings.blur_radius);

    obs_data_set_int(data, "quality",           static_cast<int>(settings.quality));
    obs_data_set_int(data, "ai_model",          static_cast<int>(settings.ai_model));
    obs_data_set_int(data, "processing_device", static_cast<int>(settings.processing_device));

    obs_data_set_bool(data, "show_mask",          settings.show_mask);
    obs_data_set_bool(data, "high_quality_edges", settings.high_quality_edges);
    obs_data_set_bool(data, "enable_gpu_blur",    settings.enable_gpu_blur);
}

void Load(FilterSettings& settings, const void* obs_data) {
    if (!obs_data) return;
    const auto* data = static_cast<const obs_data_t*>(obs_data);

    settings.blur_strength  = MathUtils::Clamp(
        static_cast<int>(obs_data_get_int(data, "blur_strength")),
        ParamLimits::BlurStrengthMin,
        ParamLimits::BlurStrengthMax
    );
    settings.edge_softness  = MathUtils::Clamp(
        static_cast<int>(obs_data_get_int(data, "edge_softness")),
        ParamLimits::EdgeSoftnessMin,
        ParamLimits::EdgeSoftnessMax
    );
    settings.mask_threshold = MathUtils::Clamp(
        static_cast<int>(obs_data_get_int(data, "mask_threshold")),
        ParamLimits::MaskThresholdMin,
        ParamLimits::MaskThresholdMax
    );
    settings.blur_radius    = MathUtils::Clamp(
        static_cast<int>(obs_data_get_int(data, "blur_radius")),
        ParamLimits::BlurRadiusMin,
        ParamLimits::BlurRadiusMax
    );

    settings.quality           = static_cast<QualityLevel>(
        MathUtils::Clamp(
            static_cast<int>(obs_data_get_int(data, "quality")),
            0, 3
        )
    );
    settings.ai_model = static_cast<AIModelType>(
        MathUtils::Clamp(
            static_cast<int>(obs_data_get_int(data, "ai_model")),
            0, 2
        )
    );
    settings.processing_device = static_cast<ProcessingDevice>(
        MathUtils::Clamp(
            static_cast<int>(obs_data_get_int(data, "processing_device")),
            0, 3
        )
    );

    settings.show_mask          = obs_data_get_bool(data, "show_mask");
    settings.high_quality_edges = obs_data_get_bool(data, "high_quality_edges");
    settings.enable_gpu_blur    = obs_data_get_bool(data, "enable_gpu_blur");
}

std::string ToString(const FilterSettings& s) {
    char buf[512] = {};
    std::snprintf(buf, sizeof(buf),
        "BlurStrength=%d EdgeSoftness=%d MaskThreshold=%d BlurRadius=%d "
        "Quality=%d AIModel=%d Device=%d ShowMask=%d HQEdges=%d GPUBlur=%d",
        s.blur_strength, s.edge_softness, s.mask_threshold, s.blur_radius,
        static_cast<int>(s.quality),
        static_cast<int>(s.ai_model),
        static_cast<int>(s.processing_device),
        static_cast<int>(s.show_mask),
        static_cast<int>(s.high_quality_edges),
        static_cast<int>(s.enable_gpu_blur)
    );
    return std::string(buf);
}

} // namespace SettingsIO
