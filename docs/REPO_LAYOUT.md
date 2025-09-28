# Repository Layout and Conventions

This project follows a Meson-friendly, GNOME/Adwaita-first structure with a clear split between public headers (installed) and private internals.

## Top-Level

- `include/gtktext/` — Public API headers (installed). Consumers should only include headers from this tree.
- `src/` — Implementations and private internals (not installed). Internal headers live under `internal/` subfolders.
- `ui/` — GtkBuilder `.ui` files.
- `data/` — AppStream metainfo, .desktop files, GSettings schemas, icons.
- `tests/` — Unit/integration tests (GLib test framework).
- `scripts/` — Local helper scripts.

## Public vs Private Headers

- Public API: headers under `include/gtktext/**`.
  - Examples: `include/gtktext/document/document_manager.h`, `include/gtktext/render/cmrender.h`.
- Private/internal API: headers under `src/**/internal/`.
  - Examples: `src/document/internal/document_manager_priv.h`, `src/document/internal/recovery_priv.h`.

Only headers under `include/gtktext/` are installed by Meson.

## Naming

- Filenames use `snake_case.c/.h` (e.g., `image_widget.c`, `hr_widget.c`, `tag_manager.c`).
- GTypes follow the standard GNOME naming (e.g., `GtktextImageWidget`).

## Build

- Meson config enforces C23 and hardening flags.
- Public headers are installed via `install_subdir('include/gtktext', install_dir: includedir / 'gtktext')`.

## Tests (Headless)

GUI tests need a display. In CI or headless environments, wrap test runs with a virtual display. Two common options:

1. Xvfb (X11)
   ```bash
   xvfb-run -s "-screen 0 1024x768x24" meson test -C builddir --print-errorlogs
   ```

2. Wayland headless compositor
   - Run a headless compositor (e.g., cage/headless) and set `GDK_BACKEND=wayland`.

Data-only suites can be run without a display:
```bash
meson test -C builddir --suite gtktext:data
```

