# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

GTKText is a modern text editor with Markdown support built using GTK4, libadwaita, and cmark. The project follows GNOME HIG guidelines and uses C99/C11 standards with GObject-based programming patterns.

## Build System & Commands

### Building
```bash
make                    # Build the application
./bin/gtktext          # Run the application
```

### Testing
```bash
make test              # Run all unit tests
```

### Code Quality
```bash
make format            # Format code using clang-format
```

### Cleaning
```bash
make clean             # Remove build artifacts
```

### Installation
```bash
make install           # Install to system (use PREFIX= to override)
make uninstall         # Remove from system
```

## Architecture

### Core Components

- **src/main.c**: Application entry point, GTK4/Adwaita setup, and text buffer management with autosave and markdown parsing
- **src/cmrender.c**: Markdown rendering engine that converts between GTK text buffers and CommonMark format
- **src/toolbar.c**: UI toolbar implementation with markdown formatting buttons
- **src/settings.c**: GSettings integration for application preferences

### Key Features

- **Markdown Processing**: Uses `cmrender.c` for bidirectional conversion between GTK text buffers and CommonMark
- **Real-time Rendering**: Text is parsed and formatted in real-time as the user types
- **Image Embedding**: Support for inline images with HTTP/HTTPS loading via optional libsoup-3.0
- **Autosave**: Debounced autosave functionality with GSettings integration

### Dependencies

Required:
- GTK4 (`pkg-config --cflags --libs gtk4`)
- libadwaita-1 (`pkg-config --cflags --libs libadwaita-1`) 
- libcmark (`pkg-config --cflags --libs libcmark`)

Optional:
- libsoup-3.0 (for HTTP image loading, enabled with `-DHAVE_LIBSOUP=1`)

### Code Standards

- **C Standard**: C99 minimum, C11 recommended
- **Compiler**: Use gcc or clang with `-Wall -Wextra -Werror`
- **Security**: Enabled stack protection, FORTIFY_SOURCE, PIE, RELRO
- **Memory**: Use GLib memory functions (`g_malloc`, `g_free`) over standard C
- **Error Handling**: Use `GError` for GTK/GLib error reporting
- **Formatting**: Use clang-format for consistent style
- **Object Model**: Follow GObject patterns with `G_DECLARE_FINAL_TYPE` and `G_DEFINE_TYPE`

### UI Guidelines

- Use GtkBuilder `.ui` files for interface layout (stored in `ui/` directory)
- Implement AdwApplication and Adwaita widgets for modern GNOME integration
- Separate UI logic from application logic
- Use `g_signal_connect` for event handling with proper memory management
- Test with keyboard navigation and high-contrast mode for accessibility

### Testing

- Tests are located in `tests/` directory using GLib testing framework
- Use `g_test_add_func()` for unit tests
- Tests exclude `main.o` to avoid conflicts with test runners
- Run individual tests from `tests/bin/` after building