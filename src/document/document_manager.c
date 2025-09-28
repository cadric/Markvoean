/* C ULTRA-MIN TEMPLATE
   Purpose: Document lifecycle, autosave, and recovery management
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.4.3] - 2025-09-19 - document_manager.c
   Changed: Added document_manager_adopt_current_buffer (with deprecated shim) to avoid double file reading
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtktext/document/document_manager.h>
#include <gtktext/document/doc_state.h>
#include "internal/document_manager_priv.h"
#include "internal/recovery_priv.h"
#include <gtktext/document/document_portal.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/render/theme_styles.h>
#include <gtktext/render/markdown/markdown_engine.h>
#include <gtktext/ui/dialogs.h>
#include "internal/external_changes.h"
#include <gtktext/ui/status_manager.h>
#include <gtktext/core/util.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <gio/gio.h>
#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* Idle callback used to mark document dirty after restore without re-entrancy */
static gboolean idle_mark_dirty_cb(gpointer u)
{
    DocumentManager *idm = GTKTEXT_DOCUMENT_MANAGER(u);
    doc_on_user_mutation(&idm->doc_state);
    g_object_unref(idm);
    return G_SOURCE_REMOVE;
}

static void document_manager_refresh_portal_uri(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);

    g_clear_pointer(&dm->document_portal_uri, g_free);

    if (!dm->file_path) {
        return;
    }

    GError *portal_error = NULL;
    gchar *uri = document_portal_export_path(dm->file_path, &portal_error);
    if (uri) {
        dm->document_portal_uri = uri;
        g_debug("Portal exported document: %s -> %s", dm->file_path, uri);
    } else {
        if (portal_error) {
            g_debug("Document portal export failed for %s: %s",
                    dm->file_path, portal_error->message);
            g_clear_error(&portal_error);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Internal type definitions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* GObject implementation struct moved to internal/document_manager_priv.h */

/* GObject type implementation */
G_DEFINE_TYPE(GtktextDocumentManager, gtktext_document_manager, G_TYPE_OBJECT)

/* Signal enumeration */
enum {
    SIGNAL_STATE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Module-level constants and static data
 * ═══════════════════════════════════════════════════════════════════════════════ */

static const guint AUTOSAVE_INTERVAL_MS = 3000;     /* 3 seconds */
static const guint RECOVERY_INTERVAL_MS = 30000;    /* 30 seconds */
static const guint DEBOUNCE_INTERVAL_MS = 500;      /* 500ms for buffer change debouncing */

/* Forward declarations for internal functions */
static void document_manager_finalize_initialization(DocumentManager *dm);
static gboolean debounce_timeout_cb(gpointer user_data);

/* ═══════════════════════════════════════════════════════════════════════════════
 * ERROR DOMAIN - DocumentManager error domain implementation
 * ═══════════════════════════════════════════════════════════════════════════════ */

GQuark gtktext_document_error_quark(void)
{
    return g_quark_from_static_string("gtktext-document-error-quark");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * GOBJECT IMPLEMENTATION - Class initialization and methods
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void gtktext_document_manager_finalize(GObject *object)
{
    GtktextDocumentManager *dm = GTKTEXT_DOCUMENT_MANAGER(object);

    g_debug("Finalizing DocumentManager");

    /* Stop timers */
    document_manager_stop_autosave(dm);
    if (dm->recovery_id > 0) {
        g_source_remove(dm->recovery_id);
        dm->recovery_id = 0;
    }
    if (dm->debounce_id > 0) {
        g_source_remove(dm->debounce_id);
        dm->debounce_id = 0;
    }

    /* Disconnect signals */
    if (dm->buffer && dm->buffer_changed_handler_id > 0) {
        g_signal_handler_disconnect(dm->buffer, dm->buffer_changed_handler_id);
    }

    /* Clean up file monitor */
    g_clear_object(&dm->file_monitor);

    /* Free references */
    g_clear_object(&dm->buffer);
    g_clear_object(&dm->window);
    g_clear_object(&dm->settings);

    /* Free owned strings */
    g_clear_pointer(&dm->file_path, g_free);
    g_clear_pointer(&dm->draft_path, g_free);
    g_clear_pointer(&dm->recovery_path, g_free);
    g_clear_pointer(&dm->last_hash, g_free);
    g_clear_pointer(&dm->original_content, g_free);
    g_clear_pointer(&dm->document_portal_uri, g_free);

    G_OBJECT_CLASS(gtktext_document_manager_parent_class)->finalize(object);
}

static void gtktext_document_manager_class_init(GtktextDocumentManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = gtktext_document_manager_finalize;

    /* Define state-changed signal */
    signals[SIGNAL_STATE_CHANGED] = g_signal_new(
        "state-changed",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        NULL,
        G_TYPE_NONE,
        2,
        G_TYPE_INT,  /* old_state */
        G_TYPE_INT   /* new_state */
    );
}

static void gtktext_document_manager_init(GtktextDocumentManager *dm)
{
    /* Initialize instance - implementation will be added later */
    /* Note: doc_state will be properly initialized in document_manager_new */
    dm->initialization_complete = FALSE;
    dm->debounce_id = 0;
    dm->autosave_in_progress = FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Utility and helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Notify UI of dirty state changes using new simplified system */
static void notify_dirty_state_changed(DocumentManager *dm)
{
    g_return_if_fail(GTKTEXT_IS_DOCUMENT_MANAGER(dm));

    bool is_dirty = doc_is_dirty(&dm->doc_state);

    g_debug("Document dirty state: %s", is_dirty ? "dirty" : "clean");

    /* For now, emit old signal with translated states for backward compatibility */
    DocumentState new_state = is_dirty ? DOC_STATE_DIRTY : DOC_STATE_CLEAN;
    DocumentState old_state = is_dirty ? DOC_STATE_CLEAN : DOC_STATE_DIRTY;

    g_signal_emit(dm, signals[SIGNAL_STATE_CHANGED], 0, old_state, new_state);

    /* Keep backward compatibility with callback */
    if (dm->state_callback) {
        dm->state_callback(dm, old_state, new_state, dm->state_callback_data);
    }
}

/* Get current buffer content as markdown */
static gchar* get_buffer_content_as_markdown(GtkTextBuffer *buffer)
{
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), NULL);

    /* Use proper markdown extraction that preserves formatting tags */
    gchar *text = cm_render_buffer_to_markdown(buffer);

    /* Debug what we're getting */
    g_debug("get_buffer_content_as_markdown: [%s] (length=%zu)",
            text ? text : "(null)", text ? strlen(text) : 0);

    return text;
}


/* Update original content after successful save */
static void update_original_content(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    g_return_if_fail(dm->buffer != NULL);

    g_free(dm->original_content);
    dm->original_content = get_buffer_content_as_markdown(dm->buffer);
}


/* Atomic write helpers moved to src/document/atomic_io.c */

/* Directory and file utilities moved to src/document/fs_utils.c */

/* Create a new draft file with timestamped name */
static gchar* create_draft_file(const gchar *content, GError **error)
{
    g_return_val_if_fail(content != NULL, NULL);
    
    g_autofree gchar *drafts_dir = get_drafts_directory();
    
    /* Generate timestamp-based filename */
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree gchar *timestamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    g_autofree gchar *filename = g_strdup_printf("draft-%s.md", timestamp);
    gchar *path = g_build_filename(drafts_dir, filename, NULL);
    
    if (atomic_write_file(path, content, strlen(content), error)) {
        g_debug("Draft file created: %s", path);
        return path;
    }
    
    g_free(path);
    return NULL;
}

/* List all draft files in the drafts directory */

/* Get draft file info including timestamp and content preview */

/* Remove a draft file */

/* Recovery System - Crash recovery and data protection */

typedef struct {
    GtktextDocumentManager *dm;
    gchar *path;
    GBytes *bytes;
    GFile *file;
} RecoveryWriteJob;

static void recovery_write_job_free(RecoveryWriteJob *job)
{
    if (!job) return;
    g_clear_object(&job->file);
    if (job->bytes) {
        g_bytes_unref(job->bytes);
    }
    if (job->dm) {
        g_object_unref(job->dm);
    }
    g_clear_pointer(&job->path, g_free);
    g_free(job);
}

static void on_recovery_snapshot_written(GObject *source_object,
                                         GAsyncResult *result,
                                         gpointer user_data)
{
    RecoveryWriteJob *job = user_data;
    GFile *file = G_FILE(source_object);
    GError *error = NULL;

    gboolean ok = g_file_replace_contents_finish(file, result, NULL, &error);

    GtktextDocumentManager *dm = job->dm;
    dm->recovery_write_in_progress = FALSE;

    if (!ok) {
        g_warning("Failed to write recovery snapshot: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
        recovery_write_job_free(job);
        return;
    }

    g_free(dm->recovery_path);
    dm->recovery_path = g_strdup(job->path);
    g_debug("Recovery snapshot written: %s", job->path);

    recovery_write_job_free(job);
}

static gboolean write_recovery_snapshot_async(GtktextDocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(dm->buffer != NULL, FALSE);

    if (dm->recovery_write_in_progress) {
        return FALSE;
    }

    g_autofree gchar *content = get_buffer_content_as_markdown(dm->buffer);
    if (!content) {
        return FALSE;
    }

    if (g_utf8_strlen(content, -1) == 0) {
        return FALSE;
    }

    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree gchar *timestamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    g_autofree gchar *basename = dm->file_path ?
        g_path_get_basename(dm->file_path) : g_strdup("untitled");

    g_autofree gchar *recovery_dir = get_recovery_directory();
    g_autofree gchar *filename = g_strdup_printf("%s-%s.recovery",
                                                 basename, timestamp);
    gchar *path = g_build_filename(recovery_dir, filename, NULL);

    g_autoptr(GKeyFile) metadata = g_key_file_new();
    g_key_file_set_string(metadata, "Recovery", "OriginalPath",
                          dm->file_path ? dm->file_path : "");
    g_key_file_set_int64(metadata, "Recovery", "Timestamp",
                         g_date_time_to_unix(now));
    g_key_file_set_string(metadata, "Recovery", "Content", content);
    g_key_file_set_boolean(metadata, "Recovery", "IsUntitled", dm->is_untitled);
    g_key_file_set_string(metadata, "Recovery", "DraftPath",
                          dm->draft_path ? dm->draft_path : "");

    gsize data_len = 0;
    gchar *data = g_key_file_to_data(metadata, &data_len, NULL);
    if (!data) {
        g_free(path);
        return FALSE;
    }

    RecoveryWriteJob *job = g_new0(RecoveryWriteJob, 1);
    job->dm = g_object_ref(dm);
    job->path = g_strdup(path);
    job->bytes = g_bytes_new_take((guchar *)data, data_len);
    job->file = g_file_new_for_path(path);

    dm->recovery_write_in_progress = TRUE;

    g_file_replace_contents_bytes_async(job->file,
                                        job->bytes,
                                        NULL,
                                        FALSE,
                                        G_FILE_CREATE_NONE,
                                        NULL,
                                        on_recovery_snapshot_written,
                                        job);

    g_free(path);
    return TRUE;
}

/* Recovery internals */
#include "internal/recovery_priv.h"

/* Get display name for recovery file */

/* Remove a recovery file */
/* Recovery file removal & old cleanup moved to recovery_drafts.c */

/* External Change Detection moved to src/document/external_changes.c */

/* Load content from file path and replace buffer contents */
static gboolean load_content_into_buffer(DocumentManager *dm, const char *file_path, GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(file_path != NULL, FALSE);
    g_return_val_if_fail(dm->buffer != NULL, FALSE);

    /* Load file content */
    g_autofree gchar *content = NULL;
    if (!g_file_get_contents(file_path, &content, NULL, error)) {
        return FALSE;
    }

    /* Clear current buffer and load new content */
    /* For file loading, we want to start with a clean undo stack */
    gtk_text_buffer_begin_irreversible_action(dm->buffer);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(dm->buffer, &start, &end);
    gtk_text_buffer_delete(dm->buffer, &start, &end);
    gtk_text_buffer_insert_at_cursor(dm->buffer, content, -1);
    gtk_text_buffer_end_irreversible_action(dm->buffer);

    /* Apply theme colors to any existing tags after loading content */
    theme_styles_update_theme_dependent_tags(dm->buffer);

    return TRUE;
}

/* Update file metadata after resolving conflict */
/* External change conflict handling moved to src/document/external_changes.c */

/* File Utilities - File system operations and checks */

/* File utilities moved to src/document/fs_utils.c */

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Signal handlers and callbacks
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    DocumentManager *dm = user_data;
    g_return_if_fail(dm != NULL);
    g_return_if_fail(buffer != NULL);

    /* Skip during initialization */
    if (!dm->initialization_complete) {
        g_debug("DocumentManager buffer changed during initialization - ignoring");
        return;
    }

    /* Note: With new doc_state system, we always process changes to update hash */

    /* Cancel any existing debounce timer */
    if (dm->debounce_id > 0) {
        g_source_remove(dm->debounce_id);
        dm->debounce_id = 0;
    }

    /* Start debounce timer to coalesce rapid changes */
    dm->debounce_id = g_timeout_add(DEBOUNCE_INTERVAL_MS, debounce_timeout_cb, dm);
    g_debug("DocumentManager buffer changed: started debounce timer (%dms)", DEBOUNCE_INTERVAL_MS);
}

static gboolean debounce_timeout_cb(gpointer user_data)
{
    DocumentManager *dm = user_data;
    g_return_val_if_fail(dm != NULL, G_SOURCE_REMOVE);

    /* Clear the debounce timer ID since it's about to be removed */
    dm->debounce_id = 0;

    /* Check if there's a pending user change that should trigger dirty state */
    gboolean user_change_pending = render_get_user_change_pending(dm->buffer);

    /* Update state using new doc_state system */
    if (dm->initialization_complete) {
        /* Record user mutation - this updates hash and dirty state */
        bool was_dirty = doc_is_dirty(&dm->doc_state);
        doc_on_user_mutation(&dm->doc_state);
        bool is_dirty = doc_is_dirty(&dm->doc_state);

        g_debug("DocumentManager debounced buffer change: user_change_pending=%s, state: %s -> %s",
               user_change_pending ? "TRUE" : "FALSE",
               was_dirty ? "dirty" : "clean",
               is_dirty ? "dirty" : "clean");

        /* Notify UI if state changed */
        if (was_dirty != is_dirty) {
            notify_dirty_state_changed(dm);
        }
    }

    return G_SOURCE_REMOVE;
}

static void on_autosave_complete(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    (void)source_object; /* Unused parameter */
    DocumentManager *dm = user_data;
    g_return_if_fail(dm != NULL);

    GError *error = NULL;
    if (!document_manager_save_finish(dm, result, &error)) {
        g_warning("Autosave failed: %s", error ? error->message : "Unknown error");
        g_clear_error(&error);
    } else {
        g_debug("Autosave completed successfully");
    }

    /* Reset the in-progress flag */
    dm->autosave_in_progress = FALSE;
}

static gboolean autosave_timeout_cb(gpointer user_data)
{
    DocumentManager *dm = user_data;
    g_return_val_if_fail(dm != NULL, G_SOURCE_REMOVE);

    /* Only autosave if dirty and not currently saving */
    if (doc_is_dirty(&dm->doc_state) && !dm->autosave_in_progress) {
        g_debug("Performing autosave");

        /* Set in-progress flag to prevent concurrent autosaves */
        dm->autosave_in_progress = TRUE;

        /* For untitled documents, save as draft */
        if (dm->is_untitled) {
            g_autoptr(GError) error = NULL;
            if (!document_manager_save_draft(dm, &error)) {
                g_warning("Autosave draft failed: %s",
                         error ? error->message : "Unknown error");
            }
            /* Reset flag for draft saves (synchronous) */
            dm->autosave_in_progress = FALSE;
        } else {
            /* For named documents, save in place using async API with callback */
            document_manager_save_async(dm, NULL, on_autosave_complete, dm);
        }
    }
    
    return G_SOURCE_CONTINUE;
}

static gboolean recovery_timeout_cb(gpointer user_data)
{
    DocumentManager *dm = user_data;
    g_return_val_if_fail(dm != NULL, G_SOURCE_REMOVE);
    
    /* Write recovery snapshot if document has content and is dirty or draft */
    if (doc_is_dirty(&dm->doc_state)) {
        if (!write_recovery_snapshot_async(dm)) {
            g_debug("Recovery snapshot skipped (no changes or write in progress)");
        }
    }
    
    /* Periodically clean up old recovery files */
    static gint cleanup_counter = 0;
    if (++cleanup_counter >= 20) { /* Every 20 recovery cycles (10 minutes) */
        /* Periodic cleanup provided by recovery_drafts.c */
        cleanup_old_recovery_files();
        cleanup_counter = 0;
    }
    
    return G_SOURCE_CONTINUE;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * LIFECYCLE - Creation, destruction, and resource management
 * ═══════════════════════════════════════════════════════════════════════════════ */

DocumentManager* document_manager_new(GtkTextBuffer *buffer, GtkWindow *window)
{
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), NULL);
    g_return_val_if_fail(GTK_IS_WINDOW(window), NULL);

    DocumentManager *dm = g_object_new(GTKTEXT_TYPE_DOCUMENT_MANAGER, NULL);

    /* Store references */
    dm->buffer = g_object_ref(buffer);
    dm->window = g_object_ref(window);
    dm->settings = g_settings_new("org.gtk.gtktext");

    /* Initialize state */
    dm->is_untitled = TRUE;
    dm->last_mtime = 0;

    /* Initialize new doc_state system */
    doc_state_init(&dm->doc_state, buffer);

    /* Connect to buffer changes */
    dm->buffer_changed_handler_id = g_signal_connect(buffer, "changed",
        G_CALLBACK(on_buffer_changed), dm);

    /* Store initial content */
    update_original_content(dm);

    /* Mark initial document as clean */
    doc_mark_loaded_or_new(&dm->doc_state);

    /* Finalize initialization to enable buffer change detection */
    document_manager_finalize_initialization(dm);

    g_debug("DocumentManager created");
    return dm;
}

void document_manager_free(DocumentManager *dm)
{
    if (!dm) return;

    g_debug("Freeing DocumentManager (using g_object_unref)");

    /* DocumentManager is now a GObject - use unref for cleanup */
    g_object_unref(dm);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * ASYNC I/O OPERATIONS - Modern async APIs using GTask + GIO
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Task data for async operations */
typedef struct {
    DocumentManager *dm;
    gchar *file_path;
    gchar *content;
} AsyncTaskData;

static void async_task_data_free(AsyncTaskData *data)
{
    if (!data) return;
    if (data->dm) g_object_unref(data->dm);
    g_free(data->file_path);
    g_free(data->content);
    g_free(data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(AsyncTaskData, async_task_data_free)

/* Async open completion in main thread */
static void document_manager_open_async_complete(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GFile *file = G_FILE(source_object);
    GTask *task = G_TASK(user_data);
    AsyncTaskData *data = g_task_get_task_data(task);
    DocumentManager *dm = data->dm;

    GError *error = NULL;
    gchar *contents = NULL;
    gsize length = 0;

    /* Get file contents from async operation */
    if (!g_file_load_contents_finish(file, result, &contents, &length, NULL, &error)) {
        /* Show error status */
        if (dm->window) {
            GtkApplication *app = gtk_window_get_application(dm->window);
            if (app) {
                status_manager_update_async_operation(app, _("Opening"), data->file_path, FALSE);
                g_autofree gchar *error_msg = g_strdup_printf(_("Failed to open %s"),
                                                            g_path_get_basename(data->file_path));
                status_manager_show_toast(app, error_msg, 4);
            }
        }

        /* Map GIO errors to our domain */
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_NOT_FOUND,
                                   "File does not exist: %s", data->file_path);
        } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)) {
            g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_READONLY,
                                   "Permission denied: %s", data->file_path);
        } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_CANCELLED,
                                   "Operation cancelled");
        } else {
            g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_IO,
                                   "I/O error: %s", error->message);
        }
        g_clear_error(&error);
        g_object_unref(task);
        return;
    }

    /* Update DocumentManager state on main thread */

    /* Block buffer change signals during loading */
    document_manager_block_buffer_signals(dm);

    /* Suppress markdown parsing during file loading to prevent automatic formatting */
    render_set_suppress_reparse(dm->buffer, TRUE);

    /* Clear current buffer and load new content */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(dm->buffer, &start, &end);
    gtk_text_buffer_delete(dm->buffer, &start, &end);
    gtk_text_buffer_insert_at_cursor(dm->buffer, contents, -1);

    /* Apply theme colors to any existing tags after loading content */
    theme_styles_update_theme_dependent_tags(dm->buffer);

    /* Re-enable markdown parsing after loading is complete */
    render_set_suppress_reparse(dm->buffer, FALSE);

    /* Update document state */
    g_free(dm->file_path);
    dm->file_path = g_strdup(data->file_path);
    dm->is_untitled = FALSE;

    /* Clear any existing draft/recovery paths */
    g_clear_pointer(&dm->draft_path, g_free);
    g_clear_pointer(&dm->recovery_path, g_free);

    document_manager_refresh_portal_uri(dm);

    /* Update file metadata */
    document_external_update_metadata(dm);

    /* Setup file monitoring */
    document_external_setup_file_monitor(dm);

    /* Update original content and set clean state */
    update_original_content(dm);
    doc_mark_loaded_or_new(&dm->doc_state);

    /* Unblock buffer change signals */
    document_manager_unblock_buffer_signals(dm);

    /* Finalize initialization to enable buffer change detection */
    document_manager_finalize_initialization(dm);

    /* Show success status */
    if (dm->window) {
        GtkApplication *app = gtk_window_get_application(dm->window);
        if (app) {
            status_manager_update_async_operation(app, _("Opening"), data->file_path, FALSE);
            g_autofree gchar *success_msg = g_strdup_printf(_("Opened %s"),
                                                          g_path_get_basename(data->file_path));
            status_manager_show_toast(app, success_msg, 3);
        }
    }

    g_debug("Async file opened successfully: %s", data->file_path);
    g_free(contents);

    /* Return success */
    g_task_return_boolean(task, TRUE);
    g_object_unref(task);
}

