/* C ULTRA-MIN TEMPLATE
   Purpose: Clean document state management with hash-based dirty tracking
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-20 - document/doc_state.h
   Added: Hash-based document state tracking for deterministic clean/dirty detection
*/

#ifndef GTKTEXT_DOC_STATE_H
#define GTKTEXT_DOC_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Core state structure and types
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * DocState: Single source of truth for document clean/dirty state
 *
 * Uses content hashing to deterministically track whether the current
 * buffer content matches the last saved snapshot. View-agnostic.
 */
typedef struct {
    void *buffer;                     /* GtkTextBuffer handle */
    uint64_t saved_hash;              /* Hash of last-saved content */
    uint64_t current_hash;            /* Hash of current buffer content */
    bool is_dirty;                    /* Computed state: current != saved */
} DocState;

/* ═══════════════════════════════════════════════════════════════════════════════
 * CORE API - State management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * doc_state_init: Initialize state tracker for a buffer
 * @ds: DocState structure to initialize
 * @buffer: GtkTextBuffer to track (stored as void* for flexibility)
 */
void doc_state_init(DocState *ds, void *buffer);

/**
 * doc_mark_loaded_or_new: Mark document as clean after load/create
 * @ds: DocState to update
 *
 * Call after:
 * - Loading existing file into buffer
 * - Creating new document
 * - Reloading from disk
 *
 * Sets saved_hash = current_hash, is_dirty = false
 */
void doc_mark_loaded_or_new(DocState *ds);

/**
 * doc_on_user_mutation: Handle user content changes
 * @ds: DocState to update
 *
 * Call after any user action that changes document content:
 * - Typing, paste, delete
 * - Applying formatting that changes underlying markup
 * - Undo/redo operations
 *
 * Recomputes current_hash and updates is_dirty state
 */
void doc_on_user_mutation(DocState *ds);

/**
 * doc_on_saved: Mark document as saved
 * @ds: DocState to update
 *
 * Call after successful save to storage.
 * Sets saved_hash = current_hash, is_dirty = false
 */
void doc_on_saved(DocState *ds);

/**
 * doc_is_dirty: Check if document has unsaved changes
 * @ds: DocState to query
 * @return: true if document is dirty, false if clean
 */
bool doc_is_dirty(const DocState *ds);

/**
 * doc_recompute_state_from_buffer: Recompute state from live buffer
 * @ds: DocState to update
 *
 * For undo/redo and other operations that might restore content
 * to exact saved state. Recomputes current_hash from buffer and
 * updates is_dirty = (current_hash != saved_hash)
 */
void doc_recompute_state_from_buffer(DocState *ds);

/* ═══════════════════════════════════════════════════════════════════════════════
 * HASH UTILITIES - Content hashing functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * doc_compute_hash: Compute FNV-1a hash of buffer content
 * @buffer: GtkTextBuffer to hash
 * @return: 64-bit hash of canonical buffer content
 *
 * Creates canonical representation that's identical for logically
 * equivalent content regardless of view (WYSIWYG vs Source).
 */
uint64_t doc_compute_hash(void *buffer);

/**
 * doc_compute_text_hash: Compute FNV-1a hash of text
 * @text: Text content to hash
 * @len: Length of text (or 0 to auto-detect with strlen)
 * @return: 64-bit FNV-1a hash
 */
uint64_t doc_compute_text_hash(const char *text, size_t len);

G_END_DECLS

#endif /* GTKTEXT_DOC_STATE_H */