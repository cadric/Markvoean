/* C ULTRA-MIN TEMPLATE
   Purpose: Settings and configuration management for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - core/settings_manager.c
   Changed: Extracted settings management from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <gtktext/core/settings_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void maybe_setup_gsettings_schemas(void)
{
    const char *already = g_getenv("GSETTINGS_SCHEMA_DIR");
    if (already && *already) return;

    const char *candidates[] = { "./data", "../data", NULL };
    for (int i = 0; candidates[i]; i++) {
        const char *dir = candidates[i];
        g_autofree char *compiled = g_build_filename(dir, "gschemas.compiled", NULL);
        g_autofree char *xml = g_build_filename(dir, "org.gtk.gtktext.gschema.xml", NULL);

        if (g_file_test(compiled, G_FILE_TEST_EXISTS)) {
            gboolean needs_recompile = FALSE;
            if (g_file_test(xml, G_FILE_TEST_EXISTS)) {
                struct stat st_xml = {0}, st_comp = {0};
                if (g_stat(xml, &st_xml) == 0 && g_stat(compiled, &st_comp) == 0) {
                    if (st_xml.st_mtime > st_comp.st_mtime) {
                        needs_recompile = TRUE;
                    }
                }
            }
            if (needs_recompile) {
                g_message("Recompiling GSettings schemas under %s (XML newer)", dir);
                gchar *argv[] = { "glib-compile-schemas", (gchar*)dir, NULL };
                gint status = 0;
                GError *err = NULL;
                if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                                  NULL, NULL, &status, &err)) {
                    g_warning("Failed to spawn glib-compile-schemas: %s",
                              err ? err->message : "unknown error");
                    g_clear_error(&err);
                }
            }
            g_setenv("GSETTINGS_SCHEMA_DIR", dir, TRUE);
            g_message("Using local GSettings schemas at %s", dir);
            return;
        }
    }

    /* Try to compile schemas if XML is present and tool is available */
    for (int i = 0; candidates[i]; i++) {
        const char *dir = candidates[i];
        g_autofree char *xml = g_build_filename(dir, "org.gtk.gtktext.gschema.xml", NULL);
        if (!g_file_test(xml, G_FILE_TEST_EXISTS)) continue;

        g_message("Compiling GSettings schemas under %s", dir);
        gchar *argv[] = { "glib-compile-schemas", (gchar*)dir, NULL };
        gint status = 0;
        GError *err = NULL;
        if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                        NULL, NULL, &status, &err)) {
            if (status == 0) {
                g_autofree char *compiled = g_build_filename(dir, "gschemas.compiled", NULL);
                if (g_file_test(compiled, G_FILE_TEST_EXISTS)) {
                    g_setenv("GSETTINGS_SCHEMA_DIR", dir, TRUE);
                    g_message("Compiled and using local GSettings schemas at %s", dir);
                    return;
                }
            } else {
                g_debug("glib-compile-schemas exited with status %d", status);
            }
        } else {
            g_debug("Failed to spawn glib-compile-schemas: %s",
                    err ? err->message : "unknown error");
            g_clear_error(&err);
        }
    }
}

static void on_autosave_setting_changed(GSettings *settings, const gchar *key,
                                       gpointer user_data)
{
    (void)settings;
    (void)key;
    DocumentManager *dm = (DocumentManager *)user_data;
    g_return_if_fail(dm != NULL);

    document_manager_update_autosave_setting(dm);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Settings management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void settings_manager_setup_gsettings_schemas(void)
{
    maybe_setup_gsettings_schemas();
}

void settings_manager_on_autosave_setting_changed(GSettings *settings, const gchar *key,
                                                  gpointer user_data)
{
    on_autosave_setting_changed(settings, key, user_data);
}

GSettings* settings_manager_initialize_app_settings(void)
{
    GSettings *settings = g_settings_new("org.gtk.gtktext");
    if (!settings) {
        g_warning("Failed to create GSettings instance for org.gtk.gtktext");
        return NULL;
    }

    g_debug("Initialized app settings");
    return settings;
}

void settings_manager_initialize_document_settings(GtkApplication *app, DocumentManager *dm)
{
    g_return_if_fail(GTK_IS_APPLICATION(app));
    g_return_if_fail(dm != NULL);

    GSettings *settings = g_settings_new("org.gtk.gtktext");
    g_signal_connect(settings, "changed::autosave-enabled",
                    G_CALLBACK(on_autosave_setting_changed), dm);
    g_object_set_data_full(G_OBJECT(app), "app_settings", settings, g_object_unref);

    g_debug("Initialized document settings monitoring");
}