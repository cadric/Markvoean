/* C ULTRA‑MIN TEMPLATE
   Purpose: Private recovery metadata types and helpers shared within document modules
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#pragma once

#include <glib.h>

/* Recovery metadata parsed from .recovery files */
typedef struct {
  gchar   *original_path;
  gint64   timestamp;
  gchar   *content;
  gboolean is_untitled;
  gchar   *draft_path;
} RecoveryInfo;

/* Free function for RecoveryInfo */
void recovery_info_free(RecoveryInfo *info);

/* g_autoptr support for RecoveryInfo */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(RecoveryInfo, recovery_info_free)

/* Parse a .recovery file into a new RecoveryInfo (caller owns) */
RecoveryInfo *document_recovery_parse_file(const gchar *recovery_path, GError **error);

/* Periodic cleanup helper exported by recovery_drafts.c */
void cleanup_old_recovery_files(void);

