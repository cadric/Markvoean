/* C ULTRA-MIN TEMPLATE
   Purpose: GNOME HIG-compliant window sizing with persistence
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-18 - core/window_size_manager.c
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib.h>
#include <gtktext/core/window_size_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * CONSTANTS - GNOME HIG sizing requirements
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* GNOME HIG minimum sizes */
#define MIN_WINDOW_WIDTH 600    /* Minimum usable width */
#define MIN_WINDOW_HEIGHT 400   /* Minimum usable height */
#define HIG_TARGET_WIDTH 1024   /* HIG target minimum */
#define HIG_TARGET_HEIGHT 600   /* HIG target minimum */

/* Default sizes for content-appropriate initial sizing */
#define DEFAULT_WINDOW_WIDTH 900   /* Good for markdown editing */
#define DEFAULT_WINDOW_HEIGHT 700  /* Sufficient for document + UI */

/* Narrow layout breakpoint (similar to libadwaita patterns) */
#define NARROW_LAYOUT_THRESHOLD 800

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Size constraint and validation functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void clamp_window_size(gint *width, gint *height)
{
    g_return_if_fail(width != NULL && height != NULL);

    /* Clamp to minimum GNOME HIG requirements */
    if (*width < MIN_WINDOW_WIDTH) {
        g_debug("Clamping window width from %d to minimum %d", *width, MIN_WINDOW_WIDTH);
        *width = MIN_WINDOW_WIDTH;
    }

    if (*height < MIN_WINDOW_HEIGHT) {
        g_debug("Clamping window height from %d to minimum %d", *height, MIN_WINDOW_HEIGHT);
        *height = MIN_WINDOW_HEIGHT;
    }

    /* Reasonable maximum limits to prevent unusable windows */
    if (*width > 3840) *width = 3840;  /* 4K width */
    if (*height > 2160) *height = 2160; /* 4K height */
}

