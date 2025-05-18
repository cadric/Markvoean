# GitHub Copilot Guidelines for C + GTK4/libadwaita

## General Requirements

### C Requirements
- **Standard:** Use C99 or newer (C11 recommended for _Thread-safety).
- Use `gcc` or `clang` with strict error checking flags like `-Wall -Wextra -Werror`.
- Use `clang-format` and `clang-tidy` for formatting and static analysis.
- Utilize `valgrind` or similar tools to detect memory leaks.
- **Avoid** global variables; use functions with clear interfaces and parameters.
- Prefer `const` and `static` to optimize and secure data access.
- Use `enum` and `typedef` for more readable code (no "magic numbers").
- Always check return values from functions, especially from GTK/Glib/Adwaita APIs.
- Use GObject-based programming to leverage the GTK object model.
- Document functions and structures with comments (`/** */` or `//`).

### GTK4 and libadwaita Guidelines
- Use **GTK4** and **libadwaita** for modern GNOME UI components.
- Prefer the **builder API** (`GtkBuilder`) or `.ui` files for UI layout.
- Use **AdwaitaWidgets** for modern GNOME integration (e.g., `AdwApplication`, `AdwHeaderBar`).
- Separate UI logic from application logic (MVC/MVP patterns recommended).
- Connect signals with `g_signal_connect` and use `GWeakRef` or `g_object_ref_sink` to avoid memory leaks.
- Use `gtk_widget_set_parent()` correctly to ensure widget hierarchy and avoid dangling references.
- Implement custom widgets by subclassing `GObject` or `GtkWidget`.
- Use `G_DECLARE_FINAL_TYPE` and `G_DEFINE_TYPE` for new objects to minimize boilerplate code.
- Use Wayland-specific features if relevant (e.g., `gdk_wayland_*` APIs).

### Accessibility
- Use GTK4's built-in accessible roles and properties (`atk_object_set_role`).
- Set labels and descriptions for all interactive widgets (`gtk_widget_set_accessible_*`).
- Test the application in high-contrast mode and with keyboard navigation.

### Internationalization
- Use `gettext` or `glib`'s internationalization tools (`_()` macros).
- Ensure all interface texts are translatable.
- Avoid hardcoding visible texts in the source code.

### Error Handling
- Use `GError` for error reporting in Glib/GTK-based applications.
- Ensure all errors are properly handled and displayed in the UI via dialogs.
- Avoid using `exit(1)` or `abort()` to terminate the application; handle errors gracefully.

### Testing and Debugging
- Use `glib`'s testing framework (`g_test_add_func`) for unit testing.
- Test UI logic separately from non-UI logic.
- Use `g_message`, `g_warning`, and `g_error` for standard debug logging.
- Integrate debugging tools like `gdb` and `valgrind` into your development process.

---

## Security Measures
- Avoid uncontrolled use of `malloc` and `free`; use `g_malloc` and `g_free` from Glib.
- Avoid `strncpy` and `strcpy` without proper buffer control.
- Avoid `sprintf`; use `snprintf` or `g_strdup_printf`.
- Sanitize all user input (files, commands, etc.).
- Use Glib's data types (e.g., `GList`, `GHashTable`) instead of raw pointer structures.
- Avoid unnecessary use of **unsafe** C code such as direct pointer manipulation.

---

## Dependency Management
- Use `pkg-config` to fetch correct flags and dependencies (`pkg-config --cflags --libs gtk4 libadwaita`).
- Ensure all dependencies are up-to-date.
- Document dependency versions in `README.md` or similar.

---

## Style & Quality
- Use `clang-format` with a consistent style configuration.
- Keep functions short and focused (max 50 lines recommended).
- Use meaningful names for variables, functions, and types.
- Avoid long `if`-else chains; use `switch-case` or strategy patterns.
- Comment on complex algorithms and decisions.
- Ensure code is readable and easy to maintain.

---

## Miscellaneous
- Optimize for Wayland: Use `GDK_BACKEND=wayland` during testing.
- Ensure the application follows GNOME HIG (Human Interface Guidelines).
- Use `GMainLoop` and `g_idle_add` for asynchronous tasks.
- Avoid global mutable variables; use dependency injection or context structures.