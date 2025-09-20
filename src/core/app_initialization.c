/* C ULTRA-MIN TEMPLATE
   Purpose: Application initialization and UI setup for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.8] - 2025-09-16 - core/app_initialization.c
   Created: Extracted application initialization from main.c for better organization
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#ifdef HAVE_LIBSOUP
#include <libsoup/soup.h>
#endif
#include <glib/gi18n.h>
#include <adwaita.h>

#include <gtktext/core/app_initialization.h>
#include <gtktext/core/window_size_manager.h>
#include <gtktext/components/toolbar.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/render/theme_styles.h>
#include <gtktext/render/images/http_images.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/document/document.h>
#include <gtktext/core/settings.h>
#include <gtktext/core/settings_manager.h>
#include <gtktext/core/window_lifecycle.h>
#include <gtktext/ui/file_actions.h>
#include <gtktext/ui/app_actions.h>
#include <gtktext/ui/dialogs.h>
#include <gtktext/ui/image_embedder.h>
#include <gtktext/ui/text_view_interactions.h>
#include <gtktext/ui/tab_manager.h>
#include <gtktext/ui/status_manager.h>
#include <gtktext/ui/event_handlers.h>
#include <gtktext/render/markdown/markdown_engine.h>
#include <gtktext/editor/buffer_manager.h>
#include <gtktext/core/signal_manager.h>
#include <gtktext/ui/tab_manager.h>
#include <gtktext/ui/tab_integration.h>

/* External functions now handled by specialized modules */

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Application initialization functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

