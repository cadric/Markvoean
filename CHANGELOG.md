# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 🏆 Modularization Journey Summary

**Versions 1.0.1 through 1.0.8** represent a comprehensive modularization effort that transformed GTKText from a monolithic architecture to a modern, component-based system:

- **Starting Point**: 2829-line main.c (monolithic, hard to maintain)
- **Final Result**: 236-line main.c (focused coordination layer)
- **Total Reduction**: 2593 lines removed (92% reduction)
- **Modules Created**: 15+ specialized modules with clear responsibilities
- **Functionality**: 100% preserved with enhanced stability and maintainability

This transformation provides a solid foundation for future development and collaborative work.

## [1.3.3] - 2025-09-18

### Added
- **🛡️ CRITICAL: Universal Save Confirmation System** - Comprehensive data loss prevention across ALL tab closure methods
  - Save dialog now appears for ANY modified tab regardless of closure method:
    - Tab X button click
    - Context menu "Close Tab", "Close Other Tabs", "Close All Tabs"
    - Ctrl+W keyboard shortcut
    - Window close button (checks ALL tabs)
  - Enhanced debugging with visual indicators for save dialog triggers
  - Prevents accidental data loss through consistent save confirmation flow
- **🏪 GNOME SOFTWARE COMPLIANCE** - Complete AppStream metadata for software center listing
  - Added `org.gtk.gtktext.metainfo.xml` with comprehensive application metadata
  - Proper categorization, keywords, and content rating for software centers
  - Release history with detailed changelogs for user visibility
  - Full AppStream 1.0 specification compliance
- **📱 MODERN DESKTOP INTEGRATION** - Enhanced desktop file with GNOME HIG compliance
  - Migrated to proper reverse-DNS application ID: `org.gtk.gtktext`
  - Added MIME type associations for markdown files (`text/markdown`, `text/x-markdown`)
  - Enhanced desktop entry with `StartupWMClass`, modern categories, and accessibility
  - Comprehensive keyword set for improved application discovery

### Enhanced
- **🔧 BUILD SYSTEM MODERNIZATION** - Advanced validation and compliance checks
  - Added automated AppStream metainfo validation with `appstreamcli`
  - Desktop file validation with `desktop-file-validate` in test suite
  - Enhanced icon system with scalable and symbolic variants
  - Proper hicolor theme compliance with multiple icon formats
- **📋 DEVELOPMENT WORKFLOW** - Updated CLAUDE.md with modern GNOME requirements
  - AppStream metainfo update requirements in release workflow
  - Application ID consistency checks in review checklist
  - Enhanced packaging guidelines for GNOME compliance

### Technical
- Verified all tab closure paths route through `on_tab_view_close_page` signal handler
- Added comprehensive debug logging with emoji indicators for save dialog events
- Enhanced `tab_document_get_modified()` validation across all closure scenarios
- Improved window close handler to check unsaved changes across ALL tabs simultaneously
- Standardized application ID to `org.gtk.gtktext` across all components
- Enhanced icon installation with proper naming conventions and symbolic variants

## [1.3.2] - 2025-09-17

### Fixed
- Segfault when closing tabs due to mismatched `AdwTabView::page-detached` handler signature
    - Corrected parameter type to `gint position` and added robust fallback to fetch `TabManager` from `tab_view` data
    - Prevents `user_data=0x1` corruption and stabilizes tab close/detach workflow

### Technical
- Store `tab_manager` back-reference on `AdwTabView` and clear it on teardown
- Minor defensive checks in detach handler for safer cleanup

## [1.3.1] - 2025-09-17

### Fixed
- **Critical GObject Errors**: Fixed NULL class pointer errors during tab close operations
  - Added proper validation in `tab_document_destroy()` before signal disconnection
  - Prevents crashes when disconnecting signals from invalid buffer objects
