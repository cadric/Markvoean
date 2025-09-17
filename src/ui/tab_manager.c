/* C ULTRA-MIN TEMPLATE
   Purpose: Multi-document tab management for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-17 - ui/tab_manager.c
   Created: Implementation of tabbed document interface following GNOME HIG
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <adwaita.h>
#include <gtktext/ui/tab_manager.h>
#include <gtktext/ui/tab_document.h>
#include <gtktext/ui/dialogs.h>
#include <gtktext/ui/file_actions.h>
#include <gtktext/components/toolbar.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Private implementation details
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct _TabManagerPrivate {
    AdwTabView *tab_view;
    AdwTabBar *tab_bar;
    GtkApplication *app;
    GHashTable *tab_documents;  /* AdwTabPage -> TabDocument */
    AdwTabPage *active_page;
    gboolean initialized;
} TabManagerPrivate;

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Module state management
 * ═══════════════════════════════════════════════════════════════════════════════ */

struct _TabManager {
    TabManagerPrivate *priv;
};

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations */
static void on_tab_view_page_detached(AdwTabView *tab_view, AdwTabPage *page, gint position, gpointer user_data);
static gboolean page_belongs_to_view(AdwTabView *tab_view, AdwTabPage *page);
static void safe_close_page_finish(AdwTabView *tab_view, AdwTabPage *page, gboolean confirm);

/* Context for save-as dialog in close workflow */
typedef struct {
    TabManager *tm;
    AdwTabView *tab_view;
    AdwTabPage *page;
    TabDocument *tab_doc;
} SaveAsCloseContext;

/* Save-as dialog completion callback for close workflow */
static void on_save_as_close_dialog_finish(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GtkFileDialog *d = GTK_FILE_DIALOG(source_object);
    GError *finish_error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_save_finish(d, res, &finish_error);

    SaveAsCloseContext *context = (SaveAsCloseContext *)user_data;

    if (finish_error) {
        g_warning("Save-as dialog for close finished with error: %s", finish_error->message);
        g_clear_error(&finish_error);
        /* Cancel the close operation */
        safe_close_page_finish(context->tab_view, context->page, FALSE);
        g_free(context);
        return;
    }
    if (!file) {
        g_debug("Save-as dialog for close dismissed without selection");
        /* Cancel the close operation */
        safe_close_page_finish(context->tab_view, context->page, FALSE);
        g_free(context);
        return;
    }

    g_autofree char *path = g_file_get_path(file);
    g_debug("Save-as for close: file selected: %s", path ? path : "(null)");

    /* Save the document to the selected path */
    GError *save_error = NULL;
    if (!tab_document_save_as(context->tab_doc, path, &save_error)) {
        g_warning("Failed to save file during close: %s", save_error ? save_error->message : "Unknown error");
        g_clear_error(&save_error);
        /* Cancel the close operation */
        safe_close_page_finish(context->tab_view, context->page, FALSE);
    } else {
        g_message("File saved as during close: %s", path);
        /* Save successful, allow close */
        safe_close_page_finish(context->tab_view, context->page, TRUE);
    }

    g_free(context);
}

static void on_tab_page_notify_selected(GObject *object, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data)
{
    AdwTabPage *page = ADW_TAB_PAGE(object);
    TabManager *tm = (TabManager *)user_data;

    if (adw_tab_page_get_selected(page)) {
        tm->priv->active_page = page;
        g_debug("Tab selected: %s", adw_tab_page_get_title(page));

        /* Update toolbar to work with the new active tab */
        GtkWidget *text_view = tab_manager_get_text_view(tm, page);
        if (text_view && GTK_IS_TEXT_VIEW(text_view)) {
            toolbar_update_text_view(text_view);
            g_debug("Updated toolbar for tab switch");
        } else {
            /* Welcome tabs and some special tabs don't have text views - this is normal */
            g_debug("Tab does not have a text view (likely welcome tab): %s", adw_tab_page_get_title(page));
        }
    }
}

