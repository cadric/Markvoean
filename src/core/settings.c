/* C ULTRA‑MIN TEMPLATE
   Purpose: Settings dialog and configuration management
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#include "config.h"
#include <adwaita.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/core/settings.h>
#include <gtktext/core/settings_manager.h>

/* ========== META ========== */
/* [1.0.1] - 2025-09-16 - src/settings.c
   Changed: Removed legacy autosave settings - DocumentManager handles autosave internally
 */

/* ========== TYPES ========== */
typedef struct {
    AdwDialog *dialog;
    GSettings *settings;
    AdwSwitchRow *autosave_switch;
    AdwComboRow *toolbar_style_row;
    AdwSwitchRow *toolbar_customization_switch;
    AdwPreferencesGroup *toolbar_order_group;
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
    if (c->toolbar_style_row) {
        c->toolbar_style_row = NULL;
    }
    if (c->toolbar_customization_switch) {
        c->toolbar_customization_switch = NULL;
    }
    if (c->toolbar_order_group) {
        c->toolbar_order_group = NULL;
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

static void on_toolbar_style_changed(AdwComboRow *combo_row,
                                    GParamSpec *pspec,
                                    gpointer user_data)
{
    (void)pspec;
    SettingsComponent *c = (SettingsComponent *)user_data;
    g_return_if_fail(c != NULL && c->settings != NULL);

    guint selected = adw_combo_row_get_selected(combo_row);
    const char *style_value = "icons"; // Default

    switch (selected) {
        case 0:
            style_value = "icons";
            break;
        case 1:
            style_value = "text";
            break;
        case 2:
            style_value = "both";
            break;
    }

    g_settings_set_string(c->settings, "toolbar-style", style_value);
    g_debug("Toolbar style changed to: %s", style_value);
}

static void on_toolbar_customization_changed(AdwSwitchRow *switch_row,
                                            GParamSpec *pspec,
                                            gpointer user_data)
{
    (void)pspec;
    SettingsComponent *c = (SettingsComponent *)user_data;
    g_return_if_fail(c != NULL && c->settings != NULL);

    gboolean enabled = adw_switch_row_get_active(switch_row);
    g_settings_set_boolean(c->settings, "toolbar-customization", enabled);

    // Show/hide button order group based on customization state
    if (c->toolbar_order_group) {
        gtk_widget_set_visible(GTK_WIDGET(c->toolbar_order_group), enabled);
    }

    g_debug("Toolbar customization changed to: %s", enabled ? "enabled" : "disabled");
}

/* Legacy autosave handlers removed - DocumentManager handles autosave internally */

/* ========== WIRING ========== */
static void settings_connect_signals(SettingsComponent *c) {
    g_return_if_fail(c != NULL);

    if (c->autosave_switch) {
        g_signal_connect(c->autosave_switch, "notify::active",
                        G_CALLBACK(on_autosave_switch_changed), c);
    }

    if (c->toolbar_style_row) {
        g_signal_connect(c->toolbar_style_row, "notify::selected",
                        G_CALLBACK(on_toolbar_style_changed), c);
    }

    if (c->toolbar_customization_switch) {
        g_signal_connect(c->toolbar_customization_switch, "notify::active",
                        G_CALLBACK(on_toolbar_customization_changed), c);
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

    // Create toolbar preferences group
    GtkWidget *toolbar_group_widget = adw_preferences_group_new();
    AdwPreferencesGroup *toolbar_group = ADW_PREFERENCES_GROUP(toolbar_group_widget);
    adw_preferences_group_set_title(toolbar_group, _("Toolbar"));
    adw_preferences_group_set_description(toolbar_group, _("Customize the toolbar appearance and layout"));

    // Create toolbar style combo row
    GtkWidget *style_row_widget = adw_combo_row_new();
    settings_state.toolbar_style_row = ADW_COMBO_ROW(style_row_widget);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(settings_state.toolbar_style_row), _("Button Style"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(settings_state.toolbar_style_row),
                               _("Choose how toolbar buttons are displayed"));

    // Set up style options
    GtkStringList *style_model = gtk_string_list_new((const char *[]){
        _("Icons only"),
        _("Text only"),
        _("Icons and text"),
        NULL
    });
    adw_combo_row_set_model(settings_state.toolbar_style_row, G_LIST_MODEL(style_model));

    // Set current selection from settings
    g_autofree gchar *toolbar_style = g_settings_get_string(settings_state.settings, "toolbar-style");
    guint style_index = 0;  // Default to icons
    if (g_strcmp0(toolbar_style, "text") == 0) {
        style_index = 1;
    } else if (g_strcmp0(toolbar_style, "both") == 0) {
        style_index = 2;
    }
    adw_combo_row_set_selected(settings_state.toolbar_style_row, style_index);

    // Create toolbar customization switch
    GtkWidget *custom_switch_widget = adw_switch_row_new();
    settings_state.toolbar_customization_switch = ADW_SWITCH_ROW(custom_switch_widget);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(settings_state.toolbar_customization_switch),
                                 _("Enable Button Reordering"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(settings_state.toolbar_customization_switch),
                               _("Allow customizing the order of toolbar buttons"));

    gboolean customization_enabled = g_settings_get_boolean(settings_state.settings, "toolbar-customization");
    adw_switch_row_set_active(settings_state.toolbar_customization_switch, customization_enabled);

    // Add toolbar widgets to group
    adw_preferences_group_add(toolbar_group, GTK_WIDGET(settings_state.toolbar_style_row));
    adw_preferences_group_add(toolbar_group, GTK_WIDGET(settings_state.toolbar_customization_switch));

    // Create button order group (visible only when customization is enabled)
    GtkWidget *order_group_widget = adw_preferences_group_new();
    settings_state.toolbar_order_group = ADW_PREFERENCES_GROUP(order_group_widget);
    adw_preferences_group_set_title(settings_state.toolbar_order_group, _("Button Order"));
    adw_preferences_group_set_description(settings_state.toolbar_order_group,
                                         _("Customize the order of toolbar buttons"));

    // Create info row about button order
    g_auto(GStrv) button_order = g_settings_get_strv(settings_state.settings, "toolbar-button-order");
    GString *order_text = g_string_new(_("Current order: "));
    for (int i = 0; button_order[i] != NULL; i++) {
        if (i > 0) g_string_append(order_text, " → ");

        // Translate button IDs to user-friendly names
        const char *display_name = button_order[i];
        if (g_strcmp0(button_order[i], "bold") == 0) {
            display_name = _("Bold");
        } else if (g_strcmp0(button_order[i], "italic") == 0) {
            display_name = _("Italic");
        } else if (g_strcmp0(button_order[i], "code") == 0) {
            display_name = _("Code");
        } else if (g_strcmp0(button_order[i], "heading") == 0) {
            display_name = _("Heading");
        } else if (g_strcmp0(button_order[i], "hr") == 0) {
            display_name = _("Horizontal Rule");
        } else if (g_strcmp0(button_order[i], "source") == 0) {
            display_name = _("Source View");
        }
        g_string_append(order_text, display_name);
    }

    GtkWidget *order_label = gtk_label_new(order_text->str);
    gtk_widget_set_margin_top(order_label, 12);
    gtk_widget_set_margin_bottom(order_label, 12);
    gtk_widget_set_margin_start(order_label, 12);
    gtk_widget_set_margin_end(order_label, 12);
    gtk_label_set_wrap(GTK_LABEL(order_label), TRUE);
    adw_preferences_group_add(settings_state.toolbar_order_group, order_label);

    // Show/hide based on customization state
    gtk_widget_set_visible(GTK_WIDGET(settings_state.toolbar_order_group), customization_enabled);

    g_string_free(order_text, TRUE);

    // Add groups to page
    adw_preferences_page_add(page, toolbar_group);
    adw_preferences_page_add(page, settings_state.toolbar_order_group);

    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(settings_state.dialog), page);
    
    // Connect signals
    settings_connect_signals(&settings_state);
    
    return settings_state.dialog;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Settings access functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Get the application settings instance - delegates to settings_manager
 */
GSettings* gtktext_get_app_settings(void)
{
    return settings_manager_initialize_app_settings();
}
