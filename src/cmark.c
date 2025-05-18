#include "gtktext_cmark.h"
#include <string.h>
#include <stdio.h>
#include <adwaita.h>

// Macro to silence unused variable warnings
#define GTKTEXT_UNUSED __attribute__((unused))

// Helper function to get or create a tag
static GtkTextTag* get_or_create_tag(GtkTextBuffer *buffer, const char *tag_name) {
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *tag = gtk_text_tag_table_lookup(tag_table, tag_name);

    if (!tag) {
        if (strcmp(tag_name, "bold") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
        } else if (strcmp(tag_name, "italic") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
        } else if (strncmp(tag_name, "h", 1) == 0 && strlen(tag_name) == 2 && tag_name[1] >= '1' && tag_name[1] <= '6') {
            // Create heading tags H1-H6
            int level = tag_name[1] - '0'; // Convert '1'-'6' to int 1-6
            double scale = 1.0;
            switch (level) {
                case 1: scale = PANGO_SCALE_XX_LARGE; break; // Typically 2.0 or 2.4
                case 2: scale = PANGO_SCALE_X_LARGE; break;  // Typically 1.5 or 1.8
                case 3: scale = PANGO_SCALE_LARGE; break;    // Typically 1.2 or 1.4
                case 4: scale = PANGO_SCALE_MEDIUM; break;   // Default size, but bold
                case 5: scale = PANGO_SCALE_SMALL; break;    // Typically 0.8
                case 6: scale = PANGO_SCALE_X_SMALL; break;  // Typically 0.7
            }
            tag = gtk_text_buffer_create_tag(buffer, tag_name,
                                             "weight", PANGO_WEIGHT_BOLD,
                                             "scale", scale,
                                             NULL);
        } else if (strcmp(tag_name, "code") == 0) {
            // Check the current theme to adapt the code styling
            AdwStyleManager *style_manager = adw_style_manager_get_default();
            gboolean is_dark = adw_style_manager_get_dark(style_manager);
            
            if (is_dark) {
                // Dark theme - darker background, light text
                tag = gtk_text_buffer_create_tag(buffer, "code", 
                    "family", "monospace", 
                    "background", "#303030", // Darker background for dark theme
                    "foreground", "#e0e0e0", // Lighter text for dark theme
                    "background-full-height", TRUE,
                    "left-margin", 4, 
                    "right-margin", 4,
                    "pixels-above-lines", 1,
                    "pixels-below-lines", 1,
                    NULL);
            } else {
                // Light theme - light grey background, default text
                tag = gtk_text_buffer_create_tag(buffer, "code", 
                    "family", "monospace", 
                    "background", "#f1f1f1", // Light grey background
                    "background-full-height", TRUE,
                    "left-margin", 4, 
                    "right-margin", 4,
                    "pixels-above-lines", 1,
                    "pixels-below-lines", 1,
                    NULL);
            }
        } else if (strcmp(tag_name, "codeblock") == 0) {
            // For code blocks - full background styling with theme awareness
            AdwStyleManager *style_manager = adw_style_manager_get_default();
            gboolean is_dark = adw_style_manager_get_dark(style_manager);
            
            if (is_dark) {
                // Dark theme styling
                tag = gtk_text_buffer_create_tag(buffer, "codeblock", 
                    "family", "monospace", 
                    "background", "#303030", // Darker background for dark theme
                    "paragraph-background", "#303030",
                    "foreground", "#e0e0e0", // Lighter text for dark theme
                    "left-margin", 12,
                    "right-margin", 12,
                    "pixels-above-lines", 6,
                    "pixels-below-lines", 6,
                    "wrap-mode", GTK_WRAP_NONE,
                    NULL);
            } else {
                // Light theme styling
                tag = gtk_text_buffer_create_tag(buffer, "codeblock", 
                    "family", "monospace", 
                    "background", "#f1f1f1", // Light grey background
                    "paragraph-background", "#f1f1f1", 
                    "left-margin", 12,
                    "right-margin", 12,
                    "pixels-above-lines", 6,
                    "pixels-below-lines", 6,
                    "wrap-mode", GTK_WRAP_NONE,
                    NULL);
            }
        } else if (strcmp(tag_name, "hr") == 0) {
            // Horizontal rule: thin grey line
            tag = gtk_text_buffer_create_tag(buffer, "hr",
                "pixels-above-lines", 8,
                "pixels-below-lines", 8,
                "paragraph-background", NULL, // No background
                NULL);
            // The actual drawing of the line will be handled by inserting a special unicode or a line of dashes,
            // but the tag can be used for future custom drawing if needed.
        }
        // Add other tags as needed
    }
    
    return tag;
}


