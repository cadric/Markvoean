/* C ULTRA-MIN TEMPLATE
   Purpose: Edit action callbacks (undo, redo) for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-20 - ui/actions/edit_actions.c
   Added: Edit actions for undo/redo functionality
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <adwaita.h>

#include <gtktext/ui/edit_actions.h>
#include <gtktext/ui/tab_manager.h>
#include <gtktext/ui/tab_document.h>
#include <gtktext/ui/dialogs.h>
#include <gtktext/document/document_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */


static GtkTextBuffer* get_active_text_buffer(GtkApplication *app)
{
    g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);

    TabManager *tab_manager = gtktext_get_tab_manager(app);
    if (!tab_manager) {
        g_warning("No tab manager found");
        return NULL;
    }

    TabDocument *tab_doc = tab_manager_get_active_document(tab_manager);
    if (!tab_doc) {
        g_warning("No active tab document found");
        return NULL;
    }

    GtkTextBuffer *buffer = tab_document_get_buffer(tab_doc);
    if (!buffer) {
        g_warning("No active text buffer found");
        return NULL;
    }

    return buffer;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Edit action callbacks
 * ═══════════════════════════════════════════════════════════════════════════════ */

void edit_action_undo_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;

    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkTextBuffer *buffer = get_active_text_buffer(app);

    if (!buffer) {
        g_debug("Cannot undo: no active text buffer");
        return;
    }

    if (!gtk_text_buffer_get_can_undo(buffer)) {
        g_debug("Cannot undo: no undo history available");
        return;
    }

    gtk_text_buffer_undo(buffer);
    g_debug("Undo performed");
}

void edit_action_redo_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;

    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkTextBuffer *buffer = get_active_text_buffer(app);

    if (!buffer) {
        g_debug("Cannot redo: no active text buffer");
        return;
    }

    if (!gtk_text_buffer_get_can_redo(buffer)) {
        g_debug("Cannot redo: no redo history available");
        return;
    }

    gtk_text_buffer_redo(buffer);
    g_debug("Redo performed");
}

/* Callback for version history actions */
static void on_version_action(RecoveryAction action, const gchar *file_path, gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION(user_data);

    switch (action) {
        case RECOVERY_ACTION_OPEN:
            if (file_path) {
                g_debug("Version history: restoring version from %s", file_path);

                /* Get the active document manager to restore the version */
                TabManager *tab_manager = gtktext_get_tab_manager(app);
                if (!tab_manager) {
                    g_warning("No tab manager found for version restore");
                    return;
                }

                TabDocument *tab_doc = tab_manager_get_active_document(tab_manager);
                if (!tab_doc) {
                    g_warning("No active document for version restore");
                    return;
                }

                DocumentManager *dm = tab_document_get_document_manager(tab_doc);
                if (!dm) {
                    g_warning("No document manager found for version restore");
                    return;
                }

                /* Restore from version file */
                g_autoptr(GError) error = NULL;
                if (document_manager_restore_from_version(dm, file_path, &error)) {
                    g_message("Successfully restored version from: %s", file_path);
                } else {
                    g_warning("Failed to restore version: %s",
                             error ? error->message : "Unknown error");
                }
            }
            break;
        case RECOVERY_ACTION_REMOVE:
            if (file_path) {
                g_debug("Version history: removing version file %s", file_path);
                if (g_unlink(file_path) == 0) {
                    g_message("Version file removed: %s", file_path);
                } else {
                    g_warning("Failed to remove version file: %s", file_path);
                }
            }
            break;
        case RECOVERY_ACTION_CANCEL:
            g_debug("Version history dialog cancelled");
            break;
    }
}

void edit_action_version_history_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;

    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *window = gtk_application_get_active_window(app);

    if (!window) {
        g_debug("Cannot show version history: no active window");
        return;
    }

    TabManager *tab_manager = gtktext_get_tab_manager(app);
    if (!tab_manager) {
        g_warning("No tab manager found for version history");
        return;
    }

    TabDocument *tab_doc = tab_manager_get_active_document(tab_manager);
    if (!tab_doc) {
        g_warning("No active document for version history");
        return;
    }

    /* Get the current document's file path */
    const char *current_file_path = tab_document_get_file_path(tab_doc);

    if (!current_file_path) {
        /* Show informative dialog for untitled documents */
        AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
            _("Version History Unavailable"),
            _("Version history is only available for saved documents. Please save this document first.")
        ));

        adw_alert_dialog_add_responses(dialog, "ok", _("_OK"), NULL);
        adw_alert_dialog_set_default_response(dialog, "ok");
        adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window));
        return;
    }

    /* Check if version history is enabled */
    g_autoptr(GSettings) settings = g_settings_new("org.gtk.gtktext");
    if (!g_settings_get_boolean(settings, "version-history-enabled")) {
        /* Show informative dialog about enabling version history */
        AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
            _("Version History Disabled"),
            _("Version history is currently disabled. Enable it in Preferences to start saving document versions.")
        ));

        adw_alert_dialog_add_responses(dialog, "ok", _("_OK"), NULL);
        adw_alert_dialog_set_default_response(dialog, "ok");
        adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window));
        return;
    }

    /* Check if there are any versions for this document */
    g_auto(GStrv) versions = document_manager_list_version_history(current_file_path);
    if (!versions || g_strv_length(versions) == 0) {
        /* Show informative dialog about no versions */
        AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
            _("No Version History"),
            _("No previous versions found for this document. Versions are created each time you save the document.")
        ));

        adw_alert_dialog_add_responses(dialog, "ok", _("_OK"), NULL);
        adw_alert_dialog_set_default_response(dialog, "ok");
        adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window));
        return;
    }

    /* Show the version history dialog */
    dialogs_show_document_version_history(window, current_file_path, on_version_action, app);
    g_debug("Version history dialog shown for document: %s", current_file_path);
}

