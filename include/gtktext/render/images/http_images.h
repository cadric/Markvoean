/* C ULTRA-MIN TEMPLATE
   Purpose: HTTP image processing and remote image fetching for GTK markdown editor
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - render/images/http_images.h
   Changed: Extracted HTTP image processing from main.c for better organization
*/

#ifndef GTKTEXT_RENDER_IMAGES_HTTP_IMAGES_H
#define GTKTEXT_RENDER_IMAGES_HTTP_IMAGES_H

#include <gtk/gtk.h>

#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif

G_BEGIN_DECLS

#ifdef HAVE_LIBSOUP
/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Type definitions for HTTP image processing
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    GtkTextBuffer *buffer;  /* ref */
    GtkTextView *view;      /* ref */
    GtkTextTag *tag;        /* ref */
    GtkTextTag *hidden;     /* weak (owned by buffer) */
    SoupSession *session;   /* ref */
    SoupMessage *msg;       /* ref */
    GtkTextMark *mark;      /* ref (anchor position) */
    char *url;              /* owned (image source) */
    char *open_url;         /* owned (prefer outer link URL on click) */
    char *title;            /* owned */
    char *alt;              /* owned */
} RemoteImageCtx;

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - HTTP image processing functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Handle HTTP image fetch completion
 * @param session Soup session
 * @param res Async result
 * @param user_data RemoteImageCtx pointer
 */
void http_images_on_http_image_fetched(SoupSession *session, GAsyncResult *res,
                                     gpointer user_data);

/**
 * Free remote image context
 * @param c RemoteImageCtx to free
 */
void http_images_remote_image_ctx_free(RemoteImageCtx *c);

/**
 * Handle picture paintable notifications for logging
 * @param object GtkPicture object
 * @param pspec Parameter spec
 * @param user_data Source URL string
 */
void http_images_on_picture_paintable_notify(GObject *object, GParamSpec *pspec,
                                            gpointer user_data);

/**
 * Free user data notification callback
 * @param data Data to free
 * @param closure GClosure (unused)
 */
void http_images_free_user_data_notify(gpointer data, GClosure *closure);

#endif /* HAVE_LIBSOUP */

G_END_DECLS

#endif /* GTKTEXT_RENDER_IMAGES_HTTP_IMAGES_H */