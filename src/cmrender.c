#include "cmrender.h"
// #include "gtktext_cmark.h" // Removed as per plan
#include <adwaita.h> // For AdwStyleManager
#include <string.h>
#include <stdio.h>

// Macro to silence unused variable warnings (if needed, or manage via compiler flags)
#define CMRENDER_UNUSED __attribute__((unused))

// Forward declaration for the recursive helper
static void cm_render_node_content_recursive(cmark_node *node, GtkTextBuffer *buffer, GtkTextIter *iter, GSList *active_tags, int *ordered_list_item_counter_ptr);
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
        } else if (strcmp(tag_name, "blockquote") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "blockquote",
                                             "left-margin", 20,
                                             "pixels-above-lines", 2,
                                             "pixels-below-lines", 2,
                                             // "wrap-mode", GTK_WRAP_WORD_CHAR, // Blockquotes should wrap
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

// Recursive function to render content of a node and its children.
// If 'node' is a block-level node, its rendered output (including children)
// will end with a single newline character.
// The 'ordered_list_item_counter_ptr' is used to pass and update the current item number for ordered lists.
static void cm_render_node_content_recursive(cmark_node *node, GtkTextBuffer *buffer, GtkTextIter *iter, GSList *active_tags, int *ordered_list_item_counter_ptr) {
    if (!node) return;

    cmark_node_type type = cmark_node_get_type(node);
    GSList *tags_for_children = active_tags; // Default, copy if modified for children
    char *tag_name_alloc = NULL;
    gboolean is_block_node = FALSE;

    // Determine if current node is a block node for trailing newline logic
    switch (type) {
        case CMARK_NODE_PARAGRAPH:
        case CMARK_NODE_HEADING:
        case CMARK_NODE_CODE_BLOCK:
        case CMARK_NODE_THEMATIC_BREAK:
        case CMARK_NODE_BLOCK_QUOTE:
        case CMARK_NODE_LIST:
        case CMARK_NODE_ITEM: // An item is also a block in terms of structure
        case CMARK_NODE_HTML_BLOCK:
            is_block_node = TRUE;
            break;
        default:
            is_block_node = FALSE;
            break;
    }

    // Specific handling for node types
    switch (type) {
        case CMARK_NODE_DOCUMENT:
            // Document node itself doesn't render, its children are the top-level blocks.
            // The main loop in cm_render_markdown_to_buffer handles iterating document children.
            // This function should be called for each child of the document.
            // So, if we reach here with DOCUMENT, we process its children directly.
            {
                cmark_node *child;
                gboolean first_block_child_of_document = TRUE;
                for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                    if (!first_block_child_of_document) {
                        // Add separating newline for blank line between top-level blocks
                        gtk_text_buffer_insert(buffer, iter, "\\n", -1);
                    }
                    cm_render_node_content_recursive(child, buffer, iter, active_tags, NULL); // No ordered list context here directly
                    first_block_child_of_document = FALSE;
                }
            }
            break;

        case CMARK_NODE_TEXT:
        {
            const char *literal = cmark_node_get_literal(node);
            if (literal) {
                cm_render_insert_with_active_tags(buffer, iter, literal, active_tags);
            }
            break;
        }
        case CMARK_NODE_EMPH: // Italic
        {
            tags_for_children = g_slist_prepend(g_slist_copy(active_tags), (gpointer)"italic");
            cmark_node *child;
            for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                cm_render_node_content_recursive(child, buffer, iter, tags_for_children, ordered_list_item_counter_ptr);
            }
            g_slist_free_full(tags_for_children, NULL);
            break;
        }
        case CMARK_NODE_STRONG: // Bold
        {
            tags_for_children = g_slist_prepend(g_slist_copy(active_tags), (gpointer)"bold");
            cmark_node *child;
            for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                cm_render_node_content_recursive(child, buffer, iter, tags_for_children, ordered_list_item_counter_ptr);
            }
            g_slist_free_full(tags_for_children, NULL);
            break;
        }
        case CMARK_NODE_HEADING:
        {
            int level = cmark_node_get_heading_level(node);
            tag_name_alloc = g_strdup_printf("h%d", level);
            tags_for_children = g_slist_prepend(g_slist_copy(active_tags), (gpointer)tag_name_alloc);
            cmark_node *child;
            for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                cm_render_node_content_recursive(child, buffer, iter, tags_for_children, ordered_list_item_counter_ptr);
            }
            g_slist_free_full(tags_for_children, NULL);
            g_free(tag_name_alloc);
            break;
        }
        case CMARK_NODE_CODE: // Inline code
        {
            const char *literal = cmark_node_get_literal(node);
            if (literal) {
                GSList *code_tag_list = g_slist_prepend(g_slist_copy(active_tags), (gpointer)"code");
                cm_render_insert_with_active_tags(buffer, iter, literal, code_tag_list);
                g_slist_free_full(code_tag_list, NULL);
            }
            break;
        }
        case CMARK_NODE_CODE_BLOCK:
        {
            const char *literal = cmark_node_get_literal(node);
            // const char *info = cmark_node_get_fence_info(node); // TODO: Use for syntax highlighting tag
            if (literal) {
                GSList *codeblock_tag_list = g_slist_prepend(NULL, (gpointer)"codeblock");
                cm_render_insert_with_active_tags(buffer, iter, literal, codeblock_tag_list);
                g_slist_free(codeblock_tag_list);
            }
            break;
        }
        case CMARK_NODE_THEMATIC_BREAK:
        {
            GSList *hr_tag_list = g_slist_prepend(NULL, (gpointer)"hr");
            cm_render_insert_with_active_tags(buffer, iter, "---", hr_tag_list);
            g_slist_free(hr_tag_list);
            break;
        }
        case CMARK_NODE_LINEBREAK: // Hard break
            gtk_text_buffer_insert(buffer, iter, "\\n", -1);
            break;

        case CMARK_NODE_SOFTBREAK:
            gtk_text_buffer_insert(buffer, iter, " ", -1);
            break;

        case CMARK_NODE_PARAGRAPH:
        {
            cmark_node *child;
            for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                cm_render_node_content_recursive(child, buffer, iter, active_tags, ordered_list_item_counter_ptr);
            }
            break;
        }
        case CMARK_NODE_LINK:
        {
            // const char *url = cmark_node_get_url(node);
            // const char *title = cmark_node_get_title(node);
            // TODO: Create a "link" tag, store URL in GObject data.
            // For now, just render link text.
            cmark_node *child;
            for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                cm_render_node_content_recursive(child, buffer, iter, active_tags, ordered_list_item_counter_ptr);
            }
            break;
        }
        case CMARK_NODE_IMAGE:
        {
            // const char *url = cmark_node_get_url(node);
            // TODO: Render alt text. Actual image display needs GtkImage + GtkTextChildAnchor.
            // For now, render alt text if present, or a placeholder.
            char *alt_text_display = NULL;
            cmark_node *text_child = cmark_node_first_child(node);
            if (text_child && cmark_node_get_type(text_child) == CMARK_NODE_TEXT) {
                alt_text_display = g_strdup_printf("[Image: %s]", cmark_node_get_literal(text_child));
            } else {
                alt_text_display = g_strdup("[Image]");
            }
            cm_render_insert_with_active_tags(buffer, iter, alt_text_display, active_tags);
            g_free(alt_text_display);
            break;
        }
        case CMARK_NODE_BLOCK_QUOTE:
        {
            // TODO: Apply "blockquote" tag with indent/margin.
            // The tag should be applied to the lines generated by children.
            // This is simpler if the tag is active for children.
            tags_for_children = g_slist_prepend(g_slist_copy(active_tags), (gpointer)"blockquote");
            ensure_base_blockquote_tag_exists(buffer); // Ensure "blockquote" tag is defined

            cmark_node *child;
            gboolean first_child_in_bq = TRUE;
            for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                 if (!first_child_in_bq) {
                    gtk_text_buffer_insert(buffer, iter, "\\n", -1); // Separator between blocks inside BQ
                }
                cm_render_node_content_recursive(child, buffer, iter, tags_for_children, NULL);
                first_child_in_bq = FALSE;
            }
            g_slist_free_full(tags_for_children, NULL);
            break;
        }
        case CMARK_NODE_LIST:
        {
            cmark_list_type list_type = cmark_node_get_list_type(node);
            int current_item_number = cmark_node_get_list_start(node); // For ordered lists

            cmark_node *item_child;
            gboolean first_item = TRUE;
            for (item_child = cmark_node_first_child(node); item_child != NULL; item_child = cmark_node_next(item_child)) {
                if (!first_item) {
                     // CommonMark rule: "Each new list item starts on a new line."
                     // The previous item's content (recursive call) should have ended with \n.
                     // So, no extra \n needed here before the marker of the current item.
                }
                // Pass down pointer to current_item_number for ordered lists, or NULL for unordered.
                cm_render_node_content_recursive(item_child, buffer, iter, active_tags,
                                                 (list_type == CMARK_ORDERED_LIST) ? &current_item_number : NULL);
                if (list_type == CMARK_ORDERED_LIST) {
                    // current_item_number should have been incremented by the ITEM's rendering logic
                }
                first_item = FALSE;
            }
            break;
        }
        case CMARK_NODE_ITEM:
        {
            // Insert list item marker (bullet or number)
            const char *marker_text;
            char num_marker[12]; // Buffer for "123. "
            cmark_node *parent_list = cmark_node_get_parent(node);

            if (parent_list && cmark_node_get_list_type(parent_list) == CMARK_ORDERED_LIST) {
                if (ordered_list_item_counter_ptr) {
                    g_snprintf(num_marker, sizeof(num_marker), "%d. ", *ordered_list_item_counter_ptr);
                    (*ordered_list_item_counter_ptr)++; // Increment for next item
                    marker_text = num_marker;
                } else {
                    marker_text = "?. "; // Should not happen if called correctly
                }
            } else { // Bullet list or unknown
                marker_text = "- "; // CommonMark: -, +, *
            }
            cm_render_insert_with_active_tags(buffer, iter, marker_text, active_tags); // No special tag for marker itself

            // Render item content
            // Children of an item can be multiple blocks. They need their own inter-block newlines.
            cmark_node *item_content_child;
            gboolean first_block_in_item = TRUE;
            for (item_content_child = cmark_node_first_child(node); item_content_child != NULL; item_content_child = cmark_node_next(item_content_child)) {
                if (!first_block_in_item) {
                    // If an item contains multiple blocks, they need blank line separation.
                    // The child block will end with \n. We add one more.
                    gtk_text_buffer_insert(buffer, iter, "\\n", -1);
                }
                cm_render_node_content_recursive(item_content_child, buffer, iter, active_tags, NULL); // No ordered list context for children of item
                first_block_in_item = FALSE;
            }
            break;
        }
        case CMARK_NODE_HTML_BLOCK:
        case CMARK_NODE_HTML_INLINE:
        {
            const char *literal = cmark_node_get_literal(node);
            if (literal) {
                cm_render_insert_with_active_tags(buffer, iter, literal, active_tags);
            }
            break;
        }
        default: // Should not be reached if all types are handled
            // Recursively call for children of unknown or container types
            {
                cmark_node *child;
                for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                    cm_render_node_content_recursive(child, buffer, iter, active_tags, ordered_list_item_counter_ptr);
                }
            }
            break;
    }

    if (is_block_node) {
        // Ensure the rendered content of this block node ends with a single newline.
        // Check if iter is already at a newline. If not, add one.
        // This is important for the main loop's logic of adding a second newline for separation.
        GtkTextIter current_pos_check_iter = *iter;
        gboolean already_ends_with_newline = FALSE;
        if (gtk_text_iter_get_offset(&current_pos_check_iter) > 0) {
            if (gtk_text_iter_backward_char(&current_pos_check_iter)) {
                if (gtk_text_iter_get_char(&current_pos_check_iter) == '\\n') {
                    already_ends_with_newline = TRUE;
                }
            }
        } else if (gtk_text_buffer_get_char_count(buffer) == 0) {
             // Empty buffer can be considered as "ending with a newline" for this purpose.
            already_ends_with_newline = TRUE;
        }


        if (!already_ends_with_newline) {
            gtk_text_buffer_insert(buffer, iter, "\\n", -1);
        }
    }
}


