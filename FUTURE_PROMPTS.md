# Future Implementation Prompts

This file contains detailed prompts for future features and improvements that we want to implement later. Each prompt includes status tracking of what's been done vs. what remains.

---

## Version History and Crash Restore - Comprehensive Implementation

**Status:** 🟡 Partially Implemented
- ✅ **Done:** Basic version saving on document save, GSettings preferences, menu integration, file-based storage
- ❌ **Not Done:** Crash recovery, proper XDG directories, compression, metadata tracking, retention policies, atomic saves, ETag validation, diff/preview, manual snapshots

**Original Prompt:**
You are implementing **Version History** and **Crash Restore** in a GTK4 text editor. Follow GNOME HIG and best practices.

**Requirements:**

* **Version History**
  * Automatic checkpoints on explicit save. ✅ *Basic implementation exists*
  * Optional manual "Save version…" snapshots. ❌
  * Immutable, read-only previews. ❌
  * Allow diff against current, restore as new file, or revert with undo safety net. ❌
  * Timeline per-document, persists across sessions. ✅ *Basic implementation exists*
  * Retention policy: keep recent dense, older sparse. ❌
  * Store in `$XDG_DATA_HOME/<app-id>/versions/` as compressed whole snapshots (e.g. zstd). ❌ *Currently uses ~/.cache/gtktext/versions/ uncompressed*
  * Metadata in SQLite or JSON: doc-uri, etag, mtime, size, content-hash, label, created_at. ❌ *Currently uses basic GKeyFile format*

* **Crash Restore**
  * Autosave after 5s idle or 30s max interval. ❌
  * Store recovery files in `$XDG_STATE_HOME/<app-id>/recovery/<doc-id>.autosave`. ❌
  * Delete recovery files on clean exit. ❌
  * On crash, present non-modal banner: "Recovered unsaved changes from <time>". ❌
  * Let user compare with last saved version, keep either, or save as new file. ❌
  * Never auto-overwrite user's last saved file. ❌

* **GNOME/GTK4 Integration**
  * Use `g_file_replace_contents_async` with `G_FILE_CREATE_REPLACE_DESTINATION` and `make_backup=TRUE` for atomic save. ❌
  * Track and validate with GIO ETags. On `WRONG_ETAG`, prompt user to reload or save as copy. ❌
  * Use GtkSourceView undo manager for editing session. Do not serialize undo into versions. ❌
  * Flatpak/sandbox: access documents only via GNOME portal APIs. Store versions only in XDG dirs. ❌
  * Use libadwaita dialogs/panels for history with clear GNOME HIG layout. ✅ *Basic implementation exists*

* **Edge cases**
  * Remote GIO files: rely on ETags if available, else timestamp/size checks with warning. ❌
  * Very large files: cap automatic versioning, require manual snapshots. ❌
  * Optional: encrypt version store with user key for sensitive content. ❌

**Deliverables:**
* GTK4 C or Rust code skeleton implementing:
  1. Version snapshot save and restore. ✅ *Basic implementation exists*
  2. Autosave and crash recovery restore flow. ❌
  3. UI for version history list and recovery banner. ✅ *Basic version history UI exists*
* Storage layout and metadata schema. ❌ *Needs proper design*

**Implementation Notes:**
- Current implementation in `src/document/document_manager.c`
- Settings in `data/org.gtk.gtktext.gschema.xml`
- UI integration in `src/ui/actions/edit_actions.c` and `ui/main_window.ui`
- Would benefit from complete rewrite following this comprehensive approach

---

## Comprehensive Keyboard Shortcuts for CommonMark Editor

**Status:** 🟡 Partially Implemented
- ✅ **Done:** Basic file operations (Ctrl+O, Ctrl+S, Ctrl+Shift+S), tab operations (Ctrl+T, Ctrl+W), basic editing (Ctrl+Z, Ctrl+Shift+Z), preferences (Ctrl+,), shortcuts window (Ctrl+?)
- ❌ **Not Done:** Rich-text formatting shortcuts (bold, italic, links, code, headings, lists), navigation shortcuts, GNOME-specific shortcuts, text editing shortcuts (cut/copy/paste, select all, find/replace), dual redo support (Ctrl+Y), comprehensive command palette

**Original Prompt:**
You are implementing and validating keyboard shortcuts for a rich-text CommonMark (Markdown) editor targeting GNOME. Use these bindings by default. Expose a rebinding layer, but keep defaults unless overridden. Avoid conflicts with GNOME window shortcuts.

Scope:
* Rich-text UX that inserts CommonMark syntax under the hood.
* Cross-platform, but optimize for GNOME on Linux. Where two conventions exist, support both.

