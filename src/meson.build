# [0.2.0] - 2025-09-15 - src/meson.build
# Changed: Added version header tracking.

sources = [
  'main.c',
  'cmrender.c',
  'toolbar.c',
  'settings.c',
  'util.c'
]

# Build dependencies list
deps = [gtk4_dep, adwaita_dep, cmark_dep]
if soup_dep.found()
  deps += soup_dep
endif

# Main executable
executable('gtktext',
  sources,
  dependencies: deps,
  include_directories: [project_inc, config_inc],
  install: true,
  install_dir: get_option('bindir')
)
