/* C ULTRA-MIN TEMPLATE
   Purpose: Application initialization and UI setup for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.8] - 2025-09-16 - core/app_initialization.h
   Created: Extracted application initialization from main.c for better organization
*/

#ifndef GTKTEXT_CORE_APP_INITIALIZATION_H
#define GTKTEXT_CORE_APP_INITIALIZATION_H

#include <glib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Application initialization functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * app_initialization_activate:
 * @application: The GApplication instance
 *
 * Initialize the main application window and all UI components.
 * This function handles the complete UI setup including window creation,
 * widget initialization, signal connections, and module setup.
 */
void app_initialization_activate(GApplication *application);

/**
 * app_initialization_open:
 * @application: The GApplication instance
 * @files: Array of GFile objects to open
 * @n_files: Number of files in the array
 * @hint: Optional hint for file opening behavior
 *
 * Handle opening files from command line or file manager.
 * Activates the application first, then opens the specified files.
 */
void app_initialization_open(GApplication *application, GFile **files, gint n_files,
                            const gchar *hint);

G_END_DECLS

#endif /* GTKTEXT_CORE_APP_INITIALIZATION_H */