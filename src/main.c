/* C ULTRA-MIN TEMPLATE
   Purpose: Main application entry point and UI coordination for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - main.c
   Changed: Complete removal of legacy save system functions and variables
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

#include <gtktext/toolbar.h>
#include <gtktext/cmrender.h>
#include <gtktext/document_manager.h>
#include <gtktext/settings.h>

/* Buffer data keys for paste→markdown coordination */
static const char *DATA_SUPPRESS_PARSE = "gtktext-suppress-reparse";
static const char *DATA_REPARSE_SOURCE_ID = "gtktext-reparse-source-id";
static const char *DATA_REPARSE_TARGET_OFFSET = "gtktext-reparse-target-offset";
static const char *DATA_USER_DIRTY = "gtktext-user-dirty";
static const char *DATA_ORIGINAL_TEXT = "gtktext-original-md";

/* ═══════════════════════════════════════════════════════════════════════════════
 * FORWARD DECLARATIONS - Function prototypes needed for cross-references
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Core handlers and callbacks */
static gboolean reparse_markdown_cb(gpointer user_data);
static void on_buffer_insert_text(GtkTextBuffer *buffer, GtkTextIter *location,
                                  gchar *text, gint len, gpointer user_data);
static void on_text_changed(GtkTextBuffer *buffer, gpointer user_data);
static gboolean on_window_close_request(GtkWindow *window, gpointer user_data);

/* Helper functions */
static gboolean has_unsaved_changes(GtkTextBuffer *buffer);
static void show_unsaved_changes_dialog(GtkWindow *parent, GtkTextBuffer *buffer);
static void on_unsaved_changes_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data);
static void check_for_autorecover(GtkApplication *app);
static void show_autorecover_dialog(GtkWindow *parent, const char *autosave_path);
static void on_autorecover_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data);

/* Action callbacks */
static void action_open_cb(GSimpleAction *a, GVariant *p, gpointer user_data);
static void action_save_cb(GSimpleAction *a, GVariant *p, gpointer user_data);
static void action_save_as_cb(GSimpleAction *a, GVariant *p, gpointer user_data);
static void action_preferences_cb(GSimpleAction *a, GVariant *p, gpointer user_data);
static void action_about_cb(GSimpleAction *a, GVariant *p, gpointer user_data);
static void action_shortcuts_cb(GSimpleAction *a, GVariant *p, gpointer user_data);

/* Status bar functions */
static void update_save_status(GtkApplication *app, const gchar *status);
static void update_file_location(GtkApplication *app, const gchar *location);
static void update_status_bar_for_state(GtkApplication *app, DocumentState state, 
                                       const gchar *file_path);

/* Public status bar API for DocumentManager integration */
void gtktext_update_status_bar_for_document_state(GtkApplication *app, 
                                                   DocumentState state, 
                                                   const gchar *file_path);

/* UI event handlers */
static void on_embedded_image_pressed(GtkGestureClick *gesture, gint n_press, 
                                     gdouble x, gdouble y, gpointer user_data);
static void on_picture_paintable_notify(GObject *object, GParamSpec *pspec, 
                                       gpointer user_data);
static void on_text_view_link_clicked(GtkGestureClick *gesture, gint n_press, 
                                     gdouble x, gdouble y, gpointer user_data);
static gboolean on_text_view_query_tooltip(GtkWidget *widget, gint x, gint y, 
                                          gboolean keyboard_mode, GtkTooltip *tooltip, 
                                          gpointer user_data);
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, 
                              guint keycode, GdkModifierType state, gpointer user_data);
static gboolean on_scroll_event(GtkEventControllerScroll *controller, gdouble dx,
                               gdouble dy, gpointer user_data);
static void on_text_view_motion(GtkEventControllerMotion *controller, gdouble x,
                               gdouble y, gpointer user_data);

/* Dialog completion callbacks */
static void on_open_file_dialog_finish(GObject *source_object, GAsyncResult *res, 
                                      gpointer user_data);
static void on_save_as_dialog_finish(GObject *source_object, GAsyncResult *res, 
                                    gpointer user_data);

/* Welcome screen handlers */
static void welcome_open_cb(GtkButton *button, gpointer user_data);
static void welcome_new_cb(GtkButton *button, gpointer user_data);

/* Window lifecycle */
static void on_map(GtkWidget *widget, gpointer user_data);
static void on_window_map(GtkWidget *window, gpointer user_data);
static void app_activate(GApplication *application);
static void app_open(GApplication *application, GFile **files, gint n_files, 
                    const gchar *hint);

/* Utility and helper functions */
static void copy_selected_text_as_markdown(GtkTextView *text_view);
static void zoom_text_view(GtkTextView *text_view, gboolean zoom_in);
static void setup_file_dialog_filters(GtkFileDialog *dialog);
static void setup_save_dialog_filters(GtkFileDialog *dialog);
static void setup_blockquote_overlay(GtkTextView *text_view);
static void embed_images_in_text_view(GtkTextView *text_view);
/* Legacy settings handler removed */

/* Drawing and overlay functions */
static void on_bq_overlay_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, 
                              gpointer user_data);
static void on_adjustment_changed(GObject *adj, GParamSpec *pspec, gpointer user_data);

/* Helper functions */
static void collect_bq(GtkTextTag *tag, gpointer user_data);
static void free_user_data_notify(gpointer data, GClosure *closure);
static void embed_foreach_tag(GtkTextTag *tag, gpointer user_data);

static void collect_bq(GtkTextTag *tag, gpointer user_data);
static void free_user_data_notify(gpointer data, GClosure *closure);
static void embed_foreach_tag(GtkTextTag *tag, gpointer user_data);

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Type definitions, structs
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    GtkTextBuffer *buffer;
    GtkTextView *view;
    GtkTextTag *hidden;
} EmbedCtx;

typedef struct {
    GPtrArray *arr;
} TagCollect;

#ifdef HAVE_LIBSOUP
typedef struct {
    GtkTextBuffer *buffer;  /* ref */
    GtkTextView *view;      /* ref */
    GtkTextTag *tag;        /* ref */
    GtkTextTag *hidden;     /* weak (owned by buffer) */
    SoupSession *session;   /* ref */
    SoupMessage *msg;       /* ref */
    GtkTextMark *mark;      /* ref (anchor position) */
    char *url;              /* owned (image source) */
    char *open_url;         /* owned (prefer outer link URL on click) */
    char *title;            /* owned */
    char *alt;              /* owned */
} RemoteImageCtx;

/* Forward declarations that depend on RemoteImageCtx */
static void on_http_image_fetched(SoupSession *session, GAsyncResult *res, 
                                 gpointer user_data);
static void remote_image_ctx_free(RemoteImageCtx *c);
#endif

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Global state variables
 * ═══════════════════════════════════════════════════════════════════════════════ */

static guint buffer_changed_signal_id = 0;     /* Store the signal handler ID */
static GSettings *app_settings = NULL;         /* org.gtk.gtktext settings */

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Utility and helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Try to make development runs work without manually exporting GSETTINGS_SCHEMA_DIR */
static void maybe_setup_gsettings_schemas(void)
{
    const char *already = g_getenv("GSETTINGS_SCHEMA_DIR");
    if (already && *already) return;

    const char *candidates[] = { "./data", "../data", NULL };
    for (int i = 0; candidates[i]; i++) {
        const char *dir = candidates[i];
        g_autofree char *compiled = g_build_filename(dir, "gschemas.compiled", NULL);
        g_autofree char *xml = g_build_filename(dir, "org.gtk.gtktext.gschema.xml", NULL);
        
        if (g_file_test(compiled, G_FILE_TEST_EXISTS)) {
            gboolean needs_recompile = FALSE;
            if (g_file_test(xml, G_FILE_TEST_EXISTS)) {
                GStatBuf st_xml = {0}, st_comp = {0};
                if (g_stat(xml, &st_xml) == 0 && g_stat(compiled, &st_comp) == 0) {
                    if (st_xml.st_mtime > st_comp.st_mtime) {
                        needs_recompile = TRUE;
                    }
                }
            }
            if (needs_recompile) {
                g_message("Recompiling GSettings schemas under %s (XML newer)", dir);
                gchar *argv[] = { "glib-compile-schemas", (gchar*)dir, NULL };
                gint status = 0;
                GError *err = NULL;
                if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, 
                                  NULL, NULL, &status, &err)) {
                    g_warning("Failed to spawn glib-compile-schemas: %s", 
                              err ? err->message : "unknown error");
                    g_clear_error(&err);
                }
            }
            g_setenv("GSETTINGS_SCHEMA_DIR", dir, TRUE);
            g_message("Using local GSettings schemas at %s", dir);
            return;
        }
    }

    /* Try to compile schemas if XML is present and tool is available */
    for (int i = 0; candidates[i]; i++) {
        const char *dir = candidates[i];
        g_autofree char *xml = g_build_filename(dir, "org.gtk.gtktext.gschema.xml", NULL);
        if (!g_file_test(xml, G_FILE_TEST_EXISTS)) continue;

        g_message("Compiling GSettings schemas under %s", dir);
        gchar *argv[] = { "glib-compile-schemas", (gchar*)dir, NULL };
        gint status = 0;
        GError *err = NULL;
        if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, 
                        NULL, NULL, &status, &err)) {
            if (status == 0) {
                g_autofree char *compiled = g_build_filename(dir, "gschemas.compiled", NULL);
                if (g_file_test(compiled, G_FILE_TEST_EXISTS)) {
                    g_setenv("GSETTINGS_SCHEMA_DIR", dir, TRUE);
                    g_message("Compiled and using local GSettings schemas at %s", dir);
                    return;
                }
            } else {
                g_debug("glib-compile-schemas exited with status %d", status);
            }
        } else {
            g_debug("Failed to spawn glib-compile-schemas: %s", 
                    err ? err->message : "unknown error");
            g_clear_error(&err);
        }
    }
}

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