void document_manager_open_async(DocumentManager *dm, const gchar *file_path,
                                GCancellable *cancellable, GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_return_if_fail(dm != NULL);
    g_return_if_fail(file_path != NULL);

    /* Create task and data */
    GTask *task = g_task_new(dm, cancellable, callback, user_data);
    AsyncTaskData *data = g_new0(AsyncTaskData, 1);
    data->dm = g_object_ref(dm);
    data->file_path = g_strdup(file_path);
    g_task_set_task_data(task, data, (GDestroyNotify)async_task_data_free);

    /* Check if file exists */
    GFile *file = g_file_new_for_path(file_path);
    if (!g_file_query_exists(file, cancellable)) {
        g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_NOT_FOUND,
                               "File does not exist: %s", file_path);
        g_object_unref(file);
        g_object_unref(task);
        return;
    }

    /* Show loading status */
    if (dm->window) {
        GtkApplication *app = gtk_window_get_application(dm->window);
        if (app) {
            status_manager_update_async_operation(app, _("Opening"), file_path, TRUE);
        }
    }

    /* Start async file loading */
    g_file_load_contents_async(file, cancellable, document_manager_open_async_complete, task);
    g_object_unref(file);
}

gboolean document_manager_open_finish(DocumentManager *dm, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(G_IS_TASK(result), FALSE);

    return g_task_propagate_boolean(G_TASK(result), error);
}