/* Phase 3: Helper function to check if page belongs to tab view */
static gboolean page_belongs_to_view(AdwTabView *tab_view, AdwTabPage *page)
{
    int n_pages = adw_tab_view_get_n_pages(tab_view);
    for (int i = 0; i < n_pages; i++) {
        if (adw_tab_view_get_nth_page(tab_view, i) == page) {
            return TRUE;
        }
    }
    return FALSE;
}

/* Phase 3: Safe wrapper for close_page_finish */
static void safe_close_page_finish(AdwTabView *tab_view, AdwTabPage *page, gboolean confirm)
{
    if (!tab_view || !ADW_IS_TAB_VIEW(tab_view)) {
        g_warning("Invalid tab view in safe_close_page_finish");
        return;
    }

    if (!page || !ADW_IS_TAB_PAGE(page)) {
        g_warning("Invalid page in safe_close_page_finish");
        return;
    }

    if (page_belongs_to_view(tab_view, page)) {
        /* Guard against duplicate finish calls on the same page */
        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page), "gtktext-closing"))) {
            g_debug("Page already in closing state; ignoring duplicate close_page_finish");
            return;
        }

        g_debug("Calling adw_tab_view_close_page_finish(confirm=%s)", confirm ? "TRUE" : "FALSE");

        /* Mark as closing to prevent re-entrancy */
        g_object_set_data(G_OBJECT(page), "gtktext-closing", GINT_TO_POINTER(1));

        /* Keep page alive during finish to avoid use-after-free in signal chain */
        g_object_ref(page);
        adw_tab_view_close_page_finish(tab_view, page, confirm);

        /* If the close was cancelled (confirm == FALSE), clear the guard */
        if (!confirm) {
            g_object_set_data(G_OBJECT(page), "gtktext-closing", NULL);
        }
        g_object_unref(page);
    } else {
        g_warning("Attempted to close page that doesn't belong to tab view - skipping close_page_finish");
        /* Don't call close_page_finish if page doesn't belong to view */
        /* This prevents the Adwaita assertion error */
    }
}

/* Phase 3: Signal handler for page detachment - clean up tab resources */
static void on_tab_view_page_detached(AdwTabView *tab_view, AdwTabPage *page, gint position G_GNUC_UNUSED, gpointer user_data)
{
    /* Validate tab_view first */
    if (!tab_view || !ADW_IS_TAB_VIEW(tab_view)) {
        g_debug("Invalid tab_view in detach signal, skipping cleanup");
        return;
    }

    /* Validate page parameter first */
    if (!page || !ADW_IS_TAB_PAGE(page)) {
        g_debug("Invalid page in detach signal, skipping cleanup");
        return;
    }

    /* Prefer TabManager from user_data, but be robust and fall back to tab_view data */
    TabManager *tm = (TabManager *)user_data;
    if (!tm || !((TabManager *)tm)->priv || ((TabManager *)tm)->priv->tab_view != tab_view) {
        tm = (TabManager *)g_object_get_data(G_OBJECT(tab_view), "tab_manager");
    }

    /* Enhanced validation with pointer integrity checks */
    if (!tm) {
        g_debug("NULL TabManager in detach signal, skipping cleanup");
        return;
    }

    /* Check if the struct itself is valid */
    if (!tm->priv) {
        g_debug("TabManager private data is NULL in detach signal, skipping cleanup");
        return;
    }

    /* Check if TabManager has been marked as destroyed */
    if (!tm->priv->initialized) {
        g_debug("TabManager not initialized in detach signal, skipping cleanup");
        return;
    }

    /* Check hash table validity */
    if (!tm->priv->tab_documents) {
        g_debug("TabManager documents hash table is NULL in detach signal, skipping cleanup");
        return;
    }

    /* Check if hash table contains this page before lookup */
    if (!g_hash_table_contains(tm->priv->tab_documents, page)) {
        g_debug("Page not found in documents hash table during detach");
        return;
    }

    /* CRITICAL FIX: If the closed page was active, reset active_page to prevent use-after-free */
    if (tm->priv->active_page == page) {
        tm->priv->active_page = adw_tab_view_get_selected_page(tab_view);
        g_debug("Active page was detached, updated to new selected page");
        /* Can be NULL if no pages left, that's OK */
    }

    /* Proactively disconnect any handlers tied to this page to avoid callbacks during teardown */
    g_signal_handlers_disconnect_by_data(page, tm);

    /* Remove mapping and destroy document */
    TabDocument *tab_doc = g_hash_table_lookup(tm->priv->tab_documents, page);
    if (tab_doc) {
        /* Double-check that TabDocument is not already being destroyed */
        if (tab_doc->being_destroyed) {
            g_debug("TabDocument already being destroyed, removing from hash table only");
            g_hash_table_remove(tm->priv->tab_documents, page);
            return;
        }

        /* Stop dirty-state callbacks firing into TabManager during destroy */
        tab_document_set_dirty_state_callback(tab_doc, NULL, NULL);

        /* First remove from hash table to prevent re-entry */
        g_hash_table_remove(tm->priv->tab_documents, page);
        /* Then destroy the document */
        tab_document_destroy(tab_doc);
        g_debug("Cleaned up tab resources for detached page");
    }
}