/* Zoom functionality */
static void zoom_text_view(GtkTextView *text_view, gboolean zoom_in)
{
    if (!text_view) return;
    
    /* Get current zoom level from widget data, default to 1.0 */
    gdouble *stored_zoom = g_object_get_data(G_OBJECT(text_view), "zoom-level");
    gdouble current_zoom = stored_zoom ? *stored_zoom : 1.0;
    
    const gdouble zoom_step = 0.1;
    const gdouble min_zoom = 0.5;
    const gdouble max_zoom = 3.0;
    
    if (zoom_in && current_zoom < max_zoom) {
        current_zoom += zoom_step;
    } else if (!zoom_in && current_zoom > min_zoom) {
        current_zoom -= zoom_step;
    } else {
        return; /* No change needed */
    }
    
    /* Apply zoom via CSS using modern API with proper formatting */
    /* Use integer scaling in percentage to avoid locale decimal issues */
    g_autoptr(GtkCssProvider) provider = gtk_css_provider_new();
    gint zoom_percent = (gint)(current_zoom * 100.0);
    g_autofree gchar *css = g_strdup_printf("textview { font-size: %d%%; }", zoom_percent);
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    /* Store updated zoom level */
    gdouble *zoom_ptr = g_malloc(sizeof(gdouble));
    *zoom_ptr = current_zoom;
    g_object_set_data_full(G_OBJECT(text_view), "zoom-level", zoom_ptr, g_free);
}

/* Legacy get_save_file_path function removed - DocumentManager handles file paths */

/* Helper function to set up file filters for open dialogs */
static void setup_file_dialog_filters(GtkFileDialog *dialog)
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
static void setup_save_dialog_filters(GtkFileDialog *dialog)
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

/* Schedules a short idle/timeout to re-parse the entire buffer as CommonMark */
void schedule_reparse_markdown(GtkTextBuffer *buffer, gint inserted_len, 
                               const GtkTextIter *at_iter)
{
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));
    g_return_if_fail(at_iter != NULL);
    
    /* Avoid scheduling if a reparse is already queued */
    guint existing = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer), 
                                                        DATA_REPARSE_SOURCE_ID));
    if (existing != 0) {
        g_source_remove(existing);
    }

    /* Record a target offset near the end of the inserted text to restore cursor */
    if (at_iter) {
        gint base = gtk_text_iter_get_offset((GtkTextIter*)at_iter);
        gint target = base + (inserted_len > 0 ? inserted_len : 0);
        g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_TARGET_OFFSET, 
                         GINT_TO_POINTER(target));
    }

    /* Performance optimization: adaptive delay based on buffer size */
    gint char_count = gtk_text_buffer_get_char_count(buffer);
    guint delay_ms = 30; /* Base delay */

    if (char_count > 10000) {
        delay_ms = 100;    /* Larger buffers get longer delay */
    } else if (char_count > 50000) {
        delay_ms = 200;    /* Very large buffers get even longer delay */
    }

    guint id = g_timeout_add_full(G_PRIORITY_LOW, delay_ms, reparse_markdown_cb, 
                                  g_object_ref(buffer), g_object_unref);
    g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID, GUINT_TO_POINTER(id));
}

/* Legacy save function removed - DocumentManager handles all save operations */

