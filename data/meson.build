# [0.2.0] - 2025-09-15 - data/meson.build
# Changed: Added version header tracking.

# GSettings schema
install_data('org.gtk.gtktext.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

# Note: Schema compilation should be done by the package manager or during system install
# For development, manually run: glib-compile-schemas ./data

# Desktop file
install_data('gtktext.desktop',
  install_dir: get_option('datadir') / 'applications'
)

# Icons
install_subdir('icons',
  install_dir: get_option('datadir') / 'gtktext'
)

# Application icon
install_data('icons/hicolor/scalable/apps/gtktext.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps'
)
