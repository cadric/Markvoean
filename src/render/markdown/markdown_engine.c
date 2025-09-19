/* C ULTRA-MIN TEMPLATE
   Purpose: Markdown processing engine for real-time parsing and buffer management
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - render/markdown/markdown_engine.c
   Changed: Extracted markdown processing engine from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/render/markdown/markdown_engine.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/render/theme_styles.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Module constants and data keys
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Buffer data keys for paste→markdown coordination */
static const char *DATA_SUPPRESS_PARSE = "gtktext-suppress-reparse";
static const char *DATA_REPARSE_SOURCE_ID = "gtktext-reparse-source-id";
static const char *DATA_REPARSE_TARGET_OFFSET = "gtktext-reparse-target-offset";

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Re-parse the whole buffer using cm_render_markdown_to_buffer */
static gboolean reparse_markdown_cb(gpointer user_data)
{
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(user_data);
    g_debug("[markdown_engine] Reparse callback triggered");
    /* Clear the marker that scheduled us */
    g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID, GUINT_TO_POINTER(0));

    /* Suppress recursive scheduling while we modify the buffer */
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(1));

    /* Export current buffer (with tags) back to Markdown */
    g_autofree char *md_src = cm_render_buffer_to_markdown(buffer);
    if (md_src && *md_src) {
        /* Performance optimization: skip re-parsing if content hasn't changed */
        guint current_hash = g_str_hash(md_src);
        guint previous_hash = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer),
                                                                 "content-hash"));

        if (current_hash == previous_hash) {
            g_debug("[perf] Content unchanged, skipping reparse");
            g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(0));
            return G_SOURCE_REMOVE;
        }

        g_object_set_data(G_OBJECT(buffer), "content-hash", GUINT_TO_POINTER(current_hash));
        /* Preserve a plausible cursor position */
        gint target = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer),
                                                       DATA_REPARSE_TARGET_OFFSET));

        /* Get text view and soup session for the new rendering function */
        GtkTextView *tv = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(buffer), "gtktext-view"));
#ifdef HAVE_LIBSOUP
        gpointer soup_session = g_object_get_data(G_OBJECT(buffer), "soup-session");
        if (!cm_render_markdown_to_buffer(buffer, md_src, tv, soup_session)) {
#else
        if (!cm_render_markdown_to_buffer(buffer, md_src, tv, NULL)) {
#endif
            g_warning("Realtime Markdown import failed");
        } else {
            /* Restore cursor position */
            GtkTextIter it;
            gint char_count = gtk_text_buffer_get_char_count(buffer);
            if (target < 0) target = 0;
            if (target > char_count) target = char_count;
            gtk_text_buffer_get_iter_at_offset(buffer, &it, target);
            gtk_text_buffer_place_cursor(buffer, &it);
            /* Re-embed images after re-render */
            if (tv) {
                theme_styles_update_theme_dependent_tags(buffer);
            }
        }
    }

    /* Re-enable scheduling */
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(0));

    /* Clear user change pending flag */
    g_object_set_data(G_OBJECT(buffer), "gtktext-user-change-pending", GINT_TO_POINTER(0));

    return G_SOURCE_REMOVE;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Markdown processing engine functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void schedule_reparse_markdown(GtkTextBuffer *buffer, gint inserted_len,
                               const GtkTextIter *at_iter)
{
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));
    g_return_if_fail(at_iter != NULL);

    /* Avoid scheduling if a reparse is already queued */
    guint existing = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer),
                                                        DATA_REPARSE_SOURCE_ID));
    if (existing != 0) {
        g_source_remove(existing);
    }

    /* Record a target offset near the end of the inserted text to restore cursor */
    if (at_iter) {
        gint base = gtk_text_iter_get_offset((GtkTextIter*)at_iter);
        gint target = base + (inserted_len > 0 ? inserted_len : 0);
        g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_TARGET_OFFSET,
                         GINT_TO_POINTER(target));
    }

    /* Performance optimization: adaptive delay based on buffer size */
    gint char_count = gtk_text_buffer_get_char_count(buffer);
    guint delay_ms = 30; /* Base delay */

    if (char_count > 10000) {
        delay_ms = 100;    /* Larger buffers get longer delay */
    } else if (char_count > 50000) {
        delay_ms = 200;    /* Very large buffers get even longer delay */
    }

    guint id = g_timeout_add_full(G_PRIORITY_LOW, delay_ms, reparse_markdown_cb,
                                  g_object_ref(buffer), g_object_unref);
    g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID, GUINT_TO_POINTER(id));
}

void markdown_engine_on_buffer_insert_text(GtkTextBuffer *buffer, GtkTextIter *location,
                                          gchar *text, gint len, gpointer user_data)
{
    (void)user_data;
    if (!buffer || !text || len <= 0) return;
    /* Ignore programmatic changes from our own re-rendering */
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE)) != 0) {
        return;
    }

    /* Avoid reparsing the entire buffer on single keystrokes */
    if (len == 1) return;

    /* Heuristics: Only treat larger insertions (pastes) as requiring reparse */
    gboolean looks_like_paste = (len > 8);
    if (!looks_like_paste) {
        /* Consider multi-character insertion containing newlines as paste */
        for (int i = 0; i < len; i++) {
            if (text[i] == '\n') {
                looks_like_paste = TRUE;
                break;
            }
        }
    }
    if (looks_like_paste) {
        /* Mark that a user-initiated change has occurred before reparse */
        g_object_set_data(G_OBJECT(buffer), "gtktext-user-change-pending", GINT_TO_POINTER(1));

        schedule_reparse_markdown(buffer, len, location);
    }
}

void markdown_engine_initialize_buffer(GtkTextBuffer *buffer, GtkTextView *text_view,
                                     gpointer soup_session)
{
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));
    g_return_if_fail(GTK_IS_TEXT_VIEW(text_view));

    /* Store references for later use */
    g_object_set_data(G_OBJECT(buffer), "gtktext-view", text_view);
    if (soup_session) {
        g_object_set_data(G_OBJECT(buffer), "soup-session", soup_session);
    }

    /* Connect buffer insert text signal for real-time parsing */
    g_signal_connect(buffer, "insert-text",
                    G_CALLBACK(markdown_engine_on_buffer_insert_text), NULL);
}