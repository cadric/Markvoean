/* [1.3.9] - 2025-09-18 - include/gtktext/render/hr_widget.h
 * Created: Custom horizontal rule widget for proper markdown HR rendering
 */

#ifndef GTKTEXT_HR_WIDGET_H
#define GTKTEXT_HR_WIDGET_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GTKTEXT_TYPE_HR_WIDGET (gtktext_hr_widget_get_type())
G_DECLARE_FINAL_TYPE(GtktextHRWidget, gtktext_hr_widget, GTKTEXT, HR_WIDGET, GtkWidget)

/**
 * gtktext_hr_widget_new:
 *
 * Creates a new horizontal rule widget.
 *
 * Returns: (transfer full): A new #GtktextHRWidget
 */
GtkWidget *gtktext_hr_widget_new(void);

/**
 * gtktext_hr_widget_set_color:
 * @self: A #GtktextHRWidget
 * @color: The color for the horizontal line
 *
 * Sets the color of the horizontal line.
 */
void gtktext_hr_widget_set_color(GtktextHRWidget *self, const GdkRGBA *color);

/**
 * gtktext_hr_widget_get_color:
 * @self: A #GtktextHRWidget
 * @color: (out): Location to store the current color
 *
 * Gets the current color of the horizontal line.
 */
void gtktext_hr_widget_get_color(GtktextHRWidget *self, GdkRGBA *color);

/**
 * gtktext_hr_widget_set_line_width:
 * @self: A #GtktextHRWidget
 * @width: The line width in pixels
 *
 * Sets the width (thickness) of the horizontal line.
 */
void gtktext_hr_widget_set_line_width(GtktextHRWidget *self, gint width);

/**
 * gtktext_hr_widget_get_line_width:
 * @self: A #GtktextHRWidget
 *
 * Gets the current line width.
 *
 * Returns: The line width in pixels
 */
gint gtktext_hr_widget_get_line_width(GtktextHRWidget *self);

G_END_DECLS

#endif /* GTKTEXT_HR_WIDGET_H */