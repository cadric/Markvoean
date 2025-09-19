/* C ULTRA‑MIN TEMPLATE
   Purpose: Toolbar UI component with formatting actions
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#include "config.h"
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/components/toolbar.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/tag_util.h>
#include <gtktext/ui/tab_document.h>
#include <gtktext/ui/tab_manager.h>
#include <gtktext/ui/tab_integration.h>

/* ========== META ========== */
/* [1.0.1] - 2025-09-16 - src/toolbar.c
   MAJOR RELEASE: Updated version for DocumentManager system integration
 * Changed: Restructured to follow Ultra-Min template pattern.
 * [0.3.5] - 2025-09-15 - Fixed heading toolbar buttons to trigger re-rendering
 * [0.3.6] - 2025-09-15 - Fixed "Normal tekst" to remove heading formatting
 * [0.3.7] - 2025-09-15 - Fixed "Normal tekst" to detect heading tags in rendered text
 */

/* ========== TYPES ========== */
typedef enum {
    VIEW_MODE_WYSIWYG,
    VIEW_MODE_SOURCE
} ViewMode;

typedef enum {
    TOOLBAR_STYLE_TEXT,
    TOOLBAR_STYLE_ICONS,
    TOOLBAR_STYLE_BOTH
} ToolbarStyle;

typedef struct {
    const char *id;
    const char *icon_name;
    const char *label;
    const char *tooltip;
} ButtonInfo;

typedef struct {
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
    GtkHeaderBar *header_bar;
    GtkButton *bold_button;
    GtkButton *italic_button;
    GtkMenuButton *heading_button;
    GtkButton *code_button;
    GtkButton *hr_button;
    GtkButton *source_view_button;
    ViewMode current_view_mode;
    GtkTextView *source_text_view;  // Separate text view for source mode
    GtkTextBuffer *source_buffer;   // Separate buffer for source mode
    GtkWidget *scrolled_window;     // Store the scrolled window container
    GtkTextView *original_text_view; // Store original text view reference
    GSettings *settings;             // Settings for toolbar configuration
    ToolbarStyle current_style;      // Current toolbar display style
} ToolbarComponent;

/* ========== STATE ========== */
static ToolbarComponent toolbar_state = { 0 };

/* Button definitions with icons and labels */
static const ButtonInfo button_info[] = {
    { "bold", "format-text-bold-symbolic", "Fed", "Bold text (Ctrl+B)" },
    { "italic", "format-text-italic-symbolic", "Kursiv", "Italic text (Ctrl+I)" },
    { "code", "text-x-generic-symbolic", "Kode", "Insert code (Ctrl+`)" },
    { "heading", "font-select-symbolic", "Overskrifter", "Select heading level" },
    { "hr", "insert-object-symbolic", "HR", "Insert horizontal rule" },
    { "source", "format-text-plaintext-symbolic", "Kilde", "Toggle source view" }
};

/* ========== HELPERS ========== */
static void toolbar_reset(ToolbarComponent *t) {
    g_return_if_fail(t != NULL);
    
    // Clean up any existing source view resources
    if (t->source_text_view && GTK_IS_WIDGET(t->source_text_view)) {
        // Only unparent if it has a parent
        GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(t->source_text_view));
        if (parent) {
            if (GTK_IS_SCROLLED_WINDOW(parent)) {
                gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(parent), NULL);
            } else {
                gtk_widget_unparent(GTK_WIDGET(t->source_text_view));
            }
        }
        // Widget will be automatically destroyed when removed from parent
    }
    
    if (t->source_buffer && G_IS_OBJECT(t->source_buffer)) {
        g_object_unref(t->source_buffer);
    }

    if (t->settings && G_IS_OBJECT(t->settings)) {
        g_object_unref(t->settings);
    }

    t->text_view = NULL;
    t->buffer = NULL;
    t->header_bar = NULL;
    t->bold_button = NULL;
    t->italic_button = NULL;
    t->heading_button = NULL;
    t->code_button = NULL;
    t->hr_button = NULL;
    t->source_view_button = NULL;
    t->current_view_mode = VIEW_MODE_WYSIWYG;
    t->source_text_view = NULL;
    t->source_buffer = NULL;
    t->scrolled_window = NULL;
    t->original_text_view = NULL;
    t->settings = NULL;
    t->current_style = TOOLBAR_STYLE_ICONS;
}