- **Missing UI Components Restored**: Toolbar and status bar functionality fully restored for tab-based UI
  - Toolbar with formatting buttons (Bold, Italic, Code, Headings, etc.) now works with active tab
  - Status bar shows save notifications and current file location
  - Both components properly integrated with tab switching system
- **Save-As Dialog**: Fixed missing save-as dialog for untitled documents in close workflow
  - Closing unsaved untitled tabs now shows proper save-as dialog
  - Dialog completion properly handles both save success and cancellation cases
  - Maintains tab close workflow integrity after save operations
- **GTK Label Warnings**: Eliminated width measurement warnings for status bar labels
  - Added proper width constraints and ellipsization to prevent layout issues
  - Status labels now handle long text gracefully without warnings

### Technical Improvements
- Enhanced tab close workflow with proper async dialog handling
- Improved error handling in file save operations during tab closure
- Better resource cleanup and signal management across tab operations

## [1.3.0] - 2025-09-17

### Added
- **Phase 4 Advanced Tab Features**: Complete implementation of professional-grade tab interface
  - Keyboard navigation between tabs (Ctrl+PageUp/PageDown, Ctrl+Tab/Ctrl+Shift+Tab)
  - Tab reordering via drag-and-drop (native AdwTabView functionality)
  - Right-click context menu framework (simplified for GTK4 compatibility)
  - Smart tab overflow handling with automatic scrolling
  - Optimized tab bar behavior with non-expanding tabs
- **Enhanced User Experience**:
  - Wraparound tab navigation for seamless workflow
  - Intuitive keyboard shortcuts following standard conventions
  - Automatic tab bar autohide when only one tab is open
  - Professional tab management capabilities

### Changed
- **Tab Navigation**: Added comprehensive keyboard shortcuts for power users
- **Tab Bar Configuration**: Optimized spacing and behavior for better UX
- **Event Handling**: Enhanced with proper GTK4 event controller patterns

### Technical
- Added `tab_manager_select_next_tab()` and `tab_manager_select_previous_tab()` functions
- Implemented `on_tab_view_key_pressed()` for keyboard navigation handling
- Added GTK4-compatible event controller for tab interactions
- Enhanced TabManager with advanced configuration options
- Prepared framework for future context menu implementation

## [1.2.0] - 2025-09-17

### Added
- **Phase 3 Document State Management**: Complete implementation of tab-based document state tracking
  - Dirty state indicators in tab titles (• bullet shows unsaved changes)
  - Real-time tab title updates when document content changes
  - Tab close confirmation dialog for unsaved changes with save/discard/cancel options
  - Automatic state management callbacks between TabDocument and TabManager
- **Enhanced User Experience**:
  - Tab titles automatically update to show file basename after save operations
  - Visual dirty state indicator follows GNOME HIG patterns
  - AdwAlertDialog for consistent unsaved changes confirmation
  - Proper cleanup of tab resources on close

### Changed
- **TabDocument API**: Added callback system for state change notifications
- **Tab Title Management**: Unified system for updating tab titles with dirty state
- **File Operations**: Save operations now properly clear dirty state and update titles
- **Tab Closing**: Enhanced with unsaved changes detection and user confirmation

### Technical
- Added `TabDocumentDirtyStateCallback` type for state change notifications
- Implemented `tab_document_set_dirty_state_callback()` for TabManager integration
- Added automatic tab title updates via `update_tab_title()` function
- Created `show_unsaved_changes_dialog()` with proper AdwAlertDialog integration
- Enhanced buffer change tracking to trigger callback-based title updates
- Added `cleanup_tab_resources()` helper for proper tab cleanup

## [1.1.0] - 2025-09-17

### Added
- **Phase 2 Tab-Aware File Operations**: Complete implementation of file operations in tab context
  - Open files now create new tabs instead of replacing current content
  - Save operations work with active tab document
  - Save-as operations preserve current filename as initial suggestion
  - Tab-aware dialog completion handlers with proper context management
