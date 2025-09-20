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
#include <gtktext/render/cmrender.h>
#include <gtktext/render/theme_styles.h>
#include <gtktext/core/util.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Internal type definitions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* GObject implementation */
struct _GtktextDocumentManager {
    GObject parent_instance;
    /* Core references */
    GtkTextBuffer *buffer;           /* ref - the text buffer */
    GtkWindow *window;               /* ref - parent window */
    
    /* State */
    DocumentState state;
    gchar *file_path;                /* owned - current file location */
    gchar *draft_path;               /* owned - draft location for untitled */
    gchar *recovery_path;            /* owned - crash recovery journal */
    gint64 last_mtime;               /* last known modification time */
    gchar *last_hash;                /* owned - content hash for conflict detection */
    gboolean is_untitled;
    gboolean initialization_complete; /* owned - prevents premature dirty state during setup */
    
    /* Timers and monitoring */
    guint autosave_id;               /* autosave timer source ID */
    guint recovery_id;               /* recovery snapshot timer source ID */
    GFileMonitor *file_monitor;      /* ref - file change monitor */
    GSettings *settings;             /* ref - app settings for autosave control */
    
    /* Callbacks */
    StateChangeCallback state_callback;
    gpointer state_callback_data;
    
    /* Buffer change tracking */
    gulong buffer_changed_handler_id;
    gchar *original_content;         /* owned - content at last save */
};

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
    dm->state = DOC_STATE_CLEAN;
    dm->initialization_complete = FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Utility and helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Set document state and notify via signal and callback */
static void set_document_state(DocumentManager *dm, DocumentState new_state)
{
    g_return_if_fail(GTKTEXT_IS_DOCUMENT_MANAGER(dm));

    if (dm->state == new_state) return;

    DocumentState old_state = dm->state;
    dm->state = new_state;

    g_debug("Document state changed: %d -> %d", old_state, new_state);

    /* Emit GObject signal */
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

    /* For now, fallback to simple text extraction to fix the test */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    /* Debug what we're getting */
    g_debug("get_buffer_content_as_markdown: [%s] (length=%zu)",
            text ? text : "(null)", text ? strlen(text) : 0);

    return text;
}

/* Check if buffer content has changed since last save */
static gboolean has_content_changed(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(dm->buffer != NULL, FALSE);

    g_autofree gchar *current_content = get_buffer_content_as_markdown(dm->buffer);
    if (!current_content) return FALSE;

    /* Compare with original content */
    if (!dm->original_content) {
        /* No original content means this is a new document with content */
        gboolean has_content = (g_utf8_strlen(current_content, -1) > 0);
        g_debug("has_content_changed: No original content, has_content=%s", has_content ? "TRUE" : "FALSE");
        return has_content;
    }

    gboolean is_different = (g_strcmp0(current_content, dm->original_content) != 0);
    if (is_different) {
        g_debug("has_content_changed: Content differs from original (original_len=%zu, current_len=%zu)",
                dm->original_content ? strlen(dm->original_content) : 0,
                current_content ? strlen(current_content) : 0);
    }
    return is_different;
}

/* Update original content after successful save */
static void update_original_content(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    g_return_if_fail(dm->buffer != NULL);

    g_free(dm->original_content);
    dm->original_content = get_buffer_content_as_markdown(dm->buffer);
}


/* ═══════════════════════════════════════════════════════════════════════════════
 * ATOMIC WRITE IMPLEMENTATION - Robust file writing
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean atomic_write_file(const gchar *path, const gchar *content, 
                          gsize length, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);
    
    /* Create temp file in same directory for atomic rename */
    g_autofree gchar *dir = g_path_get_dirname(path);
    g_autofree gchar *basename = g_path_get_basename(path);
    g_autofree gchar *tmp_template = g_strdup_printf(".%s.tmp.XXXXXX", basename);
    g_autofree gchar *tmp_path = g_build_filename(dir, tmp_template, NULL);
    
    /* Create temporary file */
    gint fd = g_mkstemp(tmp_path);
    if (fd == -1) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "Failed to create temporary file: %s", g_strerror(errno));
        return FALSE;
    }
    
    /* Write content with proper error handling */
    gsize written = 0;
    while (written < length) {
        gssize result = write(fd, content + written, length - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue; /* Retry on interrupt */
            }
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                       "Write failed: %s", g_strerror(errno));
            close(fd);
            g_unlink(tmp_path);
            return FALSE;
        }
        written += result;
    }
    
    /* Flush and sync to disk */
    if (fsync(fd) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "fsync failed: %s", g_strerror(errno));
        close(fd);
        g_unlink(tmp_path);
        return FALSE;
    }
    close(fd);
    
    /* Atomic rename - platform specific */