/* Async save completion in main thread */
static void document_manager_save_async_complete(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GFile *file = G_FILE(source_object);
    GTask *task = G_TASK(user_data);
    AsyncTaskData *data = g_task_get_task_data(task);
    DocumentManager *dm = data->dm;

    GError *error = NULL;

    /* Check save result */
    if (!g_file_replace_contents_finish(file, result, NULL, &error)) {
        /* Show error status */
        if (dm->window) {
            GtkApplication *app = gtk_window_get_application(dm->window);
            if (app) {
                status_manager_update_async_operation(app, _("Saving"), dm->file_path, FALSE);
                g_autofree gchar *error_msg = g_strdup_printf(_("Failed to save %s"),
                                                            g_path_get_basename(dm->file_path));
                status_manager_show_toast(app, error_msg, 4);
            }
        }

        /* Map GIO errors to our domain */
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)) {
            g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_READONLY,
                                   "File is read-only: %s", dm->file_path);
        } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_CANCELLED,
                                   "Save operation cancelled");
        } else {
            g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_IO,
                                   "Save failed: %s", error->message);
        }
        g_clear_error(&error);

        /* Restore state from SAVING */
        /* Error handling - keep existing behavior */
        g_object_unref(task);
        return;
    }

    /* Save successful - update DocumentManager state */

    /* Update original content */
    update_original_content(dm);

    /* Update file metadata for external change detection */
    document_external_update_metadata(dm);

    /* Setup file monitoring if not already active */
    document_external_setup_file_monitor(dm);

    /* If this was a draft, clean up the draft file and update state */
    if (dm->draft_path) {
        g_autoptr(GError) draft_error = NULL;
        if (!document_manager_remove_draft(dm->draft_path, &draft_error)) {
            g_warning("Failed to remove draft file: %s",
                     draft_error ? draft_error->message : "Unknown error");
        }
        g_clear_pointer(&dm->draft_path, g_free);
    }

    /* Clean up recovery file after successful save */
    if (dm->recovery_path) {
        document_manager_cleanup_recovery(dm->recovery_path);
        g_clear_pointer(&dm->recovery_path, g_free);
    }

    /* Update document status */
    dm->is_untitled = FALSE;
    doc_mark_loaded_or_new(&dm->doc_state);

    /* Finalize initialization to ensure buffer change detection is active */
    document_manager_finalize_initialization(dm);

    /* Show success status */
    if (dm->window) {
        GtkApplication *app = gtk_window_get_application(dm->window);
        if (app) {
            status_manager_update_async_operation(app, _("Saving"), dm->file_path, FALSE);
            g_autofree gchar *success_msg = g_strdup_printf(_("Saved %s"),
                                                          g_path_get_basename(dm->file_path));
            status_manager_show_toast(app, success_msg, 3);
        }
    }

    g_debug("Async save completed: %s", dm->file_path);

    /* Return success */
    g_task_return_boolean(task, TRUE);
    g_object_unref(task);
}

