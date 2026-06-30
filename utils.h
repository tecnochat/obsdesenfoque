// =============================================================================
// utils.h — Utilidades generales del plugin
//
// Proporciona herramientas transversales: logging, medición de tiempo,
// wrappers RAII para COM, y funciones matemáticas auxiliares.
// =============================================================================

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

// ── Logging ─────────────────────────────────────────────────────────────────
// Envuelve blog() de OBS para logging estructurado por niveles.
// En debug imprime archivo:línea.

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

void Log(LogLevel level, const char* format, ...);

#define LOG_DEBUG(...)   Log(LogLevel::Debug,   __VA_ARGS__)
#define LOG_INFO(...)    Log(LogLevel::Info,    __VA_ARGS__)
#define LOG_WARN(...)    Log(LogLevel::Warning, __VA_ARGS__)
#define LOG_ERROR(...)   Log(LogLevel::Error,   __VA_ARGS__)

// ── Timer de alto rendimiento ───────────────────────────────────────────────
// Mide tiempos de inferencia y blur para verificar las metas de rendimiento.
// Uso:
//   ScopedTimer t("Inferencia");  // Imprime al salir del scope

class ScopedTimer {
public:
    explicit ScopedTimer(const char* label) noexcept;
    ~ScopedTimer() noexcept;

    // No copiable ni movible
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    double elapsed_ms() const noexcept;
    void reset() noexcept;

private:
    const char* label_;
    std::chrono::high_resolution_clock::time_point start_;
};

// ── RAII para objetos COM (DirectX) ─────────────────────────────────────────
// Release automático en destructor. Útil para texturas D3D11, buffers, etc.

template<typename T>
class ComPtr {
public:
    ComPtr() noexcept : ptr_(nullptr) {}
    explicit ComPtr(T* ptr) noexcept : ptr_(ptr) { AddRef(); }

    ~ComPtr() noexcept { Release(); }

    ComPtr(const ComPtr& other) noexcept : ptr_(other.ptr_) { AddRef(); }
    ComPtr& operator=(const ComPtr& other) noexcept {
        if (this != &other) {
            Release();
            ptr_ = other.ptr_;
            AddRef();
        }
        return *this;
    }

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* Get() const noexcept { return ptr_; }

    T** ReleaseAndGetAddressOf() noexcept {
        Release();
        return &ptr_;
    }

    void** ReleaseAndGetVoidAddressOf() noexcept {
        Release();
        return reinterpret_cast<void**>(&ptr_);
    }

    void Reset(T* ptr = nullptr) noexcept {
        if (ptr_ != ptr) {
            Release();
            ptr_ = ptr;
            if (ptr_) ptr_->AddRef();
        }
    }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }

private:
    void AddRef() noexcept {
        if (ptr_) ptr_->AddRef();
    }

    void Release() noexcept {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    T* ptr_;
};

// ── Funciones matemáticas ────────────────────────────────────────────────────

namespace MathUtils {

/// Clamp: restringe un valor al rango [lo, hi]
template<typename T>
constexpr T Clamp(T value, T lo, T hi) noexcept {
    return (value < lo) ? lo : (value > hi) ? hi : value;
}

/// Lerp: interpolación lineal entre a y b por factor t [0, 1]
constexpr float Lerp(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

/// Normaliza un valor del rango [lo, hi] a [0, 1]
constexpr float Normalize(float value, float lo, float hi) noexcept {
    return (hi == lo) ? 0.0f : (value - lo) / (hi - lo);
}

/// Mapea un valor de un rango a otro
constexpr float Map(float value, float in_lo, float in_hi,
                    float out_lo, float out_hi) noexcept {
    return Lerp(out_lo, out_hi, Normalize(value, in_lo, in_hi));
}

/// Siguiente potencia de 2
inline uint32_t NextPowerOf2(uint32_t v) noexcept {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

/// Alinear al alza (ej: AlignUp(123, 16) = 128)
constexpr uint32_t AlignUp(uint32_t value, uint32_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace MathUtils

// ── Conversión de formatos ──────────────────────────────────────────────────

namespace FormatUtils {

/// Obtiene el nombre de un formato DXGI como string (para debug)
const char* DxgiFormatToString(int dxgi_format) noexcept;

/// Calcula el tamaño en bytes de un frame raw (sin compresión)
uint32_t FrameSizeBytes(uint32_t width, uint32_t height,
                        uint32_t bytes_per_pixel) noexcept;

} // namespace FormatUtils
