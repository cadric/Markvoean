/* C ULTRA-MIN TEMPLATE
   Purpose: Application action callbacks (preferences, about, shortcuts)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - ui/app_actions.h
   Changed: Extracted app actions from main.c for better organization
*/

#ifndef GTKTEXT_UI_APP_ACTIONS_H
#define GTKTEXT_UI_APP_ACTIONS_H

#include <gtk/gtk.h>
#include <adwaita.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Application action callbacks
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Application action callbacks for GSimpleAction */
void app_action_preferences_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void app_action_about_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void app_action_shortcuts_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);

G_END_DECLS

#endif /* GTKTEXT_UI_APP_ACTIONS_H */