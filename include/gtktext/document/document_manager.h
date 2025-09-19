/* C ULTRA-MIN TEMPLATE
   Purpose: Document lifecycle, autosave, and recovery management
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - document_manager.h
   MAJOR RELEASE: Complete DocumentManager implementation with all 6 phases
*/

#ifndef GTKTEXT_DOCUMENT_MANAGER_H
#define GTKTEXT_DOCUMENT_MANAGER_H

#include <gtk/gtk.h>
#include <adwaita.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Type definitions and enums
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    DOC_STATE_CLEAN,         /* No changes since last save */
    DOC_STATE_DIRTY,         /* Has unsaved changes */
    DOC_STATE_SAVING,        /* Currently saving */
    DOC_STATE_DRAFT,         /* Untitled document in drafts */
    DOC_STATE_READONLY,      /* Read-only file */
    DOC_STATE_CONFLICT,      /* External modification detected */
    DOC_STATE_ERROR          /* I/O error state */
} DocumentState;

typedef enum {
    SAVE_RESULT_SUCCESS,     /* Save completed successfully */
    SAVE_RESULT_ERROR,       /* Save failed with error */
    SAVE_RESULT_CANCELLED,   /* User cancelled save operation */
    SAVE_RESULT_READONLY,    /* File is read-only */
    SAVE_RESULT_CONFLICT     /* External modification conflict */
} SaveResult;

/* Forward declaration - full definition in .c file */
typedef struct _DocumentManager DocumentManager;

/* Callback types */
typedef void (*SaveCompleteCallback)(DocumentManager *dm, SaveResult result, 
                                   const gchar *error_message, gpointer user_data);
typedef void (*StateChangeCallback)(DocumentManager *dm, DocumentState old_state, 
                                   DocumentState new_state, gpointer user_data);

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Core document manager functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Lifecycle */
DocumentManager* document_manager_new(GtkTextBuffer *buffer, GtkWindow *window);
void document_manager_free(DocumentManager *dm);

/* Document operations */
gboolean document_manager_open_file(DocumentManager *dm, const gchar *file_path,
                                   GError **error);
gboolean document_manager_open_file_with_content(DocumentManager *dm, const gchar *file_path,
                                                const gchar *content, GError **error);
gboolean document_manager_save(DocumentManager *dm, gboolean force_dialog, 
                              SaveCompleteCallback callback, gpointer user_data);
gboolean document_manager_save_as(DocumentManager *dm, const gchar *file_path,
                                 SaveCompleteCallback callback, gpointer user_data);
void document_manager_start_autosave(DocumentManager *dm);
void document_manager_stop_autosave(DocumentManager *dm);
void document_manager_update_autosave_setting(DocumentManager *dm);

/* State management */
DocumentState document_manager_get_state(DocumentManager *dm);
void document_manager_set_state_callback(DocumentManager *dm,
                                        StateChangeCallback callback,
                                        gpointer user_data);
gboolean document_manager_has_unsaved_changes(DocumentManager *dm);
const gchar* document_manager_get_file_path(DocumentManager *dm);
const gchar* document_manager_get_display_name(DocumentManager *dm);
gboolean document_manager_is_untitled(DocumentManager *dm);

/* Buffer change signal management */
void document_manager_block_buffer_signals(DocumentManager *dm);
void document_manager_unblock_buffer_signals(DocumentManager *dm);

/* Content synchronization */
void document_manager_update_baseline(DocumentManager *dm);
void document_manager_finalize_initialization(DocumentManager *dm);

/* Recovery and drafts */
gboolean document_manager_save_draft(DocumentManager *dm, GError **error);
gboolean document_manager_discard_current_draft(DocumentManager *dm);
gchar** document_manager_list_drafts(void);
gchar* document_manager_get_draft_display_name(const gchar *draft_path);
gboolean document_manager_open_draft(DocumentManager *dm, const gchar *draft_path, 
                                    GError **error);
gboolean document_manager_remove_draft(const gchar *draft_path, GError **error);
gchar** document_manager_list_recovery_files(void);
gchar* document_manager_get_recovery_display_name(const gchar *recovery_path);
gboolean document_manager_recover_from_file(DocumentManager *dm, 
                                           const gchar *recovery_path, 
                                           GError **error);
void document_manager_cleanup_recovery(const gchar *recovery_path);

/* External change handling */
void document_manager_check_external_changes(DocumentManager *dm);
gboolean document_manager_resolve_conflict(DocumentManager *dm, 
                                          gboolean use_external, 
                                          GError **error);
gboolean document_manager_has_external_changes(DocumentManager *dm);
gchar* document_manager_get_external_content(DocumentManager *dm, GError **error);

/* ═══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS - Helper functions for atomic operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Atomic file operations */
gboolean atomic_write_file(const gchar *path, const gchar *content, 
                          gsize length, GError **error);
gboolean atomic_write_file_from_buffer(const gchar *path, GtkTextBuffer *buffer, 
                                      GError **error);

/* Directory management */
gchar* get_drafts_directory(void);
gchar* get_recovery_directory(void);
gchar* get_temp_directory(void);

/* File utilities */
gboolean check_file_writable(const gchar *path);
gboolean check_directory_writable(const gchar *path);
gint64 get_file_mtime(const gchar *path);
gchar* calculate_file_hash(const gchar *path, GError **error);

G_END_DECLS

#endif /* GTKTEXT_DOCUMENT_MANAGER_H */