/* Phase 3: Context structure for unsaved changes dialog */
typedef struct {
    TabManager *tm;
    AdwTabView *tab_view;
    AdwTabPage *page;
    TabDocument *tab_doc;
} UnsavedChangesContext;

/* Phase 3: Dialog response callback for unsaved changes */
static void on_unsaved_changes_dialog_response(AdwAlertDialog *dialog G_GNUC_UNUSED, const char *response, gpointer user_data)
{
    UnsavedChangesContext *context = (UnsavedChangesContext *)user_data;

    if (g_strcmp0(response, "save") == 0) {
        /* Save the document first, then close */
        const char *file_path = tab_document_get_file_path(context->tab_doc);
        if (!file_path) {
            /* Untitled document - show save-as dialog */
            GtkWidget *parent_window = gtk_widget_get_ancestor(GTK_WIDGET(context->tab_view), GTK_TYPE_WINDOW);
            GtkWindow *parent = parent_window ? GTK_WINDOW(parent_window) : NULL;

            if (!parent) {
                g_warning("Could not find parent window for save-as dialog");
                safe_close_page_finish(context->tab_view, context->page, FALSE);
                return;
            }

            GtkFileDialog *dlg = gtk_file_dialog_new();
            gtk_file_dialog_set_title(dlg, _("Save As"));

            /* Set default filename for untitled documents */
            gtk_file_dialog_set_initial_name(dlg, "untitled.md");

            /* Create context for save-as completion */
            SaveAsCloseContext *save_context = g_new(SaveAsCloseContext, 1);
            save_context->tm = context->tm;
            save_context->tab_view = context->tab_view;
            save_context->page = context->page;
            save_context->tab_doc = context->tab_doc;

            gtk_file_dialog_save(dlg, parent, NULL, on_save_as_close_dialog_finish, save_context);
            g_object_unref(dlg);
        } else {
            /* Named document - direct save */
            GError *error = NULL;
            if (tab_document_save(context->tab_doc, &error)) {
                /* Save successful, allow close */
                safe_close_page_finish(context->tab_view, context->page, TRUE);
            } else {
                /* Save failed, don't close */
                g_warning("Failed to save document: %s", error ? error->message : "Unknown error");
                g_clear_error(&error);
                safe_close_page_finish(context->tab_view, context->page, FALSE);
            }
        }
    } else if (g_strcmp0(response, "discard") == 0) {
        /* Discard changes and close */
        safe_close_page_finish(context->tab_view, context->page, TRUE);
    } else {
        /* Cancel - don't close */
        safe_close_page_finish(context->tab_view, context->page, FALSE);
    }

    g_free(context);
}

