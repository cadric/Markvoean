/* Tab Document Header
 * Purpose: Per-tab document container for GTK markdown editor
 * [1.1.0] - 2025-09-17 - ui/tab_document.h
 * MAJOR RELEASE: Opaque API design with proper encapsulation
 */

#ifndef GTKTEXT_TAB_DOCUMENT_H
#define GTKTEXT_TAB_DOCUMENT_H

#include <gtk/gtk.h>
#include <glib.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Opaque types and callback definitions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations */
typedef struct _DocumentManager DocumentManager;

/* Opaque handle - internals hidden from users */
typedef struct _TabDocument TabDocument;

/* Dirty state change callback */
typedef void (*TabDocumentDirtyStateCallback)(TabDocument *td,
                                              gboolean is_dirty,
                                              gpointer user_data);

/* ═══════════════════════════════════════════════════════════════════════════════
 * LIFECYCLE - Creation and destruction
 * ═══════════════════════════════════════════════════════════════════════════════ */

TabDocument *tab_document_new(void);
TabDocument *tab_document_new_from_file(const char *file_path);
TabDocument *tab_document_new_welcome(void);
void tab_document_destroy(TabDocument *td);

/* ═══════════════════════════════════════════════════════════════════════════════
 * INTEGRATION - System integration
 * ═══════════════════════════════════════════════════════════════════════════════ */

void tab_document_initialize_document_manager(TabDocument *td, GtkWindow *window);

/* ═══════════════════════════════════════════════════════════════════════════════
 * FILE OPERATIONS - Loading and saving
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean tab_document_load_file(TabDocument *td, const char *file_path, GError **error);
gboolean tab_document_save(TabDocument *td, GError **error);

/* Note: Current save_as returns TRUE when async save *starts*, not when it completes */
G_GNUC_DEPRECATED_FOR(tab_document_save_as_async)
gboolean tab_document_save_as(TabDocument *td, const char *file_path, GError **error);

/* Synchronous save - blocks until completion */
gboolean tab_document_save_as_sync(TabDocument *td, const char *file_path, GError **error);

/* Asynchronous save - proper async API */
void tab_document_save_as_async(TabDocument *td,
                                const char *file_path,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean tab_document_save_as_finish(TabDocument *td, GAsyncResult *result, GError **error);

/* ═══════════════════════════════════════════════════════════════════════════════
 * PROPERTIES - Access to document properties
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Note: Returned strings are owned by TabDocument and valid until the document is destroyed */
const char *tab_document_get_display_title(TabDocument *td);
const char *tab_document_get_file_path(TabDocument *td);
gboolean tab_document_get_modified(TabDocument *td);
void tab_document_set_modified(TabDocument *td, gboolean modified);

/* ═══════════════════════════════════════════════════════════════════════════════
 * UI ACCESS - Widget access for embedding
 * ═══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *tab_document_get_widget(TabDocument *td);
GtkTextBuffer *tab_document_get_buffer(TabDocument *td);
GtkWidget *tab_document_get_text_view(TabDocument *td);

/* Internal accessors for fields needed by other modules */
gboolean tab_document_is_being_destroyed(TabDocument *td);
DocumentManager *tab_document_get_document_manager(TabDocument *td);

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE CALLBACKS - Dirty state change notifications
 * ═══════════════════════════════════════════════════════════════════════════════ */

void tab_document_set_dirty_state_callback(TabDocument *td,
                                           TabDocumentDirtyStateCallback callback,
                                           gpointer user_data);

/* ═══════════════════════════════════════════════════════════════════════════════
 * VIEW MODES - Modern API for view mode switching
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean tab_document_switch_to_source_view(TabDocument *td);
gboolean tab_document_switch_to_wysiwyg_view(TabDocument *td);

/* ═══════════════════════════════════════════════════════════════════════════════
 * LEGACY API - Deprecated functions (will be removed in v2.0)
 * ═══════════════════════════════════════════════════════════════════════════════ */

G_GNUC_DEPRECATED_FOR(tab_document_switch_to_source_view)
gboolean tab_document_get_source_mode(TabDocument *td);

G_GNUC_DEPRECATED_FOR(tab_document_switch_to_source_view)
void tab_document_set_source_mode_state(TabDocument *td, gboolean is_source_mode,
                                        GtkWidget *source_text_view, GtkTextBuffer *source_buffer,
                                        GtkWidget *scrolled_window, GtkWidget *original_text_view);

G_GNUC_DEPRECATED_FOR(tab_document_switch_to_wysiwyg_view)
void tab_document_clear_source_mode_state(TabDocument *td);

G_END_DECLS

#endif /* GTKTEXT_TAB_DOCUMENT_H */