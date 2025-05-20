#include "cmrender.h"
// #include "gtktext_cmark.h" // Removed as per plan
#include <adwaita.h> // For AdwStyleManager
#include <gtk/gtk.h> // Include full gtk.h for all required functions
#include <string.h>
#include <stdio.h>
#include <cmark.h> // Ensure cmark functions are declared

// Macro to silence unused variable warnings (if needed, or manage via compiler flags)
#define CMRENDER_UNUSED __attribute__((unused))

// Helper function to safely get tag names since gtk_text_tag_get_name isn't directly
// available or is named differently in GTK4
static const char *get_tag_name_safe(GtkTextTag *tag) {
    if (!tag) return NULL;

    const char *name = g_object_get_data(G_OBJECT(tag), "tag-name");
    if (name && *name) {
        return name;
    }

    gchar *prop_name = NULL;
    g_object_get(G_OBJECT(tag), "name", &prop_name, NULL);

    if (prop_name && *prop_name) {
        g_object_set_data_full(G_OBJECT(tag), "tag-name", g_strdup(prop_name), (GDestroyNotify)g_free);
        const char *stored_name = g_object_get_data(G_OBJECT(tag), "tag-name");
        g_free(prop_name);
        return stored_name;
    }
    
    g_free(prop_name);
    g_warning("Tag name not found for tag %p (data 'tag-name' or GObject property 'name').", (void*)tag);
    return NULL;
}

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
static void open_inline_tags_for_segment(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, G_GNUC_UNUSED GtkTextBuffer *buffer);
// static char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer); // Declaration removed, will be non-static

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
            // Basic blockquote: slightly larger left margin, perhaps different background or foreground.
            // Theme-dependent aspects like background could be handled in update_theme_dependent_tags.
            // For now, a simple indent. More complex styling (like a border) is harder with just tags.
            tag = gtk_text_buffer_create_tag(buffer, "blockquote",
                                             "indent", 20, // Example: 20px indent
                                             // "left_margin", 20, // Alternative property
                                             // "pixels_above_lines", 5, // Spacing
                                             // "pixels_below_lines", 5,
                                             NULL);
        } else if (strcmp(tag_name, "link") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "link",
                                             "foreground", "blue",
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
        
        // Store the name for our own access in GTK4
        if (tag) {
            g_object_set_data_full(G_OBJECT(tag), "tag-name", g_strdup(tag_name), (GDestroyNotify)g_free);
        }
    } else {
        // Even for existing tags, make sure they have the tag name data set
        if (!g_object_get_data(G_OBJECT(tag), "tag-name")) {
            g_object_set_data_full(G_OBJECT(tag), "tag-name", g_strdup(tag_name), (GDestroyNotify)g_free);
        }
    }
    return tag;
}

