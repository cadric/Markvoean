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

static gboolean dialogs_create_recovered_document(GtkApplication *app,
                                                  const gchar *title,
                                                  const gchar *body)
{
    g_return_val_if_fail(GTK_IS_APPLICATION(app), FALSE);
    g_return_val_if_fail(body != NULL, FALSE);

    GtkWindow *window = gtk_application_get_active_window(app);
    if (!window) {
        g_warning("Could not get active window for recovery");
        return FALSE;
    }

    TabManager *tm = gtktext_get_tab_manager(app);
    if (!tm) {
        g_warning("Could not get TabManager for recovery");
        return FALSE;
    }

    g_autofree gchar *page_title = NULL;
    if (title && *title) {
        g_autofree gchar *basename = g_path_get_basename(title);
        page_title = g_strdup(basename ? basename : title);
    }

    const gchar *tab_title = page_title ? page_title : _("Recovered Document");
    AdwTabPage *page = tab_manager_new_document(tm, tab_title);
    if (!page) {
        g_warning("Failed to create recovered document tab");
        return FALSE;
    }

    GtkWidget *text_view = tab_manager_get_text_view(tm, page);
    if (!text_view) {
        g_warning("Failed to obtain text view for recovered document");
        return FALSE;
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    if (!buffer) {
        g_warning("Failed to obtain text buffer for recovered document");
        return FALSE;
    }

    gtk_text_buffer_set_text(buffer, body, -1);
    tab_manager_set_active_tab(tm, page);

    g_message("Recovered document restored to new tab");
    return TRUE;
}

static void on_recovery_banner_review_clicked(AdwBanner *banner, gpointer user_data);
static void on_recovery_browser_action(RecoveryAction action, const gchar *file_path, gpointer user_data);

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
    g_return_if_fail(GTK_IS_APPLICATION(app));

    GtkWidget *banner = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "recovery_banner"));
    g_auto(GStrv) recovery_files = document_manager_list_recovery_files();
    gsize count = recovery_files ? g_strv_length(recovery_files) : 0;

    if (!banner) {
        if (count == 0) {
            return;
        }

        GtkWindow *parent = gtk_application_get_active_window(app);
        if (parent) {
            dialogs_show_recovery_browser(parent, on_recovery_browser_action, app);
        }
        return;
    }

    if (count == 0) {
        adw_banner_set_revealed(ADW_BANNER(banner), FALSE);
        return;
    }

    gchar *latest_path = NULL;
    gchar *latest_basename = NULL;
    for (gsize i = 0; i < count; i++) {
        g_autofree gchar *basename = g_path_get_basename(recovery_files[i]);
        if (!latest_basename || g_strcmp0(basename, latest_basename) > 0) {
            g_free(latest_basename);
            latest_basename = g_strdup(basename);
            g_free(latest_path);
            latest_path = g_strdup(recovery_files[i]);
        }
    }

    g_autofree gchar *display_name = latest_path ?
        document_manager_get_recovery_display_name(latest_path) : NULL;

    adw_banner_set_title(ADW_BANNER(banner), _("Recovered autosave available"));

    g_autofree gchar *message = NULL;
    if (count == 1) {
        message = g_strdup_printf(_("Autosave ready: %s"),
                                  display_name ? display_name : latest_basename);
    } else {
        message = g_strdup_printf(ngettext("%u autosave snapshot available. Latest: %s",
                                            "%u autosave snapshots available. Latest: %s",
                                            (unsigned)count),
                                 (unsigned)count,
                                 display_name ? display_name : latest_basename);
    }

    adw_banner_set_title(ADW_BANNER(banner), message);
    g_free(latest_basename);
    adw_banner_set_button_label(ADW_BANNER(banner), _("Review…"));
    adw_banner_set_revealed(ADW_BANNER(banner), TRUE);
    g_free(latest_path);

    if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(banner), "recovery-banner-connected"))) {
        g_signal_connect(banner, "button-clicked",
                         G_CALLBACK(on_recovery_banner_review_clicked), app);
        g_object_set_data(G_OBJECT(banner), "recovery-banner-connected",
                          GINT_TO_POINTER(1));
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

        g_autoptr(GKeyFile) keyfile = g_key_file_new();
        if (g_key_file_load_from_file(keyfile, autosave_path, G_KEY_FILE_NONE, NULL)) {
            g_autofree gchar *content = g_key_file_get_string(keyfile, "Recovery", "Content", NULL);
            g_autofree gchar *original_path = g_key_file_get_string(keyfile, "Recovery", "OriginalPath", NULL);
            if (content) {
                GtkApplication *app = GTK_APPLICATION(g_application_get_default());
                if (app) {
                    dialogs_create_recovered_document(app, original_path, content);
                }
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

static void on_recovery_browser_action(RecoveryAction action, const gchar *file_path, gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION(user_data);

    if (!file_path || !app) {
        return;
    }

    switch (action) {
    case RECOVERY_ACTION_OPEN: {
        g_autoptr(GKeyFile) keyfile = g_key_file_new();
        if (g_key_file_load_from_file(keyfile, file_path, G_KEY_FILE_NONE, NULL)) {
            g_autofree gchar *content = g_key_file_get_string(keyfile, "Recovery", "Content", NULL);
            g_autofree gchar *original_path = g_key_file_get_string(keyfile, "Recovery", "OriginalPath", NULL);
            if (content && dialogs_create_recovered_document(app, original_path, content)) {
                if (g_unlink(file_path) == 0) {
                    g_debug("Removed recovery snapshot after opening: %s", file_path);
                }
            }
        }
        break;
    }
    case RECOVERY_ACTION_REMOVE:
        if (g_unlink(file_path) == 0) {
            g_message("Recovery snapshot removed: %s", file_path);
        } else {
            g_warning("Failed to remove recovery snapshot: %s", file_path);
        }
        break;
    case RECOVERY_ACTION_CANCEL:
    default:
        break;
    }
}

static void on_recovery_banner_review_clicked(AdwBanner *banner, gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION(user_data);
    if (!app) {
        return;
    }

    GtkWindow *parent = gtk_application_get_active_window(app);
    dialogs_show_recovery_browser(parent, on_recovery_browser_action, app);
    adw_banner_set_revealed(banner, FALSE);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * EXTERNAL CHANGE CONFLICT DIALOG - Phase 3
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    ConflictResolvedCallback callback;
    gpointer user_data;
} ConflictDialogData;

static void on_conflict_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    (void)dialog; /* Unused parameter */
    ConflictDialogData *data = (ConflictDialogData *)user_data;
    ConflictResolution resolution = CONFLICT_RESOLUTION_KEEP; /* Default to keep */

    if (g_strcmp0(response, "reload") == 0) {
        resolution = CONFLICT_RESOLUTION_RELOAD;
    } else if (g_strcmp0(response, "keep") == 0) {
        resolution = CONFLICT_RESOLUTION_KEEP;
    } else if (g_strcmp0(response, "meld") == 0) {
        resolution = CONFLICT_RESOLUTION_MELD;
    }

    /* Call the callback with the user's choice */
    if (data->callback) {
        data->callback(resolution, data->user_data);
    }

    g_free(data);
}

void dialogs_show_external_change_conflict(GtkWindow *parent,
                                          const gchar *file_path,
                                          ConflictResolvedCallback callback,
                                          gpointer user_data)
{
    g_return_if_fail(parent == NULL || GTK_IS_WINDOW(parent));
    g_return_if_fail(file_path != NULL);

    /* Create dialog data */
    ConflictDialogData *data = g_new0(ConflictDialogData, 1);
    data->callback = callback;
    data->user_data = user_data;

    /* Create the conflict resolution dialog */
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("File Changed Externally"),
        NULL));

    /* Set dialog body with file information */
    g_autofree gchar *body = g_strdup_printf(
        _("The file \"%s\" has been modified by another application.\n\n"
          "Do you want to reload the file and lose your changes, "
          "or keep your version?"),
        g_path_get_basename(file_path));
    adw_alert_dialog_set_body(dialog, body);

    /* Add response buttons */
    adw_alert_dialog_add_response(dialog, "keep", _("Keep My Version"));
    adw_alert_dialog_add_response(dialog, "reload", _("Reload from File"));

    /* Future: Add merge option when available */
    /* adw_alert_dialog_add_response(dialog, "meld", _("Show Differences")); */

    /* Set default and suggested responses */
    adw_alert_dialog_set_default_response(dialog, "keep");
    adw_alert_dialog_set_response_appearance(dialog, "reload", ADW_RESPONSE_DESTRUCTIVE);

    /* Connect response handler */
    g_signal_connect(dialog, "response", G_CALLBACK(on_conflict_dialog_response), data);

    /* Show the dialog */
    GtkWidget *parent_widget = parent ? GTK_WIDGET(parent) : NULL;
    if (!parent_widget) {
        /* If no parent, try to get the active window */
        GtkApplication *app = GTK_APPLICATION(g_application_get_default());
        if (app) {
            GtkWindow *active_window = gtk_application_get_active_window(app);
            if (active_window) {
                parent_widget = GTK_WIDGET(active_window);
            }
        }
    }

    if (parent_widget) {
        adw_alert_dialog_choose(dialog, parent_widget, NULL, NULL, NULL);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * RECOVERY/DRAFTS BROWSER DIALOG - Phase 3
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    RecoveryActionCallback callback;
    gpointer user_data;
    AdwDialog *dialog;
    GtkListBox *list_box;
    GtkWidget *open_button;
    GtkWidget *remove_button;
    gulong row_selected_handler_id;
    gchar *selected_file_path;
    gboolean closing;
    gboolean finalized;
} RecoveryDialogData;

typedef struct {
    RecoveryDialogData *dialog_data;
    gchar *file_path;
    gchar *display_name;
    gboolean is_draft;
} RecoveryFileItem;

static void recovery_file_item_free(RecoveryFileItem *item)
{
    if (item) {
        g_free(item->file_path);
        g_free(item->display_name);
        g_free(item);
    }
}

static void recovery_dialog_disconnect_signals(RecoveryDialogData *data)
{
    if (!data) {
        return;
    }

    if (data->list_box && data->row_selected_handler_id > 0) {
        g_signal_handler_disconnect(data->list_box, data->row_selected_handler_id);
        data->row_selected_handler_id = 0;
    }
}

static void recovery_dialog_finalize(RecoveryDialogData *data)
{
    if (!data || data->finalized) {
        return;
    }

    recovery_dialog_disconnect_signals(data);

    data->closing = TRUE;

    if (data->list_box) {
        g_object_remove_weak_pointer(G_OBJECT(data->list_box), (gpointer *)&data->list_box);
        data->list_box = NULL;
    }

    if (data->open_button) {
        g_object_remove_weak_pointer(G_OBJECT(data->open_button), (gpointer *)&data->open_button);
        data->open_button = NULL;
    }

    if (data->remove_button) {
        g_object_remove_weak_pointer(G_OBJECT(data->remove_button), (gpointer *)&data->remove_button);
        data->remove_button = NULL;
    }

    g_clear_pointer(&data->selected_file_path, g_free);
    data->finalized = TRUE;
    g_free(data);
}

static void on_recovery_dialog_closed(AdwDialog *dialog, gpointer user_data)
{
    (void)dialog;
    recovery_dialog_finalize((RecoveryDialogData *)user_data);
}

static void on_recovery_dialog_cancel(GtkWidget *button, gpointer user_data)
{
    (void)button;
    RecoveryDialogData *data = (RecoveryDialogData *)user_data;

    if (!data || data->closing) {
        return;
    }

    data->closing = TRUE;
    recovery_dialog_disconnect_signals(data);
    adw_dialog_close(data->dialog);
}

static void on_recovery_action_clicked(GtkWidget *button, gpointer user_data)
{
    RecoveryDialogData *data = (RecoveryDialogData *)user_data;
    RecoveryAction action = RECOVERY_ACTION_CANCEL;

    if (!data || data->closing) {
        return;
    }

    GtkWidget *open_button = data->open_button;
    GtkWidget *remove_button = data->remove_button;

    if (button == open_button) {
        action = RECOVERY_ACTION_OPEN;
    } else if (button == remove_button) {
        action = RECOVERY_ACTION_REMOVE;
    }

    /* Call the callback with the user's choice */
    if (data->callback) {
        data->callback(action, data->selected_file_path, data->user_data);
    }

    /* Close the dialog */
    data->closing = TRUE;
    recovery_dialog_disconnect_signals(data);
    adw_dialog_close(data->dialog);
}

static void on_list_box_row_selected(GtkListBox *list_box, GtkListBoxRow *row, gpointer user_data)
{
    (void)list_box; /* Unused parameter */
    RecoveryDialogData *data = (RecoveryDialogData *)user_data;

    if (!data || data->closing) {
        return;
    }

    g_free(data->selected_file_path);
    data->selected_file_path = NULL;

    /* Enable/disable action buttons based on selection */
    gboolean has_selection = (row != NULL);

    if (data->open_button) {
        gtk_widget_set_sensitive(data->open_button, has_selection);
    }

    if (data->remove_button) {
        gtk_widget_set_sensitive(data->remove_button, has_selection);
    }

    if (row) {
        RecoveryFileItem *item = g_object_get_data(G_OBJECT(row), "file-item");
        if (item) {
            data->selected_file_path = g_strdup(item->file_path);
        }
    }
}

static GtkWidget *create_file_row(RecoveryFileItem *item)
{
    /* Create a box for the row content */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    /* Primary label - display name */
    GtkWidget *primary_label = gtk_label_new(item->display_name);
    gtk_widget_set_halign(primary_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(primary_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(primary_label, "heading");
    gtk_box_append(GTK_BOX(box), primary_label);

    /* Secondary label - file type and path */
    /* Check if this is a version file or recovery file by extension */
    const gchar *file_type;
    if (g_str_has_suffix(item->file_path, ".version")) {
        file_type = _("Version");
    } else if (item->is_draft) {
        file_type = _("Draft");
    } else {
        file_type = _("Recovery");
    }

    g_autofree gchar *secondary_text = g_strdup_printf("%s • %s",
                                                       file_type,
                                                       item->file_path);
    GtkWidget *secondary_label = gtk_label_new(secondary_text);
    gtk_widget_set_halign(secondary_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(secondary_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(secondary_label, "dim-label");
    gtk_widget_add_css_class(secondary_label, "caption");
    gtk_box_append(GTK_BOX(box), secondary_label);

    return box;
}

static void populate_recovery_list(RecoveryDialogData *data)
{
    /* Get drafts */
    g_auto(GStrv) drafts = document_manager_list_drafts();
    if (drafts) {
        for (gsize i = 0; drafts[i] != NULL; i++) {
            RecoveryFileItem *item = g_new0(RecoveryFileItem, 1);
            item->dialog_data = data;
            item->file_path = g_strdup(drafts[i]);
            item->display_name = g_strdup(document_manager_get_draft_display_name(drafts[i]));
            item->is_draft = TRUE;

            GtkWidget *row_content = create_file_row(item);
            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_content);
            g_object_set_data_full(G_OBJECT(row), "file-item", item,
                                  (GDestroyNotify)recovery_file_item_free);

            gtk_list_box_append(data->list_box, row);
        }
    }

    /* Get recovery files */
    g_auto(GStrv) recovery_files = document_manager_list_recovery_files();
    if (recovery_files) {
        for (gsize i = 0; recovery_files[i] != NULL; i++) {
            RecoveryFileItem *item = g_new0(RecoveryFileItem, 1);
            item->dialog_data = data;
            item->file_path = g_strdup(recovery_files[i]);
            item->display_name = g_strdup(document_manager_get_recovery_display_name(recovery_files[i]));
            item->is_draft = FALSE;

            GtkWidget *row_content = create_file_row(item);
            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_content);
            g_object_set_data_full(G_OBJECT(row), "file-item", item,
                                  (GDestroyNotify)recovery_file_item_free);

            gtk_list_box_append(data->list_box, row);
        }
    }
}

static void populate_document_recovery_list(RecoveryDialogData *data, const gchar *document_path)
{
    /* For document-specific version history, we show version history files for this document */
    /* Version history files are different from recovery files - they contain saved versions */

    /* Get version history files for the specific document */
    g_auto(GStrv) version_files = document_manager_list_version_history(document_path);
    if (version_files) {
        for (gsize i = 0; version_files[i] != NULL; i++) {
            RecoveryFileItem *item = g_new0(RecoveryFileItem, 1);
            item->dialog_data = data;
            item->file_path = g_strdup(version_files[i]);
            item->display_name = g_strdup(document_manager_get_version_display_name(version_files[i]));
            item->is_draft = FALSE; /* Version files are not drafts */

            GtkWidget *row_content = create_file_row(item);
            GtkWidget *row = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_content);
            g_object_set_data_full(G_OBJECT(row), "file-item", item,
                                  (GDestroyNotify)recovery_file_item_free);

            gtk_list_box_append(data->list_box, row);
        }
    }
}

void dialogs_show_recovery_browser(GtkWindow *parent,
                                  RecoveryActionCallback callback,
                                  gpointer user_data)
{
    g_return_if_fail(parent == NULL || GTK_IS_WINDOW(parent));

    /* Create dialog data */
    RecoveryDialogData *data = g_new0(RecoveryDialogData, 1);
    data->callback = callback;
    data->user_data = user_data;

    /* Create the main dialog */
    data->dialog = ADW_DIALOG(adw_dialog_new());
    adw_dialog_set_title(data->dialog, _("Recover Documents"));
    adw_dialog_set_content_width(data->dialog, 600);
    adw_dialog_set_content_height(data->dialog, 400);

    /* Create main content box */
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Create header */
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header),
                                   gtk_label_new(_("Recover Documents")));

    /* Add cancel button */
    GtkWidget *cancel_button = gtk_button_new_with_label(_("Cancel"));
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), cancel_button);
    g_signal_connect(cancel_button, "clicked",
                    G_CALLBACK(on_recovery_dialog_cancel), data);

    gtk_box_append(GTK_BOX(content_box), header);

    /* Create main content area */
    GtkWidget *main_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(main_content, 24);
    gtk_widget_set_margin_end(main_content, 24);
    gtk_widget_set_margin_top(main_content, 12);
    gtk_widget_set_margin_bottom(main_content, 24);

    /* Add description */
    GtkWidget *description = gtk_label_new(_("Select a draft or recovery file to open or remove:"));
    gtk_widget_set_halign(description, GTK_ALIGN_START);
    gtk_widget_add_css_class(description, "dim-label");
    gtk_box_append(GTK_BOX(main_content), description);

    /* Create scrolled window for file list */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 200);
    gtk_widget_set_vexpand(scrolled, TRUE);

    /* Create list box */
    data->list_box = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(data->list_box, GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(GTK_WIDGET(data->list_box), "boxed-list");
    data->row_selected_handler_id = g_signal_connect(data->list_box, "row-selected",
                    G_CALLBACK(on_list_box_row_selected), data);
    g_object_add_weak_pointer(G_OBJECT(data->list_box), (gpointer *)&data->list_box);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(data->list_box));
    gtk_box_append(GTK_BOX(main_content), scrolled);

    /* Create action buttons */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);

    GtkWidget *remove_button = gtk_button_new_with_label(_("Remove"));
    data->remove_button = remove_button;
    g_object_add_weak_pointer(G_OBJECT(remove_button), (gpointer *)&data->remove_button);
    gtk_widget_add_css_class(remove_button, "destructive-action");
    gtk_widget_set_sensitive(remove_button, FALSE);
    g_signal_connect(remove_button, "clicked",
                    G_CALLBACK(on_recovery_action_clicked), data);
    gtk_box_append(GTK_BOX(button_box), remove_button);

    GtkWidget *open_button = gtk_button_new_with_label(_("Open"));
    data->open_button = open_button;
    g_object_add_weak_pointer(G_OBJECT(open_button), (gpointer *)&data->open_button);
    gtk_widget_add_css_class(open_button, "suggested-action");
    gtk_widget_set_sensitive(open_button, FALSE);
    g_signal_connect(open_button, "clicked",
                    G_CALLBACK(on_recovery_action_clicked), data);
    gtk_box_append(GTK_BOX(button_box), open_button);

    gtk_box_append(GTK_BOX(main_content), button_box);
    gtk_box_append(GTK_BOX(content_box), main_content);

    /* Set dialog content */
    adw_dialog_set_child(data->dialog, content_box);

    /* Populate the list */
    populate_recovery_list(data);

    g_signal_connect(data->dialog, "closed",
                    G_CALLBACK(on_recovery_dialog_closed), data);

    /* Show the dialog */
    GtkWidget *parent_widget = parent ? GTK_WIDGET(parent) : NULL;
    if (!parent_widget) {
        GtkApplication *app = GTK_APPLICATION(g_application_get_default());
        if (app) {
            GtkWindow *active_window = gtk_application_get_active_window(app);
            if (active_window) {
                parent_widget = GTK_WIDGET(active_window);
            }
        }
    }

    if (parent_widget) {
        adw_dialog_present(data->dialog, parent_widget);
    }
}

