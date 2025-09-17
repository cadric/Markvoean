/* C ULTRA-MIN TEMPLATE
   Purpose: Window and application lifecycle management for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - core/window_lifecycle.c
   Changed: Extracted window lifecycle management from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <adwaita.h>
#include <gtktext/core/window_lifecycle.h>
#include <gtktext/core/settings_manager.h>
#include <gtktext/core/app_initialization.h>
#include <gtktext/core/signal_manager.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/ui/dialogs.h>
#include <gtktext/editor/buffer_manager.h>

#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Window and application lifecycle functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean window_lifecycle_on_window_close_request(GtkWindow *window, gpointer user_data)
{
    /* Check if we're closing without dialog (to prevent recursion) */
    gpointer closing_flag = g_object_get_data(G_OBJECT(window), "closing-without-dialog");
    if (closing_flag) {
        g_object_set_data(G_OBJECT(window), "closing-without-dialog", NULL);
        return FALSE; /* Allow close */
    }

    if (!user_data) {
        g_warning("window_lifecycle_on_window_close_request: Invalid text_view (user_data).");
        return FALSE;
    }
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);

    if (!buffer) {
        g_warning("window_lifecycle_on_window_close_request: Failed to get valid buffer from text_view.");
        return FALSE;
    }

    g_debug("window_lifecycle_on_window_close_request: checking for unsaved changes");

    /* Check if there are unsaved changes */
    if (buffer_manager_has_unsaved_changes(buffer)) {
        g_message("Unsaved changes detected, showing save dialog");
        dialogs_show_unsaved_changes_dialog(window, buffer);
        return TRUE; /* Prevent close until user decides */
    }

    g_debug("window_lifecycle_on_window_close_request: no unsaved changes, proceeding with close");

    /* Clean up signal connections through proper signal manager */
    SignalManager *sm = gtktext_get_signal_manager();
    if (sm) {
        g_debug("window_lifecycle_on_window_close_request: disconnecting buffer signals");
        signal_manager_disconnect_buffer_changed(sm, buffer);
    }

    return FALSE; /* Allow close */
}

void window_lifecycle_on_map(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    g_message("Main window mapped, image widgets already embedded during rendering.");
}

void window_lifecycle_on_window_map(GtkWidget *window, gpointer user_data)
{
    (void)user_data;
    g_message("Window mapped, setting welcome screen visibility.");
    /* Show welcome screen after window is mapped */
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(window));
    if (!app) {
        g_warning("Failed to get application from window in on_window_map");
        return;
    }

    /* Welcome screen now handled by tab system */
}

void window_lifecycle_app_activate(GApplication *application)
{
    /* Delegate to the specialized application initialization module */
    app_initialization_activate(application);
}

void window_lifecycle_app_open(GApplication *application, GFile **files, gint n_files,
                              const gchar *hint)
{
    /* Delegate to the specialized application initialization module */
    app_initialization_open(application, files, n_files, hint);
}