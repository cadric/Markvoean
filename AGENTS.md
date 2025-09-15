agents.md — Development & Contribution Guidelines (GTK4/libadwaita, C)
Audience: AI coding assistants (Codex, Claude CODE). Review this file before any change.
1) Purpose & Scope
Build a modern GNOME app using C + GTK 4 + libadwaita with safe patterns (GLib/GObject, GIO). These rules balance clarity, performance, accessibility, and reproducibility.
2) Platform & Runtime
• Toolkit: GTK 4; Design: libadwaita widgets (GNOME HIG)
• Language: C11/C17 (or gnu11 if required)
• Runtime: Wayland-first; Flatpak-friendly
• Settings: GSettings schema id: org.gtk.gtktext, path: /org/gtk/gtktext/
3) Repository Layout
.
├── Makefile               # Build/test/install
├── src/                   # main.c, cmrender.c, toolbar.c, settings.c
├── include/               # headers for modules
├── ui/                    # GtkBuilder XML (e.g., main_window.ui)
├── data/                  # desktop file, icons, GSettings schema
├── tests/                 # unit tests (GLib test framework)
├── scripts/               # helpers (e.g., install-deps.sh)
└── docs/                  # extra specs (e.g., commonmark_rules.md)
4) Quickstart (Build/Run/Test)
# Optional: install deps via scripts/install-deps.sh

make                    # build → bin/gtktext
./bin/gtktext           # run

# Dev: ensure schema is visible when running uninstalled

glib-compile-schemas ./data
GSETTINGS_SCHEMA_DIR=./data ./bin/gtktext

# Tests

make test

# Install (adjust PREFIX/DESTDIR)

make install PREFIX=$HOME/.local
5) Tooling & Flags
• pkg-config: gtk4 libadwaita-1 libcmark
• Warnings: -Wall -Wextra (treat new warnings as errors in CI)
• Hardening (default):
◦ -O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
◦ Linker: -Wl,-z,relro -Wl,-z,now -pie
• Format: make format (clang-format)
• Static analysis: optional clang-tidy
6) Guiding Principles
1. GNOME-first: follow HIG; use libadwaita patterns (AdwApplicationWindow, AdwToolbarView, AdwHeaderBar).
2. Simplicity: short, focused functions; no speculative abstractions.
3. Safe C: prefer g_autoptr/g_autofree; clear ownership semantics.
4. Responsive UI: no blocking I/O on main loop—use GIO async or worker threads.
5. A11y & i18n: ship accessible UIs and translatable strings by default.
6. Reproducibility: deterministic builds; consistent flags across CI and local.

7) Coding Standards (C/GLib/GObject)
• Style: follow project clang-format; snake_case for functions/vars; TypeName for GTypes.
• Headers: keep public API in include/; static for internal functions.
• Returns: always check GTK/GLib return values; guard with g_return_(val_)if_fail() in public APIs.
• GObject: use G_DECLARE_FINAL_TYPE/G_DEFINE_TYPE for new types; manage refs with g_object_ref_sink() and g_object_unref().
• Signals: connect via g_signal_connect(); avoid leaks (disconnect or use g_weak_ref when appropriate).
• Containers & Strings: prefer GLib (GPtrArray, GHashTable, g_strdup_printf, g_strlcpy). Avoid raw malloc/free/strcpy/sprintf where GLib provides safer helpers.
8) Memory, Errors & Logging
• Ownership: document who owns/returns what; prefer transfer annotations in comments.
• Auto cleanups: use g_autoptr/g_autofree/g_auto(GStrv) to reduce leaks.
• Errors: use GError** for recoverable failures; never exit()/abort() for normal errors.
• Logging: g_debug, g_message, g_warning (no printf in production paths).
9) UI, HIG & Theming
• Build UI in GtkBuilder (.ui files). Keep logic in C.
• Respect spacing/typography per GNOME HIG; primary actions on the right, cancel on the left.
• Dark mode: follow system scheme—no custom theme switches or CSS hacks.
• Shortcuts: provide accelerators; do not use GtkShortcutsWindow (deprecated ≥ GTK 4.18). Offer a simple help menu/overlay alternative.
10) Accessibility (♿)
• Provide accessible names/labels and relations. Use gtk_accessible_update_property() as needed.
• Ensure full keyboard navigation; test tab order and focus visibility.
• Verify with a screen reader (e.g., Orca) and high-contrast themes.
11) Internationalization (🌐)
• Wrap all user-visible strings in _(); initialize gettext at startup.
• Use ngettext() for plurals. Keep translatable strings out of business logic.
• Ensure strings in .ui files are marked for translation.
12) Performance & I/O
• Avoid needless allocations and copies on hot paths.
• Never block the main loop: use GIO async (*_async/finish) or worker threads.
• Debounce UI updates; throttle expensive work triggered by signals.
13) Testing
• Add small, focused tests in tests/ (GLib g_test**).
• Cover Markdown round-trips and tag handling in cmrender.c.
• Run under Valgrind/ASan in CI or locally for leak/UB detection.
• Keep tests headless; avoid interactive UI dependencies.
14) Security
• Use GLib/GIO for file and process operations; avoid system().
• Use Flatpak portals when packaging.
• For temp files, use g_file_new_tmp().
• Sanitize and validate all user-provided data; never execute/eval external input.
15) CI & Releases

1. Code
2. Tests pass (make test)
3. Update versions.json (SemVer) and CHANGELOG.md
4. Commit & tag

16) PR Review Checklist
• HIG followed; UI built via GtkBuilder
• No blocking I/O or new global mutable state
• Accelerators present; shortcut help discoverable (no GtkShortcutsWindow)
• i18n and A11y verified
• Hardening flags present; no new warnings; tests pass; no leaks
• Docs and CHANGELOG updated if user-facing changes
17) AI Assistant Playbook (Codex, Claude CODE)
• Obey this file; prefer minimal diffs and focused commits.
• Explain changes in commit messages; include rationale and screenshots for UI.
• Honor ownership rules; avoid globals; use GObject patterns.
• Propose tests alongside feature changes or fixes.
• Avoid hallucinations: if unsure, add TODO with a precise question in the PR.
18) Deprecated / Do Not Use
• GTK 3, deprecated GTK4 widgets (e.g., GtkShortcutsWindow)
• Custom theming that breaks Adwaita
• Direct system() calls for core logic
• Raw malloc/free/strcpy/sprintf when GLib provides safer equivalents
19) Useful Commands
# Debug logging

G_MESSAGES_DEBUG=all ./bin/gtktext
# GTK action tracing

GTK_DEBUG=actions ./bin/gtktext
# Wayland-only test

GDK_BACKEND=wayland ./bin/gtktext
20) References
• GNOME HIG — https://developer.gnome.org/hig/
• GTK 4 API — https://docs.gtk.org/gtk4/
• libadwaita — https://gnome.pages.gitlab.gnome.org/libadwaita/doc/
• GLib/GIO — https://docs.gtk.org/glib/ and https://docs.gtk.org/gio/
• GObject Guide — https://developer.gnome.org/gobject/stable/
• Gettext (i18n) — https://www.gnu.org/software/gettext/manual/
