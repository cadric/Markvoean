#include <adwaita.h>
#include <gtk/gtk.h>

#include "settings.h"

// Simpelt settings-vindue med en lukke-knap
AdwDialog* create_settings_window(GtkWindow *parent) {
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
    adw_preferences_group_set_title(group, "Indstillinger");
    adw_preferences_group_set_description(group, "Her kommer indstillinger i en fremtidig version");
    
    adw_preferences_page_add(page, group);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), page);
    
    return dialog;
}
