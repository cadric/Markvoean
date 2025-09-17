/* C ULTRA-MIN TEMPLATE
   Purpose: Welcome screen functionality for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - ui/welcome_screen.c
   Changed: Extracted welcome screen from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/ui/welcome_screen.h>
#include <gtktext/ui/file_actions.h>
#include <gtktext/document/document.h>
#include <gtktext/core/settings.h>

/* Buffer data keys - local definitions */
static const char *DATA_ORIGINAL_TEXT = "gtktext-original-md";
static const char *DATA_USER_DIRTY = "gtktext-user-dirty";

/* Debug helper: window/display/environment info for diagnosing dialog layout issues */
static void debug_dump_window_env(GtkWindow *parent, const char *phase)
{
    const char *dbg = g_getenv("G_MESSAGES_DEBUG");
    if (!dbg || !*dbg) return;

    if (!parent) {
        g_debug("[file-dialog:%s] parent=(null)", phase);
        return;
    }

    GtkWidget *pw = GTK_WIDGET(parent);
    GdkDisplay *display = gtk_widget_get_display(pw);
    const char *display_name = display ? gdk_display_get_name(display) : "(null)";
    int scale = gtk_widget_get_scale_factor(pw);
    GtkNative *native = gtk_widget_get_native(pw);
    GdkSurface *surface = native ? gtk_native_get_surface(native) : NULL;
    int sw = surface ? gdk_surface_get_width(surface) : -1;
    int sh = surface ? gdk_surface_get_height(surface) : -1;
    gboolean mapped = gtk_widget_get_mapped(pw);
    gboolean visible = gtk_widget_get_visible(pw);

    g_debug("[file-dialog:%s] display='%s' scale=%d surface=%dx%d mapped=%d visible=%d",
            phase, display_name, scale, sw, sh, mapped, visible);

    const char *backend_env = g_getenv("GDK_BACKEND");
    const char *portal_env = g_getenv("GTK_USE_PORTAL");
    g_debug("[file-dialog:%s] env GDK_BACKEND=%s GTK_USE_PORTAL=%s",
            phase,
            backend_env ? backend_env : "(unset)",
            portal_env ? portal_env : "(unset)");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Welcome screen callback functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void welcome_screen_open_cb(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkApplication *app = GTK_APPLICATION(user_data);
    if (!app || !GTK_IS_APPLICATION(app)) {
        g_warning("Invalid application in welcome_open_cb");
        return;
    }
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) {
        g_warning("No active window found");
        return;
    }
    debug_dump_window_env(parent, "welcome-open:before");
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

    /* Set title and file filters for markdown files */
    gtk_file_dialog_set_title(dlg, _("Open Markdown File"));
    file_action_setup_open_dialog_filters(dlg);

    g_debug("[file-dialog] presenting open dialog (welcome)");
    gtk_file_dialog_open(dlg, parent, NULL, document_handlers_on_open_file_dialog_finish, app);
    g_object_unref(dlg);
    debug_dump_window_env(parent, "welcome-open:after");
}

void welcome_screen_new_cb(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkApplication *app = GTK_APPLICATION(user_data);
    if (!app || !GTK_IS_APPLICATION(app)) {
        g_warning("Invalid application in welcome_new_cb");
        return;
    }

    GtkWidget *text_view = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "text_view"));
    GtkWidget *main_stack = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "main_stack"));
    if (!text_view || !main_stack) {
        g_warning("Required widgets not found in welcome_new_cb");
        return;
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    /* Clear the buffer and create a new document */
    gtk_text_buffer_set_text(buffer, "", -1);

    /* Clear the current file path since this is a new document */
    g_object_set_data(G_OBJECT(app), "current_file_path", NULL);

    /* Reset dirty flag and original text for a clean new document */
    g_object_set_data_full(G_OBJECT(buffer), DATA_ORIGINAL_TEXT, g_strdup(""), g_free);
    g_object_set_data(G_OBJECT(buffer), DATA_USER_DIRTY, GINT_TO_POINTER(0));

    /* Switch to editor view */
    gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "editor");

    /* Focus the text view for immediate editing */
    gtk_widget_grab_focus(text_view);

    g_message("New markdown document created");
}

void welcome_screen_show(GtkApplication *app)
{
    g_return_if_fail(GTK_IS_APPLICATION(app));

    GtkWidget *main_stack = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "main_stack"));
    if (main_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "welcome");
        g_message("Welcome screen shown");
    }
}

void welcome_screen_hide(GtkApplication *app)
{
    g_return_if_fail(GTK_IS_APPLICATION(app));

    GtkWidget *main_stack = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "main_stack"));
    if (main_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "editor");
        g_message("Welcome screen hidden, editor view shown");
    }
}