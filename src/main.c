/* C ULTRA-MIN TEMPLATE
   Purpose: Main application entry point and UI coordination for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • LIFECYCLE
   [1.0.1] - 2025-09-16 - main.c
   Changed: Modularized architecture - core functions moved to specialized modules
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif
#include <glib/gi18n.h>
#include <adwaita.h>
#include <cmark.h>
#include <locale.h>

#include <gtktext/components/toolbar.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/render/theme_styles.h>
#include <gtktext/render/images/http_images.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/core/settings.h>
#include <gtktext/core/settings_manager.h>
#include <gtktext/core/window_lifecycle.h>
#include <gtktext/ui/file_actions.h>
#include <gtktext/ui/app_actions.h>
#include <gtktext/ui/dialogs.h>
#include <gtktext/ui/image_embedder.h>
#include <gtktext/ui/welcome_screen.h>
#include <gtktext/ui/text_view_interactions.h>
#include <gtktext/ui/status_manager.h>
#include <gtktext/render/markdown/markdown_engine.h>
#include <gtktext/editor/buffer_manager.h>

/* Buffer data keys */
static const char *DATA_SUPPRESS_PARSE = "gtktext-suppress-reparse"; /* Still used locally */
const char *DATA_USER_DIRTY = "gtktext-user-dirty";        /* Exported for welcome_screen */
const char *DATA_ORIGINAL_TEXT = "gtktext-original-md";    /* Exported for welcome_screen */

/* ═══════════════════════════════════════════════════════════════════════════════
 * FORWARD DECLARATIONS - Function prototypes needed for cross-references
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Dialog completion callbacks */
void on_open_file_dialog_finish(GObject *source_object, GAsyncResult *res,
                                gpointer user_data);  /* Exported for welcome_screen */


/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Type definitions, structs
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Most types have been moved to their respective modules */

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Global state variables
 * ═══════════════════════════════════════════════════════════════════════════════ */

guint buffer_changed_signal_id = 0;     /* Store the signal handler ID - exported */
GSettings *app_settings = NULL;               /* org.gtk.gtktext settings - exported for welcome_screen */

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Utility and helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Try to make development runs work without manually exporting GSETTINGS_SCHEMA_DIR */

/* Debug helper: window/display/environment info for diagnosing dialog layout issues */
void debug_dump_window_env(GtkWindow *parent, const char *phase)  /* Exported for welcome_screen */
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

