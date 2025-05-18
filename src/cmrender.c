#include "cmrender.h"
#include "gtktext_cmark.h" // Assuming this might contain shared definitions or can be removed if not.
                           // If gtktext_cmark.h is specific to the old cmark.c, review its necessity here.
#include <adwaita.h> // For AdwStyleManager
#include <string.h>
#include <stdio.h>

// Macro to silence unused variable warnings (if needed, or manage via compiler flags)
#define CMRENDER_UNUSED __attribute__((unused))

// Forward declaration for the recursive helper
static void cm_render_node_recursive(cmark_node *node, GtkTextBuffer *buffer, GtkTextIter *iter, GSList *active_tags);
static void cm_render_insert_with_active_tags(GtkTextBuffer *buffer, GtkTextIter *iter, const char *text, GSList *active_tags);


/**
 * @brief Gets an existing GtkTextTag or creates it with basic, non-theme-dependent properties.
 *
 * Theme-dependent properties (like background/foreground for code blocks)
 * are handled by cm_render_update_theme_dependent_tags().
 *
 * @param buffer The GtkTextBuffer.
 * @param tag_name The name of the tag.
 * @return The GtkTextTag, or NULL if creation failed for an unknown tag type.
 */
static GtkTextTag* cm_render_get_or_create_base_tag(GtkTextBuffer *buffer, const char *tag_name) {
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *tag = gtk_text_tag_table_lookup(tag_table, tag_name);

    if (!tag) {
        if (strcmp(tag_name, "bold") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
        } else if (strcmp(tag_name, "italic") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
        } else if (strncmp(tag_name, "h", 1) == 0 && strlen(tag_name) == 2 && tag_name[1] >= '1' && tag_name[1] <= '6') {
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
        } else if (strcmp(tag_name, "code") == 0) {
            // Basic properties for inline code
            tag = gtk_text_buffer_create_tag(buffer, "code",
                                             "family", "monospace",
                                             "background-full-height", TRUE,
                                             "left-margin", 4,
                                             "right-margin", 4,
                                             "pixels-above-lines", 1,
                                             "pixels-below-lines", 1,
                                             NULL);
        } else if (strcmp(tag_name, "codeblock") == 0) {
            // Basic properties for code blocks
            tag = gtk_text_buffer_create_tag(buffer, "codeblock",
                                             "family", "monospace",
                                             "left-margin", 12,
                                             "right-margin", 12,
                                             "pixels-above-lines", 6,
                                             "pixels-below-lines", 6,
                                             "wrap-mode", GTK_WRAP_NONE, // Code blocks typically don't wrap
                                             NULL);
        } else if (strcmp(tag_name, "hr") == 0) {
            // For hr, we might insert a visual separator or use paragraph styling.
            // Here, just creating a tag that could be used to style a paragraph containing "---"
            // or for custom drawing if GtkTextView is subclassed.
            tag = gtk_text_buffer_create_tag(buffer, "hr",
                                             "pixels-above-lines", 8,
                                             "pixels-below-lines", 8,
                                             // "underline", PANGO_UNDERLINE_SINGLE, // Example: could underline text
                                             // "strikethrough", TRUE, // Example
                                             NULL);
        } else {
            g_warning("cm_render_get_or_create_base_tag: Unknown tag name '%s'", tag_name);
        }
    }
    return tag;
}