static gboolean validate_text_view(GtkTextView *text_view, GError **error) {
    g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

    if (!GTK_IS_TEXT_VIEW(text_view)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Invalid text view provided");
        return FALSE;
    }
    return TRUE;
}

static ToolbarStyle get_toolbar_style_from_settings(GSettings *settings) {
    g_autofree gchar *style_str = g_settings_get_string(settings, "toolbar-style");

    if (g_strcmp0(style_str, "text") == 0) {
        return TOOLBAR_STYLE_TEXT;
    } else if (g_strcmp0(style_str, "both") == 0) {
        return TOOLBAR_STYLE_BOTH;
    } else {
        return TOOLBAR_STYLE_ICONS; // Default to icons
    }
}

static GtkWidget* create_button_for_style(const ButtonInfo *info, ToolbarStyle style) {
    GtkWidget *button = gtk_button_new();

    switch (style) {
        case TOOLBAR_STYLE_TEXT:
            gtk_button_set_label(GTK_BUTTON(button), info->label);
            break;

        case TOOLBAR_STYLE_ICONS: {
            GtkWidget *icon = gtk_image_new_from_icon_name(info->icon_name);
            gtk_button_set_child(GTK_BUTTON(button), icon);
            break;
        }

        case TOOLBAR_STYLE_BOTH: {
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            GtkWidget *icon = gtk_image_new_from_icon_name(info->icon_name);
            GtkWidget *label = gtk_label_new(info->label);
            gtk_box_append(GTK_BOX(box), icon);
            gtk_box_append(GTK_BOX(box), label);
            gtk_button_set_child(GTK_BUTTON(button), box);
            break;
        }
    }

    // Always set tooltip for accessibility (translate at runtime)
    gtk_widget_set_tooltip_text(button, _(info->tooltip));

    // Set accessible properties (translate at runtime)
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, _(info->tooltip),
                                   -1);

    // Add style classes for proper theming
    gtk_widget_add_css_class(button, "flat");

    return button;
}

static GtkWidget* create_menu_button_for_style(const ButtonInfo *info, ToolbarStyle style) {
    GtkWidget *button = gtk_menu_button_new();

    switch (style) {
        case TOOLBAR_STYLE_TEXT:
            gtk_menu_button_set_label(GTK_MENU_BUTTON(button), info->label);
            break;

        case TOOLBAR_STYLE_ICONS: {
            GtkWidget *icon = gtk_image_new_from_icon_name(info->icon_name);
            gtk_menu_button_set_child(GTK_MENU_BUTTON(button), icon);
            break;
        }

        case TOOLBAR_STYLE_BOTH: {
            GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            GtkWidget *icon = gtk_image_new_from_icon_name(info->icon_name);
            GtkWidget *label = gtk_label_new(info->label);
            gtk_box_append(GTK_BOX(box), icon);
            gtk_box_append(GTK_BOX(box), label);
            gtk_menu_button_set_child(GTK_MENU_BUTTON(button), box);
            break;
        }
    }

    // Always set tooltip (translate at runtime)
    gtk_widget_set_tooltip_text(button, _(info->tooltip));

    // Set accessible properties (translate at runtime)
    gtk_accessible_update_property(GTK_ACCESSIBLE(button),
                                   GTK_ACCESSIBLE_PROPERTY_LABEL, _(info->tooltip),
                                   -1);

    // Add style class
    gtk_widget_add_css_class(button, "flat");

    return button;
}

