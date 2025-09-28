/* C ULTRA-MIN TEMPLATE
   Purpose: Filesystem utilities and app directories (drafts/recovery/versions helpers)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [2.5.10] - 2025-09-28 - document/fs_utils.c
   Added: Extracted directory and file utility helpers from document_manager.c
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <unistd.h>

#include <gtktext/document/document_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * DIRECTORY MANAGEMENT - Paths
 * ═══════════════════════════════════════════════════════════════════════════════ */

gchar* get_drafts_directory(void)
{
    const gchar *data_dir = g_get_user_data_dir();
    g_autofree gchar *app_dir = g_build_filename(data_dir, "gtktext", NULL);
    gchar *drafts_dir = g_build_filename(app_dir, "drafts", NULL);

    if (g_mkdir_with_parents(drafts_dir, 0700) != 0) {
        g_warning("Failed to create drafts directory: %s", drafts_dir);
    }
    return drafts_dir;
}

gchar* get_recovery_directory(void)
{
    const gchar *cache_dir = g_get_user_cache_dir();
    g_autofree gchar *app_dir = g_build_filename(cache_dir, "gtktext", NULL);
    gchar *recovery_dir = g_build_filename(app_dir, "recovery", NULL);

    if (g_mkdir_with_parents(recovery_dir, 0700) != 0) {
        g_warning("Failed to create recovery directory: %s", recovery_dir);
    }
    return recovery_dir;
}

gchar* get_temp_directory(void)
{
    return g_strdup(g_get_tmp_dir());
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * FILE UTILITIES
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean check_file_writable(const gchar *path)
{
    g_return_val_if_fail(path != NULL, FALSE);

    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        g_autofree gchar *dir = g_path_get_dirname(path);
        return check_directory_writable(dir);
    }
    return g_access(path, W_OK) == 0;
}

gboolean check_directory_writable(const gchar *path)
{
    g_return_val_if_fail(path != NULL, FALSE);
    return g_access(path, W_OK) == 0;
}

gint64 get_file_mtime(const gchar *path)
{
    g_return_val_if_fail(path != NULL, 0);

    g_autoptr(GFile) file = g_file_new_for_path(path);
    g_autoptr(GFileInfo) info = g_file_query_info(file,
        G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (!info) return 0;
    return g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}

gchar* calculate_file_hash(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, NULL);
    g_autofree gchar *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(path, &contents, &length, error)) {
        return NULL;
    }
    return g_compute_checksum_for_data(G_CHECKSUM_SHA256,
                                      (const guchar *)contents, length);
}

