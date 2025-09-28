# Codex Agent – Minimal Engineering Context

## Project Constraints

* Language/stack: **C23** (idiomatic) with **GLib/GObject** only.
* Platform: **GTK 4**, **libadwaita**, Wayland-first, Flatpak-friendly.
* UX: GNOME HIG patterns; dark mode follows system (no theme overrides).
* Safety: explicit ownership; predictable lifetimes; hardened builds.

## Non‑Negotiable Rules

* **Never block UI** (no sync I/O on main loop).
* Use **GLib main loop** only; no alt event loops.
* **No raw libc** where GLib exists (`malloc/strcpy/sprintf/system`, etc.).
* **No global mutable state** (prefer instances; justify singletons).
* **No deprecated APIs** (GTK4/libadwaita only).

## Versioning & Workflow (SemVer)

* PATCH: fixes/refactors/docs · MINOR: features/public API/new strings · MAJOR: breaking/migrations.
* Each change must: bump `project(version:)` in `meson.build`; update `CHANGELOG.md` (date + SemVer); keep AppStream in sync if user-visible.
* Commits: Conventional (`feat:`, `fix:`, `refactor:` …), imperative, ≤72 chars.
* Pre‑commit gate: builds clean, tests pass, static analysis clean/justified, **no leaks**, **no UI blocking**, version + changelog updated.
* Release: CI green → update AppStream notes → sign tag `vX.Y.Z` → attach Flatpak bundle or tarball.

## Build & Tooling

* Meson ≥1.7; pkg-config: `gtk4`, `libadwaita-1`, `libcmark` (opt: `libsoup-3.0`).
* Defaults: `c_std=c23`, `warning_level=2`, `werror=false` (CI enforces `-Werror`), `optimization=2`, PIE, RELRO, canaries.
* Hardening flags: `-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`, link `-Wl,-z,relro -Wl,-z,now -pie`.
* Tools: clang-format (CI), clang-tidy/scan-build (recommended).
* Common commands: `meson setup builddir && meson compile -C builddir && meson test -C builddir --print-errorlogs && meson install -C builddir`.

## Code Standards

* Require `__STDC_VERSION__ == 202311L`; no VLAs.
* Use C23 checked-int macros (`ckd_add`/`ckd_sub`/`ckd_mul`).
* Naming: `snake_case`; lines ≤100 chars; structured logs only (no PII).
* UI single-threaded; CPU/I/O offloaded; marshal back via GLib.
* Errors: explicit codes or `GError **out`; never `exit()` for recoverables.

### Minimal C module template

```c
#include <glib.h>
#include <stdbool.h>

static int helper(void);

int module_init(void) { return 0; }
void module_update(GMainContext *ctx) { (void)ctx; }
void module_shutdown(void) {}
static int helper(void) { return 1; }
```

## UI Patterns (GTK/libadwaita)

* Build UI in **GtkBuilder .ui**; wire logic in C.
* Skeleton: `AdwApplication → AdwApplicationWindow → AdwToolbarView + AdwHeaderBar`.
* Provide accelerators + shortcuts window; respect spacing/typography; primary action right.
* Do not hardcode colors; follow Adwaita.

### Minimal GtkBuilder template

```xml
<interface>
  <requires lib="gtk" version="4.0"/>
  <template class="MyWindow" parent="AdwApplicationWindow">
    <child>
      <object class="AdwToolbarView">
        <child type="top"><object class="AdwHeaderBar" id="header"/></child>
        <child><object class="GtkBox" id="content" orientation="vertical"/></child>
      </object>
    </child>
  </template>
</interface>
```

## I/O & Concurrency

* No sync I/O in UI; use `*_async`/`*_finish` or `GTask`.
* Debounce/coalesce bursty updates.
* Worker threads for heavy CPU; marshal results to main thread.

## Accessibility & i18n

* Meaningful accessible names/roles/relations; verify focus order and visible focus.
* No color‑only cues; meet WCAG AA.
* Wrap strings in `_()`; plurals via `ngettext()`; mark translatables in `.ui`.

## Testing

* GLib test framework; deterministic, headless.
* Cover parsing, transforms, error paths.
* Run ASan/UBSan or Valgrind on changed paths.

## Security

* Use **GIO** for file/process ops; **never** `system()`.
* Use Flatpak portals for file/open‑uri/settings.
* Temp files: `g_file_new_tmp()` / `g_mkstemp_full()`.
* Validate/sanitize all external input; never execute user data.

## Packaging

* Clean build, no warnings; tests pass.
* AppStream + desktop validated (`appstream-cli`); icons/categories correct.
* Tag and publish; attach Flatpak bundle or source tarball.

## MR/PR Checklist (submitter & reviewer)

* HIG followed; GtkBuilder UI; shortcuts discoverable.
* No main‑loop blocking; no new globals; ownership rules respected.
* i18n + accessibility verified.
* AppStream/desktop validation pass; reverse‑DNS app ID consistent.
* Hardening flags intact; **tests pass; no leaks**.
* Changelog/docs updated for user‑visible changes.

## AI Assistant Notes

* Keep diffs minimal and scoped; explain rationale and UI notes in commits.
* Follow GObject ownership conventions.
* Propose tests with features/fixes; add precise TODOs/questions if uncertain.

## Do Not Use

* GTK 3, X11‑only APIs.
* Deprecated GTK4/libadwaita widgets/features.
* Theme overrides fighting Adwaita.
* Raw libc string/memory APIs when GLib alternatives exist.
