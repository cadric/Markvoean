/* [0.2.0] - 2025-09-15 - include/gtktext/document.h
 * Added: GObject type system for document management.
 */
#ifndef GTKTEXT_DOCUMENT_H
#define GTKTEXT_DOCUMENT_H

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GTKTEXT_TYPE_DOCUMENT (gtktext_document_get_type())
G_DECLARE_FINAL_TYPE(GtktextDocument, gtktext_document, GTKTEXT, DOCUMENT, GObject)

/**
 * GtktextDocument:
 * 
 * A GObject representing a markdown document with metadata and content.
 */

/**
 * gtktext_document_new:
 * 
 * Creates a new #GtktextDocument instance.
 * 
 * Returns: (transfer full): A new #GtktextDocument
 */
GtktextDocument *gtktext_document_new(void);

/**
 * gtktext_document_set_content:
 * @document: a #GtktextDocument
 * @content: the markdown content to set
 * 
 * Sets the markdown content of the document.
 */
void gtktext_document_set_content(GtktextDocument *document, const gchar *content);

/**
 * gtktext_document_get_content:
 * @document: a #GtktextDocument
 * 
 * Gets the markdown content of the document.
 * 
 * Returns: (transfer none): The markdown content
 */
const gchar *gtktext_document_get_content(GtktextDocument *document);

/**
 * gtktext_document_set_file_path:
 * @document: a #GtktextDocument
 * @file_path: (nullable): the file path to set
 * 
 * Sets the file path associated with the document.
 */
void gtktext_document_set_file_path(GtktextDocument *document, const gchar *file_path);

/**
 * gtktext_document_get_file_path:
 * @document: a #GtktextDocument
 * 
 * Gets the file path associated with the document.
 * 
 * Returns: (transfer none) (nullable): The file path or %NULL
 */
const gchar *gtktext_document_get_file_path(GtktextDocument *document);

/**
 * gtktext_document_set_modified:
 * @document: a #GtktextDocument
 * @modified: whether the document is modified
 * 
 * Sets the modified state of the document.
 */
void gtktext_document_set_modified(GtktextDocument *document, gboolean modified);

/**
 * gtktext_document_get_modified:
 * @document: a #GtktextDocument
 * 
 * Gets the modified state of the document.
 * 
 * Returns: %TRUE if the document is modified, %FALSE otherwise
 */
gboolean gtktext_document_get_modified(GtktextDocument *document);

G_END_DECLS

#endif /* GTKTEXT_DOCUMENT_H */
