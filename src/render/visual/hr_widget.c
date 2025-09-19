/* C ULTRA-MIN TEMPLATE
   Purpose: Custom horizontal rule widget for markdown rendering
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.3.9] - 2025-09-18 - render/visual/hr_widget.c
   Created: Custom GTK widget for horizontal rules that resize properly
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <gtktext/render/hr_widget.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Widget type definition
 * ═══════════════════════════════════════════════════════════════════════════════ */

struct _GtktextHRWidget {
    GtkWidget parent_instance;
    GdkRGBA color;
    gint line_width;
    gint margin_top;
    gint margin_bottom;
};

G_DEFINE_TYPE(GtktextHRWidget, gtktext_hr_widget, GTK_TYPE_WIDGET)

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Drawing and measurement functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void
gtktext_hr_widget_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    GtktextHRWidget *self = GTKTEXT_HR_WIDGET(widget);

    int width = gtk_widget_get_width(widget);
    int height = gtk_widget_get_height(widget);

    if (width <= 0 || height <= 0) return;

    /* Calculate the line position (centered vertically) */
    int line_y = (height - self->line_width) / 2;

    /* Create the line rectangle */
    graphene_rect_t line_rect = GRAPHENE_RECT_INIT(
        0, line_y,
        width, self->line_width
    );

    /* Draw the line with the current color */
    gtk_snapshot_append_color(snapshot, &self->color, &line_rect);
}

static void
gtktext_hr_widget_measure(GtkWidget *widget,
                          GtkOrientation orientation,
                          int for_size,
                          int *minimum,
                          int *natural,
                          int *minimum_baseline,
                          int *natural_baseline)
{
    GtktextHRWidget *self = GTKTEXT_HR_WIDGET(widget);
    (void)for_size; /* Unused parameter */

    if (orientation == GTK_ORIENTATION_HORIZONTAL) {
        /* Minimum width is quite small, natural width wants to expand */
        *minimum = 100;
        *natural = 400;
    } else {
        /* Height is just the line width plus margins */
        int total_height = self->line_width + self->margin_top + self->margin_bottom;
        *minimum = total_height;
        *natural = total_height;
    }

    if (minimum_baseline) *minimum_baseline = -1;
    if (natural_baseline) *natural_baseline = -1;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * LIFECYCLE - Widget initialization and cleanup
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void
gtktext_hr_widget_init(GtktextHRWidget *self)
{
    /* Set default values */
    self->color = (GdkRGBA){0.5, 0.5, 0.5, 0.7}; /* Default gray */
    self->line_width = 1;
    self->margin_top = 12;
    self->margin_bottom = 12;

    /* Make the widget expand horizontally */
    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(self), GTK_ALIGN_FILL);
}

static void
gtktext_hr_widget_class_init(GtktextHRWidgetClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->snapshot = gtktext_hr_widget_snapshot;
    widget_class->measure = gtktext_hr_widget_measure;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Widget creation and configuration
 * ═══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *
gtktext_hr_widget_new(void)
{
    return g_object_new(GTKTEXT_TYPE_HR_WIDGET, NULL);
}

void
gtktext_hr_widget_set_color(GtktextHRWidget *self, const GdkRGBA *color)
{
    g_return_if_fail(GTKTEXT_IS_HR_WIDGET(self));
    g_return_if_fail(color != NULL);

    self->color = *color;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gtktext_hr_widget_get_color(GtktextHRWidget *self, GdkRGBA *color)
{
    g_return_if_fail(GTKTEXT_IS_HR_WIDGET(self));
    g_return_if_fail(color != NULL);

    *color = self->color;
}

void
gtktext_hr_widget_set_line_width(GtktextHRWidget *self, gint width)
{
    g_return_if_fail(GTKTEXT_IS_HR_WIDGET(self));
    g_return_if_fail(width > 0);

    self->line_width = width;
    gtk_widget_queue_resize(GTK_WIDGET(self));
}

gint
gtktext_hr_widget_get_line_width(GtktextHRWidget *self)
{
    g_return_val_if_fail(GTKTEXT_IS_HR_WIDGET(self), 1);
    return self->line_width;
}