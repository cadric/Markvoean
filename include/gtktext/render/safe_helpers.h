/* Safe Rendering Helpers Header
 * Purpose: Type-safe and error-checked helpers for GTK text buffer rendering operations
 * [1.0.0] - 2025-09-19 - render/safe_helpers.h
 * Phase 2: Enhanced safety for async rendering pipeline
 */

#ifndef GTKTEXT_RENDER_SAFE_HELPERS_H
#define GTKTEXT_RENDER_SAFE_HELPERS_H

#include <gtk/gtk.h>
#include <glib.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Error domains and result types
 * ═══════════════════════════════════════════════════════════════════════════════ */

#define GTKTEXT_RENDER_ERROR (gtktext_render_error_quark())

typedef enum {
    GTKTEXT_RENDER_ERROR_INVALID_BUFFER,    /* Buffer is NULL or invalid */
    GTKTEXT_RENDER_ERROR_INVALID_ITER,      /* Iterator is invalid or out of bounds */
    GTKTEXT_RENDER_ERROR_TAG_CREATION,      /* Failed to create or retrieve tag */
    GTKTEXT_RENDER_ERROR_WIDGET_ANCHOR,     /* Widget anchor creation failed */
    GTKTEXT_RENDER_ERROR_TEXT_TOO_LARGE     /* Text exceeds safe insertion limits */
} GtktextRenderError;

GQuark gtktext_render_error_quark(void);

/* Safe operation result with optional error information */
typedef struct {
    gboolean success;
    GError *error;  /* Optional error details - caller must free */
} SafeRenderResult;

/* ═══════════════════════════════════════════════════════════════════════════════
 * BUFFER VALIDATION - Safe buffer state checking
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Validate buffer is safe for operations */
gboolean safe_buffer_is_valid(GtkTextBuffer *buffer, GError **error);

/* Validate iterator is within buffer bounds */
gboolean safe_iter_is_valid(GtkTextBuffer *buffer, const GtkTextIter *iter, GError **error);

/* Validate iterator range is safe */
gboolean safe_iter_range_is_valid(GtkTextBuffer *buffer,
                                  const GtkTextIter *start,
                                  const GtkTextIter *end,
                                  GError **error);

/* ═══════════════════════════════════════════════════════════════════════════════
 * SAFE TEXT OPERATIONS - Boundary-checked text insertion and deletion
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Safe text insertion with length limits and validation */
SafeRenderResult safe_buffer_insert_text(GtkTextBuffer *buffer,
                                         GtkTextIter *iter,
                                         const gchar *text,
                                         gssize length_limit);

/* Safe text insertion with tag application */
SafeRenderResult safe_buffer_insert_text_with_tags(GtkTextBuffer *buffer,
                                                   GtkTextIter *iter,
                                                   const gchar *text,
                                                   gssize length_limit,
                                                   GSList *tag_names);

/* Safe text deletion with bounds checking */
SafeRenderResult safe_buffer_delete_range(GtkTextBuffer *buffer,
                                          GtkTextIter *start,
                                          GtkTextIter *end);

/* ═══════════════════════════════════════════════════════════════════════════════
 * SAFE TAG OPERATIONS - Validated tag creation and application
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Safe tag creation with validation */
SafeRenderResult safe_buffer_create_tag(GtkTextBuffer *buffer,
                                        const gchar *tag_name,
                                        GtkTextTag **out_tag,
                                        const gchar *first_property_name,
                                        ...);

/* Safe tag retrieval with fallback creation */
SafeRenderResult safe_buffer_get_or_create_tag(GtkTextBuffer *buffer,
                                               const gchar *tag_name,
                                               GtkTextTag **out_tag);

/* Safe tag application with bounds checking */
SafeRenderResult safe_buffer_apply_tag(GtkTextBuffer *buffer,
                                       GtkTextTag *tag,
                                       const GtkTextIter *start,
                                       const GtkTextIter *end);

/* Safe tag application by name */
SafeRenderResult safe_buffer_apply_tag_by_name(GtkTextBuffer *buffer,
                                               const gchar *tag_name,
                                               const GtkTextIter *start,
                                               const GtkTextIter *end);

/* ═══════════════════════════════════════════════════════════════════════════════
 * SAFE WIDGET OPERATIONS - Validated widget anchor management
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Safe widget anchor creation with validation */
SafeRenderResult safe_buffer_create_widget_anchor(GtkTextBuffer *buffer,
                                                  GtkTextIter *iter,
                                                  GtkWidget *widget,
                                                  GtkTextChildAnchor **out_anchor);

/* Safe widget reference management for anchors */
SafeRenderResult safe_anchor_set_widget_data(GtkTextChildAnchor *anchor,
                                             const gchar *key,
                                             GtkWidget *widget);

/* ═══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS - Helper utilities for safe operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Create a safe render result */
SafeRenderResult safe_render_result_success(void);
SafeRenderResult safe_render_result_error(GtktextRenderError error_code,
                                         const gchar *format,
                                         ...) G_GNUC_PRINTF(2, 3);

/* Free a safe render result (clears error if present) */
void safe_render_result_clear(SafeRenderResult *result);

/* Maximum safe text length for single operations */
#define SAFE_TEXT_MAX_LENGTH (1024 * 1024)  /* 1MB limit */

G_END_DECLS

#endif /* GTKTEXT_RENDER_SAFE_HELPERS_H */