/* Phase 3: Show unsaved changes confirmation dialog */
static void show_unsaved_changes_dialog(TabManager *tm, AdwTabView *tab_view, AdwTabPage *page, TabDocument *tab_doc)
{
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(tab_view)));
    const char *doc_title = tab_document_get_display_title(tab_doc);

    g_autofree char *heading = g_strdup_printf(_("Save changes to \"%s\" before closing?"), doc_title);
    g_autofree char *body = g_strdup(_("If you don't save, your changes will be permanently lost."));

    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(heading, body));

    /* Add dialog responses */
    adw_alert_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_alert_dialog_add_response(dialog, "discard", _("Close without Saving"));
    adw_alert_dialog_add_response(dialog, "save", _("Save"));

    /* Set default and suggested responses */
    adw_alert_dialog_set_default_response(dialog, "save");
    adw_alert_dialog_set_response_appearance(dialog, "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_response_appearance(dialog, "save", ADW_RESPONSE_SUGGESTED);

    /* Create context for callback */
    UnsavedChangesContext *context = g_new(UnsavedChangesContext, 1);
    context->tm = tm;
    context->tab_view = tab_view;
    context->page = page;
    context->tab_doc = tab_doc;

    /* Connect response signal */
    g_signal_connect(dialog, "response", G_CALLBACK(on_unsaved_changes_dialog_response), context);

    /* Present dialog */
    adw_alert_dialog_choose(dialog, GTK_WIDGET(parent), NULL, NULL, NULL);
}

static gboolean on_tab_view_close_page(AdwTabView *tab_view, AdwTabPage *page, gpointer user_data)
{
    TabManager *tm = (TabManager *)user_data;

    /* Enhanced validation */
    if (!tm || !tm->priv || !tm->priv->initialized || !tm->priv->tab_documents) {
        g_warning("TabManager invalid during close page");
        return GDK_EVENT_STOP;
    }

    if (!page || !ADW_IS_TAB_PAGE(page)) {
        g_warning("Invalid page during close");
        return GDK_EVENT_STOP;
    }

    /* Check if page still belongs to this view */
    if (!page_belongs_to_view(tab_view, page)) {
        g_warning("Page does not belong to view during close");
        return GDK_EVENT_STOP;
    }

    /* Guard: avoid duplicate close handling for the same page */
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page), "gtktext-closing"))) {
        g_debug("Page already closing; ignoring duplicate close request");
        return GDK_EVENT_STOP;
    }

    TabDocument *tab_doc = g_hash_table_lookup(tm->priv->tab_documents, page);
    g_debug("Close page requested for %s", tab_doc ? "document tab" : "unknown tab");

    if (!tab_doc) {
        /* Allow close if no document associated */
    g_debug("No document associated, allowing close");
    safe_close_page_finish(tab_view, page, TRUE);
        return GDK_EVENT_STOP;
    }

    /* Phase 3: Check for unsaved changes and show confirmation dialog */
    if (tab_doc && tab_document_get_modified(tab_doc)) {
        g_debug("Document has unsaved changes, showing dialog");
    show_unsaved_changes_dialog(tm, tab_view, page, tab_doc);
        return GDK_EVENT_STOP; /* Don't call close_page_finish yet - dialog will handle it */
    } else {
        /* No unsaved changes, allow close */
        g_debug("No unsaved changes, allowing close");
        safe_close_page_finish(tab_view, page, TRUE);
        return GDK_EVENT_STOP;
    }
}

/* Phase 3: Tab title updates with dirty state indicators */
static void update_tab_title(TabManager *tm, AdwTabPage *page)
{
    TabDocument *tab_doc = g_hash_table_lookup(tm->priv->tab_documents, page);
    if (!tab_doc) return;

    const char *base_title = tab_document_get_display_title(tab_doc);
    gboolean is_modified = tab_document_get_modified(tab_doc);

    if (is_modified) {
        g_autofree char *modified_title = g_strdup_printf("%s •", base_title);
        adw_tab_page_set_title(page, modified_title);
    } else {
        adw_tab_page_set_title(page, base_title);
    }

    /* Set tooltip to full path if available */
    const char *file_path = tab_document_get_file_path(tab_doc);
    if (file_path) {
        adw_tab_page_set_tooltip(page, file_path);
    } else {
        adw_tab_page_set_tooltip(page, base_title);
    }
}

