/* C ULTRA-MIN TEMPLATE
   Purpose: Buffer management and text event handling for GTK markdown editor
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - editor/buffer_manager.h
   Changed: Extracted buffer management from main.c for better organization
*/

#pragma once

#ifndef GTKTEXT_EDITOR_BUFFER_MANAGER_H
#define GTKTEXT_EDITOR_BUFFER_MANAGER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Buffer management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Handle text changes in the buffer
 * @param buffer Text buffer that changed
 * @param user_data User data (unused)
 */
void buffer_manager_on_text_changed(GtkTextBuffer *buffer, gpointer user_data);

/**
 * Check if buffer has unsaved changes using DocumentManager
 * @param buffer Text buffer to check
 * @return TRUE if buffer has unsaved changes
 */
gboolean buffer_manager_has_unsaved_changes(GtkTextBuffer *buffer);

/**
 * Initialize buffer manager for a text buffer
 * @param buffer Text buffer to manage
 * @param app Application instance
 */
void buffer_manager_initialize_buffer(GtkTextBuffer *buffer, GtkApplication *app);

G_END_DECLS

#endif /* GTKTEXT_EDITOR_BUFFER_MANAGER_H */