/* Konverterer valgt tekst til markdown og kopierer til udklipsholderen */
static void copy_selected_text_as_markdown(GtkTextView *text_view)
{
    g_debug("copy_selected_text_as_markdown: start");

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter start_sel, end_sel;

    if (!gtk_text_buffer_get_selection_bounds(buffer, &start_sel, &end_sel)) {
        g_debug("copy_selected_text_as_markdown: no selection");
        return;
    }
    g_debug("copy_selected_text_as_markdown: selection found");

    GString *md = g_string_new("");
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(buffer, &iter, 
                                       gtk_text_iter_get_offset(&start_sel));

    gboolean currently_in_bold = FALSE;
    gboolean currently_in_italic = FALSE;
    gboolean currently_in_code = FALSE;
    gboolean currently_in_codeblock = FALSE;
    
    GtkTextIter temp_line_start_iter = iter;
    gtk_text_iter_set_line_offset(&temp_line_start_iter, 0);
    gboolean at_line_start = gtk_text_iter_equal(&iter, &temp_line_start_iter);

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

    while(gtk_text_iter_compare(&iter, &end_sel) < 0) {
        gunichar current_char = gtk_text_iter_get_char(&iter);

        gboolean iter_is_bold = FALSE;
        gboolean iter_is_italic = FALSE;
        gboolean iter_is_code = FALSE;
        gboolean iter_is_codeblock_char = FALSE;
        gboolean iter_is_h1 = FALSE, iter_is_h2 = FALSE, iter_is_h3 = FALSE;
        gboolean iter_is_h4 = FALSE, iter_is_h5 = FALSE, iter_is_h6 = FALSE;

        GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
        for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(l->data);
            gchar* tag_name = NULL;
            g_object_get(tag, "name", &tag_name, NULL);
            if (tag_name) {
                if (g_strcmp0(tag_name, "bold") == 0) iter_is_bold = TRUE;
                else if (g_strcmp0(tag_name, "italic") == 0) iter_is_italic = TRUE;
                else if (g_strcmp0(tag_name, "code") == 0) iter_is_code = TRUE;
                else if (g_strcmp0(tag_name, "codeblock") == 0) iter_is_codeblock_char = TRUE;
                else if (g_strcmp0(tag_name, "h1") == 0) iter_is_h1 = TRUE;
                else if (g_strcmp0(tag_name, "h2") == 0) iter_is_h2 = TRUE;
                else if (g_strcmp0(tag_name, "h3") == 0) iter_is_h3 = TRUE;
                else if (g_strcmp0(tag_name, "h4") == 0) iter_is_h4 = TRUE;
                else if (g_strcmp0(tag_name, "h5") == 0) iter_is_h5 = TRUE;
                else if (g_strcmp0(tag_name, "h6") == 0) iter_is_h6 = TRUE;
                g_free(tag_name);
            }
        }
        g_slist_free(tags_at_iter);

        if (at_line_start) {
            GtkTextTag *hr_tag = gtk_text_tag_table_lookup(tag_table, "hr");
            if (hr_tag && gtk_text_iter_has_tag(&iter, hr_tag) && !currently_in_codeblock) {
                g_string_append(md, "---\\n");
                
                GtkTextIter line_end_iter = iter;
                gtk_text_iter_forward_to_line_end(&line_end_iter);
                iter = line_end_iter;

                if (gtk_text_iter_compare(&iter, &end_sel) < 0) {
                    gtk_text_iter_forward_char(&iter);
                    if (gtk_text_iter_compare(&iter, &end_sel) < 0 && 
                        gtk_text_iter_get_char(&iter) == '\n') {
                        gtk_text_iter_forward_char(&iter);
                    }
                }
                at_line_start = TRUE;
                if (gtk_text_iter_compare(&iter, &end_sel) >= 0) break;
                continue;
            }

            /* Replace visual unordered bullets with '-' for Markdown copy */
            if (!currently_in_codeblock) {
                GtkTextTag *ul_tag = gtk_text_tag_table_lookup(tag_table, "ul_bullet");
                GtkTextIter tmp = iter;
                /* Preserve and output leading spaces (indentation) */
                while (gtk_text_iter_compare(&tmp, &end_sel) < 0 && 
                       gtk_text_iter_get_char(&tmp) == ' ') {
                    g_string_append_c(md, ' ');
                    gtk_text_iter_forward_char(&tmp);
                }
                if (ul_tag && gtk_text_iter_compare(&tmp, &end_sel) < 0 && 
                    gtk_text_iter_has_tag(&tmp, ul_tag)) {
                    /* Emit standard Markdown bullet and skip visual bullet + following space */
                    g_string_append(md, "- ");
                    gtk_text_iter_forward_char(&tmp); /* bullet char */
                    if (gtk_text_iter_compare(&tmp, &end_sel) < 0) {
                        gtk_text_iter_forward_char(&tmp); /* trailing space */
                    }
                    iter = tmp;
                    at_line_start = FALSE;
                    continue;
                }
            }

            if (!currently_in_codeblock) {
                gboolean heading_started_here = FALSE;
                if (iter_is_h1) { g_string_append(md, "# "); heading_started_here = TRUE; }
                else if (iter_is_h2) { g_string_append(md, "## "); heading_started_here = TRUE; }
                else if (iter_is_h3) { g_string_append(md, "### "); heading_started_here = TRUE; }
                else if (iter_is_h4) { g_string_append(md, "#### "); heading_started_here = TRUE; }
                else if (iter_is_h5) { g_string_append(md, "##### "); heading_started_here = TRUE; }
                else if (iter_is_h6) { g_string_append(md, "###### "); heading_started_here = TRUE; }

                if (!heading_started_here) {
                    GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");
                    if (codeblock_tag && gtk_text_iter_has_tag(&iter, codeblock_tag)) {
                        g_string_append(md, "```\n");
                        currently_in_codeblock = TRUE;
                    }
                }
            }
        }

        if (currently_in_codeblock && !iter_is_codeblock_char && current_char != '\n') {
            if (md->str[md->len -1] != '\n') {
                g_string_append_c(md, '\n');
            }
            g_string_append(md, "```\n");
            currently_in_codeblock = FALSE;
        }
        
        if (currently_in_codeblock) {
            g_string_append_unichar(md, current_char);
        } else {
            /* Inline formatting */
            if (iter_is_bold && iter_is_italic && !currently_in_bold && !currently_in_italic) {
                g_string_append(md, "***");
                currently_in_bold = TRUE;
                currently_in_italic = TRUE;
            } else if (!iter_is_bold && !iter_is_italic && currently_in_bold && 
                      currently_in_italic) {
                g_string_append(md, "***");
                currently_in_bold = FALSE;
                currently_in_italic = FALSE;
            } else {
                if (iter_is_bold && !currently_in_bold) {
                    g_string_append(md, "**");
                    currently_in_bold = TRUE;
                } else if (!iter_is_bold && currently_in_bold) {
                    g_string_append(md, "**");
                    currently_in_bold = FALSE;
                }

                if (iter_is_italic && !currently_in_italic) {
                    g_string_append(md, "*");
                    currently_in_italic = TRUE;
                } else if (!iter_is_italic && currently_in_italic) {
                    g_string_append(md, "*");
                    currently_in_italic = FALSE;
                }
            }

            /* Handle inline code state changes BEFORE appending the character */
            if (currently_in_code && !iter_is_code) { /* Leaving a code span */
                g_string_append_c(md, '`');
                currently_in_code = FALSE;
            }
            if (iter_is_code && !currently_in_code) { /* Entering a code span */
                g_string_append_c(md, '`');
                currently_in_code = TRUE;
            }
            
            g_string_append_unichar(md, current_char); /* Append the character itself */
        }

        if (current_char == '\n') {
            at_line_start = TRUE;
            if (currently_in_codeblock) {
                /* Check if the *next* char still has codeblock tag */
                GtkTextIter next_char_iter = iter;
                gtk_text_iter_forward_char(&next_char_iter);
                if (gtk_text_iter_compare(&next_char_iter, &end_sel) >= 0 ||
                    !gtk_text_iter_has_tag(&next_char_iter, 
                                          gtk_text_tag_table_lookup(tag_table, "codeblock"))) {
                    /* This newline is the last line of the code block content */
                }
            }
        } else {
            at_line_start = FALSE;
        }
        
        gtk_text_iter_forward_char(&iter);
    }

    if (currently_in_code) {
        g_string_append_c(md, '`');
    }
    if (currently_in_codeblock) {
        if (md->len > 0 && md->str[md->len -1] != '\n') {
            g_string_append_c(md, '\n');
        }
        g_string_append(md, "```" "\n");
    }
    /* Ensure bold/italic are closed if selection ends mid-format */
    if (currently_in_bold && currently_in_italic) g_string_append(md, "***");
    else if (currently_in_bold) g_string_append(md, "**");
    else if (currently_in_italic) g_string_append(md, "*");

    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(text_view));
    gdk_clipboard_set_text(clipboard, md->str);
    g_message("Copied selection to clipboard as markdown.");
    g_string_free(md, TRUE);
}

/* Helper to collect blockquote tags */
static void collect_bq(GtkTextTag *tag, gpointer user_data)
{
    TagCollect *C = (TagCollect*)user_data;
    const char *nm = g_object_get_data(G_OBJECT(tag), "tag-name");
    if (nm && g_str_has_prefix(nm, "blockquote")) {
        g_ptr_array_add(C->arr, g_object_ref(tag));
    }
}

