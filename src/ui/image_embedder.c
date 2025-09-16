/* C ULTRA-MIN TEMPLATE
   Purpose: Image embedding functionality for GTK text views
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - ui/image_embedder.c
   Changed: Extracted image embedding from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <gtktext/ui/image_embedder.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Local type definitions
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    GtkTextBuffer *buffer;
    GtkTextView *view;
    GtkTextTag *hidden;
} EmbedCtx;

typedef struct {
    int start;
    int end;
} Range;

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal helper functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Click handler for embedded images: opens the image URL via GtkUriLauncher
 */
static void on_embedded_image_pressed(GtkGestureClick *gesture,
                                     gint n_press,
                                     gdouble x,
                                     gdouble y,
                                     gpointer user_data)
{
    (void)gesture;  // Unused parameters
    (void)n_press;
    (void)x;
    (void)y;

    GtkWidget *widget = GTK_WIDGET(user_data);
    /* Prefer an outer link target if provided; fall back to the image URL */
    const char *url = (const char*) g_object_get_data(G_OBJECT(widget), "open-url");
    if (!url) url = (const char*) g_object_get_data(G_OBJECT(widget), "image-url");
    if (!url || !*url) return;

    GtkWidget *view = gtk_widget_get_ancestor(widget, GTK_TYPE_TEXT_VIEW);
    if (!view) return;

    GtkWidget *window = gtk_widget_get_ancestor(view, GTK_TYPE_WINDOW);
    if (!window) return;

    g_debug("Opening image URL: %s", url);

    /* Use GtkUriLauncher for opening URLs in GTK 4.10+ */
    GtkUriLauncher *launcher = gtk_uri_launcher_new(url);
    gtk_uri_launcher_launch(launcher, GTK_WINDOW(window), NULL, NULL, NULL);
    g_object_unref(launcher);
}

/**
 * Process each text tag to embed images
 */
