/* C ULTRA-MIN TEMPLATE
   Purpose: Application action callbacks (preferences, about, shortcuts)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.2] - 2025-09-20 - ui/actions/app_actions.c
   Changed: Fixed quit action to check for unsaved changes and show save dialog
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib/gi18n.h>

#include <gtktext/ui/app_actions.h>
#include <gtktext/core/settings.h>
#include <gtktext/core/window_lifecycle.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Application action callbacks
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Preferences action callback */
void app_action_preferences_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;
    AdwDialog *dlg = create_settings_window(parent);
    (void)dlg;
}

/* About dialog action callback */
void app_action_about_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;
    AdwDialog *about = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), _("IFG"));
    adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(about), _("It format good"));
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), "gtktext");
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "IFG Authors");
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), "1.0.0");
    adw_dialog_present(about, GTK_WIDGET(parent));
}

/* Shortcuts window action callback */
void app_action_shortcuts_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;

    /* Load shortcuts window UI from file path (development) or resource (installed) */
    g_autoptr(GtkBuilder) builder = gtk_builder_new();
    g_autoptr(GError) error = NULL;
    gboolean loaded = FALSE;

    /* Try development path first */
    const char *dev_paths[] = { "./ui/shortcuts.ui", "../ui/shortcuts.ui", NULL };
    for (int i = 0; dev_paths[i] && !loaded; i++) {
        if (g_file_test(dev_paths[i], G_FILE_TEST_EXISTS)) {
            if (gtk_builder_add_from_file(builder, dev_paths[i], &error)) {
                loaded = TRUE;
                g_debug("Loaded shortcuts UI from development path: %s", dev_paths[i]);
            } else {
                g_clear_error(&error);
            }
        }
    }

    /* Fallback to resource if not in development */
    if (!loaded) {
        if (gtk_builder_add_from_resource(builder, "/org/gtk/gtktext/ui/shortcuts.ui",
                                         &error)) {
            loaded = TRUE;
            g_debug("Loaded shortcuts UI from resource");
        }
    }

    if (!loaded) {
        g_warning("Failed to load shortcuts UI: %s",
                 error ? error->message : "unknown error");
        return;
    }

    GtkWidget *shortcuts_window = GTK_WIDGET(gtk_builder_get_object(builder,
                                                                   "shortcuts_window"));
    if (shortcuts_window) {
        gtk_window_set_transient_for(GTK_WINDOW(shortcuts_window), parent);
        gtk_window_present(GTK_WINDOW(shortcuts_window));
    }
}

/* Print/Export action callback (placeholder) */
void app_action_print_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;

    /* Show placeholder dialog for print/export functionality */
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Print/Export"),
        _("Print and export functionality will be implemented in a future version.")
    ));

    adw_alert_dialog_add_responses(dialog, "ok", _("_OK"), NULL);
    adw_alert_dialog_set_default_response(dialog, "ok");
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}

/* Quit application action callback */
void app_action_quit_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);

    /* Get the active window to handle unsaved changes dialog */
    GtkWindow *window = gtk_application_get_active_window(app);
    if (!window) {
        g_application_quit(G_APPLICATION(app));
        return;
    }

    /* Use the same logic as window close request to handle unsaved changes */
    if (window_lifecycle_on_window_close_request(window, NULL)) {
        /* Function returned TRUE, meaning there are unsaved changes and dialog is shown */
        /* Don't quit - let the user handle the dialog */
        return;
    }

    /* No unsaved changes, proceed with quit */
    g_application_quit(G_APPLICATION(app));
}