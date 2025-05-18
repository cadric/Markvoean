#ifndef GTKTEXT_CMARK_H
#define GTKTEXT_CMARK_H

#include <cmark.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Import Markdown from a string to a GtkTextBuffer using cmark
 * 
 * @param buffer The GtkTextBuffer to import into
 * @param markdown The markdown text to import
 * @return TRUE if import was successful, FALSE otherwise
 */
gboolean import_markdown_to_buffer_cmark(GtkTextBuffer *buffer, const char *markdown);

/**
 * Export Markdown from a GtkTextBuffer to a string
 * 
 * @param buffer The GtkTextBuffer to export from
 * @return A newly allocated string with the markdown content (caller must free)
 */
char *export_buffer_to_markdown_cmark(GtkTextBuffer *buffer);

/**
 * Update code-related tags to match the current theme
 * 
 * @param buffer The GtkTextBuffer to update tags in
 */
void update_code_tags_for_theme(GtkTextBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif // GTKTEXT_CMARK_H