static GtkTextTag* ensure_tag_exists(GtkTextBuffer *buffer, const char *tag_name, 
                                      const char *property, gpointer value) {
    g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), NULL);
    g_return_val_if_fail(tag_name != NULL, NULL);
    
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *tag = gtk_text_tag_table_lookup(tag_table, tag_name);
    
    if (!tag) {
        if (property) {
            tag = gtk_text_buffer_create_tag(buffer, tag_name, property, value, NULL);
        } else {
            tag = gtk_text_buffer_create_tag(buffer, tag_name, NULL);
        }
        ensure_tag_name_stored(tag, tag_name);
    }
    return tag;
}

static void toggle_tag_on_selection(GtkTextBuffer *buffer, const char *tag_name,
                                   const char *property, gpointer value) {
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));
    g_return_if_fail(tag_name != NULL);
    
    GtkTextIter start, end;
    if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        g_message("%s", _("No text selected for formatting"));
        return;
    }
    
    GtkTextTag *tag = ensure_tag_exists(buffer, tag_name, property, value);
    if (!tag) return;
    
    // Check if entire selection has the tag
    gboolean fully_tagged = TRUE;
    GtkTextIter iter = start;
    while (!gtk_text_iter_equal(&iter, &end)) {
        if (!gtk_text_iter_has_tag(&iter, tag)) {
            fully_tagged = FALSE;
            break;
        }
        gtk_text_iter_forward_char(&iter);
    }
    
    // Toggle: remove if fully tagged, add otherwise
    if (fully_tagged) {
        gtk_text_buffer_remove_tag(buffer, tag, &start, &end);
    } else {
        gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
    }

    // Manually emit "changed" signal to trigger status updates
    // since tag changes don't automatically emit this signal
    g_signal_emit_by_name(buffer, "changed");
}

/* ========== HANDLERS ========== */
static void on_italic_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;  // Unused parameter
    g_autoptr(GError) error = NULL;
    
    if (!validate_text_view(GTK_TEXT_VIEW(user_data), &error)) {
        g_warning("Invalid text view for italic action: %s", error->message);
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    toggle_tag_on_selection(buffer, "italic", "style", GINT_TO_POINTER(PANGO_STYLE_ITALIC));
}

static void on_bold_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;  // Unused parameter
    g_autoptr(GError) error = NULL;
    
    if (!validate_text_view(GTK_TEXT_VIEW(user_data), &error)) {
        g_warning("Invalid text view for bold action: %s", error->message);
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    toggle_tag_on_selection(buffer, "bold", "weight", GINT_TO_POINTER(PANGO_WEIGHT_BOLD));
}

static void on_code_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;  // Unused parameter
    g_autoptr(GError) error = NULL;
    
    if (!validate_text_view(GTK_TEXT_VIEW(user_data), &error)) {
        g_warning("Invalid text view for code action: %s", error->message);
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    toggle_tag_on_selection(buffer, "code", "family", "monospace");
}

static void on_hr_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;  // Unused parameter
    g_autoptr(GError) error = NULL;
    
    if (!validate_text_view(GTK_TEXT_VIEW(user_data), &error)) {
        g_warning("Invalid text view for hr action: %s", error->message);
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    
    // Ensure we're at start of line
    gtk_text_iter_set_line_offset(&cursor, 0);
    gtk_text_buffer_insert(buffer, &cursor, "---\n", -1);
}

