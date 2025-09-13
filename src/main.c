#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif
#include <glib/gi18n.h> // For _() macro
#include <adwaita.h>    // For AdwApplication, AdwHeaderBar etc.
#include <cmark.h>
#include <locale.h>

#include "toolbar.h"
// #include "gtktext_cmark.h" // Switched to gtktext_cmark
#include "cmrender.h"      // Use the new renderer
#include "settings.h"

static guint buffer_changed_signal_id = 0; // Store the signal handler ID
static guint save_timeout_id = 0;         // Debounced autosave timeout ID
static guint autosave_delay_ms = 500;     // Debounce delay (from GSettings if available)
static GSettings *app_settings = NULL;    // org.gtk.gtktext settings

// Keys for buffer data to coordinate paste→markdown conversion
static const char *DATA_SUPPRESS_PARSE = "gtktext-suppress-reparse";
static const char *DATA_REPARSE_SOURCE_ID = "gtktext-reparse-source-id";
static const char *DATA_REPARSE_TARGET_OFFSET = "gtktext-reparse-target-offset";

// Forward decls
static void schedule_reparse_markdown(GtkTextBuffer *buffer, gint inserted_len, const GtkTextIter *at_iter);
static gboolean reparse_markdown_cb(gpointer user_data);
static void on_buffer_insert_text(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint len, gpointer user_data);

// Funktionsdeklarationer
// static void setup_markdown_tags(GtkTextBuffer *buffer); // Removed duplicate
static void copy_selected_text_as_markdown(GtkTextView *text_view); // Removed __attribute__((unused))
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);
static void app_activate(GApplication *application); // Changed G_APPLICATION to GApplication

// For link handling
static gboolean on_text_view_query_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer user_data);
static void on_text_view_link_clicked(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data);

// Forward decls for static funcs defined later
static void save_buffer_as_markdown(GtkTextBuffer *buffer);

// Image embedding helpers
static void embed_images_in_text_view(GtkTextView *text_view);
static void on_embedded_image_pressed(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data);
static void on_picture_paintable_notify(GObject *object, GParamSpec *pspec, gpointer user_data);
static void free_user_data_notify(gpointer data, GClosure *closure);

// Click handler for embedded images: opens the image URL via GtkUriLauncher
static void on_embedded_image_pressed(G_GNUC_UNUSED GtkGestureClick *gesture, G_GNUC_UNUSED gint n_press, G_GNUC_UNUSED gdouble x, G_GNUC_UNUSED gdouble y, gpointer user_data) {
    GtkWidget *widget = GTK_WIDGET(user_data);
    // Prefer an outer link target if provided; fall back to the image URL
    const char *url = (const char*) g_object_get_data(G_OBJECT(widget), "open-url");
    if (!url) url = (const char*) g_object_get_data(G_OBJECT(widget), "image-url");
    if (!url || !*url) return;
    GtkWidget *view = gtk_widget_get_ancestor(widget, GTK_TYPE_TEXT_VIEW);
    GtkWindow *window = view ? GTK_WINDOW(gtk_widget_get_ancestor(view, GTK_TYPE_WINDOW)) : NULL;
    GtkUriLauncher *launcher = gtk_uri_launcher_new(url);
    gtk_uri_launcher_launch(launcher, window, NULL, NULL, NULL);
    g_object_unref(launcher);
}

// Logs when a GtkPicture finishes loading its paintable
static void on_picture_paintable_notify(GObject *object, GParamSpec *pspec, gpointer user_data) {
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

static void free_user_data_notify(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}

// Embed GtkPicture widgets at positions of image tags and hide the alt text visually
typedef struct {
    GtkTextBuffer *buffer;
    GtkTextView *view;
    GtkTextTag *hidden;
} EmbedCtx;

#ifdef HAVE_LIBSOUP
typedef struct {
    GtkTextBuffer *buffer;  // ref
    GtkTextView *view;      // ref
    GtkTextTag *tag;        // ref
    GtkTextTag *hidden;     // weak (owned by buffer)
    SoupSession *session;   // ref
    SoupMessage *msg;       // ref
    GtkTextMark *mark;      // ref (anchor position)
    char *url;              // owned (image source)
    char *open_url;         // owned (prefer outer link URL on click)
    char *title;            // owned
    char *alt;              // owned
} RemoteImageCtx;

static void remote_image_ctx_free(RemoteImageCtx *c) {
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

static void on_http_image_fetched(SoupSession *session, GAsyncResult *res, gpointer user_data) {
    RemoteImageCtx *ctx = (RemoteImageCtx*)user_data;
    GError *err = NULL;
    GBytes *bytes = soup_session_send_and_read_finish(session, res, &err);
    if (!bytes) {
        g_debug("Image fetch failed for %s: %s", ctx->url, err ? err->message : "unknown");
        g_clear_error(&err);
        remote_image_ctx_free(ctx);
        return;
    }
    // Check HTTP status and content type
    guint status = soup_message_get_status(ctx->msg);
    if (status < 200 || status >= 300) {
        g_debug("HTTP fetch status %u for %s; skipping image", status, ctx->url);
        g_bytes_unref(bytes);
        remote_image_ctx_free(ctx);
        return;
    }
    const char *ct = soup_message_headers_get_one(soup_message_get_response_headers(ctx->msg), "Content-Type");
    if (!(ct && g_str_has_prefix(ct, "image/"))) {
        g_debug("HTTP content-type not image for %s: %s", ctx->url, ct ? ct : "(null)");
        g_bytes_unref(bytes);
        remote_image_ctx_free(ctx);
        return;
    }
    gsize sz = 0;
    const guint8 *data = g_bytes_get_data(bytes, &sz);
    GInputStream *stream = g_memory_input_stream_new_from_data(data, sz, NULL);
    GdkPixbuf *pb = gdk_pixbuf_new_from_stream(stream, NULL, &err);
    g_object_unref(stream);
    if (!pb) {
        g_debug("Pixbuf decode failed for %s: %s", ctx->url, err ? err->message : "unknown");
        g_clear_error(&err);
        g_bytes_unref(bytes);
        remote_image_ctx_free(ctx);
        return;
    }
    // Create anchor and attach a GtkPicture now
    GtkTextIter pos;
    gtk_text_buffer_get_iter_at_mark(ctx->buffer, &pos, ctx->mark);
    GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(ctx->buffer, &pos);
    GdkTexture *texture = gdk_texture_new_for_pixbuf(pb);
    GtkWidget *pic = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    g_object_unref(texture);
    // For consistency with local path, also log when paintable gets set
    g_signal_connect_data(pic, "notify::paintable", G_CALLBACK(on_picture_paintable_notify), g_strdup(ctx->url), free_user_data_notify, 0);
    g_object_unref(pb);
    gtk_text_view_add_child_at_anchor(ctx->view, pic, anchor);
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(pic),
        GTK_ACCESSIBLE_PROPERTY_LABEL,
        (ctx->alt && *ctx->alt) ? ctx->alt : ((ctx->title && *ctx->title) ? ctx->title : ctx->url),
        -1);
    g_bytes_unref(bytes);
    // Tooltip and click navigation
    if (ctx->title && *ctx->title) gtk_widget_set_tooltip_text(pic, ctx->title);
    else gtk_widget_set_tooltip_text(pic, ctx->open_url ? ctx->open_url : ctx->url);
    const char *nav = ctx->open_url ? ctx->open_url : ctx->url;
    if (nav && *nav) {
        GtkGesture *click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
        g_signal_connect(click, "pressed", G_CALLBACK(on_embedded_image_pressed), pic);
        gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));
        g_object_set_data_full(G_OBJECT(pic), "open-url", g_strdup(nav), g_free);
        g_object_set_data_full(G_OBJECT(pic), "image-url", g_strdup(ctx->url), g_free);
    }

    // Hide the alt text after the image is placed
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
    
    // Rescan the buffer for any remaining images
    embed_images_in_text_view(ctx->view);

    remote_image_ctx_free(ctx);
}
#endif

