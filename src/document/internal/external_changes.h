/* C ULTRA‑MIN TEMPLATE
   Purpose: Private APIs for external change detection and conflict handling
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#pragma once

#include <gtktext/document/document_manager.h>

/* Setup/teardown */
void document_external_setup_file_monitor(DocumentManager *dm);

/* Detection and metadata */
gboolean document_external_has_changed(DocumentManager *dm);
gchar *document_external_load_content(DocumentManager *dm, GError **error);
void document_external_update_metadata(DocumentManager *dm);

/* UI */
void document_external_show_change_dialog(DocumentManager *dm);

