/* C ULTRA-MIN TEMPLATE
   Purpose: Text view interaction handlers and utilities for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.8] - 2025-09-16 - ui/text_view_interactions.c
   Fixed: GTK allocation warnings and modernized to GTK4 APIs
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/ui/text_view_interactions.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/render/theme_styles.h>

/* Forward declarations for external functions we need access to */
extern void schedule_reparse_markdown(GtkTextBuffer *buffer, gint inserted_len, const GtkTextIter *at_iter);

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Callback for blockquote tag collection */
static void collect_bq(GtkTextTag *tag, gpointer user_data)
{
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    if (!tag) return;

    gchar *name = NULL;
    g_object_get(tag, "name", &name, NULL);
    if (!name || !g_str_has_prefix(name, "blockquote")) {
        g_free(name);
        return;
    }

    int depth = 1;
    const char *p = name + 10; // strlen("blockquote")
    if (*p >= '1' && *p <= '9') {
        depth = *p - '0';
    }
    int x_offset = 5 + (depth - 1) * 20;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);

    gint *data = g_object_get_data(G_OBJECT(text_view), "bq-lines");
    int count = data ? *data : 0;

    while (gtk_text_iter_forward_to_tag_toggle(&iter, tag)) {
        gboolean inside = gtk_text_iter_has_tag(&iter, tag);
        if (inside) {
            GtkTextIter range_start = iter;
            if (gtk_text_iter_forward_to_tag_toggle(&iter, tag)) {
                GtkTextIter range_end = iter;

                GdkRectangle loc_start, loc_end;
                gtk_text_view_get_iter_location(text_view, &range_start, &loc_start);
                gtk_text_view_get_iter_location(text_view, &range_end, &loc_end);

                gint y1_line_y, y1_line_h, y2_line_y, y2_line_h;
                gtk_text_view_get_line_yrange(text_view, &range_start, &y1_line_y, &y1_line_h);
                gtk_text_view_get_line_yrange(text_view, &range_end, &y2_line_y, &y2_line_h);

                data = g_realloc(data, (count + 5) * sizeof(gint));
                data[0] = count + 4;
                data[count + 1] = loc_start.x;
                data[count + 2] = y1_line_y;
                data[count + 3] = y2_line_y + y2_line_h;
                data[count + 4] = x_offset;
                count += 4;

                g_object_set_data(G_OBJECT(text_view), "bq-lines", data);
            }
        }
    }

    g_free(name);
}

