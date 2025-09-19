/* C ULTRA-MIN TEMPLATE
   Purpose: Dialog management (unsaved changes, autorecover)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - ui/dialogs.h
   Changed: Extracted dialog management from main.c for better organization
*/

#ifndef GTKTEXT_UI_DIALOGS_H
#define GTKTEXT_UI_DIALOGS_H

#include <gtk/gtk.h>
#include <adwaita.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Dialog management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Unsaved changes dialog */
gboolean dialogs_has_unsaved_changes(GtkTextBuffer *buffer);

/* Autorecover functionality */
void dialogs_check_for_autorecover(GtkApplication *app);
void dialogs_show_autorecover_dialog(GtkWindow *parent, const char *autosave_path);
void dialogs_on_autorecover_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data);

G_END_DECLS

#endif /* GTKTEXT_UI_DIALOGS_H */