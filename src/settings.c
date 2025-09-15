#include <adwaita.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

/* [0.2.0] - 2025-09-15 - src/settings.c
 * Changed: Added proper input validation with g_return_if_fail().
 */
#include "settings.h"

// Simpelt settings-vindue med en lukke-knap
AdwDialog* create_settings_window(GtkWindow *parent) {
    g_return_val_if_fail(parent == NULL || GTK_IS_WINDOW(parent), NULL);
    
    // Brug AdwPreferencesDialog for en mere standard GNOME-stil (nyere API)
    AdwDialog *dialog = adw_preferences_dialog_new();
    
    // AdwPreferencesDialog er ikke længere en GtkWindow i nyere libadwaita,
    // så vi bruger de korrekte metoder i stedet
    adw_dialog_set_title(dialog, "Indstillinger");
    adw_dialog_set_content_width(dialog, 500);
    adw_dialog_set_content_height(dialog, 400);
    adw_dialog_set_presentation_mode(dialog, ADW_DIALOG_AUTO);
    adw_dialog_set_follows_content_size(dialog, TRUE);
    
    // Sæt parent-vinduet
    adw_dialog_present(dialog, GTK_WIDGET(parent));
    
    // Tilføj en tom præferenceside som placeholder
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
    // Optional subtitle; commented out for broader libadwaita compatibility
    // adw_action_row_set_subtitle(ADW_ACTION_ROW(row), _("Milliseconds idle before saving"));

    GtkAdjustment *adj = gtk_adjustment_new(500.0, 0.0, 10000.0, 100.0, 500.0, 0.0);
    GtkWidget *spin = gtk_spin_button_new(adj, 100.0, 0);
    gtk_widget_set_valign(spin, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), spin);
    gtk_widget_set_hexpand(spin, FALSE);

    // Bind to GSettings key if schema is available
    GSettings *settings = g_settings_new("org.gtk.gtktext");
    if (settings) {
        // Try to get and set the current value, using default if key doesn't exist
        guint current = g_settings_get_uint(settings, "autosave-delay-ms");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), current);
        g_settings_bind(settings, "autosave-delay-ms", spin, "value", G_SETTINGS_BIND_DEFAULT);
        g_object_unref(settings);
    }

    adw_preferences_group_add(group, row);
    adw_preferences_page_add(page, group);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), page);
    
    return dialog;
}
