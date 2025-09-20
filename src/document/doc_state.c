/* C ULTRA-MIN TEMPLATE
   Purpose: Clean document state management with hash-based dirty tracking
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-20 - document/doc_state.c
   Added: Hash-based document state tracking for deterministic clean/dirty detection
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtktext/document/doc_state.h>
#include <gtktext/render/cmrender.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* FNV-1a hash constants (64-bit) */
#define FNV_OFFSET_BASIS_64 14695981039346656037ULL
#define FNV_PRIME_64        1099511628211ULL

/**
 * fnv1a_hash: Fast FNV-1a hash implementation
 * @data: Data to hash
 * @len: Length of data
 * @return: 64-bit FNV-1a hash
 */
static uint64_t fnv1a_hash(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t hash = FNV_OFFSET_BASIS_64;

    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME_64;
    }

    return hash;
}

/**
 * get_canonical_buffer_content: Get canonical text representation
 * @buffer: GtkTextBuffer to extract from
 * @return: Canonical text content (caller must g_free)
 *
 * Returns the markdown representation of the buffer content.
 * This ensures WYSIWYG and Source views produce identical hashes
 * for logically equivalent content.
 */
static char *get_canonical_buffer_content(GtkTextBuffer *buffer)
{
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), g_strdup(""));

    /* Use the existing markdown export function for canonical representation */
    return cm_render_buffer_to_markdown(buffer);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * CORE API - Public state management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void doc_state_init(DocState *ds, void *buffer)
{
    g_return_if_fail(ds != NULL);
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));

    ds->buffer = buffer;
    ds->saved_hash = 0;
    ds->current_hash = 0;
    ds->is_dirty = false;

}

void doc_mark_loaded_or_new(DocState *ds)
{
    g_return_if_fail(ds != NULL);
    g_return_if_fail(GTK_IS_TEXT_BUFFER(ds->buffer));

    /* Compute hash of current buffer content */
    uint64_t hash = doc_compute_hash(ds->buffer);

    /* Mark as clean - current content is the "saved" baseline */
    ds->saved_hash = hash;
    ds->current_hash = hash;
    ds->is_dirty = false;

}

void doc_on_user_mutation(DocState *ds)
{
    g_return_if_fail(ds != NULL);
    g_return_if_fail(GTK_IS_TEXT_BUFFER(ds->buffer));

    /* Recompute current hash */
    uint64_t new_hash = doc_compute_hash(ds->buffer);
    ds->current_hash = new_hash;

    /* Update dirty state */
    ds->is_dirty = (ds->current_hash != ds->saved_hash);

}

void doc_on_saved(DocState *ds)
{
    g_return_if_fail(ds != NULL);
    g_return_if_fail(GTK_IS_TEXT_BUFFER(ds->buffer));

    /* Current content is now the saved baseline */
    ds->current_hash = doc_compute_hash(ds->buffer);
    ds->saved_hash = ds->current_hash;
    ds->is_dirty = false;

}

bool doc_is_dirty(const DocState *ds)
{
    g_return_val_if_fail(ds != NULL, false);
    return ds->is_dirty;
}

void doc_recompute_state_from_buffer(DocState *ds)
{
    g_return_if_fail(ds != NULL);
    g_return_if_fail(GTK_IS_TEXT_BUFFER(ds->buffer));

    /* Recompute current hash from live buffer */
    uint64_t new_hash = doc_compute_hash(ds->buffer);
    ds->current_hash = new_hash;

    /* Update dirty state based on comparison with saved */
    ds->is_dirty = (ds->current_hash != ds->saved_hash);

}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HASH UTILITIES - Content hashing functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

uint64_t doc_compute_hash(void *buffer)
{
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), 0);

    /* Get canonical content representation */
    g_autofree char *content = get_canonical_buffer_content(GTK_TEXT_BUFFER(buffer));
    if (!content) {
        return fnv1a_hash("", 0);  /* Empty content hash */
    }

    /* Compute hash of canonical content */
    size_t len = strlen(content);
    uint64_t hash = fnv1a_hash(content, len);
    return hash;
}

uint64_t doc_compute_text_hash(const char *text, size_t len)
{
    if (!text) {
        return fnv1a_hash("", 0);
    }

    if (len == 0) {
        len = strlen(text);
    }

    return fnv1a_hash(text, len);
}