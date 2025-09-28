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
  '../src/render/markdown/cmrender_export.c',
  '../src/render/markdown/tag_manager.c',
  '../src/render/markdown/markdown_engine.c',
  '../src/render/visual/theme_styles.c',
  '../src/render/visual/hr_widget.c',
  '../src/render/safe_helpers.c',
  '../src/render/images/image_widget.c',
  '../src/render/images/http_images.c',
  '../src/components/toolbar/toolbar.c',
  '../src/core/settings.c',
  '../src/core/settings_manager.c',
  '../src/core/signal_manager.c',
  '../src/core/util.c',
  '../src/core/app_initialization.c',
  '../src/core/window_size_manager.c',
  '../src/core/window_lifecycle.c',
  '../src/document/document.c',
  '../src/document/document_manager.c',
  '../src/document/atomic_io.c',
  '../src/document/fs_utils.c',
  '../src/document/version_history.c',
  '../src/document/recovery_drafts.c',
  '../src/document/external_changes.c',
  '../src/document/doc_state.c',
  '../src/ui/actions/file_actions.c',
  '../src/ui/actions/app_actions.c',
  '../src/ui/dialogs/dialogs.c',
  '../src/ui/image_embedder.c',
  '../src/ui/tab_document.c',
  '../src/ui/tab_manager.c',
  '../src/ui/tab_integration.c',
  '../src/ui/status_manager.c',
  '../src/ui/event_handlers.c',
  '../src/ui/text_view_interactions.c',
  '../src/editor/buffer_manager.c',
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
  'test_lists.c',
  'test_async_document.c',
  'test_doc_state.c',
  'test_export.c'
]

# Set up test environment with GSettings schema directory
test_env = environment()
test_env.set('GSETTINGS_SCHEMA_DIR', meson.current_source_dir() + '/../data')
# Enforce Wayland-only policy as per CLAUDE.md guidelines
test_env.set('GDK_BACKEND', 'wayland')
test_env.set('GSETTINGS_BACKEND', 'memory')

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
