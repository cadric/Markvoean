/* [0.1.0] - 2025-09-28 - document_portal.h
 * Purpose: Helpers for interacting with the sandbox document portal (org.freedesktop.portal.Documents)
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* Export a path via the document portal and return a document:// URI when successful.
 * Returns NULL (with optional warning) when the portal is unavailable or fails.
 */
gchar *document_portal_export_path(const gchar *path, GError **error);

/* Convenience predicate to check if the runtime appears to be sandboxed.
 * Caller may use this to skip portal calls when running outside a sandbox.
 */
gboolean document_portal_is_sandboxed(void);

G_END_DECLS

