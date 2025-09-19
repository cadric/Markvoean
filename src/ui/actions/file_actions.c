/* C ULTRA-MIN TEMPLATE
   Purpose: File action callbacks (open, save, save-as)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.4.2] - 2025-09-19 - ui/actions/file_actions.c
   Changed: Added shared helper function to consolidate duplicate dialog handlers
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <gtktext/ui/file_actions.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/core/settings.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/ui/tab_manager.h>
#include <gtktext/ui/tab_integration.h>
#include <gtktext/ui/tab_document.h>

#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif


/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Utility functions for file operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Helper function to robustly validate if a directory exists and is accessible */
static gboolean file_action_is_valid_directory(const char *path)
{
    if (!path || !*path) return FALSE;

    /* Create GFile for robust checking */
    GFile *file = g_file_new_for_path(path);
    if (!file) return FALSE;

    /* Use g_file_query_exists for robust validation */
    gboolean exists = g_file_query_exists(file, NULL);
    if (!exists) {
        g_object_unref(file);
        return FALSE;
    }

    /* Check if it's actually a directory */
    GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    g_object_unref(file);

    if (!info) return FALSE;

    GFileType type = g_file_info_get_file_type(info);
    g_object_unref(info);

    return (type == G_FILE_TYPE_DIRECTORY);
}

/* Helper function to set up file filters for open dialogs */
void file_action_setup_open_dialog_filters(GtkFileDialog *dialog)
{
    /* Create a list store to hold the file filters */
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

    /* Create markdown files filter */
    GtkFileFilter *md_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(md_filter, _("Markdown Files"));
    gtk_file_filter_add_pattern(md_filter, "*.md");
    gtk_file_filter_add_pattern(md_filter, "*.MD");
    gtk_file_filter_add_pattern(md_filter, "*.markdown");
    gtk_file_filter_add_pattern(md_filter, "*.MARKDOWN");
    gtk_file_filter_add_pattern(md_filter, "*.mdown");
    gtk_file_filter_add_pattern(md_filter, "*.mkd");
    gtk_file_filter_add_pattern(md_filter, "*.mkdn");
    g_list_store_append(filters, md_filter);

    /* Create all files filter */
    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, _("All Files"));
    gtk_file_filter_add_pattern(all_filter, "*");
    g_list_store_append(filters, all_filter);

    /* Set filters on the dialog */
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

    /* Set markdown filter as default */
    gtk_file_dialog_set_default_filter(dialog, md_filter);

    /* Clean up references */
    g_object_unref(md_filter);
    g_object_unref(all_filter);
    g_object_unref(filters);
}

/* Helper function to set up file filters for save dialogs */
void file_action_setup_save_dialog_filters(GtkFileDialog *dialog)
{
    /* Create a list store to hold the file filters */
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

    /* Create markdown files filter */
    GtkFileFilter *md_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(md_filter, _("Markdown Files"));
    gtk_file_filter_add_pattern(md_filter, "*.md");
    gtk_file_filter_add_pattern(md_filter, "*.MD");
    gtk_file_filter_add_pattern(md_filter, "*.markdown");
    gtk_file_filter_add_pattern(md_filter, "*.MARKDOWN");
    g_list_store_append(filters, md_filter);

    /* Create all files filter */
    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, _("All Files"));
    gtk_file_filter_add_pattern(all_filter, "*");
    g_list_store_append(filters, all_filter);

    /* Set filters on the dialog */
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

    /* Set markdown filter as default */
    gtk_file_dialog_set_default_filter(dialog, md_filter);

    /* Clean up references */
    g_object_unref(md_filter);
    g_object_unref(all_filter);
    g_object_unref(filters);
}

/* Helper function to handle common file dialog completion tasks */
gboolean file_action_handle_open_dialog_result(GFile *file, GtkApplication *app,
                                                     gboolean open_in_new_tab, GError **error)
{
    if (!file) {
        g_debug("File dialog dismissed without selection");
        return FALSE;
    }

    g_autofree char *path = g_file_get_path(file);
    g_debug("File selected: %s", path ? path : "(null)");

    /* Persist the directory for future opens */
    if (path) {
        g_autofree char *dir = g_path_get_dirname(path);
        if (dir) {
            GSettings *app_settings = gtktext_get_app_settings();
            if (app_settings) {
                g_settings_set_string(app_settings, "last-open-dir", dir);
                g_debug("[file-dialog] saved last-open-dir=%s", dir);
            }
        }
    }

    /* Load file content */
    g_autofree char *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &contents, &len, error)) {
        g_warning("Open failed: %s", (*error) ? (*error)->message : "unknown error");
        return FALSE;
    }

    if (open_in_new_tab) {
        /* Open in new tab via TabManager */
        TabManager *tm = gtktext_get_tab_manager(app);
        if (!tm) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "TabManager not found");
            return FALSE;
        }

        AdwTabPage *page = tab_manager_new_document(tm, "Loading...");
        if (!page) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to create new tab");
            return FALSE;
        }

        /* Load file content into the new tab */
        TabDocument *tab_doc = tab_manager_get_tab_document(tm, page);
        if (tab_doc) {
            tab_document_load_file(tab_doc, path, error);
        }
    } else {
        /* Open in current document via DocumentManager */
        DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
        if (!dm) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "DocumentManager not found");
            return FALSE;
        }

        /* Use the new function to avoid double file reading */
        if (!document_manager_open_file_with_content(dm, path, contents, error)) {
            return FALSE;
        }
    }

    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - File action callbacks and dialog completion handlers
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Async open-file completion callback */
void file_action_on_open_dialog_finish(GObject *source_object, GAsyncResult *res,
                                       gpointer user_data)
{
    GtkFileDialog *d = GTK_FILE_DIALOG(source_object);
    GError *finish_error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(d, res, &finish_error);
    if (finish_error) {
        g_warning("File dialog finished with error: %s", finish_error->message);
        g_clear_error(&finish_error);
        return;
    }
    if (!file) {
        g_debug("File dialog dismissed without selection");
        return;
    }

    GtkApplication *app = GTK_APPLICATION(user_data);
    GError *error = NULL;
    if (!file_action_handle_open_dialog_result(file, app, FALSE, &error)) {
        g_warning("Failed to open file: %s", error ? error->message : "Unknown error");
        g_clear_error(&error);
    }

}

