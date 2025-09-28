# [1.0.1] - 2025-09-16 - src/document/meson.build
# Changed: Created meson build file for document management modules

document_sources = []

# Document management modules
document_sources += files(
  'document.c',
  'document_manager.c',
  'document_portal.c',
  'fs_utils.c',
  'atomic_io.c',
  'recovery_drafts.c',
  'external_changes.c',
  'version_history.c',
  'doc_state.c'
)

# Export sources for parent build
document_lib_sources = document_sources