static void on_heading_button_clicked(GtkButton *button, gpointer user_data) {
    g_autoptr(GError) error = NULL;
    
    if (!validate_text_view(GTK_TEXT_VIEW(user_data), &error)) {
        g_warning("Invalid text view for heading action: %s", error->message);
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    
    // Get heading level from button data
    const char *level = g_object_get_data(G_OBJECT(button), "heading-level");

    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));

    // Move to start of current line
    gtk_text_iter_set_line_offset(&cursor, 0);

    // Handle "Normal tekst" case (empty level) - remove existing heading formatting
    if (!level || !*level) {

        // Check if the current line has any heading tags
        GtkTextIter line_start = cursor;
        GtkTextIter line_end = cursor;
        gtk_text_iter_set_line_offset(&line_start, 0);
        if (!gtk_text_iter_ends_line(&line_end)) {
            gtk_text_iter_forward_to_line_end(&line_end);
        }

        // Check for heading tags on the current line
        gboolean has_heading = FALSE;

        GtkTextIter check_iter = line_start;
        while (gtk_text_iter_compare(&check_iter, &line_end) < 0) {
            GSList *tags_at_iter = gtk_text_iter_get_tags(&check_iter);
            for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
                GtkTextTag *tag = GTK_TEXT_TAG(l->data);
                gchar *tag_name = NULL;
                g_object_get(tag, "name", &tag_name, NULL);
                if (tag_name) {
                    if (g_strcmp0(tag_name, "h1") == 0 || g_strcmp0(tag_name, "h2") == 0 ||
                        g_strcmp0(tag_name, "h3") == 0 || g_strcmp0(tag_name, "h4") == 0 ||
                        g_strcmp0(tag_name, "h5") == 0 || g_strcmp0(tag_name, "h6") == 0) {
                        has_heading = TRUE;
                    }
                    g_free(tag_name);
                    if (has_heading) break;
                }
            }
            g_slist_free(tags_at_iter);
            if (has_heading) break;
            gtk_text_iter_forward_char(&check_iter);
        }

        if (has_heading) {
            // Get the plain text content and replace the entire line
            g_autofree char *line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);

            // Delete the entire line and insert just the text content
            gtk_text_buffer_delete(buffer, &line_start, &line_end);
            if (line_text && *line_text) {
                gtk_text_buffer_insert(buffer, &line_start, line_text, -1);
            }
        }
    } else {
        // Insert heading prefix
        g_autofree char *prefix = g_strdup_printf("%s ", level);
        gtk_text_buffer_insert(buffer, &cursor, prefix, -1);
    }

    // Trigger re-parsing to apply formatting changes
    GtkTextIter current_pos;
    gtk_text_buffer_get_iter_at_mark(buffer, &current_pos, gtk_text_buffer_get_insert(buffer));
    schedule_reparse_markdown(buffer, 0, &current_pos);

    // Close popover if in one
    GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (popover) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
}

static void on_source_view_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;  // Unused parameter
    (void)user_data;  // Don't use user_data as it might be stale after tab switches

    // Get the active tab document from the application
    GtkWidget *window = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
    if (!window) {
        g_warning("Could not find window for source view toggle");
        return;
    }

    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(window));
    if (!app) {
        g_warning("Could not find application for source view toggle");
        return;
    }

    TabManager *tm = gtktext_get_tab_manager(app);
    if (!tm) {
        g_warning("Could not find tab manager for source view toggle");
        return;
    }

    AdwTabPage *active_page = tab_manager_get_active_tab(tm);
    if (!active_page) {
        g_warning("No active tab for source view toggle");
        return;
    }

    TabDocument *tab_doc = tab_manager_get_tab_document(tm, active_page);
    if (!tab_doc) {
        g_warning("No tab document for source view toggle");
        return;
    }

    // Check if this tab has a text view (welcome tabs don't have one)
    GtkWidget *text_view_widget = tab_document_get_text_view(tab_doc);
    if (!text_view_widget) {
        // Welcome tab or invalid tab - silently ignore source view toggle
        return;
    }

    GtkTextView *original_text_view = GTK_TEXT_VIEW(text_view_widget);
    if (!GTK_IS_TEXT_VIEW(original_text_view)) {
        // Not a valid text view, silently ignore
        return;
    }
    
    if (!tab_document_get_source_mode(tab_doc)) {
        // === SWITCH TO SOURCE VIEW ===
        if (tab_document_switch_to_source_view(tab_doc)) {
            // Update UI state
            GtkWidget *wysiwyg_icon = gtk_image_new_from_icon_name("document-properties-symbolic");
            gtk_button_set_child(GTK_BUTTON(button), wysiwyg_icon);
            gtk_widget_set_tooltip_text(GTK_WIDGET(button), _("Switch to WYSIWYG view"));

            // Disable formatting buttons
            if (toolbar_state.bold_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.bold_button), FALSE);
            if (toolbar_state.italic_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.italic_button), FALSE);
            if (toolbar_state.code_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.code_button), FALSE);
            if (toolbar_state.hr_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.hr_button), FALSE);
            if (toolbar_state.heading_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.heading_button), FALSE);

            g_debug("Switched to source view");
        } else {
            g_warning("Failed to switch to source view");
        }

    } else {
        // === SWITCH BACK TO WYSIWYG VIEW ===
        if (tab_document_switch_to_wysiwyg_view(tab_doc)) {
            // Update UI state
            GtkWidget *source_icon = gtk_image_new_from_icon_name("document-edit-symbolic");
            gtk_button_set_child(GTK_BUTTON(button), source_icon);
            gtk_widget_set_tooltip_text(GTK_WIDGET(button), _("Switch to source view"));

            // Re-enable formatting buttons
            if (toolbar_state.bold_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.bold_button), TRUE);
            if (toolbar_state.italic_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.italic_button), TRUE);
            if (toolbar_state.code_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.code_button), TRUE);
            if (toolbar_state.hr_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.hr_button), TRUE);
            if (toolbar_state.heading_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.heading_button), TRUE);

            g_debug("Switched to WYSIWYG view");
        } else {
            g_warning("Failed to switch to WYSIWYG view");
        }
    }
}

