// =============================================================================
// blur_renderer.cpp — Implementación del pipeline de blur en GPU
// =============================================================================

#include "blur_renderer.h"
#include "utils.h"

#include <graphics/graphics.h>
#include <graphics/effect.h>

// =============================================================================
// BlurRenderer
// =============================================================================

BlurRenderer::BlurRenderer() noexcept {
    LOG_INFO("[BlurRenderer] Instancia creada");
}

BlurRenderer::~BlurRenderer() noexcept {
    Shutdown();
}

// ── Initialize ──────────────────────────────────────────────────────────────

bool BlurRenderer::Initialize(const std::string& shaders_dir) noexcept {
    if (initialized_) {
        LOG_WARN("[BlurRenderer] Ya inicializado");
        return true;
    }

    shaders_dir_ = shaders_dir;
    LOG_INFO("[BlurRenderer] Inicializando desde: %s", shaders_dir.c_str());

    // Cargar shaders
    gaussian_shader_ = std::make_unique<GpuShader>();
    if (!gaussian_shader_->LoadFromFile(shaders_dir + "/gaussian.effect")) {
        LOG_ERROR("[BlurRenderer] Falló gaussian.effect");
        return false;
    }

    mask_shader_ = std::make_unique<GpuShader>();
    if (!mask_shader_->LoadFromFile(shaders_dir + "/mask.effect")) {
        LOG_ERROR("[BlurRenderer] Falló mask.effect");
        return false;
    }

    composite_shader_ = std::make_unique<GpuShader>();
    if (!composite_shader_->LoadFromFile(shaders_dir + "/composite.effect")) {
        LOG_ERROR("[BlurRenderer] Falló composite.effect");
        return false;
    }

    // Crear textura de máscara GPU (se actualiza dinámicamente)
    mask_texture_gpu_ = std::make_unique<GpuTexture>();

    initialized_ = true;
    LOG_INFO("[BlurRenderer] Inicialización correcta");
    return true;
}

// ── Shutdown ────────────────────────────────────────────────────────────────

void BlurRenderer::Shutdown() noexcept {
    rt_output_.reset();
    rt_mask_.reset();
    rt_blur_vertical_.reset();
    rt_blur_horizontal_.reset();
    mask_texture_gpu_.reset();
    gaussian_shader_.reset();
    mask_shader_.reset();
    composite_shader_.reset();

    initialized_ = false;
    width_ = 0;
    height_ = 0;

    LOG_INFO("[BlurRenderer] Shutdown completo");
}

// ── Render (pipeline completo) ──────────────────────────────────────────────

void BlurRenderer::Render(
    gs_texture* input_texture,
    const float* mask_cpu,
    gs_texture* mask_texture_gpu,
    uint32_t width, uint32_t height,
    const FilterSettings& settings) noexcept
{
    if (!initialized_ || !input_texture) {
        LOG_WARN("[BlurRenderer] Render llamado sin inicializar");
        return;
    }

    // Redimensionar si cambió la resolución
    if (width != width_ || height != height_) {
        if (!ResizeRenderTargets(width, height)) {
            LOG_ERROR("[BlurRenderer] Falló redimensionar a %ux%u",
                      width, height);
            return;
        }
        width_ = width;
        height_ = height;
    }

    ScopedTimer timer("BlurRenderer::Render");

    // ── Obtener textura de máscara ──────────────────────────────────────
    gs_texture* mask_tex = mask_texture_gpu;
    if (mask_cpu && !mask_texture_gpu) {
        UpdateMaskTexture(mask_cpu, width, height);
        mask_tex = mask_texture_gpu_->GetTexture();
    }

    // ── Pipeline ────────────────────────────────────────────────────────
    // 1. Blur horizontal  → rt_blur_horizontal
    RenderBlurHorizontal(input_texture, settings);

    // 2. Blur vertical     → rt_blur_vertical
    RenderBlurVertical(settings);

    // 3. Refinar máscara   → rt_mask
    if (mask_tex) {
        RenderMaskPass(input_texture, mask_tex, settings);
    }

    // 4. Composición final → rt_output
    RenderComposite(
        input_texture,
        (rt_blur_vertical_ && *rt_blur_vertical_)
            ? rt_blur_vertical_->GetTexture()
            : input_texture,
        (rt_mask_ && *rt_mask_)
            ? rt_mask_->GetTexture()
            : mask_tex,
        settings
    );
}