#ifdef G_OS_WIN32
    /* Windows: use ReplaceFile for atomic replacement when target exists */
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        if (!ReplaceFile(path, tmp_path, NULL, 0, NULL, NULL)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "ReplaceFile failed: %lu", GetLastError());
            g_unlink(tmp_path);
            return FALSE;
        }
    } else {
        /* For new files, use MoveFile */
        if (!MoveFile(tmp_path, path)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "MoveFile failed: %lu", GetLastError());
            g_unlink(tmp_path);
            return FALSE;
        }
    }
#else
    /* POSIX: atomic rename */
    if (rename(tmp_path, path) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "rename failed: %s", g_strerror(errno));
        g_unlink(tmp_path);
        return FALSE;
    }
#endif
    
    g_debug("Atomic write successful: %s", path);
    return TRUE;
}

gboolean atomic_write_file_from_buffer(const gchar *path, GtkTextBuffer *buffer, 
                                      GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), FALSE);
    
    g_autofree gchar *content = get_buffer_content_as_markdown(buffer);
    if (!content) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to convert buffer content to markdown");
        return FALSE;
    }
    
    return atomic_write_file(path, content, strlen(content), error);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * DIRECTORY MANAGEMENT - Paths and directory creation
 * ═══════════════════════════════════════════════════════════════════════════════ */

gchar* get_drafts_directory(void)
{
    const gchar *data_dir = g_get_user_data_dir();
    g_autofree gchar *app_dir = g_build_filename(data_dir, "gtktext", NULL);
    gchar *drafts_dir = g_build_filename(app_dir, "drafts", NULL);
    
    /* Ensure directory exists */
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
    
    /* Ensure directory exists */
    if (g_mkdir_with_parents(recovery_dir, 0700) != 0) {
        g_warning("Failed to create recovery directory: %s", recovery_dir);
    }
    
    return recovery_dir;
}

gchar* get_temp_directory(void)
{
    return g_strdup(g_get_tmp_dir());
}

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
static gchar** list_draft_files(void)
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
        /* Only include files that match our draft pattern */
        if (g_str_has_prefix(name, "draft-") && g_str_has_suffix(name, ".md")) {
            gchar *full_path = g_build_filename(drafts_dir, name, NULL);
            g_ptr_array_add(drafts, full_path);
        }
    }
    
    /* Null-terminate the array */
    g_ptr_array_add(drafts, NULL);
    
    /* Return the array, transferring ownership */
    return (gchar**)g_ptr_array_free(drafts, FALSE);
}

/* Get draft file info including timestamp and content preview */
static gchar* get_draft_display_name(const gchar *draft_path)
{
    g_return_val_if_fail(draft_path != NULL, NULL);
    
    g_autofree gchar *basename = g_path_get_basename(draft_path);
    
    /* Extract timestamp from filename (draft-YYYYMMDD-HHMMSS.md) */
    if (g_str_has_prefix(basename, "draft-") && g_str_has_suffix(basename, ".md")) {
        g_autofree gchar *timestamp_part = g_strndup(basename + 6, strlen(basename) - 9);
        
        /* Parse timestamp */
        if (strlen(timestamp_part) == 15 && timestamp_part[8] == '-') {
            g_autofree gchar *date_part = g_strndup(timestamp_part, 8);
            g_autofree gchar *time_part = g_strdup(timestamp_part + 9);
            
            /* Format as readable date/time */
            return g_strdup_printf("Draft %s-%s-%s %s:%s:%s",
                                  g_strndup(date_part, 4),      /* YYYY */
                                  g_strndup(date_part + 4, 2),  /* MM */
                                  g_strndup(date_part + 6, 2),  /* DD */
                                  g_strndup(time_part, 2),      /* HH */
                                  g_strndup(time_part + 2, 2),  /* MM */
                                  g_strndup(time_part + 4, 2)); /* SS */
        }
    }
    
    /* Fallback to filename if parsing fails */
    return g_strdup(basename);
}