/* ========== WIRING ========== */
static void toolbar_connect_signals(ToolbarComponent *t, GtkTextView *text_view) {
    g_return_if_fail(t != NULL);
    g_return_if_fail(GTK_IS_TEXT_VIEW(text_view));
    
    if (t->italic_button) {
        g_signal_connect(t->italic_button, "clicked", G_CALLBACK(on_italic_button_clicked), text_view);
    }
    if (t->bold_button) {
        g_signal_connect(t->bold_button, "clicked", G_CALLBACK(on_bold_button_clicked), text_view);
    }
    if (t->code_button) {
        g_signal_connect(t->code_button, "clicked", G_CALLBACK(on_code_button_clicked), text_view);
    }
    if (t->hr_button) {
        g_signal_connect(t->hr_button, "clicked", G_CALLBACK(on_hr_button_clicked), text_view);
    }
    if (t->source_view_button) {
        g_signal_connect(t->source_view_button, "clicked", G_CALLBACK(on_source_view_button_clicked), NULL);
    }
}

static void setup_heading_menu(GtkWidget *heading_button, GtkWidget *text_view) {
    g_return_if_fail(GTK_IS_MENU_BUTTON(heading_button));
    g_return_if_fail(GTK_IS_TEXT_VIEW(text_view));
    
    GtkWidget *popover = gtk_popover_new();
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(heading_button), popover);
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    
    const char *labels[] = {"Normal tekst", "Overskrift 1", "Overskrift 2", "Overskrift 3", 
                            "Overskrift 4", "Overskrift 5", "Overskrift 6"};
    const char *heading_levels[] = {"", "#", "##", "###", "####", "#####", "######"};
    
    for (int i = 0; i < 7; i++) {
        GtkWidget *item = gtk_button_new_with_label(labels[i]);
        gtk_widget_set_size_request(item, 120, -1);
        g_object_set_data(G_OBJECT(item), "heading-level", (gpointer)heading_levels[i]);
        g_signal_connect(item, "clicked", G_CALLBACK(on_heading_button_clicked), text_view);
        gtk_box_append(GTK_BOX(box), item);
    }
    
    gtk_popover_set_child(GTK_POPOVER(popover), box);
}

