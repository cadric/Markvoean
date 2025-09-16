/* C ULTRA-MIN TEMPLATE
   Purpose: Markdown processing engine for real-time parsing and buffer management
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - render/markdown/markdown_engine.h
   Changed: Extracted markdown processing engine from main.c for better organization
*/

#ifndef GTKTEXT_RENDER_MARKDOWN_MARKDOWN_ENGINE_H
#define GTKTEXT_RENDER_MARKDOWN_MARKDOWN_ENGINE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Markdown processing engine functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Schedule markdown re-parsing with debouncing
 * @param buffer Text buffer to reparse
 * @param inserted_len Length of inserted text (for optimization)
 * @param at_iter Position where text was inserted
 */
void schedule_reparse_markdown(GtkTextBuffer *buffer, gint inserted_len, const GtkTextIter *at_iter);

/**
 * Handle buffer text insertion for real-time markdown conversion
 * @param buffer Text buffer
 * @param location Insertion location
 * @param text Inserted text
 * @param len Length of inserted text
 * @param user_data User data (unused)
 */
void markdown_engine_on_buffer_insert_text(GtkTextBuffer *buffer, GtkTextIter *location,
                                          gchar *text, gint len, gpointer user_data);

/**
 * Initialize markdown engine for a text buffer
 * @param buffer Text buffer to initialize
 * @param text_view Associated text view
 * @param soup_session HTTP session for remote images (can be NULL)
 */
void markdown_engine_initialize_buffer(GtkTextBuffer *buffer, GtkTextView *text_view,
                                     gpointer soup_session);

G_END_DECLS

#endif /* GTKTEXT_RENDER_MARKDOWN_MARKDOWN_ENGINE_H */