- **Enhanced File Dialog Integration**:
  - File dialogs now use tab-specific completion callbacks
  - Improved filename suggestions based on current document state
  - Context structures for async dialog operations

### Changed
- **File Actions Architecture**: Updated file_actions.c to use TabManager and TabDocument APIs
- **Save Behavior**: Save operations now target the active tab instead of global document state
- **Dialog Workflows**: File dialogs integrated with multi-document tab system

### Technical
- Added `file_action_on_open_dialog_finish_tab()` for tab-aware file opening
- Added `file_action_on_save_as_dialog_finish_tab()` for tab-aware save-as operations
- Updated `file_action_save_cb()` and `file_action_save_as_cb()` to work with active tab
- Added TabDocument header dependency to file_actions.c
- Used proper C17 type definitions instead of `typeof` for compiler compatibility

## [1.0.8] - 2025-09-16

**MAJOR ACHIEVEMENT: Complete Modularization & Stability Fixes**

This release completes the comprehensive modularization of GTKText, achieving a remarkable 92% reduction in main.c size while resolving critical stability issues.

### Added
- **Application Initialization Module** (`src/core/app_initialization.c`) - Complete UI setup and initialization
- **Core Event Handlers Module** (`src/ui/event_handlers.c`) - Link clicks, tooltips, keyboard shortcuts, zoom
- **Document Event Handlers Module** (`src/document/document_handlers.c`) - File dialog completion, document state handling

### Fixed
- **GTK GtkGizmo Snapshot Warning**: Fixed "Trying to snapshot GtkGizmo without a current allocation" warning
- **Deprecated API Usage**: Replaced `gtk_widget_get_allocation` with modern `gtk_widget_get_width/height` APIs
- **Drawing Safety**: Added allocation checks before triggering widget redraws

### Changed
- **Main.c Reduction**: Reduced from 2829 lines to 236 lines (92% reduction total)
- **Module Organization**: Extracted remaining core functions to specialized modules
- **Clean Architecture**: Main.c now serves as a focused coordination layer

### Technical Details
- Completed extraction of core application initialization logic
- Moved all event handlers to specialized modules
- Updated build system to include new modules
- Maintained 100% functionality while achieving dramatic code organization improvement

## [1.0.7] - 2025-09-16

**CRITICAL FIX: Segmentation Fault Resolution**

This release resolves a critical segmentation fault that was causing the application to crash when opening files due to infinite recursion in the text view overlay setup.

### Fixed
- **Infinite Recursion Crash**: Fixed segmentation fault in text view overlay setup that occurred when opening files
- **Widget Realization Loop**: Replaced problematic `realize` signal with safer `map` signal to prevent recursion
- **Overlay Setup Safety**: Added recursion prevention mechanisms in blockquote overlay initialization

### Technical Details
- Modified `text_view_setup_blockquote_overlay` function in `src/ui/text_view_interactions.c`
- Replaced `on_text_view_realized` callback with `on_text_view_mapped` to avoid widget creation during realization
- Added recursion detection flags to prevent infinite loops

## [1.0.6] - 2025-09-16

**FEATURE: Configurable Autosave Control**

This release adds user control over the autosave functionality, allowing users to disable automatic saving if desired.

### Added
- **Autosave Toggle Setting**: New preference to enable/disable autosave
    - Accessible via preferences dialog (AdwSwitchRow UI element)
    - Stored in GSettings as `autosave-enabled` boolean key
    - Defaults to enabled (true) to preserve existing behavior
- **Dynamic Setting Changes**: Autosave can be toggled on/off without restarting the application
    - Settings changes are applied immediately
    - DocumentManager responds to GSettings notifications
Changed
- **DocumentManager**: Enhanced to respect autosave-enabled setting
    - Checks setting before starting autosave timer
    - Stops autosave when disabled via preferences
- **GSettings Schema**: Added `autosave-enabled` boolean key with proper documentation
    - Added document_manager_update_autosave_setting() API
- Settings Dialog: Updated to include document preferences section
    - Clear toggle switch with descriptive subtitle
    - Real-time setting persistence