/* Helper function to get theme color with custom alpha */
gboolean get_theme_color_with_alpha(GtkWidget *widget, const char *color_name, 
                                   gdouble alpha, GdkRGBA *result)
{
    (void)widget; /* Unused parameter - keeping for API compatibility */
    
    /* Use AdwStyleManager for theme detection */
    AdwStyleManager *sm = adw_style_manager_get_default();
    gboolean prefer_dark = FALSE;
    if (sm) {
        AdwColorScheme cs = adw_style_manager_get_color_scheme(sm);
        prefer_dark = (cs == ADW_COLOR_SCHEME_FORCE_DARK || 
                      cs == ADW_COLOR_SCHEME_PREFER_DARK);
    }
    
    /* Define theme-aware colors based on common GTK theme color names */
    if (g_strcmp0(color_name, "theme_fg_color") == 0 || 
        g_strcmp0(color_name, "foreground") == 0) {
        if (prefer_dark) {
            gdk_rgba_parse(result, "#ffffff");
        } else {
            gdk_rgba_parse(result, "#000000");
        }
        result->alpha = alpha;
        return TRUE;
    } else if (g_strcmp0(color_name, "theme_bg_color") == 0 || 
               g_strcmp0(color_name, "background") == 0) {
        if (prefer_dark) {
            gdk_rgba_parse(result, "#242424");
        } else {
            gdk_rgba_parse(result, "#ffffff");
        }
        result->alpha = alpha;
        return TRUE;
    } else if (g_strcmp0(color_name, "theme_selected_bg_color") == 0 || 
               g_strcmp0(color_name, "accent") == 0) {
        if (prefer_dark) {
            gdk_rgba_parse(result, "#78aeed");
        } else {
            gdk_rgba_parse(result, "#3584e4");
        }
        result->alpha = alpha;
        return TRUE;
    }
    
    /* Use blue fallback colors instead of grey ones */
    if (prefer_dark) {
        /* Dark mode: use blue_4 (#1c71d8) equivalent */
        result->red = 28.0/255.0;
        result->green = 113.0/255.0;
        result->blue = 216.0/255.0;
        result->alpha = alpha;
    } else {
        /* Light mode: use blue_1 (#99c1f1) equivalent */
        result->red = 153.0/255.0;
        result->green = 193.0/255.0;
        result->blue = 241.0/255.0;
        result->alpha = alpha;
    }
    return FALSE; /* Indicate fallback was used */
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Event handlers, callbacks, signal handlers
 * ═══════════════════════════════════════════════════════════════════════════════ */


/* Handle link clicks in text view */
static void on_text_view_link_clicked(GtkGestureClick *gesture, 
                                     gint n_press, 
                                     gdouble x, gdouble y, 
                                     gpointer user_data)
{
    (void)gesture;  // Unused parameters  
    (void)n_press;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextIter iter;

    /* Convert click coordinates to buffer iterator */
    gtk_text_view_get_iter_at_location(text_view, &iter, x, y);

    /* Check if the iter has the "link" tag */
    GSList *tags = gtk_text_iter_get_tags(&iter);
    gboolean link_found = FALSE;
    const gchar *url = NULL;

    for (GSList *l = tags; l != NULL; l = l->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(l->data);
        gchar *tag_name = NULL;
        g_object_get(tag, "name", &tag_name, NULL);
        if (tag_name && g_str_has_prefix(tag_name, "link_")) {
            link_found = TRUE;
            url = g_object_get_data(G_OBJECT(tag), "link-url");
            g_free(tag_name);
            break;
        }
        g_free(tag_name);
    }
    g_slist_free(tags);

    if (link_found && url) {
        g_debug("Link clicked: %s", url);
        GtkWindow *window = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(text_view), 
                                                              GTK_TYPE_WINDOW));
        
        /* Use the modern GtkUriLauncher API */
        GtkUriLauncher *uri_launcher = gtk_uri_launcher_new(url);
        gtk_uri_launcher_launch(uri_launcher, window, NULL, NULL, NULL);
        g_object_unref(uri_launcher);
    }
}

/* Public API for status bar updates */
void gtktext_update_save_status(GtkApplication *app, const gchar *status)
{
    status_manager_update_save_status(app, status);
}

/* Async open-file completion callback */
void on_open_file_dialog_finish(GObject *source_object, GAsyncResult *res, 
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
        if (dir && app_settings) {
            g_settings_set_string(app_settings, "last-open-dir", dir);
            g_debug("[file-dialog] saved last-open-dir=%s", dir);
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
        welcome_screen_hide(app);
    }
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(0));
}

/* Removed welcome screen callbacks - moved to welcome_screen module */

/* DocumentManager callback functions */

static void 
on_document_state_changed(DocumentManager *dm, DocumentState old_state, 
                         DocumentState new_state, gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION(user_data);
    const gchar *file_path = document_manager_get_file_path(dm);
    
    g_debug("Document state changed: %d -> %d, file: %s", 
            old_state, new_state, file_path ? file_path : "(none)");
    
    /* Update status bar based on new state */
    status_manager_update_status_bar_for_state(app, new_state, file_path);
    
    /* Store current file path for reference */
    if (file_path) {
        g_object_set_data_full(G_OBJECT(app), "current_file_path", 
                              g_strdup(file_path), g_free);
    } else {
        g_object_set_data(G_OBJECT(app), "current_file_path", NULL);
    }
}



/* Status bar update functions and remaining event handlers */

