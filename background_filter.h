// =============================================================================
// background_filter.h — Filtro OBS "AI Background Blur"
//
// Implementa el filtro de OBS Studio que orquesta:
//   - Captura de frames desde Video Capture Device
//   - Inferencia ONNX Runtime (hilo separado)
//   - Pipeline de blur GPU (shaders)
//   - Composición final
//
// Esta clase es el punto de integración entre OBS, la IA y la GPU.
// =============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "settings.h"
#include "segmentation_engine.h"
#include "blur_renderer.h"

// ── Forward declarations de OBS ─────────────────────────────────────────────
struct obs_source;
struct obs_data;
struct gs_texture;

// ═════════════════════════════════════════════════════════════════════════════
// BackgroundFilter
// ═════════════════════════════════════════════════════════════════════════════

class BackgroundFilter {
public:
    explicit BackgroundFilter(obs_source* source) noexcept;
    ~BackgroundFilter() noexcept;

    // No copiable ni movible
    BackgroundFilter(const BackgroundFilter&) = delete;
    BackgroundFilter& operator=(const BackgroundFilter&) = delete;

    // ── Callbacks de OBS ────────────────────────────────────────────────

    /// Obtiene el nombre mostrado en la UI
    static const char* GetName() noexcept;

    /// Obtiene los valores por defecto de los parámetros
    static void GetDefaults(obs_data* settings) noexcept;

    /// Obtiene las propiedades (controles) del filtro
    /// @param filter_data Puntero al BackgroundFilter (opcional para botones)
    static obs_properties_t* GetProperties(void* filter_data) noexcept;

    /// Actualiza la configuración cuando el usuario cambia parámetros
    void Update(obs_data* settings) noexcept;

    /// Tick de video (se llama cada frame, en el render thread)
    void VideoTick(float seconds) noexcept;

    /// Render de video (se llama en el render thread)
    void VideoRender() noexcept;

    /// Render de video OBS antiguo (obs_source_info → video_render)
    static void OBSVideoRender(void* data, gs_effect* effect) noexcept;

    // ── Ciclo de vida del filtro ────────────────────────────────────────

    /// Inicializa los recursos (shaders, modelo, etc.)
    bool Initialize() noexcept;

    /// Libera todos los recursos
    void Shutdown() noexcept;

    // ── Acceso a recursos ───────────────────────────────────────────────

    obs_source* GetSource() const noexcept { return source_; }
    const FilterSettings& GetSettings() const noexcept { return settings_; }
    FilterSettings& GetMutableSettings() noexcept { return settings_; }
    SegmentationEngine& GetEngine() noexcept { return *engine_; }
    BlurRenderer& GetRenderer() noexcept { return *renderer_; }

private:
    // ── Callbacks estáticos para OBS ────────────────────────────────────

    static void* OBSCreate(obs_data* settings, obs_source* source) noexcept;
    static void  OBSDestroy(void* data) noexcept;
    static void  OBSUpdate(void* data, obs_data* settings) noexcept;
    static void  OBSVideoTick(void* data, float seconds) noexcept;
    static uint32_t OBSGetWidth(void* data) noexcept;
    static uint32_t OBSGetHeight(void* data) noexcept;

    // ── Lógica de inferencia ────────────────────────────────────────────

    /// Procesa un frame (llamado desde el hilo de trabajo)
    void RunInferenceOnFrame(const uint8_t* frame_data,
                             uint32_t width, uint32_t height) noexcept;

    /// Inicializa la carga del modelo de IA
    bool LoadAIModel() noexcept;

    // ── Estado ──────────────────────────────────────────────────────────

    obs_source*       source_       = nullptr;
    bool              initialized_  = false;

    // Configuración
    FilterSettings    settings_;
    mutable std::mutex settings_mutex_;

    // Motor de IA
    std::unique_ptr<SegmentationEngine> engine_;

    // Renderizador GPU
    std::unique_ptr<BlurRenderer> renderer_;

    // ── Buffer de máscara (thread-safe) ─────────────────────────────────
    // El hilo de inferencia escribe, el hilo de render lee.
    // Usamos double-buffering para no bloquear nunca.

    std::vector<float> mask_buffer_1_;
    std::vector<float> mask_buffer_2_;
    std::atomic<float*> front_mask_{nullptr};  // Lo que lee el render
    std::atomic<float*> back_mask_{nullptr};   // Lo que escribe la inferencia
    std::mutex          mask_swap_mutex_;
    std::atomic<bool>   mask_valid_{false};
    uint32_t            mask_width_  = 0;
    uint32_t            mask_height_ = 0;

    // ── Frame buffer para copia al hilo de inferencia ───────────────────

    std::vector<uint8_t> frame_copy_buffer_;
    std::mutex           frame_copy_mutex_;
    std::atomic<bool>    frame_ready_{false};
    uint32_t             frame_width_  = 0;
    uint32_t             frame_height_ = 0;

    // ── Métricas de rendimiento ─────────────────────────────────────────

    std::atomic<double> total_frame_time_ms_{0.0};
    std::atomic<uint32_t> frame_count_{0};
};

// ═════════════════════════════════════════════════════════════════════════════
// Referencia a obs_source_info para registro en plugin-main
// ═════════════════════════════════════════════════════════════════════════════

/// Obtiene la estructura obs_source_info del filtro (para registrar en OBS)
struct obs_source_info* GetBackgroundFilterInfo() noexcept;