static void embed_foreach_tag(GtkTextTag *tag, gpointer user_data)
{
    EmbedCtx *c = (EmbedCtx*)user_data;
    if (!tag || !c || !c->buffer || !c->view) return;
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tag), "image-embedded")) == 1) return;

    gchar *name = NULL;
    g_object_get(tag, "name", &name, NULL);
    if (!name) return;

    /* Only treat tags as images if they carry actual image metadata */
    gboolean is_image = FALSE;
    if (g_str_has_prefix(name, "image_")) {
        /* Skip the alt-hidden helper tag if present from older buffers */
        if (g_strcmp0(name, "image_alt_hidden") != 0) {
            const char *has_url = g_object_get_data(G_OBJECT(tag), "image-url");
            if (has_url && *has_url) is_image = TRUE;
        }
    }
    if (is_image) {
        g_debug("[embed] found image tag: %s", name);
    }
    g_free(name);
    if (!is_image) return;

    const char *url = (const char*) g_object_get_data(G_OBJECT(tag), "image-url");
    const char *title = (const char*) g_object_get_data(G_OBJECT(tag), "image-title");
    const char *alt = (const char*) g_object_get_data(G_OBJECT(tag), "image-alt");

    g_debug("[embed] image tag data - url=%s, title=%s, alt=%s",
            url ? url : "(null)", title ? title : "(null)", alt ? alt : "(null)");

    /* First collect all [start,end) offsets for this tag */
    GArray *ranges = g_array_new(FALSE, FALSE, sizeof(Range));

    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(c->buffer, &iter);
    gboolean inside = gtk_text_iter_has_tag(&iter, tag);
    GtkTextIter range_start;
    if (inside) range_start = iter;

    while (gtk_text_iter_forward_char(&iter)) {
        gboolean has_tag = gtk_text_iter_has_tag(&iter, tag);
        if (!inside && has_tag) {
            /* Starting a new range */
            inside = TRUE;
            range_start = iter;
            gtk_text_iter_backward_char(&range_start);  /* Include the character that started the range */
        } else if (inside && !has_tag) {
            /* Ending the current range */
            inside = FALSE;
            Range r = { gtk_text_iter_get_offset(&range_start), gtk_text_iter_get_offset(&iter) };
            g_array_append_val(ranges, r);
        }
    }
    /* Handle case where tag extends to end of buffer */
    if (inside) {
        Range r = { gtk_text_iter_get_offset(&range_start), gtk_text_iter_get_offset(&iter) };
        g_array_append_val(ranges, r);
    }

    g_debug("[embed] found %u ranges for this image tag", ranges->len);

    /* Process ranges in reverse order to maintain text positions */
    for (int i = ranges->len - 1; i >= 0; i--) {
        Range *r = &g_array_index(ranges, Range, i);

        GtkTextIter start, end;
        gtk_text_buffer_get_iter_at_offset(c->buffer, &start, r->start);
        gtk_text_buffer_get_iter_at_offset(c->buffer, &end, r->end);

        gchar *text = gtk_text_buffer_get_text(c->buffer, &start, &end, FALSE);
        g_debug("[embed] range %d: offset %d-%d, text='%s'", i, r->start, r->end, text ? text : "(null)");

        /* Create image widget */
        GtkWidget *child = NULL;

        if (g_str_has_prefix(url, "http://") || g_str_has_prefix(url, "https://")) {
            /* Network image: create a picture that will load asynchronously */
            GtkWidget *pic = gtk_picture_new();
            gtk_picture_set_alternative_text(GTK_PICTURE(pic), alt ? alt : "Image");
            gtk_picture_set_can_shrink(GTK_PICTURE(pic), TRUE);

            /* Set a reasonable size constraint */
            gtk_widget_set_size_request(pic, -1, 200);  /* Max height 200px, width auto */

            /* Store URL for click handler */
            g_object_set_data_full(G_OBJECT(pic), "image-url", g_strdup(url), g_free);
            if (title) {
                g_object_set_data_full(G_OBJECT(pic), "image-title", g_strdup(title), g_free);
            }

            /* Add click gesture */
            GtkGesture *click = gtk_gesture_click_new();
            gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));
            g_signal_connect(click, "pressed", G_CALLBACK(on_embedded_image_pressed), pic);

            /* Start loading the image asynchronously */
            gtk_picture_set_filename(GTK_PICTURE(pic), url);  /* This will trigger async load */

            child = pic;
        } else {
            /* Local file or data URL: try to load directly */
            GtkWidget *pic = gtk_picture_new_for_filename(url);
            gtk_picture_set_alternative_text(GTK_PICTURE(pic), alt ? alt : "Image");
            gtk_picture_set_can_shrink(GTK_PICTURE(pic), TRUE);
            gtk_widget_set_size_request(pic, -1, 200);

            g_object_set_data_full(G_OBJECT(pic), "image-url", g_strdup(url), g_free);
            if (title) {
                g_object_set_data_full(G_OBJECT(pic), "image-title", g_strdup(title), g_free);
            }

            GtkGesture *click = gtk_gesture_click_new();
            gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));
            g_signal_connect(click, "pressed", G_CALLBACK(on_embedded_image_pressed), pic);

            child = pic;
        }

        if (child) {
            /* Create text child anchor and insert widget */
            GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(c->buffer, &start);
            gtk_text_view_add_child_at_anchor(c->view, child, anchor);

            /* Apply hidden tag to the alt text to make it transparent */
            if (text && *text) {
                GtkTextIter after;
                gtk_text_buffer_get_iter_at_child_anchor(c->buffer, &after, anchor);
                gtk_text_buffer_get_iter_at_offset(c->buffer, &end, r->end + 1); /* +1 for the anchor */
                if (gtk_text_iter_compare(&after, &end) < 0) {
                    gtk_text_buffer_apply_tag(c->buffer, c->hidden, &after, &end);
                }
            }
        }

        g_free(text);
    }

    g_array_free(ranges, TRUE);

    /* Mark this tag as processed */
    g_object_set_data(G_OBJECT(tag), "image-embedded", GINT_TO_POINTER(1));
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Image embedding functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void image_embedder_embed_images_in_text_view(GtkTextView *text_view)
{
    if (!text_view || !GTK_IS_TEXT_VIEW(text_view)) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    if (!buffer) return;
    g_message("[embed] *** SCANNING BUFFER FOR IMAGES ***");

    /* Create or lookup a tag used to hide alt text */
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    /* Use a tag name that does NOT collide with real image_* tags */
    GtkTextTag *hidden = gtk_text_tag_table_lookup(table, "img_alt_hidden");
    if (!hidden) {
        /* Backwards-compatibility: if previous name exists, reuse it */
        hidden = gtk_text_tag_table_lookup(table, "image_alt_hidden");
    }
    if (!hidden) {
        /* Avoid object replacement glyphs: make text transparent instead of invisible */
        GdkRGBA transparent = {0, 0, 0, 0};
        hidden = gtk_text_buffer_create_tag(buffer, "img_alt_hidden",
                                           "foreground-rgba", &transparent,
                                           NULL);
    }

    EmbedCtx ctx = { buffer, text_view, hidden };
    gtk_text_tag_table_foreach(table, embed_foreach_tag, &ctx);
}