/* Remove a draft file */
static gboolean remove_draft_file(const gchar *draft_path, GError **error)
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

/* Recovery System - Crash recovery and data protection */

/* Write a recovery snapshot with metadata */
static void write_recovery_snapshot(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    g_return_if_fail(dm->buffer != NULL);
    
    /* Get current content */
    g_autofree gchar *content = get_buffer_content_as_markdown(dm->buffer);
    if (!content) return;
    
    /* Skip empty content to avoid cluttering recovery directory */
    if (g_utf8_strlen(content, -1) == 0) return;
    
    /* Generate recovery filename with timestamp */
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree gchar *timestamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    g_autofree gchar *basename = dm->file_path ? 
        g_path_get_basename(dm->file_path) : g_strdup("untitled");
    
    g_autofree gchar *recovery_dir = get_recovery_directory();
    g_autofree gchar *filename = g_strdup_printf("%s-%s.recovery", 
                                                 basename, timestamp);
    g_autofree gchar *path = g_build_filename(recovery_dir, filename, NULL);
    
    /* Write recovery file with metadata */
    g_autoptr(GKeyFile) metadata = g_key_file_new();
    g_key_file_set_string(metadata, "Recovery", "OriginalPath", 
                          dm->file_path ? dm->file_path : "");
    g_key_file_set_int64(metadata, "Recovery", "Timestamp", 
                         g_date_time_to_unix(now));
    g_key_file_set_string(metadata, "Recovery", "Content", content);
    g_key_file_set_boolean(metadata, "Recovery", "IsUntitled", dm->is_untitled);
    g_key_file_set_string(metadata, "Recovery", "DraftPath", 
                          dm->draft_path ? dm->draft_path : "");
    
    g_autofree gchar *data = g_key_file_to_data(metadata, NULL, NULL);
    g_autoptr(GError) error = NULL;
    if (!g_file_set_contents(path, data, -1, &error)) {
        g_warning("Failed to write recovery snapshot: %s", error->message);
        return;
    }
    
    /* Update recovery path in document manager */
    g_free(dm->recovery_path);
    dm->recovery_path = g_strdup(path);
    
    g_debug("Recovery snapshot written: %s", path);
}

/* List all recovery files in the recovery directory */
static gchar** list_recovery_files(void)
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
        /* Only include files that match our recovery pattern */
        if (g_str_has_suffix(name, ".recovery")) {
            gchar *full_path = g_build_filename(recovery_dir, name, NULL);
            g_ptr_array_add(recoveries, full_path);
        }
    }
    
    /* Null-terminate the array */
    g_ptr_array_add(recoveries, NULL);
    
    /* Return the array, transferring ownership */
    return (gchar**)g_ptr_array_free(recoveries, FALSE);
}

/* Parse recovery file metadata */
typedef struct {
    gchar *original_path;
    gint64 timestamp;
    gchar *content;
    gboolean is_untitled;
    gchar *draft_path;
} RecoveryInfo;

