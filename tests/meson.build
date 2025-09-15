# [0.2.0] - 2025-09-15 - tests/meson.build
# Added: Test configuration for Meson build system.

# Build dependencies for tests (same as main app except main.o)
test_deps = [gtk4_dep, adwaita_dep, cmark_dep]
if soup_dep.found()
  test_deps += soup_dep
endif

# Source files for library (all except main.c)
lib_sources = [
  '../src/cmrender.c',
  '../src/toolbar.c',
  '../src/settings.c'
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
