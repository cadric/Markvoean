/* C ULTRA-MIN TEMPLATE
   Purpose: Edit action callbacks (undo, redo) for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-20 - ui/edit_actions.h
   Added: Edit actions for undo/redo functionality
*/

#pragma once

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Edit action callbacks
 * ═══════════════════════════════════════════════════════════════════════════════ */

void edit_action_undo_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void edit_action_redo_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void edit_action_version_history_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Standard text editing actions */
void edit_action_cut_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void edit_action_copy_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void edit_action_paste_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void edit_action_select_all_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);

G_END_DECLS
