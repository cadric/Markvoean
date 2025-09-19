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

# Custom toolbar icons - symbolic variants for toolbar formatting buttons
install_data('icons/hicolor/symbolic/apps/horizontal-rule-symbolic.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'symbolic' / 'apps'
)
install_data('icons/hicolor/symbolic/apps/inline-code-symbolic.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'symbolic' / 'apps'
)
install_data('icons/hicolor/symbolic/apps/code-block-symbolic.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'symbolic' / 'apps'
)
install_data('icons/hicolor/symbolic/apps/block-quote-symbolic.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'symbolic' / 'apps'
)

# Development icon cache update - copy icons to local cache during build for testing
# Find icon cache update tools
gtk4_update_icon_cache = find_program('gtk4-update-icon-cache', required: false)
gtk_update_icon_cache = find_program('gtk-update-icon-cache', required: false)

# Create custom target to update local icon cache during development
if gtk4_update_icon_cache.found() or gtk_update_icon_cache.found()
  update_dev_icons = custom_target('update-dev-icons',
    input: [
      'icons/hicolor/symbolic/apps/horizontal-rule-symbolic.svg',
      'icons/hicolor/symbolic/apps/inline-code-symbolic.svg',
      'icons/hicolor/symbolic/apps/code-block-symbolic.svg',
      'icons/hicolor/symbolic/apps/block-quote-symbolic.svg'
    ],
    output: 'dev-icons-updated',
    command: [
      'sh', '-c', '''
      mkdir -p "$HOME/.local/share/icons/hicolor/symbolic/apps"
      cp "@INPUT0@" "@INPUT1@" "@INPUT2@" "@INPUT3@" "$HOME/.local/share/icons/hicolor/symbolic/apps/"
      ''' + (gtk4_update_icon_cache.found() ?
        'gtk4-update-icon-cache "$HOME/.local/share/icons/hicolor/" 2>/dev/null || true' :
        'gtk-update-icon-cache "$HOME/.local/share/icons/hicolor/" 2>/dev/null || true') + '''
      touch "@OUTPUT@"
      '''
    ],
    build_by_default: true
  )
endif