// Forward declaration for the recursive helper
static void apply_tags_for_node_recursive(cmark_node *node, GtkTextBuffer *buffer, GtkTextIter *iter, GSList *active_tags);

// Helper to apply tags from a GSList of tag names
// Inserts text and then applies all specified tags to the inserted range.
static void insert_with_active_tags(GtkTextBuffer *buffer, GtkTextIter *iter, const char *text, GSList *active_tags) {
    if (!text || strlen(text) == 0) return;

    // Create a mark at the current iterator position (before insertion)
    // Use a left-gravity mark so it stays at the beginning of inserted text.
    GtkTextMark *start_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);

    // Insert text. 'iter' is updated to the end of the inserted text.
    gtk_text_buffer_insert(buffer, iter, text, -1);

    // Apply tags to the range [mark, *iter)
    if (active_tags) {
        GtkTextIter start_insert_iter;
        // Get a fresh iterator to the mark (this is the start of the inserted text)
        gtk_text_buffer_get_iter_at_mark(buffer, &start_insert_iter, start_mark);

        GSList *l;
        for (l = active_tags; l != NULL; l = l->next) {
            const char* tag_name = (const char*)l->data;
            if (tag_name) { // Ensure tag_name is not NULL
                GtkTextTag *tag_object = get_or_create_tag(buffer, tag_name);
                if (tag_object) { // Ensure tag creation/lookup was successful
                    // Apply tags using the fresh start_insert_iter and the updated iter
                    gtk_text_buffer_apply_tag(buffer, tag_object, &start_insert_iter, iter);
                } else {
                    g_warning("Failed to get or create tag: %s", tag_name);
                }
            }
        }
    }

    // Delete the mark
    gtk_text_buffer_delete_mark(buffer, start_mark);
}


