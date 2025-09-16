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

/* Show dialog asking user to save unsaved changes */
void dialogs_show_unsaved_changes_dialog(GtkWindow *parent, GtkTextBuffer *buffer)
{
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Save changes before closing?"),
        _("If you don't save, your changes will be permanently lost.")
    ));

    adw_alert_dialog_add_responses(dialog,
        "discard", _("_Don't Save"),
        "cancel", _("_Cancel"),
        "save", _("_Save"),
        NULL);

    adw_alert_dialog_set_response_appearance(dialog, "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_response_appearance(dialog, "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dialog, "save");
    adw_alert_dialog_set_close_response(dialog, "cancel");

    /* Store parent window in buffer data for later use */
    g_object_set_data(G_OBJECT(buffer), "parent-window", parent);

    g_signal_connect(dialog, "response", G_CALLBACK(dialogs_on_unsaved_changes_dialog_response), buffer);

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}

/* Handle unsaved changes dialog response */
void dialogs_on_unsaved_changes_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    (void)dialog; /* Suppress unused parameter warning */
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(user_data);
    /* Get the parent window from buffer data */
    GtkWidget *window = g_object_get_data(G_OBJECT(buffer), "parent-window");

    g_message("Dialog response: %s", response);

    if (g_strcmp0(response, "save") == 0) {
        /* Get DocumentManager to check if we have a file path */
        GtkApplication *app = GTK_APPLICATION(g_object_get_data(G_OBJECT(buffer), "app"));
        DocumentManager *doc_manager = g_object_get_data(G_OBJECT(app), "doc_manager");

        if (!doc_manager) {
            g_warning("DocumentManager not found for save operation");
            return;
        }

        if (!document_manager_is_untitled(doc_manager)) {
            /* Save directly to existing file using DocumentManager */
            const gchar *file_path = document_manager_get_file_path(doc_manager);
            g_message("Saving to existing file: %s", file_path ? file_path : "(unknown)");

            /* Set close-after-save flag before starting save */
            g_object_set_data(G_OBJECT(buffer), "close-after-save", GINT_TO_POINTER(1));

            document_manager_save(doc_manager, FALSE, NULL, app);  /* TODO: Add proper callback */
        } else {
            /* Show file dialog to choose save location */
            g_message("No file path, showing save-as dialog");

            /* Set close-after-save flag */
            g_object_set_data(G_OBJECT(buffer), "close-after-save", GINT_TO_POINTER(1));

            /* Trigger save-as action to show file dialog */
            if (app) {
                GSimpleAction *save_as_action = g_simple_action_new("save-as", NULL);
                file_action_save_as_cb(save_as_action, NULL, app);
                g_object_unref(save_as_action);
            }
        }
    } else if (g_strcmp0(response, "discard") == 0) {
        /* Close without saving - discard any drafts and set flag to prevent dialog recursion */
        g_message("Discarding changes and closing");

        if (window && GTK_IS_WINDOW(window)) {
            /* Get DocumentManager and discard any existing draft */
            GtkApplication *app = GTK_APPLICATION(g_object_get_data(G_OBJECT(buffer), "app"));
            DocumentManager *doc_manager = g_object_get_data(G_OBJECT(app), "doc_manager");
            if (doc_manager) {
                document_manager_discard_current_draft(doc_manager);
            }

            /* Set flag to prevent recursion when window close is processed */
            g_object_set_data(G_OBJECT(buffer), "closing", GINT_TO_POINTER(1));

            /* Close the window */
            gtk_window_close(GTK_WINDOW(window));
        }
    } else if (g_strcmp0(response, "cancel") == 0) {
        g_message("User cancelled, keeping application open");
        /* Do nothing - dialog closes and application remains open */
    }
}

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

        /* Load the autosave file content */
        g_autofree char *contents = NULL;
        gsize length = 0;
        if (g_file_get_contents(autosave_path, &contents, &length, NULL)) {
            /* TODO: Integrate with application to load content */
            g_message("Loaded %zu bytes from autosave file", length);
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