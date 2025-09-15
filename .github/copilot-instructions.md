# agents.md — Development & Contribution Guidelines (C + GTK4/libadwaita)

**Audience:** AI coding assistants (Copilot, Claude Code, Code LLMs). Read before any change.

---

## 0) Versioning & File Annotations

**Policy:** Semantic Versioning `MAJOR.MINOR.PATCH`. Update `project()` version in `meson.build` and `CHANGELOG.md`.

**Scope:** Source (`.c`, `.h`), build (`meson.build`, `meson_options.txt`), resources (`.ui`, schema, desktop, appdata), CI, and `CHANGELOG.md`.

**Per‑file header format:**

```c
/* [2.1.2] - 2025-09-15 - src/main.c
 * Changed: explain what changed briefly.
 */
```

* One line per file under the same version block in `CHANGELOG.md`.
* Group related edits under the same version.

**Bump rules:**

* **PATCH:** Fixes and non‑breaking tweaks.
* **MINOR:** New features, backward‑compatible.
* **MAJOR:** Breaking changes or migrations.
* Default to PATCH if unsure.

**Procedure:**

1. Read current version (Meson `project(version:)`).
2. Decide bump type.
3. Update headers in all changed files.
4. Add dated entry in `CHANGELOG.md`.
5. Do not touch unrelated files.

---

## 1) Purpose & Scope

Modern GNOME app in **C + GTK 4 + libadwaita** with safe GLib/GObject patterns. Priorities: HIG, Wayland, accessibility, security, performance, reproducibility.

## 2) Platform & Runtime

* **Toolkit:** GTK 4
* **Design:** libadwaita widgets and patterns
* **Language:** C11 or C17 (gnu11 allowed if needed)
* **Display:** Wayland‑only (no X11 deps); test with `GDK_BACKEND=wayland`
* **Packaging:** Flatpak‑friendly
* **Settings:** `org.gtk.gtktext` at `/org/gtk/gtktext/`

## 3) Repository Layout

```
.
├── meson.build              # project(), subdirs, version
├── src/                     # main.c, cmrender.c, toolbar.c, settings.c
├── include/                 # public headers
├── ui/                      # GtkBuilder XML (e.g., main_window.ui)
├── data/                    # desktop, appdata, icons, GSettings schema
├── tests/                   # GLib tests
├── scripts/                 # helper tools
└── docs/                    # specs (e.g., commonmark_rules.md)
```

## 4) Quickstart (Build/Run/Test)

```sh
meson setup build
meson compile -C build
GSETTINGS_SCHEMA_DIR=./data ./build/gtktext

# Dev: ensure schema visible
glib-compile-schemas ./data
GSETTINGS_SCHEMA_DIR=./data ./build/gtktext

# Tests
meson test -C build --print-errorlogs
```

## 5) Tooling & Flags

* **pkg-config:** gtk4, libadwaita-1, libcmark
* **Warnings:** `-Wall -Wextra`; CI treats new warnings as errors
* **Hardening:** `-O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE` and linker `-Wl,-z,relro -Wl,-z,now -pie`
* **Format:** `clang-format`; **Static analysis:** `clang-tidy`

## 6) Guiding Principles

1. **GNOME‑first:** follow HIG; use Adwaita patterns (AdwApplication, AdwApplicationWindow, AdwToolbarView, AdwHeaderBar)
2. **Simplicity:** short, focused functions; minimal state
3. **Safe C:** clear ownership; `g_autoptr`/`g_autofree`
4. **Responsive UI:** no blocking I/O; use GIO async or workers
5. **A11y & i18n:** accessible and translatable by default
6. **Reproducible:** deterministic flags; same in CI and local

## 7) Coding Standards (C/GLib/GObject)

* Style via `clang-format`; snake\_case for functions/vars; `TypeName` for GTypes
* Public API lives in `include/`; mark internals `static`
* Validate inputs with `g_return_(val_)if_fail()` in public APIs
* New types: `G_DECLARE_FINAL_TYPE` + `G_DEFINE_TYPE`
* Manage refs with `g_object_ref_sink()` / `g_object_unref()`
* Signals via `g_signal_connect()`; ensure disconnect or use weak refs when needed
* Prefer GLib containers and string APIs; avoid raw `malloc/free/strcpy/sprintf`

