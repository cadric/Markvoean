/* Tab Integration Header
 * Purpose: Temporary integration layer for TabManager with existing app structure
 * [1.0.0] - 2025-09-17 - ui/tab_integration.h
 * Created: Bridge between new tab system and existing single-document architecture
 */

#ifndef GTKTEXT_TAB_INTEGRATION_H
#define GTKTEXT_TAB_INTEGRATION_H

#include <gtk/gtk.h>
#include <adwaita.h>
#include <gtktext/document/document_manager.h>

G_BEGIN_DECLS

/* Initialize tab system with compatibility layer */
GtkWidget *tab_integration_setup_with_single_tab(GtkApplication *app,
                                                 AdwTabView *tab_view,
                                                 AdwTabBar *tab_bar);

/* Tab actions */
void tab_integration_new_tab_action(GSimpleAction *action,
                                   GVariant *parameter,
                                   gpointer user_data);

void tab_integration_close_tab_action(GSimpleAction *action,
                                     GVariant *parameter,
                                     gpointer user_data);

/* Compatibility accessors */
GtkWidget *tab_integration_get_active_text_view(GtkApplication *app);
DocumentManager *tab_integration_get_active_doc_manager(GtkApplication *app);

G_END_DECLS

#endif /* GTKTEXT_TAB_INTEGRATION_H */