# [0.2.0] - 2025-09-15 - data/meson.build
# Changed: Added version header tracking and i18n desktop file support.

# GSettings schema
install_data('org.gtk.gtktext.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

# Note: Schema compilation should be done by the package manager or during system install
# For development, manually run: glib-compile-schemas ./data

# Desktop file with i18n support
desktop_file = i18n.merge_file(
  input: 'gtktext.desktop.in',
  output: 'gtktext.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
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
