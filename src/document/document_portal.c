/* [0.1.0] - 2025-09-28 - document_portal.c
 * Purpose: Sandbox-friendly helpers for the org.freedesktop.portal.Documents interface
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <gtktext/document/document_portal.h>

#define PORTAL_BUS_NAME       "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH    "/org/freedesktop/portal/documents"
#define PORTAL_INTERFACE_NAME "org.freedesktop.portal.Documents"

static gboolean
portal_call_available(void)
{
    /* Flatpak and other sandboxed environments set either FLATPAK_ID or /.flatpak-info. */
    if (g_getenv("FLATPAK_ID") != NULL || g_file_test("/.flatpak-info", G_FILE_TEST_EXISTS)) {
        return TRUE;
    }

    /* GTK_USE_PORTAL is also exported when portal access is desired. */
    if (g_getenv("GTK_USE_PORTAL") != NULL) {
        return TRUE;
    }

    return FALSE;
}

gboolean
document_portal_is_sandboxed(void)
{
    return portal_call_available();
}

static const gchar *
resolve_app_id(void)
{
    const gchar *app_id = NULL;
    GApplication *app = g_application_get_default();
    if (G_IS_APPLICATION(app)) {
        app_id = g_application_get_application_id(app);
    }
    if (!app_id || *app_id == '\0') {
        /* Fall back to the well-known desktop ID. */
        app_id = "org.gtk.gtktext";
    }
    return app_id;
}

gchar *
document_portal_export_path(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, NULL);

    if (!portal_call_available()) {
        return NULL;
    }

    g_autoptr(GDBusConnection) connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, error);
    if (!connection) {
        return NULL;
    }

    GVariantBuilder options_builder;
    g_variant_builder_init(&options_builder, G_VARIANT_TYPE_VARDICT);

    g_autoptr(GVariant) options_variant = g_variant_builder_end(&options_builder);

    const gchar *app_id = resolve_app_id();

    g_autoptr(GVariant) parameters = g_variant_new("(ss@a{sv})",
                                                   path,
                                                   app_id,
                                                   options_variant);

    g_autoptr(GVariant) result = g_dbus_connection_call_sync(connection,
                                                             PORTAL_BUS_NAME,
                                                             PORTAL_OBJECT_PATH,
                                                             PORTAL_INTERFACE_NAME,
                                                             "AddFullPath",
                                                             parameters,
                                                             G_VARIANT_TYPE("(s)"),
                                                             G_DBUS_CALL_FLAGS_NONE,
                                                             -1,
                                                             NULL,
                                                             error);
    if (!result) {
        /* Gracefully degrade if the portal is unavailable */
        if (error && *error) {
            /* Service unknown is expected outside sandbox; convert to debug logging */
            if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_DBUS_ERROR)) {
                g_debug("Document portal call failed for %s: %s", path, (*error)->message);
                g_clear_error(error);
            }
        }
        return NULL;
    }

    const gchar *document_id = NULL;
    g_variant_get(result, "(&s)", &document_id);
    if (!document_id || *document_id == '\0') {
        return NULL;
    }

    return g_strdup_printf("document://%s", document_id);
}