void cm_render_update_theme_dependent_tags(GtkTextBuffer *buffer) {
    if (!buffer) return;

    AdwStyleManager *style_manager = adw_style_manager_get_default();
    AdwColorScheme color_scheme = adw_style_manager_get_color_scheme(style_manager);
    gboolean is_dark = (color_scheme == ADW_COLOR_SCHEME_FORCE_DARK || color_scheme == ADW_COLOR_SCHEME_PREFER_DARK);

    // GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer); // Unused variable
    
    // Ensure 'code' tag exists or create it before setting theme properties
    GtkTextTag *code_tag = cm_render_get_or_create_base_tag(buffer, "code");
    // Ensure 'codeblock' tag exists or create it
    GtkTextTag *codeblock_tag = cm_render_get_or_create_base_tag(buffer, "codeblock");
    // Ensure 'link' tag exists or create it (though its base style is non-theme dependent)
    cm_render_get_or_create_base_tag(buffer, "link");


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
    char tag_name_buffer[4]; // Buffer for constructing tag names like "h1", "h2", etc.
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
            // No specific tag for document, just process children
            break; // Added break

        case CMARK_NODE_BLOCK_QUOTE:
            tag_name_alloc = g_strdup("blockquote");
            is_block_node = TRUE;
            break;

        case CMARK_NODE_LIST:
            // TODO: Handle list-specific properties like type (bullet/ordered), spacing, etc.
            // For now, just treat as a block and process items.
            if (ordered_list_item_counter_ptr) { // Check if the pointer is valid
                if (cmark_node_get_list_type(node) == CMARK_ORDERED_LIST) {
                    *ordered_list_item_counter_ptr = cmark_node_get_list_start(node);
                } else {
                    *ordered_list_item_counter_ptr = 0; // Reset for bullet lists or indicate not in ordered list
                }
            }
            is_block_node = TRUE;
            break;

        case CMARK_NODE_ITEM:
            // TODO: Handle item markers (bullets, numbers) and indentation.
            if (ordered_list_item_counter_ptr && *ordered_list_item_counter_ptr > 0) { // Check pointer and if in an ordered list
                char item_marker[16];
                g_snprintf(item_marker, sizeof(item_marker), "%d. ", *ordered_list_item_counter_ptr);
                cm_render_insert_with_active_tags(buffer, iter, item_marker, active_tags); // No special tags for marker itself
                (*ordered_list_item_counter_ptr)++;
            } else { // Bullet list or other (or if ordered_list_item_counter_ptr is NULL or 0)
                cm_render_insert_with_active_tags(buffer, iter, "* ", active_tags); // Default bullet
            }
            is_block_node = TRUE;
            break;

        case CMARK_NODE_HEADING:
            g_snprintf(tag_name_buffer, sizeof(tag_name_buffer), "h%d", cmark_node_get_heading_level(node));
            tag_name_alloc = g_strdup(tag_name_buffer);
            is_block_node = TRUE;
            break;

        case CMARK_NODE_CODE_BLOCK:
            tag_name_alloc = g_strdup("codeblock");
            is_block_node = TRUE;
            // Special handling for code block content to preserve exact text
            cm_render_insert_with_active_tags(buffer, iter, cmark_node_get_literal(node), tags_for_children); // Apply codeblock tag
            // Children are not processed for code_block as content is literal
            if (tag_name_alloc) g_free(tag_name_alloc); // Free if allocated
            if (is_block_node && !gtk_text_iter_starts_line(iter)) {
                gtk_text_buffer_insert(buffer, iter, "\n", -1);
            }
            return; // Return early as children are not processed in the standard way

        case CMARK_NODE_HTML_BLOCK:
            // If CMARK_OPT_UNSAFE is not used, this will be escaped or omitted by cmark.
            // If it were enabled, we might insert cmark_node_get_literal(node) here.
            // For now, we assume it's handled by cmark's default (likely stripped/escaped text)
            // or we explicitly ignore it if we don't want to render raw HTML.
            cm_render_insert_with_active_tags(buffer, iter, cmark_node_get_literal(node), active_tags);
            is_block_node = TRUE;
            break;

        case CMARK_NODE_THEMATIC_BREAK: // hr
            tag_name_alloc = g_strdup("hr");
            // Insert a visual representation for the HR, e.g., "---"
            // The tag "hr" could then be styled (e.g., gray color, specific font) if desired.
            // Or, one could use a GtkSeparator widget via a GtkTextChildAnchor if complex rendering is needed.
            cm_render_insert_with_active_tags(buffer, iter, "\n--------------------\n", active_tags); // Insert with current tags, then apply HR tag to this segment
            is_block_node = TRUE;
            // No children to process for thematic break
            if (tag_name_alloc) g_free(tag_name_alloc); // Free if allocated
            if (is_block_node && !gtk_text_iter_starts_line(iter)) {
                 gtk_text_buffer_insert(buffer, iter, "\n", -1);
            }
            return; // Return early

        case CMARK_NODE_PARAGRAPH:
            // No specific tag for paragraph itself, but it's a block node.
            // Spacing around paragraphs is handled by the newline logic for block nodes.
            is_block_node = TRUE;
            break;

        // Inline types
        case CMARK_NODE_TEXT:
        {
            const char *text = cmark_node_get_literal(node);
            cm_render_insert_with_active_tags(buffer, iter, text, active_tags);
            break;
        }
        case CMARK_NODE_SOFTBREAK:
            // CommonMark: A softbreak may be rendered as a space or directly.
            // GTK default behavior with text nodes often handles this fine if newlines are preserved.
            // Or, explicitly: gtk_text_buffer_insert(buffer, iter, " ", -1);
            // Based on commonmark_rules.md, render as a space.
            cm_render_insert_with_active_tags(buffer, iter, " ", active_tags);
            break;
        case CMARK_NODE_LINEBREAK:
            // Hard line break
            cm_render_insert_with_active_tags(buffer, iter, "\n", active_tags);
            break;
        case CMARK_NODE_CODE:
            tag_name_alloc = g_strdup("code");
            break;
        case CMARK_NODE_HTML_INLINE:
            // Similar to HTML_BLOCK, depends on CMARK_OPT_UNSAFE
            // For now, insert literal content, which cmark might have escaped.
            cm_render_insert_with_active_tags(buffer, iter, cmark_node_get_literal(node), active_tags);
            break;
        case CMARK_NODE_EMPH: // Italic
            tag_name_alloc = g_strdup("italic");
            break;
        case CMARK_NODE_STRONG: // Bold
            tag_name_alloc = g_strdup("bold");
            break;
        case CMARK_NODE_LINK:
            tag_name_alloc = g_strdup("link");
            // The GtkTextTag object itself will be created/retrieved later if needed.
            // Metadata like URL will be attached when the tag object is instantiated.
            break;
        case CMARK_NODE_IMAGE:
            // TODO: Handle images. For now, we could insert the alt text.
            // tag_name_alloc = g_strdup("image"); // If we had specific styling for alt text
            {
                const char *alt_text = "";
                cmark_node *child = cmark_node_first_child(node); // Alt text is the content of the image node
                while (child) {
                    if (cmark_node_get_type(child) == CMARK_NODE_TEXT) {
                        alt_text = cmark_node_get_literal(child);
                        break;
                    }
                    child = cmark_node_next(child);
                }
                char *img_representation;
                // const char *url = cmark_node_get_url(node);
                // const char *title = cmark_node_get_title(node);
                // For now, just show alt text, or a placeholder if no alt text.
                if (alt_text && strlen(alt_text) > 0) {
                    img_representation = g_strdup_printf("[Image: %s]", alt_text);
                } else {
                    img_representation = g_strdup("[Image]");
                }
                cm_render_insert_with_active_tags(buffer, iter, img_representation, active_tags);
                g_free(img_representation);
                // No children processing for image node in this simple representation
                if (tag_name_alloc) g_free(tag_name_alloc); // Should be NULL here or handled
                return; // Return early
            }
            break;

        default:
            // For unknown node types, we can choose to ignore or log them.
            // g_message("Unhandled cmark node type: %s", cmark_node_get_type_string(node));
            break;
    }

    // If a specific tag was determined for this node type (e.g., "bold", "h1")
    // It will be applied to the children of this node.
    GSList* list_passed_to_children = active_tags;
    char* duplicated_tag_name_for_list = NULL;

    if (tag_name_alloc) {
        // Ensure the GtkTextTag object exists in the buffer (creates if not present).
        // This is also where we attach metadata like URL to the GtkTextTag object.
        GtkTextTag *tag_object_for_metadata = cm_render_get_or_create_base_tag(buffer, tag_name_alloc);
        if (tag_object_for_metadata) { // Check if tag creation was successful
            if (type == CMARK_NODE_LINK) {
                const char *url = cmark_node_get_url(node);
                const char *title = cmark_node_get_title(node);
                if (url) { 
                    // Remove previous data if any to prevent leaks if this tag is reused with different URLs
                    g_object_set_data(G_OBJECT(tag_object_for_metadata), "link-url", NULL); 
                    g_object_set_data_full(G_OBJECT(tag_object_for_metadata), "link-url", g_strdup(url), g_free); 
                }
                if (title) { 
                    g_object_set_data(G_OBJECT(tag_object_for_metadata), "link-title", NULL);
                    g_object_set_data_full(G_OBJECT(tag_object_for_metadata), "link-title", g_strdup(title), g_free); 
                }
            }
        } else {
            g_warning("Could not get or create tag for metadata: %s", tag_name_alloc);
        }

        // Prepend the TAG NAME string to the list of active tags for children.
        // We g_strdup tag_name_alloc because tag_name_alloc itself will be freed
        // at the end of this function call. The list now owns this new string.
        duplicated_tag_name_for_list = g_strdup(tag_name_alloc);
        list_passed_to_children = g_slist_prepend(active_tags, duplicated_tag_name_for_list);
    }

    // Recursively process child nodes with the (potentially updated) list of active tags
    cmark_node *child;
    for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
        cm_render_node_content_recursive(child, buffer, iter, list_passed_to_children, ordered_list_item_counter_ptr);
    }

    // Clean up: if we prepended a duplicated tag name string for our children,
    // remove it from the list and free the duplicated string.
    if (duplicated_tag_name_for_list) { // This implies tag_name_alloc was set and string was dup'd and prepended
        list_passed_to_children = g_slist_remove(list_passed_to_children, duplicated_tag_name_for_list);
        g_free(duplicated_tag_name_for_list); // Free the string we added to the list
        // Note: list_passed_to_children is now restored to the original active_tags
        // because g_slist_remove returns the new head of the list.
        // If active_tags was NULL and we prepended, list_passed_to_children became non-NULL,
        // and after remove, it becomes NULL again. This is correct.
    }
    g_free(tag_name_alloc); // Free the original tag_name_alloc for the current node type

    // After processing a block node and its children, insert a newline if not already at line start.
    // This ensures separation between block elements.
    // However, for lists and items, the structure might handle newlines differently.
    if (is_block_node) {
        // Check if the iterator is already at the start of a line or if the buffer ends here.
        // GtkTextIter next_char_iter = *iter;
        // if (gtk_text_iter_forward_char(&next_char_iter) && !gtk_text_iter_starts_line(iter)) {
        // A simpler check: if it's a block and we are not at the very start of the buffer
        // and the previous char was not a newline (difficult to check reliably without looking back)
        // A common strategy: always add a newline after a block, then perhaps consolidate multiple newlines later if needed.
        // For now, a single newline if not at start of a line.
        if (!gtk_text_iter_starts_line(iter) && gtk_text_iter_get_offset(iter) > 0) {
             // Ensure we don't add a newline if the iter is at the very end of the buffer
             // and the buffer is empty or already ends with a newline.
             // This logic is tricky; cmark often adds its own newlines for block separation in its literal output.
             // The main loop in cm_render_markdown_to_buffer already adds a newline between top-level blocks.
             // This internal one should only be for nested blocks if necessary.
             // For now, let the main loop handle inter-block newlines primarily.
             // The `is_block_node` flag here is more for conceptual grouping than forcing a newline.
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
            gtk_text_buffer_insert(buffer, &iter, "\n", -1);
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
                const char *tag_name = get_tag_name_safe(gtk_tag_obj);
                if (tag_name && strcmp(tag_name, stack_top_tag->tag_name) == 0) {
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
                g_string_append_printf(md_output, "](%s%s%s%s)",
                                      stack_top_tag->url ? stack_top_tag->url : "",
                                      (stack_top_tag->url && stack_top_tag->title) ? " \"" : "",
                                      stack_top_tag->title ? stack_top_tag->title : "",
                                      (stack_top_tag->url && stack_top_tag->title) ? "\"" : "");
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
static void open_inline_tags_for_segment(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, G_GNUC_UNUSED GtkTextBuffer *buffer) {
    // Preferred order for opening tags to maintain consistency (e.g., links before bold)
    const char *preferred_order[] = {"link", "image", "bold", "italic", "code"}; // "code" is usually innermost

    for (int i = 0; i < (int)G_N_ELEMENTS(preferred_order); ++i) { // Cast G_N_ELEMENTS to int
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
            const char *current_tag_name = get_tag_name_safe(current_gtk_tag_obj);
            if (current_tag_name && strcmp(current_tag_name, tag_to_open_name) == 0) {
                gtk_tag_to_open = current_gtk_tag_obj;
                break;
            }
        }

        if (gtk_tag_to_open) {
            ActiveMarkdownInlineTag *new_tag_info = g_new0(ActiveMarkdownInlineTag, 1);
            new_tag_info->tag_name = tag_to_open_name; // Use the preferred_order name directly

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
            const char *name = get_tag_name_safe(tag);
            if (name) {
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
        }

        // Handle Markdown code block state transitions
        if (is_segment_code_block_tagged && !in_markdown_code_block) { // Entering code block
            close_inline_tags_from_stack(md_output, &active_inline_stack, NULL, TRUE); // Close all inlines
            if (md_output->len > 0 && md_output->str[md_output->len - 1] != '\n') g_string_append_c(md_output, '\n');
            g_string_append(md_output, "```");
            if (code_block_language) g_string_append(md_output, code_block_language);
            g_string_append_c(md_output, '\n');
            in_markdown_code_block = TRUE;
            last_char_was_newline = TRUE;
        } else if (!is_segment_code_block_tagged && in_markdown_code_block) { // Exiting code block
            if (md_output->len > 0 && md_output->str[md_output->len - 1] != '\n') g_string_append_c(md_output, '\n');
            g_string_append(md_output, "```\n");
            in_markdown_code_block = FALSE;
            last_char_was_newline = TRUE;
        }

        if (in_markdown_code_block) {
            g_string_append(md_output, text_of_segment);
            if (strlen(text_of_segment) > 0) {
                last_char_was_newline = (text_of_segment[strlen(text_of_segment) - 1] == '\n');
            }
        } else {
            // Handle other block types and inline content
            if (is_segment_hr_tagged) {
                close_inline_tags_from_stack(md_output, &active_inline_stack, NULL, TRUE); // Close all inlines before HR
                if (!last_char_was_newline && md_output->len > 0) g_string_append_c(md_output, '\n'); // Ensure HR is on a new line
                g_string_append(md_output, "---\n"); // Corrected
                last_char_was_newline = TRUE;
            } else {
                // Special handling for heading prefix
                if (is_segment_heading_tagged) {
                    // Headings should not have prior inline styles carrying over into their prefix.
                    // Close all active inline tags. Any styling for heading text itself will be in gtk_tags_on_segment.
                    close_inline_tags_from_stack(md_output, &active_inline_stack, NULL, TRUE);
                    if (!last_char_was_newline && md_output->len > 0) {
                         g_string_append_c(md_output, '\n'); // Ensure heading starts on a new line // Corrected
                    }
                    for (int i = 0; i < heading_level; ++i) g_string_append_c(md_output, '#');
                    g_string_append_c(md_output, ' ');
                    last_char_was_newline = FALSE; // Prefix is not a newline
                    // Now, open any inline tags specific to the heading's text content
                    open_inline_tags_for_segment(md_output, &active_inline_stack, gtk_tags_on_segment, buffer);
                } else {
                    // General inline tag management for non-heading, non-hr, non-code-block text
                    close_inline_tags_from_stack(md_output, &active_inline_stack, gtk_tags_on_segment, FALSE);
                    open_inline_tags_for_segment(md_output, &active_inline_stack, gtk_tags_on_segment, buffer);
                }

                // Process text_of_segment line by line
                const char *line_iterator = text_of_segment;
                while (TRUE) {
                    const char *next_newline = strchr(line_iterator, '\n'); // Corrected
                    gchar *current_line_text;

                    if (next_newline) {
                        current_line_text = g_strndup(line_iterator, next_newline - line_iterator);
                    } else {
                        current_line_text = g_strdup(line_iterator); // Rest of the segment
                    }

                    // Add blockquote prefix if needed for this line
                    if (last_char_was_newline && is_segment_blockquote_tagged) {
                        // Avoid double prefix if previous segment also ended with "> \\n"
                        // A bit simplistic, assumes "> " is always 2 chars.
                        if (md_output->len < 2 || !(md_output->str[md_output->len - 2] == '>' && md_output->str[md_output->len - 1] == ' ')) {
                             g_string_append(md_output, "> ");
                        }
                    }
                    
                    g_string_append(md_output, current_line_text);
                    g_free(current_line_text);

                    if (next_newline) {
                        g_string_append(md_output, "  \n"); // Markdown hard break // Corrected
                        last_char_was_newline = TRUE;
                        line_iterator = next_newline + 1;
                        if (*line_iterator == '\0') break; // End of segment text if newline was the last char
                    } else {
                        // This was the last part of the segment (or the only part)
                        if (strlen(line_iterator) > 0) { // If there was content on this last line part
                            last_char_was_newline = FALSE;
                        }
                        // If line_iterator was empty (e.g. segment was empty, or ended with \\n),
                        // last_char_was_newline retains its state (TRUE if ended with \\n, or from before segment).
                        break; 
                    }
                }
            }
        }

        g_slist_free(gtk_tags_on_segment); // Correct: Only free the list itself
        g_free(text_of_segment);
        iter = segment_end_iter; // Move iterator to the end of the processed segment
    }

    // Close any remaining inline tags
    close_inline_tags_from_stack(md_output, &active_inline_stack, NULL, TRUE);
    g_slist_free_full(active_inline_stack, free_active_markdown_inline_tag); // Should be empty now but good practice

    // Ensure code block is closed if it was the last thing
    if (in_markdown_code_block) {
        if (md_output->len > 0 && md_output->str[md_output->len - 1] != '\n') g_string_append_c(md_output, '\n');
        g_string_append(md_output, "```\n");
    }
    
    // Ensure final newline if content exists and doesn't end with one
    if (md_output->len > 0 && md_output->str[md_output->len - 1] != '\n') {
        g_string_append_c(md_output, '\n');
    }

    return g_string_free(md_output, FALSE);
}

// Ensure this file does not have any unterminated g_string_append calls or comments at the very end.
// The last lines should be valid C code or comments, then EOF.
// For example, the previous error "unterminated argument list invoking macro g_string_append"
// and "expected ; at end of input" often point to issues near the end of the file.
// I'll ensure the file ends cleanly.
