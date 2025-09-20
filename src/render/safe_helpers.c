/* C ULTRA-MIN TEMPLATE
   Purpose: Type-safe and error-checked helpers for GTK text buffer rendering operations
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-19 - render/safe_helpers.c
   Phase 2: Enhanced safety for async rendering pipeline
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtktext/render/safe_helpers.h>
#include <stdarg.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * META - Error domain and constants
 * ═══════════════════════════════════════════════════════════════════════════════ */

GQuark gtktext_render_error_quark(void)
{
    return g_quark_from_static_string("gtktext-render-error-quark");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Utility functions for result management
 * ═══════════════════════════════════════════════════════════════════════════════ */

SafeRenderResult safe_render_result_success(void)
{
    SafeRenderResult result = {0};
    result.success = TRUE;
    result.error = NULL;
    return result;
}

SafeRenderResult safe_render_result_error(GtktextRenderError error_code,
                                         const gchar *format,
                                         ...)
{
    SafeRenderResult result = {0};
    result.success = FALSE;

    if (format) {
        va_list args;
        va_start(args, format);
        gchar *message = g_strdup_vprintf(format, args);
        va_end(args);

        result.error = g_error_new(GTKTEXT_RENDER_ERROR, error_code, "%s", message);
        g_free(message);
    } else {
        result.error = g_error_new(GTKTEXT_RENDER_ERROR, error_code, "Rendering operation failed");
    }

    return result;
}

void safe_render_result_clear(SafeRenderResult *result)
{
    if (result && result->error) {
        g_error_free(result->error);
        result->error = NULL;
    }
    if (result) {
        result->success = FALSE;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * VALIDATION - Buffer and iterator validation functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean safe_buffer_is_valid(GtkTextBuffer *buffer, GError **error)
{
    if (!buffer) {
        g_set_error(error, GTKTEXT_RENDER_ERROR, GTKTEXT_RENDER_ERROR_INVALID_BUFFER,
                   "Buffer is NULL");
        return FALSE;
    }

    if (!GTK_IS_TEXT_BUFFER(buffer)) {
        g_set_error(error, GTKTEXT_RENDER_ERROR, GTKTEXT_RENDER_ERROR_INVALID_BUFFER,
                   "Object is not a valid GtkTextBuffer");
        return FALSE;
    }

    return TRUE;
}

gboolean safe_iter_is_valid(GtkTextBuffer *buffer, const GtkTextIter *iter, GError **error)
{
    if (!buffer || !iter) {
        g_set_error(error, GTKTEXT_RENDER_ERROR, GTKTEXT_RENDER_ERROR_INVALID_ITER,
                   "Buffer or iterator is NULL");
        return FALSE;
    }

    if (!gtk_text_iter_get_buffer(iter)) {
        g_set_error(error, GTKTEXT_RENDER_ERROR, GTKTEXT_RENDER_ERROR_INVALID_ITER,
                   "Iterator is not associated with any buffer");
        return FALSE;
    }

    if (gtk_text_iter_get_buffer(iter) != buffer) {
        g_set_error(error, GTKTEXT_RENDER_ERROR, GTKTEXT_RENDER_ERROR_INVALID_ITER,
                   "Iterator is not associated with the specified buffer");
        return FALSE;
    }

    /* Check if iterator is within buffer bounds */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);

    if (gtk_text_iter_compare(iter, &start) < 0 || gtk_text_iter_compare(iter, &end) > 0) {
        g_set_error(error, GTKTEXT_RENDER_ERROR, GTKTEXT_RENDER_ERROR_INVALID_ITER,
                   "Iterator is outside buffer bounds");
        return FALSE;
    }

    return TRUE;
}

gboolean safe_iter_range_is_valid(GtkTextBuffer *buffer,
                                  const GtkTextIter *start,
                                  const GtkTextIter *end,
                                  GError **error)
{
    if (!safe_iter_is_valid(buffer, start, error)) {
        return FALSE;
    }

    if (!safe_iter_is_valid(buffer, end, error)) {
        return FALSE;
    }

    if (gtk_text_iter_compare(start, end) > 0) {
        g_set_error(error, GTKTEXT_RENDER_ERROR, GTKTEXT_RENDER_ERROR_INVALID_ITER,
                   "Start iterator is after end iterator");
        return FALSE;
    }

    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * SAFE TEXT OPERATIONS - Boundary-checked text operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

SafeRenderResult safe_buffer_insert_text(GtkTextBuffer *buffer,
                                         GtkTextIter *iter,
                                         const gchar *text,
                                         gssize length_limit)
{
    GError *error = NULL;

    /* Validate inputs */
    if (!safe_buffer_is_valid(buffer, &error)) {
        SafeRenderResult result = {.success = FALSE, .error = error};
        return result;
    }

    if (!safe_iter_is_valid(buffer, iter, &error)) {
        SafeRenderResult result = {.success = FALSE, .error = error};
        return result;
    }

    if (!text) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_INVALID_BUFFER,
                                       "Text is NULL");
    }

    /* Check text length limits */
    gsize text_length = strlen(text);
    if (length_limit > 0 && text_length > (gsize)length_limit) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_TEXT_TOO_LARGE,
                                       "Text length (%zu) exceeds limit (%zd)",
                                       text_length, length_limit);
    }

    if (text_length > SAFE_TEXT_MAX_LENGTH) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_TEXT_TOO_LARGE,
                                       "Text length (%zu) exceeds maximum safe limit (%d)",
                                       text_length, SAFE_TEXT_MAX_LENGTH);
    }

    /* Validate UTF-8 */
    if (!g_utf8_validate(text, -1, NULL)) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_INVALID_BUFFER,
                                       "Text is not valid UTF-8");
    }

    /* Perform safe insertion */
    gtk_text_buffer_insert(buffer, iter, text, -1);

    return safe_render_result_success();
}