static void embed_foreach_tag(GtkTextTag *tag, gpointer user_data) {
    EmbedCtx *c = (EmbedCtx*)user_data;
    if (!tag || !c || !c->buffer || !c->view) return;
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(tag), "image-embedded")) == 1) return;

    gchar *name = NULL;
    g_object_get(tag, "name", &name, NULL);
    if (!name) return;
    // Only treat tags as images if they carry actual image metadata.
    gboolean is_image = FALSE;
    if (g_str_has_prefix(name, "image_")) {
        // Skip the alt-hidden helper tag if present from older buffers
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

    // First collect all [start,end) offsets for this tag to avoid iterator invalidation while mutating buffer
    typedef struct { int start; int end; } Range;
    GArray *ranges = g_array_new(FALSE, FALSE, sizeof(Range));

    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(c->buffer, &iter);
    gboolean inside = gtk_text_iter_has_tag(&iter, tag);
    GtkTextIter range_start;
    while (gtk_text_iter_forward_to_tag_toggle(&iter, tag)) {
        if (!inside) {
            range_start = iter;
            inside = TRUE;
        } else {
            GtkTextIter range_end = iter;
            Range r;
            r.start = gtk_text_iter_get_offset(&range_start);
            r.end = gtk_text_iter_get_offset(&range_end);
            g_array_append_val(ranges, r);
            inside = FALSE;
        }
    }

    // Process from end to start to keep offsets valid when inserting anchors
    for (gint i = ranges->len - 1; i >= 0; i--) {
        Range r = g_array_index(ranges, Range, i);
        GtkTextIter s, e;
        gtk_text_buffer_get_iter_at_offset(c->buffer, &s, r.start);
        gtk_text_buffer_get_iter_at_offset(c->buffer, &e, r.end);

        // Preserve start position across mutations
        GtkTextMark *start_mark = gtk_text_buffer_create_mark(c->buffer, NULL, &s, TRUE);

        // Inspect if this span is inside a link to prefer its URL for navigation
        const char *link_url = NULL;
        GSList *tags_here = gtk_text_iter_get_tags(&s);
        for (GSList *tl = tags_here; tl; tl = tl->next) {
            GtkTextTag *t = (GtkTextTag*)tl->data;
            gchar *tname = NULL;
            g_object_get(t, "name", &tname, NULL);
            if (tname && g_str_has_prefix(tname, "link_")) {
                link_url = g_object_get_data(G_OBJECT(t), "link-url");
                g_free(tname);
                break;
            }
            g_free(tname);
        }
        g_slist_free(tags_here);

        // Build child widget
        GtkWidget *child = NULL;
        if (url && *url) {
            const char *scheme = g_uri_parse_scheme(url);
#ifdef HAVE_LIBSOUP
            if (scheme && (g_ascii_strcasecmp(scheme, "http") == 0 || g_ascii_strcasecmp(scheme, "https") == 0)) {
                // Remote: fetch async with libsoup; create anchor/picture after load
                SoupSession *session = soup_session_new();
                SoupMessage *msg = soup_message_new("GET", url);
                // Friendly headers for CDNs that require UA/Accept
                SoupMessageHeaders *hdr = soup_message_get_request_headers(msg);
                soup_message_headers_replace(hdr, "User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36 GTKText/0.1");
                soup_message_headers_replace(hdr, "Accept", "image/*,*/*;q=0.5");
                RemoteImageCtx *rix = g_new0(RemoteImageCtx, 1);
                rix->buffer = g_object_ref(c->buffer);
                rix->view = g_object_ref(c->view);
                rix->tag = g_object_ref(tag);
                rix->session = g_object_ref(session);
                rix->msg = g_object_ref(msg);
                rix->url = g_strdup(url);
                rix->open_url = g_strdup(link_url ? link_url : url);
                rix->title = title ? g_strdup(title) : NULL;
                rix->alt = alt ? g_strdup(alt) : NULL;
                rix->hidden = c->hidden;
                rix->mark = gtk_text_buffer_create_mark(c->buffer, NULL, &s, TRUE);
                soup_session_send_and_read_async(session, msg,
                                                G_PRIORITY_DEFAULT,
                                                NULL,
                                                (GAsyncReadyCallback)on_http_image_fetched,
                                                rix);
                g_object_unref(session);
                // Defer anchor creation; keep alt visible until success
                child = NULL;
                // We will not use this local mark; delete it to avoid leaking
                gtk_text_buffer_delete_mark(c->buffer, start_mark);
            } else
#endif
            {
                // Use GIO to load from URI (works for local files and HTTP(S) if GVfs backends are present)
                GFile *gf = g_file_new_for_uri(url);
                child = gtk_picture_new_for_file(gf);
                g_object_unref(gf);
                // Hide alt BEFORE inserting the anchor
                if (c->hidden) gtk_text_buffer_apply_tag(c->buffer, c->hidden, &s, &e);
                g_debug("[embed] GtkPicture created from GFile: %s (scheme=%s)", url, scheme ? scheme : "(null)");
                g_signal_connect_data(child, "notify::paintable", G_CALLBACK(on_picture_paintable_notify), g_strdup(url), free_user_data_notify, 0);
                // Tooltip and click navigation; prefer outer link when present
                if (title && *title) gtk_widget_set_tooltip_text(child, title);
                else gtk_widget_set_tooltip_text(child, link_url ? link_url : url);
                GtkGesture *click = gtk_gesture_click_new();
                gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
                g_signal_connect(click, "pressed", G_CALLBACK(on_embedded_image_pressed), child);
                gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(click));
                g_object_set_data_full(G_OBJECT(child), "open-url", g_strdup(link_url ? link_url : url), g_free);
                g_object_set_data_full(G_OBJECT(child), "image-url", g_strdup(url), g_free);
            }

            if (child) {
                // Accessibility: set accessible label from alt or title
                if (alt && *alt) {
                    gtk_accessible_update_property(GTK_ACCESSIBLE(child),
                        GTK_ACCESSIBLE_PROPERTY_LABEL, alt,
                        -1);
                } else if (title && *title) {
                    gtk_accessible_update_property(GTK_ACCESSIBLE(child),
                        GTK_ACCESSIBLE_PROPERTY_LABEL, title,
                        -1);
                }

                // Tooltip
                if (title && *title) gtk_widget_set_tooltip_text(child, title);
                else gtk_widget_set_tooltip_text(child, url);

                // Clickable
                GtkGesture *click = gtk_gesture_click_new();
                gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
                g_signal_connect(click, "pressed", G_CALLBACK(on_embedded_image_pressed), child);
                gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(click));
                g_object_set_data_full(G_OBJECT(child), "image-url", g_strdup(url), g_free);
            }
        } else {
            const char *txt = (alt && *alt) ? alt : _("Image");
            child = gtk_label_new(txt);
            gtk_widget_add_css_class(child, "dim-label");
            // No actual image; don't hide alt text
        }

        // Insert child at preserved position only if we have a widget now
        if (child) {
            GtkTextIter anchor_pos;
            gtk_text_buffer_get_iter_at_mark(c->buffer, &anchor_pos, start_mark);
            GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(c->buffer, &anchor_pos);
            gtk_text_view_add_child_at_anchor(c->view, child, anchor);
            gtk_text_buffer_delete_mark(c->buffer, start_mark);
        }
    }

    g_array_free(ranges, TRUE);
    g_object_set_data(G_OBJECT(tag), "image-embedded", GINT_TO_POINTER(1));
}