Technical Details
- **Signal Handling**: Implemented `changed::autosave-enabled` signal handler
- **Memory Management**: Proper cleanup of GSettings references in DocumentManager
## [1.0.5] - 2025-09-16

**CODE QUALITY IMPROVEMENT: Warning Cleanup and Dead Code Removal**

This release improves code quality by eliminating compiler warnings and removing dead code.

### Changed
- **Parameter Annotations**: Replaced excessive `G_GNUC_UNUSED` annotations with cleaner `(void)parameter;` pattern
    - Affects signal handlers in `main.c`, `toolbar.c`, `document_manager.c`, and `cmrender.c`
    - Improves code readability while maintaining warning suppression
- **Compiler Warnings**: Eliminated all unused parameter and unused function warnings
    - Build now produces clean output with no warnings
Removed
- **Dead Functions**: Removed completely unused functions from `cmrender.c`
    - `close_inline_tags_from_stack()` - never called
    - `open_inline_tags_for_segment()` - never called
    - `free_active_markdown_inline_tag()` - never called
    - `ActiveMarkdownInlineTag` struct - never used
- **Unused Files**: Removed obsolete `toolbar_new.c` file
## [1.0.4] - 2025-09-16

**CLEANUP RELEASE: Development Artifacts Removal**

This release cleans up the workspace by removing temporary files and development artifacts created during the DocumentManager migration.

### Removed
- **Test Files**: All temporary test files created during development
    - `test*.md` files (test.md, test_recovery.md, test_cursor.md, test_unsaved.md)
    - `test*.sh` scripts (test_recovery.sh, test_drafts.sh, test_external_changes.sh, test_phase6.sh, test_statusbar.sh, test_dont_save_fix.sh)
- **Development Documentation**: Completed phase documentation files
    - `AUDIT_REPORT.md` - DocumentManager audit results (migration complete)
    - `LEGACY_REMOVAL_COMPLETE.md` - Legacy system removal documentation
    - `PHASE5_STATUSBAR.md` - Status bar integration phase documentation
    - `PHASE6_INTEGRATION.md` - main.c integration phase documentation
    - `MESON_MIGRATION.md` - Build system migration documentation
    - `audit_save_system.sh` - Development audit script (no longer needed)
Changed
- **Workspace Structure**: Streamlined project structure with only essential files
- **Build System**: Verified clean build after file removal

**Rationale**: With DocumentManager migration complete and all features stable, the development artifacts and temporary test files are no longer needed. This cleanup improves project maintainability and clarity.

## [1.0.3] - 2025-09-16

**CRITICAL BUG FIX: DocumentManager Save Loop**

This release fixes a critical bug in DocumentManager where save_as operations would fail due to an infinite loop condition.

### Fixed
- **document_manager.c**: `document_manager_save_as()` infinite loop bug
    - Added missing `dm->is_untitled = FALSE` when setting file path in save_as
    - Previously, save_as would set file path but leave is_untitled=TRUE, causing document_manager_save to call save_as again
    - This caused save operations to fail silently, preventing any saves from working
    - Bug affected both toolbar save and close-dialog save for untitled documents
Technical Details

**Root Cause**: In `document_manager_save_as()`, when a file path was provided:
1. File path was set correctly: `dm->file_path = g_strdup(file_path)`
2. But `dm->is_untitled` remained TRUE
3. Called `document_manager_save()` which saw is_untitled=TRUE
4. This triggered another call to `document_manager_save_as()` with NULL path
5. NULL path caused save_as to return FALSE, failing the entire save

**Fix**: Set `dm->is_untitled = FALSE` after setting file path in save_as, breaking the loop.

## [1.0.2] - 2025-09-16

**CRITICAL BUG FIX: Save-Close Flow**

This release fixes a critical bug where the save-close workflow was broken due to mixed legacy/DocumentManager state tracking.

