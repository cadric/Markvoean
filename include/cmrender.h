#ifndef CMRENDER_H
#define CMRENDER_H

#include <gtk/gtk.h>
#include <cmark.h> // For cmark_node, etc.

/**
 * @brief Renders CommonMark text into a GtkTextBuffer.
 *
 * Clears the buffer and then parses the markdown_text, applying
 * appropriate GtkTextTags for styling. Theme-dependent styling
 * for elements like code blocks is applied after parsing.
 *
 * @param buffer The GtkTextBuffer to render into.
 * @param markdown_text The CommonMark text to parse and render.
 * @return TRUE on success, FALSE on failure (e.g., invalid input, parse error).
 */
gboolean cm_render_markdown_to_buffer(GtkTextBuffer *buffer, const char *markdown_text);

/**
 * @brief Updates theme-dependent GtkTextTags in the buffer.
 *
 * This function should be called when the application theme changes
 * (e.g., light to dark mode) to ensure elements like code blocks
 * have appropriate styling. It assumes basic tags have already been
 * created.
 *
 * @param buffer The GtkTextBuffer whose tags need updating.
 */
void cm_render_update_theme_dependent_tags(GtkTextBuffer *buffer);

/**
 * @brief Exports the content of a GtkTextBuffer to a CommonMark string.
 *
 * (Placeholder for porting export_buffer_to_markdown_cmark logic)
 *
 * @param buffer The GtkTextBuffer to export.
 * @return A newly allocated string containing the Markdown representation.
 *         The caller is responsible for freeing this string with g_free().
 *         Returns an empty string if the buffer is NULL or empty.
 */
char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer);

#endif // CMRENDER_H
