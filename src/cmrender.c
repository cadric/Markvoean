#include "cmrender.h"
// #include "gtktext_cmark.h" // Removed as per plan
#include <adwaita.h> // For AdwStyleManager
#include <gtk/gtk.h> // Include full gtk.h for all required functions
#include <libsoup/soup.h> // For image fetching
#include <string.h>
#include <stdio.h>
#include <cmark.h> // Ensure cmark functions are declared

// Forward declare the image fetch callback type to match main.c pattern
typedef void (*ImageFetchCallback)(GdkPixbuf *pixbuf, GError *error, gpointer user_data);

// Structure to hold parameters needed for immediate image fetching during rendering
typedef struct {
    SoupSession *soup_session;
    GtkTextView *text_view;
} ImageFetchContext;

// Global context for image fetching during rendering
static ImageFetchContext *g_image_fetch_context = NULL;

// Helper structure for image widget async operations
typedef struct {
    GtkPicture *picture;
    char *url;
    char *alt_text;
} ImageWidgetData;

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

// Callback function for image fetching
static void on_image_widget_fetched(SoupSession *session, GAsyncResult *res, gpointer user_data) {
    ImageWidgetData *data = (ImageWidgetData *)user_data;
    GError *error = NULL;
    GBytes *bytes = soup_session_send_and_read_finish(session, res, &error);
    
    if (!bytes) {
        g_debug("[image] Image fetch failed for %s: %s", data->url, error ? error->message : "unknown");
        g_clear_error(&error);
        g_free(data->url);
        g_free(data->alt_text);
        g_free(data);
        return;
    }
    
    // Create pixbuf from downloaded data
    gsize size = 0;
    const guint8 *image_data = g_bytes_get_data(bytes, &size);
    GInputStream *stream = g_memory_input_stream_new_from_data(image_data, size, NULL);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &error);
    g_object_unref(stream);
    
    if (pixbuf) {
        // Convert GdkPixbuf to GdkTexture and display
        GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
        gtk_picture_set_paintable(data->picture, GDK_PAINTABLE(texture));
        g_object_unref(texture);
        g_object_unref(pixbuf);
        g_debug("[image] Successfully loaded image: %s", data->url);
    } else {
        g_debug("[image] Failed to decode image %s: %s", data->url, error ? error->message : "unknown");
        g_clear_error(&error);
    }
    
    g_bytes_unref(bytes);
    
    // Clean up our custom data structure
    g_free(data->url);
    g_free(data->alt_text);
    g_free(data);
}

// Create an image widget that immediately starts fetching
// Callback to handle when the image loads and adjust sizing
static void on_picture_notify_paintable(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    (void)user_data;
    GtkPicture *picture = GTK_PICTURE(object);
    GdkPaintable *paintable = gtk_picture_get_paintable(picture);
    
    if (paintable) {
        int width = gdk_paintable_get_intrinsic_width(paintable);
        int height = gdk_paintable_get_intrinsic_height(paintable);
        
        g_debug("[image] Picture loaded: %dx%d", width, height);
        
        // Set reasonable maximum dimensions while preserving aspect ratio
        int max_width = 600;   // Maximum width in pixels
        int max_height = 400;  // Maximum height in pixels
        
        // Calculate scaling factor to fit within max dimensions
        double scale_x = (double)max_width / width;
        double scale_y = (double)max_height / height;
        double scale = MIN(scale_x, scale_y);
        
        if (scale < 1.0) {
            // Image is larger than max, scale it down
            int new_width = (int)(width * scale);
            int new_height = (int)(height * scale);
            gtk_widget_set_size_request(GTK_WIDGET(picture), new_width, new_height);
            g_debug("[image] Scaled image to: %dx%d (scale: %.2f)", new_width, new_height, scale);
        } else {
            // Image fits within max dimensions, use natural size
            gtk_widget_set_size_request(GTK_WIDGET(picture), width, height);
            g_debug("[image] Using natural size: %dx%d", width, height);
        }
    }
}

static GtkWidget *create_image_widget(const char *alt_text, const char *url) {
    g_debug("[image-widget] Creating image widget for URL: %s", url ? url : "(null)");
    
    // Create a box container for the image and optional caption
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    
    // Store metadata on the box widget for export purposes
    if (url) {
        g_object_set_data_full(G_OBJECT(box), "image-url", g_strdup(url), (GDestroyNotify)g_free);
    }
    if (alt_text) {
        g_object_set_data_full(G_OBJECT(box), "image-alt", g_strdup(alt_text), (GDestroyNotify)g_free);
    }
    
    // Create the picture widget
    GtkWidget *picture = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_SCALE_DOWN);
    // Start with a small size, will be adjusted when image loads
    gtk_widget_set_size_request(picture, 50, 50);
    gtk_widget_set_halign(picture, GTK_ALIGN_START);
    gtk_widget_set_valign(picture, GTK_ALIGN_START);
    
    // Connect signal to adjust size when image loads
    g_signal_connect(picture, "notify::paintable", G_CALLBACK(on_picture_notify_paintable), NULL);
    
    gtk_box_append(GTK_BOX(box), picture);
    
    // Add caption if provided
    if (alt_text && strlen(alt_text) > 0) {
        GtkWidget *caption = gtk_label_new(alt_text);
        gtk_widget_add_css_class(caption, "caption");
        gtk_widget_add_css_class(caption, "dim-label");
        gtk_label_set_wrap(GTK_LABEL(caption), TRUE);
        gtk_box_append(GTK_BOX(box), caption);
    }
    
    // Immediately start fetching if we have the necessary components
    if (g_image_fetch_context && g_image_fetch_context->soup_session && url && g_image_fetch_context->text_view) {
        SoupMessage *msg = soup_message_new("GET", url);
        if (msg) {
            // Set user agent and accept headers like the existing code
            SoupMessageHeaders *headers = soup_message_get_request_headers(msg);
            soup_message_headers_replace(headers, "User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36 GTKText/0.1");
            soup_message_headers_replace(headers, "Accept", "image/*,*/*;q=0.5");
            
            // Create callback data
            ImageWidgetData *img_data = g_new(ImageWidgetData, 1);
            img_data->picture = GTK_PICTURE(picture);
            img_data->url = g_strdup(url);
            img_data->alt_text = g_strdup(alt_text);
            
            // Start async fetch immediately
            g_debug("[image] Starting fetch for: %s", url);
            soup_session_send_and_read_async(g_image_fetch_context->soup_session, msg,
                                            G_PRIORITY_DEFAULT, NULL,
                                            (GAsyncReadyCallback)on_image_widget_fetched,
                                            img_data);
            g_object_unref(msg);
        } else {
            g_debug("[image] Failed to create SoupMessage for URL: %s", url);
        }
    }
    
    return box;
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
G_GNUC_UNUSED static void close_inline_tags_from_stack(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, gboolean force_close_all);
G_GNUC_UNUSED static void open_inline_tags_for_segment(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, G_GNUC_UNUSED GtkTextBuffer *buffer);
// static char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer); // Declaration removed, will be non-static

