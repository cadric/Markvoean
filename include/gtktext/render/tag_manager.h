/* C ULTRA-MIN TEMPLATE
   Purpose: Text tag creation and management for markdown rendering
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - render/tag_manager.h
   Changed: Extracted tag management from cmrender.c for better organization
*/

#pragma once

#ifndef GTKTEXT_RENDER_TAG_MANAGER_H
#define GTKTEXT_RENDER_TAG_MANAGER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Tag management structures
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    GHashTable *map; // key: char* (tag name), value: GtkTextTag*
} TagCache;

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Tag management functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Get tag name safely from tag object
 * @param tag Text tag to get name from
 * @return Tag name or NULL if not found
 */
const char* tag_manager_get_tag_name_safe(GtkTextTag *tag);

/**
 * Get or create a base tag with given name
 * @param buffer Text buffer to create tag in
 * @param tag_name Name of the tag to get or create
 * @return Text tag instance
 */
GtkTextTag* tag_manager_get_or_create_base_tag(GtkTextBuffer *buffer, const char *tag_name);

/**
 * Insert text with active tags applied
 * @param buffer Text buffer to insert into
 * @param iter Iterator position for insertion
 * @param text Text to insert
 * @param active_tags List of active tags to apply
 */
void tag_manager_insert_with_active_tags(GtkTextBuffer *buffer, GtkTextIter *iter,
                                         const char *text, GSList *active_tags);

/**
 * Free tag cache structure
 * @param p Pointer to TagCache to free
 */
void tag_manager_cache_free(gpointer p);

G_END_DECLS

#endif /* GTKTEXT_RENDER_TAG_MANAGER_H */
