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

/* Forward declaration */
static gboolean reparse_markdown_cb(gpointer user_data);

/* Check if a line contains a completed markdown heading pattern */
static gboolean is_completed_heading(const gchar *line_text)
{
    if (!line_text) return FALSE;

    /* Check for # followed by space (and optional text) */
    gint hash_count = 0;
    const gchar *p = line_text;

    /* Count leading # characters */
    while (*p == '#' && hash_count < 6) {
        hash_count++;
        p++;
    }

    /* Must have 1-6 # followed by space */
    return (hash_count >= 1 && hash_count <= 6 && *p == ' ');
}

/* Check if a line contains a completed horizontal rule */
static gboolean is_completed_hr(const gchar *line_text)
{
    if (!line_text) return FALSE;

    /* Trim whitespace */
    while (g_ascii_isspace(*line_text)) line_text++;

    /* Check for exactly three dashes (CommonMark standard) */
    return (g_str_has_prefix(line_text, "---") &&
            (line_text[3] == '\0' || g_ascii_isspace(line_text[3])));
}

/* Check if a line contains a completed list item */
static gboolean is_completed_list_item(const gchar *line_text)
{
    if (!line_text) return FALSE;

    /* Trim leading whitespace */
    while (g_ascii_isspace(*line_text)) line_text++;

    /* Check for unordered list: - or * followed by space */
    if ((*line_text == '-' || *line_text == '*') &&
        line_text[1] == ' ') {
        return TRUE;
    }

    /* Check for ordered list: digit(s) followed by . and space */
    if (g_ascii_isdigit(*line_text)) {
        const gchar *p = line_text;
        while (g_ascii_isdigit(*p)) p++;
        return (*p == '.' && *(p+1) == ' ');
    }

    return FALSE;
}

/* Get the current line text around the cursor */
static gchar* get_current_line_text(GtkTextBuffer *buffer, const GtkTextIter *iter)
{
    GtkTextIter line_start, line_end;

    /* Get line boundaries */
    line_start = *iter;
    gtk_text_iter_set_line_offset(&line_start, 0);

    line_end = line_start;
    if (!gtk_text_iter_ends_line(&line_end)) {
        gtk_text_iter_forward_to_line_end(&line_end);
    }

    return gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);
}

/* Schedule a line-based reparse for real-time markdown detection */
static void schedule_line_reparse(GtkTextBuffer *buffer, const GtkTextIter *iter)
{
    /* For now, we'll use the existing full reparse mechanism but with a shorter delay */
    guint existing = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer),
                                                        DATA_REPARSE_SOURCE_ID));
    if (existing != 0) {
        g_source_remove(existing);
    }

    /* Record target offset */
    gint target = gtk_text_iter_get_offset((GtkTextIter*)iter);
    g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_TARGET_OFFSET,
                     GINT_TO_POINTER(target));

    /* Mark that a user-initiated change has occurred */
    g_object_set_data(G_OBJECT(buffer), "gtktext-user-change-pending", GINT_TO_POINTER(1));

    /* Use a very short delay for real-time feedback */
    guint id = g_timeout_add_full(G_PRIORITY_LOW, 50, reparse_markdown_cb,
                                  g_object_ref(buffer), g_object_unref);
    g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID, GUINT_TO_POINTER(id));
}

/* Re-parse the whole buffer using cm_render_markdown_to_buffer */
static gboolean reparse_markdown_cb(gpointer user_data)
{
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(user_data);
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

void cancel_pending_reparse_markdown(GtkTextBuffer *buffer)
{
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));

    /* Cancel any pending reparse operation */
    guint existing = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer),
                                                        DATA_REPARSE_SOURCE_ID));
    if (existing != 0) {
        g_source_remove(existing);
        g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID, GUINT_TO_POINTER(0));
        g_debug("Cancelled pending markdown reparse operation");
    }
}

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

    /* Handle single character insertions for real-time markdown detection */
    if (len == 1) {
        gchar inserted_char = text[0];

        /* Check for markdown completion triggers */
        if (inserted_char == ' ' || inserted_char == '-') {
            g_autofree gchar *line_text = get_current_line_text(buffer, location);

            if (line_text) {
                gboolean should_reparse = FALSE;

                /* Check for completed markdown patterns */
                if (inserted_char == ' ') {
                    /* Space might complete heading (# ) or list item (- ) */
                    should_reparse = is_completed_heading(line_text) ||
                                   is_completed_list_item(line_text);
                } else if (inserted_char == '-') {
                    /* Dash might complete horizontal rule (---) */
                    should_reparse = is_completed_hr(line_text);
                }

                if (should_reparse) {
                    schedule_line_reparse(buffer, location);
                    return;
                }
            }
        }

        /* No markdown pattern detected, no reparse needed */
        return;
    }

    /* Handle multi-character insertions (pastes) */
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