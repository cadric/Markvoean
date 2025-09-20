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