/* Tab-aware file open dialog completion callback */
void file_action_on_open_dialog_finish_tab(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GtkFileDialog *d = GTK_FILE_DIALOG(source_object);
    GError *finish_error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(d, res, &finish_error);

    /* Unpack context */
    typedef struct {
        GtkApplication *app;
        TabManager *tm;
    } OpenFileContext;

    OpenFileContext *context = (OpenFileContext *)user_data;

    if (finish_error) {
        g_warning("File dialog finished with error: %s", finish_error->message);
        g_clear_error(&finish_error);
        g_free(context);
        return;
    }
    if (!file) {
        g_debug("File dialog dismissed without selection");
        g_free(context);
        return;
    }

    g_autofree char *path = g_file_get_path(file);
    g_debug("File selected for new tab: %s", path ? path : "(null)");

    /* Persist the directory for future opens */
    if (path) {
        g_autofree char *dir = g_path_get_dirname(path);
        if (dir) {
            GSettings *app_settings = gtktext_get_app_settings();
            if (app_settings) {
                g_settings_set_string(app_settings, "last-open-dir", dir);
            }
            g_debug("[file-dialog] saved last-open-dir=%s", dir);
        }
    }

    /* Open file in new tab using TabManager */
    AdwTabPage *new_page = tab_manager_open_file(context->tm, path);
    if (new_page) {
        g_debug("File opened in new tab successfully");
    } else {
        g_warning("Failed to open file in new tab: %s", path);
    }

    g_free(context);
}

/* DocumentManager save dialog completion callback */
void file_action_on_save_as_dialog_finish(GObject *source_object, GAsyncResult *res,
                                          gpointer user_data)
{
    GtkFileDialog *d = GTK_FILE_DIALOG(source_object);
    GError *finish_error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_save_finish(d, res, &finish_error);
    if (finish_error) {
        g_warning("Save dialog finished with error: %s", finish_error->message);
        g_clear_error(&finish_error);
        return;
    }
    if (!file) {
        g_debug("Save dialog dismissed without selection");
        return;
    }

    GtkApplication *app = GTK_APPLICATION(user_data);
    DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
    if (!dm) {
        g_warning("DocumentManager not found in application data");
        return;
    }

    g_autofree char *path = g_file_get_path(file);

    /* Use save_as to save to the new location */
    document_manager_save_as(dm, path, NULL, app);  /* TODO: Add proper callback */

    g_message("Document save-as initiated for: %s", path);
}

/* Tab-aware save dialog completion callback */
void file_action_on_save_as_dialog_finish_tab(GObject *source_object, GAsyncResult *res,
                                               gpointer user_data)
{
    GtkFileDialog *d = GTK_FILE_DIALOG(source_object);
    GError *finish_error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_save_finish(d, res, &finish_error);

    /* Unpack context */
    typedef struct {
        GtkApplication *app;
        TabManager *tm;
    } SaveFileContext;

    SaveFileContext *context = (SaveFileContext *)user_data;

    if (finish_error) {
        g_warning("Save dialog finished with error: %s", finish_error->message);
        g_clear_error(&finish_error);
        g_free(context);
        return;
    }
    if (!file) {
        g_debug("Save dialog dismissed without selection");
        g_free(context);
        return;
    }

    g_autofree char *path = g_file_get_path(file);
    g_debug("Save file selected: %s", path ? path : "(null)");

    /* Validate path before proceeding */
    if (!path || !*path) {
        g_warning("Invalid file path from dialog");
        g_free(context);
        return;
    }

    /* Get active tab document */
    TabDocument *active_tab = tab_manager_get_active_document(context->tm);
    if (!active_tab) {
        g_warning("No active tab to save");
        g_free(context);
        return;
    }

    /* Save the document to the selected path */
    GError *save_error = NULL;
    if (!tab_document_save_as(active_tab, path, &save_error)) {
        g_warning("Failed to save file: %s", save_error ? save_error->message : "Unknown error");
        g_clear_error(&save_error);
    } else {
        g_message("File saved as: %s", path);
    }

    g_free(context);
}