static void embed_images_in_text_view(GtkTextView *text_view) {
    if (!text_view || !GTK_IS_TEXT_VIEW(text_view)) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    if (!buffer) return;
    g_message("[embed] *** SCANNING BUFFER FOR IMAGES ***");

    // Create or lookup a tag used to hide alt text (keeps alt content in buffer for export/i18n)
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    // Use a tag name that does NOT collide with real image_* tags
    GtkTextTag *hidden = gtk_text_tag_table_lookup(table, "img_alt_hidden");
    if (!hidden) {
        // Backwards-compatibility: if previous name exists, reuse it
        hidden = gtk_text_tag_table_lookup(table, "image_alt_hidden");
    }
    if (!hidden) {
        // Avoid object replacement glyphs: make text transparent instead of invisible
        GdkRGBA transparent = {0, 0, 0, 0};
        hidden = gtk_text_buffer_create_tag(buffer, "img_alt_hidden",
                                            "foreground-rgba", &transparent,
                                            NULL);
    }

    EmbedCtx ctx = { buffer, text_view, hidden };
    gtk_text_tag_table_foreach(table, embed_foreach_tag, &ctx);
}

static void on_text_view_link_clicked(G_GNUC_UNUSED GtkGestureClick *gesture, G_GNUC_UNUSED gint n_press, gdouble x, gdouble y, gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextIter iter;

    // Convert click coordinates to buffer iterator
    gtk_text_view_get_iter_at_location(text_view, &iter, x, y);

    // Check if the iter has the "link" tag
    GSList *tags = gtk_text_iter_get_tags(&iter);
    gboolean link_found = FALSE;
    const gchar *url = NULL;

    for (GSList *l = tags; l != NULL; l = l->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(l->data);
        gchar *tag_name = NULL;
        g_object_get(tag, "name", &tag_name, NULL);
        if (tag_name && g_str_has_prefix(tag_name, "link_")) { // Handle unique link tags
            link_found = TRUE;
            url = g_object_get_data(G_OBJECT(tag), "link-url");
            g_free(tag_name);
            break; 
        }
        g_free(tag_name);
    }
    g_slist_free(tags);

    if (link_found && url) {
        g_debug("Link clicked: %s", url);
        GtkWindow *window = GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(text_view), GTK_TYPE_WINDOW));
        
        // Use the modern GtkUriLauncher API (GTK4) instead of the deprecated gtk_show_uri
        GtkUriLauncher *uri_launcher = gtk_uri_launcher_new(url);
        gtk_uri_launcher_launch(uri_launcher, window, NULL, NULL, NULL);
        g_object_unref(uri_launcher);
    }
}