/* Show document-specific version history dialog */
void dialogs_show_document_version_history(GtkWindow *parent,
                                          const gchar *document_path,
                                          RecoveryActionCallback callback,
                                          gpointer user_data)
{
    g_return_if_fail(parent == NULL || GTK_IS_WINDOW(parent));
    g_return_if_fail(document_path != NULL);

    /* Create dialog data */
    RecoveryDialogData *data = g_new0(RecoveryDialogData, 1);
    data->callback = callback;
    data->user_data = user_data;

    /* Create the main dialog */
    data->dialog = ADW_DIALOG(adw_dialog_new());

    /* Set title based on document name */
    g_autofree gchar *document_basename = g_path_get_basename(document_path);
    g_autofree gchar *title = g_strdup_printf(_("Version History - %s"), document_basename);
    adw_dialog_set_title(data->dialog, title);
    adw_dialog_set_content_width(data->dialog, 600);
    adw_dialog_set_content_height(data->dialog, 400);

    /* Create main content box */
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Create header */
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header),
                                   gtk_label_new(title));

    /* Add cancel button */
    GtkWidget *cancel_button = gtk_button_new_with_label(_("Cancel"));
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), cancel_button);
    g_signal_connect(cancel_button, "clicked",
                    G_CALLBACK(on_recovery_dialog_cancel), data);

    gtk_box_append(GTK_BOX(content_box), header);

    /* Create main content area */
    GtkWidget *main_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(main_content, 24);
    gtk_widget_set_margin_end(main_content, 24);
    gtk_widget_set_margin_top(main_content, 12);
    gtk_widget_set_margin_bottom(main_content, 24);

    /* Add description for document-specific version history */
    g_autofree gchar *description_text = g_strdup_printf(
        _("Select a previous version of \"%s\" to restore or remove:"), document_basename);
    GtkWidget *description = gtk_label_new(description_text);
    gtk_widget_set_halign(description, GTK_ALIGN_START);
    gtk_widget_add_css_class(description, "dim-label");
    gtk_box_append(GTK_BOX(main_content), description);

    /* Create scrolled window for file list */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 200);
    gtk_widget_set_vexpand(scrolled, TRUE);

    /* Create list box */
    data->list_box = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(data->list_box, GTK_SELECTION_SINGLE);
    gtk_widget_add_css_class(GTK_WIDGET(data->list_box), "boxed-list");
    data->row_selected_handler_id = g_signal_connect(data->list_box, "row-selected",
                    G_CALLBACK(on_list_box_row_selected), data);
    g_object_add_weak_pointer(G_OBJECT(data->list_box), (gpointer *)&data->list_box);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(data->list_box));
    gtk_box_append(GTK_BOX(main_content), scrolled);

    /* Create action buttons */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);

    GtkWidget *remove_button = gtk_button_new_with_label(_("Remove"));
    data->remove_button = remove_button;
    g_object_add_weak_pointer(G_OBJECT(remove_button), (gpointer *)&data->remove_button);
    gtk_widget_add_css_class(remove_button, "destructive-action");
    gtk_widget_set_sensitive(remove_button, FALSE);
    g_signal_connect(remove_button, "clicked",
                    G_CALLBACK(on_recovery_action_clicked), data);
    gtk_box_append(GTK_BOX(button_box), remove_button);

    GtkWidget *open_button = gtk_button_new_with_label(_("Restore"));
    data->open_button = open_button;
    g_object_add_weak_pointer(G_OBJECT(open_button), (gpointer *)&data->open_button);
    gtk_widget_add_css_class(open_button, "suggested-action");
    gtk_widget_set_sensitive(open_button, FALSE);
    g_signal_connect(open_button, "clicked",
                    G_CALLBACK(on_recovery_action_clicked), data);
    gtk_box_append(GTK_BOX(button_box), open_button);

    gtk_box_append(GTK_BOX(main_content), button_box);
    gtk_box_append(GTK_BOX(content_box), main_content);

    /* Set dialog content */
    adw_dialog_set_child(data->dialog, content_box);

    /* Populate the list with document-specific recovery files */
    populate_document_recovery_list(data, document_path);

    g_signal_connect(data->dialog, "closed",
                    G_CALLBACK(on_recovery_dialog_closed), data);

    /* Show the dialog */
    GtkWidget *parent_widget = parent ? GTK_WIDGET(parent) : NULL;
    if (!parent_widget) {
        GtkApplication *app = GTK_APPLICATION(g_application_get_default());
        if (app) {
            GtkWindow *active_window = gtk_application_get_active_window(app);
            if (active_window) {
                parent_widget = GTK_WIDGET(active_window);
            }
        }
    }

    if (parent_widget) {
        adw_dialog_present(data->dialog, parent_widget);
    }
}