gs_texture* BlurRenderer::GetOutputTexture() const noexcept {
    return (rt_output_ && *rt_output_) ? rt_output_->GetTexture() : nullptr;
}

// ── Pases individuales ──────────────────────────────────────────────────────

void BlurRenderer::RenderBlurHorizontal(
    gs_texture* input_texture,
    const FilterSettings& settings) noexcept
{
    if (!gaussian_shader_ || !*gaussian_shader_ || !rt_blur_horizontal_) return;

    ScopedTimer timer("Blur H");

    // Activar render target para blur horizontal
    if (!rt_blur_horizontal_->Bind()) return;

    // Configurar parámetros
    gaussian_shader_->SetTexture("input_texture", input_texture);
    gaussian_shader_->SetFloat("blur_radius",
                               settings.BlurRadiusFloat());
    gaussian_shader_->SetFloat("blur_strength",
                               settings.BlurStrengthFactor());

    float texel_x = 1.0f / static_cast<float>(width_);
    float texel_y = 1.0f / static_cast<float>(height_);
    gaussian_shader_->SetFloat2("texel_size", texel_x, texel_y);

    // Dibujar
    gaussian_shader_->Draw("Horizontal");

    GpuRenderTarget::Unbind();
}

void BlurRenderer::RenderBlurVertical(
    const FilterSettings& settings) noexcept
{
    if (!gaussian_shader_ || !*gaussian_shader_ || !rt_blur_vertical_) return;

    ScopedTimer timer("Blur V");

    if (!rt_blur_vertical_->Bind()) return;

    // Entrada = salida del pase horizontal
    gs_texture* input_h = (rt_blur_horizontal_ && *rt_blur_horizontal_)
                          ? rt_blur_horizontal_->GetTexture()
                          : nullptr;
    if (!input_h) return;

    gaussian_shader_->SetTexture("input_texture", input_h);
    gaussian_shader_->SetFloat("blur_radius",
                               settings.BlurRadiusFloat());
    gaussian_shader_->SetFloat("blur_strength",
                               settings.BlurStrengthFactor());

    float texel_x = 1.0f / static_cast<float>(width_);
    float texel_y = 1.0f / static_cast<float>(height_);
    gaussian_shader_->SetFloat2("texel_size", texel_x, texel_y);

    gaussian_shader_->Draw("Vertical");

    GpuRenderTarget::Unbind();
}

void BlurRenderer::RenderMaskPass(
    gs_texture* input_texture,
    gs_texture* raw_mask_texture,
    const FilterSettings& settings) noexcept
{
    if (!mask_shader_ || !*mask_shader_ || !rt_mask_) return;

    ScopedTimer timer("Mask Refine");

    if (!rt_mask_->Bind()) return;

    float texel_x = 1.0f / static_cast<float>(width_);
    float texel_y = 1.0f / static_cast<float>(height_);

    mask_shader_->SetTexture("raw_mask", raw_mask_texture);
    mask_shader_->SetTexture("source_frame", input_texture);
    mask_shader_->SetFloat("mask_threshold",
                           settings.MaskThresholdFactor());
    mask_shader_->SetFloat("edge_softness",
                           settings.EdgeSoftnessFactor());
    mask_shader_->SetFloat2("texel_size", texel_x, texel_y);
    mask_shader_->SetBool("high_quality_edges",
                           settings.high_quality_edges);

    mask_shader_->Draw("Refine");

    GpuRenderTarget::Unbind();
}

