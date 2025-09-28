# Architecture Overview

This project is a GTK4/libadwaita markdown editor with a modular architecture:

## Modules

- Render (`src/render`) — Markdown import/export and visual helpers
  - `markdown/` — `cmrender.c` engine and `cmrender_export.c` for buffer→markdown
  - `images/` — image widget integration (e.g., `image_widget.c`)
  - `visual/` — theme-aware visuals (e.g., `hr_widget.c`, `theme_styles.c`)
- Document (`src/document`) — Document lifecycle and state
  - `document_manager.c` — async load/save, autosave, drafts, recovery
  - `doc_state.c` — hash-based clean/dirty state tracking
  - `version_history.c`, `recovery_drafts.c`, `fs_utils.c`, `atomic_io.c`
  - `external_changes.c` — file monitoring and conflict resolution
  - `internal/` — private headers only
- UI (`src/ui`) — Actions, dialogs, tab management, status
- Core (`src/core`) — Settings, utility helpers
- Components (`src/components`) — Toolbar and future widgets
- Editor (`src/editor`) — Buffer manager and text interactions

## Public API

Headers under `include/gtktext/**` form the public surface (installed):

- `document/document_manager.h`, `document/doc_state.h`
- `render/cmrender.h`, `render/tag_manager.h`, `render/image_widget.h`, `render/hr_widget.h`, `render/safe_helpers.h`
- `core/*`, `ui/*` (selected public helpers)

Internal/private headers live under `src/**/internal/` and must not be installed.

## Async & UI

- All heavy I/O uses async APIs (`GFile` replace/load async, `GTask`).
- The UI thread must remain responsive; dialog operations use non-blocking GTK 4 `GtkFileDialog`.

## Testing

- GLib test framework, data-only suite can run headless.
- GUI tests can run under Xvfb or a Wayland headless compositor.

