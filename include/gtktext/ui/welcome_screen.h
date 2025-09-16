/* C ULTRA-MIN TEMPLATE
   Purpose: Welcome screen functionality for GTK markdown editor
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - ui/welcome_screen.h
   Changed: Extracted welcome screen from main.c for better organization
*/

#ifndef GTKTEXT_UI_WELCOME_SCREEN_H
#define GTKTEXT_UI_WELCOME_SCREEN_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Welcome screen functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Callback for welcome screen "Open" button
 * @param button The clicked button
 * @param user_data Application pointer
 */
void welcome_screen_open_cb(GtkButton *button, gpointer user_data);

/**
 * Callback for welcome screen "New" button
 * @param button The clicked button
 * @param user_data Application pointer
 */
void welcome_screen_new_cb(GtkButton *button, gpointer user_data);

/**
 * Show the welcome screen (switch stack to welcome view)
 * @param app Application instance
 */
void welcome_screen_show(GtkApplication *app);

/**
 * Hide the welcome screen (switch stack to editor view)
 * @param app Application instance
 */
void welcome_screen_hide(GtkApplication *app);

G_END_DECLS

#endif /* GTKTEXT_UI_WELCOME_SCREEN_H */