// Forward declaration for the recursive helper
// Maintain a stack of ordered-list counters to support nesting.
static void cm_render_node_content_recursive(cmark_node *node, GtkTextBuffer *buffer, GtkTextIter *iter, GSList *active_tags, GArray *ol_counter_stack);
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
        } else if (g_str_has_prefix(tag_name, "blockquote")) {
            // Support nested blockquotes by creating tags named
            // "blockquote1", "blockquote2", ... with identical styling.
            // Using left-margin provides clearer block-level indentation.
            tag = gtk_text_buffer_create_tag(buffer, tag_name,
                                             "left-margin", 20,
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
// Implements proper CommonMark block-level newline management.
// The 'ordered_list_item_counter_ptr' is used to pass and update the current item number for ordered lists.
static void cm_render_node_content_recursive(cmark_node *node, GtkTextBuffer *buffer, GtkTextIter *iter, GSList *active_tags, GArray *ol_counter_stack) {
    if (!node) return;

    cmark_node_type type = cmark_node_get_type(node);
    
    // Debug: Log what node types we're processing
    static const char* node_type_names[] = {
        [CMARK_NODE_DOCUMENT] = "DOCUMENT",
        [CMARK_NODE_BLOCK_QUOTE] = "BLOCK_QUOTE", 
        [CMARK_NODE_LIST] = "LIST",
        [CMARK_NODE_ITEM] = "ITEM",
        [CMARK_NODE_CODE_BLOCK] = "CODE_BLOCK",
        [CMARK_NODE_HTML_BLOCK] = "HTML_BLOCK",
        [CMARK_NODE_CUSTOM_BLOCK] = "CUSTOM_BLOCK",
        [CMARK_NODE_PARAGRAPH] = "PARAGRAPH",
        [CMARK_NODE_HEADING] = "HEADING",
        [CMARK_NODE_THEMATIC_BREAK] = "THEMATIC_BREAK",
        [CMARK_NODE_TEXT] = "TEXT",
        [CMARK_NODE_SOFTBREAK] = "SOFTBREAK",
        [CMARK_NODE_LINEBREAK] = "LINEBREAK",
        [CMARK_NODE_CODE] = "CODE",
        [CMARK_NODE_HTML_INLINE] = "HTML_INLINE",
        [CMARK_NODE_CUSTOM_INLINE] = "CUSTOM_INLINE",
        [CMARK_NODE_EMPH] = "EMPH",
        [CMARK_NODE_STRONG] = "STRONG",
        [CMARK_NODE_LINK] = "LINK",
        [CMARK_NODE_IMAGE] = "IMAGE"
    };
    
    const char* type_name = (type < sizeof(node_type_names)/sizeof(node_type_names[0]) && node_type_names[type]) 
                            ? node_type_names[type] : "UNKNOWN";
    g_debug("[node] Processing node type: %s (%d)", type_name, type);
    
    char *tag_name_alloc = NULL;
    char tag_name_buffer[4]; // Buffer for constructing tag names like "h1", "h2", etc.
    gboolean is_block_node = FALSE;
    gboolean needs_trailing_newline = FALSE;
    gboolean needs_paragraph_separation = FALSE;

    // Determine if current node is a block node and what kind of spacing it needs
    switch (type) {
        case CMARK_NODE_DOCUMENT:
            is_block_node = TRUE;
            needs_trailing_newline = FALSE; // Document doesn't need trailing newline
            break;
        case CMARK_NODE_BLOCK_QUOTE:
        case CMARK_NODE_CODE_BLOCK:
        case CMARK_NODE_HTML_BLOCK:
        case CMARK_NODE_HEADING:
        case CMARK_NODE_THEMATIC_BREAK:
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            needs_paragraph_separation = TRUE; // These need blank lines before next paragraph
            break;
        case CMARK_NODE_PARAGRAPH:
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            // Check if next sibling is also a paragraph - if so, needs blank line
            {
                cmark_node *next_sibling = cmark_node_next(node);
                if (next_sibling && cmark_node_get_type(next_sibling) == CMARK_NODE_PARAGRAPH) {
                    needs_paragraph_separation = TRUE;
                }
            }
            break;
        case CMARK_NODE_LIST:
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            // Ensure a blank line between a list and a following paragraph
            {
                cmark_node *next_sibling = cmark_node_next(node);
                if (next_sibling && cmark_node_get_type(next_sibling) == CMARK_NODE_PARAGRAPH) {
                    needs_paragraph_separation = TRUE;
                }
            }
            break;
        case CMARK_NODE_ITEM:
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            needs_paragraph_separation = FALSE; // Items handle their own spacing
            break;
        default:
            is_block_node = FALSE;
            needs_trailing_newline = FALSE;
            needs_paragraph_separation = FALSE;
            break;
    }

    // Specific handling for node types
    switch (type) {
        case CMARK_NODE_NONE:
            // No operation for NONE type
            break;
        case CMARK_NODE_DOCUMENT:
            // No specific tag for the document itself, just process children
            break;
        case CMARK_NODE_CUSTOM_BLOCK:
        case CMARK_NODE_CUSTOM_INLINE:
            // Custom nodes not currently handled
            break;
        case CMARK_NODE_BLOCK_QUOTE: {
            // Determine current nesting depth by counting existing blockquote tags
            int current_depth = 0;
            for (GSList *l = active_tags; l != NULL; l = l->next) {
                const char *nm = (const char*)l->data;
                if (nm && g_str_has_prefix(nm, "blockquote")) current_depth++;
            }
            int new_depth = current_depth + 1;
            tag_name_alloc = g_strdup_printf("blockquote%d", new_depth);
            cm_render_get_or_create_base_tag(buffer, tag_name_alloc); // Ensure tag exists
            break;
        }
        case CMARK_NODE_LIST:
            // Push ordered list counter if ordered
            if (cmark_node_get_list_type(node) == CMARK_ORDERED_LIST) {
                // Normalize ordered lists to start at 1 for export consistency
                int start = 1;
                g_array_append_val(ol_counter_stack, start);
            }
            // No direct insertion for the list container; items will handle markers/indentation.
            break;
        case CMARK_NODE_ITEM:
            // Handled by child paragraph or directly if no paragraph
            {
                cmark_node *parent_list_node = cmark_node_parent(node);
                if (parent_list_node && cmark_node_get_type(parent_list_node) == CMARK_NODE_LIST) {
                    // Compute nesting depth (number of ancestor lists)
                    int depth = 0;
                    for (cmark_node *p = parent_list_node; p; p = cmark_node_parent(p)) {
                        if (cmark_node_get_type(p) == CMARK_NODE_LIST) depth++;
                    }
                    // Indent two spaces per depth-1
                    if (depth > 1) {
                        GString *indent = g_string_sized_new(depth * 2);
                        for (int i = 1; i < depth; i++) g_string_append(indent, "  ");
                        cm_render_insert_with_active_tags(buffer, iter, indent->str, active_tags);
                        g_string_free(indent, TRUE);
                    }

                    cmark_list_type list_type = cmark_node_get_list_type(parent_list_node);
                    if (list_type == CMARK_ORDERED_LIST) {
                        // Use stack top as current counter
                        int idx = ol_counter_stack->len - 1;
                        int num = 1;
                        if (idx >= 0) num = g_array_index(ol_counter_stack, int, idx);
                        char prefix[24];
                        g_snprintf(prefix, sizeof(prefix), "%d. ", num);
                        cm_render_insert_with_active_tags(buffer, iter, prefix, active_tags);
                        // Increment stack top
                        if (idx >= 0) {
                            g_array_index(ol_counter_stack, int, idx) = num + 1;
                        }
                    } else {
                        cm_render_insert_with_active_tags(buffer, iter, "- ", active_tags);
                    }
                }
            }
            break;
        case CMARK_NODE_HEADING:
            g_snprintf(tag_name_buffer, sizeof(tag_name_buffer), "h%d", cmark_node_get_heading_level(node));
            tag_name_alloc = g_strdup(tag_name_buffer);
            cm_render_get_or_create_base_tag(buffer, tag_name_alloc); // Ensure tag exists
            break;
        case CMARK_NODE_CODE_BLOCK:
            // Code blocks handle their own spacing and preserve ALL whitespace exactly
            {
                cm_render_get_or_create_base_tag(buffer, "codeblock"); // Ensure tag exists using static string

                const char *code_content = cmark_node_get_literal(node);
                const char *info_str = NULL; // TODO: capture fenced info string when available
                if (code_content) {
                    // Mark start
                    GtkTextMark *pre_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);

                    // Apply base codeblock tag plus any inherited tags
                    char *temp_tag_name_for_list = g_strdup("codeblock");
                    GSList *tags_for_this_code_insertion = g_slist_prepend(active_tags, temp_tag_name_for_list);
                    cm_render_insert_with_active_tags(buffer, iter, code_content, tags_for_this_code_insertion);
                    g_free(tags_for_this_code_insertion->data);
                    g_slist_free_1(tags_for_this_code_insertion);

                    // Apply a unique metadata tag to hold language/info
                    if (info_str && *info_str) {
                        static int cb_meta_counter = 0;
                        cb_meta_counter++;
                        char *meta_tag_name = g_strdup_printf("codeblock_meta_%d", cb_meta_counter);
                        GtkTextTag *meta_tag = gtk_text_buffer_create_tag(buffer, meta_tag_name, NULL);
                        g_object_set_data_full(G_OBJECT(meta_tag), "tag-name", g_strdup(meta_tag_name), (GDestroyNotify)g_free);
                        g_object_set_data_full(G_OBJECT(meta_tag), "code-info", g_strdup(info_str), (GDestroyNotify)g_free);

                        GtkTextIter start_insert;
                        gtk_text_buffer_get_iter_at_mark(buffer, &start_insert, pre_mark);
                        GtkTextIter end_insert = *iter;
                        gtk_text_buffer_apply_tag(buffer, meta_tag, &start_insert, &end_insert);
                        g_free(meta_tag_name);
                    }

                    gtk_text_buffer_delete_mark(buffer, pre_mark);
                }
            }
            return; // Code blocks handle their own spacing - let the main logic add trailing newlines
        case CMARK_NODE_HTML_BLOCK:
            // For now, we might just insert the HTML as text, or skip it.
            // Proper HTML rendering is complex.
            // Let's insert it as text with a "html_block" tag if we want to style it.
            // Or, more simply, just insert the literal content.
            {
                const char *html_content = cmark_node_get_literal(node);
                if (html_content) {
                    // Optionally, create and apply an "html_block" tag
                    // For now, just insert the text.
                    cm_render_insert_with_active_tags(buffer, iter, html_content, active_tags);
                }
            }
            return; // HTML block content is literal.
        case CMARK_NODE_THEMATIC_BREAK:
            // Unify HR handling: insert "---" and apply the "hr" tag so export can detect it.
            // Ensure HR starts at a new line
            if (!gtk_text_iter_starts_line(iter)) {
                gtk_text_buffer_insert(buffer, iter, "\n", -1);
            }
            cm_render_get_or_create_base_tag(buffer, "hr");
            {
                char *temp_hr = g_strdup("hr");
                GSList *tags_for_hr = g_slist_prepend(active_tags, temp_hr); // Apply hr plus any surrounding blockquote tags
                cm_render_insert_with_active_tags(buffer, iter, "---", tags_for_hr);
                g_free(tags_for_hr->data);
                g_slist_free_1(tags_for_hr);
            }
            // Do not return; allow block-level newline handling below
            break;
        case CMARK_NODE_PARAGRAPH:
            // Paragraphs themselves don't add a tag, but they manage spacing.
            // The block node newline logic at the end of this function handles paragraph separation.
            break;
        case CMARK_NODE_TEXT:
            {
                const char *text = cmark_node_get_literal(node);
                if (text) {
                    cm_render_insert_with_active_tags(buffer, iter, text, active_tags);
                }
            }
            break;
        case CMARK_NODE_SOFTBREAK:
            // According to CommonMark spec, a softbreak is rendered as a space.
            cm_render_insert_with_active_tags(buffer, iter, " ", active_tags);
            break;
        case CMARK_NODE_LINEBREAK:
            // Hard line break, render as a newline.
            cm_render_insert_with_active_tags(buffer, iter, "\n", active_tags);
            break;
        case CMARK_NODE_CODE:
            // Inline code: insert literal content and apply 'code' tag
            {
                const char *lit = cmark_node_get_literal(node);
                cm_render_get_or_create_base_tag(buffer, "code");
                if (lit) {
                    char *dup = g_strdup("code");
                    GSList *tags_for_inline = g_slist_prepend(active_tags, dup);
                    cm_render_insert_with_active_tags(buffer, iter, lit, tags_for_inline);
                    g_free(tags_for_inline->data);
                    g_slist_free_1(tags_for_inline);
                }
            }
            return;
        case CMARK_NODE_HTML_INLINE:
            // Similar to HTML_BLOCK, for now, insert as literal text.
            {
                const char *html_content = cmark_node_get_literal(node);
                if (html_content) {
                    cm_render_insert_with_active_tags(buffer, iter, html_content, active_tags);
                }
            }
            // No children for inline HTML literal
            return;
        case CMARK_NODE_EMPH:
            tag_name_alloc = g_strdup("italic");
            cm_render_get_or_create_base_tag(buffer, tag_name_alloc); // Ensure tag exists
            break;
        case CMARK_NODE_STRONG:
            tag_name_alloc = g_strdup("bold");
            cm_render_get_or_create_base_tag(buffer, tag_name_alloc); // Ensure tag exists
            break;
        case CMARK_NODE_LINK:
            {
                // Create a unique tag for each link to store its specific URL/title
                static int link_counter = 0;
                link_counter++;
                tag_name_alloc = g_strdup_printf("link_%d", link_counter);
                
                // Create the unique link tag with basic link styling
                GtkTextTag *link_tag = gtk_text_buffer_create_tag(buffer, tag_name_alloc,
                                                                  "foreground", "blue",
                                                                  "underline", PANGO_UNDERLINE_SINGLE,
                                                                  NULL);
                // Store the tag name for our helper function
                g_object_set_data_full(G_OBJECT(link_tag), "tag-name", g_strdup(tag_name_alloc), (GDestroyNotify)g_free);
                
                // Store URL and title from the CommonMark node
                const char *url = cmark_node_get_url(node);
                const char *title = cmark_node_get_title(node);
                
                if (url) {
                    g_object_set_data_full(G_OBJECT(link_tag), "link-url", g_strdup(url), (GDestroyNotify)g_free);
                }
                if (title && *title) {
                    g_object_set_data_full(G_OBJECT(link_tag), "link-title", g_strdup(title), (GDestroyNotify)g_free);
                }
            }
            break;
        case CMARK_NODE_IMAGE:
            {
                // Extract image information from the CommonMark node
                const char *url = cmark_node_get_url(node);
                g_debug("[image-node] Found image node with URL: %s", url ? url : "(null)");
                
                // The "alt" text is the literal content of the image node's children
                GString *alt_text = g_string_new("");
                for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
                    if (cmark_node_get_type(child) == CMARK_NODE_TEXT) {
                        g_string_append(alt_text, cmark_node_get_literal(child));
                    }
                }
                
                if (url && g_image_fetch_context && g_image_fetch_context->text_view) {
                    // Create the image widget that will immediately start fetching
                    GtkWidget *image_widget = create_image_widget(alt_text->str, url);
                    
                    // Create a child anchor in the text buffer
                    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buffer, iter);
                    
                    // Add the widget to the text view at the anchor
                    gtk_text_view_add_child_at_anchor(g_image_fetch_context->text_view, image_widget, anchor);
                    
                    g_debug("[image] Created image widget for URL: %s, alt: %s", url, alt_text->str);
                } else {
                    // Fallback: insert alt text if we can't create a widget
                    const char *display_text = alt_text->len > 0 ? alt_text->str : "Image";
                    gtk_text_buffer_insert(buffer, iter, display_text, -1);
                    g_debug("[image] Fallback text for image: %s", display_text);
                }
                
                g_string_free(alt_text, TRUE);
                
                // Skip child processing since we've already handled the alt text
                return;
            }
    }

    // Add the new tag to the active list if one was created
    GSList *new_active_tags = active_tags;
    if (tag_name_alloc) {
        new_active_tags = g_slist_prepend(active_tags, tag_name_alloc);
    }

    // Recursively process child nodes
    cmark_node *child;
    for (child = cmark_node_first_child(node); child != NULL; child = cmark_node_next(child)) {
        cm_render_node_content_recursive(child, buffer, iter, new_active_tags, ol_counter_stack);
    }

    // Clean up after processing children
    if (tag_name_alloc) {
        g_slist_free_1(new_active_tags); // Frees the list link, not the data
        g_free(tag_name_alloc);
    }
    
    // Pop from ordered list counter stack if we are leaving a list
    if (type == CMARK_NODE_LIST && cmark_node_get_list_type(node) == CMARK_ORDERED_LIST) {
        if (ol_counter_stack->len > 0) {
            g_array_remove_index(ol_counter_stack, ol_counter_stack->len - 1);
        }
    }

    // After processing a block node and its children, ensure it ends with a newline.
    // This is crucial for correct paragraph and block spacing.
    if (is_block_node && needs_trailing_newline) {
        // Check if the buffer already ends with a newline at this position
        GtkTextIter prev_char_iter = *iter;
        if (gtk_text_iter_get_offset(&prev_char_iter) > 0) {
            gtk_text_iter_backward_char(&prev_char_iter);
            gunichar last_char = gtk_text_iter_get_char(&prev_char_iter);
            if (last_char != '\n') {
                gtk_text_buffer_insert(buffer, iter, "\n", -1);
            }
        } else {
            // If at the beginning of the buffer, we probably need a newline.
            gtk_text_buffer_insert(buffer, iter, "\n", -1);
        }

        // Add an extra newline for paragraph separation
        if (needs_paragraph_separation) {
            // Check if there's already a blank line
            GtkTextIter temp_iter = *iter;
            gboolean already_blank = FALSE;
            if (gtk_text_iter_get_offset(&temp_iter) > 1) {
                gtk_text_iter_backward_chars(&temp_iter, 2);
                char *two_chars = gtk_text_iter_get_text(&temp_iter, iter);
                if (strcmp(two_chars, "\n\n") == 0) {
                    already_blank = TRUE;
                }
                g_free(two_chars);
            }
            if (!already_blank) {
                gtk_text_buffer_insert(buffer, iter, "\n", -1);
            }
        }
    }

    // CRITICAL: CommonMark-compliant block element newline handling
    if (is_block_node && needs_trailing_newline) {
        // Ensure block elements end with exactly one newline
        if (!gtk_text_iter_starts_line(iter)) {
            gtk_text_buffer_insert(buffer, iter, "\n", -1);
        }
        
        // Add paragraph separation (blank line) when needed
        if (needs_paragraph_separation) {
            // Add an additional newline to create a blank line between blocks
            gtk_text_buffer_insert(buffer, iter, "\n", -1);
        }
    }

    // For loose lists, ensure an extra blank line between items
    if (type == CMARK_NODE_ITEM) {
        cmark_node *pl = cmark_node_parent(node);
        if (pl && cmark_node_get_type(pl) == CMARK_NODE_LIST && !cmark_node_get_list_tight(pl)) {
            gtk_text_buffer_insert(buffer, iter, "\n", -1);
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


gboolean cm_render_markdown_to_buffer(GtkTextBuffer *buffer, const char *markdown_text, 
                                      GtkTextView *text_view, SoupSession *soup_session) {
    if (!buffer || !markdown_text) {
        g_warning("cm_render_markdown_to_buffer: Invalid arguments (buffer or markdown_text is NULL).");
        return FALSE;
    }

    // Store image fetch context for recursive access
    ImageFetchContext *context = g_new(ImageFetchContext, 1);
    context->soup_session = soup_session;
    context->text_view = text_view;
    g_image_fetch_context = context;

    // 1. Clear the buffer
    GtkTextIter start_clear, end_clear;
    gtk_text_buffer_get_bounds(buffer, &start_clear, &end_clear);
    gtk_text_buffer_delete(buffer, &start_clear, &end_clear);

    // 2. Ensure basic non-theme dependent tags are available.
    //    This is now handled on-demand by cm_render_get_or_create_base_tag.
    //    However, it's good practice to ensure theme-dependent ones are updated after parsing.

    // 3. Parse the Markdown
    // Options: CMARK_OPT_DEFAULT is 0.
    // CMARK_OPT_HARDBREAKS: Treat newlines as hard line breaks. (We want default CommonMark behavior)
    // CMARK_OPT_SMART: Use smart punctuation. (Good for display)
    // CMARK_OPT_VALIDATE_UTF8: Ensure UTF-8 validity.
    int options = CMARK_OPT_SMART | CMARK_OPT_VALIDATE_UTF8;
    g_debug("[parse] Starting CommonMark parsing with options: %d", options);
    g_debug("[parse] Input text length: %zu", strlen(markdown_text));
    g_debug("[parse] First 100 chars: %.100s", markdown_text);
    
    cmark_parser *parser = cmark_parser_new(options);
    if (!parser) {
        g_warning("Failed to create cmark_parser.");
        return FALSE;
    }

    cmark_parser_feed(parser, markdown_text, strlen(markdown_text));
    cmark_node *document = cmark_parser_finish(parser);
    cmark_parser_free(parser);

    if (!document) {
        g_warning("Failed to parse Markdown document.");
        return FALSE;
    }
    
    g_debug("[parse] Successfully parsed document, starting rendering");

    // 4. Render the document to the buffer
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(buffer, &iter);
    GSList *active_tags = NULL; // Start with no active tags
    // Ordered-list counters stack (int per ordered list nesting level)
    GArray *ol_counter_stack = g_array_new(FALSE, FALSE, sizeof(int));

    cm_render_node_content_recursive(document, buffer, &iter, active_tags, ol_counter_stack);

    // Free the cmark document
    cmark_node_free(document);

    // 5. Update theme-dependent tags (like code block backgrounds)
    // This should be called after all content is inserted and base tags are created.
    cm_render_update_theme_dependent_tags(buffer);
    
    // Ensure buffer ends with a newline if it's not empty, for consistent spacing.
    // This might be too aggressive, consider if it's truly needed.
    // gtk_text_buffer_get_end_iter(buffer, &iter);
    // if (!gtk_text_iter_starts_line(&iter) && gtk_text_buffer_get_char_count(buffer) > 0) {
    //     GtkTextIter prev_char_iter = iter;
    //     if (gtk_text_iter_backward_char(&prev_char_iter)) {
    //         if (gtk_text_iter_get_char(&prev_char_iter) != '\\n') {
    //             gtk_text_buffer_insert(buffer, &iter, "\\n", -1);
    //         }
    //     }
    // }

    g_array_free(ol_counter_stack, TRUE);
    
    // Clear and free the global image fetch context
    if (g_image_fetch_context) {
        g_free(g_image_fetch_context);
        g_image_fetch_context = NULL;
    }
    
    return TRUE;
}


// --- Implementation of cm_render_buffer_to_markdown and its helpers ---

static void free_active_markdown_inline_tag(gpointer data) {
    ActiveMarkdownInlineTag *tag_data = (ActiveMarkdownInlineTag *)data;
    if (tag_data) {
        // tag_name is not owned by this struct, it points to static strings or GtkTextTag names
        g_free(tag_data->url);
        g_free(tag_data->title);
        g_free(tag_data);
    }
}

/**
 * @brief Closes Markdown inline tags that are no longer active.
 *
 * Iterates backwards through the active_inline_stack. If a tag on the stack
 * is not found in current_gtk_tags (or if force_close_all is true),
 * it's considered closed. The corresponding Markdown delimiter is appended
 * to md_output, and the tag is removed from the stack.
 *
 * @param md_output The GString to append Markdown to.
 * @param active_inline_stack_ptr Pointer to the GSList of ActiveMarkdownInlineTag.
 * @param current_gtk_tags GSList of GtkTextTag names currently applied to the text segment.
 * @param force_close_all If TRUE, closes all tags on the stack regardless of current_gtk_tags.
 */
G_GNUC_UNUSED static void close_inline_tags_from_stack(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, gboolean force_close_all) {
    GSList *iter = *active_inline_stack_ptr;
    GSList *prev = NULL;

    while (iter != NULL) {
        ActiveMarkdownInlineTag *active_tag = (ActiveMarkdownInlineTag *)iter->data;
        gboolean still_active = FALSE;
        if (!force_close_all) {
            for (GSList *gtk_tag_iter = current_gtk_tags; gtk_tag_iter != NULL; gtk_tag_iter = gtk_tag_iter->next) {
                const char *gtk_tag_name = get_tag_name_safe((GtkTextTag*)gtk_tag_iter->data); // Use helper
                if (gtk_tag_name && strcmp(active_tag->tag_name, gtk_tag_name) == 0) {
                    still_active = TRUE;
                    break;
                }
            }
        }

        if (!still_active) { // Tag needs to be closed
            // Append closing Markdown delimiter
            if (strcmp(active_tag->tag_name, "bold") == 0) g_string_append(md_output, "**");
            else if (strcmp(active_tag->tag_name, "italic") == 0) g_string_append(md_output, "*");
            else if (strcmp(active_tag->tag_name, "code") == 0) g_string_append(md_output, "`");
            else if (strcmp(active_tag->tag_name, "link") == 0) {
                g_string_append_printf(md_output, "](%s%s%s)",
                                       active_tag->url ? active_tag->url : "",
                                       (active_tag->url && active_tag->title) ? " \"" : "",
                                       active_tag->title ? active_tag->title : "");
                if (active_tag->title) g_string_append(md_output, "\""); // Ensure title quote is closed if present
            }
            // Note: Images are typically self-closing or handled differently, not usually on a stack like this.
            // For this example, we assume 'image' tag isn't pushed onto this particular stack
            // or would be handled by a more specific mechanism if it were.

            // Remove from stack and free
            GSList *next = iter->next;
            if (prev) {
                prev->next = next;
            } else {
                *active_inline_stack_ptr = next;
            }
            free_active_markdown_inline_tag(active_tag);
            g_slist_free_1(iter); // Free the list link itself
            iter = next;
        } else {
            prev = iter;
            iter = iter->next;
        }
    }
}

/**
 * @brief Opens new Markdown inline tags based on the current GtkTextTags.
 *
 * Iterates through current_gtk_tags. If a tag is not already on the
 * active_inline_stack, it's considered newly opened. The corresponding
 * Markdown delimiter is appended to md_output, and the tag is added to the stack.
 *
 * @param md_output The GString to append Markdown to.
 * @param active_inline_stack_ptr Pointer to the GSList of ActiveMarkdownInlineTag.
 * @param current_gtk_tags GSList of GtkTextTag names currently applied to the text segment.
 * @param buffer The GtkTextBuffer (used to fetch URL/title for links).
 */
G_GNUC_UNUSED static void open_inline_tags_for_segment(GString *md_output, GSList **active_inline_stack_ptr, GSList *current_gtk_tags, G_GNUC_UNUSED GtkTextBuffer *buffer) {
    // Iterate through current GTK tags to see which ones need to be opened
    for (GSList *gtk_tag_iter = current_gtk_tags; gtk_tag_iter != NULL; gtk_tag_iter = gtk_tag_iter->next) {
        GtkTextTag *current_gtk_tag_obj = (GtkTextTag*)gtk_tag_iter->data;
        const char *current_tag_name = get_tag_name_safe(current_gtk_tag_obj); // Use helper
        if (!current_tag_name) continue;

        gboolean already_active = FALSE;
        for (GSList *active_iter = *active_inline_stack_ptr; active_iter != NULL; active_iter = active_iter->next) {
            ActiveMarkdownInlineTag *active_md_tag = (ActiveMarkdownInlineTag *)active_iter->data;
            if (strcmp(active_md_tag->tag_name, current_tag_name) == 0) {
                already_active = TRUE;
                break;
            }
        }

        if (!already_active) {
            // This tag is new for this segment, open it
            ActiveMarkdownInlineTag *new_active_tag = g_new0(ActiveMarkdownInlineTag, 1);
            new_active_tag->tag_name = current_tag_name; // Points to the GtkTextTag's name (or our g_object_set_data copy)

            if (strcmp(current_tag_name, "bold") == 0) g_string_append(md_output, "**");
            else if (strcmp(current_tag_name, "italic") == 0) g_string_append(md_output, "*");
            else if (strcmp(current_tag_name, "code") == 0) g_string_append(md_output, "`");
            else if (strcmp(current_tag_name, "link") == 0) {
                g_string_append(md_output, "["); // Link text will follow
                // Fetch URL and title if stored on the GtkTextTag
                // This assumes URL and title are stored as GObject data on the tag
                const char *url = g_object_get_data(G_OBJECT(current_gtk_tag_obj), "url");
                const char *title = g_object_get_data(G_OBJECT(current_gtk_tag_obj), "title");
                new_active_tag->url = url ? g_strdup(url) : NULL;
                new_active_tag->title = title ? g_strdup(title) : NULL;
            }
            // Images are more complex; alt text is part of the node, URL is an attribute.
            // This simplified stack primarily handles emphasis, code, links.

            *active_inline_stack_ptr = g_slist_prepend(*active_inline_stack_ptr, new_active_tag);
        }
    }
}


char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer) {
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
    gboolean currently_in_link = FALSE;
    const char *current_link_url = NULL;
    const char *current_link_title = NULL;
    GString *current_link_text = NULL;
    gsize current_link_md_start = 0;
    gboolean currently_in_image = FALSE;
    const char *current_image_url = NULL;
    const char *current_image_title = NULL;
    GString *current_image_alt = NULL;
    gboolean at_line_start = TRUE;

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

    while(!gtk_text_iter_is_end(&iter)) {
        gunichar current_char = gtk_text_iter_get_char(&iter);

        // Check for child anchors (embedded widgets like images)
        GtkTextChildAnchor *child_anchor = gtk_text_iter_get_child_anchor(&iter);
        if (child_anchor) {
            // Get the widgets attached to this anchor
            guint widget_count = 0;
            GtkWidget **widgets = gtk_text_child_anchor_get_widgets(child_anchor, &widget_count);
            
            for (guint i = 0; i < widget_count; i++) {
                GtkWidget *widget = widgets[i];
                
                // Check if this is an image widget (should be a box containing our image)
                const char *image_url = g_object_get_data(G_OBJECT(widget), "image-url");
                const char *image_alt = g_object_get_data(G_OBJECT(widget), "image-alt");
                
                if (image_url) {
                    // This is an image widget, generate markdown
                    g_debug("[export] Found image widget with URL: %s, alt: %s", 
                           image_url, image_alt ? image_alt : "(none)");
                    
                    // Check if this image is also a link by looking at the surrounding tags
                    GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
                    const char *link_url = NULL;
                    const char *link_title = NULL;
                    
                    for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
                        GtkTextTag *tag = GTK_TEXT_TAG(l->data);
                        const char *tag_name = get_tag_name_safe(tag);
                        if (tag_name && g_str_has_prefix(tag_name, "link-")) {
                            link_url = g_object_get_data(G_OBJECT(tag), "link-url");
                            link_title = g_object_get_data(G_OBJECT(tag), "link-title");
                            break;
                        }
                    }
                    g_slist_free(tags_at_iter);
                    
                    // Generate appropriate markdown syntax
                    if (link_url) {
                        // Image with link: [![alt](image_url)](link_url)
                        g_string_append_printf(md, "[![%s](%s)](%s)",
                                              image_alt ? image_alt : "",
                                              image_url,
                                              link_url);
                        if (link_title) {
                            // Note: This doesn't handle link titles in the image-link case
                            // as the markdown syntax becomes complex
                        }
                    } else {
                        // Regular image: ![alt](url)
                        g_string_append_printf(md, "![%s](%s)",
                                              image_alt ? image_alt : "",
                                              image_url);
                    }
                }
            }
            g_free(widgets);
            
            // Skip to next character since we processed the child anchor
            gtk_text_iter_forward_char(&iter);
            continue;
        }

        gboolean iter_is_bold = FALSE;
        gboolean iter_is_italic = FALSE;
        gboolean iter_is_code = FALSE;
        gboolean iter_is_codeblock_char = FALSE;
        gboolean iter_is_link = FALSE;
        const char *link_url = NULL;
        const char *link_title = NULL;
        gboolean iter_is_image = FALSE;
        const char *image_url = NULL;
        const char *image_title = NULL;
        const char *iter_code_info = NULL;
        gboolean iter_is_h1 = FALSE, iter_is_h2 = FALSE, iter_is_h3 = FALSE, iter_is_h4 = FALSE, iter_is_h5 = FALSE, iter_is_h6 = FALSE;
        int iter_blockquote_depth = 0;

        GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
        for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(l->data);
            const char *tag_name = get_tag_name_safe(tag);
            if (tag_name) {
                // Debug: Print found tags for troubleshooting
                if (current_char != '\n' && current_char != ' ') {
                    g_debug("cm_export: tag '%s' at char '%c'", tag_name, (char)current_char);
                }
                
                if (g_strcmp0(tag_name, "bold") == 0) iter_is_bold = TRUE;
                else if (g_strcmp0(tag_name, "italic") == 0) iter_is_italic = TRUE;
                else if (g_strcmp0(tag_name, "code") == 0) {
                    iter_is_code = TRUE;
                    g_debug("cm_export: code tag at '%c' (in_code=%s)", (char)current_char, currently_in_code ? "TRUE" : "FALSE");
                }
                else if (g_strcmp0(tag_name, "codeblock") == 0) iter_is_codeblock_char = TRUE;
                else if (g_str_has_prefix(tag_name, "link_")) { // Handle unique link tags
                    iter_is_link = TRUE;
                    link_url = g_object_get_data(G_OBJECT(tag), "link-url");
                    link_title = g_object_get_data(G_OBJECT(tag), "link-title");
                    g_debug("cm_export: link tag '%s', URL=%s", tag_name, link_url ? link_url : "NULL");
                }
                else if (g_str_has_prefix(tag_name, "image_")) {
                    // Ignore helper tag used to hide alt text and only
                    // consider tags that actually carry an image URL.
                    if (g_strcmp0(tag_name, "image_alt_hidden") != 0) {
                        const char *u = g_object_get_data(G_OBJECT(tag), "image-url");
                        if (u && *u) {
                            iter_is_image = TRUE;
                            image_url = u;
                            image_title = g_object_get_data(G_OBJECT(tag), "image-title");
                        }
                    }
                }
                else if (g_str_has_prefix(tag_name, "codeblock_meta_")) {
                    const char *info = g_object_get_data(G_OBJECT(tag), "code-info");
                    if (info && !iter_code_info) iter_code_info = info;
                }
                else if (g_strcmp0(tag_name, "h1") == 0) iter_is_h1 = TRUE;
                else if (g_strcmp0(tag_name, "h2") == 0) iter_is_h2 = TRUE;
                else if (g_strcmp0(tag_name, "h3") == 0) iter_is_h3 = TRUE;
                else if (g_strcmp0(tag_name, "h4") == 0) iter_is_h4 = TRUE;
                else if (g_strcmp0(tag_name, "h5") == 0) iter_is_h5 = TRUE;
                else if (g_strcmp0(tag_name, "h6") == 0) iter_is_h6 = TRUE;
                else if (g_str_has_prefix(tag_name, "blockquote")) iter_blockquote_depth++;
            }
        }
        g_slist_free(tags_at_iter);

        if (at_line_start) {
            // Emit blockquote prefix at line start based on nesting depth
            if (!currently_in_codeblock && iter_blockquote_depth > 0) {
                for (int d = 0; d < iter_blockquote_depth; d++) {
                    g_string_append_c(md, '>');
                }
                g_string_append_c(md, ' ');
            }
            // Check for horizontal rule (hr) tag at line start
            GtkTextTag *hr_tag = gtk_text_tag_table_lookup(tag_table, "hr");
            if (hr_tag && gtk_text_iter_has_tag(&iter, hr_tag) && !currently_in_codeblock) {
                g_string_append(md, "---\n");
                
                // Skip to end of line
                GtkTextIter line_end_iter = iter;
                gtk_text_iter_forward_to_line_end(&line_end_iter);
                iter = line_end_iter; 

                if (!gtk_text_iter_is_end(&iter)) {
                     gtk_text_iter_forward_char(&iter); 
                     if (!gtk_text_iter_is_end(&iter) && gtk_text_iter_get_char(&iter) == '\n') {
                         gtk_text_iter_forward_char(&iter); 
                     }
                }
                at_line_start = TRUE; 
                if (gtk_text_iter_is_end(&iter)) break;
                continue;
            }

            if (!currently_in_codeblock) {
                gboolean heading_started_here = FALSE;
                if (iter_is_h1) { g_string_append(md, "# "); heading_started_here = TRUE; }
                else if (iter_is_h2) { g_string_append(md, "## "); heading_started_here = TRUE; }
                else if (iter_is_h3) { g_string_append(md, "### "); heading_started_here = TRUE; }
                else if (iter_is_h4) { g_string_append(md, "#### "); heading_started_here = TRUE; }
                else if (iter_is_h5) { g_string_append(md, "##### "); heading_started_here = TRUE; }
                else if (iter_is_h6) { g_string_append(md, "###### "); heading_started_here = TRUE; }

                if (!heading_started_here) { 
                    GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");
                    if (codeblock_tag && gtk_text_iter_has_tag(&iter, codeblock_tag)) {
                        if (iter_code_info && *iter_code_info) {
                            g_string_append_printf(md, "```%s\n", iter_code_info);
                        } else {
                            g_string_append(md, "```\n");
                        }
                        currently_in_codeblock = TRUE;
                    }
                }
            }
        }

        // Handle end of code block
        if (currently_in_codeblock && !iter_is_codeblock_char && current_char != '\n') {
            if (md->len > 0 && md->str[md->len -1] != '\n') {
                g_string_append_c(md, '\n');
            }
            g_string_append(md, "```\n");
            currently_in_codeblock = FALSE;
        }
        
        if (currently_in_codeblock) {
            g_string_append_unichar(md, current_char);
        } else {
            // Skip object replacement or zero-width characters in export
            if (current_char == 0xFFFC || current_char == 0x200B) {
                goto advance_only;
            }
            // Handle bold/italic inline formatting
            if (iter_is_bold && iter_is_italic && !currently_in_bold && !currently_in_italic) {
                g_string_append(md, "***");
                currently_in_bold = TRUE;
                currently_in_italic = TRUE;
            } else if (!iter_is_bold && !iter_is_italic && currently_in_bold && currently_in_italic) {
                g_string_append(md, "***");
                currently_in_bold = FALSE;
                currently_in_italic = FALSE;
            } else {
                if (iter_is_bold && !currently_in_bold) {
                    g_string_append(md, "**");
                    currently_in_bold = TRUE;
                } else if (!iter_is_bold && currently_in_bold) {
                    g_string_append(md, "**");
                    currently_in_bold = FALSE;
                }

                if (iter_is_italic && !currently_in_italic) {
                    g_string_append(md, "*");
                    currently_in_italic = TRUE;
                } else if (!iter_is_italic && currently_in_italic) {
                    g_string_append(md, "*");
                    currently_in_italic = FALSE;
                }
            }

            // Handle inline code state changes BEFORE appending the character
            if (currently_in_code && !iter_is_code) { // Leaving a code span
                g_string_append_c(md, '`');
                currently_in_code = FALSE;
            }
            if (iter_is_code && !currently_in_code) { // Entering a code span
                g_string_append_c(md, '`');
                currently_in_code = TRUE;
            }

            // Handle image/link state changes with proper nesting support
            
            // Check for nested image-in-link pattern (both tags present simultaneously)
            gboolean entering_image_in_link = (iter_is_image && iter_is_link && !currently_in_image && !currently_in_link);
            gboolean leaving_image_in_link = (currently_in_image && currently_in_link && (!iter_is_image || !iter_is_link));
            
            
            if (leaving_image_in_link) {
                // Leaving nested image-in-link: close image first, then link
                // Close image part: ](...) 
                if (current_image_url) {
                    if (current_image_title && *current_image_title) {
                        g_string_append_printf(md, "](%s \"%s\")", current_image_url, current_image_title);
                    } else {
                        g_string_append_printf(md, "](%s)", current_image_url);
                    }
                } else {
                    g_string_append(md, "]()");
                }
                
                // Close link part: ](...) 
                if (current_link_url) {
                    if (current_link_title && *current_link_title) {
                        g_string_append_printf(md, "](%s \"%s\")", current_link_url, current_link_title);
                    } else {
                        g_string_append_printf(md, "](%s)", current_link_url);
                    }
                } else {
                    g_string_append(md, "]()");
                }
                
                // Clean up state
                if (current_image_alt) { g_string_free(current_image_alt, TRUE); current_image_alt = NULL; }
                if (current_link_text) { g_string_free(current_link_text, TRUE); current_link_text = NULL; }
                currently_in_image = FALSE;
                currently_in_link = FALSE;
                current_image_url = NULL;
                current_image_title = NULL;
                current_link_url = NULL;
                current_link_title = NULL;
            }
            else if (entering_image_in_link) {
                // Entering nested image-in-link: [![
                current_link_md_start = md->len;
                g_string_append(md, "[![");
                currently_in_link = TRUE;
                currently_in_image = TRUE;
                current_link_url = link_url;
                current_link_title = link_title;
                current_image_url = image_url;
                current_image_title = image_title;
                if (current_link_text) { g_string_free(current_link_text, TRUE); }
                current_link_text = g_string_new("");
                if (current_image_alt) { g_string_free(current_image_alt, TRUE); }
                current_image_alt = g_string_new("");
            }
            else {
                // Handle regular (non-nested) image and link state changes
                if (currently_in_image && !iter_is_image) { // Leaving an image alt span
                    if (current_image_url) {
                        if (current_image_title && *current_image_title) {
                            g_string_append_printf(md, "](%s \"%s\")", current_image_url, current_image_title);
                        } else {
                            g_string_append_printf(md, "](%s)", current_image_url);
                        }
                    } else {
                        g_string_append(md, "]()");
                    }
                    if (current_image_alt) { g_string_free(current_image_alt, TRUE); current_image_alt = NULL; }
                    currently_in_image = FALSE;
                    current_image_url = NULL;
                    current_image_title = NULL;
                }
                if (iter_is_image && !currently_in_image) { // Entering image alt span
                    g_string_append(md, "![");
                    currently_in_image = TRUE;
                    current_image_url = image_url;
                    current_image_title = image_title;
                    if (current_image_alt) { g_string_free(current_image_alt, TRUE); }
                    current_image_alt = g_string_new("");
                }

                if (currently_in_link && !iter_is_link) { // Leaving a link
                    if (current_link_url) {
                        // Autolink if link text equals URL and no title
                        if (current_link_text && current_link_text->len > 0 && (!current_link_title || !*current_link_title) &&
                            g_strcmp0(current_link_text->str, current_link_url) == 0) {
                            // Rewind to md state at link open and write autolink
                            g_string_set_size(md, current_link_md_start);
                            g_string_append_printf(md, "<%s>", current_link_url);
                        } else {
                            if (current_link_title && *current_link_title) {
                                g_string_append_printf(md, "](%s \"%s\")", current_link_url, current_link_title);
                            } else {
                                g_string_append_printf(md, "](%s)", current_link_url);
                            }
                        }
                    } else {
                        g_string_append(md, "]()");
                    }
                    if (current_link_text) { g_string_free(current_link_text, TRUE); current_link_text = NULL; }
                    currently_in_link = FALSE;
                    current_link_url = NULL;
                    current_link_title = NULL;
                }
                if (iter_is_link && !currently_in_link) { // Entering a link
                    current_link_md_start = md->len;
                    g_string_append_c(md, '[');
                    currently_in_link = TRUE;
                    current_link_url = link_url;
                    current_link_title = link_title;
                    if (current_link_text) { g_string_free(current_link_text, TRUE); }
                    current_link_text = g_string_new("");
                }
            }
            
            // Skip zero-width space characters used for image/link placeholders
            if (current_char != 0x200B) { // Skip zero-width space (U+200B)
                g_string_append_unichar(md, current_char); // Append the character itself
            }
            if (currently_in_link && current_link_text && current_char != 0x200B) {
                g_string_append_unichar(current_link_text, current_char);
            }
            if (currently_in_image && current_image_alt && current_char != 0x200B) {
                g_string_append_unichar(current_image_alt, current_char);
            }
        }

        if (current_char == '\n') {
            at_line_start = TRUE;
            if (currently_in_codeblock) {
                // Check if the *next* char (if any) still has codeblock tag.
                GtkTextIter next_char_iter = iter;
                gtk_text_iter_forward_char(&next_char_iter);
                if (gtk_text_iter_is_end(&next_char_iter) || // End of buffer
                    !gtk_text_iter_has_tag(&next_char_iter, gtk_text_tag_table_lookup(tag_table, "codeblock"))) {
                    // End of code block detected
                }
            }
        } else {
            at_line_start = FALSE;
        }
        
advance_only:
        gtk_text_iter_forward_char(&iter);
    }

    // Close any remaining open tags at the end of the buffer
    if (currently_in_link) {
        if (current_link_url) {
            if (current_link_title && *current_link_title) {
                g_string_append_printf(md, "](%s \"%s\")", current_link_url, current_link_title);
            } else {
                g_string_append_printf(md, "](%s)", current_link_url);
            }
        } else {
            g_string_append(md, "]()");
        }
        if (current_link_text) { g_string_free(current_link_text, TRUE); current_link_text = NULL; }
    }
    if (currently_in_image) {
        if (current_image_url) {
            if (current_image_title && *current_image_title) {
                g_string_append_printf(md, "](%s \"%s\")", current_image_url, current_image_title);
            } else {
                g_string_append_printf(md, "](%s)", current_image_url);
            }
        } else {
            g_string_append(md, "]()");
        }
        if (current_image_alt) { g_string_free(current_image_alt, TRUE); current_image_alt = NULL; }
    }
    if (currently_in_code) {
        g_string_append_c(md, '`');
    }
    if (currently_in_codeblock) {
        if (md->len > 0 && md->str[md->len -1] != '\n') {
            g_string_append_c(md, '\n');
        }
        g_string_append(md, "```\n");
    }
    // Ensure bold/italic are closed if buffer ends mid-format
    if (currently_in_bold && currently_in_italic) g_string_append(md, "***");
    else if (currently_in_bold) g_string_append(md, "**");
    else if (currently_in_italic) g_string_append(md, "*");

    // Ensure the exported markdown ends with a newline if the buffer wasn't empty.
    if (md->len > 0 && md->str[md->len-1] != '\n') {
        g_string_append_c(md, '\n');
    }

    return g_string_free(md, FALSE); // Return the string and free the GString container
}