## 8) Memory, Errors & Logging

* Document ownership and transfer
* Auto cleanups: `g_autoptr`, `g_autofree`, `g_auto(GStrv)`
* Recoverable failures: `GError**`; do not `exit()`/`abort()` for normal errors
* Logging: `g_debug`, `g_message`, `g_warning` (no `printf` in production paths)

## 9) UI, HIG & Theming

* Build UI in GtkBuilder `.ui` files; logic in C
* Respect HIG spacing/typography; primary on the right, cancel on the left
* Follow system dark/light; avoid custom theme hacks
* Shortcuts: provide accelerators and a discoverable help entry; avoid deprecated widgets

## 10) Accessibility (WCAG‑aligned)

* Provide accessible names/labels and relations; update via `gtk_accessible_update_property()` when needed
* Full keyboard navigation; verify focus order and visibility
* Test with a screen reader (Orca) and high‑contrast themes
* Avoid color‑only cues; meet contrast (aim ≥ WCAG AA)

## 11) Internationalization

* Wrap user‑visible strings in `_()`; init gettext at startup
* Use `ngettext()` for plurals; keep translatable strings out of core logic
* Mark strings in `.ui` for translation

## 12) Performance & I/O

* Avoid needless allocations and copies in hot paths
* Never block the main loop; use `*_async`/`*_finish` or worker threads
* Debounce UI updates; throttle signal‑triggered heavy work

## 13) Testing

* Add focused tests with GLib `g_test_*`
* Cover Markdown round‑trips and tag handling in `cmrender.c`
* Run with AddressSanitizer/UBSan or Valgrind for leaks/UB
* Keep tests headless; avoid interactive dependencies

## 14) Security

* Use GLib/GIO for file and process ops; avoid `system()`
* Flatpak Portals for file access and open/save dialogs
* Temp files via `g_file_new_tmp()`
* Validate and sanitize all external input; never execute user data

## 15) Packaging, Metadata & Releases

1. Code builds cleanly; tests pass
2. Update `meson.build` version and `CHANGELOG.md`
3. AppStream metadata (`.appdata.xml`), desktop file, icons valid
4. Tag release

## 16) PR Review Checklist

* HIG followed; UI comes from GtkBuilder
* No blocking I/O; no new global mutable state
* Accelerators present; shortcut help discoverable
* i18n and accessibility verified
* Hardening flags present; no new warnings; tests pass; no leaks
* Docs and `CHANGELOG.md` updated for user‑visible changes

## 17) AI Assistant Playbook

* Obey this file; keep diffs minimal and commits focused
* Explain changes in commit messages; include rationale and UI screenshots when relevant
* Honor ownership rules; use GObject patterns
* Propose tests with features or fixes
* If unsure, add a precise TODO or question in the PR

## 18) Deprecated / Do Not Use

* GTK 3; deprecated GTK 4 widgets (check docs)
* Custom theming that breaks Adwaita
* Direct `system()` in core logic
* Raw `malloc/free/strcpy/sprintf` where GLib provides safer helpers

## 19) Useful Commands

```sh
# Debug logging
G_MESSAGES_DEBUG=all ./build/gtktext
# GTK action tracing
GTK_DEBUG=actions ./build/gtktext
# Wayland‑only run
GDK_BACKEND=wayland ./build/gtktext
```

## 20) References

* GNOME HIG — [https://developer.gnome.org/hig/](https://developer.gnome.org/hig/)
* GTK 4 API — [https://docs.gtk.org/gtk4/](https://docs.gtk.org/gtk4/)
* libadwaita — [https://gnome.pages.gitlab.gnome.org/libadwaita/doc/](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/)
* GLib/GIO — [https://docs.gtk.org/glib/](https://docs.gtk.org/glib/) and [https://docs.gtk.org/gio/](https://docs.gtk.org/gio/)
* GObject — [https://developer.gnome.org/gobject/stable/](https://developer.gnome.org/gobject/stable/)
* Gettext — [https://www.gnu.org/software/gettext/manual/](https://www.gnu.org/software/gettext/manual/)
