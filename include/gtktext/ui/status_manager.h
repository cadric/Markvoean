/* C ULTRA-MIN TEMPLATE
   Purpose: Status bar and UI state management for GTK markdown editor
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - ui/status_manager.h
   Changed: Extracted status bar management from main.c for better organization
*/

#ifndef GTKTEXT_UI_STATUS_MANAGER_H
#define GTKTEXT_UI_STATUS_MANAGER_H

#include <gtk/gtk.h>
#include <gtktext/document/document_manager.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Status bar management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Update save status in the status bar
 * @param app Application instance
 * @param status Status message to display
 */
void status_manager_update_save_status(GtkApplication *app, const gchar *status);

/**
 * Update file location in the status bar
 * @param app Application instance
 * @param location File path to display
 */
void status_manager_update_file_location(GtkApplication *app, const gchar *location);

/**
 * Update status bar based on document state
 * @param app Application instance
 * @param state Document state
 * @param file_path Current file path (can be NULL)
 */
void status_manager_update_status_bar_for_state(GtkApplication *app, DocumentState state,
                                               const gchar *file_path);

/**
 * Public API for DocumentManager integration
 * @param app Application instance
 * @param state Document state
 * @param file_path Current file path (can be NULL)
 */
void gtktext_update_status_bar_for_document_state(GtkApplication *app,
                                                   DocumentState state,
                                                   const gchar *file_path);

G_END_DECLS

#endif /* GTKTEXT_UI_STATUS_MANAGER_H */