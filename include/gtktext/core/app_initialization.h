/* Application initialization and UI setup
 * [1.1.0] - 2025-09-18 - core/app_initialization.h
 * MAJOR: Clean GTK4-compliant implementation with resource loading
 */

#ifndef GTKTEXT_CORE_APP_INITIALIZATION_H
#define GTKTEXT_CORE_APP_INITIALIZATION_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

void app_initialization_activate(GApplication *application);
void app_initialization_open(GApplication *application, GFile **files, gint n_files,
                             const gchar *hint);

G_END_DECLS

#endif /* GTKTEXT_CORE_APP_INITIALIZATION_H */