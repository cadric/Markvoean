/* C ULTRA-MIN TEMPLATE
   Purpose: Drafts and recovery listing/display/cleanup helpers
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [2.5.10] - 2025-09-28 - document/recovery_drafts.c
   Added: Extracted drafts/recovery utilities from document_manager.c
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>

#include <gtktext/document/document_manager.h>
#include "internal/recovery_priv.h"

/* ═══════════════════════════════════════════════════════════════════════════════
 * DRAFTS - List / display / remove
 * ═══════════════════════════════════════════════════════════════════════════════ */

gchar** document_manager_list_drafts(void)
{
    g_autofree gchar *drafts_dir = get_drafts_directory();
    g_autoptr(GDir) dir = g_dir_open(drafts_dir, 0, NULL);
    if (!dir) {
        g_debug("Could not open drafts directory: %s", drafts_dir);
        return NULL;
    }

    GPtrArray *drafts = g_ptr_array_new();
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_prefix(name, "draft-") && g_str_has_suffix(name, ".md")) {
            gchar *full_path = g_build_filename(drafts_dir, name, NULL);
            g_ptr_array_add(drafts, full_path);
        }
    }
    g_ptr_array_add(drafts, NULL);
    return (gchar**)g_ptr_array_free(drafts, FALSE);
}

gchar* document_manager_get_draft_display_name(const gchar *draft_path)
{
    g_return_val_if_fail(draft_path != NULL, NULL);
    g_autofree gchar *basename = g_path_get_basename(draft_path);

    if (g_str_has_prefix(basename, "draft-") && g_str_has_suffix(basename, ".md")) {
        g_autofree gchar *timestamp_part = g_strndup(basename + 6, strlen(basename) - 9);
        if (strlen(timestamp_part) == 15 && timestamp_part[8] == '-') {
            g_autofree gchar *date_part = g_strndup(timestamp_part, 8);
            g_autofree gchar *time_part = g_strdup(timestamp_part + 9);
            return g_strdup_printf("Draft %s-%s-%s %s:%s:%s",
                                  g_strndup(date_part, 4),
                                  g_strndup(date_part + 4, 2),
                                  g_strndup(date_part + 6, 2),
                                  g_strndup(time_part, 2),
                                  g_strndup(time_part + 2, 2),
                                  g_strndup(time_part + 4, 2));
        }
    }
    return g_strdup(basename);
}

gboolean document_manager_remove_draft(const gchar *draft_path, GError **error)
{
    g_return_val_if_fail(draft_path != NULL, FALSE);
    if (g_unlink(draft_path) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "Failed to remove draft file: %s", g_strerror(errno));
        return FALSE;
    }
    g_debug("Draft file removed: %s", draft_path);
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * RECOVERY - List / display / cleanup
 * ═══════════════════════════════════════════════════════════════════════════════ */

void recovery_info_free(RecoveryInfo *info)
{
    if (!info) return;
    g_free(info->original_path);
    g_free(info->content);
    g_free(info->draft_path);
    g_free(info);
}

RecoveryInfo* document_recovery_parse_file(const gchar *recovery_path, GError **error)
{
    g_return_val_if_fail(recovery_path != NULL, NULL);
    g_autoptr(GKeyFile) keyfile = g_key_file_new();
    if (!g_key_file_load_from_file(keyfile, recovery_path, G_KEY_FILE_NONE, error)) {
        return NULL;
    }
    RecoveryInfo *info = g_new0(RecoveryInfo, 1);
    info->original_path = g_key_file_get_string(keyfile, "Recovery", "OriginalPath", NULL);
    info->timestamp = g_key_file_get_int64(keyfile, "Recovery", "Timestamp", NULL);
    info->content = g_key_file_get_string(keyfile, "Recovery", "Content", NULL);
    info->is_untitled = g_key_file_get_boolean(keyfile, "Recovery", "IsUntitled", NULL);
    info->draft_path = g_key_file_get_string(keyfile, "Recovery", "DraftPath", NULL);
    return info;
}