void document_manager_save_async(DocumentManager *dm, GCancellable *cancellable,
                                GAsyncReadyCallback callback, gpointer user_data)
{
    g_return_if_fail(dm != NULL);

    /* Check if we have a file path to save to */
    if (dm->is_untitled || !dm->file_path) {
        GTask *task = g_task_new(dm, cancellable, callback, user_data);
        g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_NOT_FOUND,
                               "No file path set - use save_as_async for untitled documents");
        g_object_unref(task);
        return;
    }

    /* Create task and data */
    GTask *task = g_task_new(dm, cancellable, callback, user_data);
    AsyncTaskData *data = g_new0(AsyncTaskData, 1);
    data->dm = g_object_ref(dm);
    data->file_path = g_strdup(dm->file_path);

    /* Get current buffer content as markdown */
    data->content = get_buffer_content_as_markdown(dm->buffer);
    if (!data->content) {
        g_task_return_new_error(task, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_IO,
                               "Failed to get buffer content for save");
        async_task_data_free(data);
        g_object_unref(task);
        return;
    }

    g_task_set_task_data(task, data, (GDestroyNotify)async_task_data_free);

    /* Set saving state */
    /* Saving state - UI can track via callbacks */

    /* Show saving status */
    if (dm->window) {
        GtkApplication *app = gtk_window_get_application(dm->window);
        if (app) {
            status_manager_update_async_operation(app, _("Saving"), dm->file_path, TRUE);
        }
    }

    /* Start async file save */
    GFile *file = NULL;
    if (dm->document_portal_uri) {
        file = g_file_new_for_uri(dm->document_portal_uri);
    } else {
        file = g_file_new_for_path(dm->file_path);
    }
    gsize content_len = strlen(data->content);

    g_debug("save_async: content length = %zu, first 50 chars: [%.50s]",
            content_len, data->content);

    /* Use atomic replace with proper flags - don't use GBytes, pass data directly */
    g_file_replace_contents_async(file, data->content, content_len,
                                 NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION,
                                 cancellable, document_manager_save_async_complete, task);

    g_object_unref(file);
}

