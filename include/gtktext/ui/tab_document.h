/* Tab Document Header
 * Purpose: Per-tab document container for GTK markdown editor
 * [1.0.0] - 2025-09-17 - ui/tab_document.h
 * Created: Document container for tab-based editing
 */

#ifndef GTKTEXT_TAB_DOCUMENT_H
#define GTKTEXT_TAB_DOCUMENT_H

#include <gtk/gtk.h>
#include <gtktext/document/document.h>
#include <gtktext/document/document_manager.h>

G_BEGIN_DECLS

/* Phase 3: Forward declaration and callback type definition */
typedef struct _TabDocument TabDocument;
typedef void (*TabDocumentDirtyStateCallback)(TabDocument *td, gboolean is_dirty, gpointer user_data);

struct _TabDocument {
    /* Document model and management */
    GtktextDocument *document;          /* Document model (GObject) */
    DocumentManager *doc_manager;       /* Document operations */

    /* UI components */
    GtkWidget *text_view;              /* Editor widget */
    GtkWidget *container;             /* Container widget for tab content */
    GtkTextBuffer *buffer;            /* Text buffer */

    /* Tab metadata */
    gchar *file_path;                 /* Full file path */
    gchar *tab_title;                 /* Display title */
    gboolean is_dirty;                /* Unsaved changes */
    gboolean is_welcome;              /* Is this the welcome tab */
    gboolean being_destroyed;         /* Destruction in progress flag */

    /* Source view state - each tab tracks its own view mode */
    gboolean is_source_mode;          /* TRUE = source view, FALSE = WYSIWYG */
    GtkWidget *source_text_view;      /* Source view widget (when active) */
    GtkTextBuffer *source_buffer;     /* Source buffer (when active) */
    GtkWidget *scrolled_window;       /* Scrolled window container */
    GtkWidget *original_text_view;    /* Original WYSIWYG text view */

    /* Signal handlers */
    gulong buffer_changed_handler_id;

    /* Phase 3: State change callbacks */
    TabDocumentDirtyStateCallback dirty_state_callback;
    gpointer dirty_state_callback_data;
};

/* Factory and lifecycle */
TabDocument *tab_document_new(void);
TabDocument *tab_document_new_from_file(const char *file_path);
TabDocument *tab_document_new_welcome(void);
void tab_document_destroy(TabDocument *td);

/* Content operations */
gboolean tab_document_load_file(TabDocument *td, const char *file_path, GError **error);
gboolean tab_document_save(TabDocument *td, GError **error);
gboolean tab_document_save_as(TabDocument *td, const char *file_path, GError **error);

/* Property access */
const char *tab_document_get_display_title(TabDocument *td);
const char *tab_document_get_file_path(TabDocument *td);
gboolean tab_document_get_modified(TabDocument *td);
void tab_document_set_modified(TabDocument *td, gboolean modified);

/* UI access */
GtkWidget *tab_document_get_widget(TabDocument *td);
GtkTextBuffer *tab_document_get_buffer(TabDocument *td);
GtkWidget *tab_document_get_text_view(TabDocument *td);

/* Phase 3: State change callbacks */
void tab_document_set_dirty_state_callback(TabDocument *td,
                                           TabDocumentDirtyStateCallback callback,
                                           gpointer user_data);

/* Source view mode management - legacy API (deprecated) */
gboolean tab_document_get_source_mode(TabDocument *td);
void tab_document_set_source_mode_state(TabDocument *td, gboolean is_source_mode,
                                        GtkWidget *source_text_view, GtkTextBuffer *source_buffer,
                                        GtkWidget *scrolled_window, GtkWidget *original_text_view);
void tab_document_clear_source_mode_state(TabDocument *td);

/* Source view mode management - new stack-based API */
gboolean tab_document_switch_to_source_view(TabDocument *td);
gboolean tab_document_switch_to_wysiwyg_view(TabDocument *td);

G_END_DECLS

#endif /* GTKTEXT_TAB_DOCUMENT_H */