static gboolean save_timeout_cb(gpointer user_data) {
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(user_data);
    save_timeout_id = 0; // reset first to avoid races
    save_buffer_as_markdown(buffer);
    return G_SOURCE_REMOVE;
}

static void on_setting_changed(GSettings *settings, gchar *key, G_GNUC_UNUSED gpointer user_data) {
    if (g_strcmp0(key, "autosave-delay-ms") == 0) {
        guint val = g_settings_get_uint(settings, "autosave-delay-ms");
        autosave_delay_ms = val;
        g_message("Updated autosave delay to %u ms", autosave_delay_ms);
    }
}

// Schedules a short idle/timeout to re-parse the entire buffer as CommonMark
static void schedule_reparse_markdown(GtkTextBuffer *buffer, gint inserted_len, const GtkTextIter *at_iter) {
    if (!buffer) return;
    // Avoid scheduling if a reparse is already queued
    guint existing = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID));
    if (existing != 0) {
        g_source_remove(existing);
    }

    // Record a target offset near the end of the inserted text to restore cursor
    if (at_iter) {
        gint base = gtk_text_iter_get_offset((GtkTextIter*)at_iter);
        gint target = base + (inserted_len > 0 ? inserted_len : 0);
        g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_TARGET_OFFSET, GINT_TO_POINTER(target));
    }

    guint id = g_timeout_add_full(G_PRIORITY_LOW, 30, reparse_markdown_cb, g_object_ref(buffer), g_object_unref);
    g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID, GUINT_TO_POINTER(id));
}

// Re-parse the whole buffer using cm_render_markdown_to_buffer. Restores cursor near previous insert.
static gboolean reparse_markdown_cb(gpointer user_data) {
    GtkTextBuffer *buffer = GTK_TEXT_BUFFER(user_data);
    // Clear the marker that scheduled us
    g_object_set_data(G_OBJECT(buffer), DATA_REPARSE_SOURCE_ID, GUINT_TO_POINTER(0));

    // Suppress recursive scheduling while we modify the buffer
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(1));

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    g_autofree char *plain = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (plain && *plain) {
        // Preserve a plausible cursor position
        gint target = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), DATA_REPARSE_TARGET_OFFSET));

        // Get text view and soup session for the new rendering function
        GtkTextView *tv = GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(buffer), "gtktext-view"));
#ifdef HAVE_LIBSOUP
        SoupSession *soup_session = g_object_get_data(G_OBJECT(buffer), "soup-session");
        if (!cm_render_markdown_to_buffer(buffer, plain, tv, soup_session)) {
#else
        if (!cm_render_markdown_to_buffer(buffer, plain, tv, NULL)) {
#endif
            g_warning("Realtime Markdown import failed");
        } else {
            // Restore cursor position
            GtkTextIter it;
            gint char_count = gtk_text_buffer_get_char_count(buffer);
            if (target < 0) target = 0;
            if (target > char_count) target = char_count;
            gtk_text_buffer_get_iter_at_offset(buffer, &it, target);
            gtk_text_buffer_place_cursor(buffer, &it);
            // Re-embed images after re-render
            if (tv) {
                cm_render_update_theme_dependent_tags(buffer);
                // No longer need embed_images_in_text_view since images are embedded during rendering
                // embed_images_in_text_view(tv);
            }
        }
    }

    // Re-enable scheduling
    g_object_set_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE, GINT_TO_POINTER(0));
    return G_SOURCE_REMOVE;
}

// Detect paste-like insertions and schedule a reparse of the buffer
static void on_buffer_insert_text(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint len, G_GNUC_UNUSED gpointer user_data) {
    if (!buffer || !text || len <= 0) return;
    // Ignore programmatic changes from our own re-rendering
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), DATA_SUPPRESS_PARSE)) != 0) return;

    // Heuristics: if insertion length > 1 or includes a newline or likely Markdown markers, treat as paste
    gboolean looks_like_paste = FALSE;
    if (len > 1) looks_like_paste = TRUE;
    for (int i = 0; !looks_like_paste && i < len; i++) {
        char c = text[i];
        if (c == '\n' || c == '#' || c == '[' || c == '!' || c == '`' || c == '*' || c == '_' || c == '>' || c == '-') {
            looks_like_paste = TRUE;
        }
    }
    if (looks_like_paste) {
        schedule_reparse_markdown(buffer, len, location);
    }
}

// Async open-file completion callback (GAsyncReadyCallback signature)
static void on_open_file_dialog_finish(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GtkFileDialog *d = GTK_FILE_DIALOG(source_object);
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(d, res, NULL);
    if (!file) return;
    g_autofree char *path = g_file_get_path(file);
    g_autofree char *contents = NULL; gsize len = 0; GError *err = NULL;
    if (!g_file_get_contents(path, &contents, &len, &err)) {
        g_warning("Open failed: %s", err->message);
        g_clear_error(&err);
        return;
    }
    GtkApplication *app = GTK_APPLICATION(user_data);
    GtkWidget *text_view = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "text_view"));
    if (!text_view) return;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
#ifdef HAVE_LIBSOUP
    SoupSession *soup_session = g_object_get_data(G_OBJECT(app), "soup_session");
    if (!cm_render_markdown_to_buffer(buffer, contents, GTK_TEXT_VIEW(text_view), soup_session)) {
#else
    if (!cm_render_markdown_to_buffer(buffer, contents, GTK_TEXT_VIEW(text_view), NULL)) {
#endif
        g_warning("Import failed");
    } else {
        cm_render_update_theme_dependent_tags(buffer);
        // No longer need embed_images_in_text_view since images are embedded during rendering
        // embed_images_in_text_view(GTK_TEXT_VIEW(text_view));
    }
}

// App action callbacks
static void action_open_cb (GSimpleAction *a, GVariant *p, gpointer user_data) {
  (void)a; (void)p;
  GtkApplication *app = GTK_APPLICATION(user_data);
  GtkWindow *parent = gtk_application_get_active_window(app);
  if (!parent) return;
  GtkFileDialog *dlg = gtk_file_dialog_new();
  gtk_file_dialog_open(dlg, parent, NULL, on_open_file_dialog_finish, app);
  g_object_unref(dlg);
}

