/* C ULTRA-MIN TEMPLATE
   Purpose: File action callbacks (open, save, save-as)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.4.2] - 2025-09-19 - ui/file_actions.h
   Changed: Added shared helper function declaration
*/

#ifndef GTKTEXT_UI_FILE_ACTIONS_H
#define GTKTEXT_UI_FILE_ACTIONS_H

#include <gtk/gtk.h>
#include <adwaita.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - File action callbacks
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* File action callbacks for GSimpleAction */
void file_action_open_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void file_action_save_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void file_action_save_as_cb(GSimpleAction *action, GVariant *parameter, gpointer user_data);

/* Dialog completion callbacks */
void file_action_on_open_dialog_finish(GObject *source_object, GAsyncResult *res, gpointer user_data);
void file_action_on_open_dialog_finish_tab(GObject *source_object, GAsyncResult *res, gpointer user_data);
void file_action_on_save_as_dialog_finish(GObject *source_object, GAsyncResult *res, gpointer user_data);
void file_action_on_save_as_dialog_finish_tab(GObject *source_object, GAsyncResult *res, gpointer user_data);

/* Helper functions */
void file_action_setup_open_dialog_filters(GtkFileDialog *dialog);
void file_action_setup_save_dialog_filters(GtkFileDialog *dialog);
gboolean file_action_handle_open_dialog_result(GFile *file, GtkApplication *app,
                                              gboolean open_in_new_tab, GError **error);

G_END_DECLS

#endif /* GTKTEXT_UI_FILE_ACTIONS_H */