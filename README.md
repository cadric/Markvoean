# IFG
*It format good*

A simple text editor with Markdown support built with GTK4 and libadwaita.

## Features

- Markdown formatting and preview
- Syntax highlighting for code blocks
- Modern GTK4 and libadwaita UI
- Support for headings, bold, italic, and code formatting
- Export/import Markdown functionality

## Quick Start

### Installing Dependencies

Run the dependency installation script:

```bash
./scripts/install-deps.sh
```

### Building

#### Quick Development Workflow

```bash
# Build, test, and run in one command
./build-and-run.sh

# Or use separate scripts:
./build.sh          # Just build
./run.sh            # Just run (after building)
```

#### Manual Build Commands

```bash
# Set up build directory (first time only)
meson setup builddir

# Build
meson compile -C builddir

# Run tests
meson test -C builddir

# Run application
GSETTINGS_SCHEMA_DIR=./data ./builddir/src/gtktext
```

# Run the application
./bin/gtktext
```

### Running Tests

```bash
meson test -C builddir
```

## Project Structure

```
gtktext/
├── src/                # All C source files (.c)
│   ├── main.c
│   ├── toolbar.c
│   ├── cmark.c
│   └── settings.c
├── include/            # All header files (.h)
│   ├── toolbar.h
│   ├── cmark.h
│   └── settings.h
├── ui/                 # GtkBuilder .ui files
│   └── main_window.ui
├── po/                 # Translation files
├── tests/              # Unit tests
│   └── test_cmark.c
```
gtktext/
├── src/                # Source code
├── include/            # Header files
├── ui/                 # GTK UI files (.ui)
├── data/               # App icons, .desktop files, GSettings schema
│   └── icons/
├── tests/              # Test files
├── scripts/            # Helper scripts
├── builddir/           # Meson build directory
├── meson.build         # Main build configuration
├── build-and-run.sh    # Quick build, test, and run script
├── build.sh            # Quick build script
├── run.sh              # Quick run script
└── README.md
```

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under [LICENSE] - see the LICENSE file for details.