/* Callback for drawing blockquote overlays */
static void on_bq_overlay_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                              gpointer user_data)
{
    (void)area;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    if (!text_view) return;

    /* Check if text view has proper allocation before drawing */
    int alloc_width = gtk_widget_get_width(GTK_WIDGET(text_view));
    int alloc_height = gtk_widget_get_height(GTK_WIDGET(text_view));
    if (alloc_width <= 1 || alloc_height <= 1) return;

    /* Only draw if we have valid dimensions */
    if (width <= 0 || height <= 0) return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    if (!buffer) return;

    GdkRectangle vis;
    gtk_text_view_get_visible_rect(text_view, &vis);

    /* Ensure visible rect is valid */
    if (vis.width <= 0 || vis.height <= 0) return;

    GtkTextIter vis_start, vis_end;
    gtk_text_view_get_iter_at_location(text_view, &vis_start, vis.x, vis.y);
    gtk_text_view_get_iter_at_location(text_view, &vis_end,
                                      vis.x + vis.width, vis.y + vis.height);

    GdkRGBA color;
    theme_styles_get_color_with_alpha(GTK_WIDGET(text_view), "window_fg_color", 0.4, &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
    cairo_set_line_width(cr, 3.0);

    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    gtk_text_tag_table_foreach(table, collect_bq, (gpointer)text_view);

    /* Draw accumulated blockquote lines */
    gint *data = g_object_get_data(G_OBJECT(text_view), "bq-lines");
    if (data) {
        gint count = *data;
        for (int i = 1; i <= count; i += 4) {
            if (i + 3 < count) {
                int x = data[i], y1 = data[i+1], y2 = data[i+2], x_offset = data[i+3];
                if (y2 > y1) {
                    cairo_move_to(cr, x + x_offset, y1);
                    cairo_line_to(cr, x + x_offset, y2);
                    cairo_stroke(cr);
                }
            }
        }
        g_free(data);
        g_object_set_data(G_OBJECT(text_view), "bq-lines", NULL);
    }
}

/* Callback for scrollbar adjustments */
/* Debounced redraw to prevent layout thrashing */
static gboolean debounced_redraw(gpointer user_data)
{
    GtkDrawingArea *area = GTK_DRAWING_AREA(user_data);
    if (GTK_IS_DRAWING_AREA(area)) {
        /* Only queue draw if widget has proper allocation to prevent GTK warnings */
        int width = gtk_widget_get_width(GTK_WIDGET(area));
        int height = gtk_widget_get_height(GTK_WIDGET(area));
        if (width > 0 && height > 0) {
            gtk_widget_queue_draw(GTK_WIDGET(area));
        }
    }
    return G_SOURCE_REMOVE;
}

static void on_adjustment_changed(GObject *adj, GParamSpec *pspec, gpointer user_data)
{
    (void)adj;
    (void)pspec;
    GtkDrawingArea *area = GTK_DRAWING_AREA(user_data);

    /* Cancel any pending redraw */
    guint *timeout_id = g_object_get_data(G_OBJECT(area), "redraw-timeout");
    if (timeout_id && *timeout_id > 0) {
        g_source_remove(*timeout_id);
    }

    /* Schedule debounced redraw */
    guint new_timeout = g_timeout_add(16, debounced_redraw, area); /* ~60fps */
    g_object_set_data_full(G_OBJECT(area), "redraw-timeout",
                          g_memdup2(&new_timeout, sizeof(guint)), g_free);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Text view interaction functions
 * ═══════════════════════════════════════════════════════════════════════════════ */


gboolean text_view_on_scroll_event(GtkEventControllerScroll *controller, gdouble dx, gdouble dy,
                                   gpointer user_data)
{
    (void)controller;
    (void)dx;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);

    /* Check if Ctrl is held down for zoom */
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
    if (state & GDK_CONTROL_MASK) {
        if (dy < 0) {
            text_view_zoom(text_view, TRUE);  /* Scroll up = zoom in */
        } else if (dy > 0) {
            text_view_zoom(text_view, FALSE); /* Scroll down = zoom out */
        }
        return TRUE; /* Event handled */
    }
    return FALSE; /* Let default scrolling happen */
}

void text_view_on_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y,
                         gpointer user_data)
{
    (void)controller;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextIter iter;
    gint trailing;

    if (!gtk_text_view_get_iter_at_position(text_view, &iter, &trailing, x, y)) {
        /* Default cursor for areas without text */
        gtk_widget_set_cursor_from_name(GTK_WIDGET(text_view), "text");
        return;
    }

    /* Check for link tags at this position */
    GSList *tags = gtk_text_iter_get_tags(&iter);
    gboolean over_link = FALSE;

    for (GSList *tagp = tags; tagp != NULL; tagp = tagp->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(tagp->data);
        gchar *name = NULL;
        g_object_get(tag, "name", &name, NULL);

        if (name && g_str_has_prefix(name, "link_")) {
            over_link = TRUE;
            g_free(name);
            break;
        }
        g_free(name);
    }

    /* Check if we're over an image (anchor) */
    if (!over_link) {
        GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
        if (anchor) {
            /* Get widgets at this anchor */
            guint n_widgets;
            GtkWidget **widgets = gtk_text_child_anchor_get_widgets(anchor, &n_widgets);
            for (guint i = 0; i < n_widgets; i++) {
                if (GTK_IS_PICTURE(widgets[i])) {
                    const char *url = g_object_get_data(G_OBJECT(widgets[i]), "image-url");
                    if (url && *url) {
                        over_link = TRUE;
                        break;
                    }
                }
            }
            g_free(widgets);
        }
    }

    /* Also check if we're over an image tag for proper cursor positioning */
    if (!over_link) {
        for (GSList *tagp = tags; tagp != NULL; tagp = tagp->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(tagp->data);
            gchar *name = NULL;
            g_object_get(tag, "name", &name, NULL);

            if (name && g_str_has_prefix(name, "image_")) {
                GdkRectangle char_rect;
                gtk_text_view_get_iter_location(text_view, &iter, &char_rect);

                gint buffer_x, buffer_y;
                gtk_text_view_buffer_to_window_coords(text_view, GTK_TEXT_WINDOW_TEXT,
                                                     char_rect.x, char_rect.y,
                                                     &buffer_x, &buffer_y);

                /* Check if we're close to the character position */
                if (abs((int)x - buffer_x) < 20 && abs((int)y - buffer_y) < 20) {
                    const char *url = g_object_get_data(G_OBJECT(tag), "image-url");
                    if (url && *url) {
                        over_link = TRUE;
                    }
                }
                g_free(name);
                break;
            }
            g_free(name);
        }
    }

    if (over_link) {
        gtk_widget_set_cursor_from_name(GTK_WIDGET(text_view), "pointer");
    } else {
        gtk_widget_set_cursor_from_name(GTK_WIDGET(text_view), "text");
    }

    g_slist_free(tags);
}

