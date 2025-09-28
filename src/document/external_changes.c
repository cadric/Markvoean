/* C ULTRA‑MIN TEMPLATE
   Purpose: File monitoring, external change detection, and conflict resolution UI
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <gtktext/ui/dialogs.h>
#include <gtktext/document/document_manager.h>

#include "internal/document_manager_priv.h"
#include "internal/external_changes.h"

/* HELPERS */

static void on_conflict_resolved(ConflictResolution resolution, gpointer user_data)
{
  DocumentManager *dm = GTKTEXT_DOCUMENT_MANAGER(user_data);
  g_return_if_fail(dm != NULL);

  GError *error = NULL;

  switch (resolution) {
    case CONFLICT_RESOLUTION_RELOAD:
      if (document_manager_resolve_conflict(dm, TRUE, &error)) {
        g_debug("Successfully reloaded file from external version: %s", dm->file_path);
      } else {
        g_warning("Failed to reload from external version: %s", error ? error->message : "Unknown error");
        g_clear_error(&error);
      }
      break;

    case CONFLICT_RESOLUTION_KEEP:
      if (document_manager_resolve_conflict(dm, FALSE, &error)) {
        g_debug("Keeping current version, ignoring external changes: %s", dm->file_path);
      } else {
        g_warning("Failed to resolve conflict by keeping current version: %s", error ? error->message : "Unknown error");
        g_clear_error(&error);
      }
      break;

    case CONFLICT_RESOLUTION_MELD:
      g_message("Merge tool integration not yet implemented");
      if (document_manager_resolve_conflict(dm, FALSE, &error)) {
        g_debug("Merge requested but not implemented, keeping current version");
      } else {
        g_warning("Failed to resolve conflict: %s", error ? error->message : "Unknown error");
        g_clear_error(&error);
      }
      break;
  }

  g_object_unref(dm);
}

/* UI entry point for conflict dialog */
void document_external_show_change_dialog(DocumentManager *dm)
{
  g_return_if_fail(dm != NULL);
  GtkWindow *parent_window = dm->window;
  g_object_ref(dm);
  dialogs_show_external_change_conflict(parent_window, dm->file_path, on_conflict_resolved, dm);
  g_debug("External change conflict dialog shown for: %s", dm->file_path);
}

/* Check if file content has changed externally */
gboolean document_external_has_changed(DocumentManager *dm)
{
  g_return_val_if_fail(dm != NULL, FALSE);
  g_return_val_if_fail(dm->file_path != NULL, FALSE);

  /* Check modification time */
  gint64 current_mtime = get_file_mtime(dm->file_path);
  if (current_mtime > dm->last_mtime) {
    return TRUE;
  }

  /* Also check content hash if available */
  if (dm->last_hash) {
    g_autoptr(GError) error = NULL;
    g_autofree gchar *current_hash = calculate_file_hash(dm->file_path, &error);
    if (current_hash && g_strcmp0(current_hash, dm->last_hash) != 0) {
      return TRUE;
    }
  }
  return FALSE;
}

/* Load external file content for comparison/merge */
gchar *document_external_load_content(DocumentManager *dm, GError **error)
{
  g_return_val_if_fail(dm != NULL, NULL);
  g_return_val_if_fail(dm->file_path != NULL, NULL);

  gchar *content = NULL;
  if (!g_file_get_contents(dm->file_path, &content, NULL, error)) {
    return NULL;
  }
  return content;
}

/* Update file metadata after resolving conflict */
void document_external_update_metadata(DocumentManager *dm)
{
  g_return_if_fail(dm != NULL);
  g_return_if_fail(dm->file_path != NULL);

  dm->last_mtime = get_file_mtime(dm->file_path);

  g_autoptr(GError) error = NULL;
  g_free(dm->last_hash);
  dm->last_hash = calculate_file_hash(dm->file_path, &error);
  if (error) {
    g_warning("Failed to calculate file hash: %s", error->message);
    dm->last_hash = NULL;
  }

  g_debug("File metadata updated: mtime=%ld, hash=%s", dm->last_mtime, dm->last_hash ? dm->last_hash : "none");
}

/* Monitor callback */
static void on_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file,
                            GFileMonitorEvent event, gpointer user_data)
{
  (void)monitor; (void)other_file;
  DocumentManager *dm = user_data;
  g_return_if_fail(dm != NULL);

  if (event == G_FILE_MONITOR_EVENT_CHANGED) {
    g_autoptr(GFileInfo) info = g_file_query_info(file,
                          G_FILE_ATTRIBUTE_TIME_MODIFIED,
                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (info) {
      gint64 mtime = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
      if (mtime > dm->last_mtime) {
        g_debug("External modification detected: %s (mtime: %ld > %ld)", dm->file_path, mtime, dm->last_mtime);
        document_external_show_change_dialog(dm);
      }
    }
  } else if (event == G_FILE_MONITOR_EVENT_DELETED) {
    g_message("File was deleted externally: %s", dm->file_path);
  }
}

/* Setup file monitoring for external changes */
void document_external_setup_file_monitor(DocumentManager *dm)
{
  g_return_if_fail(dm != NULL);
  if (!dm->file_path) return;

  if (dm->file_monitor) {
    g_object_unref(dm->file_monitor);
    dm->file_monitor = NULL;
  }

  g_autoptr(GFile) file = g_file_new_for_path(dm->file_path);
  GFileMonitor *monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);
  if (monitor) {
    g_signal_connect(monitor, "changed", G_CALLBACK(on_file_changed), dm);
    dm->file_monitor = monitor;
    g_debug("File monitor setup for: %s", dm->file_path);
  } else {
    g_warning("Failed to setup file monitor for: %s", dm->file_path);
  }
}

