/* C ULTRA‑MIN TEMPLATE
   Purpose: Settings dialog and configuration management
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#include "config.h"
#include <adwaita.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/settings.h>

/* ========== META ========== */
/* [0.3.0] - 2025-09-15 - src/settings.c
 * Changed: Restructured to follow Ultra-Min template pattern.
 */

/* ========== TYPES ========== */
typedef struct {
    AdwDialog *dialog;
    GtkSpinButton *autosave_spin;
    GSettings *settings;
} SettingsComponent;

/* ========== STATE ========== */
static SettingsComponent settings_state = { 0 };

/* ========== HELPERS ========== */
static void settings_reset(SettingsComponent *c) {
    g_return_if_fail(c != NULL);
    
    if (c->dialog) {
        g_clear_object(&c->dialog);
    }
    c->autosave_spin = NULL;
    c->settings = NULL;
}

static gboolean validate_parent_window(GtkWindow *parent, GError **error) {
    g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
    
    if (parent && !GTK_IS_WINDOW(parent)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Parent must be a valid GtkWindow or NULL");
        return FALSE;
    }
    return TRUE;
}

/* ========== HANDLERS ========== */

/* ========== HANDLERS ========== */
static void on_autosave_changed(GtkSpinButton *spin, gpointer user_data) {
    (void)user_data;
    g_return_if_fail(GTK_IS_SPIN_BUTTON(spin));
    
    gdouble value = gtk_spin_button_get_value(spin);
    if (settings_state.settings) {
        g_settings_set_int(settings_state.settings, "autosave-delay", (gint)value);
    }
    g_debug("Autosave delay changed to: %.0f", value);
}

/* ========== WIRING ========== */
static void settings_connect_signals(SettingsComponent *c) {
    g_return_if_fail(c != NULL);
    
    if (c->autosave_spin) {
        g_signal_connect(c->autosave_spin, "value-changed", 
                        G_CALLBACK(on_autosave_changed), NULL);
    }
}

/* ========== LIFECYCLE ========== */
AdwDialog* create_settings_window(GtkWindow *parent) {
    g_autoptr(GError) error = NULL;
    
    if (!validate_parent_window(parent, &error)) {
        g_warning("Invalid parent window: %s", error->message);
        return NULL;
    }
    
    settings_reset(&settings_state);
    
    // Create dialog
    settings_state.dialog = adw_preferences_dialog_new();
    adw_dialog_set_title(settings_state.dialog, "Indstillinger");
    adw_dialog_set_content_width(settings_state.dialog, 500);
    adw_dialog_set_content_height(settings_state.dialog, 400);
    adw_dialog_set_presentation_mode(settings_state.dialog, ADW_DIALOG_AUTO);
    adw_dialog_set_follows_content_size(settings_state.dialog, TRUE);
    
    // Present dialog to parent
    adw_dialog_present(settings_state.dialog, GTK_WIDGET(parent));
    
    // Create preferences page
    GtkWidget *page_widget = adw_preferences_page_new();
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(page_widget);
    adw_preferences_page_set_title(page, "Generelt");
    adw_preferences_page_set_icon_name(page, "preferences-system-symbolic");
    
    GtkWidget *group_widget = adw_preferences_group_new();
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(group_widget);
    adw_preferences_group_set_title(group, _("Preferences"));

    // Autosave delay setting
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Autosave delay"));

    GtkAdjustment *adj = gtk_adjustment_new(500.0, 0.0, 10000.0, 100.0, 500.0, 0.0);
    GtkWidget *spin = gtk_spin_button_new(adj, 100.0, 0);
    settings_state.autosave_spin = GTK_SPIN_BUTTON(spin);
    gtk_widget_set_valign(spin, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), spin);
    gtk_widget_set_hexpand(spin, FALSE);

    // Bind to GSettings if available
    settings_state.settings = g_settings_new("org.gtk.gtktext");
    if (settings_state.settings) {
        guint current = g_settings_get_uint(settings_state.settings, "autosave-delay-ms");
        gtk_spin_button_set_value(settings_state.autosave_spin, current);
        g_settings_bind(settings_state.settings, "autosave-delay-ms", spin, "value", G_SETTINGS_BIND_DEFAULT);
    }

    adw_preferences_group_add(group, row);
    adw_preferences_page_add(page, group);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(settings_state.dialog), page);
    
    // Connect signals
    settings_connect_signals(&settings_state);
    
    return settings_state.dialog;
}
