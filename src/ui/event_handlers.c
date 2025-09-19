/* C ULTRA-MIN TEMPLATE
   Purpose: Core UI event handlers for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.8] - 2025-09-16 - ui/event_handlers.c
   Created: Extracted core event handlers from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/ui/event_handlers.h>
#include <gtktext/ui/status_manager.h>
#include <gtktext/ui/text_view_interactions.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Core event handler functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void event_handlers_on_document_state_changed(DocumentManager *dm, DocumentState old_state,
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

void event_handlers_on_text_view_link_clicked(GtkGestureClick *gesture, gint n_press,
                                             gdouble x, gdouble y, gpointer user_data)
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

gboolean event_handlers_on_text_view_query_tooltip(GtkWidget *widget, gint x, gint y,
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

gboolean event_handlers_on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                       guint keycode, GdkModifierType state, gpointer user_data)
{
    (void)controller;
    (void)keycode;
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);

    if (state & GDK_CONTROL_MASK) {
        /* Detect Ctrl+C */
        if (keyval == GDK_KEY_c || keyval == GDK_KEY_C) {
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

gboolean event_handlers_on_scroll_event(GtkEventControllerScroll *controller, gdouble dx, gdouble dy,
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

void event_handlers_on_text_view_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y,
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