/* File open action callback */
void file_action_open_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;

    /* Get TabManager for opening files in new tabs */
    TabManager *tm = gtktext_get_tab_manager(app);
    if (!tm) {
        g_warning("TabManager not found - cannot open file");
        return;
    }

    GtkFileDialog *dlg = gtk_file_dialog_new();

    /* Prefer the last used folder, falling back to HOME */
    const char *initial_path = NULL;
    GSettings *app_settings = gtktext_get_app_settings();
    if (app_settings) {
        const char *cfg = g_settings_get_string(app_settings, "last-open-dir");
        if (file_action_is_valid_directory(cfg)) {
            initial_path = cfg;
        }
    }
    if (!initial_path) initial_path = g_get_home_dir();

    /* Only set initial folder if we have a valid directory */
    if (file_action_is_valid_directory(initial_path)) {
        GFile *init_dir = g_file_new_for_path(initial_path);
        gtk_file_dialog_set_initial_folder(dlg, init_dir);
        g_autofree char *uri = g_file_get_uri(init_dir);
        g_debug("[file-dialog] initial-folder=%s", uri);
        g_object_unref(init_dir);
    } else {
        g_debug("[file-dialog] No valid initial directory found, using system default");
    }

    /* Set title for markdown files */
    gtk_file_dialog_set_title(dlg, _("Open Markdown File"));
    file_action_setup_open_dialog_filters(dlg);

    g_debug("[file-dialog] presenting open dialog (action) - will open in new tab");

    /* Create a context structure to pass both app and TabManager */
    typedef struct {
        GtkApplication *app;
        TabManager *tm;
    } OpenFileContext;

    OpenFileContext *context = g_new(OpenFileContext, 1);
    context->app = app;
    context->tm = tm;

    gtk_file_dialog_open(dlg, parent, NULL, file_action_on_open_dialog_finish_tab, context);
    g_object_unref(dlg);
}

/* File save action callback */
void file_action_save_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);

    /* Get TabManager for current tab operations */
    TabManager *tm = gtktext_get_tab_manager(app);
    if (!tm) {
        g_warning("TabManager not found - cannot save file");
        return;
    }

    /* Get active tab document */
    TabDocument *active_tab = tab_manager_get_active_document(tm);
    if (!active_tab) {
        g_warning("No active tab to save");
        return;
    }

    /* Check if document has a file path */
    const char *file_path = tab_document_get_file_path(active_tab);
    if (!file_path) {
        /* Show save dialog for untitled documents */
        file_action_save_as_cb(action, parameter, user_data);
    } else {
        /* Immediate save for named documents */
        GError *error = NULL;
        if (!tab_document_save(active_tab, &error)) {
            g_warning("Failed to save file: %s", error ? error->message : "Unknown error");
            g_clear_error(&error);
        } else {
            g_message("File saved: %s", file_path);
        }
    }
}

/* File save-as action callback */
void file_action_save_as_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;

    /* Get TabManager for current tab operations */
    TabManager *tm = gtktext_get_tab_manager(app);
    if (!tm) {
        g_warning("TabManager not found - cannot save file");
        return;
    }

    /* Get active tab document */
    TabDocument *active_tab = tab_manager_get_active_document(tm);
    if (!active_tab) {
        g_warning("No active tab to save");
        return;
    }

    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, _("Save Markdown File"));
    file_action_setup_save_dialog_filters(dlg);

    /* Set initial filename based on current document */
    const char *current_path = tab_document_get_file_path(active_tab);
    if (current_path) {
        g_autofree char *basename = g_path_get_basename(current_path);
        gtk_file_dialog_set_initial_name(dlg, basename);
    } else {
        gtk_file_dialog_set_initial_name(dlg, "document.md");
    }

    /* Prefer the last used folder, falling back to Documents */
    const char *initial_path = NULL;
    GSettings *app_settings = gtktext_get_app_settings();
    if (app_settings) {
        const char *cfg = g_settings_get_string(app_settings, "last-open-dir");
        if (file_action_is_valid_directory(cfg)) {
            initial_path = cfg;
        }
    }
    if (!initial_path) {
        initial_path = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
        if (!file_action_is_valid_directory(initial_path)) {
            initial_path = g_get_home_dir();
        }
    }

    /* Only set initial folder if we have a valid directory */
    if (file_action_is_valid_directory(initial_path)) {
        GFile *init_dir = g_file_new_for_path(initial_path);
        gtk_file_dialog_set_initial_folder(dlg, init_dir);
        g_object_unref(init_dir);
    } else {
        g_debug("[file-dialog] No valid initial directory found for save dialog, using system default");
    }

    /* Create context for tab-aware save completion */
    typedef struct {
        GtkApplication *app;
        TabManager *tm;
    } SaveFileContext;

    SaveFileContext *context = g_new(SaveFileContext, 1);
    context->app = app;
    context->tm = tm;

    gtk_file_dialog_save(dlg, parent, NULL, file_action_on_save_as_dialog_finish_tab, context);
    g_object_unref(dlg);
}