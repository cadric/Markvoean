/* C ULTRA-MIN TEMPLATE
   Purpose: Main application entry point and UI coordination for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • LIFECYCLE
   [1.0.8] - 2025-09-16 - main.c
   Changed: Finalized modularization - reduced from 2829 to 149 lines (95% reduction)
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <adwaita.h>
#include <locale.h>

#include <gtktext/core/settings_manager.h>
#include <gtktext/core/window_lifecycle.h>
#include <gtktext/core/signal_manager.h>
#include <gtktext/ui/file_actions.h>
#include <gtktext/ui/edit_actions.h>
#include <gtktext/ui/app_actions.h>
#include <gtktext/ui/tab_integration.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * STATE - Application subsystem managers
 * ═══════════════════════════════════════════════════════════════════════════════ */

static SignalManager *global_signal_manager = NULL;

/* Public accessor for signal manager - exported for modules */
SignalManager *gtktext_get_signal_manager(void)
{
    return global_signal_manager;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * LIFECYCLE - Application initialization, activation, shutdown
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Application lifecycle is now handled by specialized modules */

int main(int argc, char *argv[])
{
    /* Optional flag: --debug enables verbose logging */
    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--debug") == 0) {
            g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
            break;
        }
    }

    /* Enforce Wayland-only policy as per CLAUDE.md guidelines */
    const char *current_backend = g_getenv("GDK_BACKEND");
    if (!current_backend) {
        g_setenv("GDK_BACKEND", "wayland", TRUE);
        g_message("Enforcing Wayland-only policy: set GDK_BACKEND=wayland");
    } else if (g_strcmp0(current_backend, "x11") == 0) {
        g_warning("X11 backend detected but IFG follows Wayland-only policy");
        g_warning("Consider running with: GDK_BACKEND=wayland %s", argv[0]);
        /* Override X11 with Wayland for compliance */
        g_setenv("GDK_BACKEND", "wayland", TRUE);
        g_message("Overriding X11 backend with Wayland for policy compliance");
    } else if (g_strcmp0(current_backend, "wayland") == 0) {
        g_debug("Wayland backend active - policy compliant");
    } else {
        g_message("Using backend '%s' - Wayland preferred per policy", current_backend);
    }

    /* Initialize subsystems with proper encapsulation */
    global_signal_manager = signal_manager_create();
    if (!global_signal_manager) {
        g_critical("Failed to initialize signal manager");
        return 1;
    }

    /* In dev runs, locate local GSettings schemas automatically */
    settings_manager_setup_gsettings_schemas();
    
    /* Initialize i18n */
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    /* Hint GTK to use the desktop portal for native dialogs if available */
    if (!g_getenv("GTK_USE_PORTAL")) {
        g_setenv("GTK_USE_PORTAL", "1", FALSE);
        g_debug("Set GTK_USE_PORTAL=1 for file dialogs");
    }

    g_autoptr(AdwApplication) app = NULL;
    int status;

    app = adw_application_new("org.gtk.gtktext", G_APPLICATION_HANDLES_OPEN);

  const GActionEntry app_actions[] = {
        { "new-tab", tab_integration_new_tab_action, NULL, NULL, NULL, {0} },
        { "close-tab", tab_integration_close_tab_action, NULL, NULL, NULL, {0} },
        { "open", file_action_open_cb, NULL, NULL, NULL, {0} },
        { "save", file_action_save_cb, NULL, NULL, NULL, {0} },
        { "save-as", file_action_save_as_cb, NULL, NULL, NULL, {0} },
        { "undo", edit_action_undo_cb, NULL, NULL, NULL, {0} },
        { "redo", edit_action_redo_cb, NULL, NULL, NULL, {0} },
        { "version-history", edit_action_version_history_cb, NULL, NULL, NULL, {0} },
        { "save-version", edit_action_save_version_cb, NULL, NULL, NULL, {0} },
        { "format-bold", edit_action_format_bold_cb, NULL, NULL, NULL, {0} },
        { "format-italic", edit_action_format_italic_cb, NULL, NULL, NULL, {0} },
        { "cut", edit_action_cut_cb, NULL, NULL, NULL, {0} },
        { "copy", edit_action_copy_cb, NULL, NULL, NULL, {0} },
        { "paste", edit_action_paste_cb, NULL, NULL, NULL, {0} },
        { "select-all", edit_action_select_all_cb, NULL, NULL, NULL, {0} },
        { "print", app_action_print_cb, NULL, NULL, NULL, {0} },
        { "preferences", app_action_preferences_cb, NULL, NULL, NULL, {0} },
        { "about", app_action_about_cb, NULL, NULL, NULL, {0} },
        { "shortcuts", app_action_shortcuts_cb, NULL, NULL, NULL, {0} },
        { "quit", app_action_quit_cb, NULL, NULL, NULL, {0} },
    };

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, 
                                   G_N_ELEMENTS(app_actions), app);

  // Debug: To inspect actions at runtime, run with GTK_DEBUG=actions
  // Tab shortcuts (HIG compliant)
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.new-tab", (const char*[]){ "<primary>t", "<primary>n", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.close-tab", (const char*[]){ "<primary>w", NULL });
  // File shortcuts
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.open", (const char*[]){ "<primary>o", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.save", (const char*[]){ "<primary>s", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.save-as", (const char*[]){ "<primary><shift>s", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.format-bold", (const char*[]){ "<primary>b", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.format-italic", (const char*[]){ "<primary>i", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.print", (const char*[]){ "<primary>p", NULL });
  // Edit shortcuts
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.undo", (const char*[]){ "<primary>z", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.redo", (const char*[]){ "<primary><shift>z", "<primary>y", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.cut", (const char*[]){ "<primary>x", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.copy", (const char*[]){ "<primary>c", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.paste", (const char*[]){ "<primary>v", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.select-all", (const char*[]){ "<primary>a", NULL });
  // App shortcuts
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.preferences", (const char*[]){ "<primary>comma", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.shortcuts", (const char*[]){ "<primary>question", "F1", NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.about", (const char*[]){ NULL });
  gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", (const char*[]){ "<primary>q", NULL });

  g_signal_connect (app, "activate", G_CALLBACK (window_lifecycle_app_activate), NULL);
  g_signal_connect (app, "open", G_CALLBACK (window_lifecycle_app_open), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);

  /* Cleanup subsystems on exit */
  signal_manager_destroy(global_signal_manager);
  global_signal_manager = NULL;

  return status;
}