/* Phase 3: Callback for TabDocument dirty state changes */
static void on_tab_document_dirty_state_changed(TabDocument *td, gboolean is_dirty, gpointer user_data)
{
    TabManager *tm = (TabManager *)user_data;

    /* Find the AdwTabPage for this TabDocument */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, tm->priv->tab_documents);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (value == td) {
            AdwTabPage *page = (AdwTabPage *)key;
            update_tab_title(tm, page);
            g_debug("Updated tab title due to dirty state change: %s", is_dirty ? "dirty" : "clean");
            return;
        }
    }
    g_warning("TabDocument not found in tab manager hash table");
}

/* Phase 4: Right-click context menu handler - simplified for now */
static void on_tab_view_right_click(GtkGestureClick *gesture G_GNUC_UNUSED, gint n_press G_GNUC_UNUSED,
                                   gdouble x G_GNUC_UNUSED, gdouble y G_GNUC_UNUSED, gpointer user_data)
{
    TabManager *tm = (TabManager *)user_data;

    /* Get the clicked tab page */
    AdwTabPage *page = adw_tab_view_get_selected_page(tm->priv->tab_view);
    if (!page) return;

    /* For now, just show debug message - full context menu implementation needs more complex GTK4 setup */
    g_debug("Tab right-clicked - context menu would appear here");

    /* TODO: Implement proper context menu with GTK4 APIs */
    /* This would require more complex popover setup that's GTK4-compatible */
}

/* Phase 4: Keyboard navigation between tabs */
static gboolean on_tab_view_key_pressed(GtkEventControllerKey *controller G_GNUC_UNUSED, guint keyval, guint keycode,
                                        GdkModifierType state, gpointer user_data)
{
    (void)keycode;
    TabManager *tm = (TabManager *)user_data;

    /* Check for Ctrl modifier */
    if (!(state & GDK_CONTROL_MASK)) {
        return FALSE; /* Let other handlers process */
    }

    switch (keyval) {
        case GDK_KEY_Page_Up:
        case GDK_KEY_ISO_Left_Tab: /* Ctrl+Shift+Tab */
            /* Previous tab */
            tab_manager_select_previous_tab(tm);
            return TRUE;

        case GDK_KEY_Page_Down:
        case GDK_KEY_Tab:
            /* Next tab (but only Ctrl+Tab, not Ctrl+Shift+Tab) */
            if (!(state & GDK_SHIFT_MASK) || keyval == GDK_KEY_Page_Down) {
                tab_manager_select_next_tab(tm);
                return TRUE;
            }
            break;

        default:
            break;
    }

    return FALSE; /* Let other handlers process */
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - TabManager lifecycle and operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

TabManager *tab_manager_new(AdwTabView *tab_view, AdwTabBar *tab_bar, GtkApplication *app)
{
    g_return_val_if_fail(ADW_IS_TAB_VIEW(tab_view), NULL);
    g_return_val_if_fail(ADW_IS_TAB_BAR(tab_bar), NULL);
    g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);

    TabManager *tm = g_new0(TabManager, 1);
    tm->priv = g_new0(TabManagerPrivate, 1);

    tm->priv->tab_view = tab_view;
    tm->priv->tab_bar = tab_bar;
    tm->priv->app = app;
    tm->priv->tab_documents = g_hash_table_new(g_direct_hash, g_direct_equal);
    tm->priv->initialized = TRUE;

    /* Store back-reference on the view for robust signal callbacks */
    g_object_set_data(G_OBJECT(tab_view), "tab_manager", tm);

    /* Connect tab bar to tab view */
    adw_tab_bar_set_view(tab_bar, tab_view);

    /* Set autohide - show bar only when >1 tab */
    adw_tab_bar_set_autohide(tab_bar, TRUE);

    /* Phase 4: Enable advanced tab features */
    adw_tab_bar_set_expand_tabs(tab_bar, FALSE);  /* Don't expand tabs to fill space */

    /* Connect signals */
    g_signal_connect(tab_view, "close-page",
                    G_CALLBACK(on_tab_view_close_page), tm);
    g_signal_connect(tab_view, "page-detached",
                    G_CALLBACK(on_tab_view_page_detached), tm);

    /* Phase 4: Add keyboard navigation controller */
    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed",
                    G_CALLBACK(on_tab_view_key_pressed), tm);
    gtk_widget_add_controller(GTK_WIDGET(tab_view), key_controller);

    /* Phase 4: Add right-click context menu for tabs */
    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(on_tab_view_right_click), tm);
    gtk_widget_add_controller(GTK_WIDGET(tab_bar), GTK_EVENT_CONTROLLER(click_gesture));

    g_debug("TabManager created");
    return tm;
}

