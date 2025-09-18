/* C ULTRA-MIN TEMPLATE
   Purpose: Temporary integration layer for TabManager with existing app structure
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-17 - ui/tab_integration.c
   Created: Bridge between new tab system and existing single-document architecture
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <adwaita.h>
#include <gtktext/ui/tab_manager.h>
#include <gtktext/ui/tab_document.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Integration functions for gradual migration to tabs
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Initialize tab system with existing app structure
 * Returns the first tab's text_view for compatibility
 */
GtkWidget *tab_integration_setup_with_single_tab(GtkApplication *app,
                                                 AdwTabView *tab_view,
                                                 AdwTabBar *tab_bar)
{
    /* Create tab manager */
    TabManager *tab_manager = tab_manager_new(tab_view, tab_bar, app);
    if (!tab_manager) {
        g_critical("Failed to create TabManager");
        return NULL;
    }

    /* Store tab manager in app data */
    g_object_set_data_full(G_OBJECT(app), "tab_manager", tab_manager,
                          (GDestroyNotify)tab_manager_destroy);

    /* Create initial welcome tab */
    AdwTabPage *initial_page = tab_manager_new_welcome(tab_manager);
    if (!initial_page) {
        g_critical("Failed to create initial welcome tab");
        return NULL;
    }

    /* For compatibility with existing code that expects a text view,
     * return a dummy text view. This will be replaced when documents are opened. */
    GtkWidget *text_view = gtk_text_view_new();

    g_debug("Tab system initialized with single tab for compatibility");
    return text_view;
}

/**
 * Handle "New Tab" button click
 */
void tab_integration_new_tab_action(GSimpleAction *action G_GNUC_UNUSED,
                                   GVariant *parameter G_GNUC_UNUSED,
                                   gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION(user_data);
    TabManager *tm = gtktext_get_tab_manager(app);

    if (tm) {
        tab_manager_new_document(tm, _("Untitled"));
    }
}

/**
 * Handle "Close Tab" action (Ctrl+W)
 */
void tab_integration_close_tab_action(GSimpleAction *action G_GNUC_UNUSED,
                                     GVariant *parameter G_GNUC_UNUSED,
                                     gpointer user_data)
{
    GtkApplication *app = GTK_APPLICATION(user_data);
    TabManager *tm = gtktext_get_tab_manager(app);

    if (tm) {
        AdwTabPage *active = tab_manager_get_active_tab(tm);
        if (active) {
            g_message("⌨️ KEYBOARD SHORTCUT: Ctrl+W pressed - will check for unsaved changes");
            tab_manager_close_tab(tm, active);
        }
    }
}

/**
 * Get the active text view from the current tab
 * For compatibility with existing code
 */
GtkWidget *tab_integration_get_active_text_view(GtkApplication *app)
{
    TabManager *tm = gtktext_get_tab_manager(app);
    if (!tm) return NULL;

    AdwTabPage *active = tab_manager_get_active_tab(tm);
    if (!active) return NULL;

    return tab_manager_get_text_view(tm, active);
}

/**
 * Get the active document manager from the current tab
 */
DocumentManager *tab_integration_get_active_doc_manager(GtkApplication *app)
{
    TabManager *tm = gtktext_get_tab_manager(app);
    if (!tm) return NULL;

    AdwTabPage *active = tab_manager_get_active_tab(tm);
    if (!active) return NULL;

    return tab_manager_get_document_manager(tm, active);
}