static gboolean on_text_view_query_tooltip(GtkWidget *widget, gint x, gint y, 
                                          gboolean keyboard_mode, GtkTooltip *tooltip, 
                                          gpointer user_data)
{
    (void)user_data;
    GtkTextView *text_view = GTK_TEXT_VIEW(widget);
    GtkTextIter iter;

    /* Do not show tooltips if in keyboard navigation mode */
    if (keyboard_mode || !gtk_widget_has_focus(widget)) {
        return FALSE;
    }

    /* Get iterator at mouse position */
    gtk_text_view_get_iter_at_location(text_view, &iter, x, y);

    GSList *tags = gtk_text_iter_get_tags(&iter);
    const gchar *url = NULL;
    const gchar *title = NULL;
    gboolean link_found = FALSE;

    for (GSList *l = tags; l != NULL; l = l->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(l->data);
        gchar *tag_name = NULL;
        g_object_get(tag, "name", &tag_name, NULL);
        if (tag_name && g_str_has_prefix(tag_name, "link_")) {
            link_found = TRUE;
            url = g_object_get_data(G_OBJECT(tag), "link-url");
            title = g_object_get_data(G_OBJECT(tag), "link-title");
            g_free(tag_name);
            break;
        }
        g_free(tag_name);
    }
    g_slist_free(tags);

    if (link_found && url) {
        GString *tooltip_text = g_string_new(NULL);
        g_string_append_printf(tooltip_text, "Link: %s", url);
        if (title && *title) {
            g_string_append_printf(tooltip_text, "\nTitle: %s", title);
        }
        gtk_tooltip_set_text(tooltip, tooltip_text->str);
        g_string_free(tooltip_text, TRUE);
        return TRUE;
    }

    return FALSE;
}

/* Callback triggered when the text in the GtkTextBuffer changes */

/* Show dialog asking user to save unsaved changes */



