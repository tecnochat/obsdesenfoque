# ==============================================================================
# CMakeLists.txt — OBS AI Background Blur Plugin
#
# Plugin nativo para OBS Studio que agrega un filtro de difuminado de fondo
# en tiempo real mediante segmentación por IA (ONNX Runtime + GPU Shaders).
#
# Lenguaje: C++20
# Dependencias: OBS Studio, ONNX Runtime, DirectX 11
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(obs-ai-background-blur
    VERSION 1.0.0
    DESCRIPTION "AI Background Blur filter for OBS Studio"
    LANGUAGES CXX
)

# ── Configuración del lenguaje ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Opciones ────────────────────────────────────────────────────────────────
option(ENABLE_DIRECTML "Enable DirectML execution provider for ONNX" ON)
option(ENABLE_CUDA     "Enable CUDA execution provider for ONNX"    OFF)

# ── Estilo de advertencias ──────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ==============================================================================
# Dependencias
# ==============================================================================

# ── OBS Studio ───────────────────────────────────────────────────────────────
# Requiere OBS Studio instalado. OBS provee sus propios targets CMake.
# Instaladores en: https://obsproject.com/download
find_package(OBS REQUIRED)
message(STATUS "OBS encontrado: ${OBS_INCLUDE_DIR}")

# ── ONNX Runtime ─────────────────────────────────────────────────────────────
# Busca ONNX Runtime. Soporta:
#   1. find_package(onnxruntime) estándar (vcpkg, Conan, instalación manual)
#   2. Variable de entorno ONNX_RUNTIME_DIR
#   3. Flag -DONNX_RUNTIME_DIR=... en línea de comandos
#
# Descarga precompilada: https://github.com/microsoft/onnxruntime/releases
# Versión recomendada: ≥ 1.15
if(NOT onnxruntime_FOUND)
    if(DEFINED ONNX_RUNTIME_DIR)
        # Búsqueda por ruta explícita
        list(APPEND CMAKE_PREFIX_PATH "${ONNX_RUNTIME_DIR}")
    endif()
    if(DEFINED ENV{ONNX_RUNTIME_DIR})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{ONNX_RUNTIME_DIR}")
    endif()
    find_package(onnxruntime QUIET)
endif()

if(NOT onnxruntime_FOUND)
    # Fallback: búsqueda manual de cabeceras y librerías
    find_path(ONNX_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        PATHS "${ONNX_RUNTIME_DIR}/include"
              "$ENV{ONNX_RUNTIME_DIR}/include"
    )

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ONNX_ARCH_PATH "lib/x64")
    else()
        set(ONNX_ARCH_PATH "lib/x86")
    endif()

    find_library(ONNX_LIBRARY
        NAMES onnxruntime
        PATHS "${ONNX_RUNTIME_DIR}/${ONNX_ARCH_PATH}"
              "${ONNX_RUNTIME_DIR}/lib"
              "$ENV{ONNX_RUNTIME_DIR}/${ONNX_ARCH_PATH}"
              "$ENV{ONNX_RUNTIME_DIR}/lib"
    )

    if(ONNX_INCLUDE_DIR AND ONNX_LIBRARY)
        add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${ONNX_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNX_INCLUDE_DIR}"
        )
        message(STATUS "ONNX Runtime encontrado manualmente: ${ONNX_LIBRARY}")
    else()
        message(FATAL_ERROR
            "ONNX Runtime no encontrado. "
            "Descárgalo desde https://github.com/microsoft/onnxruntime/releases "
            "y señálalo con -DONNX_RUNTIME_DIR=<ruta>"
        )
    endif()
endif()

# ── DirectX 11 (Windows SDK) ────────────────────────────────────────────────
if(WIN32)
    # El Windows SDK incluye las cabeceras de D3D11, D3DCompiler, etc.
    # Se usa find_path para verificar disponibilidad
    find_path(D3D11_INCLUDE_DIR d3d11.h)
    if(NOT D3D11_INCLUDE_DIR)
        message(FATAL_ERROR "DirectX 11 SDK no encontrado. "
            "Instala el Windows SDK desde https://developer.microsoft.com/windows/downloads/windows-sdk/"
        )
    endif()
    message(STATUS "DirectX 11 SDK encontrado: ${D3D11_INCLUDE_DIR}")
