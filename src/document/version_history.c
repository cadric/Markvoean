/* C ULTRA-MIN TEMPLATE
   Purpose: Version history save/list/display/cleanup helpers
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [2.5.10] - 2025-09-28 - document/version_history.c
   Added: Extracted version history logic from document_manager.c to reduce size and improve cohesion
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <gtktext/render/cmrender.h>
#include <gtktext/document/document_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * INTERNAL HELPERS - Settings & paths
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Get version history directory */
static gchar* get_version_history_directory(void)
{
    const gchar *cache_dir;
    if (g_getenv("MESON_TEST_ITERATION")) {
        cache_dir = "/tmp"; /* isolate tests */
    } else {
        cache_dir = g_get_user_cache_dir();
    }

    gchar *versions_dir = g_build_filename(cache_dir, "gtktext", "versions", NULL);
    if (g_mkdir_with_parents(versions_dir, 0755) != 0) {
        g_warning("Failed to create version history directory: %s", versions_dir);
    }
    return versions_dir;
}

static gboolean is_version_history_enabled(void)
{
    g_autoptr(GSettings) settings = g_settings_new("org.gtk.gtktext");
    return g_settings_get_boolean(settings, "version-history-enabled");
}

static gint get_max_versions(void)
{
    g_autoptr(GSettings) settings = g_settings_new("org.gtk.gtktext");
    return g_settings_get_int(settings, "version-history-max-versions");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Version history
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* save_version_history remains implemented in document_manager.c to access internals safely */

gchar** document_manager_list_version_history(const gchar *file_path)
{
    g_return_val_if_fail(file_path != NULL, NULL);

    g_autofree gchar *versions_dir = get_version_history_directory();
    g_autoptr(GDir) dir = g_dir_open(versions_dir, 0, NULL);
    if (!dir) {
        g_debug("Could not open version history directory: %s", versions_dir);
        return NULL;
    }

    g_autofree gchar *basename = g_path_get_basename(file_path);
    g_autofree gchar *prefix = g_strdup_printf("%s-", basename);

    GPtrArray *versions = g_ptr_array_new();
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_prefix(name, prefix) && g_str_has_suffix(name, ".version")) {
            gchar *full_path = g_build_filename(versions_dir, name, NULL);
            g_ptr_array_add(versions, full_path);
        }
    }

    g_ptr_array_sort(versions, (GCompareFunc)g_strcmp0);
    g_ptr_array_add(versions, NULL);
    return (gchar**)g_ptr_array_free(versions, FALSE);
}

gchar* document_manager_get_version_display_name(const gchar *version_path)
{
    g_return_val_if_fail(version_path != NULL, NULL);
    g_autoptr(GKeyFile) metadata = g_key_file_new();
    g_autoptr(GError) error = NULL;
    if (!g_key_file_load_from_file(metadata, version_path, G_KEY_FILE_NONE, &error)) {
        return g_path_get_basename(version_path);
    }
    gint64 ts = g_key_file_get_int64(metadata, "Version", "Timestamp", NULL);
    if (ts > 0) {
        g_autoptr(GDateTime) dt = g_date_time_new_from_unix_local(ts);
        g_autofree gchar *formatted = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
        return g_strdup_printf("Version from %s", formatted);
    }
    return g_path_get_basename(version_path);
}

void document_manager_cleanup_old_versions(const gchar *file_path)
{
    g_return_if_fail(file_path != NULL);
    g_auto(GStrv) versions = document_manager_list_version_history(file_path);
    if (!versions) return;
    gint max_versions = get_max_versions();
    gint count = g_strv_length(versions);
    if (count > max_versions) {
        gint to_remove = count - max_versions;
        for (gint i = count - to_remove; i < count; i++) {
            if (g_unlink(versions[i]) == 0) {
                g_debug("Removed old version: %s", versions[i]);
            } else {
                g_warning("Failed to remove old version: %s", versions[i]);
            }
        }
    }
}