static gboolean is_size_valid_for_display(gint width, gint height, GdkDisplay *display)
{
    g_return_val_if_fail(GDK_IS_DISPLAY(display), FALSE);

    /* Get the primary monitor to validate against screen size */
    GListModel *monitors = gdk_display_get_monitors(display);
    guint n_monitors = g_list_model_get_n_items(monitors);

    if (n_monitors == 0) {
        g_warning("No monitors found, using default validation");
        return (width >= MIN_WINDOW_WIDTH && height >= MIN_WINDOW_HEIGHT);
    }

    /* Check against primary monitor */
    GdkMonitor *monitor = g_list_model_get_item(monitors, 0);
    if (!monitor) return FALSE;

    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    g_object_unref(monitor);

    /* Window should not exceed 90% of screen size */
    gint max_width = (gint)(geometry.width * 0.9);
    gint max_height = (gint)(geometry.height * 0.9);

    return (width <= max_width && height <= max_height &&
            width >= MIN_WINDOW_WIDTH && height >= MIN_WINDOW_HEIGHT);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * ADAPTIVE LAYOUT - Responsive behavior management
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void update_adaptive_classes(GtkWindow *window)
{
    g_return_if_fail(GTK_IS_WINDOW(window));

    GtkWidget *widget = GTK_WIDGET(window);
    gint width, height;
    gtk_window_get_default_size(window, &width, &height);

    /* Apply narrow layout class based on width */
    if (width < NARROW_LAYOUT_THRESHOLD) {
        gtk_widget_add_css_class(widget, "narrow-layout");
        g_debug("Applied narrow layout for width: %d", width);
    } else {
        gtk_widget_remove_css_class(widget, "narrow-layout");
        g_debug("Removed narrow layout for width: %d", width);
    }

    /* Traverse widget tree to find adaptive elements - simplified approach */
    GtkWidget *toolbar_view = gtk_widget_get_first_child(widget);
    if (toolbar_view && ADW_IS_TOOLBAR_VIEW(toolbar_view)) {
        /* Apply adaptive behavior through CSS classes */
        if (width < NARROW_LAYOUT_THRESHOLD) {
            g_debug("Applying narrow layout adaptations");
        } else {
            g_debug("Removing narrow layout adaptations");
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Window state change callbacks
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void on_window_size_changed(GtkWindow *window, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data)
{
    GSettings *settings = G_SETTINGS(user_data);
    g_return_if_fail(GTK_IS_WINDOW(window) && G_IS_SETTINGS(settings));

    /* Only save if window is not maximized and is visible */
    if (gtk_window_is_maximized(window) || !gtk_widget_get_visible(GTK_WIDGET(window))) {
        return;
    }

    gint width, height;
    gtk_window_get_default_size(window, &width, &height);

    /* Validate size before saving */
    clamp_window_size(&width, &height);

    g_debug("Saving window size: %dx%d", width, height);
    g_settings_set_int(settings, "window-width", width);
    g_settings_set_int(settings, "window-height", height);

    /* Update adaptive layout */
    update_adaptive_classes(window);
}

static void on_window_state_changed(GtkWindow *window, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data)
{
    GSettings *settings = G_SETTINGS(user_data);
    g_return_if_fail(G_IS_SETTINGS(settings));

    gboolean is_maximized = gtk_window_is_maximized(window);
    g_debug("Window maximized state changed: %s", is_maximized ? "true" : "false");
    g_settings_set_boolean(settings, "window-maximized", is_maximized);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Window size management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void window_size_manager_setup_window(GtkWindow *window, GSettings *settings)
{
    g_return_if_fail(GTK_IS_WINDOW(window));
    g_return_if_fail(G_IS_SETTINGS(settings));

    /* Set minimum size constraints per GNOME HIG */
    gtk_widget_set_size_request(GTK_WIDGET(window), MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

    /* Restore saved state or use defaults */
    window_size_manager_restore_state(window, settings);

    /* Connect signals for automatic persistence */
    g_signal_connect(window, "notify::default-width",
                    G_CALLBACK(on_window_size_changed), settings);
    g_signal_connect(window, "notify::default-height",
                    G_CALLBACK(on_window_size_changed), settings);
    g_signal_connect(window, "notify::maximized",
                    G_CALLBACK(on_window_state_changed), settings);

    g_debug("Window size manager initialized for window");
}

void window_size_manager_restore_state(GtkWindow *window, GSettings *settings)
{
    g_return_if_fail(GTK_IS_WINDOW(window));
    g_return_if_fail(G_IS_SETTINGS(settings));

    /* Get saved dimensions */
    gint saved_width = g_settings_get_int(settings, "window-width");
    gint saved_height = g_settings_get_int(settings, "window-height");
    gboolean was_maximized = g_settings_get_boolean(settings, "window-maximized");

    /* Validate against current display */
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(window));
    if (!is_size_valid_for_display(saved_width, saved_height, display)) {
        g_debug("Saved size %dx%d invalid for current display, using defaults",
                saved_width, saved_height);
        saved_width = DEFAULT_WINDOW_WIDTH;
        saved_height = DEFAULT_WINDOW_HEIGHT;
        was_maximized = FALSE;
    }

    /* Apply size constraints */
    clamp_window_size(&saved_width, &saved_height);

    /* Set the size */
    gtk_window_set_default_size(window, saved_width, saved_height);

    /* Restore maximized state if it was set */
    if (was_maximized) {
        gtk_window_maximize(window);
    }

    g_debug("Restored window state: %dx%d, maximized: %s",
            saved_width, saved_height, was_maximized ? "true" : "false");
}

void window_size_manager_save_state(GtkWindow *window, GSettings *settings)
{
    g_return_if_fail(GTK_IS_WINDOW(window));
    g_return_if_fail(G_IS_SETTINGS(settings));

    /* Save current maximized state */
    gboolean is_maximized = gtk_window_is_maximized(window);
    g_settings_set_boolean(settings, "window-maximized", is_maximized);

    /* Save size only if not maximized */
    if (!is_maximized) {
        gint width, height;
        gtk_window_get_default_size(window, &width, &height);

        clamp_window_size(&width, &height);

        g_settings_set_int(settings, "window-width", width);
        g_settings_set_int(settings, "window-height", height);

        g_debug("Saved window size on close: %dx%d", width, height);
    }
}

gboolean window_size_manager_is_narrow_layout(GtkWindow *window)
{
    g_return_val_if_fail(GTK_IS_WINDOW(window), FALSE);

    gint width, height;
    gtk_window_get_default_size(window, &width, &height);

    return width < NARROW_LAYOUT_THRESHOLD;
}

void window_size_manager_apply_size_constraints(GtkWindow *window)
{
    g_return_if_fail(GTK_IS_WINDOW(window));

    /* Ensure minimum size is always enforced */
    gtk_widget_set_size_request(GTK_WIDGET(window), MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

    /* Get current size and clamp if needed */
    gint width, height;
    gtk_window_get_default_size(window, &width, &height);

    gint original_width = width, original_height = height;
    clamp_window_size(&width, &height);

    if (width != original_width || height != original_height) {
        g_debug("Applying size constraints: %dx%d -> %dx%d",
                original_width, original_height, width, height);
        gtk_window_set_default_size(window, width, height);
    }
}