void tab_manager_destroy(TabManager *tm)
{
    if (!tm) return;

    g_debug("Starting TabManager destruction");

    if (tm->priv) {
        /* Step 1: Disconnect ALL signals IMMEDIATELY to prevent any callbacks */
        if (tm->priv->tab_view && G_IS_OBJECT(tm->priv->tab_view)) {
            g_debug("Disconnecting signals from tab_view");
            g_signal_handlers_disconnect_by_data(tm->priv->tab_view, tm);
            /* Clear back-reference to avoid stale pointer */
            g_object_set_data(G_OBJECT(tm->priv->tab_view), "tab_manager", NULL);
        }

        /* Step 2: Mark as uninitialized to block signal handlers */
        tm->priv->initialized = FALSE;

        /* Step 3: Clear the hash table first to prevent lookups */
        if (tm->priv->tab_documents) {
            g_debug("Cleaning up tab documents");
            GHashTableIter iter;
            gpointer key, value;
            g_hash_table_iter_init(&iter, tm->priv->tab_documents);
            while (g_hash_table_iter_next(&iter, &key, &value)) {
                TabDocument *tab_doc = (TabDocument *)value;
                if (tab_doc) {
                    tab_document_destroy(tab_doc);
                }
            }
            g_hash_table_destroy(tm->priv->tab_documents);
            tm->priv->tab_documents = NULL; /* Explicitly null the pointer */
        }

        /* Step 4: Clear tab_view reference */
        tm->priv->tab_view = NULL;

        g_free(tm->priv);
        tm->priv = NULL; /* Explicitly null the pointer */
    }
    g_free(tm);

    g_debug("TabManager destroyed successfully");
}

AdwTabPage *tab_manager_new_document(TabManager *tm, const char *title)
{
    g_return_val_if_fail(tm != NULL, NULL);
    g_return_val_if_fail(tm->priv != NULL, NULL);
    g_return_val_if_fail(tm->priv->initialized, NULL);

    /* Create new tab document */
    TabDocument *tab_doc = tab_document_new();
    if (!tab_doc) {
        g_warning("Failed to create tab document");
        return NULL;
    }

    /* Create tab page with document widget */
    AdwTabPage *page = adw_tab_view_add_page(tm->priv->tab_view,
                                            tab_document_get_widget(tab_doc), NULL);

    /* Set tab properties */
    const char *display_title = title ? title : _("Untitled");
    adw_tab_page_set_title(page, display_title);
    adw_tab_page_set_icon(page, g_themed_icon_new("text-x-generic-symbolic"));

    /* Store document association */
    g_hash_table_insert(tm->priv->tab_documents, page, tab_doc);

    /* Phase 3: Set up dirty state callback */
    tab_document_set_dirty_state_callback(tab_doc, on_tab_document_dirty_state_changed, tm);

    /* Connect selection signal */
    g_signal_connect(page, "notify::selected",
                    G_CALLBACK(on_tab_page_notify_selected), tm);

    /* Select the new tab */
    adw_tab_view_set_selected_page(tm->priv->tab_view, page);

    g_debug("Created new document tab: %s", display_title);
    return page;
}

