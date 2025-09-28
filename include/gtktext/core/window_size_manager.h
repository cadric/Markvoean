/* Window Size Manager Header
 * Purpose: GNOME HIG-compliant window sizing with persistence
 * [1.0.0] - 2025-09-18 - core/window_size_manager.h
 */

#pragma once

#ifndef GTKTEXT_WINDOW_SIZE_MANAGER_H
#define GTKTEXT_WINDOW_SIZE_MANAGER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* GNOME HIG window sizing policy implementation */
void window_size_manager_setup_window(GtkWindow *window, GSettings *settings);
void window_size_manager_save_state(GtkWindow *window, GSettings *settings);
void window_size_manager_restore_state(GtkWindow *window, GSettings *settings);

/* Adaptive layout support */
gboolean window_size_manager_is_narrow_layout(GtkWindow *window);
void window_size_manager_apply_size_constraints(GtkWindow *window);

G_END_DECLS

#endif /* GTKTEXT_WINDOW_SIZE_MANAGER_H */