static void on_toolbar_settings_changed(GSettings *settings, gchar *key, gpointer user_data) {
    (void)settings; // Unused but required by signal signature
    (void)user_data; // We'll use the global state directly

    if (g_strcmp0(key, "toolbar-style") == 0 ||
        g_strcmp0(key, "toolbar-customization") == 0 ||
        g_strcmp0(key, "toolbar-button-order") == 0) {

        // Store current text view before rebuilding
        GtkTextView *current_view = toolbar_state.text_view;
        if (current_view) {
            // Recreate toolbar
            create_toolbar(GTK_WIDGET(current_view));
        }
    }
}

/* ========== LIFECYCLE ========== */
GtkWidget* create_toolbar(GtkWidget *text_view) {
    g_autoptr(GError) error = NULL;

    if (!validate_text_view(GTK_TEXT_VIEW(text_view), &error)) {
        g_warning("Invalid text view for toolbar: %s", error->message);
        return NULL;
    }
    
    toolbar_reset(&toolbar_state);
    toolbar_state.text_view = GTK_TEXT_VIEW(text_view);
    toolbar_state.buffer = gtk_text_view_get_buffer(toolbar_state.text_view);
    
    // Find or create toolbar container
    GtkWidget *parent_window = gtk_widget_get_ancestor(text_view, GTK_TYPE_WINDOW);
    GtkWidget *toolbar_container = NULL;

    if (parent_window) {
        GtkBuilder *builder = g_object_get_data(G_OBJECT(parent_window), "builder");
        if (builder) {
            toolbar_container = GTK_WIDGET(gtk_builder_get_object(builder, "toolbar_container"));
        }
    }

    if (!toolbar_container) {
        toolbar_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_add_css_class(toolbar_container, "toolbar");
        g_debug("Created fallback toolbar container");
    }
    
    // Clear existing children
    GtkWidget *child = gtk_widget_get_first_child(toolbar_container);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(toolbar_container), child);
        child = next;
    }
    
    // Get toolbar style from settings (take a reference)
    if (toolbar_state.settings) {
        g_object_unref(toolbar_state.settings);
    }
    toolbar_state.settings = g_settings_new("org.gtk.gtktext");
    toolbar_state.current_style = get_toolbar_style_from_settings(toolbar_state.settings);

    // Get button order if customization is enabled
    gboolean custom_order = g_settings_get_boolean(toolbar_state.settings, "toolbar-customization");
    g_auto(GStrv) button_order = NULL;
    if (custom_order) {
        button_order = g_settings_get_strv(toolbar_state.settings, "toolbar-button-order");
    } else {
        // Use default order
        button_order = g_strsplit("bold,italic,code,heading,hr,source", ",", -1);
    }

    // Create buttons based on current style
    toolbar_state.bold_button = GTK_BUTTON(create_button_for_style(&button_info[0], toolbar_state.current_style));
    toolbar_state.italic_button = GTK_BUTTON(create_button_for_style(&button_info[1], toolbar_state.current_style));
    toolbar_state.code_button = GTK_BUTTON(create_button_for_style(&button_info[2], toolbar_state.current_style));
    toolbar_state.heading_button = GTK_MENU_BUTTON(create_menu_button_for_style(&button_info[3], toolbar_state.current_style));
    toolbar_state.hr_button = GTK_BUTTON(create_button_for_style(&button_info[4], toolbar_state.current_style));
    toolbar_state.source_view_button = GTK_BUTTON(create_button_for_style(&button_info[5], toolbar_state.current_style));

    // Adjust button sizing based on style
    if (toolbar_state.current_style == TOOLBAR_STYLE_ICONS) {
        // Icon-only buttons are square
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.bold_button), 32, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.italic_button), 32, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.code_button), 32, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.heading_button), 32, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.hr_button), 32, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.source_view_button), 32, 32);
    } else if (toolbar_state.current_style == TOOLBAR_STYLE_TEXT) {
        // Text-only buttons need more width
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.bold_button), 50, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.italic_button), 60, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.code_button), 50, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.heading_button), 80, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.hr_button), 40, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.source_view_button), 50, 32);
    } else {
        // Both mode needs the most width
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.bold_button), 70, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.italic_button), 80, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.code_button), 70, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.heading_button), 100, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.hr_button), 60, 32);
        gtk_widget_set_size_request(GTK_WIDGET(toolbar_state.source_view_button), 70, 32);
    }
    
    // Add buttons to container in the specified order
    for (int i = 0; button_order[i] != NULL; i++) {
        GtkWidget *button = NULL;
        const char *btn_id = button_order[i];

        // Add separator before heading button
        if (g_strcmp0(btn_id, "heading") == 0) {
            GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
            gtk_widget_set_margin_start(separator, 6);
            gtk_widget_set_margin_end(separator, 6);
            gtk_box_append(GTK_BOX(toolbar_container), separator);
            gtk_widget_set_visible(separator, TRUE);
        }

        // Add separator before source button
        if (g_strcmp0(btn_id, "source") == 0) {
            GtkWidget *separator2 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
            gtk_widget_set_margin_start(separator2, 6);
            gtk_widget_set_margin_end(separator2, 6);
            gtk_box_append(GTK_BOX(toolbar_container), separator2);
            gtk_widget_set_visible(separator2, TRUE);
        }

        // Get the appropriate button widget
        if (g_strcmp0(btn_id, "bold") == 0) {
            button = GTK_WIDGET(toolbar_state.bold_button);
        } else if (g_strcmp0(btn_id, "italic") == 0) {
            button = GTK_WIDGET(toolbar_state.italic_button);
        } else if (g_strcmp0(btn_id, "code") == 0) {
            button = GTK_WIDGET(toolbar_state.code_button);
        } else if (g_strcmp0(btn_id, "heading") == 0) {
            button = GTK_WIDGET(toolbar_state.heading_button);
        } else if (g_strcmp0(btn_id, "hr") == 0) {
            button = GTK_WIDGET(toolbar_state.hr_button);
        } else if (g_strcmp0(btn_id, "source") == 0) {
            button = GTK_WIDGET(toolbar_state.source_view_button);
        }

        if (button) {
            gtk_box_append(GTK_BOX(toolbar_container), button);
            gtk_widget_set_visible(button, TRUE);
        }
    }
    
    // Connect all signals
    toolbar_connect_signals(&toolbar_state, toolbar_state.text_view);
    setup_heading_menu(GTK_WIDGET(toolbar_state.heading_button), GTK_WIDGET(toolbar_state.text_view));

    // Connect settings change signal
    g_signal_connect(toolbar_state.settings, "changed::toolbar-style",
                     G_CALLBACK(on_toolbar_settings_changed), NULL);
    g_signal_connect(toolbar_state.settings, "changed::toolbar-customization",
                     G_CALLBACK(on_toolbar_settings_changed), NULL);
    g_signal_connect(toolbar_state.settings, "changed::toolbar-button-order",
                     G_CALLBACK(on_toolbar_settings_changed), NULL);

    // Make sure toolbar is visible
    gtk_widget_set_visible(toolbar_container, TRUE);

    g_debug("Toolbar created successfully and made visible");
    return toolbar_container;
}