// Helper to ensure the 'blockquote' tag exists with basic styling
static void ensure_base_blockquote_tag_exists(GtkTextBuffer *buffer) {
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    if (!gtk_text_tag_table_lookup(tag_table, "blockquote")) {
        gtk_text_buffer_create_tag(buffer, "blockquote",
                                 "left-margin", 20, // Example indent
                                 "pixels-above-lines", 2,
                                 "pixels-below-lines", 2,
                                 // "border", ??? GtkTextTag does not have simple border
                                 // Could use paragraph-background for a subtle effect
                                 NULL);
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
    cm_render_get_or_create_base_tag(buffer, "blockquote"); // Add this in the pre-creation part


    // 3. Parse Markdown
    // CMARK_OPT_SMART enables smart quotes, dashes, etc.
    // CMARK_OPT_VALIDATE_UTF8 is good practice.
    // CMARK_OPT_LIBERAL_HTML_TAG allows more flexible HTML.
    // CMARK_OPT_FOOTNOTES if you want to support footnotes (not in initial scope)
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
    gtk_text_buffer_get_start_iter(buffer, &iter); // Start iter for the whole document

    cmark_node *doc_child_node;
    gboolean is_first_block_in_document = TRUE;

    for (doc_child_node = cmark_node_first_child(document); doc_child_node != NULL; doc_child_node = cmark_node_next(doc_child_node)) {
        if (!is_first_block_in_document) {
            // Add the separating newline for the "blank line" between blocks.
            // The previous block's rendering (via cm_render_node_content_recursive)
            // should have ended with one \n. This makes it \n\n.
            gtk_text_buffer_insert(buffer, &iter, "\\n", -1);
        }
        // Render the block node itself and its children.
        // This call will ensure that the content of 'doc_child_node' ends with a single '\n'.
        cm_render_node_content_recursive(doc_child_node, buffer, &iter, NULL, NULL); // Top-level blocks, no inherited ordered list counter

        is_first_block_in_document = FALSE;
        // iter is updated by cm_render_node_content_recursive and gtk_text_buffer_insert
    }

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