static void action_save_cb (GSimpleAction *a, GVariant *p, gpointer user_data) {
  (void)a; (void)p;
  GtkApplication *app = GTK_APPLICATION(user_data);
  GtkWidget *text_view = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "text_view"));
  if (!text_view) return;
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
  save_buffer_as_markdown(buffer);
}

static void action_preferences_cb (GSimpleAction *a, GVariant *p, gpointer user_data) {
  (void)a; (void)p;
  GtkApplication *app = GTK_APPLICATION(user_data);
  GtkWindow *parent = gtk_application_get_active_window(app);
  if (!parent) return;
  AdwDialog *dlg = create_settings_window(parent);
  (void)dlg;
}

static void action_about_cb (GSimpleAction *a, GVariant *p, gpointer user_data) {
  (void)a; (void)p;
  GtkApplication *app = GTK_APPLICATION(user_data);
  GtkWindow *parent = gtk_application_get_active_window(app);
  if (!parent) return;
  AdwDialog *about = adw_about_dialog_new();
  adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), _("GTKText"));
  adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), "gtktext");
  adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "GTKText Authors");
  adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), "0.1");
  adw_dialog_present(about, GTK_WIDGET(parent));
}

static gboolean on_text_view_query_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, G_GNUC_UNUSED gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(widget);
    GtkTextIter iter;

    // Do not show tooltips if in keyboard navigation mode for links,
    // or if the text view doesn't have focus (e.g. pointer is just passing over)
    if (keyboard_mode || !gtk_widget_has_focus(widget)) {
        return FALSE;
    }

    // Get iterator at mouse position
    gtk_text_view_get_iter_at_location(text_view, &iter, x, y);

    GSList *tags = gtk_text_iter_get_tags(&iter);
    const gchar *url = NULL;
    const gchar *title = NULL;
    gboolean link_found = FALSE;

    for (GSList *l = tags; l != NULL; l = l->next) {
        GtkTextTag *tag = GTK_TEXT_TAG(l->data);
        gchar *tag_name = NULL;
        g_object_get(tag, "name", &tag_name, NULL);
        if (tag_name && g_str_has_prefix(tag_name, "link_")) { // Handle unique link tags
            link_found = TRUE;
            url = g_object_get_data(G_OBJECT(tag), "link-url");
            title = g_object_get_data(G_OBJECT(tag), "link-title");
            g_free(tag_name);
            break;
        }
        g_free(tag_name);
    }
    g_slist_free(tags);

    if (link_found && url) {
        GString *tooltip_text = g_string_new(NULL);
        g_string_append_printf(tooltip_text, "Link: %s", url);
        if (title && *title) {
            g_string_append_printf(tooltip_text, "\nTitle: %s", title);
        }
        gtk_tooltip_set_text(tooltip, tooltip_text->str);
        g_string_free(tooltip_text, TRUE);
        return TRUE; // Tooltip was set
    }

    return FALSE; // No tooltip for this location
}

// Determines the full path for the save file.
static gchar* get_save_file_path(void) {
    const gchar *doc_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
    if (!doc_dir) {
        doc_dir = g_get_home_dir(); // Fallback to home directory if Documents isn't found
    }
    return g_build_filename(doc_dir, "mini_text_editor.md", NULL);
}

// Loads text content from the predefined save file.
static void load_markdown_to_buffer(GtkTextView *text_view) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    g_autofree gchar *filename = get_save_file_path();
    gchar *content = NULL;
    GError *error = NULL;
    g_message("Loading markdown from file: %s", filename);
    if (!g_file_get_contents(filename, &content, NULL, &error)) {
        g_warning("Error loading file: %s", error->message);
        g_clear_error(&error);
        // Optional: Insert some default content or leave buffer empty
        // gtk_text_buffer_set_text(buffer, "---"Velkommen til Markvoean!\n---"Start med at skrive din Markdown her.", -1);
        return;
    }
    g_debug("Loaded file contents: %s", content);
    
    // Get the application and soup session for image fetching
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(text_view));
    GtkApplication *app = NULL;
    if (GTK_IS_WINDOW(root)) {
        app = gtk_window_get_application(GTK_WINDOW(root));
    }
    
#ifdef HAVE_LIBSOUP
    SoupSession *soup_session = app ? g_object_get_data(G_OBJECT(app), "soup_session") : NULL;
    if (!cm_render_markdown_to_buffer(buffer, content, text_view, soup_session)) {
#else
    if (!cm_render_markdown_to_buffer(buffer, content, text_view, NULL)) {
#endif
        g_warning("Failed to import markdown to buffer");
    } else {
        g_message("Markdown imported successfully.");
        // After loading, ensure theme-dependent tags are updated
        cm_render_update_theme_dependent_tags(buffer);
        // No longer need embed_images_in_text_view since images are embedded during rendering
        // embed_images_in_text_view(text_view);
    }
    g_free(content);
}

// Saves the content of the GtkTextBuffer to the predefined save file as markdown.
static void save_buffer_as_markdown(GtkTextBuffer *buffer) {
    // Ensure buffer is valid before proceeding
    if (!buffer || !GTK_IS_TEXT_BUFFER(buffer)) { // Corrected G_IS_TEXT_BUFFER
        g_warning("save_buffer_as_markdown: Invalid text buffer provided.");
        return;
    }
    g_autofree gchar *filename = get_save_file_path();
    if (!filename) {
        g_warning("Cannot save: Failed to determine save file path");
        return;
    }
    
    // Get markdown content safely
    char *md = cm_render_buffer_to_markdown(buffer);
    if (!md) {
        g_warning("Cannot save: Failed to convert buffer to markdown");
        return;
    }
    
    // Save to file
    GError *error = NULL;
    if (!g_file_set_contents(filename, md, -1, &error)) {
        g_warning("Error saving file: %s", error->message);
        g_clear_error(&error);
    } else {
        g_print("Buffer saved as markdown to: %s\n", filename);
    }
    
    g_free(md);
}

