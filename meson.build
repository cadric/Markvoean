# [0.3.0] - 2025-09-15 - meson.build
# Added: UI compliance, accessibility, i18n, and GObject types

project('gtktext', 'c',
  version: '0.3.0',
  meson_version: '>= 1.7.2',
  default_options: [
    'c_std=c17',
    'warning_level=2',
    'werror=false',
    'optimization=2',
    'b_pie=true',
    'b_lto=false',
    'buildtype=release'
  ]
)

# Add project-wide compiler arguments for hardening
add_project_arguments([
  '-DHAVE_CONFIG_H',
  '-D_FORTIFY_SOURCE=2',
  '-fstack-protector-strong'
], language: 'c')

# Add project-wide linker arguments for hardening
add_project_link_arguments([
  '-Wl,-z,relro',
  '-Wl,-z,now',
  '-pie'
], language: 'c')

# i18n module for internationalization
i18n = import('i18n')

# Dependencies
gtk4_dep = dependency('gtk4')
adwaita_dep = dependency('libadwaita-1')
cmark_dep = dependency('libcmark')

# Optional libsoup-3.0 for HTTP(S) image loading
soup_dep = dependency('libsoup-3.0', required: false)
if soup_dep.found()
  add_project_arguments('-DHAVE_LIBSOUP=1', language: 'c')
  message('libsoup-3.0 found; HTTP image loading enabled')
else
  message('libsoup-3.0 not found; HTTP image loading disabled')
endif

# Configuration data
conf_data = configuration_data()
conf_data.set_quoted('PKGDATADIR', get_option('prefix') / get_option('datadir') / 'gtktext')
conf_data.set_quoted('GETTEXT_PACKAGE', 'gtktext')
conf_data.set_quoted('LOCALEDIR', get_option('prefix') / get_option('datadir') / 'locale')

configure_file(
  output: 'config.h',
  configuration: conf_data
)

# Add configuration include directory
config_inc = include_directories('.')

# Project include directories
project_inc = include_directories('include')

# Subdirectories
subdir('src')
subdir('data')
subdir('ui')
subdir('po')
subdir('tests')

# Summary
summary({
  'GTK4': gtk4_dep.version(),
  'libadwaita': adwaita_dep.version(),
  'libcmark': cmark_dep.version(),
  'libsoup-3.0': soup_dep.found() ? soup_dep.version() : 'disabled'
}, section: 'Dependencies')

summary({
  'prefix': get_option('prefix'),
  'bindir': get_option('bindir'),
  'datadir': get_option('datadir'),
}, section: 'Directories')
