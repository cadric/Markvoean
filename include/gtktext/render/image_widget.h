/* [0.2.1] - 2025-09-28 - include/gtktext/image_widget.h
 * Added: GObject type system for image widget management.
 */

#pragma once
#ifndef GTKTEXT_IMAGE_WIDGET_H
#define GTKTEXT_IMAGE_WIDGET_H

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GTKTEXT_TYPE_IMAGE_WIDGET (gtktext_image_widget_get_type())
G_DECLARE_FINAL_TYPE(GtktextImageWidget, gtktext_image_widget, GTKTEXT, IMAGE_WIDGET, GObject)

/**
 * GtktextImageWidget:
 * 
 * A GObject representing an image widget with metadata for markdown documents.
 */

/**
 * gtktext_image_widget_new:
 * @picture: the #GtkPicture widget
 * @url: the image URL
 * @alt_text: (nullable): the alt text for the image
 * 
 * Creates a new #GtktextImageWidget instance.
 * 
 * Returns: (transfer full): A new #GtktextImageWidget
 */
GtktextImageWidget *gtktext_image_widget_new(GtkPicture *picture, const gchar *url, const gchar *alt_text);

/**
 * gtktext_image_widget_get_picture:
 * @image_widget: a #GtktextImageWidget
 * 
 * Gets the GtkPicture widget.
 * 
 * Returns: (transfer none): The #GtkPicture widget
 */
GtkPicture *gtktext_image_widget_get_picture(GtktextImageWidget *image_widget);

/**
 * gtktext_image_widget_get_url:
 * @image_widget: a #GtktextImageWidget
 * 
 * Gets the image URL.
 * 
 * Returns: (transfer none): The image URL
 */
const gchar *gtktext_image_widget_get_url(GtktextImageWidget *image_widget);

/**
 * gtktext_image_widget_get_alt_text:
 * @image_widget: a #GtktextImageWidget
 * 
 * Gets the alt text.
 * 
 * Returns: (transfer none) (nullable): The alt text or %NULL
 */
const gchar *gtktext_image_widget_get_alt_text(GtktextImageWidget *image_widget);

/**
 * gtktext_image_widget_set_alt_text:
 * @image_widget: a #GtktextImageWidget
 * @alt_text: (nullable): the alt text to set
 * 
 * Sets the alt text for the image.
 */
void gtktext_image_widget_set_alt_text(GtktextImageWidget *image_widget, const gchar *alt_text);

G_END_DECLS

#endif /* GTKTEXT_IMAGE_WIDGET_H */