static void apply_tags_for_node_recursive(cmark_node *node, GtkTextBuffer *buffer, GtkTextIter *iter, GSList *active_tags) {
    if (!node) return;

    // Ensure standard tags are created if they don't exist yet.
    // This is a good place to ensure common tags are available.
    get_or_create_tag(buffer, "bold");
    get_or_create_tag(buffer, "italic");
    for (int i = 1; i <= 6; i++) {
        char h_tag[3];
        g_snprintf(h_tag, sizeof(h_tag), "h%d", i);
        get_or_create_tag(buffer, h_tag);
    }
    get_or_create_tag(buffer, "code");
    get_or_create_tag(buffer, "codeblock");


    cmark_node *child;
    for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
        cmark_node_type type = cmark_node_get_type(child);
        GSList *current_tags = active_tags;
        gboolean pushed_tag = FALSE;
        char *tag_name_alloc = NULL; // For dynamically created tag names like "h1"

        switch (type) {
            case CMARK_NODE_TEXT:
            case CMARK_NODE_CODE: // Inline code
                {
                    const char *literal = cmark_node_get_literal(child);
                    if (literal) {
                        GSList *tags_for_this_segment = active_tags;
                        GSList *temp_prepended_list = NULL; // To manage the lifetime of the list if "code" is added

                        if (type == CMARK_NODE_CODE) {
                            // For inline code, prepend the "code" tag to the current list of active tags.
                            // The "code" tag itself should already be created by get_or_create_tag.
                            get_or_create_tag(buffer, "code"); // Ensure "code" tag exists
                            temp_prepended_list = g_slist_prepend(active_tags, (gpointer)"code");
                            tags_for_this_segment = temp_prepended_list;
                        }
                        
                        insert_with_active_tags(buffer, iter, literal, tags_for_this_segment);
                        
                        if (temp_prepended_list) {
                            // Free only the list cell we added, not the data (string literal "code")
                            // and not the rest of the list (original active_tags).
                            g_slist_free_1(temp_prepended_list);
                        }
                    }
                }
                break;
            case CMARK_NODE_HTML_INLINE:
                {
                    const char *literal = cmark_node_get_literal(child);
                    if (literal) {
                        insert_with_active_tags(buffer, iter, literal, current_tags);
                    }
                }
                break;
            case CMARK_NODE_STRONG:
                current_tags = g_slist_prepend(current_tags, (gpointer)"bold");
                pushed_tag = TRUE;
                get_or_create_tag(buffer, "bold"); // Ensure tag exists
                apply_tags_for_node_recursive(child, buffer, iter, current_tags);
                break;
            case CMARK_NODE_EMPH:
                current_tags = g_slist_prepend(current_tags, (gpointer)"italic");
                pushed_tag = TRUE;
                get_or_create_tag(buffer, "italic"); // Ensure tag exists
                apply_tags_for_node_recursive(child, buffer, iter, current_tags);
                break;
            case CMARK_NODE_HEADING:
                {
                    int level = cmark_node_get_heading_level(child);
                    tag_name_alloc = g_strdup_printf("h%d", level);
                    current_tags = g_slist_prepend(current_tags, (gpointer)tag_name_alloc);
                    pushed_tag = TRUE;
                    get_or_create_tag(buffer, tag_name_alloc); // Ensure tag exists
                    apply_tags_for_node_recursive(child, buffer, iter, current_tags);
                    gtk_text_buffer_insert(buffer, iter, "\n\n", -1); // Ensure two newlines after heading
                }
                break;
            case CMARK_NODE_THEMATIC_BREAK:
                // Insert a horizontal rule (HR)
                get_or_create_tag(buffer, "hr");
                // Insert a line of dashes (or a unicode line) and apply the hr tag
                insert_with_active_tags(buffer, iter, "\u2014\u2014\u2014\n", g_slist_prepend(NULL, (gpointer)"hr"));
                // Removed extra newline after HR
                break;
            case CMARK_NODE_PARAGRAPH:
                apply_tags_for_node_recursive(child, buffer, iter, current_tags);
                // Ensure block elements like paragraphs (and headings) are followed by a single newline
                // in the buffer, rather than two, to prevent excessive blank lines in round-tripped Markdown.
                if (cmark_node_parent(node) && cmark_node_get_type(cmark_node_parent(node)) != CMARK_NODE_ITEM) {
                     gtk_text_buffer_insert(buffer, iter, "\n", -1); // Changed from \\n\\n
                } else if (!cmark_node_parent(node)) { // Root document's direct child paragraph
                     gtk_text_buffer_insert(buffer, iter, "\n", -1); // Changed from \\n\\n
                } else { // Paragraph within a list item, etc.
                    // No extra newline needed here by the paragraph itself
                }
                break;
            case CMARK_NODE_LIST:
                apply_tags_for_node_recursive(child, buffer, iter, current_tags);
                // Add a newline after the entire list if it's not followed by one.
                // Paragraphs/headings after list will add their own \n\n.
                // This ensures list itself is separated if it's the last item.
                // if (!gtk_text_iter_ends_line(iter)) {
                //    gtk_text_buffer_insert(buffer, iter, "\n", -1);
                // }
                break;
            case CMARK_NODE_ITEM:
                {
                    cmark_node *parent_list = cmark_node_parent(child); // Corrected function name
                    if (parent_list) { // Should always have a parent if it\'s an item
                        cmark_list_type lt = cmark_node_get_list_type(parent_list);
                        if (lt == CMARK_BULLET_LIST) {
                            insert_with_active_tags(buffer, iter, "* ", current_tags);
                        } else if (lt == CMARK_ORDERED_LIST) {
                            // cmark doesn't easily give the item number here for CommonMark.
                            // For simplicity, using "1."
                            // A more complex renderer could count items.
                            insert_with_active_tags(buffer, iter, "1. ", current_tags);
                        }
                    }
                    apply_tags_for_node_recursive(child, buffer, iter, current_tags);
                    // Newline after item content is usually handled by paragraph inside item.
                    // If an item is the last in a list, and the list is the last in doc, ensure newline.
                    // The paragraph inside item will add \n. If item is simple text, it might need one.
                    if (!gtk_text_iter_ends_line(iter)) {
                         // gtk_text_buffer_insert(buffer, iter, "\n", -1);
                    }
                }
                break;
            case CMARK_NODE_CODE_BLOCK:
                {
                    const char *code_content = cmark_node_get_literal(child);
                    const char *info = cmark_node_get_fence_info(child);
                    char *fence_info_str = (info && strlen(info) > 0) ? g_strdup(info) : g_strdup("");
                    
                    // Create a mark to track where the code block starts
                    GtkTextMark *codeblock_start = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                    
                    if (code_content && strlen(code_content) > 0) {
                        // For the content of the code block, apply only the "codeblock" tag.
                        // Do not inherit other tags like bold/italic into code blocks.
                        GSList* code_block_specific_tags = NULL;
                        get_or_create_tag(buffer, "codeblock"); // Ensure "codeblock" tag exists
                        code_block_specific_tags = g_slist_prepend(code_block_specific_tags, (gpointer)"codeblock");
                        
                        insert_with_active_tags(buffer, iter, code_content, code_block_specific_tags);
                        
                        g_slist_free_1(code_block_specific_tags); // Free the list cell

                        // If the content doesn't end with a newline, we add one
                        if (code_content[strlen(code_content)-1] != '\n') { 
                           insert_with_active_tags(buffer, iter, "\n", NULL);
                        }
                    }
                    
                    // Delete the mark - we used it to track where the code block started
                    gtk_text_buffer_delete_mark(buffer, codeblock_start);
                    
                    g_free(fence_info_str);
                }
                break;
            case CMARK_NODE_LINEBREAK:
                insert_with_active_tags(buffer, iter, "\n", current_tags); // Hard break
                break;
            case CMARK_NODE_SOFTBREAK:
                insert_with_active_tags(buffer, iter, " ", current_tags); // Render softbreak as a space (CommonMark compliant)
                break;
            default:
                apply_tags_for_node_recursive(child, buffer, iter, current_tags);
                break;
        }

        if (pushed_tag) {
            current_tags = g_slist_remove(current_tags, current_tags->data);
        }
        g_free(tag_name_alloc); // Free strduped "hX" tag name
    }
}


