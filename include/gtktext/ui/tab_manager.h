/* Tab Manager Header
 * Purpose: Multi-document tab management for GTK markdown editor
 * [1.0.1] - 2025-09-18 - ui/tab_manager.h
 */

#pragma once

#ifndef GTKTEXT_TAB_MANAGER_H
#define GTKTEXT_TAB_MANAGER_H

#include <gtk/gtk.h>
#include <adwaita.h>
#include <gtktext/document/document_manager.h>  /* exposes DocumentManager */

G_BEGIN_DECLS

/* Opaque types */
typedef struct _TabManager TabManager;
typedef struct _TabDocument TabDocument;

/* Lifecycle */
TabManager  *tab_manager_new(AdwTabView *tab_view, AdwTabBar *tab_bar, GtkApplication *app);
void         tab_manager_destroy(TabManager *tm);

/* Create/Open */
AdwTabPage  *tab_manager_new_document(TabManager *tm, const char *title);
AdwTabPage  *tab_manager_new_welcome(TabManager *tm);
AdwTabPage  *tab_manager_open_file(TabManager *tm, const char *file_path);

/* Per-tab ops */
void         tab_manager_close_tab(TabManager *tm, AdwTabPage *page);
void         tab_manager_close_all_tabs(TabManager *tm);
void         tab_manager_set_active_tab(TabManager *tm, AdwTabPage *page);

/* Queries */
gint         tab_manager_get_tab_count(TabManager *tm);
AdwTabPage  *tab_manager_get_active_tab(TabManager *tm);
TabDocument *tab_manager_get_active_document(TabManager *tm);
gboolean     tab_manager_has_unsaved_changes(TabManager *tm, AdwTabPage *page);
gboolean     tab_manager_has_any_unsaved_changes(TabManager *tm);
AdwTabPage  *tab_manager_get_first_unsaved_tab(TabManager *tm);
void         tab_manager_cleanup_all_signals(TabManager *tm);
const char  *tab_manager_get_tab_title(TabManager *tm, AdwTabPage *page);
TabDocument *tab_manager_get_tab_document(TabManager *tm, AdwTabPage *page);
DocumentManager *tab_manager_get_document_manager(TabManager *tm, AdwTabPage *page);
GtkWidget   *tab_manager_get_text_view(TabManager *tm, AdwTabPage *page);

/* Title/dirty state */
void         tab_manager_update_tab_title(TabManager *tm, AdwTabPage *page);
void         tab_manager_mark_tab_dirty(TabManager *tm, AdwTabPage *page, gboolean dirty);

/* Window close dialog */
void         tab_manager_show_window_close_dialog(GtkWindow *window, TabDocument *tab_doc);

/* Navigation */
void         tab_manager_select_next_tab(TabManager *tm);
void         tab_manager_select_previous_tab(TabManager *tm);

/* Global accessor (implemented in ui/tab_manager.c).
 * Contract: the app stores this pointer via tab_manager_new().
 */
TabManager  *gtktext_get_tab_manager(GtkApplication *app);

G_END_DECLS
#endif /* GTKTEXT_TAB_MANAGER_H */