void BlurRenderer::RenderComposite(
    gs_texture* original_texture,
    gs_texture* blurred_texture,
    gs_texture* mask_texture,
    const FilterSettings& settings) noexcept
{
    if (!composite_shader_ || !*composite_shader_ || !rt_output_) return;

    ScopedTimer timer("Composite");

    if (!rt_output_->Bind()) return;

    composite_shader_->SetTexture("original_frame", original_texture);
    composite_shader_->SetTexture("blurred_frame", blurred_texture);
    composite_shader_->SetTexture("mask_texture", mask_texture);
    composite_shader_->SetFloat("edge_softness",
                                settings.EdgeSoftnessFactor());
    composite_shader_->SetFloat("blur_strength_factor",
                                settings.BlurStrengthFactor());
    composite_shader_->SetBool("show_mask", settings.show_mask);

    composite_shader_->Draw();

    GpuRenderTarget::Unbind();
}

// ── Actualizar textura de máscara ───────────────────────────────────────────

bool BlurRenderer::UpdateMaskTexture(const float* mask_data,
                                      uint32_t width, uint32_t height) noexcept
{
    if (!mask_data || width == 0 || height == 0) return false;

    // Crear textura si no existe o si cambió el tamaño
    if (!*mask_texture_gpu_ ||
        mask_texture_gpu_->Width() != width ||
        mask_texture_gpu_->Height() != height) {

        mask_texture_gpu_ = std::make_unique<GpuTexture>();
        // Usamos GS_R8_UNORM para máscara de 1 canal
        // OBS no tiene GS_R8, usamos GS_A8 como alternativa
        if (!mask_texture_gpu_->Create(width, height, GS_A8, false, nullptr)) {
            LOG_ERROR("[BlurRenderer] Falló crear textura de máscara %ux%u",
                      width, height);
            return false;
        }
    }

    // Escribir datos en la textura
    // OBS usa gs_texture_set_image para datos RGBA en uint32.
    // Para datos float, necesitamos usar el enfoque de actualización directa.
    // Nota: En una implementación real, usaríamos gs_register_texture
    // con un staging buffer, o usaríamos un enfoque de "texture_from_cpu".
    //
    // Simplificación: los datos float se convierten a uint8 y se
    // escriben como canal alpha (GS_A8).
    std::vector<uint8_t> pixels(width * height);
    for (uint32_t i = 0; i < width * height; i++) {
        pixels[i] = static_cast<uint8_t>(
            MathUtils::Clamp(mask_data[i] * 255.0f, 0.0f, 255.0f));
    }

    gs_texture_set_image(mask_texture_gpu_->GetTexture(),
                         pixels.data(), width, false);

    return true;
}

// ── Redimensionar render targets ────────────────────────────────────────────

bool BlurRenderer::ResizeRenderTargets(uint32_t width, uint32_t height) noexcept {
    LOG_DEBUG("[BlurRenderer] Redimensionando a %ux%u", width, height);

    uint32_t gs_format = GS_RGBA_UNORM;  // Formato estándar de OBS

    // Crear render targets
    rt_blur_horizontal_ = std::make_unique<GpuRenderTarget>();
    if (!rt_blur_horizontal_->Create(width, height, gs_format)) {
        LOG_ERROR("[BlurRenderer] Falló RT horizontal %ux%u", width, height);
        return false;
    }

    rt_blur_vertical_ = std::make_unique<GpuRenderTarget>();
    if (!rt_blur_vertical_->Create(width, height, gs_format)) {
        LOG_ERROR("[BlurRenderer] Falló RT vertical %ux%u", width, height);
        return false;
    }

    rt_mask_ = std::make_unique<GpuRenderTarget>();
    if (!rt_mask_->Create(width, height, gs_format)) {
        LOG_ERROR("[BlurRenderer] Falló RT mask %ux%u", width, height);
        return false;
    }

    rt_output_ = std::make_unique<GpuRenderTarget>();
    if (!rt_output_->Create(width, height, gs_format)) {
        LOG_ERROR("[BlurRenderer] Falló RT output %ux%u", width, height);
        return false;
    }

    width_ = width;
    height_ = height;
    LOG_INFO("[BlurRenderer] Render targets creados: %ux%u", width, height);
    return true;
}