### Fixed
- **main.c**: Save-close dialog flow now works correctly
    - `has_unsaved_changes()` - Fixed to use DocumentManager state instead of legacy buffer text comparison
    - `on_unsaved_changes_dialog_response()` - Fixed to use DocumentManager file state for save-as vs save decision
    - `on_document_save_completed()` - Added close-after-save logic to properly close app after successful save
    - Buffer initialization - Added missing app reference storage for save dialog access to DocumentManager
Changed
- **UI State Tracking**: All unsaved changes detection now uses DocumentManager exclusively
- **Save Dialog Logic**: Dialog responses now query DocumentManager for file state instead of legacy variables

**Bug Details**: Previously, users could save via toolbar (DocumentManager worked), but trying to close the app would show save dialog, and clicking save in that dialog would fail to save or close. This was because UI state tracking still used legacy methods while DocumentManager operated independently.

## [1.0.1] - 2025-09-16

**CLEANUP RELEASE: Complete Legacy Save System Removal**

This release completes the DocumentManager migration by permanently removing all legacy save system components that were marked as deprecated in v1.0.0.

### Removed
- **Legacy Save Functions**: Completely removed deprecated functions
    - `save_buffer_to_file()` - replaced by DocumentManager save operations
    - `autosave_buffer()` - replaced by DocumentManager autosave system
    - `save_timeout_cb()` - replaced by DocumentManager timeout handling
    - Legacy autosave timeout and debounce variables (`save_timeout_id`, `autosave_delay_ms`)
- **Legacy Settings**: Removed old autosave configuration from schema
    - Removed `autosave-delay-ms` GSettings key - DocumentManager handles autosave internally at 3000ms
    - Cleaned up settings UI to remove deprecated autosave configuration options
- **Legacy Signal Handlers**: Removed deprecated settings change handlers
    - Removed `on_setting_changed()` autosave delay processing
    - Cleaned up GSettings signal connections for removed settings
Changed
- **Version Bumped**: All file headers updated to reflect v1.0.1
- **Code Cleanup**: Simplified codebase with pure DocumentManager implementation
- **Schema Simplification**: GSettings schema now only contains active configuration keys
Fixed
- **Reduced Complexity**: No more dual save system conflicts or legacy code paths
- **Memory Optimization**: Removed unused variables and function declarations
- **Build Cleanliness**: No more unused function warnings for deprecated components
Migration Notes
- **Automatic Migration**: No user action required - DocumentManager was already active in v1.0.0
- **Settings Reset**: Users will no longer see autosave interval settings in preferences (handled internally)
- **API Compatibility**: All public DocumentManager APIs remain unchanged
## [1.0.0] - 2025-01-16

**MAJOR RELEASE: Complete Save System Overhaul**

This release represents a complete rewrite of the document save system, implementing a production-ready document management infrastructure with zero data loss guarantees, predictable behavior, and comprehensive recovery capabilities.

### Added
- **Phase 1 - Core Save Infrastructure**: Complete DocumentManager system with atomic file operations
    - Atomic writes using platform-specific operations (POSIX rename, Windows ReplaceFile)
    - Comprehensive state machine (CLEAN, DIRTY, SAVING, DRAFT, READONLY, CONFLICT, ERROR)
    - Fsync-backed data integrity guarantees for zero data loss
    - Proper error handling and rollback mechanisms

- **Phase 2 - Drafts System**: Automatic draft management for unsaved changes
    - Automatic draft creation for any unsaved modifications
    - Draft persistence across application restarts
    - Intelligent draft cleanup when files are properly saved
    - Draft location tracking and metadata preservation

- **Phase 3 - Recovery System**: Crash recovery and session restoration
    - GKeyFile-based recovery format with metadata preservation
    - Automatic recovery snapshot creation during editing sessions
    - Crash detection and recovery prompt on application restart
    - Recovery file cleanup after successful restoration