/* Free user data notify function */
static void free_user_data_notify(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

#ifdef HAVE_LIBSOUP
/* Free remote image context */
static void remote_image_ctx_free(RemoteImageCtx *c)
{
    if (!c) return;
    if (c->buffer) g_object_unref(c->buffer);
    if (c->view) g_object_unref(c->view);
    if (c->tag) g_object_unref(c->tag);
    if (c->session) g_object_unref(c->session);
    if (c->msg) g_object_unref(c->msg);
    if (c->mark) gtk_text_buffer_delete_mark(c->buffer, c->mark);
    g_free(c->url);
    g_free(c->open_url);
    g_free(c->title);
    g_free(c->alt);
    g_free(c);
}
#endif

/* Embed images in text view */
static void embed_foreach_tag(GtkTextTag *tag, gpointer user_data)
{
    EmbedCtx *c = (EmbedCtx*)user_data;
    if (!tag || !c || !c->buffer || !c->view) return;
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tag), "image-embedded")) == 1) return;

    gchar *name = NULL;
    g_object_get(tag, "name", &name, NULL);
    if (!name) return;
    
    /* Only treat tags as images if they carry actual image metadata */
    gboolean is_image = FALSE;
    if (g_str_has_prefix(name, "image_")) {
        /* Skip the alt-hidden helper tag if present from older buffers */
        if (g_strcmp0(name, "image_alt_hidden") != 0) {
            const char *has_url = g_object_get_data(G_OBJECT(tag), "image-url");
            if (has_url && *has_url) is_image = TRUE;
        }
    }
    if (is_image) {
        g_debug("[embed] found image tag: %s", name);
    }
    g_free(name);
    if (!is_image) return;

    const char *url = (const char*) g_object_get_data(G_OBJECT(tag), "image-url");
    const char *title = (const char*) g_object_get_data(G_OBJECT(tag), "image-title");
    const char *alt = (const char*) g_object_get_data(G_OBJECT(tag), "image-alt");
    
    g_debug("[embed] image tag data - url=%s, title=%s, alt=%s", 
            url ? url : "(null)", title ? title : "(null)", alt ? alt : "(null)");

    /* First collect all [start,end) offsets for this tag */
    typedef struct { int start; int end; } Range;
    GArray *ranges = g_array_new(FALSE, FALSE, sizeof(Range));

    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(c->buffer, &iter);
    gboolean inside = gtk_text_iter_has_tag(&iter, tag);
    GtkTextIter range_start;
    while (gtk_text_iter_forward_to_tag_toggle(&iter, tag)) {
        if (!inside) {
            range_start = iter;
            inside = TRUE;
        } else {
            GtkTextIter range_end = iter;
            Range r;
            r.start = gtk_text_iter_get_offset(&range_start);
            r.end = gtk_text_iter_get_offset(&range_end);
            g_array_append_val(ranges, r);
            inside = FALSE;
        }
    }

    /* Process from end to start to keep offsets valid when inserting anchors */
    for (gint i = ranges->len - 1; i >= 0; i--) {
        Range r = g_array_index(ranges, Range, i);
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_offset(c->buffer, &s, r.start);
        gtk_text_buffer_get_iter_at_offset(c->buffer, &e, r.end);

        /* Preserve start position across mutations */
        GtkTextMark *start_mark = gtk_text_buffer_create_mark(c->buffer, NULL, &s, TRUE);

        /* Inspect if this span is inside a link to prefer its URL for navigation */
        const char *link_url = NULL;
        GSList *tags_here = gtk_text_iter_get_tags(&s);
        for (GSList *tl = tags_here; tl; tl = tl->next) {
            GtkTextTag *t = (GtkTextTag*)tl->data;
            gchar *tname = NULL;
            g_object_get(t, "name", &tname, NULL);
            if (tname && g_str_has_prefix(tname, "link_")) {
                link_url = g_object_get_data(G_OBJECT(t), "link-url");
                g_free(tname);
                break;
            }
            g_free(tname);
        }
        g_slist_free(tags_here);

        /* Build child widget */
        GtkWidget *child = NULL;
        if (url && *url) {
            const char *scheme = g_uri_parse_scheme(url);
#ifdef HAVE_LIBSOUP
            if (scheme && (g_ascii_strcasecmp(scheme, "http") == 0 || 
                          g_ascii_strcasecmp(scheme, "https") == 0)) {
                /* Remote: fetch async with libsoup; create anchor/picture after load */
                SoupSession *session = soup_session_new();
                SoupMessage *msg = soup_message_new("GET", url);
                /* Friendly headers for CDNs that require UA/Accept */
                SoupMessageHeaders *hdr = soup_message_get_request_headers(msg);
                soup_message_headers_replace(hdr, "User-Agent", 
                    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
                    "Chrome/124.0 Safari/537.36 GTKText/0.1");
                soup_message_headers_replace(hdr, "Accept", "image/*,*/*;q=0.5");
                
                RemoteImageCtx *rix = g_new0(RemoteImageCtx, 1);
                rix->buffer = g_object_ref(c->buffer);
                rix->view = g_object_ref(c->view);
                rix->tag = g_object_ref(tag);
                rix->session = g_object_ref(session);
                rix->msg = g_object_ref(msg);
                rix->url = g_strdup(url);
                rix->open_url = g_strdup(link_url ? link_url : url);
                rix->title = title ? g_strdup(title) : NULL;
                rix->alt = alt ? g_strdup(alt) : NULL;
                rix->hidden = c->hidden;
                rix->mark = gtk_text_buffer_create_mark(c->buffer, NULL, &s, TRUE);
                
                soup_session_send_and_read_async(session, msg,
                                                G_PRIORITY_DEFAULT,
                                                NULL,
                                                (GAsyncReadyCallback)on_http_image_fetched,
                                                rix);
                g_object_unref(session);
                /* Defer anchor creation; keep alt visible until success */
                child = NULL;
                /* We will not use this local mark; delete it to avoid leaking */
                gtk_text_buffer_delete_mark(c->buffer, start_mark);
            } else
#endif
            {
                /* Use GIO to load from URI */
                GFile *gf = g_file_new_for_uri(url);
                child = gtk_picture_new_for_file(gf);
                g_object_unref(gf);
                /* Hide alt BEFORE inserting the anchor */
                if (c->hidden) gtk_text_buffer_apply_tag(c->buffer, c->hidden, &s, &e);
                g_debug("[embed] GtkPicture created from GFile: %s (scheme=%s)", 
                        url, scheme ? scheme : "(null)");
                g_signal_connect_data(child, "notify::paintable", 
                                     G_CALLBACK(on_picture_paintable_notify), 
                                     g_strdup(url), free_user_data_notify, 0);
                /* Tooltip and click navigation; prefer outer link when present */
                if (title && *title) gtk_widget_set_tooltip_text(child, title);
                else gtk_widget_set_tooltip_text(child, link_url ? link_url : url);
                GtkGesture *click = gtk_gesture_click_new();
                gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
                g_signal_connect(click, "pressed", G_CALLBACK(on_embedded_image_pressed), child);
                gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(click));
                g_object_set_data_full(G_OBJECT(child), "open-url", 
                                       g_strdup(link_url ? link_url : url), g_free);
                g_object_set_data_full(G_OBJECT(child), "image-url", g_strdup(url), g_free);
            }

            if (child) {
                /* Accessibility: set accessible label from alt or title */
                if (alt && *alt) {
                    gtk_accessible_update_property(GTK_ACCESSIBLE(child),
                        GTK_ACCESSIBLE_PROPERTY_LABEL, alt, -1);
                } else if (title && *title) {
                    gtk_accessible_update_property(GTK_ACCESSIBLE(child),
                        GTK_ACCESSIBLE_PROPERTY_LABEL, title, -1);
                }

                /* Tooltip */
                if (title && *title) gtk_widget_set_tooltip_text(child, title);
                else gtk_widget_set_tooltip_text(child, url);

                /* Clickable */
                GtkGesture *click = gtk_gesture_click_new();
                gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
                g_signal_connect(click, "pressed", G_CALLBACK(on_embedded_image_pressed), child);
                gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(click));
                g_object_set_data_full(G_OBJECT(child), "image-url", g_strdup(url), g_free);
            }
        } else {
            const char *txt = (alt && *alt) ? alt : _("Image");
            child = gtk_label_new(txt);
            gtk_widget_add_css_class(child, "dim-label");
            /* No actual image; don't hide alt text */
        }

        /* Insert child at preserved position only if we have a widget now */
        if (child) {
            GtkTextIter anchor_pos;
            gtk_text_buffer_get_iter_at_mark(c->buffer, &anchor_pos, start_mark);
            GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(c->buffer, 
                                                                            &anchor_pos);
            gtk_text_view_add_child_at_anchor(c->view, child, anchor);
            gtk_text_buffer_delete_mark(c->buffer, start_mark);
        }
    }

    g_array_free(ranges, TRUE);
    g_object_set_data(G_OBJECT(tag), "image-embedded", GINT_TO_POINTER(1));
}

static void embed_images_in_text_view(GtkTextView *text_view)
{
    if (!text_view || !GTK_IS_TEXT_VIEW(text_view)) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    if (!buffer) return;
    g_message("[embed] *** SCANNING BUFFER FOR IMAGES ***");

    /* Create or lookup a tag used to hide alt text */
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    /* Use a tag name that does NOT collide with real image_* tags */
    GtkTextTag *hidden = gtk_text_tag_table_lookup(table, "img_alt_hidden");
    if (!hidden) {
        /* Backwards-compatibility: if previous name exists, reuse it */
        hidden = gtk_text_tag_table_lookup(table, "image_alt_hidden");
    }
    if (!hidden) {
        /* Avoid object replacement glyphs: make text transparent instead of invisible */
        GdkRGBA transparent = {0, 0, 0, 0};
        hidden = gtk_text_buffer_create_tag(buffer, "img_alt_hidden",
                                           "foreground-rgba", &transparent,
                                           NULL);
    }

    EmbedCtx ctx = { buffer, text_view, hidden };
    gtk_text_tag_table_foreach(table, embed_foreach_tag, &ctx);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Event handlers, callbacks, signal handlers
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Click handler for embedded images: opens the image URL via GtkUriLauncher */
static void on_embedded_image_pressed(GtkGestureClick *gesture, 
                                     gint n_press, 
                                     gdouble x, 
                                     gdouble y, 
                                     gpointer user_data)
{
    (void)gesture;  // Unused parameters
    (void)n_press;
    (void)x;
    (void)y;
    
    GtkWidget *widget = GTK_WIDGET(user_data);
    /* Prefer an outer link target if provided; fall back to the image URL */
    const char *url = (const char*) g_object_get_data(G_OBJECT(widget), "open-url");
    if (!url) url = (const char*) g_object_get_data(G_OBJECT(widget), "image-url");
    if (!url || !*url) return;
    
    GtkWidget *view = gtk_widget_get_ancestor(widget, GTK_TYPE_TEXT_VIEW);
    GtkWindow *window = view ? GTK_WINDOW(gtk_widget_get_ancestor(view, GTK_TYPE_WINDOW)) : NULL;
    GtkUriLauncher *launcher = gtk_uri_launcher_new(url);
    gtk_uri_launcher_launch(launcher, window, NULL, NULL, NULL);
    g_object_unref(launcher);
}

/* Logs when a GtkPicture finishes loading its paintable */
static void on_picture_paintable_notify(GObject *object, GParamSpec *pspec, 
                                       gpointer user_data)
{
    (void)pspec;
    const char *src = (const char*)user_data;
    GtkPicture *pic = GTK_PICTURE(object);
    GdkPaintable *p = gtk_picture_get_paintable(pic);
    if (p) {
        int iw = gdk_paintable_get_intrinsic_width(p);
        int ih = gdk_paintable_get_intrinsic_height(p);
        g_debug("[embed] picture loaded: %s (%dx%d)", src ? src : "(null)", iw, ih);
    } else {
        g_debug("[embed] picture paintable cleared: %s", src ? src : "(null)");
    }
}

/* Helper to queue redraw when scrolling changes */
static void on_adjustment_changed(GObject *adj, GParamSpec *pspec, gpointer user_data)
{
    (void)adj; (void)pspec;
    GtkDrawingArea *area = GTK_DRAWING_AREA(user_data);
    if (GTK_IS_WIDGET(area)) {
        gtk_widget_queue_draw(GTK_WIDGET(area));
    }
}

/* Draw thin left borders for visible blockquote ranges */
static void on_bq_overlay_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, 
                              gpointer user_data)
{
    (void)area; (void)width; (void)height;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    if (!text_view) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);

    /* Determine visible region in buffer coords */
    GdkRectangle vis;
    gtk_text_view_get_visible_rect(text_view, &vis);

    GtkTextIter vis_start, vis_end;
    gtk_text_view_get_iter_at_location(text_view, &vis_start, vis.x, vis.y);
    gtk_text_view_get_iter_at_location(text_view, &vis_end, 
                                       vis.x + vis.width, vis.y + vis.height);

    /* Use theme-aware color for blockquote borders */
    GdkRGBA color;
    get_theme_color_with_alpha(GTK_WIDGET(text_view), "window_fg_color", 0.4, &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);

    /* Collect blockquote tags from the table */
    TagCollect tc = { g_ptr_array_new_with_free_func(g_object_unref) };
    gtk_text_tag_table_foreach(table, collect_bq, &tc);

    for (guint i = 0; i < tc.arr->len; i++) {
        GtkTextTag *tag = GTK_TEXT_TAG(g_ptr_array_index(tc.arr, i));
        /* Find ranges for this tag within the buffer */
        GtkTextIter it;
        gtk_text_buffer_get_start_iter(buffer, &it);
        gboolean inside = gtk_text_iter_has_tag(&it, tag);
        GtkTextIter range_start;
        while (gtk_text_iter_forward_to_tag_toggle(&it, tag)) {
            if (!inside) { 
                range_start = it; 
                inside = TRUE; 
            } else {
                GtkTextIter range_end = it; 
                inside = FALSE;
                /* Skip if entirely above or below visible region */
                if (gtk_text_iter_compare(&range_end, &vis_start) <= 0 ||
                    gtk_text_iter_compare(&range_start, &vis_end) >= 0) {
                    continue;
                }
                /* Compute x position from the start of range */
                GdkRectangle loc_start, loc_end;
                gtk_text_view_get_iter_location(text_view, &range_start, &loc_start);
                gtk_text_view_get_iter_location(text_view, &range_end, &loc_end);
                /* Stripe position in overlay coords */
                int x = loc_start.x - vis.x;
                int stripe_x = x - 8;
                if (stripe_x < 0) stripe_x = 0;

                /* Vertical span: from first line y to last line bottom */
                int y1 = loc_start.y - vis.y;
                int y2_line_y, y2_line_h;
                gtk_text_view_get_line_yrange(text_view, &range_end, &y2_line_y, &y2_line_h);
                int y2 = (y2_line_y + y2_line_h) - vis.y;
                if (y2 < y1) { int t=y1; y1=y2; y2=t; }

                /* Draw 3px wide vertical stripe */
                cairo_rectangle(cr, stripe_x, y1, 3, y2 - y1);
                cairo_fill(cr);
            }
        }
    }
    g_ptr_array_free(tc.arr, TRUE);
}

