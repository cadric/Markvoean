/* C ULTRA-MIN TEMPLATE
   Purpose: Atomic file write helpers used by DocumentManager and UI
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [2.5.10] - 2025-09-28 - document/atomic_io.c
   Added: Extracted atomic write helpers from document_manager.c to reduce file size/cohesion
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <gtktext/render/cmrender.h>
#include <gtktext/document/document_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * ATOMIC WRITE IMPLEMENTATION - Robust file writing
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean atomic_write_file(const gchar *path, const gchar *content,
                           gsize length, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);

    g_autofree gchar *dir = g_path_get_dirname(path);
    g_autofree gchar *basename = g_path_get_basename(path);
    g_autofree gchar *tmp_template = g_strdup_printf(".%s.tmp.XXXXXX", basename);
    g_autofree gchar *tmp_path = g_build_filename(dir, tmp_template, NULL);

    gint fd = g_mkstemp(tmp_path);
    if (fd == -1) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "Failed to create temporary file: %s", g_strerror(errno));
        return FALSE;
    }

    gsize written = 0;
    while (written < length) {
        gssize result = write(fd, content + written, length - written);
        if (result < 0) {
            if (errno == EINTR) continue;
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                       "Write failed: %s", g_strerror(errno));
            close(fd);
            g_unlink(tmp_path);
            return FALSE;
        }
        written += result;
    }

    if (fsync(fd) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "fsync failed: %s", g_strerror(errno));
        close(fd);
        g_unlink(tmp_path);
        return FALSE;
    }
    close(fd);

#ifdef G_OS_WIN32
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        if (!ReplaceFile(path, tmp_path, NULL, 0, NULL, NULL)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "ReplaceFile failed: %lu", GetLastError());
            g_unlink(tmp_path);
            return FALSE;
        }
    } else {
        if (!MoveFile(tmp_path, path)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "MoveFile failed: %lu", GetLastError());
            g_unlink(tmp_path);
            return FALSE;
        }
    }
#else
    if (rename(tmp_path, path) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                   "rename failed: %s", g_strerror(errno));
        g_unlink(tmp_path);
        return FALSE;
    }
#endif

    g_debug("Atomic write successful: %s", path);
    return TRUE;
}

gboolean atomic_write_file_from_buffer(const gchar *path, GtkTextBuffer *buffer,
                                       GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), FALSE);

    /* Export buffer content to markdown directly via renderer */
    g_autofree gchar *content = cm_render_buffer_to_markdown(buffer);
    if (!content) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to convert buffer content to markdown");
        return FALSE;
    }

    return atomic_write_file(path, content, strlen(content), error);
}