- **Phase 4 - External Change Detection**: Real-time file monitoring and conflict resolution
    - GFileMonitor integration for real-time external change detection
    - Intelligent conflict resolution with user choice dialogs
    - Automatic reload for files without local modifications
    - External modification tracking and notification system

- **Phase 5 - UI Integration**: Status bar and visual feedback system
    - Comprehensive status bar showing save status and file location
    - Real-time status updates for all document state changes
    - Visual indicators for dirty state, saving progress, and conflicts
    - Accessibility-compliant status information with proper labeling

- **Phase 6 - Main Application Integration**: Complete replacement of legacy save system
    - Full DocumentManager integration replacing all legacy save functions
    - Refactored action_save_cb and action_save_as_cb with robust error handling
    - Integrated callback system for UI updates and status synchronization
    - Complete removal of old save infrastructure in favor of DocumentManager API
### Changed
- **document_manager.h**: New comprehensive public API for document lifecycle management
- **document_manager.c**: Complete implementation of robust save infrastructure with 1000+ lines of production code
- **main.c**: Full integration with DocumentManager API, replacing all legacy save operations
- **main_window.ui**: Enhanced UI with integrated status bar for real-time feedback
- **meson.build**: Updated to version 1.0.0 reflecting the major system overhaul
Technical Improvements
- Zero data loss guarantee through atomic operations and fsync
- Predictable save behavior with comprehensive state management
- Autosave every 2-5 seconds with intelligent draft management
- Platform-specific optimizations for file operations
- Comprehensive error handling with user-friendly recovery options
- Real-time UI feedback for all document operations
- Memory-safe implementation following GObject best practices
Testing
- Comprehensive test suite covering all 6 phases
- Individual test scripts for each component (test_phase*.sh)
- Integration testing with real file operations
- Crash recovery testing with forced application termination
- External modification testing with concurrent file access
- Memory leak testing and validation

This release establishes a production-ready document management foundation suitable for professional text editing applications.

## [0.3.14] - 2025-01-16log

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.14] - 2025-09-16

### Fixed
- Fixed false positive unsaved changes detection after auto-save by updating DATA_ORIGINAL_TEXT in both save functions
- Auto-save operations now properly update the baseline content for dirty detection
- Eliminated unnecessary unsaved changes dialogs when opening and immediately closing documents
## [0.3.13] - 2025-09-16

### Fixed
- Fixed “Don’t Save” button causing infinite dialog loop by adding recursion prevention flags
- Save button now saves directly to existing file path instead of always showing file dialog
- Fixed dialog response handling to prevent window close request recursion
- Enhanced dirty detection with debug output to identify content comparison issues
- Improved parent window management for proper dialog cleanup
## [0.3.12] - 2025-09-16

### Fixed
- Fixed “Don’t Save” button not responding in unsaved changes dialog
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
- Fixed “Normal tekst” option to correctly detect and remove heading formatting from rendered text
## [0.3.6] - 2025-09-15

### Fixed
- Fixed “Normal tekst” option to properly remove heading formatting instead of adding Heading 1
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
Changed
- Migrated from Make to Meson 1.7.2 build system
- Updated build configuration to use Meson’s native dependency management
- Enhanced hardening flags integration through Meson options
- Improved test execution with proper environment setup
- Optimized build configuration to use Meson’s warning_level instead of manual flags
- File dialog implementation with better error handling
- Memory management patterns with more consistent GLib usage
- Reorganized header files to follow GNOME standards (include/gtktext/ structure)
- Updated all source files to use standardized include paths
- Enhanced input validation across all public API functions
- **UI Files**: Moved XML declaration to top for proper i18n parsing
- **Util Module**: Converted from simple struct to proper GObject type
- **Menu Structure**: Added keyboard shortcuts menu item for better accessibility
Removed
- Makefile and Make build system (fully replaced by Meson)
- Make-generated obj/ and bin/ directories
- Legacy build artifacts and temporary files
- Duplicate warning flags in favor of Meson’s warning_level
Technical Details
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
