/* Signal Manager Header
 * Purpose: Encapsulated signal management for GTK text editor
 * [1.0.1] - 2025-09-16 - core/signal_manager.h
 * Created: Proper encapsulation replacement for global signal variables
 */

#pragma once

#ifndef GTKTEXT_SIGNAL_MANAGER_H
#define GTKTEXT_SIGNAL_MANAGER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Opaque handle for signal manager - implementation details hidden */
typedef struct _SignalManager SignalManager;

/* Factory pattern with proper lifecycle management */
SignalManager *signal_manager_create(void);
void signal_manager_destroy(SignalManager *sm);

/* Type-safe signal connection with automatic tracking */
guint signal_manager_connect_buffer_changed(SignalManager *sm,
                                           GtkTextBuffer *buffer,
                                           GCallback callback,
                                           gpointer user_data);

/* Safe disconnection with validation */
void signal_manager_disconnect_buffer_changed(SignalManager *sm,
                                             GtkTextBuffer *buffer);

/* Cleanup all tracked signals */
void signal_manager_disconnect_all(SignalManager *sm);

/* Query signal state */
gboolean signal_manager_is_connected(SignalManager *sm, guint signal_id);
guint signal_manager_get_buffer_changed_id(SignalManager *sm);

/* Global accessor - implemented in main.c */
SignalManager *gtktext_get_signal_manager(void);

G_END_DECLS

#endif /* GTKTEXT_SIGNAL_MANAGER_H */
