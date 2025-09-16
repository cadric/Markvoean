# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.13] - 2025-09-16

### Fixed
- Fixed "Don't Save" button causing infinite dialog loop by adding recursion prevention flags
- Save button now saves directly to existing file path instead of always showing file dialog
- Fixed dialog response handling to prevent window close request recursion
- Enhanced dirty detection with debug output to identify content comparison issues
- Improved parent window management for proper dialog cleanup

## [0.3.12] - 2025-09-16

### Fixed
- Fixed "Don't Save" button not responding in unsaved changes dialog
- Save button in unsaved changes dialog now shows file dialog for choosing save location
- Autorecover functionality now properly called on application startup
- Fixed dialog response handling and parent window management

## [0.3.11] - 2025-09-15

### Added
- Unsaved changes dialog on exit: Prompts user to save, discard, or cancel when closing with unsaved changes
- Better file content tracking for detecting modifications

## [0.3.10] - 2025-09-15

### Fixed
- Fixed link cursor boundary detection with precise character rectangle checking to prevent cursor extending beyond link text

## [0.3.9] - 2025-09-15

### Fixed
- Improved link cursor hit-testing accuracy using gtk_text_view_get_iter_at_position() with trailing character handling

## [0.3.8] - 2025-09-15

### Added
- Link hover cursor: Cursor changes to pointer when hovering over clickable links in the text view

## [0.3.7] - 2025-09-15

### Fixed
- Fixed "Normal tekst" option to correctly detect and remove heading formatting from rendered text

## [0.3.6] - 2025-09-15

### Fixed
- Fixed "Normal tekst" option to properly remove heading formatting instead of adding Heading 1

## [0.3.5] - 2025-09-15

### Fixed
- Fixed heading toolbar buttons to trigger re-rendering immediately when applied

## [0.3.4] - 2025-09-15

### Fixed
- Fixed heading formatting to preserve tight spacing between consecutive headings

## [0.3.3] - 2025-09-15

### Fixed
- main.c: Fixed space key causing unwanted line breaks by making live reparse selective
- main.c: Live reparse now only triggers for specific markdown characters (#, *, _, `, newline)
- main.c: Fixed normal typing behavior - spaces no longer interfere with text input
- toolbar.c: Completely rewrote source view toggle with proper widget lifecycle management
- toolbar.c: Fixed source view crashes after multiple toggles by creating fresh widgets each time
- toolbar.c: Added proper widget cleanup and validation to prevent GTK assertion failures

## [0.3.2] - 2025-09-15

### Fixed
- main.c: Fixed heading rendering not working when typing #, ##, ### directly in WYSIWYG view
- main.c: Added live markdown reparse to on_text_changed callback for immediate heading formatting
- toolbar.c: Fixed source view crashes on second toggle by properly managing widget references
- toolbar.c: Redesigned view swapping to use stored scrolled_window and original_text_view references
- toolbar.c: Fixed GTK widget assertion failures when switching between WYSIWYG and source modes

## [0.3.1] - 2025-09-15

### Fixed
- toolbar.c: Fixed critical GTK widget assertion crashes when using source view toggle
- toolbar.c: Fixed segmentation fault in heading button handlers (1-6)
- toolbar.c: Corrected heading button data storage from integers to proper markdown strings
- toolbar.c: Removed unsafe manual widget reference management in view swapping
- toolbar.c: Added proper widget validity checks in source view creation
- toolbar.c: Fixed heading insertion to work at cursor position instead of selection

## [0.3.0] - 2025-09-15

### Added
- UI compliance with GNOME HIG patterns
- Comprehensive accessibility support (WCAG AA)
- Full internationalization (i18n) framework
- GObject type system for data structures (Document, ImageWidget)
- Keyboard shortcuts discovery window
- Accessible labels and descriptions for all UI elements
- POT file generation for translations

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
- **UI Structure Compliance**: Verified HIG and libadwaita patterns compliance
- **Accessibility Support**: Comprehensive accessible names, roles, relations, and keyboard navigation
- **Keyboard Shortcuts Window**: Discoverable shortcuts window (ui/shortcuts.ui) with Ctrl+?
- **i18n Framework**: Complete gettext integration with POT file generation
- **GObject Type System**: Proper G_DECLARE_FINAL_TYPE + G_DEFINE_TYPE implementation
  - GtktextDocument: Document management with content, file path, and modified state
  - GtktextImageWidget: Image widget metadata management for markdown
  - GtktextUtilState: Utility state management using GObject pattern
- **WCAG AA Compliance**: Proper labeling, keyboard navigation, and contrast support
- **Translation Infrastructure**: POTFILES.in, LINGUAS, and .desktop.in template

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
- **UI Files**: Moved XML declaration to top for proper i18n parsing
- **Util Module**: Converted from simple struct to proper GObject type
- **Menu Structure**: Added keyboard shortcuts menu item for better accessibility

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