SafeRenderResult safe_buffer_insert_text_with_tags(GtkTextBuffer *buffer,
                                                   GtkTextIter *iter,
                                                   const gchar *text,
                                                   gssize length_limit,
                                                   GSList *tag_names)
{
    /* First validate the basic insertion */
    SafeRenderResult basic_result = safe_buffer_insert_text(buffer, iter, text, length_limit);
    if (!basic_result.success) {
        return basic_result;
    }

    /* If no tags, we're done */
    if (!tag_names) {
        return safe_render_result_success();
    }

    /* Create mark to track insertion start */
    GtkTextMark *start_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
    if (!start_mark) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_INVALID_BUFFER,
                                       "Failed to create text mark");
    }

    /* Insert text (iter now points to end of inserted text) */
    gtk_text_buffer_insert(buffer, iter, text, -1);

    /* Get start position and apply tags */
    GtkTextIter start_iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &start_iter, start_mark);

    for (GSList *l = tag_names; l; l = l->next) {
        const gchar *tag_name = (const gchar *)l->data;
        if (tag_name) {
            SafeRenderResult tag_result = safe_buffer_apply_tag_by_name(buffer, tag_name, &start_iter, iter);
            if (!tag_result.success) {
                gtk_text_buffer_delete_mark(buffer, start_mark);
                return tag_result;
            }
        }
    }

    gtk_text_buffer_delete_mark(buffer, start_mark);
    return safe_render_result_success();
}

SafeRenderResult safe_buffer_delete_range(GtkTextBuffer *buffer,
                                          GtkTextIter *start,
                                          GtkTextIter *end)
{
    GError *error = NULL;

    if (!safe_iter_range_is_valid(buffer, start, end, &error)) {
        SafeRenderResult result = {.success = FALSE, .error = error};
        return result;
    }

    gtk_text_buffer_delete(buffer, start, end);
    return safe_render_result_success();
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * SAFE TAG OPERATIONS - Validated tag creation and application
 * ═══════════════════════════════════════════════════════════════════════════════ */

SafeRenderResult safe_buffer_get_or_create_tag(GtkTextBuffer *buffer,
                                               const gchar *tag_name,
                                               GtkTextTag **out_tag)
{
    GError *error = NULL;

    if (!safe_buffer_is_valid(buffer, &error)) {
        SafeRenderResult result = {.success = FALSE, .error = error};
        return result;
    }

    if (!tag_name || !*tag_name) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_TAG_CREATION,
                                       "Tag name is NULL or empty");
    }

    if (!out_tag) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_TAG_CREATION,
                                       "Output tag pointer is NULL");
    }

    /* Try to get existing tag */
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *tag = gtk_text_tag_table_lookup(tag_table, tag_name);

    if (!tag) {
        /* Create new tag */
        tag = gtk_text_buffer_create_tag(buffer, tag_name, NULL);
        if (!tag) {
            return safe_render_result_error(GTKTEXT_RENDER_ERROR_TAG_CREATION,
                                           "Failed to create tag '%s'", tag_name);
        }
    }

    *out_tag = tag;
    return safe_render_result_success();
}

