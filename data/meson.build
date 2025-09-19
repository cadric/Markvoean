# [1.3.3] - 2025-09-18 - data/meson.build
# Changed: Added AppStream metainfo.xml for GNOME Software compliance

# GSettings schema
install_data('org.gtk.gtktext.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

# AppStream metainfo for software centers
metainfo_file = 'org.gtk.gtktext.metainfo.xml'
install_data(metainfo_file,
  install_dir: get_option('datadir') / 'metainfo'
)

# Validate AppStream metainfo if appstreamcli is available
appstreamcli = find_program('appstreamcli', required: false)
if appstreamcli.found()
  test('validate-metainfo',
       appstreamcli,
       args: ['validate', '--no-net', files(metainfo_file)],
       suite: 'data')
endif

# Note: Schema compilation should be done by the package manager or during system install
# For development, manually run: glib-compile-schemas ./data

# Desktop file with i18n support
desktop_file = i18n.merge_file(
  input: 'org.gtk.gtktext.desktop.in',
  output: 'org.gtk.gtktext.desktop',
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: get_option('datadir') / 'applications'
)

# Validate desktop file if desktop-file-validate is available
desktop_file_validate = find_program('desktop-file-validate', required: false)
if desktop_file_validate.found()
  test('validate-desktop-file',
       desktop_file_validate,
       args: [desktop_file],
       suite: 'data')
endif

# Application icons - scalable and symbolic variants
install_data('icons/hicolor/scalable/apps/gtktext.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps'
)
install_data('icons/hicolor/scalable/apps/org.gtk.gtktext.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps'
)
install_data('icons/hicolor/symbolic/apps/org.gtk.gtktext-symbolic.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'symbolic' / 'apps'
)
