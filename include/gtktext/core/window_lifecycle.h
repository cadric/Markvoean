/* C ULTRA-MIN TEMPLATE
   Purpose: Window and application lifecycle management for GTK markdown editor
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - core/window_lifecycle.h
   Changed: Extracted window lifecycle management from main.c for better organization
*/

#ifndef GTKTEXT_CORE_WINDOW_LIFECYCLE_H
#define GTKTEXT_CORE_WINDOW_LIFECYCLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Window and application lifecycle functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Handle window close request with unsaved changes check
 * @param window The main window
 * @param user_data Pointer to the text view widget
 * @return TRUE to prevent closing, FALSE to allow closing
 */
gboolean window_lifecycle_on_window_close_request(GtkWindow *window, gpointer user_data);

/**
 * Handle widget map event for image embedding
 * @param widget The mapped widget
 * @param user_data User data (unused)
 */
void window_lifecycle_on_map(GtkWidget *widget, gpointer user_data);

/**
 * Handle window map event for welcome screen setup
 * @param window The main window
 * @param user_data User data (unused)
 */
void window_lifecycle_on_window_map(GtkWidget *window, gpointer user_data);

/**
 * Application activation callback
 * @param application The GTK application
 */
void window_lifecycle_app_activate(GApplication *application);

/**
 * Application open callback for handling file arguments
 * @param application The GTK application
 * @param files Array of files to open
 * @param n_files Number of files
 * @param hint Opening hint (unused)
 */
void window_lifecycle_app_open(GApplication *application, GFile **files, gint n_files,
                              const gchar *hint);

G_END_DECLS

#endif /* GTKTEXT_CORE_WINDOW_LIFECYCLE_H */