// Callback triggered when the text in the GtkTextBuffer changes.
static void on_text_changed(GtkTextBuffer *buffer, G_GNUC_UNUSED gpointer user_data) {
    // Debounced autosave: reset pending timer and schedule a save
    if (save_timeout_id != 0) {
        g_source_remove(save_timeout_id);
        save_timeout_id = 0;
    }
    guint delay = autosave_delay_ms;
    if (delay == 0) {
        // Immediate save when delay is disabled
        save_buffer_as_markdown(buffer);
        return;
    }
    // Take a ref to buffer for the timeout and unref when done
    save_timeout_id = g_timeout_add_full(G_PRIORITY_DEFAULT, delay, save_timeout_cb,
                                         g_object_ref(buffer), g_object_unref);
}

// Callback triggered when the main window requests to be closed.
// Change the function signature to match GTK4's close-request signal
static gboolean on_window_close_request(G_GNUC_UNUSED GtkWindow *window,
                                        gpointer user_data) {
    // Ensure text_view is valid before using it
    if (!user_data || !GTK_IS_TEXT_VIEW(user_data)) {
        g_warning("on_window_close_request: Invalid text_view (user_data).");
        return FALSE; // Or handle error appropriately
    }
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view); // GTK4: Does not return a new ref

    // Ensure buffer is valid after getting it from text_view
    if (!buffer || !GTK_IS_TEXT_BUFFER(buffer)) { 
        g_warning("on_window_close_request: Failed to get valid buffer from text_view.");
        return FALSE; // Or handle error appropriately
    }

    g_debug("on_window_close_request: preparing to close");
    
    // Attempt to disconnect the signal handler if it was connected
    if (buffer_changed_signal_id > 0) {
        // G_IS_OBJECT(buffer) is implicitly true if GTK_IS_TEXT_BUFFER(buffer) was true
        if (g_signal_handler_is_connected(buffer, buffer_changed_signal_id)) {
            g_debug("on_window_close_request: disconnecting 'changed' signal handler (ID: %u)", 
                    buffer_changed_signal_id);
            g_signal_handler_disconnect(buffer, buffer_changed_signal_id);
        }
    }
    buffer_changed_signal_id = 0; // Always reset the ID, marking it as handled/disconnected

    // Cancel pending autosave and save immediately
    if (save_timeout_id != 0) {
        g_source_remove(save_timeout_id);
        save_timeout_id = 0;
    }
    // Save the buffer contents (buffer is guaranteed to be valid if we reached this point)
    g_message("Saving buffer before closing...");
    save_buffer_as_markdown(buffer);
    g_message("Buffer saved. Allowing default close handling.");
    
    // Return FALSE to let the default handler proceed with window destruction
    return FALSE;
}

// Konverterer valgt tekst til markdown og kopierer til udklipsholderen
static void copy_selected_text_as_markdown(GtkTextView *text_view) {
    g_debug("copy_selected_text_as_markdown: start");

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter start_sel, end_sel;

    if (!gtk_text_buffer_get_selection_bounds(buffer, &start_sel, &end_sel)) {
        g_debug("copy_selected_text_as_markdown: no selection");
        return;
    }
    g_debug("copy_selected_text_as_markdown: selection found");

    GString *md = g_string_new("");
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(buffer, &iter, gtk_text_iter_get_offset(&start_sel));

    gboolean currently_in_bold = FALSE;
    gboolean currently_in_italic = FALSE;
    gboolean currently_in_code = FALSE;
    gboolean currently_in_codeblock = FALSE;
    
    GtkTextIter temp_line_start_iter = iter;
    gtk_text_iter_set_line_offset(&temp_line_start_iter, 0);
    gboolean at_line_start = gtk_text_iter_equal(&iter, &temp_line_start_iter);

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

    while(gtk_text_iter_compare(&iter, &end_sel) < 0) {
        gunichar current_char = gtk_text_iter_get_char(&iter);

        gboolean iter_is_bold = FALSE;
        gboolean iter_is_italic = FALSE;
        gboolean iter_is_code = FALSE;
        gboolean iter_is_codeblock_char = FALSE;
        gboolean iter_is_h1 = FALSE, iter_is_h2 = FALSE, iter_is_h3 = FALSE, iter_is_h4 = FALSE, iter_is_h5 = FALSE, iter_is_h6 = FALSE;

        GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
        for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(l->data);
            gchar* tag_name = NULL;
            g_object_get(tag, "name", &tag_name, NULL);
            if (tag_name) {
                if (g_strcmp0(tag_name, "bold") == 0) iter_is_bold = TRUE;
                else if (g_strcmp0(tag_name, "italic") == 0) iter_is_italic = TRUE;
                else if (g_strcmp0(tag_name, "code") == 0) iter_is_code = TRUE;
                else if (g_strcmp0(tag_name, "codeblock") == 0) iter_is_codeblock_char = TRUE;
                else if (g_strcmp0(tag_name, "h1") == 0) iter_is_h1 = TRUE;
                else if (g_strcmp0(tag_name, "h2") == 0) iter_is_h2 = TRUE;
                else if (g_strcmp0(tag_name, "h3") == 0) iter_is_h3 = TRUE;
                else if (g_strcmp0(tag_name, "h4") == 0) iter_is_h4 = TRUE;
                else if (g_strcmp0(tag_name, "h5") == 0) iter_is_h5 = TRUE;
                else if (g_strcmp0(tag_name, "h6") == 0) iter_is_h6 = TRUE;
                g_free(tag_name);
            }
        }
        g_slist_free(tags_at_iter);

        if (at_line_start) {
            GtkTextTag *hr_tag = gtk_text_tag_table_lookup(tag_table, "hr");
            if (hr_tag && gtk_text_iter_has_tag(&iter, hr_tag) && !currently_in_codeblock) {
                g_string_append(md, "---\\n");
                
                GtkTextIter line_end_iter = iter;
                gtk_text_iter_forward_to_line_end(&line_end_iter);
                iter = line_end_iter; 

                if (gtk_text_iter_compare(&iter, &end_sel) < 0) {
                     gtk_text_iter_forward_char(&iter); 
                     if (gtk_text_iter_compare(&iter, &end_sel) < 0 && gtk_text_iter_get_char(&iter) == '\n') { // Corrected from '\\n'
                         gtk_text_iter_forward_char(&iter); 
                     }
                }
                at_line_start = TRUE; 
                if (gtk_text_iter_compare(&iter, &end_sel) >= 0) break;
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
                        g_string_append(md, "```\n"); // Corrected from "```\\n"
                        currently_in_codeblock = TRUE;
                    }
                }
            }
        }

        if (currently_in_codeblock && !iter_is_codeblock_char && current_char != '\n') { // Corrected from '\\n'
            if (md->str[md->len -1] != '\n') {
                g_string_append_c(md, '\n');
            }
            g_string_append(md, "```\n"); // Corrected from "```\\n"
            currently_in_codeblock = FALSE;
        }
        
        if (currently_in_codeblock) {
            g_string_append_unichar(md, current_char);
        } else {
            // Inline formatting
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
            
            g_string_append_unichar(md, current_char); // Append the character itself
            
            // Old inline code closing logic removed, it's handled by the checks above
            // or by the final check after the loop.
        }

        if (current_char == '\n') {
            at_line_start = TRUE;
            if (currently_in_codeblock) {
                // Check if the *next* char (if any) still has codeblock tag.
                // If not, this newline might be the end of the codeblock content.
                GtkTextIter next_char_iter = iter;
                gtk_text_iter_forward_char(&next_char_iter); // Look ahead
                if (gtk_text_iter_compare(&next_char_iter, &end_sel) >= 0 || // End of selection
                    !gtk_text_iter_has_tag(&next_char_iter, gtk_text_tag_table_lookup(tag_table, "codeblock"))) {
                    // This newline is the last line of the code block content, or selection ends.
                    // The fence will be added at loop end or when tag disappears.
                    // If the newline itself means end of block (e.g. selection ends here, or next line no tag)
                    // then we need to close the block.
                    if (md->str[md->len -1] != '\n') { // md already has the current_char = \n
                        // This condition is tricky. The current_char is \n.
                        // We need to check if the char *before* it was \n.
                        // Let's assume the newline was appended.
                    }
                    // If the selection ends right after this newline, the after-loop logic handles it.
                    // If next line has no codeblock tag, the logic at start of loop for non-codeblock char handles it.
                }
            }
        } else {
            at_line_start = FALSE;
        }
        
        gtk_text_iter_forward_char(&iter);
    }

    if (currently_in_code) {
        g_string_append_c(md, '`');
    }
    if (currently_in_codeblock) {
        if (md->len > 0 && md->str[md->len -1] != '\n') {
            g_string_append_c(md, '\n');
        }
        g_string_append(md, "```" "\n");
    }
    // Ensure bold/italic are closed if selection ends mid-format
    if (currently_in_bold && currently_in_italic) g_string_append(md, "***");
    else if (currently_in_bold) g_string_append(md, "**");
    else if (currently_in_italic) g_string_append(md, "*");


    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(text_view));
    gdk_clipboard_set_text(clipboard, md->str);
    g_message("Copied selection to clipboard as markdown.");
    g_string_free(md, TRUE);
}

