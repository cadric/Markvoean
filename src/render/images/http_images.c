/* C ULTRA-MIN TEMPLATE
   Purpose: HTTP image processing and remote image fetching for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - render/images/http_images.c
   Changed: Extracted HTTP image processing from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/render/images/http_images.h>

#ifdef HAVE_LIBSOUP
#include <gtktext/ui/image_embedder.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - HTTP image processing functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void http_images_remote_image_ctx_free(RemoteImageCtx *c)
{
    if (!c) return;
    if (c->buffer) g_object_unref(c->buffer);
    if (c->view) g_object_unref(c->view);
    if (c->tag) g_object_unref(c->tag);
    if (c->session) g_object_unref(c->session);
    if (c->msg) g_object_unref(c->msg);
    if (c->mark) gtk_text_buffer_delete_mark(c->buffer, c->mark);
    g_free(c->url);
    g_free(c->open_url);
    g_free(c->title);
    g_free(c->alt);
    g_free(c);
}

void http_images_on_picture_paintable_notify(GObject *object, GParamSpec *pspec,
                                           gpointer user_data)
{
    (void)pspec;
    const char *src = (const char*)user_data;
    GtkPicture *pic = GTK_PICTURE(object);
    GdkPaintable *p = gtk_picture_get_paintable(pic);
    if (p) {
        int iw = gdk_paintable_get_intrinsic_width(p);
        int ih = gdk_paintable_get_intrinsic_height(p);
        g_debug("[embed] picture loaded: %s (%dx%d)", src ? src : "(null)", iw, ih);
    } else {
        g_debug("[embed] picture paintable cleared: %s", src ? src : "(null)");
    }
}

void http_images_free_user_data_notify(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

void http_images_on_http_image_fetched(SoupSession *session, GAsyncResult *res,
                                     gpointer user_data)
{
    RemoteImageCtx *ctx = (RemoteImageCtx*)user_data;
    GError *err = NULL;
    GBytes *bytes = soup_session_send_and_read_finish(session, res, &err);
    if (!bytes) {
        g_debug("Image fetch failed for %s: %s", ctx->url,
                err ? err->message : "unknown");
        g_clear_error(&err);
        http_images_remote_image_ctx_free(ctx);
        return;
    }
    /* Check HTTP status and content type */
    guint status = soup_message_get_status(ctx->msg);
    if (status < 200 || status >= 300) {
        g_debug("HTTP fetch status %u for %s; skipping image", status, ctx->url);
        g_bytes_unref(bytes);
        http_images_remote_image_ctx_free(ctx);
        return;
    }
    const char *ct = soup_message_headers_get_one(
        soup_message_get_response_headers(ctx->msg), "Content-Type");
    if (!(ct && g_str_has_prefix(ct, "image/"))) {
        g_debug("HTTP content-type not image for %s: %s", ctx->url, ct ? ct : "(null)");
        g_bytes_unref(bytes);
        http_images_remote_image_ctx_free(ctx);
        return;
    }
    gsize sz = 0;
    const guint8 *data = g_bytes_get_data(bytes, &sz);
    GInputStream *stream = g_memory_input_stream_new_from_data(data, sz, NULL);
    GdkPixbuf *pb = gdk_pixbuf_new_from_stream(stream, NULL, &err);
    g_object_unref(stream);
    if (!pb) {
        g_debug("Pixbuf decode failed for %s: %s", ctx->url,
                err ? err->message : "unknown");
        g_clear_error(&err);
        g_bytes_unref(bytes);
        http_images_remote_image_ctx_free(ctx);
        return;
    }

    /* Create anchor and attach a GtkPicture now */
    GtkTextIter pos;
    gtk_text_buffer_get_iter_at_mark(ctx->buffer, &pos, ctx->mark);
    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(ctx->buffer, &pos);
    GdkTexture *texture = gdk_texture_new_for_pixbuf(pb);
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    g_object_unref(texture);
    /* For consistency with local path, also log when paintable gets set */
    g_signal_connect_data(pic, "notify::paintable",
                         G_CALLBACK(http_images_on_picture_paintable_notify),
                         g_strdup(ctx->url), http_images_free_user_data_notify, 0);
    g_object_unref(pb);
    gtk_text_view_add_child_at_anchor(ctx->view, pic, anchor);
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(pic),
        GTK_ACCESSIBLE_PROPERTY_LABEL,
        (ctx->alt && *ctx->alt) ? ctx->alt :
            ((ctx->title && *ctx->title) ? ctx->title : ctx->url),
        -1);
    g_bytes_unref(bytes);
    /* Tooltip and click navigation */
    if (ctx->title && *ctx->title) gtk_widget_set_tooltip_text(pic, ctx->title);
    else gtk_widget_set_tooltip_text(pic, ctx->open_url ? ctx->open_url : ctx->url);
    const char *nav = ctx->open_url ? ctx->open_url : ctx->url;
    if (nav && *nav) {
        GtkGesture *click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
        /* TODO: Move this to image_embedder or create a callback interface */
        gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));
        g_object_set_data_full(G_OBJECT(pic), "open-url", g_strdup(nav), g_free);
        g_object_set_data_full(G_OBJECT(pic), "image-url", g_strdup(ctx->url), g_free);
    }

    /* Hide the alt text after the image is placed */
    if (ctx->hidden) {
        GtkTextIter iter;
        gtk_text_buffer_get_start_iter(ctx->buffer, &iter);
        gboolean inside = gtk_text_iter_has_tag(&iter, ctx->tag);
        GtkTextIter s;
        while (gtk_text_iter_forward_to_tag_toggle(&iter, ctx->tag)) {
            if (!inside) { s = iter; inside = TRUE; }
            else {
                GtkTextIter e = iter;
                gtk_text_buffer_apply_tag(ctx->buffer, ctx->hidden, &s, &e);
                inside = FALSE;
            }
        }
    }

    g_debug("[embed] Image fetched successfully for %s, rescanning buffer", ctx->url);

    /* Rescan the buffer for any remaining images */
    image_embedder_embed_images_in_text_view(ctx->view);

    http_images_remote_image_ctx_free(ctx);
}

#endif /* HAVE_LIBSOUP */