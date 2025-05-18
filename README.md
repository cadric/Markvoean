# GTKText

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

```bash
# Navigate to the project directory
cd gtktext

# Build the project
make

# Run the application
./bin/gtktext
```

### Running Tests

```bash
make test
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
├── data/               # App icons, .desktop files
│   └── icons/
├── scripts/            # Helper scripts
├── README.md
└── Makefile
```

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under [LICENSE] - see the LICENSE file for details.