// Callback for tastaturgenvej (Ctrl+C)
static gboolean on_key_pressed(G_GNUC_UNUSED GtkEventControllerKey *controller,
                               guint keyval,
                               G_GNUC_UNUSED guint keycode,
                               GdkModifierType state,
                               gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);

    // Detect Ctrl+C
    if (keyval == GDK_KEY_c && (state & GDK_CONTROL_MASK)) {
        g_debug("Ctrl+C detected");
        copy_selected_text_as_markdown(text_view);
        return TRUE; // Event handled
    }

    return FALSE; // Pass event to other handlers
}


static void on_map(G_GNUC_UNUSED GtkWidget *widget, gpointer user_data) {
    g_message("Main window mapped, image widgets already embedded during rendering.");
    // No longer need embed_images_in_text_view since images are embedded during rendering
    // embed_images_in_text_view(GTK_TEXT_VIEW(user_data));
}

static void app_activate(GApplication *application) {
    GtkApplication *app = GTK_APPLICATION(application);
    GtkBuilder *builder = gtk_builder_new();
    // Set translation domain for .ui strings
    gtk_builder_set_translation_domain(builder, GETTEXT_PACKAGE);

    // Try installed path first, then fall back to local paths for dev
    const char *ui_candidates[] = {
        PKGDATADIR "/ui/main_window.ui",
        "./ui/main_window.ui",
        "../ui/main_window.ui",
        NULL
    };
    gboolean ui_loaded = FALSE;
    for (int i = 0; ui_candidates[i] != NULL; i++) {
        if (g_file_test(ui_candidates[i], G_FILE_TEST_EXISTS)) {
            GError *err = NULL;
            ui_loaded = gtk_builder_add_from_file(builder, ui_candidates[i], &err);
            if (!ui_loaded) {
                g_warning("Failed to load UI from %s: %s", ui_candidates[i], err ? err->message : "unknown error");
                g_clear_error(&err);
            }
            break;
        }
    }
    if (!ui_loaded) {
        g_critical("Failed to load any UI file for main_window");
        g_object_unref(builder);
        return;
    }
    if (!builder) {
        g_critical("Failed to load UI file main_window.ui");
        return;
    }

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!window) {
        g_critical("Failed to get main_window from UI");
        g_object_unref(builder);
        return;
    }
    gtk_window_set_application(GTK_WINDOW(window), GTK_APPLICATION(app));
    // Ensure window action context has the application action group under the "app" prefix
    gtk_widget_insert_action_group(window, "app", G_ACTION_GROUP(app));

    GtkWidget *text_view = GTK_WIDGET(gtk_builder_get_object(builder, "text_view"));
    if (!text_view) {
        g_critical("Failed to get text_view from UI");
        g_object_unref(builder);
        return;
    }
    
    // Expose text_view to application scope for actions to use
    g_object_set_data(G_OBJECT(app), "text_view", text_view);

    // Create a global soup session for image fetching
