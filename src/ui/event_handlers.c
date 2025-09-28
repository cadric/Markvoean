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
    /* Delegate to canonical text view interactions implementation */
    text_view_on_link_clicked(gesture, n_press, x, y, user_data);
}

gboolean event_handlers_on_text_view_query_tooltip(GtkWidget *widget, gint x, gint y,
                                                   gboolean keyboard_mode, GtkTooltip *tooltip,
                                                   gpointer user_data)
{
    /* Delegate to canonical text view interactions implementation */
    return text_view_on_query_tooltip(widget, x, y, keyboard_mode, tooltip, user_data);
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
    /* Delegate to canonical text view interactions implementation */
    return text_view_on_scroll_event(controller, dx, dy, user_data);
}

void event_handlers_on_text_view_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y,
                                        gpointer user_data)
{
    /* Delegate to canonical text view interactions implementation */
    text_view_on_motion(controller, x, y, user_data);
}
