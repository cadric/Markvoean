# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2025-09-15

### Added
- Meson build system migration from Make
- Full Meson configuration with all build targets
- Test environment configuration for GSettings schema
- Config.h generation via Meson
- Build system dependency validation and summary
- Version headers to all source files for tracking changes
- Proper input validation with g_return_if_fail() to all public APIs
- Performance optimizations in cmrender.c with GQuark caching
- Tag lookup caching system for improved rendering performance
- Pre-sized dynamic arrays to reduce memory allocations
- CLAUDE.md Ultra-Min C Module Template implementation (src/util.c)
- Proper include/ directory structure for public APIs (include/gtktext/)
- Application icon integration using favicon.svg
- Comprehensive Wayland-only policy enforcement

### Changed
- Migrated from Make to Meson 1.7.2 build system
- Updated build configuration to use Meson's native dependency management
- Enhanced hardening flags integration through Meson options
- Improved test execution with proper environment setup
- Optimized build configuration to use Meson's warning_level instead of manual flags
- File dialog implementation with better error handling
- Memory management patterns with more consistent GLib usage
- Reorganized header files to follow GNOME standards (include/gtktext/ structure)
- Updated all source files to use standardized include paths
- Enhanced input validation across all public API functions

### Removed
- Makefile and Make build system (fully replaced by Meson)
- Make-generated obj/ and bin/ directories
- Legacy build artifacts and temporary files
- Duplicate warning flags in favor of Meson's warning_level

### Technical Details
- Requires Meson >= 1.7.2 and Ninja >= 1.12.1
- All dependencies properly detected: GTK4, libadwaita-1, libcmark, libsoup-3.0
- Test suite fully functional with proper GSettings schema resolution
- Installation targets preserved: binary, desktop file, icons, UI files, GSettings schema
- Enhanced memory safety with automatic cleanup patterns
- Improved rendering performance through optimized tag operations

## [0.1.0] - Previous releases

### Added
- Initial application development with Make build system
- GTK4 and libadwaita integration
- CommonMark rendering engine
- GSettings configuration system
- Test suite implementation
- Basic Markdown editing functionality
- Image embedding support with HTTP(S) loading
- Syntax highlighting for code blocks
- Auto-save functionality
- Zoom controls and keyboard shortcuts