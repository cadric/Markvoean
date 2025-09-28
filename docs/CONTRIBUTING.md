# Contributing

Thanks for your interest in contributing! This project follows GNOME-first patterns with GLib/GObject and libadwaita.

## Coding Standards

- Language: C23 (C11 acceptable). Prefer clarity over cleverness.
- GObject/GLib: Use `G_DECLARE_FINAL_TYPE`, `G_DEFINE_TYPE`, `g_autoptr`, `g_autofree`.
- Ownership: Explicit transfer; unref/free on teardown.
- Logging: `g_debug/g_message/g_warning/g_critical` — no `printf`.
- Threading: UI on main thread; async I/O or `GTask` for work.
- Errors: Use `GError**` for recoverable errors; never `exit()` for normal errors.
- File I/O: Use GIO/portals; no raw `system()` or sync I/O in UI paths.

## Layout

- Public headers live under `include/gtktext/**` and are installed.
- Internal/private headers live under `src/**/internal/` and are not installed.
- Filenames are `snake_case.c/.h`.

## Build & Tests

- Build with Meson:
  ```bash
  meson setup builddir -D c_std=c23
  meson compile -C builddir
  meson test -C builddir --print-errorlogs
  ```
- Headless GUI tests: `scripts/test-headless.sh` wraps tests with Xvfb if available.

## Static Analysis

- Clang-tidy: `./scripts/clang-tidy.sh` (uses compile_commands.json)
- IWYU: `./scripts/iwyu.sh` (if installed)

## Commit Style

- Conventional Commits (feat:, fix:, refactor:, perf:, docs:, test:, build:, ci:, chore:)
- Imperative, ≤72 chars subject.

## MR/PR Checklist

- HIG followed; GtkBuilder UI.
- No blocking the main loop; no new global mutable state.
- i18n + accessibility verified.
- Hardening flags intact; no new warnings.
- Tests passing (data suite at minimum), no leaks in touched paths.
- Docs + changelog updated for user-visible changes.

