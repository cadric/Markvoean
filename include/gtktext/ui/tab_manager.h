/* Tab Manager Header
 * Purpose: Multi-document tab management for GTK markdown editor
 * [1.0.1] - 2025-09-18 - ui/tab_manager.h
 * FIXED: Complete API redesign with opaque types, all public functions declared
 */

#ifndef GTKTEXT_TAB_MANAGER_H
#define GTKTEXT_TAB_MANAGER_H

#include <gtk/gtk.h>
#include <adwaita.h>

G_BEGIN_DECLS

/* Opaque handle */
typedef struct _TabManager TabManager;
/* Forward declare without exposing implementation */
typedef struct _TabDocument TabDocument;
typedef struct _DocumentManager DocumentManager;

/* Lifecycle */
TabManager *tab_manager_new(AdwTabView *tab_view, AdwTabBar *tab_bar, GtkApplication *app);
void        tab_manager_destroy(TabManager *tm);

/* Tab creation */
AdwTabPage *tab_manager_new_document(TabManager *tm, const char *title);
AdwTabPage *tab_manager_new_welcome(TabManager *tm);
AdwTabPage *tab_manager_open_file(TabManager *tm, const char *file_path);

/* Per-tab operations */
void        tab_manager_set_active_tab(TabManager *tm, AdwTabPage *page);
void        tab_manager_close_tab(TabManager *tm, AdwTabPage *page);
void        tab_manager_close_all_tabs(TabManager *tm);

/* Document access */
TabDocument*tab_manager_get_tab_document(TabManager *tm, AdwTabPage *page);
DocumentManager *tab_manager_get_document_manager(TabManager *tm, AdwTabPage *page);
GtkWidget  *tab_manager_get_text_view(TabManager *tm, AdwTabPage *page);

/* Active tab operations */
AdwTabPage *tab_manager_get_active_tab(TabManager *tm);
TabDocument*tab_manager_get_active_document(TabManager *tm);

/* Queries */
gint        tab_manager_get_tab_count(TabManager *tm);
gboolean    tab_manager_has_unsaved_changes(TabManager *tm, AdwTabPage *page);
const char *tab_manager_get_tab_title(TabManager *tm, AdwTabPage *page);

/* Title/dirty state */
void        tab_manager_update_tab_title(TabManager *tm, AdwTabPage *page);
void        tab_manager_mark_tab_dirty(TabManager *tm, AdwTabPage *page, gboolean dirty);

/* Navigation */
void        tab_manager_select_next_tab(TabManager *tm);
void        tab_manager_select_previous_tab(TabManager *tm);

/* Global accessor
 * NOTE: Implemented in ui/tab_manager.c. Ensure the app stores the pointer:
 *   g_object_set_data(G_OBJECT(app), "tab_manager", tm);
 */
TabManager *gtktext_get_tab_manager(GtkApplication *app);

G_END_DECLS
#endif /* GTKTEXT_TAB_MANAGER_H */