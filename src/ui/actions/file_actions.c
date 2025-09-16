/* C ULTRA-MIN TEMPLATE
   Purpose: File action callbacks (open, save, save-as)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - ui/actions/file_actions.c
   Changed: Extracted file actions from main.c for better organization
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

#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif

/* Buffer data keys from main.c - these should be moved to a shared header */
static const char *DATA_SUPPRESS_PARSE = "gtktext-suppress-reparse";
static const char *DATA_ORIGINAL_TEXT = "gtktext-original-md";
static const char *DATA_USER_DIRTY = "gtktext-user-dirty";

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Utility functions for file operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

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
    GError *err = NULL;
    if (!g_file_get_contents(path, &contents, &len, &err)) {
        g_warning("Open failed: %s", err->message);
        g_clear_error(&err);
        return;
    }

    GtkApplication *app = GTK_APPLICATION(user_data);
    DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
    if (!dm) {
        g_warning("DocumentManager not found in application data");
        return;
    }

    GtkWidget *text_view = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "text_view"));
    GtkWidget *main_stack = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "main_stack"));
    if (!text_view || !main_stack) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    /* Open file through DocumentManager */
    GError *open_error = NULL;
    if (!document_manager_open_file(dm, path, &open_error)) {
        g_warning("Failed to open file: %s", open_error ? open_error->message : "Unknown error");
        g_clear_error(&open_error);
        return;
    }

    /* Preserve original text and reset dirty flag (for compatibility) */
    g_object_set_data_full(G_OBJECT(buffer), DATA_ORIGINAL_TEXT, g_strdup(contents), g_free);
    g_object_set_data(G_OBJECT(buffer), DATA_USER_DIRTY, GINT_TO_POINTER(0));

#ifdef HAVE_LIBSOUP
    SoupSession *soup_session = g_object_get_data(G_OBJECT(app), "soup_session");
    /* Suppress dirty marking while we render programmatically */
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(1));
    if (!cm_render_markdown_to_buffer(buffer, contents, GTK_TEXT_VIEW(text_view),
                                      soup_session)) {
#else
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(1));
    if (!cm_render_markdown_to_buffer(buffer, contents, GTK_TEXT_VIEW(text_view), NULL)) {
#endif
        g_warning("Import failed");
    } else {
        cm_render_update_theme_dependent_tags(buffer);
        /* Switch to editor view after successful file load */
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "editor");
    }

    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(0));
    g_message("File opened: %s", path);
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

/* File open action callback */
void file_action_open_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;

    GtkFileDialog *dlg = gtk_file_dialog_new();

    /* Prefer the last used folder, falling back to HOME */
    const char *initial_path = NULL;
    GSettings *app_settings = gtktext_get_app_settings();
    if (app_settings) {
        const char *cfg = g_settings_get_string(app_settings, "last-open-dir");
        if (cfg && *cfg && g_file_test(cfg, G_FILE_TEST_IS_DIR)) initial_path = cfg;
    }
    if (!initial_path) initial_path = g_get_home_dir();
    if (initial_path && *initial_path) {
        GFile *init_dir = g_file_new_for_path(initial_path);
        gtk_file_dialog_set_initial_folder(dlg, init_dir);
        g_autofree char *uri = g_file_get_uri(init_dir);
        g_debug("[file-dialog] initial-folder=%s", uri);
        g_object_unref(init_dir);
    }

    /* Set title for markdown files */
    gtk_file_dialog_set_title(dlg, _("Open Markdown File"));
    file_action_setup_open_dialog_filters(dlg);

    g_debug("[file-dialog] presenting open dialog (action)");
    gtk_file_dialog_open(dlg, parent, NULL, file_action_on_open_dialog_finish, app);
    g_object_unref(dlg);
}

/* File save action callback */
void file_action_save_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");

    if (!dm) {
        g_warning("DocumentManager not found in application data");
        return;
    }

    if (document_manager_is_untitled(dm)) {
        /* Show save dialog for untitled documents */
        file_action_save_as_cb(action, parameter, user_data);
    } else {
        /* Immediate save for named documents */
        document_manager_save(dm, FALSE, NULL, app);  /* TODO: Add proper callback */
    }
}

/* File save-as action callback */
void file_action_save_as_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action; (void)parameter;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;

    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, _("Save Markdown File"));
    file_action_setup_save_dialog_filters(dlg);

    /* Set initial filename */
    gtk_file_dialog_set_initial_name(dlg, "document.md");

    /* Prefer the last used folder, falling back to Documents */
    const char *initial_path = NULL;
    GSettings *app_settings = gtktext_get_app_settings();
    if (app_settings) {
        const char *cfg = g_settings_get_string(app_settings, "last-open-dir");
        if (cfg && *cfg && g_file_test(cfg, G_FILE_TEST_IS_DIR)) initial_path = cfg;
    }
    if (!initial_path) {
        initial_path = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
        if (!initial_path) initial_path = g_get_home_dir();
    }
    if (initial_path && *initial_path) {
        GFile *init_dir = g_file_new_for_path(initial_path);
        gtk_file_dialog_set_initial_folder(dlg, init_dir);
        g_object_unref(init_dir);
    }

    gtk_file_dialog_save(dlg, parent, NULL, file_action_on_save_as_dialog_finish, app);
    g_object_unref(dlg);
}