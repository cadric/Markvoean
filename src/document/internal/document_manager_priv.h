/* C ULTRA‑MIN TEMPLATE
   Purpose: Private DocumentManager struct definition (internal fields)
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#pragma once

#include <gtktext/document/document_manager.h>
#include <gtktext/document/doc_state.h>
#include <gio/gio.h>

/* GObject instance structure with internal fields */
struct _GtktextDocumentManager {
  GObject parent_instance;
  /* Core references */
  GtkTextBuffer *buffer;           /* ref - the text buffer */
  GtkWindow     *window;           /* ref - parent window */

  /* State */
  DocState  doc_state;             /* New hash-based state tracking */
  gchar    *file_path;             /* owned - current file location */
  gchar    *draft_path;            /* owned - draft location for untitled */
  gchar    *recovery_path;         /* owned - crash recovery journal */
  gint64    last_mtime;            /* last known modification time */
  gchar    *last_hash;             /* owned - content hash for conflict detection */
  gboolean  is_untitled;
  gboolean  initialization_complete; /* prevents premature dirty state during setup */

  /* Timers and monitoring */
  guint        autosave_id;        /* autosave timer source ID */
  guint        recovery_id;        /* recovery snapshot timer source ID */
  guint        debounce_id;        /* buffer change debounce timer source ID */
  gboolean     autosave_in_progress; /* flag to prevent concurrent autosave operations */
  gboolean     recovery_write_in_progress; /* async recovery snapshot in flight */
  gchar       *document_portal_uri;    /* portal-exported URI (document://) */
  gchar       *document_handle;    /* portal handle for persistence */
  GFileMonitor *file_monitor;      /* ref - file change monitor */
  GSettings    *settings;          /* ref - app settings for autosave control */

  /* Callbacks */
  StateChangeCallback state_callback;
  gpointer            state_callback_data;

  /* Buffer change tracking */
  gulong  buffer_changed_handler_id;
  gchar  *original_content;        /* owned - content at last save */
};
