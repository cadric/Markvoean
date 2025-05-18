#include "cmrender.h"
// #include "gtktext_cmark.h" // Removed as per plan
#include <adwaita.h> // For AdwStyleManager
#include <string.h>
#include <stdio.h>

// Helper struct for managing active inline Markdown tags during conversion
typedef struct {
    const char *tag_name; // "bold", "italic", "code", "link", "image"
    char *url;            // For links/images
    char *title;          // For links
    // The actual Markdown delimiters (e.g., "**", "*") are handled by the functions
} ActiveMarkdownInlineTag;

// Forward declarations for static helper functions for buffer_to_markdown
static void free_active_markdown_inline_tag(gpointer data);
static void close_inline_tags_from_stack(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, gboolean force_close_all);
static void open_inline_tags_for_segment(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, GtkTextBuffer *buffer);
static char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer);


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
        } else if (strcmp(tag_name, "link") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "link",
                                             "foreground", "blue", // Standard link color
                                             "underline", PANGO_UNDERLINE_SINGLE,
                                             NULL);
        } else if (strcmp(tag_name, "image") == 0) {
            // This tag is for the alt text or placeholder for an image.
            // Actual image display using GtkTextChildAnchor is more complex and
            // typically requires a GtkTextView instance.
            tag = gtk_text_buffer_create_tag(buffer, "image",
                                             // Example: "font-style", PANGO_STYLE_ITALIC,
                                             NULL); // No specific visual style for now, primarily for metadata.
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
    // Ensure iter does not go past end of buffer if text is very long or buffer is small
    GtkTextIter current_pos = *iter;
    gtk_text_buffer_insert(buffer, &current_pos, text, -1); // current_pos moves to end of inserted text
    *iter = current_pos; // Update the original iterator


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
            const char *info = cmark_node_get_fence_info(node);
            GSList *codeblock_tags = NULL;
            GtkTextTag *codeblock_tag_obj = NULL;

            // Ensure the "codeblock" tag exists
            codeblock_tag_obj = cm_render_get_or_create_base_tag(buffer, "codeblock");

            if (codeblock_tag_obj && info && strlen(info) > 0) {
                // Store the language info string on the tag for potential syntax highlighting
                g_object_set_data_full(G_OBJECT(codeblock_tag_obj), "language-info", g_strdup(info), g_free);
            }

            if (literal) {
                // Apply only the "codeblock" tag, not other active tags from parent.
                codeblock_tags = g_slist_prepend(NULL, (gpointer)"codeblock");
                cm_render_insert_with_active_tags(buffer, iter, literal, codeblock_tags);
                g_slist_free_full(codeblock_tags, NULL); // Changed for consistency
            }
            break;
        }
        case CMARK_NODE_THEMATIC_BREAK:
        {
            GSList *hr_tag_list = g_slist_prepend(NULL, (gpointer)"hr");
            cm_render_insert_with_active_tags(buffer, iter, "---", hr_tag_list);
            g_slist_free_full(hr_tag_list, NULL); // Changed for consistency
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
            const char *url = cmark_node_get_url(node);
            const char *title = cmark_node_get_title(node);
            GSList *link_tags = NULL;

            if (url) {
                // Ensure the "link" tag exists and set its data
                GtkTextTag *link_tag_obj = cm_render_get_or_create_base_tag(buffer, "link");
                if (link_tag_obj) {
                    g_object_set_data_full(G_OBJECT(link_tag_obj), "url", g_strdup(url), g_free);
                    if (title && strlen(title) > 0) {
                        g_object_set_data_full(G_OBJECT(link_tag_obj), "title", g_strdup(title), g_free);
                    }
                }
                link_tags = g_slist_prepend(g_slist_copy(active_tags), (gpointer)"link");
            } else {
                // If no URL, render as normal text without link tag
                link_tags = g_slist_copy(active_tags);
            }

            cmark_node *child;
            for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                cm_render_node_content_recursive(child, buffer, iter, link_tags, ordered_list_item_counter_ptr);
            }
            g_slist_free_full(link_tags, NULL); // Free the copied list
            break;
        }
        case CMARK_NODE_IMAGE:
        {
            const char *url = cmark_node_get_url(node);
            // const char *title = cmark_node_get_title(node); // Title can also be on images

            GSList *tags_for_image_content = NULL;
            gboolean has_valid_url = (url && strlen(url) > 0);

            if (has_valid_url) {
                GtkTextTag *image_tag_obj = cm_render_get_or_create_base_tag(buffer, "image");
                if (image_tag_obj) {
                    g_object_set_data_full(G_OBJECT(image_tag_obj), "url", g_strdup(url), g_free);
                    // if (title && strlen(title) > 0) {
                    // g_object_set_data_full(G_OBJECT(image_tag_obj), "title", g_strdup(title), g_free);
                    // }
                }
                tags_for_image_content = g_slist_prepend(g_slist_copy(active_tags), (gpointer)"image");
            } else {
                // No valid URL, or URL is empty. Render alt text (children) without "image" tag.
                tags_for_image_content = g_slist_copy(active_tags);
            }
            
            cmark_node *child;
            gboolean has_alt_text = FALSE;
            for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
                has_alt_text = TRUE;
                cm_render_node_content_recursive(child, buffer, iter, tags_for_image_content, ordered_list_item_counter_ptr);
            }
            
            if (!has_alt_text) {
                const char* placeholder = NULL;
                // Only add placeholder if there was a valid URL to indicate a missing image resource
                if (has_valid_url) { 
                    placeholder = "[Image]";
                } 
                // If no URL and no alt text (e.g. ![]()), this will currently render nothing for the placeholder part.
                // CommonMark specifies ![]() should render as literal text "![]()". 
                // This might require specific handling if cmark-gfm produces an IMAGE node for it.
                // For now, this logic focuses on correct tag application for alt-text/placeholder-with-URL.
                if (placeholder) {
                     cm_render_insert_with_active_tags(buffer, iter, placeholder, tags_for_image_content);
                }
            }
            g_slist_free_full(tags_for_image_content, NULL); // Free the (potentially) copied list
            break;
        }
        case CMARK_NODE_BLOCK_QUOTE:
        {
            tags_for_children = g_slist_prepend(g_slist_copy(active_tags), (gpointer)"blockquote");
            // Ensure "blockquote" tag is defined using the standard function
            cm_render_get_or_create_base_tag(buffer, "blockquote"); 

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
            gboolean is_loose_list = !cmark_node_get_list_tight(node);

            cmark_node *item_child;
            gboolean first_item = TRUE;
            for (item_child = cmark_node_first_child(node); item_child != NULL; item_child = cmark_node_next(item_child)) {
                if (!first_item && is_loose_list) {
                    // For loose lists, add an extra newline between items.
                    // The previous item already ended with one newline (is_block_node logic)
                    // This creates the blank line.
                    gtk_text_buffer_insert(buffer, iter, "\\n", -1);
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
                    // The child block will end with \\n. We add one more.
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
// This function is now effectively replaced by calling cm_render_get_or_create_base_tag directly.
// static void ensure_base_blockquote_tag_exists(GtkTextBuffer *buffer) {
// GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
// if (!gtk_text_tag_table_lookup(tag_table, "blockquote")) {
// // This was incomplete; cm_render_get_or_create_base_tag handles creation.
//     }
// }


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


// --- Implementation of cm_render_buffer_to_markdown and its helpers ---

static void free_active_markdown_inline_tag(gpointer data) {
    ActiveMarkdownInlineTag *tag_info = (ActiveMarkdownInlineTag *)data;
    if (!tag_info) return;
    g_free(tag_info->url);
    g_free(tag_info->title);
    // tag_info->tag_name is not g_free'd as it points to GtkTextTag's name (usually static or managed by Gtk)
    g_free(tag_info);
}

/**
 * @brief Closes Markdown inline tags from the top of the stack if they are not present
 * in current_gtk_tags, or if force_close_all is true.
 */
static void close_inline_tags_from_stack(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, gboolean force_close_all) {
    while (*active_inline_stack_ptr) {
        ActiveMarkdownInlineTag *stack_top_tag = (ActiveMarkdownInlineTag *)(*active_inline_stack_ptr)->data;
        gboolean still_active = FALSE;
        if (!force_close_all) {
            for (GSList *l_gtk = current_gtk_tags; l_gtk; l_gtk = l_gtk->next) {
                GtkTextTag *gtk_tag_obj = GTK_TEXT_TAG(l_gtk->data);
                if (strcmp(gtk_text_tag_get_name(gtk_tag_obj), stack_top_tag->tag_name) == 0) {
                    still_active = TRUE;
                    break;
                }
            }
        }

        if (still_active && !force_close_all) {
            // Top of stack tag is still active, so tags below it must also be. Stop.
            break;
        } else {
            // Close this tag
            if (strcmp(stack_top_tag->tag_name, "bold") == 0) g_string_append(md_output, "**");
            else if (strcmp(stack_top_tag->tag_name, "italic") == 0) g_string_append(md_output, "*");
            else if (strcmp(stack_top_tag->tag_name, "code") == 0) g_string_append(md_output, "`");
            else if (strcmp(stack_top_tag->tag_name, "link") == 0) {
                g_string_append_printf(md_output, "](%s%s%s)",
                                       stack_top_tag->url ? stack_top_tag->url : "",
                                       (stack_top_tag->url && stack_top_tag->title) ? " \\"" : "",
                                       stack_top_tag->title ? stack_top_tag->title : "",
                                       (stack_top_tag->url && stack_top_tag->title) ? "\\"" : "");
            } else if (strcmp(stack_top_tag->tag_name, "image") == 0) {
                g_string_append_printf(md_output, "](%s)", stack_top_tag->url ? stack_top_tag->url : "");
            }
            // Pop from stack
            *active_inline_stack_ptr = g_slist_remove_link(*active_inline_stack_ptr, *active_inline_stack_ptr);
            free_active_markdown_inline_tag(stack_top_tag);
        }
    }
}

/**
 * @brief Opens new Markdown inline tags if they are in current_gtk_tags but not on the active_inline_stack.
 * Respects a preferred order for opening.
 */
static void open_inline_tags_for_segment(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, GtkTextBuffer *buffer) {
    // Preferred order for opening tags to maintain consistency (e.g., links before bold)
    const char *preferred_order[] = {"link", "image", "bold", "italic", "code"}; // "code" is usually innermost

    for (int i = 0; i < G_N_ELEMENTS(preferred_order); ++i) {
        const char *tag_to_open_name = preferred_order[i];
        gboolean is_already_on_stack = FALSE;
        for (GSList *l_stack = *active_inline_stack_ptr; l_stack; l_stack = l_stack->next) {
            if (strcmp(((ActiveMarkdownInlineTag *)l_stack->data)->tag_name, tag_to_open_name) == 0) {
                is_already_on_stack = TRUE;
                break;
            }
        }

        if (is_already_on_stack) continue;

        // Check if this tag exists in current_gtk_tags
        GtkTextTag *gtk_tag_to_open = NULL;
        for (GSList *l_gtk = current_gtk_tags; l_gtk; l_gtk = l_gtk->next) {
            GtkTextTag *current_gtk_tag_obj = GTK_TEXT_TAG(l_gtk->data);
            if (strcmp(gtk_text_tag_get_name(current_gtk_tag_obj), tag_to_open_name) == 0) {
                gtk_tag_to_open = current_gtk_tag_obj;
                break;
            }
        }

        if (gtk_tag_to_open) {
            ActiveMarkdownInlineTag *new_tag_info = g_new0(ActiveMarkdownInlineTag, 1);
            new_tag_info->tag_name = gtk_text_tag_get_name(gtk_tag_to_open); // Points to tag's name

            if (strcmp(new_tag_info->tag_name, "bold") == 0) g_string_append(md_output, "**");
            else if (strcmp(new_tag_info->tag_name, "italic") == 0) g_string_append(md_output, "*");
            else if (strcmp(new_tag_info->tag_name, "code") == 0) g_string_append(md_output, "`");
            else if (strcmp(new_tag_info->tag_name, "link") == 0) {
                const char *url_val = g_object_get_data(G_OBJECT(gtk_tag_to_open), "url");
                const char *title_val = g_object_get_data(G_OBJECT(gtk_tag_to_open), "title");
                if (url_val) new_tag_info->url = g_strdup(url_val);
                if (title_val) new_tag_info->title = g_strdup(title_val);
                g_string_append(md_output, "[");
            } else if (strcmp(new_tag_info->tag_name, "image") == 0) {
                const char *url_val = g_object_get_data(G_OBJECT(gtk_tag_to_open), "url");
                if (url_val) new_tag_info->url = g_strdup(url_val);
                g_string_append(md_output, "![");
            } else {
                free_active_markdown_inline_tag(new_tag_info); // Should not happen if preferred_order is correct
                continue;
            }
            *active_inline_stack_ptr = g_slist_prepend(*active_inline_stack_ptr, new_tag_info);
        }
    }
}


char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer) {
    GString *md_output = g_string_new("");
    GtkTextIter iter, segment_end_iter, buffer_end_iter;
    GSList *active_inline_stack = NULL; // Stack of ActiveMarkdownInlineTag*

    gtk_text_buffer_get_start_iter(buffer, &iter);
    gtk_text_buffer_get_end_iter(buffer, &buffer_end_iter);

    gboolean in_markdown_code_block = FALSE;
    gboolean last_char_was_newline = TRUE; // Treat start of buffer as if after a newline

    while (gtk_text_iter_compare(&iter, &buffer_end_iter) < 0) {
        segment_end_iter = iter;
        if (!gtk_text_iter_forward_to_tag_toggle(&segment_end_iter, NULL)) {
            segment_end_iter = buffer_end_iter; // No more toggles, process till end
        }
        // If iter didn't move but not at end, process till end (e.g. untagged text at end)
        if (gtk_text_iter_equal(&segment_end_iter, &iter) && gtk_text_iter_compare(&iter, &buffer_end_iter) < 0) {
            segment_end_iter = buffer_end_iter;
        }

        GSList *gtk_tags_on_segment = gtk_text_iter_get_tags(&iter);
        char *text_of_segment = gtk_text_buffer_get_text(buffer, &iter, &segment_end_iter, FALSE);

        // --- Block Element Detection & Handling ---
        gboolean is_segment_code_block_tagged = FALSE;
        const char* code_block_language = NULL;
        gboolean is_segment_heading_tagged = FALSE;
        int heading_level = 0;
        gboolean is_segment_hr_tagged = FALSE;
        gboolean is_segment_blockquote_tagged = FALSE;

        for (GSList *l = gtk_tags_on_segment; l; l = l->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(l->data);
            const char *name = gtk_text_tag_get_name(tag);
            if (strcmp(name, "codeblock") == 0) {
                is_segment_code_block_tagged = TRUE;
                code_block_language = g_object_get_data(G_OBJECT(tag), "language-info");
            } else if (strncmp(name, "h", 1) == 0 && strlen(name) == 2 && name[1] >= '1' && name[1] <= '6') {
                is_segment_heading_tagged = TRUE;
                heading_level = name[1] - '0';
            } else if (strcmp(name, "hr") == 0) {
                is_segment_hr_tagged = TRUE;
            } else if (strcmp(name, "blockquote") == 0) {
                is_segment_blockquote_tagged = TRUE;
            }
        }

        // Handle Markdown code block state transitions
        if (is_segment_code_block_tagged && !in_markdown_code_block) { // Entering code block
            close_inline_tags_from_stack(md_output, &active_inline_stack, NULL, TRUE); // Close all inlines
            if (md_output->len > 0 && md_output->str[md_output->len - 1] != '\\n') g_string_append_c(md_output, '\\n');
            g_string_append(md_output, "```");
            if (code_block_language) g_string_append(md_output, code_block_language);
            g_string_append_c(md_output, '\\n');
            in_markdown_code_block = TRUE;
            last_char_was_newline = TRUE;
        } else if (!is_segment_code_block_tagged && in_markdown_code_block) { // Exiting code block
            if (md_output->len > 0 && md_output->str[md_output->len - 1] != '\\n') g_string_append_c(md_output, '\\n');
            g_string_append(md_output, "```\\n");
            in_markdown_code_block = FALSE;
            last_char_was_newline = TRUE;
        }

        if (in_markdown_code_block) {
            g_string_append(md_output, text_of_segment); // Append raw text
            if (strlen(text_of_segment) > 0) {
                last_char_was_newline = (text_of_segment[strlen(text_of_segment) - 1] == '\\n');
            }
        } else {
            // Handle other block types and inline content
            if (is_segment_hr_tagged) {
                close_inline_tags_from_stack(md_output, &active_inline_stack, NULL, TRUE);
                if (!last_char_was_newline) g_string_append_c(md_output, '\\n');
                g_string_append(md_output, "---\\n");
                last_char_was_newline = TRUE;
            } else {
                 // Prepend block prefixes if at the start of a new line in Markdown
                if (last_char_was_newline) {
                    if (is_segment_heading_tagged) {
                        close_inline_tags_from_stack(md_output, &active_inline_stack, gtk_tags_on_segment, FALSE); // Close conflicting
                        for (int i = 0; i < heading_level; ++i) g_string_append_c(md_output, '#');
                        g_string_append_c(md_output, ' ');
                    } else if (is_segment_blockquote_tagged) {
                        // Blockquote prefix applies after potential heading, and before inline content
                         g_string_append(md_output, "> ");
                    }
                }

                // Manage inline tags for the current segment
                close_inline_tags_from_stack(md_output, &active_inline_stack, gtk_tags_on_segment, FALSE);
                open_inline_tags_for_segment(md_output, &active_inline_stack, gtk_tags_on_segment, buffer);

                // Append text, converting "\\n" to "  \n" (hard break)
                // And handle newlines within blockquotes
                char *temp_text = g_strdup(text_of_segment);
                char *ptr = temp_text;
                GString *processed_segment_text = g_string_new("");

                // Split by literal "\\n" first for hard breaks, then by "\n" for blockquote lines
                char** hard_break_parts = g_strsplit(ptr, "\\\\n", -1); // Split by literal "\\n"
                for (int hb_idx = 0; hard_break_parts[hb_idx] != NULL; ++hb_idx) {
                    if (hb_idx > 0) { // Re-insert hard break
                        g_string_append(processed_segment_text, "  \\n");
                        last_char_was_newline = TRUE;
                         if (is_segment_blockquote_tagged && last_char_was_newline) {
                            g_string_append(processed_segment_text, "> ");
                        }
                    }
                    
                    if (is_segment_blockquote_tagged) {
                        char** bq_lines = g_strsplit(hard_break_parts[hb_idx], "\\n", -1);
                        for (int bq_idx = 0; bq_lines[bq_idx] != NULL; ++bq_idx) {
                            if (bq_idx > 0) { // Re-insert newline
                                g_string_append_c(processed_segment_text, '\\n');
                                last_char_was_newline = TRUE;
                                g_string_append(processed_segment_text, "> "); // Add "> " after newline
                            }
                            g_string_append(processed_segment_text, bq_lines[bq_idx]);
                            if (strlen(bq_lines[bq_idx]) > 0) last_char_was_newline = FALSE;
                        }
                        g_strfreev(bq_lines);
                    } else {
                        g_string_append(processed_segment_text, hard_break_parts[hb_idx]);
                         if (strlen(hard_break_parts[hb_idx]) > 0) {
                            char last_char_in_part = hard_break_parts[hb_idx][strlen(hard_break_parts[hb_idx])-1];
                            last_char_was_newline = (last_char_in_part == '\\n');
                        } else if (hb_idx > 0) { // if it was just a \\n, then last_char_was_newline is true
                            last_char_was_newline = TRUE;
                        }
                    }
                }
                g_strfreev(hard_break_parts);
                g_free(temp_text);
                
                g_string_append_gstring(md_output, processed_segment_text);
                g_string_free(processed_segment_text, TRUE);

                // Update last_char_was_newline based on the actual content appended
                if (md_output->len > 0) {
                    last_char_was_newline = (md_output->str[md_output->len - 1] == '\\n');
                } else {
                    last_char_was_newline = TRUE; // If output is empty, effectively after a newline
                }
            }
        }

        g_slist_free_full(gtk_tags_on_segment, g_object_unref);
        g_free(text_of_segment);
        iter = segment_end_iter; // Move iterator to the end of the processed segment
    }

    // Close any remaining inline tags
    close_inline_tags_from_stack(md_output, &active_inline_stack, NULL, TRUE);
    g_slist_free_full(active_inline_stack, free_active_markdown_inline_tag); // Should be empty now but good practice

    // Ensure code block is closed if it was the last thing
    if (in_markdown_code_block) {
        if (md_output->len > 0 && md_output->str[md_output->len - 1] != '\\n') g_string_append_c(md_output, '\\n');
        g_string_append(md_output, "```\\n");
    }
    
    // Ensure final newline if content exists and doesn't end with one
    if (md_output->len > 0 && md_output->str[md_output->len - 1] != '\\n') {
        g_string_append_c(md_output, '\\n');
    }

    return g_string_free(md_output, FALSE);
}

// Note: The include "gtktext_cmark.h" was in the original cmark.c.
// If it contains definitions specific to the old implementation or general utilities,
// ensure they are correctly handled or migrated. For a clean cmrender module,
// it should ideally only depend on its own header, GTK, Adwaita, and cmarklib. 
