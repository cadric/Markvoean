/* Tab Manager Header
 * Purpose: Multi-document tab management for GTK markdown editor
 * [1.0.0] - 2025-09-17 - ui/tab_manager.h
 * Created: Implementation of tabbed document interface following GNOME HIG
 */

#ifndef GTKTEXT_TAB_MANAGER_H
#define GTKTEXT_TAB_MANAGER_H

#include <gtk/gtk.h>
#include <adwaita.h>
#include <gtktext/document/document_manager.h>

G_BEGIN_DECLS

/* Opaque handle for tab manager - implementation details hidden */
typedef struct _TabManager TabManager;

/* Tab document container */
typedef struct _TabDocument TabDocument;

/* Factory pattern with proper lifecycle management */
TabManager *tab_manager_new(AdwTabView *tab_view, AdwTabBar *tab_bar, GtkApplication *app);
void tab_manager_destroy(TabManager *tm);

/* Tab operations */
AdwTabPage *tab_manager_new_document(TabManager *tm, const char *title);
AdwTabPage *tab_manager_new_welcome(TabManager *tm);
AdwTabPage *tab_manager_open_file(TabManager *tm, const char *file_path);
void tab_manager_close_tab(TabManager *tm, AdwTabPage *page);
void tab_manager_close_all_tabs(TabManager *tm);

/* Document access */
TabDocument *tab_manager_get_tab_document(TabManager *tm, AdwTabPage *page);
DocumentManager *tab_manager_get_document_manager(TabManager *tm, AdwTabPage *page);
GtkWidget *tab_manager_get_text_view(TabManager *tm, AdwTabPage *page);

/* Active tab operations */
AdwTabPage *tab_manager_get_active_tab(TabManager *tm);
TabDocument *tab_manager_get_active_document(TabManager *tm);
void tab_manager_set_active_tab(TabManager *tm, AdwTabPage *page);

/* Tab state queries */
gboolean tab_manager_has_unsaved_changes(TabManager *tm, AdwTabPage *page);
gint tab_manager_get_tab_count(TabManager *tm);
const char *tab_manager_get_tab_title(TabManager *tm, AdwTabPage *page);

/* Tab title management - Phase 3 */
void tab_manager_update_tab_title(TabManager *tm, AdwTabPage *page);
void tab_manager_mark_tab_dirty(TabManager *tm, AdwTabPage *page, gboolean dirty);

/* Tab navigation - Phase 4 */
void tab_manager_select_next_tab(TabManager *tm);
void tab_manager_select_previous_tab(TabManager *tm);

/* Global accessor - implemented in app_initialization.c */
TabManager *gtktext_get_tab_manager(GtkApplication *app);

G_END_DECLS

#endif /* GTKTEXT_TAB_MANAGER_H */