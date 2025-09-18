/* C ULTRA-MIN TEMPLATE
   Purpose: Window and application lifecycle management for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.1.0] - 2025-09-18 - core/window_lifecycle.c
   MAJOR: Fixed critical data loss - window close now checks ALL tabs for unsaved changes
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
#include <gtktext/core/window_size_manager.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/ui/dialogs.h>
#include <gtktext/editor/buffer_manager.h>
#include <gtktext/ui/tab_manager.h>

#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Window and application lifecycle functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean window_lifecycle_on_window_close_request(GtkWindow *window, gpointer user_data)
{
    (void)user_data; /* Parameter required for signal signature but unused */
    /* Check if we're closing without dialog (to prevent recursion) */
    gpointer closing_flag = g_object_get_data(G_OBJECT(window), "closing-without-dialog");
    if (closing_flag) {
        g_object_set_data(G_OBJECT(window), "closing-without-dialog", NULL);
        return FALSE; /* Allow close */
    }

    g_debug("window_lifecycle_on_window_close_request: checking for unsaved changes across all tabs");

    /* Get the tab manager from the application */
    GtkApplication *app = gtk_window_get_application(window);
    TabManager *tab_manager = gtktext_get_tab_manager(app);

    if (!tab_manager) {
        g_warning("window_lifecycle_on_window_close_request: Failed to get tab manager");
        return FALSE;
    }

    /* Check if ANY tab has unsaved changes */
    if (tab_manager_has_any_unsaved_changes(tab_manager)) {
        g_message("Unsaved changes detected in one or more tabs, showing close confirmation dialog");

        /* Get first tab with unsaved changes for the dialog */
        AdwTabPage *page = tab_manager_get_first_unsaved_tab(tab_manager);
        if (page) {
            GtkWidget *text_view = tab_manager_get_text_view(tab_manager, page);
            if (text_view) {
                GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
                if (buffer) {
                    dialogs_show_unsaved_changes_dialog(window, buffer);
                    return TRUE; /* Prevent close until user decides */
                }
            }
        }
        return TRUE; /* Prevent close as fallback */
    }

    g_debug("window_lifecycle_on_window_close_request: no unsaved changes, proceeding with close");

    /* Save window state before closing */
    GSettings *settings = g_object_get_data(G_OBJECT(window), "settings");
    if (settings) {
        window_size_manager_save_state(window, settings);
    }

    /* Clean up signal connections for all tabs */
    tab_manager_cleanup_all_signals(tab_manager);

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