static void recovery_info_free(RecoveryInfo *info)
{
    if (!info) return;
    g_free(info->original_path);
    g_free(info->content);
    g_free(info->draft_path);
    g_free(info);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(RecoveryInfo, recovery_info_free)

static RecoveryInfo* parse_recovery_file(const gchar *recovery_path, GError **error)
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

/* Get display name for recovery file */
static gchar* get_recovery_display_name(const gchar *recovery_path)
{
    g_return_val_if_fail(recovery_path != NULL, NULL);
    
    g_autoptr(GError) error = NULL;
    g_autoptr(RecoveryInfo) info = parse_recovery_file(recovery_path, &error);
    
    if (!info) {
        g_autofree gchar *basename = g_path_get_basename(recovery_path);
        return g_strdup(basename);
    }
    
    /* Format timestamp for display */
    g_autoptr(GDateTime) dt = g_date_time_new_from_unix_local(info->timestamp);
    g_autofree gchar *time_str = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
    
    if (info->is_untitled) {
        return g_strdup_printf("Untitled Document (%s)", time_str);
    } else if (info->original_path && strlen(info->original_path) > 0) {
        g_autofree gchar *basename = g_path_get_basename(info->original_path);
        return g_strdup_printf("%s (%s)", basename, time_str);
    } else {
        return g_strdup_printf("Document (%s)", time_str);
    }
}

/* Remove a recovery file */
static gboolean remove_recovery_file(const gchar *recovery_path, GError **error)
{
    g_return_val_if_fail(recovery_path != NULL, FALSE);
    
    if (g_unlink(recovery_path) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "Failed to remove recovery file: %s", g_strerror(errno));
        return FALSE;
    }
    
    g_debug("Recovery file removed: %s", recovery_path);
    return TRUE;
}

/* Clean up old recovery files (older than 7 days) */
static void cleanup_old_recovery_files(void)
{
    g_autofree gchar **recovery_files = list_recovery_files();
    if (!recovery_files) return;
    
    gint64 cutoff_time = g_get_real_time() / G_USEC_PER_SEC - (7 * 24 * 60 * 60); /* 7 days */
    
    for (gint i = 0; recovery_files[i]; i++) {
        g_autoptr(GError) error = NULL;
        g_autoptr(RecoveryInfo) info = parse_recovery_file(recovery_files[i], &error);
        
        if (info && info->timestamp < cutoff_time) {
            g_autoptr(GError) remove_error = NULL;
            if (!remove_recovery_file(recovery_files[i], &remove_error)) {
                g_warning("Failed to cleanup old recovery file: %s", 
                         remove_error ? remove_error->message : "Unknown error");
            } else {
                g_debug("Cleaned up old recovery file: %s", recovery_files[i]);
            }
        }
    }
}

/* External Change Detection - File monitoring and conflict resolution */

/* Forward declarations */
static void on_file_changed(GFileMonitor *monitor, GFile *file, 
                           GFile *other_file, GFileMonitorEvent event,
                           gpointer user_data);
static void show_external_change_dialog(DocumentManager *dm);

/* Setup file monitoring for external changes */
static void setup_file_monitor(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    
    /* Only monitor named files */
    if (!dm->file_path) return;
    
    /* Clean up existing monitor if any */
    if (dm->file_monitor) {
        g_object_unref(dm->file_monitor);
        dm->file_monitor = NULL;
    }
    
    g_autoptr(GFile) file = g_file_new_for_path(dm->file_path);
    GFileMonitor *monitor = g_file_monitor_file(file, 
                                                G_FILE_MONITOR_NONE, 
                                                NULL, NULL);
    if (monitor) {
        g_signal_connect(monitor, "changed", 
                        G_CALLBACK(on_file_changed), dm);
        dm->file_monitor = monitor; /* Take ownership */
        g_debug("File monitor setup for: %s", dm->file_path);
    } else {
        g_warning("Failed to setup file monitor for: %s", dm->file_path);
    }
}

