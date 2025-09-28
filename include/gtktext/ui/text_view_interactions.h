/* C ULTRA-MIN TEMPLATE
   Purpose: Text view interaction handlers and utilities for GTK markdown editor
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - ui/text_view_interactions.h
   Changed: Extracted text view interactions from main.c for better organization
*/

#pragma once

#ifndef GTKTEXT_UI_TEXT_VIEW_INTERACTIONS_H
#define GTKTEXT_UI_TEXT_VIEW_INTERACTIONS_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Text view interaction functions
 * ═══════════════════════════════════════════════════════════════════════════════ */


/**
 * Handle scroll events on text view (Ctrl+scroll for zoom)
 * @param controller Scroll controller
 * @param dx Horizontal scroll delta
 * @param dy Vertical scroll delta
 * @param user_data Text view pointer
 * @return TRUE if handled, FALSE otherwise
 */
gboolean text_view_on_scroll_event(GtkEventControllerScroll *controller, gdouble dx,
                                   gdouble dy, gpointer user_data);

/**
 * Handle mouse motion events on text view (cursor changes for links)
 * @param controller Motion controller
 * @param x X coordinate
 * @param y Y coordinate
 * @param user_data Text view pointer
 */
void text_view_on_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y,
                         gpointer user_data);

/**
 * Handle link clicks in text view
 * @param gesture Click gesture
 * @param n_press Number of presses
 * @param x X coordinate
 * @param y Y coordinate
 * @param user_data Text view pointer
 */
void text_view_on_link_clicked(GtkGestureClick *gesture, gint n_press, gdouble x,
                               gdouble y, gpointer user_data);

/**
 * Handle tooltip queries for text view
 * @param widget Text view widget
 * @param x X coordinate
 * @param y Y coordinate
 * @param keyboard_mode Whether triggered by keyboard
 * @param tooltip Tooltip object
 * @param user_data User data
 * @return TRUE if tooltip should be shown
 */
gboolean text_view_on_query_tooltip(GtkWidget *widget, gint x, gint y,
                                    gboolean keyboard_mode, GtkTooltip *tooltip,
                                    gpointer user_data);

/**
 * Zoom text view in or out
 * @param text_view Text view to zoom
 * @param zoom_in TRUE to zoom in, FALSE to zoom out
 */
void text_view_zoom(GtkTextView *text_view, gboolean zoom_in);

/**
 * Copy selected text as markdown
 * @param text_view Text view to copy from
 */
void text_view_copy_selected_as_markdown(GtkTextView *text_view);

/**
 * Setup blockquote overlay for text view
 * @param text_view Text view to setup overlay for
 */
void text_view_setup_blockquote_overlay(GtkTextView *text_view);

G_END_DECLS

#endif /* GTKTEXT_UI_TEXT_VIEW_INTERACTIONS_H */
