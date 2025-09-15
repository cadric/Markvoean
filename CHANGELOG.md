# Chang## [0.2.0] - 2025-09-15

### Added
- Meson build system migration from Make
- Full Meson configuration with all build targets
- Test environment configuration for GSettings schema
- Config.h generation via Meson
- Build system dependency validation and summary
- Version headers to all source files for tracking changes
- Proper input validation with g_return_if_fail() to all public APIs

### Changed
- Migrated from Make to Meson 1.7.2 build system
- Updated build configuration to use Meson's native dependency management
- Enhanced hardening flags integration through Meson options
- Improved test execution with proper environment setup
- Optimized build configuration to use Meson's warning_level instead of manual flags

### Removed
- Makefile and Make build system (fully replaced by Meson)
- Make-generated obj/ and bin/ directories
- Legacy build artifacts and temporary files
- Duplicate warning flags in favor of Meson's warning_level

### Technical Details
- Requires Meson >= 1.7.2 and Ninja >= 1.12.1
- All dependencies properly detected: GTK4, libadwaita-1, libcmark, libsoup-3.0
- Test suite fully functional with proper GSettings schema resolution
- Installation targets preserved: binary, desktop file, icons, UI files, GSettings schemahanges to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2025-09-15

### Added
- Meson build system migration from Make
- Full Meson configuration with all build targets
- Test environment configuration for GSettings schema
- Config.h generation via Meson
- Build system dependency validation and summary
Changed
- Migrated from Make to Meson 1.7.2 build system
- Updated build configuration to use Meson’s native dependency management
- Enhanced hardening flags integration through Meson options
- Improved test execution with proper environment setup
Technical Details
- Requires Meson >= 1.7.2 and Ninja >= 1.12.1
- Maintains compatibility with existing Make-based build (parallel support)
- All dependencies properly detected: GTK4, libadwaita-1, libcmark, libsoup-3.0
- Test suite fully functional with proper GSettings schema resolution
- Installation targets preserved: binary, desktop file, icons, UI files, GSettings schema
## [0.1.0] - Previous releases
- Initial application development with Make build system
- GTK4 and libadwaita integration
- CommonMark rendering engine
- GSettings configuration system
- Test suite implementation