SafeRenderResult safe_buffer_apply_tag(GtkTextBuffer *buffer,
                                       GtkTextTag *tag,
                                       const GtkTextIter *start,
                                       const GtkTextIter *end)
{
    GError *error = NULL;

    if (!safe_buffer_is_valid(buffer, &error)) {
        SafeRenderResult result = {.success = FALSE, .error = error};
        return result;
    }

    if (!tag || !GTK_IS_TEXT_TAG(tag)) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_TAG_CREATION,
                                       "Tag is NULL or invalid");
    }

    if (!safe_iter_range_is_valid(buffer, start, end, &error)) {
        SafeRenderResult result = {.success = FALSE, .error = error};
        return result;
    }

    gtk_text_buffer_apply_tag(buffer, tag, start, end);
    return safe_render_result_success();
}

SafeRenderResult safe_buffer_apply_tag_by_name(GtkTextBuffer *buffer,
                                               const gchar *tag_name,
                                               const GtkTextIter *start,
                                               const GtkTextIter *end)
{
    GtkTextTag *tag = NULL;
    SafeRenderResult tag_result = safe_buffer_get_or_create_tag(buffer, tag_name, &tag);
    if (!tag_result.success) {
        return tag_result;
    }

    return safe_buffer_apply_tag(buffer, tag, start, end);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * SAFE WIDGET OPERATIONS - Validated widget anchor management
 * ═══════════════════════════════════════════════════════════════════════════════ */

SafeRenderResult safe_buffer_create_widget_anchor(GtkTextBuffer *buffer,
                                                  GtkTextIter *iter,
                                                  GtkWidget *widget,
                                                  GtkTextChildAnchor **out_anchor)
{
    GError *error = NULL;

    if (!safe_buffer_is_valid(buffer, &error)) {
        SafeRenderResult result = {.success = FALSE, .error = error};
        return result;
    }

    if (!safe_iter_is_valid(buffer, iter, &error)) {
        SafeRenderResult result = {.success = FALSE, .error = error};
        return result;
    }

    if (!widget || !GTK_IS_WIDGET(widget)) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_WIDGET_ANCHOR,
                                       "Widget is NULL or invalid");
    }

    if (!out_anchor) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_WIDGET_ANCHOR,
                                       "Output anchor pointer is NULL");
    }

    /* Create the anchor */
    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buffer, iter);
    if (!anchor) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_WIDGET_ANCHOR,
                                       "Failed to create child anchor");
    }

    *out_anchor = anchor;
    return safe_render_result_success();
}

SafeRenderResult safe_anchor_set_widget_data(GtkTextChildAnchor *anchor,
                                             const gchar *key,
                                             GtkWidget *widget)
{
    if (!anchor || !GTK_IS_TEXT_CHILD_ANCHOR(anchor)) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_WIDGET_ANCHOR,
                                       "Anchor is NULL or invalid");
    }

    if (!key || !*key) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_WIDGET_ANCHOR,
                                       "Key is NULL or empty");
    }

    if (!widget || !GTK_IS_WIDGET(widget)) {
        return safe_render_result_error(GTKTEXT_RENDER_ERROR_WIDGET_ANCHOR,
                                       "Widget is NULL or invalid");
    }

    /* Safely manage widget reference */
    g_object_ref_sink(widget);
    g_object_set_data_full(G_OBJECT(anchor), key, widget, (GDestroyNotify)g_object_unref);

    return safe_render_result_success();
}