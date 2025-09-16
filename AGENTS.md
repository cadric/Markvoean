## 📋 PRE‑EDIT CHECKLIST

**Mandatory before any change**

* Read this file end‑to‑end.
* Build, run, test locally.
* No main‑loop blocking, no new warnings.
* Update version + changelog.

---

## 🧭 Guiding Principles

1. **Simplicity**: idiomatic C + GLib/GObject only.
2. **GNOME‑first UX**: HIG + libadwaita patterns.
3. **Safety**: explicit ownership, predictable lifetimes, hardened builds.
4. **Modern only**: GTK 4, Wayland focus, Flatpak‑friendly.
5. **Accessibility**: keyboard, AT‑SPI, contrast, i18n.

---

## ⚠️ Prohibited Practices

* ❌ Block UI thread (no sync I/O).
* ❌ Leak refs or memory (always unref/free).
* ❌ Raw libc when GLib exists (`malloc/strcpy/sprintf/system`).
* ❌ Override Adwaita theme.
* ❌ Deprecated APIs (check GTK4/libadwaita docs).
* ❌ Global mutable state (prefer instances; justify singletons).

---

## 🔄 Version Control & Workflow

**SemVer** in `meson.build` → `project(version:)`; mirror in AppStream.

**Bumps**: PATCH = fixes/refactors/docs; MINOR = features/public API/new strings; MAJOR = breaking API/migrations/removed features. Default to PATCH.

**Artifacts per change**

1. Update `meson.build` version. 2) Update `CHANGELOG.md` (date + SemVer). 3) Per‑file header note. 4) If schemas: bump, recompile, note migration. 5) If UI changed: short before/after note or screenshot link.

**Commit style**: Conventional Commits (`feat:`, `fix:`, `refactor:`, `perf:`, `docs:`, `test:`, `build:`, `ci:`, `chore:`). Imperative, ≤72 chars.

**Pre‑commit**: builds clean, tests pass, static analysis clean or justified, no UI blocking, no leaks (ASan/Valgrind on touched paths), version + changelog updated.

**Release**: CI green → update AppStream notes → tag `vX.Y.Z` (signed) → produce Flatpak bundle or tarball and attach.

---

## 📐 Code Standards (C, GLib, GObject)

* **Language**: C17 (C11 ok). Prefer clarity over cleverness.
* **Headers**: public API in `include/`. Internal functions `static`.
* **Naming**: `snake_case` for funcs/vars; `TypeName` for GTypes.
* **Preconditions**: `g_return_*_if_fail()` in public APIs.
* **Types**: `G_DECLARE_FINAL_TYPE` + `G_DEFINE_TYPE` for classes.
* **Ownership**: document transfer; use `g_autoptr`, `g_autofree`, `g_auto(GStrv)`.
* **Signals**: store handler IDs; disconnect on teardown.
* **Logging**: `g_debug/g_message/g_warning/g_critical` (no `printf`).
* **Threading**: UI only on main thread; use `GTask`/workers for work.
* **Errors**: `GError**` for recoverable failures; never `exit()` for normal errors.

**Ultra‑Min C Module Template (mandatory at top)**

```c
/* C ULTRA‑MIN TEMPLATE
   Purpose: [short]
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#include "config.h" // if present
#include <adwaita.h>

typedef struct { /* state */ } Component;

static void component_reset(Component *c) { /* cleanup */ }

static gboolean do_something(Component *c, GError **err) {
  g_return_val_if_fail(c != NULL, FALSE); return TRUE;
}

static void on_action(GSimpleAction *a, GVariant *p, gpointer u) { }

void component_connect(GtkWidget *root) { /* g_signal_connect(...) */ }

void component_init(GtkWidget *root) { /* alloc/bind/connect */ }
void component_teardown(GtkWidget *root) { /* disconnect/free */ }
```

**Crash policy (debug)**