void toolbar_update_text_view(GtkWidget *new_text_view)
{
    if (!new_text_view || !GTK_IS_TEXT_VIEW(new_text_view)) {
        g_warning("Invalid text view passed to toolbar_update_text_view: %p", new_text_view);
        return;
    }

    if (!toolbar_state.text_view) {
        g_warning("Toolbar not initialized, cannot update text view");
        return;
    }

    // Don't update if it's the same text view
    if (toolbar_state.text_view == GTK_TEXT_VIEW(new_text_view)) {
        return;
    }

    g_debug("Updating toolbar text view from %p to %p", toolbar_state.text_view, new_text_view);

    // Disconnect old signal handlers
    if (toolbar_state.italic_button) {
        g_signal_handlers_disconnect_by_func(toolbar_state.italic_button, on_italic_button_clicked, toolbar_state.text_view);
    }
    if (toolbar_state.bold_button) {
        g_signal_handlers_disconnect_by_func(toolbar_state.bold_button, on_bold_button_clicked, toolbar_state.text_view);
    }
    if (toolbar_state.code_button) {
        g_signal_handlers_disconnect_by_func(toolbar_state.code_button, on_code_button_clicked, toolbar_state.text_view);
    }
    if (toolbar_state.hr_button) {
        g_signal_handlers_disconnect_by_func(toolbar_state.hr_button, on_hr_button_clicked, toolbar_state.text_view);
    }
    if (toolbar_state.source_view_button) {
        g_signal_handlers_disconnect_by_func(toolbar_state.source_view_button, on_source_view_button_clicked, NULL);
    }

    // Update the toolbar state
    toolbar_state.text_view = GTK_TEXT_VIEW(new_text_view);
    toolbar_state.buffer = gtk_text_view_get_buffer(toolbar_state.text_view);

    // Get the tab document to sync with its view mode
    GtkWidget *window = gtk_widget_get_ancestor(new_text_view, GTK_TYPE_WINDOW);
    if (window) {
        GtkApplication *app = gtk_window_get_application(GTK_WINDOW(window));
        if (app) {
            TabManager *tm = gtktext_get_tab_manager(app);
            if (tm) {
                AdwTabPage *active_page = tab_manager_get_active_tab(tm);
                if (active_page) {
                    TabDocument *tab_doc = tab_manager_get_tab_document(tm, active_page);
                    if (tab_doc) {
                        // Sync toolbar UI with tab's current view mode
                        gboolean is_source_mode = tab_document_get_source_mode(tab_doc);
                        if (is_source_mode) {
                            // Tab is in source mode - update toolbar to reflect this
                            if (toolbar_state.source_view_button) {
                                GtkWidget *wysiwyg_icon = gtk_image_new_from_icon_name("document-properties-symbolic");
                                gtk_button_set_child(toolbar_state.source_view_button, wysiwyg_icon);
                                gtk_widget_set_tooltip_text(GTK_WIDGET(toolbar_state.source_view_button),
                                                           _("Switch to WYSIWYG view"));
                            }
                            // Disable formatting buttons
                            if (toolbar_state.bold_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.bold_button), FALSE);
                            if (toolbar_state.italic_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.italic_button), FALSE);
                            if (toolbar_state.code_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.code_button), FALSE);
                            if (toolbar_state.hr_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.hr_button), FALSE);
                            if (toolbar_state.heading_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.heading_button), FALSE);
                        } else {
                            // Tab is in WYSIWYG mode - update toolbar to reflect this
                            if (toolbar_state.source_view_button) {
                                GtkWidget *source_icon = gtk_image_new_from_icon_name("document-edit-symbolic");
                                gtk_button_set_child(toolbar_state.source_view_button, source_icon);
                                gtk_widget_set_tooltip_text(GTK_WIDGET(toolbar_state.source_view_button),
                                                           _("Switch to source view"));
                            }
                            // Enable formatting buttons
                            if (toolbar_state.bold_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.bold_button), TRUE);
                            if (toolbar_state.italic_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.italic_button), TRUE);
                            if (toolbar_state.code_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.code_button), TRUE);
                            if (toolbar_state.hr_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.hr_button), TRUE);
                            if (toolbar_state.heading_button) gtk_widget_set_sensitive(GTK_WIDGET(toolbar_state.heading_button), TRUE);
                        }
                    }
                }
            }
        }
    }

    // Reconnect signals with new text view
    toolbar_connect_signals(&toolbar_state, toolbar_state.text_view);

    // Also need to update the heading menu - recreate it for the new text view
    if (toolbar_state.heading_button) {
        // Clear old popover
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(toolbar_state.heading_button), NULL);
        // Setup new one
        setup_heading_menu(GTK_WIDGET(toolbar_state.heading_button), GTK_WIDGET(toolbar_state.text_view));
    }

    g_debug("Toolbar updated to new text view and signals reconnected");
}
