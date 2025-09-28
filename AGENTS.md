## 📋 PRE‑EDIT CHECKLIST

**Mandatory before any change**

- Read this file end‑to‑end.
- Build, run, test locally.
- No main‑loop blocking, no new warnings.
- Update version + changelog.
- KISS principle

---

## 🧭 Guiding Principles

1. **Simplicity**: idiomatic C23 + GLib/GObject only.
2. **GNOME‑first UX**: HIG + libadwaita patterns.
3. **Safety**: explicit ownership, predictable lifetimes, hardened builds.
4. **Modern only**: GTK 4, Wayland focus, Flatpak‑friendly.
5. **Accessibility**: keyboard, AT‑SPI, contrast, i18n.
6. **Documentation**: Keep docs current and accurate.
---

## ⚠️ Prohibited Practices

- ❌ Block UI thread (no sync I/O).
- ❌ Leak refs or memory (always unref/free).
- ❌ Raw libc when GLib exists (`malloc/strcpy/sprintf/system`).
- ❌ Override Adwaita theme.
- ❌ Deprecated APIs (check GTK4/libadwaita docs).
- ❌ Non‑GLib event loops (use GLib main loop).
- ❌ Global mutable state (prefer instances; justify singletons).

---

## 🔄 Version Control & Workflow

**SemVer** in `meson.build` → `project(version:)`; mirror in AppStream.

**Bumps**: PATCH = fixes/refactors/docs; MINOR = features/public API/new strings; MAJOR = breaking API/migrations/removed features. Default to PATCH.

**Artifacts per change**

1. Update `meson.build` version. 2) Update `CHANGELOG.md` (date + SemVer). 3) Per‑file header note. 4) If schemas: bump, recompile, note migration. 5) If UI changed: short before/after note or screenshot link. 6) If new features: update AppStream metainfo.xml releases section.

**Commit style**: Conventional Commits (`feat:`, `fix:`, `refactor:`, `perf:`, `docs:`, `test:`, `build:`, `ci:`, `chore:`). Imperative, ≤72 chars.

**Pre‑commit**: builds clean, tests pass, static analysis clean or justified, no UI blocking, no leaks (ASan/Valgrind on touched paths), version + changelog updated.

**Release**: CI green → update AppStream notes → tag `vX.Y.Z` (signed) → produce Flatpak bundle or tarball and attach.

---

## 📐 Code Standards (C, GLib, GObject)

### C23 (GLib/GObject)

* Require `__STDC_VERSION__ == 202311L`. Use `-std=c23` or `-std=gnu23`.
* No VLAs on stack; use fixed sizes or heap.
* Use C23 checked integer macros (`ckd_add`, `ckd_sub`, `ckd_mul`).
* Structured logs only. No PII.
* Single‑threaded UI; I/O may be threaded.
* `snake_case`, lines ≤ 100 chars.
* Event loop: GLib main loop (`GMainContext`/`GMainLoop`); no libuv/libevent.
* Error handling: explicit codes or GError‑style out params. No `exit()` on recoverable errors.
* Hardening: debug with ASan/UBSan; release with PIE, full RELRO, stack canaries, optional LTO.

**Minimal C module**

```c
/*
 * Module: [module_name.c]
 * Purpose: [One-line description]
 */
#include <glib.h>
#include <stddef.h>
#include <stdbool.h>

static int helper_function(void);

int module_init(void) { return 0; }

void module_update(GMainContext *context) {
  (void)context;
}

void module_shutdown(void) {}

static int helper_function(void) { return 1; }
```

---

## 🎨 UI, HIG, libadwaita

- Build UI with GtkBuilder .ui; wire logic in C.
- Base: AdwApplication → AdwApplicationWindow → AdwToolbarView + AdwHeaderBar.
- Respect platform spacing/typography; primary action right, cancel left.
- Provide accelerators + shortcuts window.
- Support dark mode automatically; never hardcode palette.

### GtkBuilder ultra‑min

