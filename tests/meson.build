# [0.2.0] - 2025-09-15 - tests/meson.build
# Changed: Added version header tracking.

# Build dependencies for tests (same as main app except main.o)
test_deps = [gtk4_dep, adwaita_dep, cmark_dep]
if soup_dep.found()
  test_deps += soup_dep
endif

# Source files for library (all except main.c) - using new module structure
lib_sources = [
  '../src/render/markdown/cmrender.c',
  '../src/render/markdown/tag_manager.c',
  '../src/render/visual/theme_styles.c',
  '../src/components/toolbar/toolbar.c',
  '../src/core/settings.c',
  '../src/core/util.c',
  '../src/document/document.c',
  '../src/document/document_manager.c',
  '../src/render/images/image-widget.c',
  '../src/ui/actions/file_actions.c',
  '../src/ui/actions/app_actions.c',
  '../src/ui/dialogs/dialogs.c',
  '../src/ui/image_embedder.c',
  '../src/ui/welcome_screen.c'
]

# Build a static library for testing (excludes main.c)
gtktext_lib = static_library('gtktext_test',
  lib_sources,
  dependencies: test_deps,
  include_directories: [project_inc, config_inc],
  build_by_default: false
)

# Test sources
test_sources = [
  'test_lists.c'
]

# Set up test environment with GSettings schema directory
test_env = environment()
test_env.set('GSETTINGS_SCHEMA_DIR', meson.current_source_dir() + '/../data')
# Enforce Wayland-only policy as per CLAUDE.md guidelines
test_env.set('GDK_BACKEND', 'wayland')

# Create test executables
foreach test_src : test_sources
  test_name = test_src.split('.')[0]
  test_exe = executable(test_name,
    test_src,
    dependencies: test_deps,
    include_directories: [project_inc, config_inc],
    link_with: gtktext_lib,
    build_by_default: false
  )
  test(test_name, test_exe, env: test_env)
endforeach
