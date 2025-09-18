/* C ULTRA-MIN TEMPLATE
   Purpose: GTK4 + libadwaita window sizing with GSettings persistence and adaptivity
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.1.0] - 2025-09-18 - core/window_size_manager.c
   MAJOR: Proper GTK4 GSettings binding approach with monitor workarea validation
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

#define MIN_WINDOW_WIDTH        600
#define MIN_WINDOW_HEIGHT       400
#define DEFAULT_WINDOW_WIDTH    900
#define DEFAULT_WINDOW_HEIGHT   700
#define MAX_WINDOW_WIDTH        3840
#define MAX_WINDOW_HEIGHT       2160
#define NARROW_LAYOUT_THRESHOLD 800  /* tune to your UI */

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Size constraint and validation functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations */
static GtkWidget *find_widget_by_name(GtkWidget *parent, const char *name);

/* clamp to HIG and hard caps */
static inline void clamp_window_size(gint *w, gint *h) {
    if (!w || !h) return;
    if (*w < MIN_WINDOW_WIDTH)  *w = MIN_WINDOW_WIDTH;
    if (*h < MIN_WINDOW_HEIGHT) *h = MIN_WINDOW_HEIGHT;
    if (*w > MAX_WINDOW_WIDTH)  *w = MAX_WINDOW_WIDTH;
    if (*h > MAX_WINDOW_HEIGHT) *h = MAX_WINDOW_HEIGHT;
}

/* clamp to current monitor geometry (Wayland/X11 safe) */
static void clamp_to_workarea(GtkWindow *window, gint *w, gint *h) {
    if (!GTK_IS_WINDOW(window) || !w || !h) return;

    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(window));
    if (!GDK_IS_DISPLAY(display)) return;

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (!surface) return;

    GdkMonitor *monitor = gdk_display_get_monitor_at_surface(display, surface);
    if (!monitor) return;

    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);

    /* Use 90% of monitor size as reasonable maximum */
    gint max_w = (gint)(geometry.width * 0.9);
    gint max_h = (gint)(geometry.height * 0.9);

    if (*w > max_w) *w = max_w;
    if (*h > max_h) *h = max_h;
}

/* Global state to track save-as button and its parent for layout-shift-free hiding */
static struct {
    GtkWidget *save_as_button;
    GtkWidget *parent_container;
    gint position_in_parent;
    gboolean is_detached;
    gboolean last_narrow_state;
} button_state = {NULL, NULL, -1, FALSE, FALSE};

/* adaptive layout toggle with no-layout-shift widget management */
static void update_adaptive_classes(GtkWindow *window) {
    GtkWidget *w = GTK_WIDGET(window);
    const int alloc_w = gtk_widget_get_width(w);
    const gboolean is_narrow = alloc_w < NARROW_LAYOUT_THRESHOLD;

    if (is_narrow) {
        gtk_widget_add_css_class(w, "narrow-layout");
    } else {
        gtk_widget_remove_css_class(w, "narrow-layout");
    }

    /* Only perform widget operations if narrow state changed */
    if (button_state.last_narrow_state != is_narrow) {
        /* Find save-as button on first run or if not found */
        if (!button_state.save_as_button) {
            GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(window));
            if (child) {
                button_state.save_as_button = find_widget_by_name(child, "save_as_button");
                if (button_state.save_as_button) {
                    button_state.parent_container = gtk_widget_get_parent(button_state.save_as_button);
                    /* Find position in parent for later re-insertion */
                    if (GTK_IS_BOX(button_state.parent_container)) {
                        GtkWidget *sibling = gtk_widget_get_first_child(button_state.parent_container);
                        button_state.position_in_parent = 0;
                        while (sibling && sibling != button_state.save_as_button) {
                            button_state.position_in_parent++;
                            sibling = gtk_widget_get_next_sibling(sibling);
                        }
                    }
                }
            }
        }

        /* Remove/add button from/to layout to prevent measurement issues */
        if (button_state.save_as_button && button_state.parent_container) {
            if (is_narrow && !button_state.is_detached) {
                /* Remove from layout entirely */
                g_object_ref(button_state.save_as_button);  /* Keep alive */
                gtk_box_remove(GTK_BOX(button_state.parent_container), button_state.save_as_button);
                button_state.is_detached = TRUE;
                g_debug("Adaptive layout: narrow - save-as button removed from layout (width: %d)", alloc_w);
            } else if (!is_narrow && button_state.is_detached) {
                /* Re-add to layout at original position */
                GtkWidget *sibling = gtk_widget_get_first_child(button_state.parent_container);
                GtkWidget *insert_after = NULL;

                /* Find the widget to insert after based on original position */
                for (gint i = 0; i < button_state.position_in_parent - 1 && sibling; i++) {
                    insert_after = sibling;
                    sibling = gtk_widget_get_next_sibling(sibling);
                }

                if (button_state.position_in_parent == 0) {
                    /* Insert at beginning */
                    gtk_box_prepend(GTK_BOX(button_state.parent_container), button_state.save_as_button);
                } else {
                    /* Insert after the calculated sibling */
                    gtk_box_insert_child_after(GTK_BOX(button_state.parent_container),
                                             button_state.save_as_button,
                                             insert_after);
                }

                g_object_unref(button_state.save_as_button);  /* Release our ref */
                button_state.is_detached = FALSE;
                g_debug("Adaptive layout: wide - save-as button restored to layout at position %d (width: %d)",
                       button_state.position_in_parent, alloc_w);
            }
        }

        button_state.last_narrow_state = is_narrow;
    }
}