gboolean document_manager_save_finish(DocumentManager *dm, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(G_IS_TASK(result), FALSE);

    return g_task_propagate_boolean(G_TASK(result), error);
}

void document_manager_save_as_async(DocumentManager *dm, const gchar *file_path,
                                   GCancellable *cancellable, GAsyncReadyCallback callback,
                                   gpointer user_data)
{
    g_return_if_fail(dm != NULL);
    g_return_if_fail(file_path != NULL);

    /* Set the new file path first */
    g_free(dm->file_path);
    dm->file_path = g_strdup(file_path);
    dm->is_untitled = FALSE;

    document_manager_refresh_portal_uri(dm);

    /* Delegate to regular save_async */
    document_manager_save_async(dm, cancellable, callback, user_data);
}

gboolean document_manager_save_as_finish(DocumentManager *dm, GAsyncResult *result, GError **error)
{
    return document_manager_save_finish(dm, result, error);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Core document operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

DocumentState document_manager_get_state(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, DOC_STATE_ERROR);
    /* Return simple clean/dirty state for backward compatibility */
    return doc_is_dirty(&dm->doc_state) ? DOC_STATE_DIRTY : DOC_STATE_CLEAN;
}

void document_manager_set_state_callback(DocumentManager *dm, 
                                        StateChangeCallback callback, 
                                        gpointer user_data)
{
    g_return_if_fail(dm != NULL);
    dm->state_callback = callback;
    dm->state_callback_data = user_data;
}

gboolean document_manager_has_unsaved_changes(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    return doc_is_dirty(&dm->doc_state);
}

const gchar* document_manager_get_file_path(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, NULL);
    return dm->file_path;
}

const gchar* document_manager_get_display_name(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, "");
    
    if (dm->file_path) {
        return g_path_get_basename(dm->file_path);
    }
    
    return _("Untitled");
}

gboolean document_manager_is_untitled(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, TRUE);
    return dm->is_untitled;
}

void document_manager_start_autosave(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    
    /* Check if autosave is enabled in settings */
    if (!g_settings_get_boolean(dm->settings, "autosave-enabled")) {
        g_debug("Autosave disabled in settings, not starting");
        return;
    }
    
    if (dm->autosave_id > 0) {
        return; /* Already running */
    }
    
    dm->autosave_id = g_timeout_add(AUTOSAVE_INTERVAL_MS, 
                                   autosave_timeout_cb, dm);
    
    /* Also start recovery snapshots */
    if (dm->recovery_id == 0) {
        dm->recovery_id = g_timeout_add(RECOVERY_INTERVAL_MS,
                                       recovery_timeout_cb, dm);
    }
    
    g_debug("Autosave started (interval: %ums)", AUTOSAVE_INTERVAL_MS);
}

void document_manager_stop_autosave(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    
    if (dm->autosave_id > 0) {
        g_source_remove(dm->autosave_id);
        dm->autosave_id = 0;
        g_debug("Autosave stopped");
    }
}

void document_manager_update_autosave_setting(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    
    gboolean autosave_enabled = g_settings_get_boolean(dm->settings, "autosave-enabled");
    
    if (autosave_enabled) {
        /* Start autosave if not already running */
        document_manager_start_autosave(dm);
        g_debug("Autosave enabled via settings change");
    } else {
        /* Stop autosave if running */
        document_manager_stop_autosave(dm);
        g_debug("Autosave disabled via settings change");
    }
}