void app_initialization_activate(GApplication *application)
{
    GtkApplication *app = GTK_APPLICATION(application);
    GtkBuilder *builder = gtk_builder_new();
    /* Set translation domain for .ui strings */
    gtk_builder_set_translation_domain(builder, GETTEXT_PACKAGE);

    /* Try installed path first, then fall back to local paths for dev */
    const char *ui_candidates[] = {
        PKGDATADIR "/ui/main_window.ui",
        "./ui/main_window.ui",
        "../ui/main_window.ui",
        NULL
    };
    gboolean ui_loaded = FALSE;
    for (int i = 0; ui_candidates[i] != NULL; i++) {
        if (g_file_test(ui_candidates[i], G_FILE_TEST_EXISTS)) {
            GError *err = NULL;
            ui_loaded = gtk_builder_add_from_file(builder, ui_candidates[i], &err);
            if (!ui_loaded) {
                g_warning("Failed to load UI from %s: %s", ui_candidates[i],
                         err ? err->message : "unknown error");
                g_clear_error(&err);
            }
            break;
        }
    }
    if (!ui_loaded) {
        g_critical("Failed to load any UI file for main_window");
        g_object_unref(builder);
        return;
    }

    /* Setup GSettings schemas before creating settings objects */
    settings_manager_setup_gsettings_schemas();

    /* Create main window */
    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!window) {
        g_critical("Failed to get main_window from UI");
        g_object_unref(builder);
        return;
    }
    gtk_window_set_application(GTK_WINDOW(window), GTK_APPLICATION(app));
    /* Ensure window action context has the application action group */
    gtk_widget_insert_action_group(window, "app", G_ACTION_GROUP(app));

    /* Setup GNOME HIG-compliant window sizing with persistence */
    GSettings *settings = g_settings_new("org.gtk.gtktext");
    window_size_manager_setup_window(GTK_WINDOW(window), settings);
    g_object_set_data_full(G_OBJECT(window), "settings", settings, g_object_unref);

    /* Load adaptive layout CSS */
    GtkCssProvider *css_provider = gtk_css_provider_new();
    const char *css_candidates[] = {
        "data/adaptive-layout.css",          /* Development */
        "share/gtktext/adaptive-layout.css", /* Installed */
        NULL
    };
    for (int i = 0; css_candidates[i] != NULL; i++) {
        if (g_file_test(css_candidates[i], G_FILE_TEST_EXISTS)) {
            GFile *css_file = g_file_new_for_path(css_candidates[i]);
            gtk_css_provider_load_from_file(css_provider, css_file);
            g_object_unref(css_file);
            gtk_style_context_add_provider_for_display(
                gtk_widget_get_display(window),
                GTK_STYLE_PROVIDER(css_provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            g_debug("Loaded adaptive CSS from: %s", css_candidates[i]);
            break;
        }
    }
    g_object_unref(css_provider);

    /* Get tab infrastructure from UI */
    AdwTabView *tab_view = ADW_TAB_VIEW(gtk_builder_get_object(builder, "tab_view"));
    AdwTabBar *tab_bar = ADW_TAB_BAR(gtk_builder_get_object(builder, "tab_bar"));
    if (!tab_view || !tab_bar) {
        g_critical("Failed to get tab_view or tab_bar from UI");
        g_object_unref(builder);
        return;
    }

    /* Initialize tab system with compatibility layer */
    GtkWidget *text_view = tab_integration_setup_with_single_tab(app, tab_view, tab_bar);
    if (!text_view) {
        g_critical("Failed to initialize tab system");
        g_object_unref(builder);
        return;
    }

    /* Phase 5: Get status bar widgets - restored for tab-based UI */
    GtkWidget *save_status = GTK_WIDGET(gtk_builder_get_object(builder, "save_status"));
    GtkWidget *file_location = GTK_WIDGET(gtk_builder_get_object(builder, "file_location"));

    /* Store status bar widgets for access by status manager */
    if (save_status) {
        g_object_set_data(G_OBJECT(app), "save_status", save_status);
    }
    if (file_location) {
        g_object_set_data(G_OBJECT(app), "file_location", file_location);
    }


    /* Expose widgets to application scope for actions to use */
    g_object_set_data(G_OBJECT(app), "text_view", text_view);  /* Compatibility - points to first tab's text view */

    /* Status management will be handled per-tab in Phase 2 */

    /* Create and initialize DocumentManager */
    GtkWindow *main_window = gtk_application_get_active_window(app);
    GtkTextBuffer *dm_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    DocumentManager *doc_manager = document_manager_new(dm_buffer, main_window);
    if (!doc_manager) {
        g_critical("Failed to create DocumentManager");
        g_object_unref(builder);
        return;
    }
    g_object_set_data_full(G_OBJECT(app), "doc_manager", doc_manager,
                          (GDestroyNotify)document_manager_free);

    /* Connect to DocumentManager state-changed signal */
    g_signal_connect(doc_manager, "state-changed",
                    G_CALLBACK(event_handlers_on_document_state_changed), app);

    /* Set up settings monitoring through settings manager */
    settings_manager_initialize_document_settings(app, doc_manager);

    /* Start autosave */
    document_manager_start_autosave(doc_manager);

    /* Create a global soup session for image fetching */
#ifdef HAVE_LIBSOUP
    SoupSession *global_soup_session = soup_session_new();
    g_object_set_data_full(G_OBJECT(app), "soup_session", global_soup_session,
                          g_object_unref);
#endif

    /* Store builder reference with window */
    g_object_set_data_full(G_OBJECT(window), "builder", g_object_ref(builder),
                          g_object_unref);

    /* Phase 5: Initialize toolbar with active tab's text view (after builder is stored) */
    /* Find the AdwToolbarView directly from UI builder */
    GObject *toolbar_view_obj = gtk_builder_get_object(builder, "main_window");
    if (toolbar_view_obj && ADW_IS_APPLICATION_WINDOW(toolbar_view_obj)) {
        GtkWidget *content = gtk_window_get_child(GTK_WINDOW(toolbar_view_obj));
        g_debug("Window content widget: %s", content ? G_OBJECT_TYPE_NAME(content) : "NULL");

        /* AdwApplicationWindow might wrap content multiple times, let's search deeper */
        GtkWidget *toolbar_view = content;
        while (toolbar_view && !ADW_IS_TOOLBAR_VIEW(toolbar_view)) {
            GtkWidget *child = gtk_widget_get_first_child(toolbar_view);
            g_debug("Searching deeper: current=%s, child=%s",
                   G_OBJECT_TYPE_NAME(toolbar_view),
                   child ? G_OBJECT_TYPE_NAME(child) : "NULL");
            if (!child) break;
            toolbar_view = child;
        }

        if (toolbar_view && ADW_IS_TOOLBAR_VIEW(toolbar_view)) {
            /* Use the existing create_toolbar function to get fully functional toolbar */
            GtkWidget *functional_toolbar = create_toolbar(text_view);
            if (functional_toolbar) {
                /* Style the functional toolbar to align with header button positioning */
                gtk_widget_set_halign(functional_toolbar, GTK_ALIGN_START);
                gtk_widget_set_margin_start(functional_toolbar, 0);
                gtk_widget_set_margin_end(functional_toolbar, 6);

                /* Add the functional toolbar directly as second top-bar */
                adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), functional_toolbar);

                g_debug("Toolbar added as second top-bar successfully");
            }
        } else {
            g_warning("Could not find AdwToolbarView - toolbar_view is: %s",
                     toolbar_view ? G_OBJECT_TYPE_NAME(toolbar_view) : "NULL");
        }
    } else {
        g_warning("Could not find main_window in builder");
    }

    /* Setup key controller to detect Ctrl+C */
    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(event_handlers_on_key_pressed), text_view);
    gtk_widget_add_controller(text_view, key_controller);

    /* Setup motion controller for link hover cursor */
    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion", G_CALLBACK(event_handlers_on_text_view_motion), text_view);
    gtk_widget_add_controller(text_view, motion_controller);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    /* Initialize GSettings */
    GSettings *app_settings = settings_manager_initialize_app_settings();
    if (!app_settings) {
        g_warning("GSettings schema org.gtk.gtktext not found");
    }

    /* Keep a back-pointer from buffer to the view for reparse callbacks */
    g_object_set_data(G_OBJECT(buffer), "gtktext-view", text_view);

    /* Store app reference on buffer for save dialog callbacks */
    g_object_set_data(G_OBJECT(buffer), "app", app);

    /* Store soup session on buffer for reparse callbacks */
