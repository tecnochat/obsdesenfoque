// =============================================================================
// blur_renderer.h — Renderizador del pipeline de blur en GPU
//
// Orquesta los shaders GPU para ejecutar el pipeline completo:
//   1. Refinamiento de máscara (mask.effect)
//   2. Gaussian blur horizontal (gaussian.effect — técnica "Horizontal")
//   3. Gaussian blur vertical   (gaussian.effect — técnica "Vertical")
//   4. Composición final        (composite.effect)
//
// Todo el blur se ejecuta mediante shaders — nunca OpenCV ni CPU.
// =============================================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "gpu_shader.h"     // GpuShader, GpuTexture, GpuRenderTarget
#include "settings.h"       // FilterSettings

// ═════════════════════════════════════════════════════════════════════════════
// BlurRenderer
// ═════════════════════════════════════════════════════════════════════════════

class BlurRenderer {
public:
    BlurRenderer() noexcept;
    ~BlurRenderer() noexcept;

    // No copiable ni movible
    BlurRenderer(const BlurRenderer&) = delete;
    BlurRenderer& operator=(const BlurRenderer&) = delete;

    // ── Ciclo de vida ───────────────────────────────────────────────────

    /// Inicializa el renderizador (carga shaders, crea recursos).
    /// @param shaders_dir Directorio donde están los .effect
    /// @return true si se inicializó correctamente
    bool Initialize(const std::string& shaders_dir) noexcept;

    /// Libera todos los recursos GPU
    void Shutdown() noexcept;

    /// Verifica si está listo para renderizar
    bool IsReady() const noexcept {
        return gaussian_shader_ && composite_shader_ && mask_shader_;
    }

    // ── Render ──────────────────────────────────────────────────────────

    /// Ejecuta el pipeline de blur completo.
    ///
    /// @param input_texture Textura OBS de entrada (frame original)
    /// @param mask_cpu       Máscara CPU (float[width*height]) o nullptr
    ///                       si se usa mask_texture_gpu
    /// @param mask_texture_gpu Textura GPU de máscara (opcional)
    /// @param width          Ancho del frame
    /// @param height         Alto del frame
    /// @param settings       Configuración actual del filtro
    void Render(gs_texture* input_texture,
                const float* mask_cpu,
                gs_texture* mask_texture_gpu,
                uint32_t width,
                uint32_t height,
                const FilterSettings& settings) noexcept;

    /// Obtiene el resultado del renderizado (llamar después de Render)
    gs_texture* GetOutputTexture() const noexcept;

    // ── Gestión de recursos ─────────────────────────────────────────────

    /// Actualiza la textura GPU de máscara a partir de datos CPU
    /// @return true si se actualizó correctamente
    bool UpdateMaskTexture(const float* mask_data,
                           uint32_t width, uint32_t height) noexcept;

    /// Redimensiona los render targets internos (cuando cambia la resolución)
    bool ResizeRenderTargets(uint32_t width, uint32_t height) noexcept;

private:
    // ── Pipeline de render ──────────────────────────────────────────────

    /// Paso 1: Refinar máscara (threshold + bordes)
    void RenderMaskPass(gs_texture* input_texture,
                        gs_texture* raw_mask_texture,
                        const FilterSettings& settings) noexcept;

    /// Paso 2: Gaussian blur horizontal
    void RenderBlurHorizontal(gs_texture* input_texture,
                              const FilterSettings& settings) noexcept;

    /// Paso 3: Gaussian blur vertical
    void RenderBlurVertical(const FilterSettings& settings) noexcept;

    /// Paso 4: Composición final
    void RenderComposite(gs_texture* original_texture,
                         gs_texture* blurred_texture,
                         gs_texture* mask_texture,
                         const FilterSettings& settings) noexcept;

    // ── Shaders ─────────────────────────────────────────────────────────

    std::unique_ptr<GpuShader> gaussian_shader_;   // gaussian.effect
    std::unique_ptr<GpuShader> mask_shader_;        // mask.effect
    std::unique_ptr<GpuShader> composite_shader_;   // composite.effect

    // ── Render targets (pases intermedios) ──────────────────────────────

    std::unique_ptr<GpuRenderTarget> rt_blur_horizontal_;  // Salida blur H
    std::unique_ptr<GpuRenderTarget> rt_blur_vertical_;    // Salida blur V
    std::unique_ptr<GpuRenderTarget> rt_mask_;             // Máscara refinada
    std::unique_ptr<GpuRenderTarget> rt_output_;           // Salida final

    // ── Textura GPU para la máscara ─────────────────────────────────────

    std::unique_ptr<GpuTexture> mask_texture_gpu_;

    // ── Dimensiones actuales ────────────────────────────────────────────

    uint32_t width_  = 0;
    uint32_t height_ = 0;

    // ── Estado de inicialización ────────────────────────────────────────

    bool initialized_ = false;
    std::string shaders_dir_;
};

// ═════════════════════════════════════════════════════════════════════════════
// PipelineMetrics
// ═════════════════════════════════════════════════════════════════════════════
// Métricas de rendimiento del pipeline de render.

struct PipelineMetrics {
    double blur_time_ms    = 0.0;  // Tiempo del blur (H+V)
    double composite_time_ms = 0.0; // Tiempo de composición
    double mask_time_ms    = 0.0;  // Tiempo de procesamiento de máscara
    double total_time_ms   = 0.0;  // Tiempo total del pipeline
};