gboolean document_manager_save(DocumentManager *dm, gboolean force_dialog, 
                              SaveCompleteCallback callback, gpointer user_data)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    
    /* If untitled or force dialog, show save as dialog */
    if (dm->is_untitled || force_dialog || !dm->file_path) {
        return document_manager_save_as(dm, NULL, callback, user_data);
    }
    
    /* Check if file is writable */
    if (!check_file_writable(dm->file_path)) {
        if (callback) {
            callback(dm, SAVE_RESULT_READONLY, 
                    _("File is read-only"), user_data);
        }
        return FALSE;
    }
    
    /* Perform the save */
    /* Saving state - UI can track via callbacks */
    
    g_autoptr(GError) error = NULL;
    if (atomic_write_file_from_buffer(dm->file_path, dm->buffer, &error)) {
        /* Save successful */
        update_original_content(dm);
        
        /* Update file metadata for external change detection */
        document_external_update_metadata(dm);
        
        /* Setup file monitoring if not already active */
        document_external_setup_file_monitor(dm);
        
        /* If this was a draft, clean up the draft file and update state */
        if (dm->draft_path) {
            g_autoptr(GError) draft_error = NULL;
            if (!document_manager_remove_draft(dm->draft_path, &draft_error)) {
                g_warning("Failed to remove draft file: %s", 
                         draft_error ? draft_error->message : "Unknown error");
            }
            g_clear_pointer(&dm->draft_path, g_free);
        }
        
        /* Clean up recovery file after successful save */
        if (dm->recovery_path) {
            document_manager_cleanup_recovery(dm->recovery_path);
            g_clear_pointer(&dm->recovery_path, g_free);
        }
        
        /* Update document status */
        dm->is_untitled = FALSE;
        doc_on_saved(&dm->doc_state);

        /* Save version history if enabled */
        g_autoptr(GError) version_error = NULL;
        if (!document_manager_save_version_history(dm, &version_error)) {
            g_warning("Failed to save version history: %s",
                     version_error ? version_error->message : "Unknown error");
            /* Don't fail the save operation for version history errors */
        }

        if (callback) {
            callback(dm, SAVE_RESULT_SUCCESS, NULL, user_data);
        }
        
        g_debug("Save completed: %s", dm->file_path);
        return TRUE;
    } else {
        /* Save failed */
        /* Error handling - keep existing behavior */
        
        if (callback) {
            callback(dm, SAVE_RESULT_ERROR, 
                    error ? error->message : _("Unknown error"), user_data);
        }
        
        g_warning("Save failed: %s", error ? error->message : "Unknown error");
        return FALSE;
    }
}

gboolean document_manager_save_as(DocumentManager *dm, const gchar *file_path,
                                 SaveCompleteCallback callback, gpointer user_data)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    
    /* If file_path is provided, use it directly. Otherwise, this will be 
     * handled by UI to show save dialog and call this function again with path */
    if (file_path) {
        /* Set the new file path */
        g_free(dm->file_path);
        dm->file_path = g_strdup(file_path);
        
        /* Mark as no longer untitled since we have a path now */
        dm->is_untitled = FALSE;
        
        /* Perform the save with the new path */
        return document_manager_save(dm, FALSE, callback, user_data);
    }
    
    /* TODO: Integrate with UI save dialog - for now just indicate dialog needed */
    g_debug("Save As requested - dialog integration needed");
    
    if (callback) {
        callback(dm, SAVE_RESULT_CANCELLED, _("Save As dialog needed"), user_data);
    }
    
    return FALSE;
}

/* Placeholder implementations for remaining API functions */
gboolean document_manager_open_file(DocumentManager *dm, const gchar *file_path, 
                                   GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(file_path != NULL, FALSE);
    
    /* Check if file exists and is readable */
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "File does not exist: %s", file_path);
        return FALSE;
    }
    
    /* Load file content into buffer using consolidated function */
    if (!load_content_into_buffer(dm, file_path, error)) {
        return FALSE;
    }

    /* Update document state */
    g_free(dm->file_path);
    dm->file_path = g_strdup(file_path);
    dm->is_untitled = FALSE;
    
    /* Clear any existing draft/recovery paths */
    g_clear_pointer(&dm->draft_path, g_free);
    g_clear_pointer(&dm->recovery_path, g_free);
    
    /* Update file metadata */
    document_external_update_metadata(dm);
    
    /* Setup file monitoring */
    document_external_setup_file_monitor(dm);
    
    /* Update original content and set clean state */
    update_original_content(dm);
    doc_mark_loaded_or_new(&dm->doc_state);

    g_debug("File opened successfully: %s", file_path);
    return TRUE;
}

/* Adopt current buffer for a given file path - avoids double file reading */
gboolean document_manager_adopt_current_buffer(DocumentManager *dm, const gchar *file_path,
                                               GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(file_path != NULL, FALSE);

    /* Check if file exists and is readable */
    if (!g_file_test(file_path, G_FILE_TEST_EXISTS)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "File does not exist: %s", file_path);
        return FALSE;
    }

    /* Block buffer signals during adoption to prevent false dirty state */
    document_manager_block_buffer_signals(dm);

    /* Don't re-read file, just update DocumentManager state */
    g_free(dm->file_path);
    dm->file_path = g_strdup(file_path);
    dm->is_untitled = FALSE;

    /* Clear any existing draft/recovery paths */
    g_clear_pointer(&dm->draft_path, g_free);
    g_clear_pointer(&dm->recovery_path, g_free);

    document_manager_refresh_portal_uri(dm);

    /* Update file metadata */
    document_external_update_metadata(dm);

    /* Setup file monitoring */
    document_external_setup_file_monitor(dm);

    /* Update original content and set clean state */
    update_original_content(dm);
    doc_mark_loaded_or_new(&dm->doc_state);

    /* Unblock buffer signals after adoption is complete */
    document_manager_unblock_buffer_signals(dm);

    /* Finalize initialization to enable buffer change detection */
    document_manager_finalize_initialization(dm);

    g_debug("Adopted current buffer for file: %s", file_path);
    return TRUE;
}

/* Deprecated shim maintained for compatibility */
gboolean document_manager_open_file_with_content(DocumentManager *dm, const gchar *file_path,
                                                const gchar *content, GError **error)
{
    (void)content; /* Unused – content already present in buffer */
    return document_manager_adopt_current_buffer(dm, file_path, error);
}

