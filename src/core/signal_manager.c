/* C ULTRA-MIN TEMPLATE
   Purpose: Encapsulated signal management for GTK text editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - core/signal_manager.c
   Created: Proper encapsulation replacement for global signal variables
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/core/signal_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Private implementation details
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct _SignalManagerPrivate {
    guint buffer_changed_signal_id;
    GtkTextBuffer *connected_buffer;
    gboolean initialized;

    /* Expandable for future signal types */
    guint other_signal_ids[10];
    guint signal_count;
} SignalManagerPrivate;

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Module state management
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Private implementation - completely hidden from outside world */
struct _SignalManager {
    SignalManagerPrivate *priv;
};

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void signal_manager_reset_private(SignalManagerPrivate *priv)
{
    g_return_if_fail(priv != NULL);

    priv->buffer_changed_signal_id = 0;
    priv->connected_buffer = NULL;
    priv->signal_count = 0;

    for (int i = 0; i < 10; i++) {
        priv->other_signal_ids[i] = 0;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Signal manager lifecycle and operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

SignalManager *signal_manager_create(void)
{
    SignalManager *sm = g_new0(SignalManager, 1);
    sm->priv = g_new0(SignalManagerPrivate, 1);

    signal_manager_reset_private(sm->priv);
    sm->priv->initialized = TRUE;

    g_debug("SignalManager created with proper encapsulation");
    return sm;
}

void signal_manager_destroy(SignalManager *sm)
{
    if (!sm) return;

    /* Ensure all signals are disconnected before cleanup */
    signal_manager_disconnect_all(sm);

    if (sm->priv) {
        sm->priv->initialized = FALSE;
        g_free(sm->priv);
    }
    g_free(sm);

    g_debug("SignalManager destroyed and cleaned up");
}

guint signal_manager_connect_buffer_changed(SignalManager *sm,
                                           GtkTextBuffer *buffer,
                                           GCallback callback,
                                           gpointer user_data)
{
    g_return_val_if_fail(sm != NULL, 0);
    g_return_val_if_fail(sm->priv != NULL, 0);
    g_return_val_if_fail(sm->priv->initialized, 0);
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), 0);
    g_return_val_if_fail(callback != NULL, 0);

    /* Disconnect existing signal if any */
    signal_manager_disconnect_buffer_changed(sm, sm->priv->connected_buffer);

    /* Connect new signal */
    sm->priv->buffer_changed_signal_id = g_signal_connect(buffer, "changed",
                                                         callback, user_data);
    sm->priv->connected_buffer = buffer;

    g_debug("Buffer changed signal connected (ID: %u)",
            sm->priv->buffer_changed_signal_id);

    return sm->priv->buffer_changed_signal_id;
}

void signal_manager_disconnect_buffer_changed(SignalManager *sm, GtkTextBuffer *buffer)
{
    if (!sm || !sm->priv || !sm->priv->initialized) return;

    if (sm->priv->buffer_changed_signal_id > 0 && buffer) {
        if (g_signal_handler_is_connected(buffer, sm->priv->buffer_changed_signal_id)) {
            g_debug("Disconnecting buffer changed signal (ID: %u)",
                    sm->priv->buffer_changed_signal_id);
            g_signal_handler_disconnect(buffer, sm->priv->buffer_changed_signal_id);
        }
        sm->priv->buffer_changed_signal_id = 0;
        sm->priv->connected_buffer = NULL;
    }
}

void signal_manager_disconnect_all(SignalManager *sm)
{
    if (!sm || !sm->priv || !sm->priv->initialized) return;

    /* Disconnect buffer changed signal */
    signal_manager_disconnect_buffer_changed(sm, sm->priv->connected_buffer);

    /* Future: disconnect other tracked signals */
    for (int i = 0; i < 10; i++) {
        if (sm->priv->other_signal_ids[i] > 0) {
            /* Would disconnect other signals here */
            sm->priv->other_signal_ids[i] = 0;
        }
    }

    signal_manager_reset_private(sm->priv);
    g_debug("All signals disconnected from SignalManager");
}

gboolean signal_manager_is_connected(SignalManager *sm, guint signal_id)
{
    g_return_val_if_fail(sm != NULL, FALSE);
    g_return_val_if_fail(sm->priv != NULL, FALSE);
    g_return_val_if_fail(sm->priv->initialized, FALSE);

    if (signal_id == sm->priv->buffer_changed_signal_id && sm->priv->connected_buffer) {
        return g_signal_handler_is_connected(sm->priv->connected_buffer, signal_id);
    }

    return FALSE;
}

guint signal_manager_get_buffer_changed_id(SignalManager *sm)
{
    g_return_val_if_fail(sm != NULL, 0);
    g_return_val_if_fail(sm->priv != NULL, 0);
    g_return_val_if_fail(sm->priv->initialized, 0);

    return sm->priv->buffer_changed_signal_id;
}