/* C ULTRA-MIN TEMPLATE
   Purpose: Dialog management (unsaved changes, autorecover)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - ui/dialogs/dialogs.c
   Changed: Extracted dialog management from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <gtktext/ui/dialogs.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/ui/file_actions.h>
#include <gtktext/ui/tab_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Utility functions for dialog management
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Check if buffer has unsaved changes using DocumentManager state */
gboolean dialogs_has_unsaved_changes(GtkTextBuffer *buffer)
{
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), FALSE);

    /* Get DocumentManager from application */
    GtkApplication *app = GTK_APPLICATION(g_object_get_data(G_OBJECT(buffer), "app"));
    if (!app) {
        g_warning("Application not found in buffer data");
        return FALSE;
    }

    DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
    if (!dm) {
        g_warning("DocumentManager not found in application data");
        return FALSE;
    }

    /* Use DocumentManager to check for unsaved changes */
    return document_manager_has_unsaved_changes(dm);
}

/* Check for autosave file and offer recovery on startup */
void dialogs_check_for_autorecover(GtkApplication *app)
{
    const gchar *temp_dir = g_get_tmp_dir();
    g_autofree gchar *autosave_path = g_build_filename(temp_dir, "gtktext-autosave.md", NULL);

    if (g_file_test(autosave_path, G_FILE_TEST_EXISTS)) {
        g_autofree gchar *contents = NULL;
        gsize length = 0;
        if (g_file_get_contents(autosave_path, &contents, &length, NULL)) {
            if (length > 0) {
                g_message("Found autosave file: %s (%zu bytes)", autosave_path, length);
                GtkWindow *parent = gtk_application_get_active_window(app);
                if (parent) {
                    dialogs_show_autorecover_dialog(parent, autosave_path);
                }
            } else {
                g_debug("Autosave file is empty, removing");
                g_unlink(autosave_path);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Dialog display and response handlers
 * ═══════════════════════════════════════════════════════════════════════════════ */


/* Show dialog asking user to recover from autosave */
void dialogs_show_autorecover_dialog(GtkWindow *parent, const char *autosave_path)
{
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Recover unsaved document?"),
        _("A previous session was interrupted. Would you like to recover the unsaved document?")
    ));

    adw_alert_dialog_add_responses(dialog,
        "discard", _("_Don't Recover"),
        "recover", _("_Recover"),
        NULL);

    adw_alert_dialog_set_response_appearance(dialog, "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_response_appearance(dialog, "recover", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dialog, "recover");
    adw_alert_dialog_set_close_response(dialog, "discard");

    g_signal_connect(dialog, "response", G_CALLBACK(dialogs_on_autorecover_dialog_response), g_strdup(autosave_path));

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}

/* Handle autorecover dialog response */
void dialogs_on_autorecover_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    (void)dialog;
    char *autosave_path = (char*)user_data;

    if (g_strcmp0(response, "recover") == 0) {
        g_message("User chose to recover from autosave: %s", autosave_path);

        /* Load the autosave file content and create new document */
        g_autofree char *contents = NULL;
        gsize length = 0;
        if (g_file_get_contents(autosave_path, &contents, &length, NULL)) {
            g_message("Loaded %zu bytes from autosave file", length);

            /* Get the application context from the main window */
            GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default()));
            if (window) {
                GtkApplication *app = gtk_window_get_application(window);
                TabManager *tm = gtktext_get_tab_manager(app);

                if (tm) {
                    /* Create new document tab with recovered content */
                    AdwTabPage *page = tab_manager_new_document(tm, "Recovered Document");
                    if (page) {
                        GtkWidget *text_view = tab_manager_get_text_view(tm, page);
                        if (text_view) {
                            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
                            if (buffer) {
                                /* Insert recovered content */
                                gtk_text_buffer_set_text(buffer, contents, -1);
                                g_message("Successfully recovered autosave content to new document");
                            }
                        }
                    }
                } else {
                    g_warning("Could not get TabManager for autorecover");
                }
            } else {
                g_warning("Could not get active window for autorecover");
            }
        }

        /* Remove the autosave file after successful recovery */
        if (g_unlink(autosave_path) == 0) {
            g_debug("Removed autosave file: %s", autosave_path);
        }
    } else {
        g_message("User chose not to recover from autosave");
        /* Remove the autosave file */
        if (g_unlink(autosave_path) == 0) {
            g_debug("Removed autosave file: %s", autosave_path);
        }
    }

    g_free(autosave_path);
}