endif()

# ==============================================================================
# Fuentes del proyecto
# ==============================================================================

set(PLUGIN_SOURCES
    plugin-main.cpp
    plugin-main.h
    background_filter.cpp
    background_filter.h
    segmentation_engine.cpp
    segmentation_engine.h
    blur_renderer.cpp
    blur_renderer.h
    gpu_shader.cpp
    gpu_shader.h
    settings.cpp
    settings.h
    utils.cpp
    utils.h
)

# ── Shaders (se copian como recursos para carga en runtime) ──────────────────
file(GLOB SHADER_FILES
    shaders/*.effect
    shaders/*.hlsl
    shaders/*.glsl
)

# ── Archivos de recursos (iconos, traducciones) ──────────────────────────────
file(GLOB RESOURCE_FILES
    resources/icons/*
    resources/locale/*
)

# ==============================================================================
# Build del plugin
# ==============================================================================

# Los plugins de OBS se compilan como MODULE (librería dinámica)
add_library(obs-ai-background-blur MODULE ${PLUGIN_SOURCES})

target_include_directories(obs-ai-background-blur
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${D3D11_INCLUDE_DIR}
)

target_link_libraries(obs-ai-background-blur
    PRIVATE
        OBS::libobs
        onnxruntime::onnxruntime
        d3d11
        d3dcompiler
)

# ── Definiciones de compilación ─────────────────────────────────────────────
target_compile_definitions(obs-ai-background-blur PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    PLUGIN_VERSION="${PROJECT_VERSION}"
)

if(ENABLE_DIRECTML)
    target_compile_definitions(obs-ai-background-blur PRIVATE HAVE_DIRECTML=1)
endif()

if(ENABLE_CUDA)
    target_compile_definitions(obs-ai-background-blur PRIVATE HAVE_CUDA=1)
endif()

# ── Propiedades de salida ────────────────────────────────────────────────────
set_target_properties(obs-ai-background-blur PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

# ── Copiar shaders al directorio de salida (junto al .dll) ──────────────────
foreach(SHADER ${SHADER_FILES})
    configure_file(
        "${SHADER}"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}"
    )
endforeach()

# ==============================================================================
# Instalación
# ==============================================================================

# Ruta de instalación por defecto para OBS Studio en Windows
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OBS_PLUGIN_DIR "$ENV{ProgramFiles}/obs-studio/obs-plugins/64bit")
    else()
        set(OBS_PLUGIN_DIR "$ENV{ProgramFiles(x86)}/obs-studio/obs-plugins/32bit")
    endif()
endif()

install(TARGETS obs-ai-background-blur
    LIBRARY DESTINATION "${OBS_PLUGIN_DIR}"
    RUNTIME DESTINATION "${OBS_PLUGIN_DIR}"
)

# Instalar shaders junto al plugin
install(
    DIRECTORY shaders/
    DESTINATION "${OBS_PLUGIN_DIR}/shaders"
    FILES_MATCHING PATTERN "*.effect"
)

# Instalar modelos ONNX
install(
    DIRECTORY models/
    DESTINATION "${OBS_PLUGIN_DIR}/models"
    FILES_MATCHING PATTERN "*.onnx"
)

# Instalar recursos
install(
    DIRECTORY resources/
    DESTINATION "${OBS_PLUGIN_DIR}/resources"
)

# ==============================================================================
# Targets auxiliares
# ==============================================================================

add_custom_target(run-obs
    COMMAND "${OBS_PLUGIN_DIR}/../obs64.exe"
    DEPENDS obs-ai-background-blur
    COMMENT "Ejecutando OBS Studio con el plugin compilado..."
)

# ==============================================================================
# Fin del archivo
# ==============================================================================