gboolean import_markdown_to_buffer_cmark(GtkTextBuffer *buffer, const char *markdown_text) {
    if (!buffer || !markdown_text) {
        return FALSE;
    }
    
    get_or_create_tag(buffer, "bold");
    get_or_create_tag(buffer, "italic");
    for (int i = 1; i <= 6; i++) {
        char h_tag[3];
        g_snprintf(h_tag, sizeof(h_tag), "h%d", i);
        get_or_create_tag(buffer, h_tag);
    }
    
    // Check if code and codeblock tags already exist
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    gboolean code_tag_exists = (gtk_text_tag_table_lookup(tag_table, "code") != NULL);
    gboolean codeblock_tag_exists = (gtk_text_tag_table_lookup(tag_table, "codeblock") != NULL);
    
    // Only recreate tags if they don't exist - avoid removing tags that were just created by update_code_tags_for_theme
    if (!code_tag_exists) {
        get_or_create_tag(buffer, "code");
    } 
    
    if (!codeblock_tag_exists) {
        get_or_create_tag(buffer, "codeblock");
    }

    GtkTextIter start_iter, end_iter;
    gtk_text_buffer_get_bounds(buffer, &start_iter, &end_iter);
    gtk_text_buffer_delete(buffer, &start_iter, &end_iter);

    // CMARK_OPT_SMART enables smart quotes, dashes, etc.
    int options = CMARK_OPT_DEFAULT | CMARK_OPT_SMART;
    cmark_node *document = cmark_parse_document(markdown_text, strlen(markdown_text), options);
    if (!document) {
        return FALSE;
    }

    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);
    apply_tags_for_node_recursive(document, buffer, &iter, NULL);

    cmark_node_free(document);
    return TRUE;
}

/**
 * Update code-related tags to match the current theme
 * 
 * @param buffer The GtkTextBuffer to update tags in
 */