void cm_render_update_theme_dependent_tags(GtkTextBuffer *buffer) {
    if (!buffer) return;

    AdwStyleManager *style_manager = adw_style_manager_get_default();
    AdwColorScheme color_scheme = adw_style_manager_get_color_scheme(style_manager);
    gboolean is_dark = (color_scheme == ADW_COLOR_SCHEME_FORCE_DARK || color_scheme == ADW_COLOR_SCHEME_PREFER_DARK);

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    
    // Ensure 'code' tag exists or create it before setting theme properties
    GtkTextTag *code_tag = cm_render_get_or_create_base_tag(buffer, "code");
    // Ensure 'codeblock' tag exists or create it
    GtkTextTag *codeblock_tag = cm_render_get_or_create_base_tag(buffer, "codeblock");


    const char* code_fg_color = is_dark ? "#e0e0e0" : NULL; 
    const char* code_bg_color = is_dark ? "rgba(50,50,50,0.7)" : "rgba(241,241,241,0.7)"; // Slightly transparent
    const char* codeblock_fg_color = is_dark ? "#e0e0e0" : NULL; 
    const char* codeblock_bg_color = is_dark ? "#282c34" : "#f6f8fa"; // Common editor theme colors

    if (code_tag) {
        g_object_set(code_tag,
                     "background", code_bg_color,
                     "foreground", code_fg_color,
                     NULL);
    } else {
        g_warning("Failed to get or create 'code' tag during theme update.");
    }

    if (codeblock_tag) {
        g_object_set(codeblock_tag,
                     "background", codeblock_bg_color, // Background for the text itself
                     "paragraph-background", codeblock_bg_color, // Background for the entire paragraph block
                     "foreground", codeblock_fg_color,
                     NULL);
    } else {
         g_warning("Failed to get or create 'codeblock' tag during theme update.");
    }
}

static void cm_render_insert_with_active_tags(GtkTextBuffer *buffer, GtkTextIter *iter, const char *text, GSList *active_tags) {
    if (!text || strlen(text) == 0) return;

    GtkTextMark *start_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE); // Left gravity
    gtk_text_buffer_insert(buffer, iter, text, -1); // iter moves to end of inserted text

    if (active_tags) {
        GtkTextIter start_insert_iter;
        gtk_text_buffer_get_iter_at_mark(buffer, &start_insert_iter, start_mark);
        GSList *l;
        for (l = active_tags; l != NULL; l = l->next) {
            const char* tag_name = (const char*)l->data;
            if (tag_name) {
                // Use the base tag creator, theme properties are applied globally later
                GtkTextTag *tag_object = cm_render_get_or_create_base_tag(buffer, tag_name);
                if (tag_object) {
                    gtk_text_buffer_apply_tag(buffer, tag_object, &start_insert_iter, iter);
                } else {
                    g_warning("cm_render_insert_with_active_tags: Failed to get or create tag: %s", tag_name);
                }
            }
        }
    }
    gtk_text_buffer_delete_mark(buffer, start_mark);
}

// Recursive function to apply tags 
static void cm_render_node_recursive(cmark_node *node, GtkTextBuffer *buffer, GtkTextIter *iter, GSList *active_tags) {
    if (!node) return;

    cmark_node *child;
    for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
        cmark_node_type type = cmark_node_get_type(child);
        GSList *current_tags = active_tags; // By default, pass parent's active tags
        gboolean pushed_tag_on_current_tags = FALSE;
        char *tag_name_alloc = NULL; // For dynamically created tag names like "h1"

        // --- PORT THE SWITCH-CASE LOGIC FROM cmark.c:apply_tags_for_node_recursive HERE ---
        // Remember to adapt calls to get_or_create_tag to cm_render_get_or_create_base_tag
        // and insert_with_active_tags to cm_render_insert_with_active_tags.
        
        // Example for CMARK_NODE_TEXT:
        switch (type) {
            case CMARK_NODE_TEXT:
            {
                const char *literal = cmark_node_get_literal(child);
                if (literal) {
                    cm_render_insert_with_active_tags(buffer, iter, literal, current_tags);
                }
                break;
            }
            case CMARK_NODE_CODE: // Inline code
            {
                const char *literal = cmark_node_get_literal(child);
                if (literal) {
                    // Prepend "code" tag for this segment. Copy the list to avoid modifying parent's.
                    GSList *tags_for_this_segment = g_slist_prepend(g_slist_copy(active_tags), (gpointer)"code");
                    cm_render_insert_with_active_tags(buffer, iter, literal, tags_for_this_segment);
                    g_slist_free_full(tags_for_this_segment, NULL); // Free the copied list structure (not the string "code")
                }
                break;
            }
            // ... (other cases: STRONG, EMPH, HEADING, PARAGRAPH, LIST, ITEM, CODE_BLOCK, THEMATIC_BREAK, etc.)
            // ... (LINEBREAK, SOFTBREAK)

            default:
                // Recursively call for children of unknown or container types
                cm_render_node_recursive(child, buffer, iter, current_tags); // Pass current_tags
                break;
        }
        // --- END OF PORTED SWITCH-CASE LOGIC ---

        if (pushed_tag_on_current_tags) {
            // If current_tags was locally modified (e.g. by prepending a tag for the current node type
            // and its children), it needs to be cleaned up here.
            // Example: current_tags = g_slist_remove(current_tags, current_tags->data);
            // This depends on how current_tags is managed within the switch cases.
            // The provided example for CMARK_NODE_CODE handles its own list copy.
        }
        g_free(tag_name_alloc); // Free dynamically allocated tag names (e.g. "h1")
    }
}