void text_view_on_link_clicked(GtkGestureClick *gesture, gint n_press, gdouble x,
                               gdouble y, gpointer user_data)
{
    (void)gesture;
    (void)n_press;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextIter iter;
    gint trailing;

    if (!gtk_text_view_get_iter_at_position(text_view, &iter, &trailing, x, y)) {
        return;
    }

    GSList *tags = gtk_text_iter_get_tags(&iter);
    for (GSList *tagp = tags; tagp != NULL; tagp = tagp->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(tagp->data);
        gchar *name = NULL;
        g_object_get(tag, "name", &name, NULL);

        if (name && g_str_has_prefix(name, "link_")) {
            const char *url = g_object_get_data(G_OBJECT(tag), "link-url");
            if (url && *url) {
                GtkWindow *window = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(text_view),
                                                                       GTK_TYPE_WINDOW));
                if (window) {
                    GtkUriLauncher *launcher = gtk_uri_launcher_new(url);
                    gtk_uri_launcher_launch(launcher, window, NULL, NULL, NULL);
                    g_object_unref(launcher);
                }
            }
            g_free(name);
            break;
        }
        g_free(name);
    }
    g_slist_free(tags);
}

gboolean text_view_on_query_tooltip(GtkWidget *widget, gint x, gint y,
                                    gboolean keyboard_mode, GtkTooltip *tooltip,
                                    gpointer user_data)
{
    (void)user_data;
    (void)keyboard_mode;
    GtkTextView *text_view = GTK_TEXT_VIEW(widget);
    GtkTextIter iter;
    gint trailing;

    /* Convert window coordinates to buffer coordinates and get iterator */
    gint buffer_x, buffer_y;
    gtk_text_view_window_to_buffer_coords(text_view, GTK_TEXT_WINDOW_TEXT, x, y, &buffer_x, &buffer_y);

    if (!gtk_text_view_get_iter_at_position(text_view, &iter, &trailing, buffer_x, buffer_y)) {
        return FALSE;
    }

    /* Check for link tags */
    GSList *tags = gtk_text_iter_get_tags(&iter);
    for (GSList *tagp = tags; tagp != NULL; tagp = tagp->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(tagp->data);
        gchar *name = NULL;
        g_object_get(tag, "name", &name, NULL);

        if (name && g_str_has_prefix(name, "link_")) {
            const char *url = g_object_get_data(G_OBJECT(tag), "link-url");
            const char *title = g_object_get_data(G_OBJECT(tag), "link-title");

            if (url && *url) {
                gchar *tooltip_text;
                if (title && *title) {
                    tooltip_text = g_strdup_printf("%s\n%s", title, url);
                } else {
                    tooltip_text = g_strdup(url);
                }
                gtk_tooltip_set_text(tooltip, tooltip_text);
                g_free(tooltip_text);
                g_free(name);
                g_slist_free(tags);
                return TRUE;
            }
        }
        g_free(name);
    }

    g_slist_free(tags);
    return FALSE;
}