AdwTabPage *tab_manager_open_file(TabManager *tm, const char *file_path)
{
    g_return_val_if_fail(tm != NULL, NULL);
    g_return_val_if_fail(file_path != NULL, NULL);

    /* Check if file is already open in a tab */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, tm->priv->tab_documents);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        TabDocument *tab_doc = (TabDocument *)value;
        const char *doc_path = tab_document_get_file_path(tab_doc);
        if (doc_path && g_strcmp0(doc_path, file_path) == 0) {
            /* File already open, just select its tab */
            AdwTabPage *page = (AdwTabPage *)key;
            adw_tab_view_set_selected_page(tm->priv->tab_view, page);
            g_debug("File already open in tab, selecting: %s", file_path);
            return page;
        }
    }

    /* Create new tab with file */
    TabDocument *tab_doc = tab_document_new_from_file(file_path);
    if (!tab_doc) {
        g_warning("Failed to load file: %s", file_path);
        return NULL;
    }

    /* Create tab page */
    AdwTabPage *page = adw_tab_view_add_page(tm->priv->tab_view,
                                            tab_document_get_widget(tab_doc), NULL);

    /* Set tab properties */
    adw_tab_page_set_title(page, tab_document_get_display_title(tab_doc));
    adw_tab_page_set_tooltip(page, file_path);

    /* Set icon based on file type */
    if (g_str_has_suffix(file_path, ".md")) {
        adw_tab_page_set_icon(page, g_themed_icon_new("text-x-markdown-symbolic"));
    } else {
        adw_tab_page_set_icon(page, g_themed_icon_new("text-x-generic-symbolic"));
    }

    /* Store document association */
    g_hash_table_insert(tm->priv->tab_documents, page, tab_doc);

    /* Phase 3: Set up dirty state callback */
    tab_document_set_dirty_state_callback(tab_doc, on_tab_document_dirty_state_changed, tm);

    /* Connect selection signal */
    g_signal_connect(page, "notify::selected",
                    G_CALLBACK(on_tab_page_notify_selected), tm);

    /* Select the new tab */
    adw_tab_view_set_selected_page(tm->priv->tab_view, page);

    g_debug("Opened file in new tab: %s", file_path);
    return page;
}

void tab_manager_close_tab(TabManager *tm, AdwTabPage *page)
{
    g_return_if_fail(tm != NULL);
    g_return_if_fail(page != NULL);

    /* Close will trigger the close-page signal which handles cleanup */
    adw_tab_view_close_page(tm->priv->tab_view, page);
}

void tab_manager_close_all_tabs(TabManager *tm)
{
    g_return_if_fail(tm != NULL);

    /* Close all pages */
    int n_pages = adw_tab_view_get_n_pages(tm->priv->tab_view);
    for (int i = n_pages - 1; i >= 0; i--) {
        AdwTabPage *page = adw_tab_view_get_nth_page(tm->priv->tab_view, i);
        adw_tab_view_close_page(tm->priv->tab_view, page);
    }
}

TabDocument *tab_manager_get_tab_document(TabManager *tm, AdwTabPage *page)
{
    g_return_val_if_fail(tm != NULL, NULL);
    g_return_val_if_fail(page != NULL, NULL);

    return g_hash_table_lookup(tm->priv->tab_documents, page);
}

DocumentManager *tab_manager_get_document_manager(TabManager *tm, AdwTabPage *page)
{
    TabDocument *tab_doc = tab_manager_get_tab_document(tm, page);
    return tab_doc ? tab_doc->doc_manager : NULL;
}

GtkWidget *tab_manager_get_text_view(TabManager *tm, AdwTabPage *page)
{
    TabDocument *tab_doc = tab_manager_get_tab_document(tm, page);
    return tab_doc ? tab_document_get_text_view(tab_doc) : NULL;
}

AdwTabPage *tab_manager_get_active_tab(TabManager *tm)
{
    g_return_val_if_fail(tm != NULL, NULL);
    return adw_tab_view_get_selected_page(tm->priv->tab_view);
}

TabDocument *tab_manager_get_active_document(TabManager *tm)
{
    AdwTabPage *active_page = tab_manager_get_active_tab(tm);
    return active_page ? tab_manager_get_tab_document(tm, active_page) : NULL;
}