#ifdef HAVE_LIBSOUP
/* Handle HTTP image fetch completion */
static void on_http_image_fetched(SoupSession *session, GAsyncResult *res, 
                                 gpointer user_data)
{
    RemoteImageCtx *ctx = (RemoteImageCtx*)user_data;
    GError *err = NULL;
    GBytes *bytes = soup_session_send_and_read_finish(session, res, &err);
    if (!bytes) {
        g_debug("Image fetch failed for %s: %s", ctx->url, 
                err ? err->message : "unknown");
        g_clear_error(&err);
        remote_image_ctx_free(ctx);
        return;
    }
    /* Check HTTP status and content type */
    guint status = soup_message_get_status(ctx->msg);
    if (status < 200 || status >= 300) {
        g_debug("HTTP fetch status %u for %s; skipping image", status, ctx->url);
        g_bytes_unref(bytes);
        remote_image_ctx_free(ctx);
        return;
    }
    const char *ct = soup_message_headers_get_one(
        soup_message_get_response_headers(ctx->msg), "Content-Type");
    if (!(ct && g_str_has_prefix(ct, "image/"))) {
        g_debug("HTTP content-type not image for %s: %s", ctx->url, ct ? ct : "(null)");
        g_bytes_unref(bytes);
        remote_image_ctx_free(ctx);
        return;
    }
    gsize sz = 0;
    const guint8 *data = g_bytes_get_data(bytes, &sz);
    GInputStream *stream = g_memory_input_stream_new_from_data(data, sz, NULL);
    GdkPixbuf *pb = gdk_pixbuf_new_from_stream(stream, NULL, &err);
    g_object_unref(stream);
    if (!pb) {
        g_debug("Pixbuf decode failed for %s: %s", ctx->url, 
                err ? err->message : "unknown");
        g_clear_error(&err);
        g_bytes_unref(bytes);
        remote_image_ctx_free(ctx);
        return;
    }
   /* ... previous content up to line 1051 ... */

    /* Create anchor and attach a GtkPicture now */
    GtkTextIter pos;
    gtk_text_buffer_get_iter_at_mark(ctx->buffer, &pos, ctx->mark);
    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(ctx->buffer, &pos);
    GdkTexture *texture = gdk_texture_new_for_pixbuf(pb);
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    g_object_unref(texture);
    /* For consistency with local path, also log when paintable gets set */
    g_signal_connect_data(pic, "notify::paintable", 
                         G_CALLBACK(on_picture_paintable_notify), 
                         g_strdup(ctx->url), free_user_data_notify, 0);
    g_object_unref(pb);
    gtk_text_view_add_child_at_anchor(ctx->view, pic, anchor);
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(pic),
        GTK_ACCESSIBLE_PROPERTY_LABEL,
        (ctx->alt && *ctx->alt) ? ctx->alt : 
            ((ctx->title && *ctx->title) ? ctx->title : ctx->url),
        -1);
    g_bytes_unref(bytes);
    /* Tooltip and click navigation */
    if (ctx->title && *ctx->title) gtk_widget_set_tooltip_text(pic, ctx->title);
    else gtk_widget_set_tooltip_text(pic, ctx->open_url ? ctx->open_url : ctx->url);
    const char *nav = ctx->open_url ? ctx->open_url : ctx->url;
    if (nav && *nav) {
        GtkGesture *click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
        g_signal_connect(click, "pressed", G_CALLBACK(on_embedded_image_pressed), pic);
        gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));
        g_object_set_data_full(G_OBJECT(pic), "open-url", g_strdup(nav), g_free);
        g_object_set_data_full(G_OBJECT(pic), "image-url", g_strdup(ctx->url), g_free);
    }

    /* Hide the alt text after the image is placed */
    if (ctx->hidden) {
        GtkTextIter iter;
        gtk_text_buffer_get_start_iter(ctx->buffer, &iter);
        gboolean inside = gtk_text_iter_has_tag(&iter, ctx->tag);
        GtkTextIter s;
        while (gtk_text_iter_forward_to_tag_toggle(&iter, ctx->tag)) {
            if (!inside) { s = iter; inside = TRUE; }
            else {
                GtkTextIter e = iter;
                gtk_text_buffer_apply_tag(ctx->buffer, ctx->hidden, &s, &e);
                inside = FALSE;
            }
        }
    }

    g_debug("[embed] Image fetched successfully for %s, rescanning buffer", ctx->url);
    
    /* Rescan the buffer for any remaining images */
    embed_images_in_text_view(ctx->view);

    remote_image_ctx_free(ctx);
}
#endif

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

/* Settings change handler - no longer needed since autosave settings removed */

/* Re-parse the whole buffer using cm_render_markdown_to_buffer */
static gboolean reparse_markdown_cb(gpointer user_data)
{
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(user_data);
    /* Clear the marker that scheduled us */
    g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID, GUINT_TO_POINTER(0));

    /* Suppress recursive scheduling while we modify the buffer */
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(1));

    /* Export current buffer (with tags) back to Markdown */
    g_autofree char *md_src = cm_render_buffer_to_markdown(buffer);
    if (md_src && *md_src) {
        /* Performance optimization: skip re-parsing if content hasn't changed */
        guint current_hash = g_str_hash(md_src);
        guint previous_hash = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer), 
                                                                 "content-hash"));

        if (current_hash == previous_hash) {
            g_debug("[perf] Content unchanged, skipping reparse");
            g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(0));
            return G_SOURCE_REMOVE;
        }

        g_object_set_data(G_OBJECT(buffer), "content-hash", GUINT_TO_POINTER(current_hash));
        /* Preserve a plausible cursor position */
        gint target = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), 
                                                       DATA_REPARSE_TARGET_OFFSET));

        /* Get text view and soup session for the new rendering function */
        GtkTextView *tv = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(buffer), "gtktext-view"));
