# [1.0.1] - 2025-09-16 - src/meson.build
# Changed: Restructured to use modular organization with subprojects

# Include subdirectories
subdir('app')
subdir('ui')
subdir('document')
subdir('render')
subdir('core')
subdir('components')
subdir('editor')

# Collect all sources from modules
sources = [
  'main.c'  # Temporary - will be moved to app/
]

# Add sources from each module
sources += ui_lib_sources
sources += document_lib_sources
sources += render_lib_sources
sources += core_lib_sources
sources += components_lib_sources
sources += editor_lib_sources

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
