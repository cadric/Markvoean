/* C ULTRA-MIN TEMPLATE
   Purpose: CommonMark markdown rendering engine with GTK text buffer integration
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.4.0] - 2025-09-20 - render/markdown/cmrender.c
   Added: cm_render_selection_to_markdown function for preserving formatting in selections
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtktext/render/cmrender.h>
#include <gtktext/render/tag_manager.h>
#include <gtktext/render/theme_styles.h>
#include <gtktext/render/hr_widget.h>
#include <gtktext/render/safe_helpers.h>
#include <gtktext/render/markdown/markdown_engine.h>
#include <adwaita.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <string.h>
#include <stdio.h>
#include <cmark.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * META - Forward declarations and macros
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef void (*ImageFetchCallback)(GdkPixbuf *pixbuf, GError *error, gpointer user_data);

#define CMRENDER_UNUSED __attribute__((unused))

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Data structures and type definitions
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    SoupSession *soup_session;
    GtkTextView *text_view;
} ImageFetchContext;

typedef struct {
    GWeakRef picture_ref;
    char *url;
    char *alt_text;
} ImageWidgetData;

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Global state and caching
 * ═══════════════════════════════════════════════════════════════════════════════ */

static GQuark quark_tag_name = 0;
static GQuark quark_tag_cache = 0;
static ImageFetchContext *g_image_fetch_context = NULL;
static gboolean g_use_visual_bullets = TRUE;

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

// Function to free tag cache structure
static void tag_cache_free(gpointer p)
{
    TagCache *cache = (TagCache*)p;
    if (cache) {
        if (cache->map) g_hash_table_destroy(cache->map);
        g_free(cache);
    }
}

// Helper function to safely get tag names since gtk_text_tag_get_name isn't directly
// available or is named differently in GTK4
static const char *get_tag_name_safe(GtkTextTag *tag) {
    if (!tag) return NULL;

    if (G_UNLIKELY(quark_tag_name == 0))
        quark_tag_name = g_quark_from_static_string("tag-name");

    const char *name = g_object_get_qdata(G_OBJECT(tag), quark_tag_name);
    if (name && *name) return name;

    gchar *prop_name = NULL;
    g_object_get(G_OBJECT(tag), "name", &prop_name, NULL);
    if (prop_name && *prop_name) {
        g_object_set_qdata_full(G_OBJECT(tag), quark_tag_name, g_strdup(prop_name), g_free);
        const char *stored_name = g_object_get_qdata(G_OBJECT(tag), quark_tag_name);
        g_free(prop_name);
        return stored_name;
    }
    g_free(prop_name);
    g_warning("Tag name not found for tag %p.", (void*)tag);
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
        g_weak_ref_clear(&data->picture_ref);
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
        // Try to get the picture widget from weak reference
        GtkPicture *picture = g_weak_ref_get(&data->picture_ref);
        if (picture) {
            // Convert GdkPixbuf to GdkTexture and display
            GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
            gtk_picture_set_paintable(picture, GDK_PAINTABLE(texture));
            g_object_unref(texture);
            g_object_unref(picture);  // Release the strong reference from g_weak_ref_get
            g_debug("[image] Successfully loaded image: %s", data->url);
        } else {
            g_debug("[image] Picture widget no longer exists for: %s", data->url);
        }
        g_object_unref(pixbuf);
    } else {
        g_debug("[image] Failed to decode image %s: %s", data->url, error ? error->message : "unknown");
        g_clear_error(&error);
    }
    
    g_bytes_unref(bytes);
    
    // Clean up our custom data structure
    g_weak_ref_clear(&data->picture_ref);
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

// Click handler for image widgets: opens the preferred URL in default handler
static void on_image_picture_pressed(GtkGestureClick *gesture,
                                     gint n_press,
                                     gdouble x,
                                     gdouble y,
                                     gpointer user_data) {
    (void)gesture;  // Unused parameters
    (void)n_press;
    (void)x;
    (void)y;
    
    GtkWidget *widget = GTK_WIDGET(user_data);
    const char *open = (const char*) g_object_get_data(G_OBJECT(widget), "open-url");
    if (!open || !*open) open = (const char*) g_object_get_data(G_OBJECT(widget), "image-url");
    if (!open || !*open) return;

    GtkWidget *view = g_image_fetch_context ? GTK_WIDGET(g_image_fetch_context->text_view)
                                            : gtk_widget_get_ancestor(widget, GTK_TYPE_TEXT_VIEW);
    GtkWindow *win = view ? GTK_WINDOW(gtk_widget_get_ancestor(view, GTK_TYPE_WINDOW)) : NULL;
    GtkUriLauncher *launcher = gtk_uri_launcher_new(open);
    gtk_uri_launcher_launch(launcher, win, NULL, NULL, NULL);
    g_object_unref(launcher);
}

