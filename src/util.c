/* C ULTRA‑MIN TEMPLATE
   Purpose: GObject utility state for managing application utilities
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#include "config.h"
#include <adwaita.h>
#include <gtktext/util.h>
#include <glib-object.h>

/* ========== META ========== */
/* [0.2.0] - 2025-09-15 - src/util.c
 * Changed: Converted to proper GObject type system implementation.
 */

/* ========== TYPES ========== */
#define GTKTEXT_TYPE_UTIL_STATE (gtktext_util_state_get_type())
G_DECLARE_FINAL_TYPE(GtktextUtilState, gtktext_util_state, GTKTEXT, UTIL_STATE, GObject)

struct _GtktextUtilState {
    GObject parent_instance;
    gchar *name;
    gint count;
    gboolean active;
};

/* ========== STATE ========== */
G_DEFINE_TYPE(GtktextUtilState, gtktext_util_state, G_TYPE_OBJECT)

static GtktextUtilState *global_state = NULL;

typedef enum {
    PROP_0,
    PROP_NAME,
    PROP_COUNT,
    PROP_ACTIVE,
    N_PROPS
} GtktextUtilStateProperty;

static GParamSpec *properties[N_PROPS];

/* ========== HELPERS ========== */
static void gtktext_util_state_finalize(GObject *object) {
    GtktextUtilState *self = GTKTEXT_UTIL_STATE(object);
    
    g_free(self->name);
    
    G_OBJECT_CLASS(gtktext_util_state_parent_class)->finalize(object);
}

static void gtktext_util_state_get_property(GObject *object,
                                             guint prop_id,
                                             GValue *value,
                                             GParamSpec *pspec) {
    GtktextUtilState *self = GTKTEXT_UTIL_STATE(object);
    
    switch (prop_id) {
    case PROP_NAME:
        g_value_set_string(value, self->name);
        break;
    case PROP_COUNT:
        g_value_set_int(value, self->count);
        break;
    case PROP_ACTIVE:
        g_value_set_boolean(value, self->active);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gtktext_util_state_set_property(GObject *object,
                                             guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec) {
    GtktextUtilState *self = GTKTEXT_UTIL_STATE(object);
    
    switch (prop_id) {
    case PROP_NAME:
        g_free(self->name);
        self->name = g_value_dup_string(value);
        break;
    case PROP_COUNT:
        self->count = g_value_get_int(value);
        break;
    case PROP_ACTIVE:
        self->active = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static gboolean util_validate_name(const gchar *name, GError **error) {
    g_return_val_if_fail(name != NULL, FALSE);
    g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

    if (g_utf8_strlen(name, -1) == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Name cannot be empty");
        return FALSE;
    }

    return TRUE;
}

/* ========== HANDLERS ========== */
static void util_state_reset(GtktextUtilState *state) {
    g_return_if_fail(GTKTEXT_IS_UTIL_STATE(state));
    
    g_object_set(state,
                 "name", "",
                 "count", 0,
                 "active", FALSE,
                 NULL);
}

static void on_widget_event(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    g_return_if_fail(GTK_IS_WIDGET(widget));
    
    if (global_state) {
        g_object_set(global_state,
                     "count", global_state->count + 1,
                     "active", TRUE,
                     NULL);
        g_debug("Widget event occurred, count: %d", global_state->count);
    }
}

static void gtktext_util_state_class_init(GtktextUtilStateClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    
    object_class->finalize = gtktext_util_state_finalize;
    object_class->get_property = gtktext_util_state_get_property;
    object_class->set_property = gtktext_util_state_set_property;
    
    /* Properties */
    properties[PROP_NAME] = g_param_spec_string("name",
                                                "Name",
                                                "The name of the utility state",
                                                "",
                                                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    
    properties[PROP_COUNT] = g_param_spec_int("count",
                                              "Count",
                                              "The count value",
                                              0, G_MAXINT, 0,
                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    
    properties[PROP_ACTIVE] = g_param_spec_boolean("active",
                                                   "Active",
                                                   "Whether the utility is active",
                                                   FALSE,
                                                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    
    g_object_class_install_properties(object_class, N_PROPS, properties);
}

static void gtktext_util_state_init(GtktextUtilState *self) {
    self->name = g_strdup("");
    self->count = 0;
    self->active = FALSE;
}

static GtktextUtilState *gtktext_util_state_new(const gchar *name) {
    return g_object_new(GTKTEXT_TYPE_UTIL_STATE,
                        "name", name ? name : "",
                        NULL);
}

/* ========== WIRING ========== */
void util_connect_signals(GtkWidget *widget) {
    g_return_if_fail(GTK_IS_WIDGET(widget));
    
    // Example: connect to button clicked or other signals
    if (GTK_IS_BUTTON(widget)) {
        g_signal_connect(widget, "clicked", G_CALLBACK(on_widget_event), NULL);
    }
}

/* ========== LIFECYCLE ========== */
void util_init(const gchar *initial_name) {
    g_autoptr(GError) error = NULL;
    
    if (!util_validate_name(initial_name, &error)) {
        g_warning("Invalid initial name for util: %s", error->message);
        initial_name = "default";
    }
    
    if (global_state) {
        g_warning("Utility already initialized, resetting");
        util_state_reset(global_state);
        g_object_set(global_state, "name", initial_name, NULL);
    } else {
        global_state = gtktext_util_state_new(initial_name);
        g_debug("Utility initialized with name: %s", initial_name);
    }
}

void util_teardown(void) {
    if (global_state) {
        g_debug("Tearing down utility state");
        g_clear_object(&global_state);
    }
}