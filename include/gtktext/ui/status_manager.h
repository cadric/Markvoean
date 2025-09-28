/* C ULTRA-MIN TEMPLATE
   Purpose: Status bar and UI state management for GTK markdown editor
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - ui/status_manager.h
   Changed: Extracted status bar management from main.c for better organization
*/

#pragma once

#ifndef GTKTEXT_UI_STATUS_MANAGER_H
#define GTKTEXT_UI_STATUS_MANAGER_H

#include <gtk/gtk.h>
#include <adwaita.h>
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

/**
 * Show a toast notification in the active window
 * @param app Application instance
 * @param message Toast message to display
 * @param timeout_seconds Timeout in seconds (0 for default)
 */
void status_manager_show_toast(GtkApplication *app, const gchar *message, int timeout_seconds);

/**
 * Show a loading toast for async operations
 * @param app Application instance
 * @param message Loading message to display
 * @return Toast widget that can be dismissed later
 */
AdwToast* status_manager_show_loading_toast(GtkApplication *app, const gchar *message);

/**
 * Update status for async file operations
 * @param app Application instance
 * @param operation_type Type of operation ("Opening", "Saving", etc.)
 * @param file_path File path being operated on
 * @param is_loading Whether operation is in progress
 */
void status_manager_update_async_operation(GtkApplication *app, const gchar *operation_type,
                                          const gchar *file_path, gboolean is_loading);

G_END_DECLS

#endif /* GTKTEXT_UI_STATUS_MANAGER_H */
