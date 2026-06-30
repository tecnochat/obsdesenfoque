// =============================================================================
// gpu_shader.h — Gestión de shaders GPU para el pipeline de blur
//
// Proporciona una capa de abstracción sobre OBS Graphics API para:
//   - Cargar y gestionar .effect shaders
//   - Crear y gestionar texturas GPU, render targets, samplers
//   - Ejecutar pases de render con parámetros tipados
//
// Todo el blur se ejecuta mediante shaders — nunca OpenCV ni CPU.
// =============================================================================

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

// ── Forward declarations de OBS ─────────────────────────────────────────────
// (evitamos incluir obs.h aquí para mantener separación)
struct gs_effect;
struct gs_effect_param;
struct gs_texture;
struct gs_sampler_state;

// ═════════════════════════════════════════════════════════════════════════════
// GpuShader
// ═════════════════════════════════════════════════════════════════════════════
// Carga un archivo .effect de OBS y proporciona acceso tipado a sus
// parámetros y técnicas.

class GpuShader {
public:
    GpuShader() noexcept = default;
    ~GpuShader() noexcept;

    // No copiable
    GpuShader(const GpuShader&) = delete;
    GpuShader& operator=(const GpuShader&) = delete;

    // Movible
    GpuShader(GpuShader&& other) noexcept;
    GpuShader& operator=(GpuShader&& other) noexcept;

    /// Carga un archivo .effect desde el sistema de archivos.
    /// @param file_path Ruta al archivo .effect
    /// @return true si se cargó correctamente
    bool LoadFromFile(const std::string& file_path) noexcept;

    /// Carga un shader desde una cadena (útil para shaders embebidos)
    bool LoadFromString(const std::string& shader_source,
                        const std::string& name) noexcept;

    /// Verifica si el shader se cargó correctamente
    bool IsValid() const noexcept { return effect_ != nullptr; }

    /// Libera todos los recursos
    void Release() noexcept;

    // ── Setters de parámetros ───────────────────────────────────────────

    bool SetBool(const std::string& param_name, bool value) noexcept;
    bool SetFloat(const std::string& param_name, float value) noexcept;
    bool SetInt(const std::string& param_name, int value) noexcept;

    bool SetFloat2(const std::string& param_name,
                   float x, float y) noexcept;
    bool SetFloat4(const std::string& param_name,
                   float x, float y, float z, float w) noexcept;
    bool SetFloat4x4(const std::string& param_name,
                     const float* matrix_16) noexcept;

    bool SetTexture(const std::string& param_name,
                    gs_texture* texture) noexcept;

    // ── Render ──────────────────────────────────────────────────────────

    /// Dibuja un quad con la técnica especificada
    /// @param technique_name Nombre de la técnica ("Draw", "Horizontal", etc.)
    ///        nullptr usa la primera técnica disponible
    void Draw(const char* technique_name = nullptr) noexcept;

    /// Obtiene el puntero OBS interno (para uso avanzado)
    gs_effect* GetEffect() noexcept { return effect_; }

private:
    /// Busca un parámetro por nombre (con caché)
    gs_effect_param* FindParam(const std::string& name) noexcept;

    gs_effect* effect_ = nullptr;
    std::unordered_map<std::string, gs_effect_param*> param_cache_;
};

// ═════════════════════════════════════════════════════════════════════════════
// GpuTexture
// ═════════════════════════════════════════════════════════════════════════════
// RAII wrapper para texturas GPU (render targets y textures de entrada).

class GpuTexture {
public:
    GpuTexture() noexcept = default;
    ~GpuTexture() noexcept;

    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    GpuTexture(GpuTexture&& other) noexcept;
    GpuTexture& operator=(GpuTexture&& other) noexcept;

    /// Crea una textura 2D (render target o source)
    bool Create(uint32_t width, uint32_t height,
                uint32_t gs_color_format, bool is_render_target,
                const void* initial_data = nullptr) noexcept;

    /// Redimensiona la textura (útil cuando cambia la resolución de entrada)
    bool Resize(uint32_t width, uint32_t height) noexcept;

    /// Obtiene el puntero OBS interno
    gs_texture* GetTexture() const noexcept { return texture_; }

    /// Obtiene las dimensiones
    uint32_t Width()  const noexcept { return width_; }
    uint32_t Height() const noexcept { return height_; }

    /// Libera los recursos
    void Release() noexcept;

    /// Verifica si la textura es válida
    explicit operator bool() const noexcept { return texture_ != nullptr; }

private:
    gs_texture* texture_ = nullptr;
    uint32_t width_  = 0;
    uint32_t height_ = 0;
};

// ═════════════════════════════════════════════════════════════════════════════
// GpuSampler
// ═════════════════════════════════════════════════════════════════════════════
// RAII wrapper para samplers de textura.

class GpuSampler {
public:
    GpuSampler() noexcept = default;
    ~GpuSampler() noexcept;

    GpuSampler(const GpuSampler&) = delete;
    GpuSampler& operator=(const GpuSampler&) = delete;

    GpuSampler(GpuSampler&& other) noexcept;
    GpuSampler& operator=(GpuSampler&& other) noexcept;

    /// Crea un sampler state con los parámetros especificados
    /// @param filter 0=Punto, 1=Lineal, 2=Anisotrópico
    /// @param address_u 0=Clamp, 1=Wrap, 2=Mirror, 3=Border
    /// @param address_v igual que address_u
    bool Create(int filter = 1, int address_u = 0,
                int address_v = 0) noexcept;

    gs_sampler_state* GetSampler() const noexcept { return sampler_; }

    void Release() noexcept;
    explicit operator bool() const noexcept { return sampler_ != nullptr; }

private:
    gs_sampler_state* sampler_ = nullptr;
};

// ═════════════════════════════════════════════════════════════════════════════
// GpuRenderTarget
// ═════════════════════════════════════════════════════════════════════════════
// RAII wrapper para render targets con swap gestionado.
// Se usa para los pases intermedios del blur.

class GpuRenderTarget {
public:
    GpuRenderTarget() noexcept = default;
    ~GpuRenderTarget() noexcept;

    GpuRenderTarget(const GpuRenderTarget&) = delete;
    GpuRenderTarget& operator=(const GpuRenderTarget&) = delete;

    GpuRenderTarget(GpuRenderTarget&& other) noexcept;
    GpuRenderTarget& operator=(GpuRenderTarget&& other) noexcept;

    /// Crea un render target con textura asociada
    bool Create(uint32_t width, uint32_t height,
                uint32_t gs_color_format) noexcept;

    /// Activa este render target para dibujar en él
    bool Bind() noexcept;

    /// Desactiva (restaura el target por defecto)
    static void Unbind() noexcept;

    /// Obtiene la textura asociada (para usarla como input de shaders)
    gs_texture* GetTexture() const noexcept;

    uint32_t Width()  const noexcept { return width_; }
    uint32_t Height() const noexcept { return height_; }

    void Release() noexcept;
    explicit operator bool() const noexcept { return target_ != nullptr; }

private:
    void*    target_ = nullptr;  // gs_render_target* (opaco por diseño)
    GpuTexture texture_;
    uint32_t width_  = 0;
    uint32_t height_ = 0;
};