/* Callback triggered when the main window requests to be closed */
/* Callback for keyboard shortcuts (Ctrl+C and zoom) */
static gboolean on_key_pressed(GtkEventControllerKey *controller,
                              guint keyval,
                              guint keycode,
                              GdkModifierType state,
                              gpointer user_data)
{
    (void)controller;
    (void)keycode;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);

    if (state & GDK_CONTROL_MASK) {
        /* Detect Ctrl+C */
        if (keyval == GDK_KEY_c) {
            g_debug("Ctrl+C detected");
            text_view_copy_selected_as_markdown(text_view);
            return TRUE;
        }
        /* Zoom in: Ctrl+plus/equal/KP_Add */
        else if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal || 
                keyval == GDK_KEY_KP_Add) {
            text_view_zoom(text_view, TRUE);
            return TRUE;
        }
        /* Zoom out: Ctrl+minus/KP_Subtract */
        else if (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract) {
            text_view_zoom(text_view, FALSE);
            return TRUE;
        }
        /* Reset zoom: Ctrl+0/KP_0 */
        else if (keyval == GDK_KEY_0 || keyval == GDK_KEY_KP_0) {
            /* Reset zoom to 100% */
            g_autoptr(GtkCssProvider) provider = gtk_css_provider_new();
            gtk_css_provider_load_from_string(provider, "textview { font-size: 100%; }");
            gtk_style_context_add_provider_for_display(
                gdk_display_get_default(),
                GTK_STYLE_PROVIDER(provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            gdouble *zoom_ptr = g_malloc(sizeof(gdouble));
            *zoom_ptr = 1.0;
            g_object_set_data_full(G_OBJECT(text_view), "zoom-level", zoom_ptr, g_free);
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean on_scroll_event(GtkEventControllerScroll *controller, gdouble dx, gdouble dy, 
                               gpointer user_data)
{
    (void)controller; (void)dx;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    
    GdkModifierType state = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(controller));
    
    if (state & GDK_CONTROL_MASK) {
        if (dy < 0) {
            text_view_zoom(text_view, TRUE);  /* Scroll up = zoom in */
        } else if (dy > 0) {
            text_view_zoom(text_view, FALSE); /* Scroll down = zoom out */
        }
        return TRUE;
    }
    
    return FALSE;
}

static void on_text_view_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y,
                               gpointer user_data)
{
    (void)controller;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextIter iter;
    gint trailing;

    /* Use get_iter_at_position for more accurate hit testing */
    if (!gtk_text_view_get_iter_at_position(text_view, &iter, &trailing, x, y)) {
        /* Fallback to text cursor if position is outside text area */
        gtk_widget_set_cursor_from_name(GTK_WIDGET(text_view), "text");
        return;
    }

    /* More precise handling: check both current position AND trailing position */
    gboolean has_link = FALSE;

    /* First check the exact character at the position */
    GSList *tags = gtk_text_iter_get_tags(&iter);
    for (GSList *l = tags; l != NULL; l = l->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(l->data);
        gchar *tag_name = NULL;
        g_object_get(tag, "name", &tag_name, NULL);

        if (tag_name && g_str_has_prefix(tag_name, "link_")) {
            has_link = TRUE;
            g_free(tag_name);
            break;
        }
        g_free(tag_name);
    }
    g_slist_free(tags);

    /* If no link found and we have trailing chars, check the trailing position too */
    if (!has_link && trailing > 0) {
        GtkTextIter trailing_iter = iter;
        gtk_text_iter_forward_chars(&trailing_iter, trailing);

        tags = gtk_text_iter_get_tags(&trailing_iter);
        for (GSList *l = tags; l != NULL; l = l->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(l->data);
            gchar *tag_name = NULL;
            g_object_get(tag, "name", &tag_name, NULL);

            if (tag_name && g_str_has_prefix(tag_name, "link_")) {
                has_link = TRUE;
                g_free(tag_name);
                break;
            }
            g_free(tag_name);
        }
        g_slist_free(tags);
    }

    /* Additional boundary check: ensure we're actually within character bounds */
    if (has_link) {
        /* Get the character rectangle to ensure we're really over the character */
        GdkRectangle char_rect;
        gtk_text_view_get_iter_location(text_view, &iter, &char_rect);

        /* Convert to widget coordinates */
        gint wx, wy;
        gtk_text_view_buffer_to_window_coords(text_view, GTK_TEXT_WINDOW_TEXT,
                                             char_rect.x, char_rect.y, &wx, &wy);

        /* Check if mouse is actually within reasonable bounds of the character */
        if (x < wx - 2 || x > wx + char_rect.width + 2) {
            has_link = FALSE;
        }
    }

    /* Set cursor based on whether we're over a link */
    if (has_link) {
        gtk_widget_set_cursor_from_name(GTK_WIDGET(text_view), "pointer");
    } else {
        gtk_widget_set_cursor_from_name(GTK_WIDGET(text_view), "text");
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * LIFECYCLE - Application initialization, activation, shutdown
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Temporary: Core app activation implementation for window_lifecycle to call */
void core_app_activate(GApplication *application)
{
    GtkApplication *app = GTK_APPLICATION(application);
    GtkBuilder *builder = gtk_builder_new();
    /* Set translation domain for .ui strings */
    gtk_builder_set_translation_domain(builder, GETTEXT_PACKAGE);

    /* Try installed path first, then fall back to local paths for dev */
    const char *ui_candidates[] = {
        PKGDATADIR "/ui/main_window.ui",
        "./ui/main_window.ui",
        "../ui/main_window.ui",
        NULL
    };
    gboolean ui_loaded = FALSE;
    for (int i = 0; ui_candidates[i] != NULL; i++) {
        if (g_file_test(ui_candidates[i], G_FILE_TEST_EXISTS)) {
            GError *err = NULL;
            ui_loaded = gtk_builder_add_from_file(builder, ui_candidates[i], &err);
            if (!ui_loaded) {
                g_warning("Failed to load UI from %s: %s", ui_candidates[i],
                         err ? err->message : "unknown error");
                g_clear_error(&err);
            }
            break;
        }
    }
    if (!ui_loaded) {
        g_critical("Failed to load any UI file for main_window");
        g_object_unref(builder);
        return;
    }

    /* Setup GSettings schemas before creating settings objects */
    settings_manager_setup_gsettings_schemas();

    /* Create main window */
    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!window) {
        g_critical("Failed to get main_window from UI");
        g_object_unref(builder);
        return;
    }
    gtk_window_set_application(GTK_WINDOW(window), GTK_APPLICATION(app));
    /* Ensure window action context has the application action group */
    gtk_widget_insert_action_group(window, "app", G_ACTION_GROUP(app));

    GtkWidget *text_view = GTK_WIDGET(gtk_builder_get_object(builder, "text_view"));
    if (!text_view) {
        g_critical("Failed to get text_view from UI");
        g_object_unref(builder);
        return;
    }

    /* Get status bar widgets */
    GtkWidget *save_status = GTK_WIDGET(gtk_builder_get_object(builder, "save_status"));
    GtkWidget *file_location = GTK_WIDGET(gtk_builder_get_object(builder, "file_location"));
    GtkWidget *main_stack = GTK_WIDGET(gtk_builder_get_object(builder, "main_stack"));
    GtkWidget *welcome_open_button = GTK_WIDGET(gtk_builder_get_object(builder, "welcome_open_button"));
    GtkWidget *welcome_new_button = GTK_WIDGET(gtk_builder_get_object(builder, "welcome_new_button"));

    if (!save_status || !file_location || !main_stack || !welcome_open_button || !welcome_new_button) {
        g_critical("Failed to get required UI components");
        g_object_unref(builder);
        return;
    }

    /* Expose widgets to application scope for actions to use */
    g_object_set_data(G_OBJECT(app), "text_view", text_view);
    g_object_set_data(G_OBJECT(app), "main_stack", main_stack);
    g_object_set_data(G_OBJECT(app), "welcome_open_button", welcome_open_button);
    g_object_set_data(G_OBJECT(app), "welcome_new_button", welcome_new_button);
    g_object_set_data(G_OBJECT(app), "save_status", save_status);
    g_object_set_data(G_OBJECT(app), "file_location", file_location);

    /* Initialize status bar with default values */
    status_manager_update_save_status(app, _("Ready"));
    status_manager_update_file_location(app, NULL);

    /* Create and initialize DocumentManager */
    GtkWindow *main_window = gtk_application_get_active_window(app);
    GtkTextBuffer *dm_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    DocumentManager *doc_manager = document_manager_new(dm_buffer, main_window);
    if (!doc_manager) {
        g_critical("Failed to create DocumentManager");
        g_object_unref(builder);
        return;
    }
    g_object_set_data_full(G_OBJECT(app), "doc_manager", doc_manager,
                          (GDestroyNotify)document_manager_free);

    /* Register DocumentManager callbacks */
    document_manager_set_state_callback(doc_manager, on_document_state_changed, app);

    /* Set up settings monitoring through settings manager */
    settings_manager_initialize_document_settings(app, doc_manager);

    /* Start autosave */
    document_manager_start_autosave(doc_manager);

    /* Create a global soup session for image fetching */
#ifdef HAVE_LIBSOUP
    SoupSession *global_soup_session = soup_session_new();
    g_object_set_data_full(G_OBJECT(app), "soup_session", global_soup_session,
                          g_object_unref);
#endif

    /* Create toolbar and add to container */
    GtkWidget *toolbar_container = GTK_WIDGET(gtk_builder_get_object(builder,
                                                                    "toolbar_container"));
    if (!toolbar_container) {
        g_critical("Failed to get toolbar_container from UI");
        g_object_unref(builder);
        return;
    }

    /* Add CSS styling to toolbar */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".toolbar { background-color: @theme_bg_color; border-bottom: 1px solid @borders; "
        "padding: 8px; margin: 4px; }"
        ".toolbar button { padding: 4px 8px; min-height: 24px; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* Store builder reference with window */
    g_object_set_data_full(G_OBJECT(window), "builder", g_object_ref(builder),
                          g_object_unref);

    /* Create toolbar and add to UI */
    GtkWidget *toolbar = create_toolbar(text_view);

    /* Ensure toolbar is visible and correctly added */
    if (toolbar && toolbar_container) {
        gtk_widget_set_visible(toolbar_container, TRUE);
        g_message("Toolbar is visible and active");
    }

    /* Setup key controller to detect Ctrl+C */
    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), text_view);
    gtk_widget_add_controller(text_view, key_controller);

    /* Setup motion controller for link hover cursor */
    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion", G_CALLBACK(on_text_view_motion), text_view);
    gtk_widget_add_controller(text_view, motion_controller);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    /* Initialize GSettings */
    app_settings = settings_manager_initialize_app_settings();
    if (!app_settings) {
        g_warning("GSettings schema org.gtk.gtktext not found");
    }

    /* Keep a back-pointer from buffer to the view for reparse callbacks */
    g_object_set_data(G_OBJECT(buffer), "gtktext-view", text_view);

    /* Store app reference on buffer for save dialog callbacks */
    g_object_set_data(G_OBJECT(buffer), "app", app);

    /* Store soup session on buffer for reparse callbacks */
#ifdef HAVE_LIBSOUP
    SoupSession *soup_session = g_object_get_data(G_OBJECT(app), "soup_session");
    if (soup_session) {
        g_object_set_data(G_OBJECT(buffer), "soup-session", soup_session);
    }
#endif

    /* Install blockquote overlay for visual left border */
    text_view_setup_blockquote_overlay(GTK_TEXT_VIEW(text_view));

    /* Connect welcome screen buttons */
    g_signal_connect(welcome_open_button, "clicked", G_CALLBACK(welcome_screen_open_cb), app);
    g_signal_connect(welcome_new_button, "clicked", G_CALLBACK(welcome_screen_new_cb), app);

    /* Store main_stack reference for setting visibility after window is shown */
    g_object_set_data(G_OBJECT(window), "main_stack", main_stack);

    /* Initialize buffer manager and connect change handlers */
    buffer_manager_initialize_buffer(buffer, app);
    buffer_changed_signal_id = g_signal_connect(buffer, "changed",
                                               G_CALLBACK(buffer_manager_on_text_changed), NULL);
    /* Initialize markdown engine for real-time processing */
#ifdef HAVE_LIBSOUP
    markdown_engine_initialize_buffer(buffer, GTK_TEXT_VIEW(text_view), soup_session);
#else
    markdown_engine_initialize_buffer(buffer, GTK_TEXT_VIEW(text_view), NULL);
#endif

    /* Enable tooltips and connect the query-tooltip signal */
    gtk_widget_set_has_tooltip(GTK_WIDGET(text_view), TRUE);
    g_signal_connect(text_view, "query-tooltip", G_CALLBACK(on_text_view_query_tooltip), NULL);

    /* Create a GtkGestureClick controller for link clicking */
    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_text_view_link_clicked), text_view);
    gtk_widget_add_controller(GTK_WIDGET(text_view), GTK_EVENT_CONTROLLER(click_gesture));

    /* Add zoom support with Ctrl+mouse wheel */
    GtkEventController *scroll_controller = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_controller, "scroll", G_CALLBACK(on_scroll_event), text_view);
    gtk_widget_add_controller(GTK_WIDGET(text_view), scroll_controller);

    /* Add signal for window close */
    g_signal_connect(window, "close-request", G_CALLBACK(window_lifecycle_on_window_close_request), text_view);

    /* Wire headerbar buttons to actions */
    GtkWidget *open_button = GTK_WIDGET(gtk_builder_get_object(builder, "open_button"));
    GtkWidget *save_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_button"));
    GtkWidget *save_as_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_as_button"));
    if (open_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(open_button), "app.open");
    if (save_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(save_button), "app.save");
    if (save_as_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(save_as_button),
                                                      "app.save-as");

    /* Accessibility: Provide accessible names for icon-only buttons */
    if (open_button) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(open_button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Open"), -1);
    }
    if (save_button) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(save_button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Save"), -1);
    }
    if (save_as_button) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(save_as_button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Save As"), -1);
    }

    g_object_set(text_view, "editable", TRUE, "cursor-visible", TRUE, NULL);
    g_signal_connect(text_view, "map", G_CALLBACK(window_lifecycle_on_map), text_view);

    /* Connect window map signal to set welcome screen after proper initialization */
    g_signal_connect(window, "map", G_CALLBACK(window_lifecycle_on_window_map), NULL);

    /* Check for autorecover on startup */
    dialogs_check_for_autorecover(app);

    gtk_window_present(GTK_WINDOW(window));
    g_object_unref(builder);
}

