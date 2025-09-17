# [1.0.1] - 2025-09-16 - src/ui/meson.build
# Changed: Created meson build file for UI modules

ui_sources = []

# Add action modules
ui_sources += files(
  'actions/file_actions.c',
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

# Add welcome screen module
ui_sources += files(
  'welcome_screen.c'
)

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


# Export sources for parent build
ui_lib_sources = ui_sources