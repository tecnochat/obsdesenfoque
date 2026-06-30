// =============================================================================
// plugin-main.cpp — Punto de entrada del plugin OBS Studio
//
// Implementa las funciones obs_module_* requeridas por OBS para cargar
// y registrar el filtro AI Background Blur en el sistema de plugins.
// =============================================================================

#include "plugin-main.h"
#include "background_filter.h"
#include "utils.h"

#include <obs-module.h>
#include <string>

// =============================================================================
// Información de versión para OBS
// =============================================================================

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "locale")

// =============================================================================
// Metadatos del módulo
// =============================================================================

MODULE_EXPORT const char* obs_module_name() noexcept {
    return PLUGIN_NAME;
}

MODULE_EXPORT const char* obs_module_description() noexcept {
    return PLUGIN_DESCRIPTION;
}

// =============================================================================
// obs_module_ver — Versión mínima de OBS requerida y versión del plugin
// =============================================================================

MODULE_EXPORT uint32_t obs_module_ver() noexcept {
    // Requiere OBS 30.0 o superior
    return MAKE_SEMANTIC_VERSION(30, 0, 0);
}

// =============================================================================
// obs_module_load — Se llama cuando OBS carga el plugin
// =============================================================================

MODULE_EXPORT bool obs_module_load() noexcept {
    LOG_INFO("=== %s v%s cargando ===", PLUGIN_NAME, PLUGIN_VERSION);
    LOG_INFO("Autor: %s", PLUGIN_AUTHOR);

    // Registrar el filtro en OBS
    struct obs_source_info* filter_info = GetBackgroundFilterInfo();
    obs_register_source(filter_info);

    LOG_INFO("Filtro registrado: '%s' (ID: %s)",
             filter_info->get_name(nullptr),
             filter_info->id);

    // Informar sobre estados de los modelos
    for (int i = 0; i < 3; i++) {
        auto model = static_cast<AIModelType>(i);
        bool exists = ModelPathUtils::ModelFileExists(model);
        LOG_INFO("  Modelo %s: %s",
                 ModelPathUtils::GetModelDisplayName(model).c_str(),
                 exists ? "ENCONTRADO" : "NO ENCONTRADO");
    }

    LOG_INFO("=== %s v%s cargado correctamente ===",
             PLUGIN_NAME, PLUGIN_VERSION);
    return true;
}

// =============================================================================
// obs_module_unload — Se llama cuando OBS descarga el plugin
// =============================================================================

MODULE_EXPORT void obs_module_unload() noexcept {
    LOG_INFO("=== %s v%s descargado ===", PLUGIN_NAME, PLUGIN_VERSION);
}

// =============================================================================
// obs_module_set_locale — Configuración de traducciones
// =============================================================================

MODULE_EXPORT void obs_module_set_locale(const char* locale) noexcept {
    // OBS gestiona las traducciones automáticamente con
    // OBS_MODULE_USE_DEFAULT_LOCALE si los archivos .ini están
    // en resources/locale/
    LOG_INFO("Locale configurado: %s", locale ? locale : "default");
}

// =============================================================================
// obs_module_post_load — Post-procesamiento después de cargar
// =============================================================================

MODULE_EXPORT void obs_module_post_load() noexcept {
    LOG_DEBUG("[Plugin] Post-load completado");
}

// =============================================================================
// Fin del archivo
// =============================================================================