#ifdef HAVE_LIBSOUP
        SoupSession *soup_session = g_object_get_data(G_OBJECT(buffer), "soup-session");
        if (!cm_render_markdown_to_buffer(buffer, md_src, tv, soup_session)) {
#else
        if (!cm_render_markdown_to_buffer(buffer, md_src, tv, NULL)) {
#endif
            g_warning("Realtime Markdown import failed");
        } else {
            /* Restore cursor position */
            GtkTextIter it;
            gint char_count = gtk_text_buffer_get_char_count(buffer);
            if (target < 0) target = 0;
            if (target > char_count) target = char_count;
            gtk_text_buffer_get_iter_at_offset(buffer, &it, target);
            gtk_text_buffer_place_cursor(buffer, &it);
            /* Re-embed images after re-render */
            if (tv) {
                cm_render_update_theme_dependent_tags(buffer);
            }
        }
    }

    /* Re-enable scheduling */
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(0));
    return G_SOURCE_REMOVE;
}

/* Detect paste-like insertions and schedule a reparse of the buffer */
static void on_buffer_insert_text(GtkTextBuffer *buffer, GtkTextIter *location, 
                                 gchar *text, gint len, 
                                 gpointer user_data)
{
    (void)user_data;
    if (!buffer || !text || len <= 0) return;
    /* Ignore programmatic changes from our own re-rendering */
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE)) != 0) {
        return;
    }

    /* Avoid reparsing the entire buffer on single keystrokes */
    if (len == 1) return;

    /* Heuristics: Only treat larger insertions (pastes) as requiring reparse */
    gboolean looks_like_paste = (len > 8);
    if (!looks_like_paste) {
        /* Consider multi-character insertion containing newlines as paste */
        for (int i = 0; i < len; i++) {
            if (text[i] == '\n') { 
                looks_like_paste = TRUE; 
                break; 
            }
        }
    }
    if (looks_like_paste) {
        schedule_reparse_markdown(buffer, len, location);
    }
}

/* Async open-file completion callback */
static void on_open_file_dialog_finish(GObject *source_object, GAsyncResult *res, 
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
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "editor");
    }
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(0));
}

/* Welcome screen open button callback */
static void welcome_open_cb(GtkButton *button, gpointer user_data)
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
    setup_file_dialog_filters(dlg);

    g_debug("[file-dialog] presenting open dialog (welcome)");
    gtk_file_dialog_open(dlg, parent, NULL, on_open_file_dialog_finish, app);
    g_object_unref(dlg);
    debug_dump_window_env(parent, "welcome-open:after");
}

/* Welcome screen new file button callback */
static void welcome_new_cb(GtkButton *button, gpointer user_data)
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
    gtktext_update_status_bar_for_document_state(app, new_state, file_path);
    
    /* Update legacy current_file_path for compatibility */
    if (file_path) {
        g_object_set_data_full(G_OBJECT(app), "current_file_path", 
                              g_strdup(file_path), g_free);
    } else {
        g_object_set_data(G_OBJECT(app), "current_file_path", NULL);
    }
}

/* Handler for autosave setting changes */
static void on_autosave_setting_changed(GSettings *settings, const gchar *key, 
                                       gpointer user_data)
{
    (void)settings;
    (void)key;
    DocumentManager *dm = (DocumentManager *)user_data;
    g_return_if_fail(dm != NULL);
    
    document_manager_update_autosave_setting(dm);
}

static void 
on_document_save_completed(DocumentManager *dm, SaveResult result, 
                          const gchar *error_message, gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION(user_data);
    const gchar *file_path = document_manager_get_file_path(dm);
    
    g_debug("Save completed: result=%d, file=%s, error=%s", 
            result, file_path ? file_path : "(none)", 
            error_message ? error_message : "(none)");
    
    switch (result) {
        case SAVE_RESULT_SUCCESS:
            update_save_status(app, _("Saved"));
            if (file_path) {
                update_file_location(app, file_path);
            }
            g_message("Document saved successfully to: %s", file_path);
            
            /* Check if we should close the window after successful save */
            GtkWindow *main_window = gtk_application_get_active_window(app);
            if (main_window) {
                GtkWidget *text_view = g_object_get_data(G_OBJECT(main_window), "text_view");
                if (text_view) {
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
                    gpointer close_after_save = g_object_get_data(G_OBJECT(buffer), "close-after-save");
                    if (close_after_save) {
                        g_message("Closing window after successful save");
                        /* Clear the flag and close */
                        g_object_set_data(G_OBJECT(buffer), "close-after-save", NULL);
                        g_object_set_data(G_OBJECT(main_window), "closing-without-dialog", GINT_TO_POINTER(1));
                        gtk_window_close(main_window);
                    }
                }
            }
            break;
            
        case SAVE_RESULT_ERROR:
            update_save_status(app, _("Save error"));
            g_warning("Save failed: %s", error_message ? error_message : "Unknown error");
            break;
            
        case SAVE_RESULT_CANCELLED:
            update_save_status(app, _("Save cancelled"));
            g_debug("Save operation was cancelled");
            break;
            
        case SAVE_RESULT_READONLY:
            update_save_status(app, _("Read-only"));
            g_warning("Cannot save: file is read-only");
            break;
            
        case SAVE_RESULT_CONFLICT:
            update_save_status(app, _("External changes detected"));
            g_warning("Save conflict: external changes detected");
            break;
            
        default:
            update_save_status(app, _("Unknown save result"));
            g_warning("Unknown save result: %d", result);
            break;
    }
}

/* Status bar update functions */

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

/* Helper function for DocumentManager integration (future use) */
void gtktext_update_status_bar_for_document_state(GtkApplication *app, 
                                                   DocumentState state, 
                                                   const gchar *file_path)
{
    update_status_bar_for_state(app, state, file_path);
}

/* App action callbacks */
static void action_open_cb(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    (void)a; (void)p;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;
    
    debug_dump_window_env(parent, "action-open:before");
    GtkFileDialog *dlg = gtk_file_dialog_new();

    /* Prefer the last used folder, falling back to HOME */
    const char *initial_path = NULL;
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
    setup_file_dialog_filters(dlg);

    g_debug("[file-dialog] presenting open dialog (action)");
    gtk_file_dialog_open(dlg, parent, NULL, on_open_file_dialog_finish, app);
    g_object_unref(dlg);
    debug_dump_window_env(parent, "action-open:after");
}

static void action_save_cb(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    (void)a; (void)p;
    GtkApplication *app = GTK_APPLICATION(user_data);
    DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
    
    if (!dm) {
        g_warning("DocumentManager not found in application data");
        return;
    }
    
    if (document_manager_is_untitled(dm)) {
        /* Show save dialog for untitled documents */
        action_save_as_cb(a, p, user_data);
    } else {
        /* Immediate save for named documents */
        document_manager_save(dm, FALSE, on_document_save_completed, app);
    }
}

/* DocumentManager save dialog completion callback */
static void on_save_as_dialog_finish(GObject *source_object, GAsyncResult *res, 
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
    document_manager_save_as(dm, path, on_document_save_completed, app);
    
    g_message("Document save-as initiated for: %s", path);
}

static void action_save_as_cb(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    (void)a; (void)p;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;
    
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, _("Save Markdown File"));
    setup_save_dialog_filters(dlg);
    
    /* Set initial filename */
    gtk_file_dialog_set_initial_name(dlg, "document.md");
    
    /* Prefer the last used folder, falling back to Documents */
    const char *initial_path = NULL;
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
    
    gtk_file_dialog_save(dlg, parent, NULL, on_save_as_dialog_finish, app);
    g_object_unref(dlg);
}

static void action_preferences_cb(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    (void)a; (void)p;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;
    AdwDialog *dlg = create_settings_window(parent);
    (void)dlg;
}

static void action_about_cb(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    (void)a; (void)p;
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWindow *parent = gtk_application_get_active_window(app);
    if (!parent) return;
    AdwDialog *about = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), _("GTKText"));
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), "gtktext");
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "GTKText Authors");
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), "1.0.0");
    adw_dialog_present(about, GTK_WIDGET(parent));
}

