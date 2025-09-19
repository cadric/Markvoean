/* C ULTRA-MIN TEMPLATE
   Purpose: Text tag creation and management for markdown rendering
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - render/markdown/tag_manager.c
   Changed: Extracted tag management from cmrender.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <pango/pango.h>
#include <glib.h>

#include <gtktext/render/tag_manager.h>
#include <gtktext/render/theme_styles.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Global state for tag management
 * ═══════════════════════════════════════════════════════════════════════════════ */

// Fast qdata keys for tag name and tag cache
static GQuark quark_tag_name = 0;
static GQuark quark_tag_cache = 0;

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Tag management utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Free tag cache structure
 */
void tag_manager_cache_free(gpointer p)
{
    TagCache *cache = (TagCache*)p;
    if (cache) {
        if (cache->map) g_hash_table_destroy(cache->map);
        g_free(cache);
    }
}

/**
 * Get tag name safely from tag object
 */
const char* tag_manager_get_tag_name_safe(GtkTextTag *tag)
{
    if (!tag) return NULL;

    if (G_UNLIKELY(quark_tag_name == 0))
        quark_tag_name = g_quark_from_static_string("tag-name");

    const char *name = g_object_get_qdata(G_OBJECT(tag), quark_tag_name);
    if (name && *name) return name;

    gchar *prop_name = NULL;
    g_object_get(tag, "name", &prop_name, NULL);
    if (prop_name && *prop_name) {
        g_object_set_qdata_full(G_OBJECT(tag), quark_tag_name, prop_name, g_free);
        return prop_name;
    }

    g_free(prop_name);
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Tag creation and management
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Get or create a base tag with given name
 */
GtkTextTag* tag_manager_get_or_create_base_tag(GtkTextBuffer *buffer, const char *tag_name)
{
    if (!buffer || !tag_name || !*tag_name) return NULL;

    if (G_UNLIKELY(quark_tag_cache == 0))
        quark_tag_cache = g_quark_from_static_string("cm-tag-cache");

    TagCache *cache = g_object_get_qdata(G_OBJECT(buffer), quark_tag_cache);
    if (!cache) {
        cache = g_new0(TagCache, 1);
        cache->map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        g_object_set_qdata_full(G_OBJECT(buffer), quark_tag_cache, cache, tag_manager_cache_free);
    }

    // 1) Try cache first
    GtkTextTag *tag = g_hash_table_lookup(cache->map, tag_name);
    if (tag) return tag;

    // 2) Lookup in tag table
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    tag = gtk_text_tag_table_lookup(tag_table, tag_name);

    // 3) Create when missing
    if (!tag) {
        if (g_strcmp0(tag_name, "bold") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
        } else if (g_strcmp0(tag_name, "italic") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
        } else if (g_str_has_prefix(tag_name, "h") && g_utf8_strlen(tag_name, -1) == 2 && tag_name[1] >= '1' && tag_name[1] <= '6') {
            int level = tag_name[1] - '0';
            double scale = 1.0;
            switch (level) {
                case 1: scale = PANGO_SCALE_XX_LARGE; break;
                case 2: scale = PANGO_SCALE_X_LARGE; break;
                case 3: scale = PANGO_SCALE_LARGE; break;
                case 4: scale = PANGO_SCALE_MEDIUM; break;
                case 5: scale = PANGO_SCALE_SMALL; break;
                case 6: scale = PANGO_SCALE_X_SMALL; break;
            }
            tag = gtk_text_buffer_create_tag(buffer, tag_name,
                                             "weight", PANGO_WEIGHT_BOLD,
                                             "scale", scale,
                                             NULL);
        } else if (g_strcmp0(tag_name, "code") == 0) {
            // Basic properties for inline code - should align with normal text (0px left margin)
            tag = gtk_text_buffer_create_tag(buffer, "code",
                                             "family", "monospace",
                                             "background-full-height", TRUE,
                                             "left-margin", 0,
                                             "right-margin", 0,
                                             "pixels-above-lines", 1,
                                             "pixels-below-lines", 1,
                                             NULL);
        } else if (g_strcmp0(tag_name, "codeblock") == 0) {
            // Basic properties for fenced code blocks — consistent 32px indentation
            tag = gtk_text_buffer_create_tag(buffer, "codeblock",
                                             "family", "monospace",
                                             "background-full-height", FALSE,
                                             "left-margin", 32,
                                             "right-margin", 8,
                                             "pixels-above-lines", 6,
                                             "pixels-below-lines", 6,
                                             "wrap-mode", GTK_WRAP_NONE,
                                             "indent", 0,
                                             NULL);
        } else if (g_strcmp0(tag_name, "codeblock_indented") == 0) {
            // Indented code blocks should visually represent the 4+ space indentation (48px)
            tag = gtk_text_buffer_create_tag(buffer, "codeblock_indented",
                                             "family", "monospace",
                                             "background-full-height", FALSE,
                                             "left-margin", 48,
                                             "right-margin", 8,
                                             "pixels-above-lines", 6,
                                             "pixels-below-lines", 6,
                                             "wrap-mode", GTK_WRAP_NONE,
                                             "indent", 0,
                                             NULL);
        } else if (g_strcmp0(tag_name, "hr") == 0) {
            // Horizontal rule: create a full-width line effect
            tag = gtk_text_buffer_create_tag(buffer, "hr",
                                             "pixels-above-lines", 12,
                                             "pixels-below-lines", 12,
                                             "justification", GTK_JUSTIFY_LEFT,
                                             "foreground-rgba", NULL, // Will be set by theme update
                                             NULL);
        } else if (g_str_has_prefix(tag_name, "blockquote")) {
            // Support nested blockquotes by creating tags named "blockquote1", "blockquote2", ...
            int depth = 1;
            const char *p = tag_name + 10; // strlen("blockquote")
            if (*p >= '1' && *p <= '9') {
                depth = *p - '0';
            }
            int left_margin = 16 * depth; // 16 pixels per nesting level for consistent hierarchy
            tag = gtk_text_buffer_create_tag(buffer, tag_name,
                                             "left-margin", left_margin,
                                             NULL);
        }
    }

    // 4) Cache the result
    if (tag) {
        g_hash_table_insert(cache->map, g_strdup(tag_name), tag);

        /* Apply theme colors immediately after creating any tag */
        theme_styles_update_theme_dependent_tags(buffer);
    }

    return tag;
}

/**
 * Insert text with active tags applied
 */
void tag_manager_insert_with_active_tags(GtkTextBuffer *buffer, GtkTextIter *iter,
                                         const char *text, GSList *active_tags)
{
    if (!text || !*text) return;

    if (!active_tags) {
        gtk_text_buffer_insert(buffer, iter, text, -1);
        return;
    }

    // Insert text with all active tags
    GtkTextMark *start_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
    gtk_text_buffer_insert(buffer, iter, text, -1);

    GtkTextIter start_iter;
    gtk_text_buffer_get_iter_at_mark(buffer, &start_iter, start_mark);

    for (GSList *l = active_tags; l; l = l->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(l->data);
        if (tag) {
            gtk_text_buffer_apply_tag(buffer, tag, &start_iter, iter);
        }
    }

    gtk_text_buffer_delete_mark(buffer, start_mark);
}