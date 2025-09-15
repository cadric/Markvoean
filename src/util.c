/* C ULTRA‑MIN TEMPLATE
   Purpose: Simple utility functions following CLAUDE.md template structure
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#include "config.h" // if present
#include <adwaita.h>

/* ========== META ========== */
/* [0.2.0] - 2025-09-15 - src/util.c
 * Added: Ultra-Min C Module Template example implementation.
 */

/* ========== TYPES ========== */
typedef struct {
    gchar *name;
    gint count;
    gboolean active;
} UtilState;

/* ========== STATE ========== */
static UtilState *global_state = NULL;

/* ========== HELPERS ========== */
static void util_state_reset(UtilState *state) {
    g_return_if_fail(state != NULL);

    g_free(state->name);
    state->name = NULL;
    state->count = 0;
    state->active = FALSE;
}

G_GNUC_UNUSED static gboolean util_validate_name(const gchar *name, GError **error) {
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
G_GNUC_UNUSED static void on_state_activate(G_GNUC_UNUSED GSimpleAction *action,
                              G_GNUC_UNUSED GVariant *parameter,
                              gpointer user_data) {
    g_return_if_fail(user_data != NULL);

    UtilState *state = (UtilState *)user_data;
    state->active = !state->active;
    state->count++;

    g_message("State toggled: active=%s, count=%d",
              state->active ? "TRUE" : "FALSE", state->count);
}

/* ========== WIRING ========== */
void util_connect_signals(GtkWidget *widget) {
    g_return_if_fail(GTK_IS_WIDGET(widget));

    // Example signal connection - would connect real signals in practice
    // g_signal_connect(widget, "signal-name", G_CALLBACK(callback), data);
}

/* ========== LIFECYCLE ========== */
void util_init(const gchar *initial_name) {
    g_return_if_fail(initial_name != NULL);

    if (global_state != NULL) {
        g_warning("Util state already initialized");
        return;
    }

    global_state = g_new0(UtilState, 1);
    global_state->name = g_strdup(initial_name);
    global_state->count = 0;
    global_state->active = FALSE;

    g_message("Util initialized with name: %s", initial_name);
}

void util_teardown(void) {
    if (global_state == NULL) {
        return;
    }

    util_state_reset(global_state);
    g_free(global_state);
    global_state = NULL;

    g_message("Util teardown complete");
}