void update_code_tags_for_theme(GtkTextBuffer *buffer) {

    AdwStyleManager *style_manager = adw_style_manager_get_default();
    AdwColorScheme color_scheme = adw_style_manager_get_color_scheme(style_manager);
    gboolean is_dark = (color_scheme == ADW_COLOR_SCHEME_FORCE_DARK);

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *code_tag = gtk_text_tag_table_lookup(tag_table, "code");
    GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");

    // Update code tag properties
    if (code_tag) {
        if (is_dark) {
            g_object_set(code_tag,
                         "background", "#303030",
                         "foreground", "#e0e0e0",
                         NULL);
        } else {
            g_object_set(code_tag,
                         "background", "#f1f1f1",
                         "foreground", NULL, // Reset to default text color
                         NULL);
        }
    } else {
        if (is_dark) {
            gtk_text_buffer_create_tag(buffer, "code",
                                       "family", "monospace",
                                       "background", "#303030",
                                       "foreground", "#e0e0e0",
                                       "background-full-height", TRUE,
                                       "left-margin", 4,
                                       "right-margin", 4,
                                       "pixels-above-lines", 1,
                                       "pixels-below-lines", 1,
                                       NULL);
        } else {
            gtk_text_buffer_create_tag(buffer, "code",
                                       "family", "monospace",
                                       "background", "#f1f1f1",
                                       "background-full-height", TRUE,
                                       "left-margin", 4,
                                       "right-margin", 4,
                                       "pixels-above-lines", 1,
                                       "pixels-below-lines", 1,
                                       NULL);
        }
    }

    // Update codeblock tag properties
    if (codeblock_tag) {
        if (is_dark) {
            g_object_set(codeblock_tag,
                         "background", "#303030",
                         "paragraph-background", "#303030",
                         "foreground", "#e0e0e0",
                         NULL);
        } else {
            g_object_set(codeblock_tag,
                         "background", "#f1f1f1",
                         "paragraph-background", "#f1f1f1",
                         "foreground", NULL, // Reset to default text color
                         NULL);
        }
    } else {
        if (is_dark) {
            gtk_text_buffer_create_tag(buffer, "codeblock",
                                       "family", "monospace",
                                       "background", "#303030",
                                       "paragraph-background", "#303030",
                                       "foreground", "#e0e0e0",
                                       "left-margin", 12,
                                       "right-margin", 12,
                                       "pixels-above-lines", 6,
                                       "pixels-below-lines", 6,
                                       "wrap-mode", GTK_WRAP_NONE,
                                       NULL);
        } else {
            gtk_text_buffer_create_tag(buffer, "codeblock",
                                       "family", "monospace",
                                       "background", "#f1f1f1",
                                       "paragraph-background", "#f1f1f1",
                                       "left-margin", 12,
                                       "right-margin", 12,
                                       "pixels-above-lines", 6,
                                       "pixels-below-lines", 6,
                                       "wrap-mode", GTK_WRAP_NONE,
                                       NULL);
        }
    }
    
}

