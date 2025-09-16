/* [0.2.0] - 2025-09-15 - include/gtktext/util.h
 * Added: Ultra-Min C Module Template example header.
 */
#ifndef GTKTEXT_UTIL_H
#define GTKTEXT_UTIL_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the utility module with a name.
 *
 * @param initial_name The initial name for the utility state.
 */
void util_init(const gchar *initial_name);

/**
 * @brief Clean up and teardown the utility module.
 */
void util_teardown(void);

/**
 * @brief Connect utility-related signals to a widget.
 *
 * @param widget The widget to connect signals to.
 */
void util_connect_signals(GtkWidget *widget);

#ifdef __cplusplus
}
#endif

#endif /* GTKTEXT_UTIL_H */