static GtkWidget *create_image_widget(const char *alt_text, const char *url,
                                      const char *open_url, const char *title) {
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

    // Accessibility label from alt or title/url
    const char *acc_label = (alt_text && *alt_text) ? alt_text
                          : (title && *title) ? title
                          : (open_url && *open_url) ? open_url
                          : url;
    if (acc_label) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(picture),
                                       GTK_ACCESSIBLE_PROPERTY_LABEL,
                                       acc_label,
                                       -1);
    }

    // Tooltip and click navigation; prefer enclosing link when present
    if (title && *title) gtk_widget_set_tooltip_text(picture, title);
    else if (open_url && *open_url) gtk_widget_set_tooltip_text(picture, open_url);
    else if (url) gtk_widget_set_tooltip_text(picture, url);

    // Make the image clickable
    if ((open_url && *open_url) || (url && *url)) {
        GtkGesture *click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
        g_signal_connect(click, "pressed", G_CALLBACK(on_image_picture_pressed), picture);
        gtk_widget_add_controller(picture, GTK_EVENT_CONTROLLER(click));
        if (open_url && *open_url)
            g_object_set_data_full(G_OBJECT(picture), "open-url", g_strdup(open_url), g_free);
        if (url && *url)
            g_object_set_data_full(G_OBJECT(picture), "image-url", g_strdup(url), g_free);
    }

    gtk_box_append(GTK_BOX(box), picture);
    
    // Add caption if provided
    if (alt_text && g_utf8_strlen(alt_text, -1) > 0) {
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
            soup_message_headers_replace(headers, "User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36 IFG/0.1");
            soup_message_headers_replace(headers, "Accept", "image/*,*/*;q=0.5");
            
            // Create callback data
            ImageWidgetData *img_data = g_new(ImageWidgetData, 1);
            g_weak_ref_init(&img_data->picture_ref, G_OBJECT(picture));
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

// Forward declarations for static helper functions for buffer_to_markdown
// static char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer); // Declaration removed, will be non-static

// Forward declaration for the recursive helper
// Maintain a stack of list state (ordered start, delimiter, tightness) to support nesting.
typedef struct {
    gboolean ordered;
    int next_number;      // for ordered lists
    char delim_char;      // '.' or ')'
    gboolean tight;       // from cmark
} ListCtx;

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
    if (!buffer || !tag_name || !*tag_name) return NULL;

    if (G_UNLIKELY(quark_tag_cache == 0))
        quark_tag_cache = g_quark_from_static_string("cm-tag-cache");

    TagCache *cache = g_object_get_qdata(G_OBJECT(buffer), quark_tag_cache);
    if (!cache) {
        cache = g_new0(TagCache, 1);
        cache->map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        g_object_set_qdata_full(G_OBJECT(buffer), quark_tag_cache, cache, tag_cache_free);
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
            // Basic properties for inline code
            tag = gtk_text_buffer_create_tag(buffer, "code",
                                             "family", "monospace",
                                             "background-full-height", TRUE,
                                             "left-margin", 4,
                                             "right-margin", 4,
                                             "pixels-above-lines", 1,
                                             "pixels-below-lines", 1,
                                             NULL);
        } else if (g_strcmp0(tag_name, "codeblock") == 0) {
            // Basic properties for fenced code blocks — visual colors are set by theme update.
            // Do NOT paint per-glyph background; we only want a paragraph-wide background.
            tag = gtk_text_buffer_create_tag(buffer, "codeblock",
                                             "family", "monospace",
                                             "background-full-height", FALSE,
                                             "left-margin", 32,       // Indented margin for fenced blocks (4 spaces equivalent)
                                             "right-margin", 12,      // Standard margin for fenced blocks
                                             "pixels-above-lines", 6, // Standard padding for fenced blocks
                                             "pixels-below-lines", 6, // Standard padding for fenced blocks
                                             "wrap-mode", GTK_WRAP_NONE, // Code blocks typically don't wrap
                                             "indent", 2,             // Minimal indent for fenced blocks
                                             NULL);
        } else if (g_strcmp0(tag_name, "codeblock_indented") == 0) {
            // Indented code blocks should visually represent the 4+ space indentation
            // Make them feel properly indented according to CommonMark spec
            tag = gtk_text_buffer_create_tag(buffer, "codeblock_indented",
                                             "family", "monospace",
                                             "background-full-height", FALSE,
                                             "left-margin", 48,       // Larger margin to represent 4+ space indentation
                                             "right-margin", 12,      // Standard right margin
                                             "pixels-above-lines", 6, // Standard padding
                                             "pixels-below-lines", 6, // Standard padding
                                             "wrap-mode", GTK_WRAP_NONE, // Code blocks typically don't wrap
                                             "indent", 8,             // More prominent indent for indented blocks
                                             NULL);
        } else if (g_strcmp0(tag_name, "hr") == 0) {
            // Horizontal rule: create a full-width line effect
            // We insert line characters for visual effect but export as "---"
            tag = gtk_text_buffer_create_tag(buffer, "hr",
                                             "pixels-above-lines", 12,
                                             "pixels-below-lines", 12,
                                             "justification", GTK_JUSTIFY_LEFT,
                                             "foreground-rgba", NULL, // Will be set by theme update
                                             NULL);
        } else if (g_str_has_prefix(tag_name, "blockquote")) {
            // Support nested blockquotes by creating tags named
            // "blockquote1", "blockquote2", ...
            // Style with left margin only; no background colors
            int depth = 1;
            const char *p = tag_name + 10; // strlen("blockquote")
            if (p && *p >= '0' && *p <= '9') depth = MAX(1, atoi(p));
            int margin = 16 * depth; // 16px per nesting level
            tag = gtk_text_buffer_create_tag(buffer, tag_name,
                                             "left-margin", margin,
                                             "right-margin", 0,
                                             "pixels-above-lines", 2,
                                             "pixels-below-lines", 2,
                                             "paragraph-background", NULL,  // Explicitly remove paragraph background
                                             "background", NULL,            // Explicitly remove text background
                                             NULL);
        } else if (g_strcmp0(tag_name, "link") == 0) {
            tag = gtk_text_buffer_create_tag(buffer, "link",
                                             "foreground", "blue",
                                             "underline", PANGO_UNDERLINE_SINGLE,
                                             NULL);
        } else if (g_strcmp0(tag_name, "image") == 0) {
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

    // Store readable name via qdata and insert into cache
    if (tag) {
        if (G_UNLIKELY(quark_tag_name == 0))
            quark_tag_name = g_quark_from_static_string("tag-name");
        if (!g_object_get_qdata(G_OBJECT(tag), quark_tag_name))
            g_object_set_qdata_full(G_OBJECT(tag), quark_tag_name, g_strdup(tag_name), g_free);

        if (!g_hash_table_lookup(cache->map, tag_name))
            g_hash_table_insert(cache->map, g_strdup(tag_name), tag);
    }
    return tag;
}

static void update_blockquote_tag(GtkTextTag *tag, gpointer user_data) {
    (void)user_data;  // Mark unused parameter
    const char *name = g_object_get_data(G_OBJECT(tag), "tag-name");
    if (!name || !g_str_has_prefix(name, "blockquote")) return;
    // Ensure left margin is set consistently and no backgrounds
    int depth = 1; const char *p = name + 10; // strlen("blockquote")
    if (p && *p >= '0' && *p <= '9') depth = MAX(1, atoi(p));
    int margin = 16 * depth;
    g_object_set(tag, 
                 "left-margin", margin,
                 "paragraph-background", NULL,  // Explicitly clear paragraph background
                 "background", NULL,            // Explicitly clear text background
                 NULL);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Public API functions and event handlers
 * ═══════════════════════════════════════════════════════════════════════════════ */

void cm_render_update_theme_dependent_tags(GtkTextBuffer *buffer) {
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));

    AdwStyleManager *style_manager = adw_style_manager_get_default();
    AdwColorScheme color_scheme = adw_style_manager_get_color_scheme(style_manager);
    gboolean is_dark = (color_scheme == ADW_COLOR_SCHEME_FORCE_DARK || color_scheme == ADW_COLOR_SCHEME_PREFER_DARK);

    // GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer); // Unused variable
    
    // Ensure 'code' tag exists or create it before setting theme properties
    GtkTextTag *code_tag = cm_render_get_or_create_base_tag(buffer, "code");
    // Ensure 'codeblock' tag exists or create it
    GtkTextTag *codeblock_tag = cm_render_get_or_create_base_tag(buffer, "codeblock");
    // Ensure 'codeblock_indented' tag exists or create it
    GtkTextTag *codeblock_indented_tag = cm_render_get_or_create_base_tag(buffer, "codeblock_indented");
    // Ensure 'hr' tag exists or create it
    GtkTextTag *hr_tag = cm_render_get_or_create_base_tag(buffer, "hr");
    // Ensure 'link' tag exists or create it (though its base style is non-theme dependent)
    cm_render_get_or_create_base_tag(buffer, "link");


    // Get proper theme colors
    const char* code_fg_color = NULL;  // Will be set based on theme lookup
    const char* codeblock_fg_color = is_dark ? "#e0e0e0" : NULL;
    
    // Try to get the associated text view to lookup theme colors
    GtkWidget *text_view = g_object_get_data(G_OBJECT(buffer), "gtktext-view");
    GdkRGBA code_bg_rgba, code_fg_rgba, codeblock_bg_rgba;
    g_autofree char *code_bg_color_str = NULL;
    g_autofree char *code_fg_color_str = NULL;
    g_autofree char *codeblock_bg_color_str = NULL;
    
    if (text_view) {
        // Try different GNOME red color names for inline code
        const char* red_color_names[] = { 
            is_dark ? "red_4" : "red_1",
            is_dark ? "@red_4" : "@red_1", 
            is_dark ? "destructive_bg_color" : "destructive_bg_color",
            NULL 
        };
        
        // Try different GNOME red color names for inline code foreground
        const char* red_fg_color_names[] = { 
            is_dark ? "red_2" : "red_5",
            is_dark ? "@red_2" : "@red_5", 
            is_dark ? "destructive_color" : "destructive_color",
            NULL 
        };
        
        // Try different GNOME blue color names for code blocks
        const char* blue_color_names[] = { 
            is_dark ? "blue_4" : "blue_1",
            is_dark ? "@blue_4" : "@blue_1", 
            "accent_bg_color",  // Fallback to accent color
            NULL 
        };
        
        // Get red background color for inline code
        for (int i = 0; red_color_names[i] && !code_bg_color_str; i++) {
            if (theme_styles_get_color_with_alpha(text_view, red_color_names[i], is_dark ? 0.3 : 0.2, &code_bg_rgba)) {
                // Convert RGBA to string - use integer alpha to avoid locale decimal issues
                int alpha_int = (int)(code_bg_rgba.alpha * 1000); // Convert to integer (0.2 -> 200)
                code_bg_color_str = g_strdup_printf("rgba(%d,%d,%d,0.%03d)",
                                                    (int)(code_bg_rgba.red * 255),
                                                    (int)(code_bg_rgba.green * 255),
                                                    (int)(code_bg_rgba.blue * 255),
                                                    alpha_int);
                break;
            }
        }
        
        // Get red foreground color for inline code
        for (int i = 0; red_fg_color_names[i] && !code_fg_color_str; i++) {
            if (theme_styles_get_color_with_alpha(text_view, red_fg_color_names[i], 1.0, &code_fg_rgba)) {
                code_fg_color_str = g_strdup_printf("rgba(%d,%d,%d,1.000)", 
                                                    (int)(code_fg_rgba.red * 255), 
                                                    (int)(code_fg_rgba.green * 255), 
                                                    (int)(code_fg_rgba.blue * 255));
                break;
            }
        }
        
        // Get blue color for code blocks
        for (int i = 0; blue_color_names[i] && !codeblock_bg_color_str; i++) {
            if (theme_styles_get_color_with_alpha(text_view, blue_color_names[i], is_dark ? 0.3 : 0.5, &codeblock_bg_rgba)) {
                // Convert RGBA to string - use integer alpha to avoid locale decimal issues
                int alpha_int = (int)(codeblock_bg_rgba.alpha * 1000); // Convert to integer (0.5 -> 500)
                codeblock_bg_color_str = g_strdup_printf("rgba(%d,%d,%d,0.%03d)", 
                                                        (int)(codeblock_bg_rgba.red * 255), 
                                                        (int)(codeblock_bg_rgba.green * 255), 
                                                        (int)(codeblock_bg_rgba.blue * 255), 
                                                        alpha_int);
                break;
            }
        }
    }
    
    // Fallback to theme-appropriate colors if theme lookup failed
    const char* code_bg_color = code_bg_color_str ? code_bg_color_str : 
                                (is_dark ? "rgba(192, 97, 203, 0.3)" : "rgba(246, 97, 81, 0.2)"); // Red tones
    
    code_fg_color = code_fg_color_str ? code_fg_color_str :
                    (is_dark ? "#ff6b6b" : "#c92a2a"); // Red text colors
    
    const char* codeblock_bg_color = codeblock_bg_color_str ? codeblock_bg_color_str :
                                     (is_dark ? "rgba(28, 113, 216, 0.3)" : "rgba(153, 193, 241, 0.5)"); // Blue tones

    if (code_tag) {
        g_object_set(code_tag,
                     "background", code_bg_color,
                     "foreground", code_fg_color,
                     NULL);
    } else {
        g_warning("Failed to get or create 'code' tag during theme update.");
    }

    if (codeblock_tag) {
        // Only paragraph-wide background to avoid darker blue behind glyphs ("blue on blue").
        g_object_set(codeblock_tag,
                     "background", NULL,
                     "background-rgba", NULL,
                     "paragraph-background", codeblock_bg_color,
                     "foreground", codeblock_fg_color,
                     "background-full-height", TRUE,
                     NULL);
    } else {
         g_warning("Failed to get or create 'codeblock' tag during theme update.");
    }

    if (codeblock_indented_tag) {
        // Same colors as regular codeblock but with more prominent indentation
        g_object_set(codeblock_indented_tag,
                     "background", NULL,
                     "background-rgba", NULL,
                     "paragraph-background", codeblock_bg_color,
                     "foreground", codeblock_fg_color,
                     "background-full-height", TRUE,
                     NULL);
    } else {
         g_warning("Failed to get or create 'codeblock_indented' tag during theme update.");
    }

    if (hr_tag) {
        // Style horizontal rules with appropriate theme colors
        GdkRGBA hr_color;
        
        // Use theme border/outline color
        if (is_dark) {
            hr_color = (GdkRGBA){0.6, 0.6, 0.6, 0.8}; // Light gray for dark theme
        } else {
            hr_color = (GdkRGBA){0.4, 0.4, 0.4, 0.7}; // Dark gray for light theme
        }
        
        g_object_set(hr_tag,
                     "foreground-rgba", &hr_color,
                     NULL);
    } else {
         g_warning("Failed to get or create 'hr' tag during theme update.");
    }
    
    // Update blockquote margins consistently and set the stripe color tag
    GtkTextTagTable *tbl = gtk_text_buffer_get_tag_table(buffer);
    gtk_text_tag_table_foreach(tbl, update_blockquote_tag, GINT_TO_POINTER(is_dark));

    // Setup the stripe tag color (used for the inserted hair spaces)
    GtkTextTag *stripe = gtk_text_tag_table_lookup(tbl, "blockquote_stripe");
    if (!stripe) {
        stripe = gtk_text_buffer_create_tag(buffer, "blockquote_stripe",
                                            "foreground-rgba", &(GdkRGBA){0,0,0,0},
                                            NULL);
        g_object_set_data_full(G_OBJECT(stripe), "tag-name", g_strdup("blockquote_stripe"), g_free);
    }
    GdkRGBA stripe_rgba;
    if (is_dark) { stripe_rgba.red=0.8; stripe_rgba.green=0.8; stripe_rgba.blue=0.8; stripe_rgba.alpha=0.8; }
    else { stripe_rgba.red=0.7; stripe_rgba.green=0.7; stripe_rgba.blue=0.7; stripe_rgba.alpha=1.0; }
    g_object_set(stripe, "background-rgba", &stripe_rgba, NULL);
    
    // Color strings are automatically cleaned up with g_autofree
}