gboolean document_manager_save_draft(DocumentManager *dm, GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(dm->buffer != NULL, FALSE);
    
    /* Get current buffer content */
    g_autofree gchar *content = get_buffer_content_as_markdown(dm->buffer);
    if (!content) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to get buffer content for draft");
        return FALSE;
    }
    
    /* Create draft file */
    g_autofree gchar *draft_path = create_draft_file(content, error);
    if (!draft_path) {
        return FALSE;
    }
    
    /* Update document state if this is an untitled document */
    if (dm->is_untitled) {
        g_free(dm->draft_path);
        dm->draft_path = g_strdup(draft_path);
        /* Draft handling - simplified for now */
        
        /* Update original content to mark as "saved" to draft */
        update_original_content(dm);
    }
    
    g_debug("Draft saved successfully: %s", draft_path);
    return TRUE;
}

gboolean document_manager_discard_current_draft(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    
    if (dm->draft_path) {
        GError *error = NULL;
        if (!document_manager_remove_draft(dm->draft_path, &error)) {
            g_warning("Failed to remove draft file: %s", error ? error->message : "Unknown error");
            g_clear_error(&error);
            return FALSE;
        }
        
        g_debug("Draft discarded: %s", dm->draft_path);
        g_clear_pointer(&dm->draft_path, g_free);
        
        /* If this was a draft-only document, mark it as clean */
        /* For drafts, just mark as clean */
        doc_mark_loaded_or_new(&dm->doc_state);
    }
    
    return TRUE;
}

/* Public draft list/display functions moved to recovery_drafts.c */

gboolean document_manager_open_draft(DocumentManager *dm, const gchar *draft_path, 
                                    GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(draft_path != NULL, FALSE);
    
    /* Load draft content into buffer using consolidated function */
    if (!load_content_into_buffer(dm, draft_path, error)) {
        return FALSE;
    }

    /* Update document state */
    g_free(dm->draft_path);
    dm->draft_path = g_strdup(draft_path);
    dm->is_untitled = TRUE;
    /* Draft handling - simplified for now */
    
    /* Update original content */
    update_original_content(dm);
    
    g_debug("Draft opened: %s", draft_path);
    return TRUE;
}

/* Removal implemented in recovery_drafts.c */

/* Recovery list implemented in recovery_drafts.c */

/* Get recovery files for a specific document */
/* Recovery file filtering implemented in recovery_drafts.c */

/* Get display name for recovery file */
/* Recovery display implemented in recovery_drafts.c */

gboolean document_manager_recover_from_file(DocumentManager *dm, 
                                           const gchar *recovery_path, 
                                           GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(recovery_path != NULL, FALSE);
    
    /* Parse recovery file */
    g_autoptr(RecoveryInfo) info = document_recovery_parse_file(recovery_path, error);
    if (!info) {
        return FALSE;
    }
    
    /* Clear current buffer */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(dm->buffer, &start, &end);
    gtk_text_buffer_delete(dm->buffer, &start, &end);
    
    /* Load recovered content */
    if (info->content) {
        gtk_text_buffer_insert_at_cursor(dm->buffer, info->content, -1);
    }
    
    /* Update document state based on recovery info */
    if (info->original_path && strlen(info->original_path) > 0) {
        /* Recovered file had a path */
        g_free(dm->file_path);
        dm->file_path = g_strdup(info->original_path);
        dm->is_untitled = FALSE;
        /* Mark as dirty after recovery */
        doc_on_user_mutation(&dm->doc_state); /* Needs save to confirm recovery */
    } else {
        /* Recovered untitled document */
        g_free(dm->file_path);
        dm->file_path = NULL;
        dm->is_untitled = TRUE;
        
        if (info->draft_path && strlen(info->draft_path) > 0) {
            g_free(dm->draft_path);
            dm->draft_path = g_strdup(info->draft_path);
            /* Draft handling - simplified for now */
        } else {
            /* Mark as dirty after recovery */
        doc_on_user_mutation(&dm->doc_state);
        }
    }
    
    /* Update original content */
    update_original_content(dm);
    
    g_debug("Recovery completed from: %s", recovery_path);
    return TRUE;
}

/* Recovery cleanup implemented in recovery_drafts.c */

void document_manager_check_external_changes(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    
    /* Only check named files */
    if (!dm->file_path) return;
    
    /* Skip check if already in conflict state */
    /* Conflict detection simplified for now */
    
    /* Check if file has been modified externally */
    if (document_external_has_changed(dm)) {
        g_debug("Manual check detected external changes: %s", dm->file_path);
        
        /* Update document state */
        /* Conflict detection simplified for now */
        
        /* Show conflict resolution dialog */
        document_external_show_change_dialog(dm);
    }
}

gboolean document_manager_resolve_conflict(DocumentManager *dm, 
                                          gboolean use_external, 
                                          GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    /* Conflict detection simplified for now */
    g_return_val_if_fail(dm->file_path != NULL, FALSE);
    
    if (use_external) {
        /* Use external version - reload from file using consolidated function */
        if (!load_content_into_buffer(dm, dm->file_path, error)) {
            return FALSE;
        }
        
        /* Update metadata and state */
        document_external_update_metadata(dm);
        update_original_content(dm);
        doc_mark_loaded_or_new(&dm->doc_state);
        
        g_debug("Conflict resolved: using external version");
        return TRUE;
    } else {
        /* Use local version - save current content to file */
        gboolean save_result = document_manager_save(dm, FALSE, NULL, NULL);
        if (save_result) {
            /* Update metadata and resolve conflict */
            document_external_update_metadata(dm);
            doc_mark_loaded_or_new(&dm->doc_state);
            g_debug("Conflict resolved: using local version");
        }
        return save_result;
    }
}

/* Additional helper functions for external change detection */
gboolean document_manager_has_external_changes(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    
    /* Only named files can have external changes */
    if (!dm->file_path) return FALSE;
    
    return document_external_has_changed(dm);
}

gchar* document_manager_get_external_content(DocumentManager *dm, GError **error)
{
    g_return_val_if_fail(dm != NULL, NULL);
    g_return_val_if_fail(dm->file_path != NULL, NULL);

    return document_external_load_content(dm, error);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * BUFFER SIGNAL MANAGEMENT - Control buffer change detection
 * ═══════════════════════════════════════════════════════════════════════════════ */

void document_manager_block_buffer_signals(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);

    if (dm->buffer && dm->buffer_changed_handler_id > 0) {
        g_signal_handler_block(dm->buffer, dm->buffer_changed_handler_id);
        g_debug("DocumentManager buffer signals blocked");
    }
}

