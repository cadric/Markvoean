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
#include <gtktext/ui/welcome_screen.h>
#include <gtktext/ui/text_view_interactions.h>
#include <gtktext/ui/status_manager.h>
#include <gtktext/ui/event_handlers.h>
#include <gtktext/render/markdown/markdown_engine.h>
#include <gtktext/editor/buffer_manager.h>
#include <gtktext/core/signal_manager.h>

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

    GtkWidget *text_view = GTK_WIDGET(gtk_builder_get_object(builder, "text_view"));
    if (!text_view) {
        g_critical("Failed to get text_view from UI");
        g_object_unref(builder);
        return;
    }

    /* Get status bar widgets */
    GtkWidget *save_status = GTK_WIDGET(gtk_builder_get_object(builder, "save_status"));
    GtkWidget *file_location = GTK_WIDGET(gtk_builder_get_object(builder, "file_location"));
    GtkWidget *main_stack = GTK_WIDGET(gtk_builder_get_object(builder, "main_stack"));
    GtkWidget *welcome_open_button = GTK_WIDGET(gtk_builder_get_object(builder, "welcome_open_button"));
    GtkWidget *welcome_new_button = GTK_WIDGET(gtk_builder_get_object(builder, "welcome_new_button"));

    if (!save_status || !file_location || !main_stack || !welcome_open_button || !welcome_new_button) {
        g_critical("Failed to get required UI components");
        g_object_unref(builder);
        return;
    }

    /* Expose widgets to application scope for actions to use */
    g_object_set_data(G_OBJECT(app), "text_view", text_view);
    g_object_set_data(G_OBJECT(app), "main_stack", main_stack);
    g_object_set_data(G_OBJECT(app), "welcome_open_button", welcome_open_button);
    g_object_set_data(G_OBJECT(app), "welcome_new_button", welcome_new_button);
    g_object_set_data(G_OBJECT(app), "save_status", save_status);
    g_object_set_data(G_OBJECT(app), "file_location", file_location);

    /* Initialize status bar with default values */
    status_manager_update_save_status(app, _("Ready"));
    status_manager_update_file_location(app, NULL);

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

    /* Register DocumentManager callbacks */
    document_manager_set_state_callback(doc_manager, event_handlers_on_document_state_changed, app);

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

    /* Create toolbar and add to container */
    GtkWidget *toolbar_container = GTK_WIDGET(gtk_builder_get_object(builder,
                                                                    "toolbar_container"));
    if (!toolbar_container) {
        g_critical("Failed to get toolbar_container from UI");
        g_object_unref(builder);
        return;
    }

    /* Add CSS styling to toolbar */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".toolbar { background-color: @theme_bg_color; border-bottom: 1px solid @borders; "
        "padding: 8px; margin: 4px; }"
        ".toolbar button { padding: 4px 8px; min-height: 24px; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* Store builder reference with window */
    g_object_set_data_full(G_OBJECT(window), "builder", g_object_ref(builder),
                          g_object_unref);

    /* Create toolbar and add to UI */
    GtkWidget *toolbar = create_toolbar(text_view);

    /* Ensure toolbar is visible and correctly added */
    if (toolbar && toolbar_container) {
        gtk_widget_set_visible(toolbar_container, TRUE);
        g_message("Toolbar is visible and active");
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

    /* Connect welcome screen buttons */
    g_signal_connect(welcome_open_button, "clicked", G_CALLBACK(welcome_screen_open_cb), app);
    g_signal_connect(welcome_new_button, "clicked", G_CALLBACK(welcome_screen_new_cb), app);

    /* Store main_stack reference for setting visibility after window is shown */
    g_object_set_data(G_OBJECT(window), "main_stack", main_stack);

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
    GtkWidget *open_button = GTK_WIDGET(gtk_builder_get_object(builder, "open_button"));
    GtkWidget *save_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_button"));
    GtkWidget *save_as_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_as_button"));
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

    /* Connect window map signal to set welcome screen after proper initialization */
    g_signal_connect(window, "map", G_CALLBACK(window_lifecycle_on_window_map), NULL);

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
            DocumentManager *dm = g_object_get_data(G_OBJECT(app), "doc_manager");
            if (dm) {
                GError *error = NULL;
                if (!document_manager_open_file(dm, path, &error)) {
                    g_warning("Failed to open file from command line: %s",
                             error ? error->message : "Unknown error");
                    g_clear_error(&error);
                }
            }
        }
    }
}