/* [0.2.0] - 2025-09-15 - include/settings.h
 * Changed: Added version header tracking.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <gtk/gtk.h>
#include <adwaita.h>
#include "cmrender.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create and display a settings dialog window
 * 
 * @param parent The parent GtkWindow for the dialog
 * @return An AdwDialog instance representing the settings dialog
 */
AdwDialog* create_settings_window(GtkWindow *parent);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H
