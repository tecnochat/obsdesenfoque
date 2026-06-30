// =============================================================================
// gpu_shader.cpp — Implementación del sistema de shaders GPU
// =============================================================================

#include "gpu_shader.h"
#include "utils.h"

#include <graphics/effect.h>
#include <graphics/graphics.h>

// =============================================================================
// GpuShader
// =============================================================================

GpuShader::~GpuShader() noexcept {
    Release();
}

GpuShader::GpuShader(GpuShader&& other) noexcept
    : effect_(other.effect_)
    , param_cache_(std::move(other.param_cache_))
{
    other.effect_ = nullptr;
}

GpuShader& GpuShader::operator=(GpuShader&& other) noexcept {
    if (this != &other) {
        Release();
        effect_ = other.effect_;
        param_cache_ = std::move(other.param_cache_);
        other.effect_ = nullptr;
    }
    return *this;
}

bool GpuShader::LoadFromFile(const std::string& file_path) noexcept {
    Release();

    effect_ = gs_effect_create_from_file(file_path.c_str(), nullptr);
    if (!effect_) {
        LOG_ERROR("[GpuShader] Falló al cargar shader: %s", file_path.c_str());
        return false;
    }

    LOG_INFO("[GpuShader] Shader cargado: %s", file_path.c_str());
    return true;
}

bool GpuShader::LoadFromString(const std::string& shader_source,
                               const std::string& name) noexcept {
    Release();

    effect_ = gs_effect_create(shader_source.c_str(),
                               nullptr, nullptr, name.c_str());
    if (!effect_) {
        LOG_ERROR("[GpuShader] Falló al crear shader desde string: %s",
                  name.c_str());
        return false;
    }

    LOG_INFO("[GpuShader] Shader creado desde string: %s", name.c_str());
    return true;
}

void GpuShader::Release() noexcept {
    if (effect_) {
        gs_effect_destroy(effect_);
        effect_ = nullptr;
    }
    param_cache_.clear();
}

bool GpuShader::SetBool(const std::string& param_name, bool value) noexcept {
    auto* param = FindParam(param_name);
    if (!param) return false;
    gs_effect_set_bool(param, value);
    return true;
}

bool GpuShader::SetFloat(const std::string& param_name, float value) noexcept {
    auto* param = FindParam(param_name);
    if (!param) return false;
    gs_effect_set_float(param, value);
    return true;
}

bool GpuShader::SetInt(const std::string& param_name, int value) noexcept {
    auto* param = FindParam(param_name);
    if (!param) return false;
    gs_effect_set_int(param, value);
    return true;
}

bool GpuShader::SetFloat2(const std::string& param_name,
                          float x, float y) noexcept {
    auto* param = FindParam(param_name);
    if (!param) return false;
    struct { float x, y; } v = { x, y };
    gs_effect_set_vec2(param, &v);
    return true;
}

bool GpuShader::SetFloat4(const std::string& param_name,
                          float x, float y, float z, float w) noexcept {
    auto* param = FindParam(param_name);
    if (!param) return false;
    struct { float x, y, z, w; } v = { x, y, z, w };
    gs_effect_set_vec4(param, &v);
    return true;
}

bool GpuShader::SetFloat4x4(const std::string& param_name,
                            const float* matrix_16) noexcept {
    auto* param = FindParam(param_name);
    if (!param) return false;
    gs_effect_set_matrix4(param, reinterpret_cast<const matrix4*>(matrix_16));
    return true;
}

bool GpuShader::SetTexture(const std::string& param_name,
                           gs_texture* texture) noexcept {
    auto* param = FindParam(param_name);
    if (!param) return false;
    gs_effect_set_texture(param, texture);
    return true;
}

void GpuShader::Draw(const char* technique_name) noexcept {
    if (!effect_) return;
    while (gs_effect_loop(effect_, technique_name)) {
        gs_draw_sprite(nullptr, 0, 0, 0);
    }
}

gs_effect_param* GpuShader::FindParam(const std::string& name) noexcept {
    if (!effect_) return nullptr;

    // Buscar en caché primero
    auto it = param_cache_.find(name);
    if (it != param_cache_.end()) {
        return it->second;
    }

    // Buscar en OBS y cachear
    auto* param = gs_effect_get_param_by_name(effect_, name.c_str());
    if (param) {
        param_cache_[name] = param;
    } else {
        LOG_WARN("[GpuShader] Parámetro no encontrado: %s", name.c_str());
    }

    return param;
}

// =============================================================================
// GpuTexture
// =============================================================================

GpuTexture::~GpuTexture() noexcept {
    Release();
}

GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : texture_(other.texture_)
    , width_(other.width_)
    , height_(other.height_)
{
    other.texture_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

GpuTexture& GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        Release();
        texture_ = other.texture_;
        width_ = other.width_;
        height_ = other.height_;
        other.texture_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

bool GpuTexture::Create(uint32_t width, uint32_t height,
                        uint32_t gs_color_format, bool is_render_target,
                        const void* initial_data) noexcept {
    Release();

    uint32_t flags = GS_DYNAMIC;
    if (is_render_target) {
        flags |= GS_RENDER_TARGET;
    }

    texture_ = gs_texture_create(width, height, gs_color_format,
                                 1, nullptr, flags);
    if (!texture_) {
        LOG_ERROR("[GpuTexture] Falló creación: %ux%u fmt=%u",
                  width, height, gs_color_format);
        return false;
    }

    if (initial_data && !is_render_target) {
        gs_texture_set_image(texture_, initial_data, width * 4, false);
    }

    width_ = width;
    height_ = height;
    LOG_DEBUG("[GpuTexture] Creada: %ux%u fmt=%u", width, height, gs_color_format);
    return true;
}

bool GpuTexture::Resize(uint32_t width, uint32_t height) noexcept {
    if (width_ == width && height_ == height && texture_) {
        return true;  // Ya tiene el tamaño correcto
    }
    // No conservamos el formato anterior así que necesitamos que el
    // llamador vuelva a crear. Esta función es un helper conceptual.
    LOG_WARN("[GpuTexture] Resize no implementado directamente — recrear");
    return false;
}

void GpuTexture::Release() noexcept {
    if (texture_) {
        gs_texture_destroy(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

// =============================================================================
// GpuSampler
// =============================================================================

GpuSampler::~GpuSampler() noexcept {
    Release();
}

GpuSampler::GpuSampler(GpuSampler&& other) noexcept
    : sampler_(other.sampler_)
{
    other.sampler_ = nullptr;
}

GpuSampler& GpuSampler::operator=(GpuSampler&& other) noexcept {
    if (this != &other) {
        Release();
        sampler_ = other.sampler_;
        other.sampler_ = nullptr;
    }
    return *this;
}

bool GpuSampler::Create(int filter, int address_u, int address_v) noexcept {
    Release();

    gs_sampler_info info = {};
    info.max_anisotropy = 1;

    switch (filter) {
        case 0:  info.filter = GS_FILTER_POINT;      break;
        case 1:  info.filter = GS_FILTER_LINEAR;     break;
        case 2:  info.filter = GS_FILTER_ANISOTROPIC; break;
        default: info.filter = GS_FILTER_LINEAR;      break;
    }

    auto addr_mode = [](int mode) -> gs_address_mode {
        switch (mode) {
            case 0:  return GS_ADDRESS_CLAMP;
            case 1:  return GS_ADDRESS_WRAP;
            case 2:  return GS_ADDRESS_MIRROR;
            case 3:  return GS_ADDRESS_BORDER;
            default: return GS_ADDRESS_CLAMP;
        }
    };

    info.address_u = addr_mode(address_u);
    info.address_v = addr_mode(address_v);
    info.address_w = GS_ADDRESS_CLAMP;

    sampler_ = gs_samplerstate_create(&info);
    if (!sampler_) {
        LOG_ERROR("[GpuSampler] Falló creación de sampler state");
        return false;
    }

    return true;
}

void GpuSampler::Release() noexcept {
    if (sampler_) {
        gs_samplerstate_destroy(sampler_);
        sampler_ = nullptr;
    }
}

// =============================================================================
// GpuRenderTarget
// =============================================================================

GpuRenderTarget::~GpuRenderTarget() noexcept {
    Release();
}

GpuRenderTarget::GpuRenderTarget(GpuRenderTarget&& other) noexcept
    : target_(other.target_)
    , texture_(std::move(other.texture_))
    , width_(other.width_)
    , height_(other.height_)
{
    other.target_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

GpuRenderTarget& GpuRenderTarget::operator=(GpuRenderTarget&& other) noexcept {
    if (this != &other) {
        Release();
        target_ = other.target_;
        texture_ = std::move(other.texture_);
        width_ = other.width_;
        height_ = other.height_;
        other.target_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

bool GpuRenderTarget::Create(uint32_t width, uint32_t height,
                             uint32_t gs_color_format) noexcept {
    Release();

    // Crear la textura como render target primero
    if (!texture_.Create(width, height, gs_color_format, true, nullptr)) {
        LOG_ERROR("[GpuRenderTarget] Falló creación de textura");
        return false;
    }

    // Crear el render target de OBS a partir de la textura
    target_ = gs_render_target_create(width, height, gs_color_format, 0);
    if (!target_) {
        LOG_ERROR("[GpuRenderTarget] Falló creación de render target OBS");
        texture_.Release();
        return false;
    }

    width_ = width;
    height_ = height;
    LOG_DEBUG("[GpuRenderTarget] Creado: %ux%u", width, height);
    return true;
}

bool GpuRenderTarget::Bind() noexcept {
    if (!target_) return false;
    gs_set_render_target(target_, nullptr);
    return true;
}

void GpuRenderTarget::Unbind() noexcept {
    gs_set_render_target(nullptr, nullptr);
}

gs_texture* GpuRenderTarget::GetTexture() const noexcept {
    return texture_.GetTexture();
}

void GpuRenderTarget::Release() noexcept {
    if (target_) {
        gs_render_target_destroy(target_);
        target_ = nullptr;
    }
    texture_.Release();
    width_ = 0;
    height_ = 0;
}
