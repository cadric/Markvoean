/* C ULTRA-MIN TEMPLATE
   Purpose: Core UI event handlers for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.8] - 2025-09-16 - ui/event_handlers.h
   Created: Extracted core event handlers from main.c for better organization
*/

#ifndef GTKTEXT_UI_EVENT_HANDLERS_H
#define GTKTEXT_UI_EVENT_HANDLERS_H

#include <glib.h>
#include <gtk/gtk.h>
#include <gtktext/document/document_manager.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Core event handler functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * event_handlers_on_document_state_changed:
 * @dm: The DocumentManager instance
 * @old_state: Previous document state
 * @new_state: New document state
 * @user_data: User data (typically the GtkApplication)
 *
 * Handle document state changes and update UI accordingly.
 */
void event_handlers_on_document_state_changed(DocumentManager *dm, DocumentState old_state,
                                              DocumentState new_state, gpointer user_data);

/**
 * event_handlers_on_text_view_link_clicked:
 * @gesture: The gesture that triggered the event
 * @n_press: Number of button presses
 * @x: X coordinate of the click
 * @y: Y coordinate of the click
 * @user_data: User data (typically the GtkTextView)
 *
 * Handle clicks on links in the text view.
 */
void event_handlers_on_text_view_link_clicked(GtkGestureClick *gesture, gint n_press,
                                             gdouble x, gdouble y, gpointer user_data);

/**
 * event_handlers_on_text_view_query_tooltip:
 * @widget: The widget that triggered the tooltip
 * @x: X coordinate for tooltip query
 * @y: Y coordinate for tooltip query
 * @keyboard_mode: Whether in keyboard mode
 * @tooltip: The tooltip object to set
 * @user_data: User data
 *
 * Handle tooltip queries for text view elements.
 * Returns: TRUE if tooltip should be shown, FALSE otherwise.
 */
gboolean event_handlers_on_text_view_query_tooltip(GtkWidget *widget, gint x, gint y,
                                                   gboolean keyboard_mode, GtkTooltip *tooltip,
                                                   gpointer user_data);

/**
 * event_handlers_on_key_pressed:
 * @controller: The key event controller
 * @keyval: The key value
 * @keycode: The key code
 * @state: Modifier state
 * @user_data: User data (typically the GtkTextView)
 *
 * Handle keyboard shortcuts and key press events.
 * Returns: TRUE if event was handled, FALSE otherwise.
 */
gboolean event_handlers_on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                                       guint keycode, GdkModifierType state, gpointer user_data);

/**
 * event_handlers_on_scroll_event:
 * @controller: The scroll event controller
 * @dx: Horizontal scroll delta
 * @dy: Vertical scroll delta
 * @user_data: User data (typically the GtkTextView)
 *
 * Handle scroll events for zoom functionality.
 * Returns: TRUE if event was handled, FALSE otherwise.
 */
gboolean event_handlers_on_scroll_event(GtkEventControllerScroll *controller, gdouble dx, gdouble dy,
                                        gpointer user_data);

/**
 * event_handlers_on_text_view_motion:
 * @controller: The motion event controller
 * @x: X coordinate of motion
 * @y: Y coordinate of motion
 * @user_data: User data (typically the GtkTextView)
 *
 * Handle mouse motion events for cursor changes.
 */
void event_handlers_on_text_view_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y,
                                        gpointer user_data);

G_END_DECLS

#endif /* GTKTEXT_UI_EVENT_HANDLERS_H */