/* Helper function to find widget by buildable ID */
static GtkWidget *find_widget_by_name(GtkWidget *parent, const char *name) {
    if (!GTK_IS_WIDGET(parent) || !name) return NULL;

    /* Check if this widget has the name we're looking for */
    const char *widget_name = gtk_buildable_get_buildable_id(GTK_BUILDABLE(parent));
    if (widget_name && g_strcmp0(widget_name, name) == 0) {
        return parent;
    }

    /* Recursively search children */
    for (GtkWidget *child = gtk_widget_get_first_child(parent);
         child != NULL;
         child = gtk_widget_get_next_sibling(child)) {
        GtkWidget *found = find_widget_by_name(child, name);
        if (found) return found;
    }

    return NULL;
}

/* notify handler: keep adaptivity in sync */
static void on_default_size_notify(GObject *obj, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED) {
    update_adaptive_classes(GTK_WINDOW(obj));
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Window size management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void window_size_manager_apply_size_constraints(GtkWindow *window) {
    if (!GTK_IS_WINDOW(window)) return;

    gtk_widget_set_size_request(GTK_WIDGET(window), MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

    gint w = 0, h = 0;
    gtk_window_get_default_size(window, &w, &h);
    if (w == 0 || h == 0) {
        w = DEFAULT_WINDOW_WIDTH;
        h = DEFAULT_WINDOW_HEIGHT;
    }
    clamp_window_size(&w, &h);
    clamp_to_workarea(window, &w, &h);
    gtk_window_set_default_size(window, w, h);
}

gboolean window_size_manager_is_narrow_layout(GtkWindow *window) {
    if (!GTK_IS_WINDOW(window)) return FALSE;
    return gtk_widget_get_width(GTK_WIDGET(window)) < NARROW_LAYOUT_THRESHOLD;
}

void window_size_manager_setup_window(GtkWindow *window, GSettings *settings) {
    g_return_if_fail(GTK_IS_WINDOW(window));
    g_return_if_fail(G_IS_SETTINGS(settings));

    /* Minimum size per HIG */
    gtk_widget_set_size_request(GTK_WIDGET(window), MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

    /* Bind settings <-> window properties. Why: fewer handlers, instant persistence. */
    g_settings_bind(settings, "window-width",  window, "default-width",  G_SETTINGS_BIND_DEFAULT);
    g_settings_bind(settings, "window-height", window, "default-height", G_SETTINGS_BIND_DEFAULT);
    g_settings_bind(settings, "window-maximized", window, "maximized",  G_SETTINGS_BIND_DEFAULT);

    /* Initialize sane defaults if schema values are unset (0) */
    gint w = 0, h = 0;
    gtk_window_get_default_size(window, &w, &h);
    if (w <= 0 || h <= 0) {
        gtk_window_set_default_size(window, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
    }

    /* Enforce constraints and adaptivity on startup */
    window_size_manager_apply_size_constraints(window);
    update_adaptive_classes(window);

    /* Keep adaptivity in sync with user-driven resizes */
    g_signal_connect(window, "notify::default-width",  G_CALLBACK(on_default_size_notify), NULL);
    g_signal_connect(window, "notify::default-height", G_CALLBACK(on_default_size_notify), NULL);

    g_debug("Window size manager initialized with GSettings binding");
}

void window_size_manager_restore_state(GtkWindow *window, GSettings *settings) {
    g_return_if_fail(GTK_IS_WINDOW(window));
    g_return_if_fail(G_IS_SETTINGS(settings));

    gint w = g_settings_get_int(settings, "window-width");
    gint h = g_settings_get_int(settings, "window-height");
    gboolean maximized = g_settings_get_boolean(settings, "window-maximized");

    if (w <= 0 || h <= 0) {
        w = DEFAULT_WINDOW_WIDTH;
        h = DEFAULT_WINDOW_HEIGHT;
    }

    clamp_window_size(&w, &h);
    clamp_to_workarea(window, &w, &h);
    gtk_window_set_default_size(window, w, h);

    if (maximized) gtk_window_maximize(window);

    /* Ensure constraints after restore */
    window_size_manager_apply_size_constraints(window);

    g_debug("Restored window state: %dx%d, maximized: %s", w, h, maximized ? "true" : "false");
}

void window_size_manager_save_state(GtkWindow *window, GSettings *settings) {
    g_return_if_fail(GTK_IS_WINDOW(window));
    g_return_if_fail(G_IS_SETTINGS(settings));

    const gboolean maximized = gtk_window_is_maximized(window);
    g_settings_set_boolean(settings, "window-maximized", maximized);

    /* Avoid stomping last normal size while maximized. Why: preserve user's non-max size. */
    if (!maximized) {
        gint w = 0, h = 0;
        gtk_window_get_default_size(window, &w, &h);
        if (w <= 0 || h <= 0) {
            w = DEFAULT_WINDOW_WIDTH;
            h = DEFAULT_WINDOW_HEIGHT;
        }
        clamp_window_size(&w, &h);
        g_settings_set_int(settings, "window-width",  w);
        g_settings_set_int(settings, "window-height", h);

        g_debug("Saved window size on close: %dx%d", w, h);
    }
}