static void action_shortcuts_cb(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    (void)a; (void)p;
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
static void on_text_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    (void)user_data;
    /* Mark buffer as dirty only for user-initiated edits */
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE)) == 0) {
        g_object_set_data(G_OBJECT(buffer), DATA_USER_DIRTY, GINT_TO_POINTER(1));
        
        /* Update status bar to show modified status */
        GtkTextView *text_view = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(buffer), 
                                                                 "gtktext-view"));
        if (text_view) {
            GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(text_view));
            if (root && GTK_IS_APPLICATION_WINDOW(root)) {
                GtkApplication *app = gtk_window_get_application(GTK_WINDOW(root));
                if (app) {
                    update_save_status(app, _("Modified"));
                    
                    /* DocumentManager should automatically track buffer changes, 
                       so no need to manually update content here */
                }
            }
        }
        
        /* Only schedule live reparse for specific markdown formatting characters */
        GtkTextIter cursor_iter;
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor_iter, 
                                        gtk_text_buffer_get_insert(buffer));
        
        /* Check if we just typed a character that might trigger markdown formatting */
        if (!gtk_text_iter_is_start(&cursor_iter)) {
            GtkTextIter prev_iter = cursor_iter;
            gtk_text_iter_backward_char(&prev_iter);
            gunichar last_char = gtk_text_iter_get_char(&prev_iter);
            
            /* Only trigger reparse for specific scenarios */
            gboolean should_reparse = FALSE;
            
            /* Check for heading markers: # at start of line followed by space */
            if (last_char == ' ') {
                GtkTextIter line_start = prev_iter;
                gtk_text_iter_set_line_offset(&line_start, 0);
                g_autofree char *line_text = gtk_text_buffer_get_text(buffer, 
                                                                      &line_start, 
                                                                      &cursor_iter, FALSE);
                /* Only reparse for headings with content */
                if (line_text && g_str_has_prefix(line_text, "#") && 
                    g_str_has_suffix(line_text, "# ") && strlen(line_text) > 2) {
                    should_reparse = TRUE;
                }
            }
            /* For other markdown characters, be more selective */
            else if (last_char == '*' || last_char == '_' || last_char == '`') {
                should_reparse = TRUE;
            }
            
            if (should_reparse) {
                /* Use a longer delay to avoid interrupting consecutive typing */
                schedule_reparse_markdown(buffer, 0, &cursor_iter);
            }
        }
    }
    
    /* DocumentManager now handles all autosave functionality - old system disabled */
    /* The DocumentManager will detect buffer changes via its own monitoring */
}

/* Check if buffer has unsaved changes using DocumentManager state */
static gboolean has_unsaved_changes(GtkTextBuffer *buffer)
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

/* Show dialog asking user to save unsaved changes */
static void show_unsaved_changes_dialog(GtkWindow *parent, GtkTextBuffer *buffer)
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

    g_signal_connect(dialog, "response", G_CALLBACK(on_unsaved_changes_dialog_response), buffer);

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}

/* Handle unsaved changes dialog response */
static void on_unsaved_changes_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
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
            
            document_manager_save(doc_manager, FALSE, on_document_save_completed, app);
        } else {
            /* Show file dialog to choose save location */
            g_message("No file path, showing save-as dialog");
            
            /* Set close-after-save flag */
            g_object_set_data(G_OBJECT(buffer), "close-after-save", GINT_TO_POINTER(1));
            
            /* Trigger save-as action to show file dialog */
            if (app) {
                GSimpleAction *save_as_action = g_simple_action_new("save-as", NULL);
                action_save_as_cb(save_as_action, NULL, app);
                g_object_unref(save_as_action);
            }
        }
    } else if (g_strcmp0(response, "discard") == 0) {
        /* Close without saving - discard any drafts and set flag to prevent dialog recursion */
        g_message("Discarding changes and closing");
        
        if (window && GTK_IS_WINDOW(window)) {
            /* Get DocumentManager and discard any existing draft */
            GtkApplication *app = gtk_window_get_application(GTK_WINDOW(window));
            if (app) {
                DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
                if (dm) {
                    document_manager_discard_current_draft(dm);
                    g_debug("Draft discarded before closing");
                }
            }
            
            g_object_set_data(G_OBJECT(window), "closing-without-dialog", GINT_TO_POINTER(1));
            gtk_window_close(GTK_WINDOW(window));
        }
    }
    /* Cancel: do nothing, dialog closes automatically */
}

/* Check for autosave file and offer recovery on startup */
static void check_for_autorecover(GtkApplication *app)
{
    const gchar *temp_dir = g_get_tmp_dir();
    g_autofree gchar *autosave_path = g_build_filename(temp_dir, "gtktext-autosave.md", NULL);

    /* Check if autosave file exists and is recent */
    if (g_file_test(autosave_path, G_FILE_TEST_EXISTS)) {
        GStatBuf stat_buf;
        if (g_stat(autosave_path, &stat_buf) == 0) {
            /* Check if autosave file is less than 24 hours old */
            time_t now = time(NULL);
            if (now - stat_buf.st_mtime < (24 * 60 * 60)) {
                g_message("Found recent autosave file: %s", autosave_path);

                /* Get main window to show dialog */
                GtkWindow *parent = gtk_application_get_active_window(app);
                if (parent) {
                    show_autorecover_dialog(parent, autosave_path);
                }
            } else {
                g_message("Autosave file is too old, removing: %s", autosave_path);
                g_unlink(autosave_path);
            }
        }
    }
}

/* Show dialog asking user to recover from autosave */
static void show_autorecover_dialog(GtkWindow *parent, const char *autosave_path)
{
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(
        _("Recover unsaved changes?"),
        _("The application was closed unexpectedly. Would you like to recover your unsaved work?")
    ));

    adw_alert_dialog_add_responses(dialog,
        "discard", _("_Start Fresh"),
        "recover", _("_Recover"),
        NULL);

    adw_alert_dialog_set_response_appearance(dialog, "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_response_appearance(dialog, "recover", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dialog, "recover");
    adw_alert_dialog_set_close_response(dialog, "discard");

    g_signal_connect(dialog, "response", G_CALLBACK(on_autorecover_dialog_response), g_strdup(autosave_path));

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}

/* Handle autorecover dialog response */
static void on_autorecover_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    const char *autosave_path = (const char *)user_data;

    if (g_strcmp0(response, "recover") == 0) {
        /* Load autosave content */
        g_autofree gchar *contents = NULL;
        gsize length = 0;
        g_autoptr(GError) error = NULL;

        if (g_file_get_contents(autosave_path, &contents, &length, &error)) {
            /* Get the application and text buffer */
            GtkWidget *window = gtk_widget_get_ancestor(GTK_WIDGET(dialog), GTK_TYPE_WINDOW);
            if (window) {
                GtkApplication *app = gtk_window_get_application(GTK_WINDOW(window));
                GtkWidget *text_view = g_object_get_data(G_OBJECT(app), "text_view");
                if (text_view) {
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

                    /* Load recovered content */
                    gtk_text_buffer_set_text(buffer, contents, length);

                    /* Set original text to empty so it's marked as dirty */
                    g_object_set_data_full(G_OBJECT(buffer), DATA_ORIGINAL_TEXT, g_strdup(""), g_free);

                    /* Switch to editor view */
                    GtkWidget *main_stack = g_object_get_data(G_OBJECT(app), "main_stack");
                    if (main_stack) {
                        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "editor");
                    }

                    g_message("Successfully recovered content from autosave");
                }
            }
        } else {
            g_warning("Failed to read autosave file: %s", error->message);
        }
    }

    /* Remove autosave file after decision */
    g_unlink(autosave_path);
    g_free((gpointer)user_data); /* Free the duplicated autosave_path */
}