void tab_manager_set_active_tab(TabManager *tm, AdwTabPage *page)
{
    g_return_if_fail(tm != NULL);
    g_return_if_fail(page != NULL);

    adw_tab_view_set_selected_page(tm->priv->tab_view, page);
}

gboolean tab_manager_has_unsaved_changes(TabManager *tm, AdwTabPage *page)
{
    TabDocument *tab_doc = tab_manager_get_tab_document(tm, page);
    return tab_doc ? tab_document_get_modified(tab_doc) : FALSE;
}

gint tab_manager_get_tab_count(TabManager *tm)
{
    g_return_val_if_fail(tm != NULL, 0);
    return adw_tab_view_get_n_pages(tm->priv->tab_view);
}

const char *tab_manager_get_tab_title(TabManager *tm G_GNUC_UNUSED, AdwTabPage *page)
{
    g_return_val_if_fail(page != NULL, NULL);
    return adw_tab_page_get_title(page);
}

/* Phase 3: Tab title management functions */
void tab_manager_update_tab_title(TabManager *tm, AdwTabPage *page)
{
    g_return_if_fail(tm != NULL);
    g_return_if_fail(page != NULL);

    update_tab_title(tm, page);
    g_debug("Updated tab title for page");
}

void tab_manager_mark_tab_dirty(TabManager *tm, AdwTabPage *page, gboolean dirty)
{
    g_return_if_fail(tm != NULL);
    g_return_if_fail(page != NULL);

    TabDocument *tab_doc = g_hash_table_lookup(tm->priv->tab_documents, page);
    if (!tab_doc) {
        g_warning("TabDocument not found for page");
        return;
    }

    /* Update document dirty state */
    tab_document_set_modified(tab_doc, dirty);

    /* Update tab title to reflect new state */
    update_tab_title(tm, page);

    g_debug("Marked tab as %s", dirty ? "dirty" : "clean");
}

/* Phase 4: Tab navigation functions */
void tab_manager_select_next_tab(TabManager *tm)
{
    g_return_if_fail(tm != NULL);

    AdwTabPage *current = adw_tab_view_get_selected_page(tm->priv->tab_view);
    if (!current) return;

    gint current_pos = adw_tab_view_get_page_position(tm->priv->tab_view, current);
    gint n_pages = adw_tab_view_get_n_pages(tm->priv->tab_view);

    if (n_pages <= 1) return; /* Only one tab, nothing to switch to */

    /* Select next tab, wrapping around */
    gint next_pos = (current_pos + 1) % n_pages;
    AdwTabPage *next_page = adw_tab_view_get_nth_page(tm->priv->tab_view, next_pos);
    if (next_page) {
        adw_tab_view_set_selected_page(tm->priv->tab_view, next_page);
        g_debug("Selected next tab at position %d", next_pos);
    }
}

void tab_manager_select_previous_tab(TabManager *tm)
{
    g_return_if_fail(tm != NULL);

    AdwTabPage *current = adw_tab_view_get_selected_page(tm->priv->tab_view);
    if (!current) return;

    gint current_pos = adw_tab_view_get_page_position(tm->priv->tab_view, current);
    gint n_pages = adw_tab_view_get_n_pages(tm->priv->tab_view);

    if (n_pages <= 1) return; /* Only one tab, nothing to switch to */

    /* Select previous tab, wrapping around */
    gint prev_pos = (current_pos - 1 + n_pages) % n_pages;
    AdwTabPage *prev_page = adw_tab_view_get_nth_page(tm->priv->tab_view, prev_pos);
    if (prev_page) {
        adw_tab_view_set_selected_page(tm->priv->tab_view, prev_page);
        g_debug("Selected previous tab at position %d", prev_pos);
    }
}

/* Global accessor implementation */
TabManager *gtktext_get_tab_manager(GtkApplication *app)
{
    g_return_val_if_fail(GTK_IS_APPLICATION(app), NULL);
    return g_object_get_data(G_OBJECT(app), "tab_manager");
}