/* Legacy function - kept for compatibility */
static void cm_render_insert_with_active_tags(GtkTextBuffer *buffer, GtkTextIter *iter, const char *text, GSList *active_tags) {
    if (!text || g_utf8_strlen(text, -1) == 0) return;

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

    // Determine if current node is a block node and what kind of spacing it needs
    switch (type) {
        case CMARK_NODE_DOCUMENT:
            is_block_node = TRUE;
            needs_trailing_newline = FALSE; // Document doesn't need trailing newline
            break;
        case CMARK_NODE_BLOCK_QUOTE:
        case CMARK_NODE_CODE_BLOCK:
        case CMARK_NODE_HTML_BLOCK:
        case CMARK_NODE_THEMATIC_BREAK:
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            break;
        case CMARK_NODE_HEADING:
            // Headings should not force a blank line; keep a single trailing newline
            // But avoid extra spacing when followed by another heading
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            break;
        case CMARK_NODE_PARAGRAPH:
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            break;
        case CMARK_NODE_LIST:
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            break;
        case CMARK_NODE_ITEM:
            is_block_node = TRUE;
            needs_trailing_newline = TRUE;
            break;
        default:
            is_block_node = FALSE;
            needs_trailing_newline = FALSE;
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
            // Push list state for this nesting level
            {
                ListCtx ctx = {0};
                cmark_list_type lt = cmark_node_get_list_type(node);
                ctx.ordered = (lt == CMARK_ORDERED_LIST);
                ctx.tight = cmark_node_get_list_tight(node);
                if (ctx.ordered) {
                    int start = cmark_node_get_list_start(node);
                    if (start < 1) start = 1;
                    ctx.next_number = start;
                    cmark_delim_type dt = cmark_node_get_list_delim(node);
                    ctx.delim_char = (dt == CMARK_PAREN_DELIM) ? ')' : '.';
                }
                g_array_append_val(ol_counter_stack, ctx);
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
                    // Indent four spaces per level beyond the first, as per CommonMark guidance
                    if (depth > 1) {
                        GString *indent = g_string_sized_new(depth * 4);
                        for (int i = 1; i < depth; i++) g_string_append(indent, "    ");
                        cm_render_insert_with_active_tags(buffer, iter, indent->str, active_tags);
                        g_string_free(indent, TRUE);
                    }

                    // Render item marker based on current list context
                    int idx = ol_counter_stack->len - 1;
                    if (idx >= 0) {
                        ListCtx *lc = &g_array_index(ol_counter_stack, ListCtx, idx);
                        if (lc->ordered) {
                            int num = lc->next_number;
                            char prefix[32];
                            g_snprintf(prefix, sizeof(prefix), "%d%c ", num, lc->delim_char ? lc->delim_char : '.');
                            cm_render_insert_with_active_tags(buffer, iter, prefix, active_tags);
                            lc->next_number = num + 1;
                        } else {
                            if (g_use_visual_bullets) {
                                // Unordered: render visual bullets depending on depth
                                const char *bullet = "\xE2\x97\x8F "; // ● default
                                if (depth >= 3) bullet = "\xE2\x96\xA0 "; // ■
                                else if (depth == 2) bullet = "\xE2\x97\x8B "; // ○

                                // Ensure a tag exists to mark visual bullets for export replacement
                                GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(buffer);
                                GtkTextTag *ul_tag = gtk_text_tag_table_lookup(tt, "ul_bullet");
                                if (!ul_tag) ul_tag = gtk_text_buffer_create_tag(buffer, "ul_bullet", NULL);

                                // Mark start, insert bullet+space, tag it, then continue with content
                                GtkTextMark *m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                                cm_render_insert_with_active_tags(buffer, iter, bullet, active_tags);
                                GtkTextIter s, e;
                                gtk_text_buffer_get_iter_at_mark(buffer, &s, m);
                                e = s; gtk_text_iter_forward_char(&e); // bullet char
                                gtk_text_iter_forward_char(&e);        // trailing space
                                gtk_text_buffer_apply_tag(buffer, ul_tag, &s, &e);
                                gtk_text_buffer_delete_mark(buffer, m);
                            } else {
                                cm_render_insert_with_active_tags(buffer, iter, "- ", active_tags);
                            }
                        }
                    } else {
                        // Fallback when context stack is empty
                        if (g_use_visual_bullets) {
                            GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(buffer);
                            GtkTextTag *ul_tag = gtk_text_tag_table_lookup(tt, "ul_bullet");
                            if (!ul_tag) ul_tag = gtk_text_buffer_create_tag(buffer, "ul_bullet", NULL);
                            GtkTextMark *m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                            cm_render_insert_with_active_tags(buffer, iter, "\xE2\x97\x8F ", active_tags);
                            GtkTextIter s, e;
                            gtk_text_buffer_get_iter_at_mark(buffer, &s, m);
                            e = s; gtk_text_iter_forward_char(&e); gtk_text_iter_forward_char(&e);
                            gtk_text_buffer_apply_tag(buffer, ul_tag, &s, &e);
                            gtk_text_buffer_delete_mark(buffer, m);
                        } else {
                            cm_render_insert_with_active_tags(buffer, iter, "- ", active_tags);
                        }
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
                const char *code_content = cmark_node_get_literal(node);
                // Capture fenced code info string (language) when available
                const char *info_str = NULL;
                // libcmark provides cmark_node_get_fence_info() for fenced code blocks
                info_str = cmark_node_get_fence_info(node);
                
                // Improved heuristic to determine if this is a fenced or indented code block
                // Check if we have fence_info (language) or if the content suggests it's fenced
                gboolean is_fenced = FALSE;
                
                if (info_str && g_utf8_strlen(info_str, -1) > 0) {
                    // Has language info, definitely fenced
                    is_fenced = TRUE;
                } else if (code_content) {
                    // Analyze content to guess block type
                    // If all non-empty lines start with 4+ spaces, likely indented
                    // Otherwise, likely fenced (even without language)
                    const char *line = code_content;
                    gboolean all_lines_indented = TRUE;
                    gboolean has_content_lines = FALSE;
                    
                    while (*line) {
                        // Skip to next line or process current line
                        const char *line_end = strchr(line, '\n');
                        if (!line_end) line_end = line + strlen(line);
                        
                        // Check if line has content (not just whitespace)
                        gboolean line_has_content = FALSE;
                        for (const char *p = line; p < line_end; p++) {
                            if (*p != ' ' && *p != '\t') {
                                line_has_content = TRUE;
                                break;
                            }
                        }
                        
                        if (line_has_content) {
                            has_content_lines = TRUE;
                            // Count leading spaces/tabs
                            int leading_spaces = 0;
                            for (const char *p = line; p < line_end && (*p == ' ' || *p == '\t'); p++) {
                                leading_spaces += (*p == '\t') ? 4 : 1; // Tab counts as 4 spaces
                            }
                            
                            // If line doesn't start with 4+ spaces, not an indented block
                            if (leading_spaces < 4) {
                                all_lines_indented = FALSE;
                                break;
                            }
                        }
                        
                        // Move to next line
                        if (*line_end == '\n') line = line_end + 1;
                        else break;
                    }
                    
                    // If we have content and all lines are indented with 4+ spaces,
                    // it's likely an indented code block. Otherwise, assume fenced.
                    is_fenced = !(has_content_lines && all_lines_indented);
                }
                
                const char *tag_name = is_fenced ? "codeblock" : "codeblock_indented";
                
                cm_render_get_or_create_base_tag(buffer, tag_name); // Ensure tag exists

                if (code_content) {
                    // Mark start
                    GtkTextMark *pre_mark = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);

                    // Apply appropriate codeblock tag plus any inherited tags
                    char *temp_tag_name_for_list = g_strdup(tag_name);
                    GSList *tags_for_this_code_insertion = g_slist_prepend(active_tags, temp_tag_name_for_list);
                    cm_render_insert_with_active_tags(buffer, iter, code_content, tags_for_this_code_insertion);
                    g_free(tags_for_this_code_insertion->data);
                    g_slist_free_1(tags_for_this_code_insertion);

                    // Apply a unique metadata tag to hold language/info (only for fenced blocks)
                    if (is_fenced && info_str && *info_str) {
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
            // Insert proper GTK widget-based HR that resizes with window
            // Ensure HR starts at a new line
            if (!gtk_text_iter_starts_line(iter)) {
                gtk_text_buffer_insert(buffer, iter, "\n", -1);
            }

            // Insert HR marker text first for export detection
            GtkTextTag *hr_tag = tag_manager_get_or_create_base_tag(buffer, "hr");
            if (hr_tag) {
                // Insert visible marker that will be replaced by widget
                tag_manager_insert_with_active_tags(buffer, iter, "---", NULL);

                // Apply HR tag to the inserted text for export detection
                GtkTextIter start_mark, end_mark;
                gtk_text_buffer_get_iter_at_offset(buffer, &start_mark,
                    gtk_text_iter_get_offset(iter) - 3);
                end_mark = *iter;
                gtk_text_buffer_apply_tag(buffer, hr_tag, &start_mark, &end_mark);
            }

            // Now create the widget anchor to replace the text
            GtkTextIter hr_start, hr_end;
            gtk_text_buffer_get_iter_at_offset(buffer, &hr_start, gtk_text_iter_get_offset(iter) - 3);
            hr_end = *iter;

            // Delete the text and insert widget anchor
            gtk_text_buffer_delete(buffer, &hr_start, &hr_end);

            // Create HR widget with theme-aware styling
            GtkWidget *hr_widget = gtktext_hr_widget_new();
            if (!hr_widget) {
                g_warning("Failed to create HR widget");
                break;
            }

            GdkRGBA hr_color;
            if (theme_styles_get_color_with_alpha(NULL, "theme_fg_color", 0.3, &hr_color)) {
                gtktext_hr_widget_set_color(GTKTEXT_HR_WIDGET(hr_widget), &hr_color);
            }

            // Create widget anchor at the HR position
            GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buffer, &hr_start);
            if (anchor && G_IS_OBJECT(hr_widget)) {
                // Take a reference for storing in the anchor data
                g_object_ref_sink(hr_widget); // Convert floating reference to normal reference
                g_object_set_data_full(G_OBJECT(anchor), "hr-widget", hr_widget, (GDestroyNotify)g_object_unref);
            } else {
                g_warning("Failed to create anchor or invalid widget");
                // If hr_widget is floating, sink and unref; if not floating, just unref
                if (hr_widget) {
                    g_object_ref_sink(hr_widget);
                    g_object_unref(hr_widget);
                }
            }

            // Also store HR tag for export
            if (hr_tag) {
                g_object_set_data(G_OBJECT(anchor), "hr-tag", hr_tag);
            }

            // Update iter to position after anchor and add newline
            *iter = hr_start;
            gtk_text_iter_forward_char(iter); // Move past the anchor
            gtk_text_buffer_insert(buffer, iter, "\n", -1);
            break;
        case CMARK_NODE_PARAGRAPH:
            // Paragraphs themselves don't add a tag, but they manage spacing.
            // If we're inside a blockquote, insert a thin visual stripe before the text.
            {
                int bq_depth = 0;
                for (GSList *l = active_tags; l != NULL; l = l->next) {
                    const char *nm = (const char*)l->data;
                    if (nm && g_str_has_prefix(nm, "blockquote")) bq_depth++;
                }
                if (bq_depth > 0 && g_use_visual_bullets) {
                    GtkTextTag *stripe = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "blockquote_stripe");
                    if (!stripe) {
                        stripe = gtk_text_buffer_create_tag(buffer, "blockquote_stripe",
                                                            "foreground-rgba", &(GdkRGBA){0,0,0,0}, // hide any glyph
                                                            NULL);
                        g_object_set_data_full(G_OBJECT(stripe), "tag-name", g_strdup("blockquote_stripe"), g_free);
                    }
                    // Use several hair spaces U+200A to approximate a thin vertical rule
                    char stripe_run[64] = {0};
                    // 6 hair spaces ≈ thin border, tweakable
                    g_strlcpy(stripe_run, "\xE2\x80\x8A\xE2\x80\x8A\xE2\x80\x8A\xE2\x80\x8A\xE2\x80\x8A\xE2\x80\x8A", sizeof(stripe_run));
                    GtkTextMark *m = gtk_text_buffer_create_mark(buffer, NULL, iter, TRUE);
                    gtk_text_buffer_insert(buffer, iter, stripe_run, -1);
                    GtkTextIter s, e;
                    gtk_text_buffer_get_iter_at_mark(buffer, &s, m);
                    e = s; for (int i = 0; i < 6; i++) gtk_text_iter_forward_char(&e);
                    gtk_text_buffer_apply_tag(buffer, stripe, &s, &e);
                    gtk_text_buffer_delete_mark(buffer, m);
                }
            }
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
            // Preserve user-entered single newlines while editing by inserting an actual newline.
            // CommonMark treats this as a soft break (space) for HTML, but in an editor
            // we want the visual newline to remain intact after a reparse.
            cm_render_insert_with_active_tags(buffer, iter, "\n", active_tags);
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
                // Image title (optional)
                const char *img_title = cmark_node_get_title(node);
                // Prefer outer link's URL if image is inside a link
                const char *open_url = NULL;
                const char *open_title = NULL;
                cmark_node *parent = cmark_node_parent(node);
                if (parent && cmark_node_get_type(parent) == CMARK_NODE_LINK) {
                    open_url = cmark_node_get_url(parent);
                    open_title = cmark_node_get_title(parent);
                }
                if (!open_url || !*open_url) open_url = url;
                const char *tooltip_title = (open_title && *open_title) ? open_title : img_title;

                if (url && g_image_fetch_context && g_image_fetch_context->text_view) {
                    // Create the image widget that will immediately start fetching
                    GtkWidget *image_widget = create_image_widget(alt_text->str, url, open_url, tooltip_title);
                    
                    // Create a child anchor in the text buffer
                    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buffer, iter);
                    
                    // Add the widget to the text view at the anchor (only if text view is ready)
                    if (gtk_widget_get_mapped(GTK_WIDGET(g_image_fetch_context->text_view)) &&
                        gtk_widget_get_width(GTK_WIDGET(g_image_fetch_context->text_view)) > 1) {
                        gtk_text_view_add_child_at_anchor(g_image_fetch_context->text_view, image_widget, anchor);
                    }
                    
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
    
    // Pop from list context stack when leaving any list
    if (type == CMARK_NODE_LIST) {
        if (ol_counter_stack->len > 0) {
            g_array_remove_index(ol_counter_stack, ol_counter_stack->len - 1);
        }
    }

    // After processing a block node and its children, ensure it ends with a newline.
    // Then, using cmark SOURCEPOS, preserve any additional blank lines that existed
    // between this block and the next sibling in the original file.
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
        // Preserve original blank-line count between this node and its next sibling
        cmark_node *next_sibling = cmark_node_next(node);
        int gap = 0;
        if (next_sibling) {
            int end_line = cmark_node_get_end_line(node);
            int next_start = cmark_node_get_start_line(next_sibling);
            gap = next_start - end_line - 1; // number of blank lines in source

            // Special case: For consecutive headings with no gap, don't add extra newlines
            if (gap == 0 && type == CMARK_NODE_HEADING && cmark_node_get_type(next_sibling) == CMARK_NODE_HEADING) {
                // Do nothing - the single trailing newline is sufficient
            } else if (gap > 0) {
                for (int i = 0; i < gap; i++) {
                    gtk_text_buffer_insert(buffer, iter, "\n", -1);
                }
            }
        }
        // Fallbacks when SOURCEPOS doesn't expose the visual gap clearly
        if (gap == 0 && next_sibling && cmark_node_get_type(next_sibling) == CMARK_NODE_PARAGRAPH) {
            // Ensure a blank line before a paragraph that follows another block
            if (type == CMARK_NODE_PARAGRAPH ||
                type == CMARK_NODE_LIST ||
                type == CMARK_NODE_HEADING ||
                type == CMARK_NODE_CODE_BLOCK ||
                type == CMARK_NODE_HTML_BLOCK ||
                type == CMARK_NODE_BLOCK_QUOTE ||
                type == CMARK_NODE_THEMATIC_BREAK) {
                gtk_text_buffer_insert(buffer, iter, "\n", -1);
            }
        }
        // Fallback: list followed by list — ensure a visual blank line for readability if gap unknown
        if (gap == 0 && type == CMARK_NODE_LIST && next_sibling && cmark_node_get_type(next_sibling) == CMARK_NODE_LIST) {
            gtk_text_buffer_insert(buffer, iter, "\n", -1);
        }
    }

    // Ensure loose lists have a blank line between items even when SOURCEPOS
    // does not report the gap explicitly between item nodes.
    if (type == CMARK_NODE_ITEM) {
        cmark_node *pl = cmark_node_parent(node);
        if (pl && cmark_node_get_type(pl) == CMARK_NODE_LIST && !cmark_node_get_list_tight(pl)) {
            cmark_node *ns = cmark_node_next(node);
            if (ns && cmark_node_get_type(ns) == CMARK_NODE_ITEM) {
                // Ensure there are at least two consecutive newlines here
                GtkTextIter back = *iter;
                int nl = 0;
                if (gtk_text_iter_backward_char(&back) && gtk_text_iter_get_char(&back) == '\n') {
                    nl++;
                    GtkTextIter back2 = back;
                    if (gtk_text_iter_backward_char(&back2) && gtk_text_iter_get_char(&back2) == '\n') nl++;
                }
                if (nl < 2) {
                    gtk_text_buffer_insert(buffer, iter, "\n", -1);
                }
            }
        }
    }

    // Note: newline handling is done once above. Avoid duplicating here to prevent extra blank lines.

    // For list items, blank line spacing is preserved using SOURCEPOS logic above.
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
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), FALSE);
    g_return_val_if_fail(markdown_text != NULL, FALSE);
    g_return_val_if_fail(text_view == NULL || GTK_IS_TEXT_VIEW(text_view), FALSE);

    // Enhanced validation: check for reasonable input size (prevent DoS)
    size_t md_len = g_utf8_strlen(markdown_text, -1);
    if (md_len == 0) {
        g_debug("[parse] Empty markdown text provided, clearing buffer");
        gtk_text_buffer_set_text(buffer, "", 0);
        return TRUE;
    }
    if (md_len > 1000000) { // 1MB limit for safety
        g_warning("Markdown text too large (%zu chars), truncating for safety", md_len);
        // Could implement truncation here if needed
    }
    
    // Store image fetch context for recursive access
    ImageFetchContext *context = g_new(ImageFetchContext, 1);
    context->soup_session = soup_session;
    context->text_view = text_view;
    g_image_fetch_context = context;
    // Disable visual bullets in headless contexts (e.g., unit tests)
    g_use_visual_bullets = (text_view != NULL);

    // 1. Clear the buffer (suppress dirty marking while we render)
    render_set_suppress_reparse(buffer, TRUE);
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
    // Enable SOURCEPOS to preserve exact blank-line gaps between blocks
    size_t md_byte_len = strlen(markdown_text); // Use byte length for cmark parser
    int options = CMARK_OPT_SMART | CMARK_OPT_VALIDATE_UTF8 | CMARK_OPT_SOURCEPOS;
    g_debug("[parse] Starting CommonMark parsing with options: %d", options);
    g_debug("[parse] Input text length: %zu bytes (%zu chars)", md_byte_len, md_len);
    g_debug("[parse] First 100 chars: %.100s", markdown_text);

    cmark_parser *parser = cmark_parser_new(options);
    if (!parser) {
        g_warning("Failed to create cmark_parser.");
        return FALSE;
    }

    cmark_parser_feed(parser, markdown_text, md_byte_len);
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
    // List context stack (ordered start, delimiter, tightness) per nesting level
    GArray *ol_counter_stack = g_array_sized_new(FALSE, FALSE, sizeof(ListCtx), 8);

    cm_render_node_content_recursive(document, buffer, &iter, active_tags, ol_counter_stack);

    // Free the cmark document
    cmark_node_free(document);

    // 5. Update theme-dependent tags (like code block backgrounds)
    // Skip during headless tests where no Gtk initialization is present.
    if (text_view) {
        cm_render_update_theme_dependent_tags(buffer);

        // 6. Attach HR widgets to text view
        GtkTextIter start_iter, end_iter;
        gtk_text_buffer_get_bounds(buffer, &start_iter, &end_iter);

        GtkTextIter iter = start_iter;
        while (!gtk_text_iter_equal(&iter, &end_iter)) {
            GtkTextChildAnchor *anchor = gtk_text_iter_get_child_anchor(&iter);
            if (anchor) {
                GtkWidget *hr_widget = g_object_get_data(G_OBJECT(anchor), "hr-widget");
                if (hr_widget && GTKTEXT_IS_HR_WIDGET(hr_widget)) {
                    /* Only add widget if text view is properly allocated to prevent GTK warnings */
                    if (gtk_widget_get_mapped(GTK_WIDGET(text_view)) &&
                        gtk_widget_get_width(GTK_WIDGET(text_view)) > 1) {
                        gtk_text_view_add_child_at_anchor(text_view, hr_widget, anchor);
                    }
                    /* If text view isn't ready, widget will be added during next reparse */

                    // Update widget color to match current theme
                    GdkRGBA hr_color;
                    if (theme_styles_get_color_with_alpha(NULL, "theme_fg_color", 0.3, &hr_color)) {
                        gtktext_hr_widget_set_color(GTKTEXT_HR_WIDGET(hr_widget), &hr_color);
                    }
                }
            }
            if (!gtk_text_iter_forward_char(&iter)) break;
        }
    }

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
    render_set_suppress_reparse(buffer, FALSE);
    
    return TRUE;
}


