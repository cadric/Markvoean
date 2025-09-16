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

# Add window modules (will be created)
# ui_sources += files(
#   'window/main_window.c',
#   'window/welcome_screen.c',
#   'window/status_bar.c'
# )

# Add editor modules (will be created)
# ui_sources += files(
#   'editor/text_view.c',
#   'editor/text_events.c',
#   'editor/overlays.c'
# )

# Export sources for parent build
ui_lib_sources = ui_sources