gchar** document_manager_list_recovery_files(void)
{
    g_autofree gchar *recovery_dir = get_recovery_directory();
    g_autoptr(GDir) dir = g_dir_open(recovery_dir, 0, NULL);
    if (!dir) {
        g_debug("Could not open recovery directory: %s", recovery_dir);
        return NULL;
    }
    GPtrArray *recoveries = g_ptr_array_new();
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (g_str_has_suffix(name, ".recovery")) {
            gchar *full_path = g_build_filename(recovery_dir, name, NULL);
            g_ptr_array_add(recoveries, full_path);
        }
    }
    g_ptr_array_add(recoveries, NULL);
    return (gchar**)g_ptr_array_free(recoveries, FALSE);
}

gchar** document_manager_list_recovery_files_for_document(const gchar *file_path)
{
    g_return_val_if_fail(file_path != NULL, NULL);
    g_autofree gchar *basename = g_path_get_basename(file_path);
    g_auto(GStrv) all_recovery_files = document_manager_list_recovery_files();
    if (!all_recovery_files) return NULL;

    GPtrArray *filtered = g_ptr_array_new();
    for (gsize i = 0; all_recovery_files[i] != NULL; i++) {
        g_autofree gchar *recovery_basename = g_path_get_basename(all_recovery_files[i]);
        g_autofree gchar *expected_prefix = g_strdup_printf("%s-", basename);
        if (g_str_has_prefix(recovery_basename, expected_prefix) &&
            g_str_has_suffix(recovery_basename, ".recovery")) {
            g_ptr_array_add(filtered, g_strdup(all_recovery_files[i]));
            continue;
        }
        g_autoptr(RecoveryInfo) info = document_recovery_parse_file(all_recovery_files[i], NULL);
        if (info && info->original_path && g_strcmp0(info->original_path, file_path) == 0) {
            gboolean already_added = FALSE;
            for (guint j = 0; j < filtered->len; j++) {
                if (g_strcmp0(g_ptr_array_index(filtered, j), all_recovery_files[i]) == 0) {
                    already_added = TRUE; break;
                }
            }
            if (!already_added) g_ptr_array_add(filtered, g_strdup(all_recovery_files[i]));
        }
    }
    g_ptr_array_add(filtered, NULL);
    return (gchar**)g_ptr_array_free(filtered, FALSE);
}

gchar* document_manager_get_recovery_display_name(const gchar *recovery_path)
{
    g_return_val_if_fail(recovery_path != NULL, NULL);
    g_autoptr(GError) error = NULL;
    g_autoptr(RecoveryInfo) info = document_recovery_parse_file(recovery_path, &error);
    if (!info) {
        return g_path_get_basename(recovery_path);
    }
    g_autoptr(GDateTime) dt = g_date_time_new_from_unix_local(info->timestamp);
    g_autofree gchar *time_str = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
    if (info->is_untitled) {
        return g_strdup_printf("Untitled Document (%s)", time_str);
    } else if (info->original_path && *info->original_path) {
        g_autofree gchar *basename = g_path_get_basename(info->original_path);
        return g_strdup_printf("%s (%s)", basename, time_str);
    } else {
        return g_strdup_printf("Document (%s)", time_str);
    }
}

void document_manager_cleanup_recovery(const gchar *recovery_path)
{
    g_return_if_fail(recovery_path != NULL);
    g_autoptr(GError) error = NULL;
    if (g_unlink(recovery_path) != 0) {
        g_set_error(&error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "Failed to remove recovery file: %s", g_strerror(errno));
        g_warning("%s", error->message);
    } else {
        g_debug("Recovery file cleaned up: %s", recovery_path);
    }
}

/* Periodic cleanup for old recovery files (older than 7 days) */
void cleanup_old_recovery_files(void)
{
    g_auto(GStrv) recovery_files = document_manager_list_recovery_files();
    if (!recovery_files) return;
    gint64 cutoff_time = g_get_real_time() / G_USEC_PER_SEC - (7 * 24 * 60 * 60);
    for (gint i = 0; recovery_files[i]; i++) {
        g_autoptr(GError) error = NULL;
    g_autoptr(RecoveryInfo) info = document_recovery_parse_file(recovery_files[i], &error);
        if (info && info->timestamp < cutoff_time) {
            if (g_unlink(recovery_files[i]) == 0) {
                g_debug("Cleaned up old recovery file: %s", recovery_files[i]);
            } else {
                g_warning("Failed to cleanup old recovery file: %s", recovery_files[i]);
            }
        }
    }
}