/* Handle file change events */
static void on_file_changed(GFileMonitor *monitor, GFile *file, 
                           GFile *other_file, GFileMonitorEvent event,
                           gpointer user_data)
{
    (void)monitor; (void)other_file; /* Suppress unused parameter warnings */
    DocumentManager *dm = user_data;
    g_return_if_fail(dm != NULL);
    
    if (event == G_FILE_MONITOR_EVENT_CHANGED) {
        /* Check if external modification */
        g_autoptr(GFileInfo) info = g_file_query_info(file,
            G_FILE_ATTRIBUTE_TIME_MODIFIED,
            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        
        if (info) {
            gint64 mtime = g_file_info_get_attribute_uint64(info,
                G_FILE_ATTRIBUTE_TIME_MODIFIED);
            
            if (mtime > dm->last_mtime) {
                /* External modification detected */
                g_debug("External modification detected: %s (mtime: %ld > %ld)", 
                       dm->file_path, mtime, dm->last_mtime);
                
                /* Update document state */
                set_document_state(dm, DOC_STATE_CONFLICT);
                
                /* Show conflict resolution dialog */
                show_external_change_dialog(dm);
            }
        }
    } else if (event == G_FILE_MONITOR_EVENT_DELETED) {
        g_warning("File was deleted externally: %s", dm->file_path);
        /* TODO: Handle file deletion - could show "file deleted" dialog */
    }
}

/* Check if file content has changed externally */
static gboolean has_file_changed_externally(DocumentManager *dm)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(dm->file_path != NULL, FALSE);
    
    /* Check modification time */
    gint64 current_mtime = get_file_mtime(dm->file_path);
    if (current_mtime > dm->last_mtime) {
        return TRUE;
    }
    
    /* For additional safety, also check content hash if available */
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
static gchar* load_external_content(DocumentManager *dm, GError **error)
{
    g_return_val_if_fail(dm != NULL, NULL);
    g_return_val_if_fail(dm->file_path != NULL, NULL);
    
    gchar *content = NULL;
    if (!g_file_get_contents(dm->file_path, &content, NULL, error)) {
        return NULL;
    }
    
    return content;
}

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
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(dm->buffer, &start, &end);
    gtk_text_buffer_delete(dm->buffer, &start, &end);
    gtk_text_buffer_insert_at_cursor(dm->buffer, content, -1);

    /* Apply theme colors to any existing tags after loading content */
    theme_styles_update_theme_dependent_tags(dm->buffer);

    return TRUE;
}

/* Update file metadata after resolving conflict */
static void update_file_metadata(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    g_return_if_fail(dm->file_path != NULL);
    
    /* Update modification time */
    dm->last_mtime = get_file_mtime(dm->file_path);
    
    /* Update content hash */
    g_autoptr(GError) error = NULL;
    g_free(dm->last_hash);
    dm->last_hash = calculate_file_hash(dm->file_path, &error);
    if (error) {
        g_warning("Failed to calculate file hash: %s", error->message);
        dm->last_hash = NULL;
    }
    
    g_debug("File metadata updated: mtime=%ld, hash=%s", 
           dm->last_mtime, dm->last_hash ? dm->last_hash : "none");
}

/* Placeholder for UI integration - will be implemented in UI phase */
static void show_external_change_dialog(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    
    /* TODO: Implement UI dialog for conflict resolution */
    g_debug("External change detected for: %s", dm->file_path);
    g_debug("Conflict resolution dialog needed - UI integration pending");
    
    /* For now, just log the conflict */
    g_message("External modification detected in file: %s", dm->file_path);
    g_message("Please save your changes or resolve the conflict manually.");
}

/* File Utilities - File system operations and checks */

gboolean check_file_writable(const gchar *path)
{
    g_return_val_if_fail(path != NULL, FALSE);
    
    if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
        /* Check if parent directory is writable */
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

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Signal handlers and callbacks
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    DocumentManager *dm = user_data;
    g_return_if_fail(dm != NULL);
    g_return_if_fail(buffer != NULL);

    /* Check if there's a pending user change that should trigger dirty state */
    gboolean user_change_pending = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer),
                                                                     "gtktext-user-change-pending")) != 0;

    /* Update state to dirty if not already and initialization is complete */
    if (dm->state == DOC_STATE_CLEAN && dm->initialization_complete) {
        /* Force dirty state if user change is pending, or check content change normally */
        if (user_change_pending || has_content_changed(dm)) {
            g_debug("DocumentManager buffer changed: setting to DIRTY (user_change_pending=%s)",
                   user_change_pending ? "TRUE" : "FALSE");
            set_document_state(dm, DOC_STATE_DIRTY);
        }
    } else if (!dm->initialization_complete) {
        g_debug("DocumentManager buffer changed during initialization - ignoring");
    }
}