```xml
<interface>
  <requires lib="gtk" version="4.0"/>
  <template class="MyWindow" parent="AdwApplicationWindow">
    <child><object class="AdwToolbarView">
      <child type="top"><object class="AdwHeaderBar" id="header"/></child>
      <child><object class="GtkBox" id="content" orientation="vertical"/></child>
    </object></child>
  </template>
</interface>
```

---

## ♿ Accessibility & i18n

- Accessible names/roles/relations; update via `gtk_accessible_update_property()`.
- Full keyboard navigation; verify focus order and visible focus.
- Avoid color‑only cues; meet WCAG AA contrast.
- Wrap user strings in `_()`; use `ngettext()` for plurals.
- Mark translatable strings in `.ui` files.

---

## ⚙️ Build & Tooling

- Build: Meson 1.7+ + Ninja. pkg-config: `gtk4`, `libadwaita-1`, `libcmark`; optional `libsoup-3.0`.
- Defaults: `c_std=c23`, `warning_level=2`, `werror=false` (CI enforces `-Werror`), `optimization=2`, `b_pie=true`, `b_lto=false`, `buildtype=release`.
- Hardening: `-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`, link `-Wl,-z,relro -Wl,-z,now -pie`; define `HAVE_CONFIG_H`.
- Analysis: clang-tidy, scan-build, optional cppcheck.
- Format: clang-format enforced in CI.

### Commands

```bash
meson setup builddir
meson compile -C builddir
meson test -C builddir --print-errorlogs
meson install -C builddir
```

### Meson snippet

```meson
project('gtktext','c',
  version: '2.5.11',
  meson_version: '>= 1.7.2',
  default_options: [
    'c_std=c23',
    'warning_level=2',
    'werror=false',
    'optimization=2',
    'b_pie=true',
    'b_lto=false',
    'buildtype=release'
  ]
)

add_project_arguments([
  '-DHAVE_CONFIG_H',
  '-D_FORTIFY_SOURCE=2',
  '-fstack-protector-strong'
], language: 'c')

add_project_link_arguments([
  '-Wl,-z,relro',
  '-Wl,-z,now',
  '-pie'
], language: 'c')
```

---

## 🔌 I/O & Concurrency

- No sync I/O in UI code; use `*_async`/`*_finish` or `GTask`.
- Debounce bursty handlers; coalesce updates.
- Worker threads for CPU heavy; marshal back to main thread.

---

## 🧪 Testing

- GLib test framework; deterministic, headless.
- Test parsing, transforms, error paths.
- Run ASan/UBSan or Valgrind on changed paths.
- CI: `meson test --print-errorlogs`.

---

## 🔒 Security

- Use GIO for file/process ops; never `system()` in core.
- Use Flatpak portals for file/open‑uri/settings.
- Create temp files via `g_file_new_tmp()`/`g_mkstemp_full()`.
- Validate/sanitize all external input; never execute user data.
- For tests, set `GIO_USE_VFS` as needed; avoid host paths in Flatpak.
---
## 📦 Packaging & Releases

1. Clean build, no warnings, tests pass. 2) Update AppStream notes and desktop file if needed. 3) Validate with appstream‑cli; verify icons/categories. 4) Tag and publish; attach Flatpak bundle or source tarball.

---

## ✅ MR/PR Review Checklist

- HIG followed; GtkBuilder UI.
- No main‑loop blocking; no new global mutable state.
- Shortcuts present and discoverable.
- i18n + accessibility verified.
- AppStream metainfo.xml and desktop file validation pass.
- Consistent reverse-DNS application ID across all files.
- Hardening flags intact; no new warnings; tests pass; no leaks.
- Docs + changelog updated for user‑visible changes.

---

## 🧭 AI Assistant Playbook

- Obey this file; keep diffs minimal and scoped.
- Explain changes in commits; include rationale and UI notes.
- Follow GObject ownership rules.
- Propose tests with features/fixes.
- If uncertain, add a precise TODO or question in MR.

---

## 🗑️ Deprecated / Do Not Use

- GTK 3 or X11‑only APIs.
- Deprecated GTK 4 widgets or libadwaita features.
- Direct theme overrides that fight Adwaita.
- Raw libc string/memory APIs where GLib offers safer options.