gboolean cm_render_markdown_to_buffer(GtkTextBuffer *buffer, const char *markdown_text) {
    if (!buffer || !markdown_text) {
        g_warning("cm_render_markdown_to_buffer: Invalid arguments.");
        return FALSE;
    }

    // 1. Clear the buffer
    GtkTextIter start_clear, end_clear;
    gtk_text_buffer_get_bounds(buffer, &start_clear, &end_clear);
    gtk_text_buffer_delete(buffer, &start_clear, &end_clear);

    // 2. Ensure basic non-theme dependent tags are available.
    //    These will be created by cm_render_get_or_create_base_tag as needed when encountered,
    //    but pre-creating common ones here can be a slight conceptual clarity.
    //    Theme properties are applied *after* all rendering.
    cm_render_get_or_create_base_tag(buffer, "bold");
    cm_render_get_or_create_base_tag(buffer, "italic");
    for (int i = 1; i <= 6; i++) {
        char h_tag[3];
        g_snprintf(h_tag, sizeof(h_tag), "h%d", i);
        cm_render_get_or_create_base_tag(buffer, h_tag);
    }
    cm_render_get_or_create_base_tag(buffer, "code");
    cm_render_get_or_create_base_tag(buffer, "codeblock");
    cm_render_get_or_create_base_tag(buffer, "hr");


    // 3. Parse Markdown
    // CMARK_OPT_SMART enables smart quotes, dashes, etc.
    // CMARK_OPT_VALIDATE_UTF8 is good practice.
    int options = CMARK_OPT_DEFAULT | CMARK_OPT_SMART | CMARK_OPT_VALIDATE_UTF8;
    cmark_parser *parser = cmark_parser_new(options);
    if (!parser) {
        g_warning("cm_render_markdown_to_buffer: Failed to create cmark_parser.");
        return FALSE;
    }
    cmark_parser_feed(parser, markdown_text, strlen(markdown_text));
    cmark_node *document = cmark_parser_finish(parser);
    cmark_parser_free(parser);
    
    if (!document) {
        g_warning("cm_render_markdown_to_buffer: Failed to parse Markdown document.");
        return FALSE;
    }

    // 4. Render nodes
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);
    cm_render_node_recursive(document, buffer, &iter, NULL); // Start with no active tags

    // 5. Free cmark document
    cmark_node_free(document);

    // 6. Apply theme-dependent styles
    // This is crucial for elements like code blocks that need theme-specific colors.
    cm_render_update_theme_dependent_tags(buffer);

    return TRUE;
}

char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer) {
    if (!buffer) {
        return g_strdup(""); // Return empty, allocated string for consistency
    }
    // --- PORT LOGIC FROM cmark.c:export_buffer_to_markdown_cmark HERE ---
    // For now, a placeholder:
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    if (gtk_text_iter_equal(&start, &end)) {
        return g_strdup("");
    }

    g_warning("cm_render_buffer_to_markdown: Not yet fully implemented. Porting required.");
    return g_strdup("<!-- TODO: Implement Markdown export -->");
}

// Note: The include "gtktext_cmark.h" was in the original cmark.c.
// If it contains definitions specific to the old implementation or general utilities,
// ensure they are correctly handled or migrated. For a clean cmrender module,
// it should ideally only depend on its own header, GTK, Adwaita, and cmarklib.
