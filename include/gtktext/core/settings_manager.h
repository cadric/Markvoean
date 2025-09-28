/* C ULTRA-MIN TEMPLATE
   Purpose: Settings and configuration management for GTK markdown editor
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - core/settings_manager.h
   Changed: Extracted settings management from main.c for better organization
*/

#pragma once

#ifndef GTKTEXT_CORE_SETTINGS_MANAGER_H
#define GTKTEXT_CORE_SETTINGS_MANAGER_H

#include <gtk/gtk.h>
#include <gtktext/document/document_manager.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Settings management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Setup GSettings schemas for development/testing
 * Automatically detects and compiles local schemas if needed
 */
void settings_manager_setup_gsettings_schemas(void);

/**
 * Handle autosave setting changes
 * @param settings GSettings instance
 * @param key Settings key that changed
 * @param user_data DocumentManager instance
 */
void settings_manager_on_autosave_setting_changed(GSettings *settings, const gchar *key,
                                                  gpointer user_data);

/**
 * Initialize global app settings
 * @return GSettings instance for org.gtk.gtktext
 */
GSettings* settings_manager_initialize_app_settings(void);

/**
 * Initialize DocumentManager settings monitoring
 * @param app Application instance
 * @param dm DocumentManager instance
 */
void settings_manager_initialize_document_settings(GtkApplication *app, DocumentManager *dm);

G_END_DECLS

#endif /* GTKTEXT_CORE_SETTINGS_MANAGER_H */
