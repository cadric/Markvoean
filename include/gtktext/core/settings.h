/* [2.5.9] - 2025-09-28 - include/gtktext/core/settings.h
 * Changed: Add #pragma once for C23 header hygiene; keep API intact.
 */
#pragma once
/* Legacy include guard retained for compatibility with external tools */
#ifndef GTKTEXT_CORE_SETTINGS_H
#define GTKTEXT_CORE_SETTINGS_H

#include <gtk/gtk.h>
#include <adwaita.h>
#include <gtktext/render/cmrender.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create and display a settings dialog window
 * 
 * @param parent The parent GtkWindow for the dialog
 * @return An AdwDialog instance representing the settings dialog
 */
AdwDialog* create_settings_window(GtkWindow *parent);

/**
 * Get the application settings instance
 *
 * @return GSettings instance for the application
 */
GSettings* gtktext_get_app_settings(void);

#ifdef __cplusplus
}
#endif

#endif // GTKTEXT_CORE_SETTINGS_H