char* export_buffer_to_markdown_cmark(GtkTextBuffer *buffer) {
    if (!buffer) {
        return g_strdup("");
    }

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);

    if (gtk_text_iter_equal(&start, &end)) {
        return g_strdup("");
    }
    
    GString *md = g_string_new("");
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);

    gboolean currently_in_bold = FALSE;
    gboolean currently_in_italic = FALSE;
    gboolean currently_in_code = FALSE;
    gboolean currently_in_codeblock = FALSE;
    gboolean at_line_start = TRUE;

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

    while(!gtk_text_iter_is_end(&iter)) {
        if (at_line_start) {
            // Track if we found and processed a heading
            GTKTEXT_UNUSED gboolean heading_found = FALSE;
            for (int i = 1; i <= 6; ++i) {
                char h_tag_name[4];
                g_snprintf(h_tag_name, sizeof(h_tag_name), "h%d", i);
                GtkTextTag *h_tag = gtk_text_tag_table_lookup(tag_table, h_tag_name);
                if (h_tag && gtk_text_iter_has_tag(&iter, h_tag)) {
                    for (int j = 0; j < i; ++j) g_string_append_c(md, '#');
                    g_string_append_c(md, ' ');
                    heading_found = TRUE;
                    break; 
                }
            }
            
            // Check for code block (if this text has the codeblock tag and we're at the start of a line)
            GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");
            if (codeblock_tag && gtk_text_iter_has_tag(&iter, codeblock_tag) && !currently_in_codeblock) {
                // This is the start of a code block, add opening fence
                g_string_append(md, "```\n");
                currently_in_codeblock = TRUE;
            }
            
            // Check for horizontal rule (hr) tag at line start
            GtkTextTag *hr_tag = gtk_text_tag_table_lookup(tag_table, "hr");
            if (hr_tag && gtk_text_iter_has_tag(&iter, hr_tag)) {
                // Check if we need to remove a trailing newline from the previous content
                // This prevents an extra blank line from appearing before the horizontal rule
                if (md->len > 0 && md->str[md->len-1] == '\n') {
                    // If the last char was a newline, check if the second-to-last was also a newline
                    if (md->len > 1 && md->str[md->len-2] == '\n') {
                        // We have two consecutive newlines - remove one to avoid the extra blank line
                        g_string_truncate(md, md->len - 1);
                    }
                }
                
                // Add the horizontal rule
                g_string_append(md, "---\n");
                
                // Advance to the end of the line (skip the HR line)
                while (!gtk_text_iter_is_end(&iter) && !gtk_text_iter_ends_line(&iter)) {
                    gtk_text_iter_forward_char(&iter);
                }
                // Move past the newline
                if (!gtk_text_iter_is_end(&iter)) {
                    gtk_text_iter_forward_char(&iter);
                }
                at_line_start = TRUE; // Explicitly set true after consuming a whole line block for HR
                continue;
            }
        }

        GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
        gboolean iter_is_bold = FALSE;
        gboolean iter_is_italic = FALSE;
        gboolean iter_is_code = FALSE;
        gboolean iter_is_codeblock = FALSE;

        for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(l->data);
            const char *name = NULL;
            gchar* prop_name = NULL;
            g_object_get(tag, "name", &prop_name, NULL);
            name = prop_name;

            if (name) {
                if (g_strcmp0(name, "bold") == 0) iter_is_bold = TRUE;
                else if (g_strcmp0(name, "italic") == 0) iter_is_italic = TRUE;
                else if (g_strcmp0(name, "code") == 0) iter_is_code = TRUE;
                else if (g_strcmp0(name, "codeblock") == 0) iter_is_codeblock = TRUE;
            }
            g_free(prop_name);
        }
        g_slist_free(tags_at_iter);

        // This variable tracks if we made a style change on this character
        // which can be useful for debugging and future extensions
        GTKTEXT_UNUSED gboolean style_transition = FALSE;
        
        // Special case: If we're starting both bold and italic at the same time, use combined marker
        if (iter_is_bold && iter_is_italic && !currently_in_bold && !currently_in_italic) {
            g_string_append(md, "***");
            currently_in_bold = TRUE;
            currently_in_italic = TRUE;
            style_transition = TRUE;
        }
        // Special case: If we're ending both bold and italic at the same time, use combined marker
        else if (!iter_is_bold && !iter_is_italic && currently_in_bold && currently_in_italic) {
            g_string_append(md, "***");
            currently_in_bold = FALSE;
            currently_in_italic = FALSE;
            style_transition = TRUE;
        }
        else {
            // Handle bold state transition
            if (iter_is_bold && !currently_in_bold) {
                g_string_append(md, "**");
                currently_in_bold = TRUE;
                style_transition = TRUE;
            } else if (!iter_is_bold && currently_in_bold) {
                if (md->len > 0 && (md->str[md->len-1] != ' ' && md->str[md->len-1] != '*')) {
                    g_string_append(md, "**");
                } else if (md->len == 0 || (md->str[md->len-1] == ' ' || md->str[md->len-1] == '*')) {
                    // If previous was space or another markdown char, or empty string, append.
                    // This logic needs to be robust against creating invalid markdown like "****" for empty bold.
                } else {
                    g_string_append(md, "**");
                }
                currently_in_bold = FALSE;
                style_transition = TRUE;
            }

            // Handle italic state transition
            if (iter_is_italic && !currently_in_italic) {
                g_string_append(md, "*");
                currently_in_italic = TRUE;
                style_transition = TRUE;
            } else if (!iter_is_italic && currently_in_italic) {
                if (md->len > 0 && (md->str[md->len-1] != ' ' && md->str[md->len-1] != '*')) {
                    g_string_append(md, "*");
                } else if (md->len == 0 || (md->str[md->len-1] == ' ' || md->str[md->len-1] == '*')) {
                    // Similar logic as bold
                } else {
                    g_string_append(md, "*");
                }
                currently_in_italic = FALSE;
                style_transition = TRUE;
            }
        }
        
        // Handle code span transitions
        if (iter_is_code && !currently_in_code) {
            g_string_append(md, "`");
            currently_in_code = TRUE;
            style_transition = TRUE;
        } else if (!iter_is_code && currently_in_code) {
            g_string_append(md, "`");
            currently_in_code = FALSE;
            style_transition = TRUE;
        }
        
        gunichar c = gtk_text_iter_get_char(&iter);
        if (c != 0) { // 0 indicates end of buffer or invalid char
            // Inside codeblocks and code spans, just append characters verbatim
            if (iter_is_codeblock || iter_is_code) {
                g_string_append_unichar(md, c);
                
                // Check for codeblock state transitions
                if (iter_is_codeblock && !currently_in_codeblock) {
                    // We're entering a code block (should be handled at line start)
                    currently_in_codeblock = TRUE;
                }
            } else if (currently_in_codeblock) {
                // We're exiting a code block mid-stream
                
                // Since we're crossing a tag boundary, we should close the code block now
                // Make sure there's exactly one newline before the fence
                if (md->len > 0 && md->str[md->len-1] == '\n') {
                    g_string_append(md, "```\n");
                } else {
                    g_string_append(md, "\n```\n");
                }
                
                currently_in_codeblock = FALSE;
                
                // Now append the current character
                g_string_append_unichar(md, c);
            }
            // Revised Markdown escaping logic for normal text (not in codeblock)
            else if ((c == '*' || c == '_') && (iter_is_bold || iter_is_italic)) {
                // If the character is a markdown control character AND it's part of a recognized tag,
                // assume it's part of the content and not a new markdown sequence to be escaped.
                g_string_append_unichar(md, c);
            } else if (c == '`' && iter_is_code) {
                // If we have a backtick within a code span, we need to just append it
                // as it's part of the content, not a closing marker
                g_string_append_unichar(md, c);
            } else if (c == '#' && at_line_start && 
                      gtk_text_iter_has_tag(&iter, gtk_text_tag_table_lookup(tag_table, "h1"))) {
                // Similar logic for headings - if it's a # at line start and has a heading tag,
                // it's part of the heading structure, not content to be escaped.
                g_string_append_unichar(md, c);
            } else if (c == '\\' && !iter_is_code && !iter_is_codeblock) {
                // Only escape backslashes outside of code blocks/spans
                // Let's do a simple check to avoid double escaping
                if (gtk_text_iter_get_offset(&iter) + 1 < gtk_text_buffer_get_char_count(buffer)) {
                    GtkTextIter next_iter = iter;
                    gtk_text_iter_forward_char(&next_iter);
                    gunichar next_c = gtk_text_iter_get_char(&next_iter);
                    
                    // Only escape backslash when it's followed by a character that needs escaping
                    if (next_c == '*' || next_c == '_' || next_c == '`' || next_c == '\\') {
                        g_string_append_c(md, '\\');
                    }
                }
                g_string_append_unichar(md, c);
            } else if ((c == '[' || c == ']' || c == '!') && !iter_is_code && !iter_is_codeblock) {
                // Other common characters that often need escaping outside code blocks
                g_string_append_unichar(md, c);
            } else if (c == '`' && !iter_is_code && !iter_is_codeblock) {
                // For backticks outside of code blocks, don't escape them - this was causing issues
                g_string_append_unichar(md, c);
            } else {
                // For other characters, append as is
                g_string_append_unichar(md, c);
            }
        }

        at_line_start = gtk_text_iter_ends_line(&iter);
        if (!gtk_text_iter_forward_char(&iter)) {
            break; // End of buffer
        }
    }

    // Close any open tags at the very end
    // If both bold and italic are open, close them with combined marker
    if (currently_in_bold && currently_in_italic) {
        g_string_append(md, "***"); // Combined close for both
        currently_in_bold = FALSE;
        currently_in_italic = FALSE;
    } else {
        // Close in reverse order of precedence: codeblock, code, italic, bold
        if (currently_in_codeblock) {
            // For codeblocks, add a closing fence without adding extra newlines
            // Trim any trailing newlines to avoid accumulation
            if (md->len > 0 && md->str[md->len-1] == '\n') {
                // There's already a newline at the end, just add the fence
                g_string_append(md, "```\n");
            } else {
                // No newline at the end, add one before the fence
                g_string_append(md, "\n```\n");
            }
            currently_in_codeblock = FALSE;
        }
        if (currently_in_code) g_string_append(md, "`");
        if (currently_in_italic) g_string_append(md, "*");
        if (currently_in_bold) g_string_append(md, "**");
    }
    
    // Ensure the exported markdown ends with a newline if the buffer wasn't empty.
    if (md->len > 0 && md->str[md->len-1] != '\n') {
        g_string_append_c(md, '\n');
    }

    return g_string_free(md, FALSE); // FALSE to return the char* (caller owns it)
}