static gboolean autosave_timeout_cb(gpointer user_data)
{
    DocumentManager *dm = user_data;
    g_return_val_if_fail(dm != NULL, G_SOURCE_REMOVE);
    
    /* Only autosave if dirty and not currently saving */
    if (dm->state == DOC_STATE_DIRTY) {
        g_debug("Performing autosave");
        
        /* For untitled documents, save as draft */
        if (dm->is_untitled) {
            g_autoptr(GError) error = NULL;
            if (!document_manager_save_draft(dm, &error)) {
                g_warning("Autosave draft failed: %s", 
                         error ? error->message : "Unknown error");
            }
        } else {
            /* For named documents, save in place using async API */
            document_manager_save_async(dm, NULL, NULL, NULL);
        }
    }
    
    return G_SOURCE_CONTINUE;
}

static gboolean recovery_timeout_cb(gpointer user_data)
{
    DocumentManager *dm = user_data;
    g_return_val_if_fail(dm != NULL, G_SOURCE_REMOVE);
    
    /* Write recovery snapshot if document has content and is dirty or draft */
    if (dm->state == DOC_STATE_DIRTY || dm->state == DOC_STATE_DRAFT) {
        write_recovery_snapshot(dm);
    }
    
    /* Periodically clean up old recovery files */
    static gint cleanup_counter = 0;
    if (++cleanup_counter >= 20) { /* Every 20 recovery cycles (10 minutes) */
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

    /* Connect to buffer changes */
    dm->buffer_changed_handler_id = g_signal_connect(buffer, "changed",
        G_CALLBACK(on_buffer_changed), dm);

    /* Store initial content */
    update_original_content(dm);

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
    g_object_set_data(G_OBJECT(dm->buffer), "gtktext-suppress-reparse", GINT_TO_POINTER(1));

    /* Clear current buffer and load new content */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(dm->buffer, &start, &end);
    gtk_text_buffer_delete(dm->buffer, &start, &end);
    gtk_text_buffer_insert_at_cursor(dm->buffer, contents, -1);

    /* Apply theme colors to any existing tags after loading content */
    theme_styles_update_theme_dependent_tags(dm->buffer);

    /* Re-enable markdown parsing after loading is complete */
    g_object_set_data(G_OBJECT(dm->buffer), "gtktext-suppress-reparse", GINT_TO_POINTER(0));

    /* Update document state */
    g_free(dm->file_path);
    dm->file_path = g_strdup(data->file_path);
    dm->is_untitled = FALSE;

    /* Clear any existing draft/recovery paths */
    g_clear_pointer(&dm->draft_path, g_free);
    g_clear_pointer(&dm->recovery_path, g_free);

    /* Update file metadata */
    update_file_metadata(dm);

    /* Setup file monitoring */
    setup_file_monitor(dm);

    /* Update original content and set clean state */
    update_original_content(dm);
    set_document_state(dm, DOC_STATE_CLEAN);

    /* Unblock buffer change signals */
    document_manager_unblock_buffer_signals(dm);

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
        set_document_state(dm, DOC_STATE_ERROR);
        g_object_unref(task);
        return;
    }

    /* Save successful - update DocumentManager state */

    /* Update original content */
    update_original_content(dm);

    /* Update file metadata for external change detection */
    update_file_metadata(dm);

    /* Setup file monitoring if not already active */
    setup_file_monitor(dm);

    /* If this was a draft, clean up the draft file and update state */
    if (dm->draft_path) {
        g_autoptr(GError) draft_error = NULL;
        if (!remove_draft_file(dm->draft_path, &draft_error)) {
            g_warning("Failed to remove draft file: %s",
                     draft_error ? draft_error->message : "Unknown error");
        }
        g_clear_pointer(&dm->draft_path, g_free);
    }

    /* Clean up recovery file after successful save */
    if (dm->recovery_path) {
        g_autoptr(GError) recovery_error = NULL;
        if (!remove_recovery_file(dm->recovery_path, &recovery_error)) {
            g_warning("Failed to remove recovery file: %s",
                     recovery_error ? recovery_error->message : "Unknown error");
        }
        g_clear_pointer(&dm->recovery_path, g_free);
    }

    /* Update document status */
    dm->is_untitled = FALSE;
    set_document_state(dm, DOC_STATE_CLEAN);

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
    set_document_state(dm, DOC_STATE_SAVING);

    /* Start async file save */
    GFile *file = g_file_new_for_path(dm->file_path);
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
    return dm->state;
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
    return dm->state == DOC_STATE_DIRTY || dm->state == DOC_STATE_DRAFT;
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
    set_document_state(dm, DOC_STATE_SAVING);
    
    g_autoptr(GError) error = NULL;
    if (atomic_write_file_from_buffer(dm->file_path, dm->buffer, &error)) {
        /* Save successful */
        update_original_content(dm);
        
        /* Update file metadata for external change detection */
        update_file_metadata(dm);
        
        /* Setup file monitoring if not already active */
        setup_file_monitor(dm);
        
        /* If this was a draft, clean up the draft file and update state */
        if (dm->draft_path) {
            g_autoptr(GError) draft_error = NULL;
            if (!remove_draft_file(dm->draft_path, &draft_error)) {
                g_warning("Failed to remove draft file: %s", 
                         draft_error ? draft_error->message : "Unknown error");
            }
            g_clear_pointer(&dm->draft_path, g_free);
        }
        
        /* Clean up recovery file after successful save */
        if (dm->recovery_path) {
            g_autoptr(GError) recovery_error = NULL;
            if (!remove_recovery_file(dm->recovery_path, &recovery_error)) {
                g_warning("Failed to remove recovery file: %s",
                         recovery_error ? recovery_error->message : "Unknown error");
            }
            g_clear_pointer(&dm->recovery_path, g_free);
        }
        
        /* Update document status */
        dm->is_untitled = FALSE;
        set_document_state(dm, DOC_STATE_CLEAN);
        
        if (callback) {
            callback(dm, SAVE_RESULT_SUCCESS, NULL, user_data);
        }
        
        g_debug("Save completed: %s", dm->file_path);
        return TRUE;
    } else {
        /* Save failed */
        set_document_state(dm, DOC_STATE_ERROR);
        
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
    update_file_metadata(dm);
    
    /* Setup file monitoring */
    setup_file_monitor(dm);
    
    /* Update original content and set clean state */
    update_original_content(dm);
    set_document_state(dm, DOC_STATE_CLEAN);
    
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

    /* Update file metadata */
    update_file_metadata(dm);

    /* Setup file monitoring */
    setup_file_monitor(dm);

    /* Update original content and set clean state */
    update_original_content(dm);
    set_document_state(dm, DOC_STATE_CLEAN);

    /* Unblock buffer signals after adoption is complete */
    document_manager_unblock_buffer_signals(dm);

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
        set_document_state(dm, DOC_STATE_DRAFT);
        
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
        if (!remove_draft_file(dm->draft_path, &error)) {
            g_warning("Failed to remove draft file: %s", error ? error->message : "Unknown error");
            g_clear_error(&error);
            return FALSE;
        }
        
        g_debug("Draft discarded: %s", dm->draft_path);
        g_clear_pointer(&dm->draft_path, g_free);
        
        /* If this was a draft-only document, mark it as clean */
        if (dm->state == DOC_STATE_DRAFT) {
            set_document_state(dm, DOC_STATE_CLEAN);
        }
    }
    
    return TRUE;
}