/* Temporary: Core app open implementation for window_lifecycle to call */
void core_app_open(GApplication *application, GFile **files, gint n_files,
                  const gchar *hint)
{
    (void)hint;
    /* First activate the application to ensure window is created */
    core_app_activate(application);

    if (n_files > 0) {
        /* Open the first file (ignore additional files for now) */
        GFile *file = files[0];
        g_autofree gchar *path = g_file_get_path(file);
        if (path) {
            g_debug("Opening file from command line: %s", path);
            GtkApplication *app = GTK_APPLICATION(application);
            DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
            if (dm) {
                GError *error = NULL;
                if (!document_manager_open_file(dm, path, &error)) {
                    g_warning("Failed to open file from command line: %s",
                             error ? error->message : "Unknown error");
                    g_clear_error(&error);
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    /* Optional flag: --debug enables verbose logging */
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--debug") == 0) {
            g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
            break;
        }
    }

    /* Enforce Wayland-only policy as per CLAUDE.md guidelines */
    const char *current_backend = g_getenv("GDK_BACKEND");
    if (!current_backend) {
        g_setenv("GDK_BACKEND", "wayland", TRUE);
        g_message("Enforcing Wayland-only policy: set GDK_BACKEND=wayland");
    } else if (g_strcmp0(current_backend, "x11") == 0) {
        g_warning("X11 backend detected but GTKText follows Wayland-only policy");
        g_warning("Consider running with: GDK_BACKEND=wayland %s", argv[0]);
        /* Override X11 with Wayland for compliance */
        g_setenv("GDK_BACKEND", "wayland", TRUE);
        g_message("Overriding X11 backend with Wayland for policy compliance");
    } else if (g_strcmp0(current_backend, "wayland") == 0) {
        g_debug("Wayland backend active - policy compliant");
    } else {
        g_message("Using backend '%s' - Wayland preferred per policy", current_backend);
    }

    /* In dev runs, locate local GSettings schemas automatically */
    settings_manager_setup_gsettings_schemas();
    
    /* Initialize i18n */
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    /* Hint GTK to use the desktop portal for native dialogs if available */
    if (!g_getenv("GTK_USE_PORTAL")) {
        g_setenv("GTK_USE_PORTAL", "1", FALSE);
        g_debug("Set GTK_USE_PORTAL=1 for file dialogs");
    }

    g_autoptr(AdwApplication) app = NULL;
    int status;

    app = adw_application_new("com.example.MiniTextEditor", G_APPLICATION_HANDLES_OPEN);

    const GActionEntry app_actions[] = {
        { "open", file_action_open_cb, NULL, NULL, NULL, {0} },
        { "save", file_action_save_cb, NULL, NULL, NULL, {0} },
        { "save-as", file_action_save_as_cb, NULL, NULL, NULL, {0} },
        { "preferences", app_action_preferences_cb, NULL, NULL, NULL, {0} },
        { "about", app_action_about_cb, NULL, NULL, NULL, {0} },
        { "shortcuts", app_action_shortcuts_cb, NULL, NULL, NULL, {0} },
    };

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, 
                                   G_N_ELEMENTS(app_actions), app);

  // Debug: To inspect actions at runtime, run with GTK_DEBUG=actions
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.open", (const char*[]){ "<primary>o", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.save", (const char*[]){ "<primary>s", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.save-as", (const char*[]){ "<primary><shift>s", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.preferences", (const char*[]){ "<primary>comma", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.shortcuts", (const char*[]){ "<primary>question", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.about", (const char*[]){ NULL });

  g_signal_connect (app, "activate", G_CALLBACK (window_lifecycle_app_activate), NULL);
  g_signal_connect (app, "open", G_CALLBACK (window_lifecycle_app_open), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);

  return status;
}