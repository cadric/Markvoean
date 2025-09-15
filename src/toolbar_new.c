/* C ULTRA‑MIN TEMPLATE
   Purpose: Toolbar UI component with formatting actions
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/
#include "config.h"
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/toolbar.h>
#include <gtktext/cmrender.h>
#include <gtktext/tag_util.h>

/* ========== META ========== */
/* [0.3.0] - 2025-09-15 - src/toolbar.c
 * Changed: Restructured to follow Ultra-Min template pattern.
 */

/* ========== TYPES ========== */
typedef struct {
    GtkTextView *text_view;
    GtkTextBuffer *buffer;
    GtkHeaderBar *header_bar;
    GtkButton *bold_button;
    GtkButton *italic_button;
    GtkButton *heading_button;
    GtkButton *code_button;
    GtkButton *hr_button;
} ToolbarComponent;

/* ========== STATE ========== */
static ToolbarComponent toolbar_state = { 0 };

/* ========== HELPERS ========== */
static void toolbar_reset(ToolbarComponent *t) {
    g_return_if_fail(t != NULL);
    
    t->text_view = NULL;
    t->buffer = NULL;
    t->header_bar = NULL;
    t->bold_button = NULL;
    t->italic_button = NULL;
    t->heading_button = NULL;
    t->code_button = NULL;
    t->hr_button = NULL;
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
}

/* ========== HANDLERS ========== */
static void on_italic_button_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data) {
    g_autoptr(GError) error = NULL;
    
    if (!validate_text_view(GTK_TEXT_VIEW(user_data), &error)) {
        g_warning("Invalid text view for italic action: %s", error->message);
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    toggle_tag_on_selection(buffer, "italic", "style", GINT_TO_POINTER(PANGO_STYLE_ITALIC));
}

static void on_bold_button_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data) {
    g_autoptr(GError) error = NULL;
    
    if (!validate_text_view(GTK_TEXT_VIEW(user_data), &error)) {
        g_warning("Invalid text view for bold action: %s", error->message);
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    toggle_tag_on_selection(buffer, "bold", "weight", GINT_TO_POINTER(PANGO_WEIGHT_BOLD));
}

static void on_code_button_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data) {
    g_autoptr(GError) error = NULL;
    
    if (!validate_text_view(GTK_TEXT_VIEW(user_data), &error)) {
        g_warning("Invalid text view for code action: %s", error->message);
        return;
    }

    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    toggle_tag_on_selection(buffer, "code", "family", "monospace");
}

static void on_hr_button_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data) {
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
    
    const char *level = g_object_get_data(G_OBJECT(button), "heading-level");
    if (!level) level = "1";
    
    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        g_autofree char *prefix = g_strdup_printf("%s ", level);
        gtk_text_buffer_insert(buffer, &start, prefix, -1);
    }
    
    // Close popover if in one
    GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (popover) {
        gtk_popover_popdown(GTK_POPOVER(popover));
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
    
    for (int i = 0; i < 7; i++) {
        GtkWidget *item = gtk_button_new_with_label(labels[i]);
        gtk_widget_set_size_request(item, 120, -1);
        g_object_set_data(G_OBJECT(item), "heading-level", GINT_TO_POINTER(i));
        g_signal_connect(item, "clicked", G_CALLBACK(on_heading_button_clicked), text_view);
        gtk_box_append(GTK_BOX(box), item);
    }
    
    gtk_popover_set_child(GTK_POPOVER(popover), box);
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
    
    // Create buttons
    toolbar_state.italic_button = GTK_BUTTON(gtk_button_new_with_label("Kursiv"));
    toolbar_state.bold_button = GTK_BUTTON(gtk_button_new_with_label("Fed"));
    toolbar_state.code_button = GTK_BUTTON(gtk_button_new_with_label("Kode"));
    toolbar_state.hr_button = GTK_BUTTON(gtk_button_new_with_label("HR"));
    toolbar_state.heading_button = GTK_BUTTON(gtk_menu_button_new());
    
    gtk_menu_button_set_label(GTK_MENU_BUTTON(toolbar_state.heading_button), "Overskrifter");
    
    // Set tooltips
    gtk_widget_set_tooltip_text(GTK_WIDGET(toolbar_state.italic_button), "Sæt tekst i kursiv");
    gtk_widget_set_tooltip_text(GTK_WIDGET(toolbar_state.bold_button), "Sæt tekst med fed skrift");
    gtk_widget_set_tooltip_text(GTK_WIDGET(toolbar_state.code_button), "Indsæt inline kode eller kodeblok");
    gtk_widget_set_tooltip_text(GTK_WIDGET(toolbar_state.hr_button), "Indsæt horisontal streg");
    gtk_widget_set_tooltip_text(GTK_WIDGET(toolbar_state.heading_button), "Vælg overskriftstype");
    
    // Add buttons to container
    gtk_box_append(GTK_BOX(toolbar_container), GTK_WIDGET(toolbar_state.italic_button));
    gtk_box_append(GTK_BOX(toolbar_container), GTK_WIDGET(toolbar_state.bold_button));
    gtk_box_append(GTK_BOX(toolbar_container), GTK_WIDGET(toolbar_state.code_button));
    gtk_box_append(GTK_BOX(toolbar_container), GTK_WIDGET(toolbar_state.hr_button));
    
    // Add separator
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(separator, 6);
    gtk_widget_set_margin_end(separator, 6);
    gtk_box_append(GTK_BOX(toolbar_container), separator);
    
    gtk_box_append(GTK_BOX(toolbar_container), GTK_WIDGET(toolbar_state.heading_button));
    
    // Connect all signals
    toolbar_connect_signals(&toolbar_state, toolbar_state.text_view);
    setup_heading_menu(GTK_WIDGET(toolbar_state.heading_button), GTK_WIDGET(toolbar_state.text_view));
    
    g_debug("Toolbar created successfully");
    return toolbar_container;
}