/* Callback triggered when the main window requests to be closed */
static gboolean on_window_close_request(GtkWindow *window, gpointer user_data)
{
    /* Check if we're closing without dialog (to prevent recursion) */
    gpointer closing_flag = g_object_get_data(G_OBJECT(window), "closing-without-dialog");
    if (closing_flag) {
        g_object_set_data(G_OBJECT(window), "closing-without-dialog", NULL);
        g_message("Closing window without dialog (flag set)");
        return FALSE; /* Allow close */
    }

    /* Ensure text_view is valid before using it */
    if (!user_data || !GTK_IS_TEXT_VIEW(user_data)) {
        g_warning("on_window_close_request: Invalid text_view (user_data).");
        return FALSE;
    }
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);

    /* Ensure buffer is valid after getting it from text_view */
    if (!buffer || !GTK_IS_TEXT_BUFFER(buffer)) {
        g_warning("on_window_close_request: Failed to get valid buffer from text_view.");
        return FALSE;
    }

    g_debug("on_window_close_request: checking for unsaved changes");

    /* Check if there are unsaved changes */
    if (has_unsaved_changes(buffer)) {
        g_message("Unsaved changes detected, showing save dialog");
        show_unsaved_changes_dialog(window, buffer);
        return TRUE; /* Prevent automatic close, let dialog handle it */
    }

    g_debug("on_window_close_request: no unsaved changes, proceeding with close");

    /* Clean up if no unsaved changes */
    if (buffer_changed_signal_id > 0) {
        if (g_signal_handler_is_connected(buffer, buffer_changed_signal_id)) {
            g_debug("on_window_close_request: disconnecting 'changed' signal handler (ID: %u)",
                    buffer_changed_signal_id);
            g_signal_handler_disconnect(buffer, buffer_changed_signal_id);
        }
    }
    buffer_changed_signal_id = 0;

    g_message("Allowing default close handling");
    return FALSE; /* Allow default close behavior */
}

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
            copy_selected_text_as_markdown(text_view);
            return TRUE;
        }
        /* Zoom in: Ctrl+plus/equal/KP_Add */
        else if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal || 
                keyval == GDK_KEY_KP_Add) {
            zoom_text_view(text_view, TRUE);
            return TRUE;
        }
        /* Zoom out: Ctrl+minus/KP_Subtract */
        else if (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract) {
            zoom_text_view(text_view, FALSE);
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
            zoom_text_view(text_view, TRUE);  /* Scroll up = zoom in */
        } else if (dy > 0) {
            zoom_text_view(text_view, FALSE); /* Scroll down = zoom out */
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

static void on_map(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    g_message("Main window mapped, image widgets already embedded during rendering.");
}

static void on_window_map(GtkWidget *window, gpointer user_data)
{
    (void)user_data;
    g_message("Window mapped, setting welcome screen visibility.");
    GtkWidget *main_stack = GTK_WIDGET(g_object_get_data(G_OBJECT(window), "main_stack"));
    if (main_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "welcome");
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * WIRING - Signal connections, widget setup
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void setup_blockquote_overlay(GtkTextView *text_view)
{
    if (!text_view) return;
    GtkWidget *tv = GTK_WIDGET(text_view);
    GtkWidget *parent = gtk_widget_get_parent(tv);
    if (!GTK_IS_SCROLLED_WINDOW(parent)) return;

    /* Create overlay and drawing area */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(overlay, TRUE);
    gtk_widget_set_vexpand(overlay, TRUE);

    /* Reparent text_view under overlay */
    gtk_widget_unparent(tv);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(parent), overlay);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), tv);

    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);
    gtk_widget_set_can_target(area, FALSE);  /* Allow events to pass through */
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), area);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_bq_overlay_draw, 
                                   text_view, NULL);

    /* Redraw on scroll adjustments */
    GtkAdjustment *hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(parent));
    GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));
    g_signal_connect(hadj, "value-changed", G_CALLBACK(on_adjustment_changed), area);
    g_signal_connect(vadj, "value-changed", G_CALLBACK(on_adjustment_changed), area);

    /* Keep pointers for later if needed */
    g_object_set_data(G_OBJECT(tv), "bq-overlay", overlay);
    g_object_set_data(G_OBJECT(tv), "bq-area", area);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * LIFECYCLE - Application initialization, activation, shutdown
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void app_activate(GApplication *application)
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
    if (!builder) {
        g_critical("Failed to load UI file main_window.ui");
        return;
    }

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
    if (!save_status) {
        g_critical("Failed to get save_status from UI");
        g_object_unref(builder);
        return;
    }
    
    GtkWidget *file_location = GTK_WIDGET(gtk_builder_get_object(builder, "file_location"));
    if (!file_location) {
        g_critical("Failed to get file_location from UI");
        g_object_unref(builder);
        return;
    }
    
    /* Get the main stack widget */
    GtkWidget *main_stack = GTK_WIDGET(gtk_builder_get_object(builder, "main_stack"));
    if (!main_stack) {
        g_critical("Failed to get main_stack from UI");
        g_object_unref(builder);
        return;
    }
    
    /* Get the welcome screen buttons */
    GtkWidget *welcome_open_button = GTK_WIDGET(gtk_builder_get_object(builder, 
                                                                      "welcome_open_button"));
    if (!welcome_open_button) {
        g_critical("Failed to get welcome_open_button from UI");
        g_object_unref(builder);
        return;
    }
    
    GtkWidget *welcome_new_button = GTK_WIDGET(gtk_builder_get_object(builder, 
                                                                     "welcome_new_button"));
    if (!welcome_new_button) {
        g_critical("Failed to get welcome_new_button from UI");
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
    update_save_status(app, _("Ready"));
    update_file_location(app, NULL);  /* Shows "Untitled Document" */

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
    
    /* Set up GSettings change handler for autosave setting */
    GSettings *settings = g_settings_new("org.gtk.gtktext");
    g_signal_connect(settings, "changed::autosave-enabled",
                    G_CALLBACK(on_autosave_setting_changed), doc_manager);
    g_object_set_data_full(G_OBJECT(app), "app_settings", settings, g_object_unref);
    
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
    app_settings = g_settings_new("org.gtk.gtktext");
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
    setup_blockquote_overlay(GTK_TEXT_VIEW(text_view));
    
    /* Connect welcome screen buttons */
    g_signal_connect(welcome_open_button, "clicked", G_CALLBACK(welcome_open_cb), app);
    g_signal_connect(welcome_new_button, "clicked", G_CALLBACK(welcome_new_cb), app);
    
    /* Store main_stack reference for setting visibility after window is shown */
    g_object_set_data(G_OBJECT(window), "main_stack", main_stack);
    
    /* Store the handler ID so we can disconnect it later if needed */
    buffer_changed_signal_id = g_signal_connect(buffer, "changed", 
                                               G_CALLBACK(on_text_changed), NULL);
    /* Realtime paste→markdown conversion: listen to inserted text */
    g_signal_connect(buffer, "insert-text", G_CALLBACK(on_buffer_insert_text), NULL);

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
    g_signal_connect(window, "close-request", G_CALLBACK(on_window_close_request), text_view);

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
    g_signal_connect(text_view, "map", G_CALLBACK(on_map), text_view);

    /* Connect window map signal to set welcome screen after proper initialization */
    g_signal_connect(window, "map", G_CALLBACK(on_window_map), NULL);

    /* Check for autorecover on startup */
    check_for_autorecover(app);

    gtk_window_present(GTK_WINDOW(window));
}

static void app_open(GApplication *application, GFile **files, gint n_files, 
                    const gchar *hint)
{
    (void)hint;
    /* First activate the application to ensure window is created */
    app_activate(application);
    
    if (n_files > 0) {
        /* Open the first file (ignore additional files for now) */
        GFile *file = files[0];
        g_autofree gchar *path = g_file_get_path(file);
        
        if (path) {
            g_message("Opening file: %s", path);
            
            /* Get the current window and text buffer */
            GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(application));
            if (window) {
                GtkTextView *text_view = g_object_get_data(G_OBJECT(application), "text_view");
                if (text_view) {
                    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
                    
                    /* Load file content */
                    g_autofree gchar *contents = NULL;
                    gsize length = 0;
                    g_autoptr(GError) error = NULL;

                    if (g_file_get_contents(path, &contents, &length, &error)) {
                        /* Set the content in the buffer */
                        gtk_text_buffer_set_text(buffer, contents, length);
                        
                        /* Store the file path for saving */
                        g_object_set_data_full(G_OBJECT(window), "current_file_path", 
                                             g_strdup(path), g_free);
                        
                        /* Update window title */
                        g_autofree gchar *basename = g_path_get_basename(path);
                        g_autofree gchar *title = g_strdup_printf("GTK Text - %s", basename);
                        gtk_window_set_title(window, title);
                        
                        /* Trigger markdown parsing */
                        schedule_reparse_markdown(buffer, 0, NULL);
                        
                        /* Switch to editor view (hide welcome screen) */
                        GtkWidget *main_stack = g_object_get_data(G_OBJECT(window), "main_stack");
                        if (main_stack) {
                            gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "editor");
                        }
                        
                        g_message("Successfully loaded file: %s", path);
                    } else {
                        g_warning("Failed to load file %s: %s", path, error->message);

                        /* Enhanced recovery: check for backup files */
                        g_autofree gchar *backup_path = g_strdup_printf("%s.backup", path);
                        g_autofree gchar *backup_contents = NULL;
                        g_autoptr(GError) backup_error = NULL;
                        gsize backup_length = 0;

                        if (g_file_get_contents(backup_path, &backup_contents, &backup_length, 
                                              &backup_error)) {
                            g_message("Found backup file, loading: %s", backup_path);
                            gtk_text_buffer_set_text(buffer, backup_contents, backup_length);
                        } else {
                            g_debug("No backup file available at: %s", backup_path);
                        }
                    }
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
    maybe_setup_gsettings_schemas();
    
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
        { "open", action_open_cb, NULL, NULL, NULL, {0} },
        { "save", action_save_cb, NULL, NULL, NULL, {0} },
        { "save-as", action_save_as_cb, NULL, NULL, NULL, {0} },
        { "preferences", action_preferences_cb, NULL, NULL, NULL, {0} },
        { "about", action_about_cb, NULL, NULL, NULL, {0} },
        { "shortcuts", action_shortcuts_cb, NULL, NULL, NULL, {0} },
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

  g_signal_connect (app, "activate", G_CALLBACK (app_activate), NULL);
  g_signal_connect (app, "open", G_CALLBACK (app_open), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);

  return status;
}