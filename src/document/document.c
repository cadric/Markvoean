/* C ULTRA‑MIN TEMPLATE
   Purpose: GObject document type for managing markdown documents with metadata
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
/* [1.4.2] - 2025-09-19 - src/document.c
 * Changed: Simplified dialog handler using shared helper function
 */
#include "config.h"
#include <gtktext/document/document.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/render/theme_styles.h>
#include <gtktext/core/settings.h>
#include <gtktext/ui/file_actions.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif

/* ========== META ========== */
/* [0.2.0] - 2025-09-15 - src/document.c
 * Added: GObject type system implementation for document management.
 */

/* ========== TYPES ========== */
struct _GtktextDocument {
    GObject parent_instance;
    
    gchar *content;
    gchar *file_path;
    gboolean modified;
};

/* ========== STATE ========== */
G_DEFINE_TYPE(GtktextDocument, gtktext_document, G_TYPE_OBJECT)

typedef enum {
    PROP_0,
    PROP_CONTENT,
    PROP_FILE_PATH,
    PROP_MODIFIED,
    N_PROPS
} GtktextDocumentProperty;

static GParamSpec *properties[N_PROPS];

typedef enum {
    CONTENT_CHANGED,
    N_SIGNALS
} GtktextDocumentSignal;

static guint signals[N_SIGNALS];

/* ========== HELPERS ========== */
static void gtktext_document_finalize(GObject *object) {
    GtktextDocument *self = GTKTEXT_DOCUMENT(object);
    
    g_free(self->content);
    g_free(self->file_path);
    
    G_OBJECT_CLASS(gtktext_document_parent_class)->finalize(object);
}

static void gtktext_document_get_property(GObject *object,
                                          guint prop_id,
                                          GValue *value,
                                          GParamSpec *pspec) {
    GtktextDocument *self = GTKTEXT_DOCUMENT(object);
    
    switch (prop_id) {
    case PROP_CONTENT:
        g_value_set_string(value, self->content);
        break;
    case PROP_FILE_PATH:
        g_value_set_string(value, self->file_path);
        break;
    case PROP_MODIFIED:
        g_value_set_boolean(value, self->modified);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gtktext_document_set_property(GObject *object,
                                          guint prop_id,
                                          const GValue *value,
                                          GParamSpec *pspec) {
    GtktextDocument *self = GTKTEXT_DOCUMENT(object);
    
    switch (prop_id) {
    case PROP_CONTENT:
        gtktext_document_set_content(self, g_value_get_string(value));
        break;
    case PROP_FILE_PATH:
        gtktext_document_set_file_path(self, g_value_get_string(value));
        break;
    case PROP_MODIFIED:
        gtktext_document_set_modified(self, g_value_get_boolean(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* ========== HANDLERS ========== */
static void gtktext_document_class_init(GtktextDocumentClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    
    object_class->finalize = gtktext_document_finalize;
    object_class->get_property = gtktext_document_get_property;
    object_class->set_property = gtktext_document_set_property;
    
    /* Properties */
    properties[PROP_CONTENT] = g_param_spec_string("content",
                                                   "Content",
                                                   "The markdown content of the document",
                                                   "",
                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    
    properties[PROP_FILE_PATH] = g_param_spec_string("file-path",
                                                     "File Path",
                                                     "The file path associated with the document",
                                                     NULL,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    
    properties[PROP_MODIFIED] = g_param_spec_boolean("modified",
                                                     "Modified",
                                                     "Whether the document has been modified",
                                                     FALSE,
                                                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    
    g_object_class_install_properties(object_class, N_PROPS, properties);
    
    /* Signals */
    signals[CONTENT_CHANGED] = g_signal_new("content-changed",
                                           G_TYPE_FROM_CLASS(klass),
                                           G_SIGNAL_RUN_LAST,
                                           0,
                                           NULL, NULL,
                                           NULL,
                                           G_TYPE_NONE, 0);
}

static void gtktext_document_init(GtktextDocument *self) {
    self->content = g_strdup("");
    self->file_path = NULL;
    self->modified = FALSE;
}

/* ========== LIFECYCLE ========== */
GtktextDocument *gtktext_document_new(void) {
    return g_object_new(GTKTEXT_TYPE_DOCUMENT, NULL);
}

void gtktext_document_set_content(GtktextDocument *document, const gchar *content) {
    g_return_if_fail(GTKTEXT_IS_DOCUMENT(document));
    
    if (g_strcmp0(document->content, content) != 0) {
        g_free(document->content);
        document->content = g_strdup(content ? content : "");
        
        gtktext_document_set_modified(document, TRUE);
        g_object_notify_by_pspec(G_OBJECT(document), properties[PROP_CONTENT]);
        g_signal_emit(document, signals[CONTENT_CHANGED], 0);
    }
}

const gchar *gtktext_document_get_content(GtktextDocument *document) {
    g_return_val_if_fail(GTKTEXT_IS_DOCUMENT(document), NULL);
    return document->content;
}

void gtktext_document_set_file_path(GtktextDocument *document, const gchar *file_path) {
    g_return_if_fail(GTKTEXT_IS_DOCUMENT(document));
    
    if (g_strcmp0(document->file_path, file_path) != 0) {
        g_free(document->file_path);
        document->file_path = g_strdup(file_path);
        g_object_notify_by_pspec(G_OBJECT(document), properties[PROP_FILE_PATH]);
    }
}

const gchar *gtktext_document_get_file_path(GtktextDocument *document) {
    g_return_val_if_fail(GTKTEXT_IS_DOCUMENT(document), NULL);
    return document->file_path;
}

void gtktext_document_set_modified(GtktextDocument *document, gboolean modified) {
    g_return_if_fail(GTKTEXT_IS_DOCUMENT(document));
    
    if (document->modified != modified) {
        document->modified = modified;
        g_object_notify_by_pspec(G_OBJECT(document), properties[PROP_MODIFIED]);
    }
}

gboolean gtktext_document_get_modified(GtktextDocument *document) {
    g_return_val_if_fail(GTKTEXT_IS_DOCUMENT(document), FALSE);
    return document->modified;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * DOCUMENT EVENT HANDLERS - File dialog callbacks and document operations
 * ═══════════════════════════════════════════════════════════════════════════════ */


void document_handlers_on_open_file_dialog_finish(GObject *source_object, GAsyncResult *res,
                                                  gpointer user_data)
{
    GtkFileDialog *d = GTK_FILE_DIALOG(source_object);
    GError *finish_error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(d, res, &finish_error);
    if (finish_error) {
        if (g_error_matches(finish_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            g_debug("File dialog dismissed by user");
        } else {
            g_warning("File dialog finished with error: %s", finish_error->message);
        }
        g_clear_error(&finish_error);
        return;
    }
    if (!file) {
        g_debug("File dialog dismissed without selection");
        return;
    }

    GtkApplication *app = GTK_APPLICATION(user_data);
    GError *error = NULL;
    if (!file_action_handle_open_dialog_result(file, app, FALSE, &error)) {
        g_warning("Failed to open file: %s", error ? error->message : "Unknown error");
        g_clear_error(&error);
    }
}
