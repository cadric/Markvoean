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
```
.
├── include/gtktext/         # Public headers (installed)
│   ├── core/                # App/core public APIs
│   ├── document/            # Document manager + state public APIs
│   ├── render/              # Rendering public APIs (cmrender, tag_manager, image_widget, etc.)
│   └── ui/                  # UI-facing helper APIs
├── src/                     # Implementation (.c) and private internals
│   ├── core/
│   ├── document/
│   │   └── internal/        # Private headers (not installed)
│   ├── render/
│   │   ├── markdown/
│   │   ├── images/
│   │   └── visual/
│   └── ui/
├── ui/                      # GtkBuilder .ui files
├── data/                    # .desktop, metainfo, GSettings schema, icons
├── tests/                   # Unit/integration tests (Meson)
├── scripts/                 # Helper scripts
├── builddir/                # Meson build directory (generated)
├── meson.build              # Root Meson configuration
├── src/meson.build          # Source Meson (aggregates subdirs)
├── build-and-run.sh         # Build + test + run helper
├── build.sh                 # Build helper
├── run.sh                   # Run helper
└── README.md
```

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under [LICENSE] - see the LICENSE file for details.