void document_manager_unblock_buffer_signals(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);

    if (dm->buffer && dm->buffer_changed_handler_id > 0) {
        g_signal_handler_unblock(dm->buffer, dm->buffer_changed_handler_id);
        g_debug("DocumentManager buffer signals unblocked");
    }
}

void document_manager_update_baseline(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);

    /* Update original content to match current buffer content */
    /* This is useful after markdown rendering to establish the rendered content as the baseline */
    update_original_content(dm);
    doc_mark_loaded_or_new(&dm->doc_state);
    g_debug("DocumentManager baseline updated to current buffer content - state set to CLEAN");
}

static void document_manager_finalize_initialization(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);

    dm->initialization_complete = TRUE;
    g_debug("DocumentManager initialization finalized - buffer change detection enabled");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * VERSION HISTORY SYSTEM - Save and restore document versions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Local helpers to avoid exposing internal details */
static gboolean dm_is_version_history_enabled(void)
{
    g_autoptr(GSettings) settings = g_settings_new("org.gtk.gtktext");
    return g_settings_get_boolean(settings, "version-history-enabled");
}

static gchar* dm_get_version_history_directory(void)
{
    const gchar *cache_dir;
    if (g_getenv("MESON_TEST_ITERATION")) cache_dir = "/tmp"; else cache_dir = g_get_user_cache_dir();
    gchar *versions_dir = g_build_filename(cache_dir, "gtktext", "versions", NULL);
    if (g_mkdir_with_parents(versions_dir, 0755) != 0) {
        g_warning("Failed to create version history directory: %s", versions_dir);
    }
    return versions_dir;
}

gboolean document_manager_save_version_history(DocumentManager *dm, GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(dm->file_path != NULL, FALSE);

    if (!dm_is_version_history_enabled()) {
        return TRUE; /* disabled */
    }

    /* Convert current buffer to markdown */
    g_autofree gchar *content = cm_render_buffer_to_markdown(dm->buffer);
    if (!content) {
        g_set_error(error, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_IO,
                   "Failed to get buffer content for version history");
        return FALSE;
    }
    if (g_utf8_strlen(content, -1) == 0) return TRUE;

    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree gchar *timestamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    g_autofree gchar *basename = g_path_get_basename(dm->file_path);
    g_autofree gchar *versions_dir = dm_get_version_history_directory();
    g_autofree gchar *filename = g_strdup_printf("%s-%s.version", basename, timestamp);
    g_autofree gchar *version_path = g_build_filename(versions_dir, filename, NULL);

    g_autoptr(GKeyFile) metadata = g_key_file_new();
    g_key_file_set_string(metadata, "Version", "OriginalPath", dm->file_path);
    g_key_file_set_int64(metadata, "Version", "Timestamp", g_date_time_to_unix(now));
    g_key_file_set_string(metadata, "Version", "Content", content);
    g_key_file_set_string(metadata, "Version", "OriginalBasename", basename);

    gsize data_length = 0;
    g_autofree gchar *data = g_key_file_to_data(metadata, &data_length, error);
    if (!data) return FALSE;
    if (!g_file_set_contents(version_path, data, data_length, error)) return FALSE;

    g_debug("Version history saved: %s", version_path);
    document_manager_cleanup_old_versions(dm->file_path);
    return TRUE;
}



/* Version save implemented above using dm_ helpers */

/* Version list is implemented in version_history.c */

/* Version display is implemented in version_history.c */

/* Restore document from version */
gboolean document_manager_restore_from_version(DocumentManager *dm,
                                              const gchar *version_path,
                                              GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(version_path != NULL, FALSE);

    g_autoptr(GKeyFile) metadata = g_key_file_new();

    if (!g_key_file_load_from_file(metadata, version_path, G_KEY_FILE_NONE, error)) {
        return FALSE;
    }

    g_autofree gchar *content = g_key_file_get_string(metadata, "Version", "Content", error);
    if (!content) {
        return FALSE;
    }

    /* Perform buffer replacement safely */
    document_manager_block_buffer_signals(dm);

    GtkTextView *text_view = NULL;
    gpointer view_data = g_object_get_data(G_OBJECT(dm->buffer), "gtktext-view");
    if (view_data && GTK_IS_TEXT_VIEW(view_data)) {
        text_view = GTK_TEXT_VIEW(view_data);
    }
#ifdef HAVE_LIBSOUP
    SoupSession *soup_session = NULL;
    gpointer soup_data = g_object_get_data(G_OBJECT(dm->buffer), "soup-session");
    if (soup_data) {
        soup_session = (SoupSession *)soup_data;
    }
#endif

    gboolean rendered = FALSE;

    /* Irreversible action to avoid building huge undo entries during restore */
    gtk_text_buffer_begin_irreversible_action(dm->buffer);

    if (text_view && GTK_IS_TEXT_VIEW(text_view)) {
#ifdef HAVE_LIBSOUP
        rendered = cm_render_markdown_to_buffer(dm->buffer, content, text_view, soup_session);
#else
        rendered = cm_render_markdown_to_buffer(dm->buffer, content, text_view, NULL);
#endif
        if (!rendered) {
            g_warning("Falling back to plain restore - markdown render failed");
        }
    }

    if (!rendered) {
        GtkTextIter start, end;
        render_set_suppress_reparse(dm->buffer, TRUE);
        gtk_text_buffer_get_bounds(dm->buffer, &start, &end);
        gtk_text_buffer_delete(dm->buffer, &start, &end);
        gtk_text_buffer_get_start_iter(dm->buffer, &start);
        gtk_text_buffer_insert(dm->buffer, &start, content, -1);
        render_set_suppress_reparse(dm->buffer, FALSE);
    }

    gtk_text_buffer_end_irreversible_action(dm->buffer);

    /* Update theme-dependent tags for consistency */
    theme_styles_update_theme_dependent_tags(dm->buffer);

    /* Unblock signals */
    document_manager_unblock_buffer_signals(dm);

    /* Mark as dirty after the main loop settles to avoid re-entrancy */
    g_object_ref(dm);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, idle_mark_dirty_cb, dm, NULL);

    g_debug("Document restored from version: %s", version_path);
    return TRUE;
}

/* Version cleanup is implemented in version_history.c */