General (document):
* Ctrl+N → New document ❌
* Ctrl+O → Open file ✅
* Ctrl+S → Save ✅
* Ctrl+Shift+S → Save As ✅
* Ctrl+P → Print/Export ❌
* Ctrl+W → Close tab/document ✅
* Ctrl+Q → Quit app ❌
* Ctrl+Z → Undo ✅
* Ctrl+Shift+Z and Ctrl+Y → Redo (support both) 🟡 *Only Ctrl+Shift+Z supported*
* Ctrl+X / Ctrl+C / Ctrl+V → Cut/Copy/Paste ❌
* Ctrl+A → Select all ❌
* Ctrl+F → Find ❌
* Ctrl+H → Replace ❌

Text formatting → emit CommonMark:
* Ctrl+B → Bold → wrap selection with \*\* \*\* (toggle) ❌
* Ctrl+I → Italic → wrap selection with \* \* (toggle) ❌
* Ctrl+K → Insert/edit link → [text](url) (open dialog if selection empty) ❌
* Ctrl+Shift+I → Insert image → ![alt](url) ❌
* Ctrl+`→ Inline code → wrap with` \` (toggle) ❌
* Ctrl+Shift+K → Fenced code block → \`\`\` on separate lines (language prompt optional) ❌
* Ctrl+H (with Shift to cycle) → Heading level 1–6 → prefix line with # … ###### (idempotent) ❌
* Ctrl+Shift+7 (or Ctrl+L as fallback) → Unordered list item → prefix with "- " (toggle line/bulk) ❌
* Ctrl+Shift+8 → Ordered list item → "1. " with auto-renumber ❌
* Ctrl+Q (or Ctrl+>) → Blockquote → prefix with "> " (toggle) ❌

Navigation and editing:
* Ctrl+Left/Right → Move by word ❌
* Ctrl+Up/Down → Move by paragraph ❌
* Ctrl+Home / Ctrl+End → Doc start/end ❌
* Ctrl+Backspace / Ctrl+Delete → Delete prev/next word ❌
* Page Up / Page Down → Scroll page ❌
* Ctrl+PageUp / Ctrl+PageDown → Switch tabs (or pages if applicable) ❌

GNOME-specific app/window:
* F1 → Help ❌
* F10 → Menu bar ❌
* Ctrl+, → Preferences ✅
* Ctrl+Tab → Next tab ❌
* Ctrl+Shift+T → Reopen closed tab ❌
* Ctrl+Shift+N → New window ❌
* Alt+F10 → Maximize/restore window ❌
* Alt+F7 → Move window ❌
* Alt+F8 → Resize window ❌

Behavioral rules:
* All formatting shortcuts must be reversible (toggle) and respect mixed selections. ❌
* Preserve selection and caret intelligently after transforms. ❌
* Maintain CommonMark validity: escape backticks inside code fences; avoid nested emphasis errors; normalize list numbering on save/render. ❌
* Multicursor and block selections must apply line-wise transforms for lists, quotes, headings. ❌
* Provide command palette entries mirroring each shortcut. ❌
* Collision policy: if OS/WM intercepts a binding, show a non-modal hint and keep a secondary binding (listed above where provided). ❌
* Accessibility: expose all commands via menu entries; ensure shortcuts are discoverable in tooltips. ❌

Telemetry (optional):
* Log command ids, not content. No PII. Use to surface most-used shortcuts in onboarding. ❌

Acceptance tests:
* T1: Bold toggle wraps/unwraps exactly \*\* around selection; works at word boundaries; caret restored. ❌
* T2: Heading cycle Ctrl+H: plain → "# " → "## " … → "###### " → plain. ❌
* T3: List toggle on 3 selected lines produces "- " prefix; toggling again removes cleanly. ❌
* T4: Code block inserts fenced region with balanced backticks; language label preserved. ❌
* T5: Redo works via both Ctrl+Shift+Z and Ctrl+Y. ❌
* T6: Tab switching responds to Ctrl+Tab and Ctrl+PageUp/PageDown where applicable. ❌
* T7: GNOME window shortcuts remain functional and do not get shadowed by editor bindings. ❌

References:
* GNOME Human Interface Guidelines: Keyboard Input.
* CommonMark Spec 0.30 (syntax rules for emphasis, code, lists, headings).

**Implementation Notes:**
- Current shortcuts defined in `src/main.c` (lines 116-129)
- Shortcuts window UI in `ui/shortcuts.ui`
- Basic file operations and tab management working
- Missing: All rich-text formatting, navigation, standard text editing, dual redo support
- Would need new action system for formatting commands
- Command palette not implemented

---

## Template for Future Prompts

**Status:** 🔴 Not Started / 🟡 Partially Implemented / 🟢 Completed
- ✅ **Done:** [List completed items]
- ❌ **Not Done:** [List remaining items]

**Original Prompt:**
[Paste the full prompt here]

**Implementation Notes:**
[Any relevant notes about current state, file locations, or considerations]

---

*Add new prompts above this line*