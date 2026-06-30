// =============================================================================
// utils.cpp — Implementación de utilidades generales
// =============================================================================

#include "utils.h"

#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// ── OBS Logging ─────────────────────────────────────────────────────────────
// NOTA: Este archivo se compila dentro del plugin de OBS, donde blog() y
// obs_log_viewer están disponibles desde libobs. La implementación concreta
// usa blog() en el contexto de OBS; si se compila fuera (tests), se define
// una alternativa.

#ifdef OBS_PLUGIN_BUILD
#include <obs-module.h>

void Log(LogLevel level, const char* format, ...) {
    int obs_level = LOG_INFO;
    switch (level) {
        case LogLevel::Debug:   obs_level = LOG_DEBUG;   break;
        case LogLevel::Info:    obs_level = LOG_INFO;    break;
        case LogLevel::Warning: obs_level = LOG_WARNING; break;
        case LogLevel::Error:   obs_level = LOG_ERROR;   break;
    }

    va_list args;
    va_start(args, format);
    blogva(obs_level, format, args);
    va_end(args);
}

#else
// Stub para compilación fuera de OBS (tests unitarios)
#include <iostream>
void Log(LogLevel level, const char* format, ...) {
    const char* prefix = "";
    switch (level) {
        case LogLevel::Debug:   prefix = "[DEBUG] ";   break;
        case LogLevel::Info:    prefix = "[INFO] ";    break;
        case LogLevel::Warning: prefix = "[WARN] ";    break;
        case LogLevel::Error:   prefix = "[ERROR] ";   break;
    }

    va_list args;
    va_start(args, format);
    std::vprintf((std::string(prefix) + format + "\n").c_str(), args);
    va_end(args);
}
#endif

// ── ScopedTimer ──────────────────────────────────────────────────────────────

ScopedTimer::ScopedTimer(const char* label) noexcept
    : label_(label)
    , start_(std::chrono::high_resolution_clock::now())
{
}

ScopedTimer::~ScopedTimer() noexcept {
    const auto end = std::chrono::high_resolution_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - start_).count();
    LOG_DEBUG("[Timer] %s: %.3f ms", label_, ms);
}

double ScopedTimer::elapsed_ms() const noexcept {
    const auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(now - start_).count();
}

void ScopedTimer::reset() noexcept {
    start_ = std::chrono::high_resolution_clock::now();
}

// ── FormatUtils ─────────────────────────────────────────────────────────────

namespace FormatUtils {

const char* DxgiFormatToString(int dxgi_format) noexcept {
    switch (dxgi_format) {
        case 0:  return "DXGI_FORMAT_UNKNOWN";
        case 1:  return "DXGI_FORMAT_R32G32B32A32_TYPELESS";
        case 2:  return "DXGI_FORMAT_R32G32B32A32_FLOAT";
        case 3:  return "DXGI_FORMAT_R32G32B32A32_UINT";
        case 4:  return "DXGI_FORMAT_R32G32B32A32_SINT";
        case 10: return "DXGI_FORMAT_R16G16B16A16_TYPELESS";
        case 11: return "DXGI_FORMAT_R16G16B16A16_FLOAT";
        case 12: return "DXGI_FORMAT_R16G16B16A16_UNORM";
        case 13: return "DXGI_FORMAT_R16G16B16A16_UINT";
        case 14: return "DXGI_FORMAT_R16G16B16A16_SNORM";
        case 15: return "DXGI_FORMAT_R16G16B16A16_SINT";
        case 28: return "DXGI_FORMAT_R8G8B8A8_TYPELESS";
        case 29: return "DXGI_FORMAT_R8G8B8A8_UNORM";
        case 30: return "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB";
        case 31: return "DXGI_FORMAT_R8G8B8A8_UINT";
        case 32: return "DXGI_FORMAT_R8G8B8A8_SNORM";
        case 33: return "DXGI_FORMAT_R8G8B8A8_SINT";
        default: return "DXGI_FORMAT_OTHER";
    }
}

uint32_t FrameSizeBytes(uint32_t width, uint32_t height,
                        uint32_t bytes_per_pixel) noexcept {
    return width * height * bytes_per_pixel;
}

} // namespace FormatUtils
