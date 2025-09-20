# [1.0.1] - 2025-09-16 - src/render/meson.build
# Changed: Created meson build file for rendering modules

render_sources = []

# Markdown rendering modules
render_sources += files(
  'markdown/cmrender.c',
  'markdown/tag_manager.c',
  'markdown/markdown_engine.c'
)

# Image rendering modules
render_sources += files(
  'images/image-widget.c',
  'images/http_images.c'
)

# Visual rendering modules
render_sources += files(
  'visual/theme_styles.c',
  'visual/hr_widget.c'
)

# Safe rendering helpers - Phase 2 enhancement
render_sources += files(
  'safe_helpers.c'
)

# Export sources for parent build
render_lib_sources = render_sources