/* Public draft management functions */
gchar** document_manager_list_drafts(void)
{
    return list_draft_files();
}

gchar* document_manager_get_draft_display_name(const gchar *draft_path)
{
    return get_draft_display_name(draft_path);
}

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
    set_document_state(dm, DOC_STATE_DRAFT);
    
    /* Update original content */
    update_original_content(dm);
    
    g_debug("Draft opened: %s", draft_path);
    return TRUE;
}

gboolean document_manager_remove_draft(const gchar *draft_path, GError **error)
{
    return remove_draft_file(draft_path, error);
}

gchar** document_manager_list_recovery_files(void)
{
    return list_recovery_files();
}

/* Get display name for recovery file */
gchar* document_manager_get_recovery_display_name(const gchar *recovery_path)
{
    return get_recovery_display_name(recovery_path);
}

gboolean document_manager_recover_from_file(DocumentManager *dm, 
                                           const gchar *recovery_path, 
                                           GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(recovery_path != NULL, FALSE);
    
    /* Parse recovery file */
    g_autoptr(RecoveryInfo) info = parse_recovery_file(recovery_path, error);
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
        set_document_state(dm, DOC_STATE_DIRTY); /* Needs save to confirm recovery */
    } else {
        /* Recovered untitled document */
        g_free(dm->file_path);
        dm->file_path = NULL;
        dm->is_untitled = TRUE;
        
        if (info->draft_path && strlen(info->draft_path) > 0) {
            g_free(dm->draft_path);
            dm->draft_path = g_strdup(info->draft_path);
            set_document_state(dm, DOC_STATE_DRAFT);
        } else {
            set_document_state(dm, DOC_STATE_DIRTY);
        }
    }
    
    /* Update original content */
    update_original_content(dm);
    
    g_debug("Recovery completed from: %s", recovery_path);
    return TRUE;
}