#ifdef HAVE_LIBSOUP
    SoupSession *global_soup_session = soup_session_new();
    g_object_set_data_full(G_OBJECT(app), "soup_session", global_soup_session, g_object_unref);
#endif

    // Opret toolbar og tilføj til container
    GtkWidget *toolbar_container = GTK_WIDGET(gtk_builder_get_object(builder, "toolbar_container"));
    if (!toolbar_container) {
        g_critical("Failed to get toolbar_container from UI");
        g_object_unref(builder);
        return;
    }
    
    // Tilføj lidt CSS styling til toolbaren
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, 
        ".toolbar { background-color: @theme_bg_color; border-bottom: 1px solid @borders; padding: 8px; margin: 4px; }"
        ".toolbar button { padding: 4px 8px; min-height: 24px; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    
    // Sørg for at builder associeres med window, så vi kan få det fra ethvert widget
    // der er forbundet med vinduet - Genetablerer den nødvendige reference
    g_object_set_data_full(G_OBJECT(window), "builder", g_object_ref(builder), g_object_unref);
    
    // Opret toolbar og tilføj til UI
    GtkWidget *toolbar = create_toolbar(text_view);
    
    // Sørg for at toolbar er synlig og korrekt tilføjet
    if (toolbar && toolbar_container) {
        gtk_widget_set_visible(toolbar_container, TRUE);
        g_message("Toolbar is visible and active");
    }

    // Opsæt key controller til at detektere Ctrl+C (legacy path, actions handle most shortcuts)
    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), text_view);
    gtk_widget_add_controller(text_view, key_controller);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    // Initialize GSettings and load autosave delay
    app_settings = g_settings_new("org.gtk.gtktext");
    if (app_settings) {
        // Try to get the value using a simple approach that handles missing keys gracefully
        autosave_delay_ms = g_settings_get_uint(app_settings, "autosave-delay-ms");
        g_signal_connect(app_settings, "changed::autosave-delay-ms", G_CALLBACK(on_setting_changed), NULL);
    } else {
        g_warning("GSettings schema org.gtk.gtktext not found; using default autosave delay %u ms", autosave_delay_ms);
    }
    // Keep a back-pointer from buffer to the view for reparse callbacks
    g_object_set_data(G_OBJECT(buffer), "gtktext-view", text_view);
    
    // Store soup session on buffer for reparse callbacks
#ifdef HAVE_LIBSOUP
    SoupSession *soup_session = g_object_get_data(G_OBJECT(app), "soup_session");
    if (soup_session) {
        g_object_set_data(G_OBJECT(buffer), "soup-session", soup_session);
    }
#endif
    load_markdown_to_buffer(GTK_TEXT_VIEW(text_view));
    // Store the handler ID so we can disconnect it later if needed
    buffer_changed_signal_id = g_signal_connect(buffer, "changed", G_CALLBACK(on_text_changed), NULL);
    // Realtime paste→markdown conversion: listen to inserted text
    g_signal_connect(buffer, "insert-text", G_CALLBACK(on_buffer_insert_text), NULL);

    // Enable tooltips and connect the query-tooltip signal
    gtk_widget_set_has_tooltip(GTK_WIDGET(text_view), TRUE);
    g_signal_connect(text_view, "query-tooltip", G_CALLBACK(on_text_view_query_tooltip), NULL);

    // Create a GtkGestureClick controller for link clicking
    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_text_view_link_clicked), text_view);
    gtk_widget_add_controller(GTK_WIDGET(text_view), GTK_EVENT_CONTROLLER(click_gesture));

    // Tilføj signal for window close
    g_signal_connect(window, "close-request", G_CALLBACK(on_window_close_request), text_view);

    // Når vinduet bliver ødelagt, så frigiv referencen til builder
    // REMOVED: g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_object_unref), builder);

    // Wire headerbar buttons to actions
    GtkWidget *open_button = GTK_WIDGET(gtk_builder_get_object(builder, "open_button"));
    GtkWidget *save_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_button"));
    if (open_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(open_button), "app.open");
    if (save_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(save_button), "app.save");

    // Accessibility: Provide accessible names for icon-only buttons
    if (open_button) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(open_button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Open"),
            -1);
    }
    if (save_button) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(save_button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Save"),
            -1);
    }

    g_object_set(text_view, "editable", TRUE, "cursor-visible", TRUE, NULL);
    g_signal_connect(text_view, "map", G_CALLBACK(on_map), text_view); // Re-scan after widget is mapped

    gtk_window_present(GTK_WINDOW(window));
    // Vi frigiver ikke builder her, da vi gemmer en reference i window-objektet
    // Den frigives, når window ødelægges
}

int main (int argc, char *argv[]) {
  // Optional flag: --debug enables verbose logging
  for (int i = 1; i < argc; i++) {
    if (g_strcmp0(argv[i], "--debug") == 0) {
      g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
      break;
    }
  }
  // Initialize i18n
  setlocale(LC_ALL, "");
  bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
  textdomain(GETTEXT_PACKAGE);

  g_autoptr (AdwApplication) app = NULL;
  int status;

  app = adw_application_new ("com.example.MiniTextEditor", G_APPLICATION_DEFAULT_FLAGS);

  const GActionEntry app_actions[] = {
    { "open", action_open_cb, NULL, NULL, NULL, {0} },
    { "save", action_save_cb, NULL, NULL, NULL, {0} },
    { "preferences", action_preferences_cb, NULL, NULL, NULL, {0} },
    { "about", action_about_cb, NULL, NULL, NULL, {0} },
  };

  g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), app);

  // Debug: To inspect actions at runtime, run with GTK_DEBUG=actions
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.open", (const char*[]){ "<primary>o", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.save", (const char*[]){ "<primary>s", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.preferences", (const char*[]){ "<primary>comma", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.about", (const char*[]){ NULL });

  g_signal_connect (app, "activate", G_CALLBACK (app_activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);

  return status;
}
