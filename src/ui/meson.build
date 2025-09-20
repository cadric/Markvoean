# [1.0.1] - 2025-09-16 - src/ui/meson.build
# Changed: Created meson build file for UI modules

ui_sources = []

# Add action modules
ui_sources += files(
  'actions/file_actions.c',
  'actions/edit_actions.c',
  'actions/app_actions.c'
)

# Add dialog modules
ui_sources += files(
  'dialogs/dialogs.c'
)

# Add image embedding module
ui_sources += files(
  'image_embedder.c'
)

# Welcome screen now implemented in tab system

# Add text view interactions module
ui_sources += files(
  'text_view_interactions.c'
)

# Add status management module
ui_sources += files(
  'status_manager.c'
)

# Add event handlers module
ui_sources += files(
  'event_handlers.c'
)

# Add tab management modules
ui_sources += files(
  'tab_manager.c',
  'tab_document.c',
  'tab_integration.c'
)


# Export sources for parent build
ui_lib_sources = ui_sources