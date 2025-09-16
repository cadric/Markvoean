/* C ULTRA‑MIN TEMPLATE
   Purpose: Settings dialog and configuration management
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#include "config.h"
#include <adwaita.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/core/settings.h>

/* ========== META ========== */
/* [1.0.1] - 2025-09-16 - src/settings.c
   Changed: Removed legacy autosave settings - DocumentManager handles autosave internally
 */

/* ========== TYPES ========== */
typedef struct {
    AdwDialog *dialog;
    GSettings *settings;
    AdwSwitchRow *autosave_switch;
} SettingsComponent;

/* ========== STATE ========== */
static SettingsComponent settings_state = { 0 };

/* ========== HELPERS ========== */
static void settings_reset(SettingsComponent *c) {
    g_return_if_fail(c != NULL);
    
    if (c->dialog) {
        g_clear_object(&c->dialog);
    }
    if (c->autosave_switch) {
        c->autosave_switch = NULL;
    }
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
static void on_autosave_switch_changed(AdwSwitchRow *switch_row, 
                                      GParamSpec *pspec,
                                      gpointer user_data)
{
    (void)pspec;
    SettingsComponent *c = (SettingsComponent *)user_data;
    g_return_if_fail(c != NULL && c->settings != NULL);
    
    gboolean enabled = adw_switch_row_get_active(switch_row);
    g_settings_set_boolean(c->settings, "autosave-enabled", enabled);
    
    g_debug("Autosave setting changed to: %s", enabled ? "enabled" : "disabled");
}

/* ========== HANDLERS ========== */
/* Legacy autosave handlers removed - DocumentManager handles autosave internally */

/* ========== WIRING ========== */
static void settings_connect_signals(SettingsComponent *c) {
    g_return_if_fail(c != NULL);
    
    if (c->autosave_switch) {
        g_signal_connect(c->autosave_switch, "notify::active",
                        G_CALLBACK(on_autosave_switch_changed), c);
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
    
    // Get GSettings instance
    settings_state.settings = g_settings_new("org.gtk.gtktext");
    
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
    adw_preferences_group_set_title(group, _("Document"));

    // Create autosave switch
    GtkWidget *autosave_switch_widget = adw_switch_row_new();
    settings_state.autosave_switch = ADW_SWITCH_ROW(autosave_switch_widget);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(settings_state.autosave_switch), _("Enable Autosave"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(settings_state.autosave_switch), 
                               _("Automatically save changes every few seconds"));
    
    // Set initial state from GSettings
    gboolean autosave_enabled = g_settings_get_boolean(settings_state.settings, "autosave-enabled");
    adw_switch_row_set_active(settings_state.autosave_switch, autosave_enabled);
    
    // Add switch to group
    adw_preferences_group_add(group, GTK_WIDGET(settings_state.autosave_switch));

    /* Future settings can be added here */

    adw_preferences_page_add(page, group);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(settings_state.dialog), page);
    
    // Connect signals
    settings_connect_signals(&settings_state);
    
    return settings_state.dialog;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Settings access functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static GSettings *global_app_settings = NULL;

/**
 * Get the application settings instance
 */
GSettings* gtktext_get_app_settings(void)
{
    if (!global_app_settings) {
        global_app_settings = g_settings_new("org.gtk.gtktext");
    }
    return global_app_settings;
}