void text_view_zoom(GtkTextView *text_view, gboolean zoom_in)
{
    if (!text_view) return;

    /* Get current zoom level */
    gdouble *stored_zoom = g_object_get_data(G_OBJECT(text_view), "zoom-level");
    gdouble current_zoom = stored_zoom ? *stored_zoom : 1.0;

    /* Calculate new zoom level */
    gdouble new_zoom;
    if (zoom_in) {
        new_zoom = current_zoom * 1.1; /* 10% increase */
        if (new_zoom > 3.0) new_zoom = 3.0; /* Max 300% */
    } else {
        new_zoom = current_zoom / 1.1; /* 10% decrease */
        if (new_zoom < 0.5) new_zoom = 0.5; /* Min 50% */
    }

    /* Apply zoom via CSS */
    int zoom_percent = (int)(new_zoom * 100);
    g_autofree gchar *css = g_strdup_printf("textview { font-size: %d%%; }", zoom_percent);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* Store new zoom level */
    gdouble *zoom_ptr = g_new(gdouble, 1);
    *zoom_ptr = new_zoom;
    g_object_set_data_full(G_OBJECT(text_view), "zoom-level", zoom_ptr, g_free);
}

void text_view_copy_selected_as_markdown(GtkTextView *text_view)
{
    if (!text_view) return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    if (!gtk_text_buffer_get_has_selection(buffer)) {
        return; /* No selection to copy */
    }

    /* Get selected text range */
    GtkTextIter start, end;
    gtk_text_buffer_get_selection_bounds(buffer, &start, &end);

    /* Use the rendering engine to export to markdown */
    char *markdown = cm_render_buffer_to_markdown(buffer);
    if (!markdown) {
        g_warning("Failed to export buffer to markdown");
        return;
    }

    /* For now, we copy the full buffer's markdown since we don't have
     * a selection-only export function. This could be improved. */
    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(text_view));
    gdk_clipboard_set_text(clipboard, markdown);

    g_free(markdown);
    g_debug("Copied buffer as markdown to clipboard");
}

/* Callback to set up overlay when widget is mapped (safer than realized) */
static void on_text_view_mapped(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    GtkTextView *text_view = GTK_TEXT_VIEW(widget);

    /* Check if overlay already exists to prevent infinite recursion */
    if (g_object_get_data(G_OBJECT(text_view), "blockquote-overlay-setup")) {
        return;
    }

    /* Mark as setup in progress to prevent recursion */
    g_object_set_data(G_OBJECT(text_view), "blockquote-overlay-setup", GINT_TO_POINTER(1));

    /* For now, disable the complex overlay setup that was causing infinite recursion.
       This can be re-implemented later in a safer way using CSS overlays or drawing directly. */

    g_debug("Blockquote overlay setup skipped to prevent recursion - will be reimplemented safely");

    /* Connect scroll events for potential future overlay implementation */
    GtkAdjustment *vadj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text_view));
    if (vadj) {
        /* Store for potential future use */
        g_object_set_data(G_OBJECT(text_view), "v-adjustment", vadj);
    }

    GtkAdjustment *hadj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(text_view));
    if (hadj) {
        /* Store for potential future use */
        g_object_set_data(G_OBJECT(text_view), "h-adjustment", hadj);
    }
}

void text_view_setup_blockquote_overlay(GtkTextView *text_view)
{
    if (!text_view) return;

    /* Use the safer mapped callback instead of realized to prevent infinite recursion */
    if (gtk_widget_get_mapped(GTK_WIDGET(text_view))) {
        on_text_view_mapped(GTK_WIDGET(text_view), NULL);
    } else {
        /* Otherwise, wait for map signal */
        g_signal_connect(text_view, "map", G_CALLBACK(on_text_view_mapped), NULL);
    }
}