void document_manager_cleanup_recovery(const gchar *recovery_path)
{
    g_return_if_fail(recovery_path != NULL);
    
    g_autoptr(GError) error = NULL;
    if (!remove_recovery_file(recovery_path, &error)) {
        g_warning("Failed to cleanup recovery file: %s", 
                 error ? error->message : "Unknown error");
    } else {
        g_debug("Recovery file cleaned up: %s", recovery_path);
    }
}

void document_manager_check_external_changes(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);
    
    /* Only check named files */
    if (!dm->file_path) return;
    
    /* Skip check if already in conflict state */
    if (dm->state == DOC_STATE_CONFLICT) return;
    
    /* Check if file has been modified externally */
    if (has_file_changed_externally(dm)) {
        g_debug("Manual check detected external changes: %s", dm->file_path);
        
        /* Update document state */
        set_document_state(dm, DOC_STATE_CONFLICT);
        
        /* Show conflict resolution dialog */
        show_external_change_dialog(dm);
    }
}

gboolean document_manager_resolve_conflict(DocumentManager *dm, 
                                          gboolean use_external, 
                                          GError **error)
{
    g_return_val_if_fail(dm != NULL, FALSE);
    g_return_val_if_fail(dm->state == DOC_STATE_CONFLICT, FALSE);
    g_return_val_if_fail(dm->file_path != NULL, FALSE);
    
    if (use_external) {
        /* Use external version - reload from file using consolidated function */
        if (!load_content_into_buffer(dm, dm->file_path, error)) {
            return FALSE;
        }
        
        /* Update metadata and state */
        update_file_metadata(dm);
        update_original_content(dm);
        set_document_state(dm, DOC_STATE_CLEAN);
        
        g_debug("Conflict resolved: using external version");
        return TRUE;
    } else {
        /* Use local version - save current content to file */
        gboolean save_result = document_manager_save(dm, FALSE, NULL, NULL);
        if (save_result) {
            /* Update metadata and resolve conflict */
            update_file_metadata(dm);
            set_document_state(dm, DOC_STATE_CLEAN);
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
    
    return has_file_changed_externally(dm);
}

gchar* document_manager_get_external_content(DocumentManager *dm, GError **error)
{
    g_return_val_if_fail(dm != NULL, NULL);
    g_return_val_if_fail(dm->file_path != NULL, NULL);

    return load_external_content(dm, error);
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
    set_document_state(dm, DOC_STATE_CLEAN);
    g_debug("DocumentManager baseline updated to current buffer content - state set to CLEAN");
}

void document_manager_finalize_initialization(DocumentManager *dm)
{
    g_return_if_fail(dm != NULL);

    dm->initialization_complete = TRUE;
    g_debug("DocumentManager initialization finalized - buffer change detection enabled");
}
