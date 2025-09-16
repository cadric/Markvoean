/* C ULTRA-MIN TEMPLATE
   Purpose: Buffer management and text event handling for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - editor/buffer_manager.c
   Changed: Extracted buffer management from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/editor/buffer_manager.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/render/markdown/markdown_engine.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Module constants and forward declarations
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Buffer data keys */
static const char *DATA_SUPPRESS_PARSE = "gtktext-suppress-reparse";
static const char *DATA_USER_DIRTY = "gtktext-user-dirty";

#include <gtktext/ui/status_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Buffer management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void buffer_manager_on_text_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    (void)user_data;
    /* Mark buffer as dirty only for user-initiated edits */
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE)) == 0) {
        g_object_set_data(G_OBJECT(buffer), DATA_USER_DIRTY, GINT_TO_POINTER(1));

        /* Update status bar to show modified status */
        GtkTextView *text_view = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(buffer),
                                                                 "gtktext-view"));
        if (text_view) {
            GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(text_view));
            if (root && GTK_IS_APPLICATION_WINDOW(root)) {
                GtkApplication *app = gtk_window_get_application(GTK_WINDOW(root));
                if (app) {
                    status_manager_update_save_status(app, _("Modified"));

                    /* DocumentManager should automatically track buffer changes,
                       so no need to manually update content here */
                }
            }
        }

        /* Only schedule live reparse for specific markdown formatting characters */
        GtkTextIter cursor_iter;
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor_iter,
                                        gtk_text_buffer_get_insert(buffer));

        /* Check if we just typed a character that might trigger markdown formatting */
        if (!gtk_text_iter_is_start(&cursor_iter)) {
            GtkTextIter prev_iter = cursor_iter;
            gtk_text_iter_backward_char(&prev_iter);
            gunichar last_char = gtk_text_iter_get_char(&prev_iter);

            /* Only trigger reparse for specific scenarios */
            gboolean should_reparse = FALSE;

            /* Check for heading markers: # at start of line followed by space */
            if (last_char == ' ') {
                GtkTextIter line_start = prev_iter;
                gtk_text_iter_set_line_offset(&line_start, 0);
                g_autofree char *line_text = gtk_text_buffer_get_text(buffer,
                                                                      &line_start,
                                                                      &cursor_iter, FALSE);
                /* Only reparse for headings with content */
                if (line_text && g_str_has_prefix(line_text, "#") &&
                    g_str_has_suffix(line_text, "# ") && strlen(line_text) > 2) {
                    should_reparse = TRUE;
                }
            }
            /* For other markdown characters, be more selective */
            else if (last_char == '*' || last_char == '_' || last_char == '`') {
                should_reparse = TRUE;
            }

            if (should_reparse) {
                /* Use a longer delay to avoid interrupting consecutive typing */
                schedule_reparse_markdown(buffer, 0, &cursor_iter);
            }
        }
    }

    /* DocumentManager now handles all autosave functionality - old system disabled */
    /* The DocumentManager will detect buffer changes via its own monitoring */
}

gboolean buffer_manager_has_unsaved_changes(GtkTextBuffer *buffer)
{
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), FALSE);

    /* Get DocumentManager from application */
    GtkApplication *app = GTK_APPLICATION(g_object_get_data(G_OBJECT(buffer), "app"));
    if (!app) {
        g_warning("Application not found in buffer data");
        return FALSE;
    }

    DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
    if (!dm) {
        g_warning("DocumentManager not found in application data");
        return FALSE;
    }

    /* Use DocumentManager to check for unsaved changes */
    return document_manager_has_unsaved_changes(dm);
}

void buffer_manager_initialize_buffer(GtkTextBuffer *buffer, GtkApplication *app)
{
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));
    g_return_if_fail(GTK_IS_APPLICATION(app));

    /* Store application reference for later use */
    g_object_set_data(G_OBJECT(buffer), "app", app);

    /* Connect buffer change signal for dirty state tracking */
    g_signal_connect(buffer, "changed", G_CALLBACK(buffer_manager_on_text_changed), NULL);
}