#ifdef HAVE_LIBSOUP
    SoupSession *soup_session = g_object_get_data(G_OBJECT(app), "soup_session");
    if (soup_session) {
        g_object_set_data(G_OBJECT(buffer), "soup-session", soup_session);
    }
#endif

    /* Install blockquote overlay for visual left border */
    text_view_setup_blockquote_overlay(GTK_TEXT_VIEW(text_view));

    /* Welcome screen removed - handled by tabs now */

    /* Initialize buffer manager and connect change handlers */
    buffer_manager_initialize_buffer(buffer, app);

    /* Connect buffer change signal through proper signal manager */
    SignalManager *sm = gtktext_get_signal_manager();
    if (sm) {
        signal_manager_connect_buffer_changed(sm, buffer,
                                            G_CALLBACK(buffer_manager_on_text_changed), NULL);
    }
    /* Initialize markdown engine for real-time processing */
#ifdef HAVE_LIBSOUP
    markdown_engine_initialize_buffer(buffer, GTK_TEXT_VIEW(text_view), soup_session);
#else
    markdown_engine_initialize_buffer(buffer, GTK_TEXT_VIEW(text_view), NULL);
#endif

    /* Enable tooltips and connect the query-tooltip signal */
    gtk_widget_set_has_tooltip(GTK_WIDGET(text_view), TRUE);
    g_signal_connect(text_view, "query-tooltip", G_CALLBACK(event_handlers_on_text_view_query_tooltip), NULL);

    /* Create a GtkGestureClick controller for link clicking */
    GtkGesture *click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect(click_gesture, "pressed", G_CALLBACK(event_handlers_on_text_view_link_clicked), text_view);
    gtk_widget_add_controller(GTK_WIDGET(text_view), GTK_EVENT_CONTROLLER(click_gesture));

    /* Add zoom support with Ctrl+mouse wheel */
    GtkEventController *scroll_controller = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_controller, "scroll", G_CALLBACK(event_handlers_on_scroll_event), text_view);
    gtk_widget_add_controller(GTK_WIDGET(text_view), scroll_controller);

    /* Add signal for window close */
    g_signal_connect(window, "close-request", G_CALLBACK(window_lifecycle_on_window_close_request), text_view);

    /* Wire headerbar buttons to actions */
    GtkWidget *new_tab_button = GTK_WIDGET(gtk_builder_get_object(builder, "new_tab_button"));
    GtkWidget *open_button = GTK_WIDGET(gtk_builder_get_object(builder, "open_button"));
    GtkWidget *save_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_button"));
    GtkWidget *save_as_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_as_button"));
    if (new_tab_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(new_tab_button), "app.new-tab");
    if (open_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(open_button), "app.open");
    if (save_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(save_button), "app.save");
    if (save_as_button) gtk_actionable_set_action_name(GTK_ACTIONABLE(save_as_button),
                                                      "app.save-as");

    /* Accessibility: Provide accessible names for icon-only buttons */
    if (open_button) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(open_button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Open"), -1);
    }
    if (save_button) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(save_button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Save"), -1);
    }
    if (save_as_button) {
        gtk_accessible_update_property(GTK_ACCESSIBLE(save_as_button),
            GTK_ACCESSIBLE_PROPERTY_LABEL, _("Save As"), -1);
    }

    g_object_set(text_view, "editable", TRUE, "cursor-visible", TRUE, NULL);
    g_signal_connect(text_view, "map", G_CALLBACK(window_lifecycle_on_map), text_view);

    /* Window map signal - may need adjustment for tabs */
    /* g_signal_connect(window, "map", G_CALLBACK(window_lifecycle_on_window_map), NULL); */

    /* Check for autorecover on startup */
    dialogs_check_for_autorecover(app);

    gtk_window_present(GTK_WINDOW(window));
    g_object_unref(builder);
}

void app_initialization_open(GApplication *application, GFile **files, gint n_files,
                            const gchar *hint)
{
    (void)hint;
    /* First activate the application to ensure window is created */
    app_initialization_activate(application);

    if (n_files > 0) {
        /* Open the first file (ignore additional files for now) */
        GFile *file = files[0];
        g_autofree gchar *path = g_file_get_path(file);
        if (path) {
            g_debug("Opening file from command line: %s", path);
            GtkApplication *app = GTK_APPLICATION(application);

            /* Use TabManager to open files instead of global DocumentManager */
            TabManager *tm = g_object_get_data(G_OBJECT(app), "tab_manager");
            if (tm) {
                AdwTabPage *page = tab_manager_open_file(tm, path);
                if (!page) {
                    g_warning("Failed to open file from command line: %s", path);
                }
            } else {
                g_warning("TabManager not available for opening command line file: %s", path);
            }
        }
    }
}