```c
static void crit_abort(const gchar *d, GLogLevelFlags l, const gchar *m, gpointer x){
  g_log_default_handler(d,l,m,x);#ifndef NDEBUG
  g_abort();#endif
}
static void setup_logging(void){
  g_log_set_handler(NULL, G_LOG_LEVEL_CRITICAL|G_LOG_FLAG_FATAL, crit_abort, NULL);
}
```

---

## 🎨 UI, HIG, libadwaita

* Build UI with GtkBuilder `.ui`; wire logic in C.
* Base: `AdwApplication` → `AdwApplicationWindow` → `AdwToolbarView` + `AdwHeaderBar`.
* Respect platform spacing/typography; primary action right, cancel left.
* Provide accelerators + shortcuts window.
* Support dark mode automatically; never hardcode palette.

**GtkBuilder ultra‑min**

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

* Accessible names/roles/relations; update via `gtk_accessible_update_property()`.
* Full keyboard navigation; verify focus order and visible focus.
* Avoid color‑only cues; meet WCAG AA contrast.
* Wrap user strings in `_()`; use `ngettext()` for plurals.
* Mark translatable strings in `.ui` files.

---

## ⚙️ Build & Tooling

* **Build**: Meson + Ninja. `pkg-config`: `gtk4`, `libadwaita-1`, others as needed.
* **Warnings**: `-Wall -Wextra`; Werror in CI.
* **Hardening**: `-O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE` + link `-Wl,-z,relro -Wl,-z,now -pie`.
* **Analysis**: `clang-tidy`, `scan-build`, optional `cppcheck`.
* **Format**: `clang-format` enforced in CI.

**Meson snippet**

```meson
project('app','c',version:'1.2.3',default_options:['warning_level=3'])
add_project_arguments('-D_FORTIFY_SOURCE=2',language:'c')
add_project_link_arguments('-Wl,-z,relro','-Wl,-z,now',language:'c')
```

---

## 🔌 I/O & Concurrency

* No sync I/O in UI code; use `*_async`/`*_finish` or `GTask`.
* Debounce bursty handlers; coalesce updates.
* Worker threads for CPU heavy; marshal back to main thread.

---

## 🧪 Testing

* GLib test framework; deterministic, headless.
* Test parsing, transforms, error paths.
* Run ASan/UBSan or Valgrind on changed paths.
* CI: `meson test --print-errorlogs`.

---

## 🔒 Security

* Use GIO for file/process ops; never `system()` in core.
* Use Flatpak portals for file/open‑uri/settings.
* Create temp files via `g_file_new_tmp()`/`g_mkstemp_full()`.
* Validate/sanitize all external input; never execute user data.
* For tests, set `GIO_USE_VFS` as needed; avoid host paths in Flatpak.

---

## 📦 Packaging & Releases

1. Clean build, no warnings, tests pass. 2) Update AppStream notes and desktop file if needed. 3) Validate with `appstream‑cli`; verify icons/categories. 4) Tag and publish; attach Flatpak bundle or source tarball.

---

## ✅ MR/PR Review Checklist

* HIG followed; GtkBuilder UI.
* No main‑loop blocking; no new global mutable state.
* Shortcuts present and discoverable.
* i18n + accessibility verified.
* Hardening flags intact; no new warnings; tests pass; no leaks.
* Docs + changelog updated for user‑visible changes.

---

## 🧭 AI Assistant Playbook

* Obey this file; keep diffs minimal and scoped.
* Explain changes in commits; include rationale and UI notes.
* Follow GObject ownership rules.
* Propose tests with features/fixes.
* If uncertain, add a precise TODO or question in MR.

---

## 🗑️ Deprecated / Do Not Use

* GTK 3 or X11‑only APIs.
* Deprecated GTK 4 widgets or libadwaita features.
* Direct theme overrides that fight Adwaita.
* Raw libc string/memory APIs where GLib offers safer options.
