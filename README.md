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

### 1) Install dependencies

Use the helper script (detects your distro, installs dev packages, ninja, and ensures Meson ≥ 1.7.2):

```bash
./scripts/install-deps.sh
# If Meson was installed via pip (user), ensure ~/.local/bin is on PATH
export PATH="$HOME/.local/bin:$PATH"
```

### 2) Build

```bash
meson setup builddir -D c_std=c23
meson compile -C builddir
```

### 3) Test

```bash
# Full test suite (requires a display)
meson test -C builddir --print-errorlogs

# Headless-friendly (uses Xvfb if available or runs data-only)
./scripts/test-headless.sh
```

### 4) Run

```bash
# Ensure GSettings schema path is set
GSETTINGS_SCHEMA_DIR=./data ./builddir/src/gtktext
```

## Project Structure

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

## Documentation

- Repo layout: docs/REPO_LAYOUT.md
- Architecture: docs/ARCHITECTURE.md
- Contributing: docs/CONTRIBUTING.md
- Test docs/examples: docs/tests/

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under [LICENSE] - see the LICENSE file for details.
