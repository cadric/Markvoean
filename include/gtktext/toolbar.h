/* [0.3.0] - 2025-09-15 - include/toolbar.h
 * Added: Source view toggle functionality for switching between WYSIWYG and raw markdown.
 */
#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a toolbar with formatting options for the text editor
 * 
 * Features:
 * - Bold, italic, code, and heading formatting buttons
 * - Horizontal rule insertion
 * - Source view toggle (WYSIWYG ↔ Raw Markdown)
 * 
 * The source view toggle allows users to switch between:
 * - WYSIWYG mode: Formatted text with inline rendering
 * - Source mode: Raw markdown text for direct editing
 * 
 * @param text_view The GtkTextView that will be affected by toolbar actions
 * @return The toolbar container widget with all formatting controls
 */
GtkWidget* create_toolbar(GtkWidget *text_view);

#ifdef __cplusplus
}
#endif

#endif // TOOLBAR_H