// --- Implementation of cm_render_buffer_to_markdown and its helpers ---


char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer) {
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), g_strdup(""));

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);

    if (gtk_text_iter_equal(&start, &end)) {
        g_debug("[export] Empty buffer, returning empty string");
        return g_strdup("");
    }

    // Enhanced validation: check buffer size for performance
    gint char_count = gtk_text_buffer_get_char_count(buffer);
    if (char_count > 500000) { // 500K chars limit for performance
        g_warning("[export] Large buffer (%d chars) may impact performance", char_count);
    }
    
    GString *md = g_string_new("");
    
    // Check user preference for heading format once at the beginning
    g_autoptr(GSettings) settings = g_settings_new("org.gtk.gtktext");
    g_autofree gchar *heading_format = g_settings_get_string(settings, "heading-format");
    gboolean use_setext = g_strcmp0(heading_format, "setext") == 0;
    
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

    // Setext heading support
    gboolean currently_in_heading = FALSE;
    int current_heading_level = 0;
    int previous_heading_level = 0;
    GString *current_heading_text = NULL;

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

    while(!gtk_text_iter_is_end(&iter)) {
        gunichar current_char = gtk_text_iter_get_char(&iter);

        // Check for child anchors (embedded widgets like images or HR)
        GtkTextChildAnchor *child_anchor = gtk_text_iter_get_child_anchor(&iter);
        if (child_anchor) {
            // First check if this is an HR anchor
            if (g_object_get_data(G_OBJECT(child_anchor), "hr-widget")) {
                // This is an HR widget anchor, export as horizontal rule
                g_string_append(md, "---\n");
                gtk_text_iter_forward_char(&iter);
                continue;
            }

            // Get the widgets attached to this anchor
            guint widget_count = 0;
            GtkWidget **widgets = gtk_text_child_anchor_get_widgets(child_anchor, &widget_count);

            for (guint i = 0; i < widget_count; i++) {
                GtkWidget *widget = widgets[i];

                // Check if this is an HR widget
                if (GTKTEXT_IS_HR_WIDGET(widget)) {
                    // Export as horizontal rule
                    g_string_append(md, "---\n");
                    break; // Only one HR per anchor
                }

                // Check if this is an image widget (should be a box containing our image)
                const char *image_url = g_object_get_data(G_OBJECT(widget), "image-url");
                const char *image_alt = g_object_get_data(G_OBJECT(widget), "image-alt");

                if (image_url) {
                    // This is an image widget, generate markdown
                    g_debug("[export] Found image widget with URL: %s, alt: %s", 
                           image_url, image_alt ? image_alt : "(none)");
                    
                    // Prefer an explicit open-url stored on the widget
                    const char *link_url = g_object_get_data(G_OBJECT(widget), "open-url");
                    const char *link_title = NULL;
                    // If no explicit open-url, fall back to tags at this iter (legacy detection)
                    if (!link_url || !*link_url || g_strcmp0(link_url, image_url) == 0) {
                        GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
                        for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
                            GtkTextTag *tag = GTK_TEXT_TAG(l->data);
                            const char *tag_name = get_tag_name_safe(tag);
                            if (tag_name && (g_str_has_prefix(tag_name, "link_") || g_str_has_prefix(tag_name, "link-"))) {
                                link_url = g_object_get_data(G_OBJECT(tag), "link-url");
                                link_title = g_object_get_data(G_OBJECT(tag), "link-title");
                                break;
                            }
                        }
                        g_slist_free(tags_at_iter);
                        if (link_url && g_strcmp0(link_url, image_url) == 0) link_url = NULL; // treat as non-link
                    }
                    
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
                else if (g_strcmp0(tag_name, "codeblock") == 0 || g_strcmp0(tag_name, "codeblock_indented") == 0) iter_is_codeblock_char = TRUE;
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

            // Replace visual unordered bullets (●/○/■ tagged with ul_bullet) with '-' for Markdown export
            if (!currently_in_codeblock) {
                GtkTextIter tmp = iter;
                // Skip leading spaces (indentation)
                while (!gtk_text_iter_is_end(&tmp) && gtk_text_iter_get_char(&tmp) == ' ') {
                    g_string_append_c(md, ' ');
                    gtk_text_iter_forward_char(&tmp);
                }
                // Detect visual bullets (● U+25CF, ○ U+25CB, ■ U+25A0) and emit '- '
                gunichar bullet = gtk_text_iter_get_char(&tmp);
                if (bullet == 0x25CF || bullet == 0x25CB || bullet == 0x25A0) {
                    // Append standard Markdown bullet and skip visual bullet+space in buffer
                    g_string_append(md, "- ");
                    // Advance tmp by bullet and following space
                    gtk_text_iter_forward_char(&tmp);
                    if (!gtk_text_iter_is_end(&tmp)) gtk_text_iter_forward_char(&tmp);
                    iter = tmp;
                    at_line_start = FALSE;
                    continue;
                }
                // Consume the indentation we already emitted and resync current_char
                iter = tmp;
                current_char = gtk_text_iter_get_char(&iter);
            }

            if (!currently_in_codeblock) {
                // Determine current heading level
                previous_heading_level = current_heading_level;
                if (iter_is_h1) current_heading_level = 1;
                else if (iter_is_h2) current_heading_level = 2;
                else if (iter_is_h3) current_heading_level = 3;
                else if (iter_is_h4) current_heading_level = 4;
                else if (iter_is_h5) current_heading_level = 5;
                else if (iter_is_h6) current_heading_level = 6;
                else current_heading_level = 0;

                // Handle heading level transitions
                if (current_heading_level != previous_heading_level) {
                    // Finish previous heading if it was Setext (H1/H2) and setext is enabled
                    if (use_setext && previous_heading_level >= 1 && previous_heading_level <= 2 && 
                        current_heading_text && current_heading_text->len > 0) {
                        
                        g_string_append(md, current_heading_text->str);
                        g_string_append_c(md, '\n');
                        
                        if (previous_heading_level == 1) {
                            // H1: underline with = characters
                            for (guint i = 0; i < current_heading_text->len; i++) {
                                g_string_append_c(md, '=');
                            }
                        } else {
                            // H2: underline with - characters  
                            for (guint i = 0; i < current_heading_text->len; i++) {
                                g_string_append_c(md, '-');
                            }
                        }
                        g_string_append_c(md, '\n');
                    }
                    
                    // Start new heading
                    if (current_heading_level >= 1 && current_heading_level <= 6) {
                        currently_in_heading = TRUE;
                        
                        if (current_heading_text) {
                            g_string_free(current_heading_text, TRUE);
                            current_heading_text = NULL;
                        }
                        
                        if (use_setext && current_heading_level <= 2) {
                            // For H1/H2 with setext preference, prepare to collect text 
                            current_heading_text = g_string_new("");
                        } else {
                            // For all other cases, use ATX format immediately
                            for (int i = 0; i < current_heading_level; i++) {
                                g_string_append_c(md, '#');
                            }
                            g_string_append_c(md, ' ');
                        }
                    } else {
                        // No longer in a heading
                        currently_in_heading = FALSE;
                        if (current_heading_text) {
                            g_string_free(current_heading_text, TRUE);
                            current_heading_text = NULL;
                        }
                    }
                }

                if (current_heading_level == 0 || current_heading_level >= 3) { 
                    GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");
                    GtkTextTag *codeblock_indented_tag = gtk_text_tag_table_lookup(tag_table, "codeblock_indented");
                    gboolean is_fenced_block = codeblock_tag && gtk_text_iter_has_tag(&iter, codeblock_tag);
                    gboolean is_indented_block = codeblock_indented_tag && gtk_text_iter_has_tag(&iter, codeblock_indented_tag);
                    
                    if (is_fenced_block) {
                        if (iter_code_info && *iter_code_info) {
                            g_string_append_printf(md, "```%s\n", iter_code_info);
                        } else {
                            g_string_append(md, "```\n");
                        }
                        currently_in_codeblock = TRUE;
                    } else if (is_indented_block) {
                        // For indented code blocks, we don't add fence markers
                        // The indentation is preserved in the content
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
                // For Setext headings (H1/H2), collect text only if setext format is enabled and we have a collection buffer
                if (currently_in_heading && current_heading_text) {
                    g_string_append_unichar(current_heading_text, current_char);
                } else {
                    g_string_append_unichar(md, current_char); // Append the character itself
                }
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
                GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");
                GtkTextTag *codeblock_indented_tag = gtk_text_tag_table_lookup(tag_table, "codeblock_indented");
                gboolean next_has_codeblock = (codeblock_tag && gtk_text_iter_has_tag(&next_char_iter, codeblock_tag)) ||
                                             (codeblock_indented_tag && gtk_text_iter_has_tag(&next_char_iter, codeblock_indented_tag));
                
                if (gtk_text_iter_is_end(&next_char_iter) || !next_has_codeblock) {
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
    
    // Handle any remaining Setext heading at the end of buffer
    if (use_setext && current_heading_level >= 1 && current_heading_level <= 2 && 
        current_heading_text && current_heading_text->len > 0) {
        
        g_string_append(md, current_heading_text->str);
        g_string_append_c(md, '\n');
        
        if (current_heading_level == 1) {
            // H1: underline with = characters
            for (guint i = 0; i < current_heading_text->len; i++) {
                g_string_append_c(md, '=');
            }
        } else {
            // H2: underline with - characters  
            for (guint i = 0; i < current_heading_text->len; i++) {
                g_string_append_c(md, '-');
            }
        }
        g_string_append_c(md, '\n');
    }
    
    // Clean up heading state
    if (current_heading_text) {
        g_string_free(current_heading_text, TRUE);
        current_heading_text = NULL;
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

char* cm_render_selection_to_markdown(GtkTextBuffer *buffer, const GtkTextIter *start_iter, const GtkTextIter *end_iter) {
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), g_strdup(""));
    g_return_val_if_fail(start_iter != NULL, g_strdup(""));
    g_return_val_if_fail(end_iter != NULL, g_strdup(""));

    // Check if selection is valid and not empty
    if (gtk_text_iter_equal(start_iter, end_iter)) {
        g_debug("[export] Empty selection, returning empty string");
        return g_strdup("");
    }

    // Ensure start comes before end
    GtkTextIter start_copy = *start_iter;
    GtkTextIter end_copy = *end_iter;
    gtk_text_iter_order(&start_copy, &end_copy);

    // Check user preference for heading format
    g_autoptr(GSettings) settings = g_settings_new("org.gtk.gtktext");
    g_autofree gchar *heading_format = g_settings_get_string(settings, "heading-format");
    gboolean use_setext = g_strcmp0(heading_format, "setext") == 0;

    GString *md = g_string_new("");
    GtkTextIter iter = start_copy;

    gboolean currently_in_bold = FALSE;
    gboolean currently_in_italic = FALSE;
    gboolean currently_in_code = FALSE;
    gboolean currently_in_codeblock = FALSE;
    gboolean currently_in_link = FALSE;
    const char *current_link_url = NULL;
    const char *current_link_title = NULL;
    GString *current_link_text = NULL;
    gboolean at_line_start = TRUE;

    // Setext heading support
    gboolean currently_in_heading = FALSE;
    int current_heading_level = 0;
    GString *current_heading_text = NULL;

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

    while (!gtk_text_iter_equal(&iter, &end_copy)) {
        gunichar current_char = gtk_text_iter_get_char(&iter);

        // Check for child anchors (embedded widgets like images or HR)
        GtkTextChildAnchor *child_anchor = gtk_text_iter_get_child_anchor(&iter);
        if (child_anchor) {
            // First check if this is an HR anchor
            if (g_object_get_data(G_OBJECT(child_anchor), "hr-widget")) {
                g_string_append(md, "---\n");
                gtk_text_iter_forward_char(&iter);
                continue;
            }

            // Get the widgets attached to this anchor
            guint widget_count = 0;
            GtkWidget **widgets = gtk_text_child_anchor_get_widgets(child_anchor, &widget_count);

            for (guint i = 0; i < widget_count; i++) {
                GtkWidget *widget = widgets[i];

                // Check if this is an HR widget
                if (GTKTEXT_IS_HR_WIDGET(widget)) {
                    g_string_append(md, "---\n");
                    break;
                }

                // Check if this is an image widget
                const char *image_url = g_object_get_data(G_OBJECT(widget), "image-url");
                const char *image_alt = g_object_get_data(G_OBJECT(widget), "image-alt");

                if (image_url) {
                    g_debug("[export] Found image widget with URL: %s, alt: %s",
                           image_url, image_alt ? image_alt : "(none)");

                    const char *link_url = g_object_get_data(G_OBJECT(widget), "open-url");
                    if (!link_url || !*link_url || g_strcmp0(link_url, image_url) == 0) {
                        // No clickable link, just an image
                        g_string_append_printf(md, "![%s](%s)",
                                             image_alt ? image_alt : "", image_url);
                    } else {
                        // Clickable image
                        g_string_append_printf(md, "[![%s](%s)](%s)",
                                             image_alt ? image_alt : "", image_url, link_url);
                    }
                    break;
                }
            }
            g_free(widgets);
            gtk_text_iter_forward_char(&iter);
            continue;
        }

        // Get all tags at this position
        GSList *tags = gtk_text_iter_get_tags(&iter);

        // Check for various formatting tags
        gboolean iter_is_bold = FALSE;
        gboolean iter_is_italic = FALSE;
        gboolean iter_is_code = FALSE;
        gboolean iter_is_codeblock_char = FALSE;
        gboolean iter_is_heading = FALSE;
        int iter_heading_level = 0;
        gboolean iter_is_link = FALSE;
        const char *iter_link_url = NULL;
        const char *iter_link_title = NULL;
        const char *iter_code_info = NULL;

        for (GSList *tagp = tags; tagp != NULL; tagp = tagp->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(tagp->data);
            const char *tag_name = get_tag_name_safe(tag);

            if (!tag_name) continue;

            if (g_strcmp0(tag_name, "bold") == 0) {
                iter_is_bold = TRUE;
            } else if (g_strcmp0(tag_name, "italic") == 0) {
                iter_is_italic = TRUE;
            } else if (g_strcmp0(tag_name, "code") == 0) {
                iter_is_code = TRUE;
            } else if (g_str_has_prefix(tag_name, "codeblock")) {
                iter_is_codeblock_char = TRUE;
                if (g_str_has_prefix(tag_name, "codeblock_")) {
                    iter_code_info = tag_name + 10; // Skip "codeblock_"
                }
            } else if (g_str_has_prefix(tag_name, "heading_")) {
                iter_is_heading = TRUE;
                iter_heading_level = g_ascii_digit_value(tag_name[8]);
            } else if (g_str_has_prefix(tag_name, "link_")) {
                iter_is_link = TRUE;
                iter_link_url = g_object_get_data(G_OBJECT(tag), "link-url");
                iter_link_title = g_object_get_data(G_OBJECT(tag), "link-title");
            }
        }
        g_slist_free(tags);

        // Handle heading changes
        if (iter_is_heading != currently_in_heading || iter_heading_level != current_heading_level) {
            if (currently_in_heading && current_heading_text && current_heading_text->len > 0) {
                // End current heading
                if (use_setext && (current_heading_level == 1 || current_heading_level == 2)) {
                    // Use setext style for h1 and h2
                    g_string_append_c(md, '\n');
                    char underline_char = (current_heading_level == 1) ? '=' : '-';
                    for (gsize i = 0; i < current_heading_text->len; i++) {
                        g_string_append_c(md, underline_char);
                    }
                    g_string_append_c(md, '\n');
                } else {
                    // Use ATX style
                    g_string_append_c(md, '\n');
                }
                g_string_free(current_heading_text, TRUE);
                current_heading_text = NULL;
            }

            if (iter_is_heading) {
                // Start new heading
                if (use_setext && (iter_heading_level == 1 || iter_heading_level == 2)) {
                    // For setext, just collect the text
                    current_heading_text = g_string_new("");
                } else {
                    // For ATX, add the prefix
                    for (int i = 0; i < iter_heading_level; i++) {
                        g_string_append_c(md, '#');
                    }
                    g_string_append_c(md, ' ');
                }
            }

            currently_in_heading = iter_is_heading;
            current_heading_level = iter_heading_level;
        }

        // Handle link changes
        if (iter_is_link != currently_in_link ||
            (iter_is_link && g_strcmp0(iter_link_url, current_link_url) != 0)) {

            if (currently_in_link && current_link_text && current_link_text->len > 0) {
                // End current link
                g_string_append_printf(md, "](%s", current_link_url ? current_link_url : "");
                if (current_link_title && *current_link_title) {
                    g_string_append_printf(md, " \"%s\"", current_link_title);
                }
                g_string_append_c(md, ')');
                g_string_free(current_link_text, TRUE);
                current_link_text = NULL;
            }

            if (iter_is_link && iter_link_url && *iter_link_url) {
                // Start new link
                g_string_append_c(md, '[');
                current_link_text = g_string_new("");
            }

            currently_in_link = iter_is_link;
            current_link_url = iter_link_url;
            current_link_title = iter_link_title;
        }

        // Handle code block changes
        if (iter_is_codeblock_char != currently_in_codeblock) {
            if (iter_is_codeblock_char && !currently_in_codeblock) {
                // Start of code block
                if (at_line_start) {
                    GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");
                    GtkTextTag *codeblock_indented_tag = gtk_text_tag_table_lookup(tag_table, "codeblock_indented");
                    gboolean is_fenced_block = codeblock_tag && gtk_text_iter_has_tag(&iter, codeblock_tag);
                    gboolean is_indented_block = codeblock_indented_tag && gtk_text_iter_has_tag(&iter, codeblock_indented_tag);

                    if (is_fenced_block) {
                        if (iter_code_info && *iter_code_info) {
                            g_string_append_printf(md, "```%s\n", iter_code_info);
                        } else {
                            g_string_append(md, "```\n");
                        }
                        currently_in_codeblock = TRUE;
                    } else if (is_indented_block) {
                        // For indented code blocks, we don't add fence markers
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
                    g_string_append_c(md, '*');
                    currently_in_italic = TRUE;
                } else if (!iter_is_italic && currently_in_italic) {
                    g_string_append_c(md, '*');
                    currently_in_italic = FALSE;
                }
            }

            // Handle inline code
            if (iter_is_code && !currently_in_code) {
                g_string_append_c(md, '`');
                currently_in_code = TRUE;
            } else if (!iter_is_code && currently_in_code) {
                g_string_append_c(md, '`');
                currently_in_code = FALSE;
            }

            // Add the character to output
            if (currently_in_heading && current_heading_text) {
                g_string_append_unichar(current_heading_text, current_char);
            }
            if (currently_in_link && current_link_text) {
                g_string_append_unichar(current_link_text, current_char);
            }

            g_string_append_unichar(md, current_char);
        }

        // Track line start position
        at_line_start = (current_char == '\n');

advance_only:
        gtk_text_iter_forward_char(&iter);
    }

    // Close any remaining open formatting
    if (currently_in_link && current_link_text) {
        g_string_append_printf(md, "](%s", current_link_url ? current_link_url : "");
        if (current_link_title && *current_link_title) {
            g_string_append_printf(md, " \"%s\"", current_link_title);
        }
        g_string_append_c(md, ')');
        g_string_free(current_link_text, TRUE);
    }

    if (currently_in_heading && current_heading_text && current_heading_text->len > 0) {
        if (use_setext && (current_heading_level == 1 || current_heading_level == 2)) {
            g_string_append_c(md, '\n');
            char underline_char = (current_heading_level == 1) ? '=' : '-';
            for (gsize i = 0; i < current_heading_text->len; i++) {
                g_string_append_c(md, underline_char);
            }
            g_string_append_c(md, '\n');
        }
        g_string_free(current_heading_text, TRUE);
    }

    if (currently_in_codeblock) {
        if (md->len > 0 && md->str[md->len -1] != '\n') {
            g_string_append_c(md, '\n');
        }
        g_string_append(md, "```\n");
    }

    if (currently_in_bold && currently_in_italic) {
        g_string_append(md, "***");
    } else {
        if (currently_in_bold) {
            g_string_append(md, "**");
        }
        if (currently_in_italic) {
            g_string_append_c(md, '*');
        }
    }

    if (currently_in_code) {
        g_string_append_c(md, '`');
    }

    return g_string_free(md, FALSE);
}
