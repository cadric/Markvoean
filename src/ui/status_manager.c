/* C ULTRA-MIN TEMPLATE
   Purpose: Status bar and UI state management for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - ui/status_manager.c
   Changed: Extracted status bar management from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/ui/status_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void
update_save_status(GtkApplication *app, const gchar *status)
{
    GtkWindow *main_window;
    GtkWidget *save_status_label;

    g_return_if_fail(GTK_IS_APPLICATION(app));
    g_return_if_fail(status != NULL);

    main_window = gtk_application_get_active_window(app);
    if (!main_window) {
        g_warning("No active window found");
        return;
    }

    save_status_label = g_object_get_data(G_OBJECT(app), "save_status");
    if (!save_status_label) {
        g_warning("Save status label not found");
        return;
    }

    gtk_label_set_text(GTK_LABEL(save_status_label), status);
    g_debug("Updated save status: %s", status);
}

static void
update_file_location(GtkApplication *app, const gchar *location)
{
    GtkWindow *main_window;
    GtkWidget *file_location_label;
    const gchar *display_text;

    g_return_if_fail(GTK_IS_APPLICATION(app));

    main_window = gtk_application_get_active_window(app);
    if (!main_window) {
        g_warning("No active window found");
        return;
    }

    file_location_label = g_object_get_data(G_OBJECT(app), "file_location");
    if (!file_location_label) {
        g_warning("File location label not found");
        return;
    }

    display_text = location ? location : _("Untitled Document");
    gtk_label_set_text(GTK_LABEL(file_location_label), display_text);
    g_debug("Updated file location: %s", display_text);
}

static void
update_status_bar_for_state(GtkApplication *app, DocumentState state,
                           const gchar *file_path)
{
    const gchar *status_text;
    gchar *location_text = NULL;

    g_return_if_fail(GTK_IS_APPLICATION(app));

    /* Determine status text based on document state */
    switch (state) {
        case DOC_STATE_CLEAN:
            status_text = _("Saved");
            break;
        case DOC_STATE_DIRTY:
            status_text = _("Modified");
            break;
        case DOC_STATE_SAVING:
            status_text = _("Saving...");
            break;
        case DOC_STATE_DRAFT:
            status_text = _("Draft saved");
            break;
        case DOC_STATE_READONLY:
            status_text = _("Read-only");
            break;
        case DOC_STATE_CONFLICT:
            status_text = _("External changes detected");
            break;
        case DOC_STATE_ERROR:
            status_text = _("Save error");
            break;
        default:
            status_text = _("Unknown");
            break;
    }

    /* Prepare file location text */
    if (file_path) {
        gchar *basename = g_path_get_basename(file_path);
        gchar *dirname = g_path_get_dirname(file_path);
        location_text = g_strdup_printf("%s — %s", basename, dirname);
        g_free(basename);
        g_free(dirname);
    }

    update_save_status(app, status_text);
    update_file_location(app, location_text ? location_text : file_path);

    g_free(location_text);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Status bar management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void status_manager_update_save_status(GtkApplication *app, const gchar *status)
{
    update_save_status(app, status);
}

void status_manager_update_file_location(GtkApplication *app, const gchar *location)
{
    update_file_location(app, location);
}

void status_manager_update_status_bar_for_state(GtkApplication *app, DocumentState state,
                                               const gchar *file_path)
{
    update_status_bar_for_state(app, state, file_path);
}

void gtktext_update_status_bar_for_document_state(GtkApplication *app,
                                                   DocumentState state,
                                                   const gchar *file_path)
{
    update_status_bar_for_state(app, state, file_path);
}