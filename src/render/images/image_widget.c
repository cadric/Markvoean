/* C ULTRA‑MIN TEMPLATE
   Purpose: GObject image widget type for managing image metadata in markdown
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
/* [0.2.0] - 2025-09-15 - src/image-widget.c
 * Added: GObject type system implementation for image widget management.
 */
#include "config.h"
#include <gtktext/render/image_widget.h>
#include <glib-object.h>

/* ========== META ========== */
/* [0.2.0] - 2025-09-15 - src/image-widget.c
 * Added: GObject type system implementation for image widget management.
 */

/* ========== TYPES ========== */
struct _GtktextImageWidget {
    GObject parent_instance;
    
    GtkPicture *picture;
    gchar *url;
    gchar *alt_text;
};

/* ========== STATE ========== */
G_DEFINE_TYPE(GtktextImageWidget, gtktext_image_widget, G_TYPE_OBJECT)

typedef enum {
    PROP_0,
    PROP_PICTURE,
    PROP_URL,
    PROP_ALT_TEXT,
    N_PROPS
} GtktextImageWidgetProperty;

static GParamSpec *properties[N_PROPS];

/* ========== HELPERS ========== */
static void gtktext_image_widget_finalize(GObject *object) {
    GtktextImageWidget *self = GTKTEXT_IMAGE_WIDGET(object);
    
    g_clear_object(&self->picture);
    g_free(self->url);
    g_free(self->alt_text);
    
    G_OBJECT_CLASS(gtktext_image_widget_parent_class)->finalize(object);
}

static void gtktext_image_widget_get_property(GObject *object,
                                               guint prop_id,
                                               GValue *value,
                                               GParamSpec *pspec) {
    GtktextImageWidget *self = GTKTEXT_IMAGE_WIDGET(object);
    
    switch (prop_id) {
    case PROP_PICTURE:
        g_value_set_object(value, self->picture);
        break;
    case PROP_URL:
        g_value_set_string(value, self->url);
        break;
    case PROP_ALT_TEXT:
        g_value_set_string(value, self->alt_text);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gtktext_image_widget_set_property(GObject *object,
                                               guint prop_id,
                                               const GValue *value,
                                               GParamSpec *pspec) {
    GtktextImageWidget *self = GTKTEXT_IMAGE_WIDGET(object);
    
    switch (prop_id) {
    case PROP_PICTURE:
        g_clear_object(&self->picture);
        self->picture = g_value_dup_object(value);
        break;
    case PROP_URL:
        g_free(self->url);
        self->url = g_value_dup_string(value);
        break;
    case PROP_ALT_TEXT:
        gtktext_image_widget_set_alt_text(self, g_value_get_string(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* ========== HANDLERS ========== */
static void gtktext_image_widget_class_init(GtktextImageWidgetClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    
    object_class->finalize = gtktext_image_widget_finalize;
    object_class->get_property = gtktext_image_widget_get_property;
    object_class->set_property = gtktext_image_widget_set_property;
    
    /* Properties */
    properties[PROP_PICTURE] = g_param_spec_object("picture",
                                                   "Picture",
                                                   "The GtkPicture widget",
                                                   GTK_TYPE_PICTURE,
                                                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    
    properties[PROP_URL] = g_param_spec_string("url",
                                               "URL",
                                               "The image URL",
                                               "",
                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    
    properties[PROP_ALT_TEXT] = g_param_spec_string("alt-text",
                                                    "Alt Text",
                                                    "The alt text for the image",
                                                    NULL,
                                                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    
    g_object_class_install_properties(object_class, N_PROPS, properties);
}

static void gtktext_image_widget_init(GtktextImageWidget *self) {
    self->picture = NULL;
    self->url = NULL;
    self->alt_text = NULL;
}

/* ========== LIFECYCLE ========== */
GtktextImageWidget *gtktext_image_widget_new(GtkPicture *picture, const gchar *url, const gchar *alt_text) {
    g_return_val_if_fail(GTK_IS_PICTURE(picture), NULL);
    g_return_val_if_fail(url != NULL, NULL);
    
    return g_object_new(GTKTEXT_TYPE_IMAGE_WIDGET,
                        "picture", picture,
                        "url", url,
                        "alt-text", alt_text,
                        NULL);
}

GtkPicture *gtktext_image_widget_get_picture(GtktextImageWidget *image_widget) {
    g_return_val_if_fail(GTKTEXT_IS_IMAGE_WIDGET(image_widget), NULL);
    return image_widget->picture;
}

const gchar *gtktext_image_widget_get_url(GtktextImageWidget *image_widget) {
    g_return_val_if_fail(GTKTEXT_IS_IMAGE_WIDGET(image_widget), NULL);
    return image_widget->url;
}

const gchar *gtktext_image_widget_get_alt_text(GtktextImageWidget *image_widget) {
    g_return_val_if_fail(GTKTEXT_IS_IMAGE_WIDGET(image_widget), NULL);
    return image_widget->alt_text;
}

void gtktext_image_widget_set_alt_text(GtktextImageWidget *image_widget, const gchar *alt_text) {
    g_return_if_fail(GTKTEXT_IS_IMAGE_WIDGET(image_widget));
    
    if (g_strcmp0(image_widget->alt_text, alt_text) != 0) {
        g_free(image_widget->alt_text);
        image_widget->alt_text = g_strdup(alt_text);
        g_object_notify_by_pspec(G_OBJECT(image_widget), properties[PROP_ALT_TEXT]);
    }
}
