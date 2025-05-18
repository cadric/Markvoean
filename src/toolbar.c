#include <gtk/gtk.h>
#include "toolbar.h"
#include "gtktext_cmark.h"

/* Knap callbacks */
static void on_italic_button_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter start, end;
    
    // Få fat i tag-tabellen
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *italic_tag = gtk_text_tag_table_lookup(tag_table, "italic");
    
    // Opret tag hvis det ikke findes
    if (!italic_tag) {
        italic_tag = gtk_text_buffer_create_tag(buffer, "italic", 
                                            "style", PANGO_STYLE_ITALIC, 
                                            NULL);
    }
    
    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        // Tjek om hele markeringen allerede er i kursiv
        gboolean fully_italic = TRUE;
        GtkTextIter iter = start;
        while (!gtk_text_iter_equal(&iter, &end)) {
            if (!gtk_text_iter_has_tag(&iter, italic_tag)) {
                fully_italic = FALSE;
                break;
            }
            gtk_text_iter_forward_char(&iter);
        }
        
        // Toggling: Hvis hele markeringen er kursiv, fjern det. Ellers tilføj det.
        if (fully_italic) {
            gtk_text_buffer_remove_tag(buffer, italic_tag, &start, &end);
        } else {
            gtk_text_buffer_apply_tag(buffer, italic_tag, &start, &end);
        }
    } else {
        g_print("Ingen tekst markeret for kursiv\n");
    }
}

static void on_bold_button_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter start, end;
    
    // Få fat i tag-tabellen
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *bold_tag = gtk_text_tag_table_lookup(tag_table, "bold");
    
    // Opret tag hvis det ikke findes
    if (!bold_tag) {
        bold_tag = gtk_text_buffer_create_tag(buffer, "bold", 
                                          "weight", PANGO_WEIGHT_BOLD, 
                                          NULL);
    }
    
    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        // Tjek om hele markeringen allerede er fed
        gboolean fully_bold = TRUE;
        GtkTextIter iter = start;
        while (!gtk_text_iter_equal(&iter, &end)) {
            if (!gtk_text_iter_has_tag(&iter, bold_tag)) {
                fully_bold = FALSE;
                break;
            }
            gtk_text_iter_forward_char(&iter);
        }
        
        // Toggling: Hvis hele markeringen er fed, fjern det. Ellers tilføj det.
        if (fully_bold) {
            gtk_text_buffer_remove_tag(buffer, bold_tag, &start, &end);
        } else {
            gtk_text_buffer_apply_tag(buffer, bold_tag, &start, &end);
        }
    } else {
        g_print("Ingen tekst markeret for fed skrift\n");
    }
}

static void on_hr_button_clicked(G_GNUC_UNUSED GtkButton *button, gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter insert;
    GtkTextMark *cursor_mark;
    
    // Få fat i tag-tabellen
    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
    GtkTextTag *hr_tag = gtk_text_tag_table_lookup(tag_table, "hr");
    
    // Opret tag hvis det ikke findes
    if (!hr_tag) {
        hr_tag = gtk_text_buffer_create_tag(buffer, "hr", 
                                         "paragraph-background", "#ddd",
                                         "paragraph-background-set", TRUE,
                                         "pixels-above-lines", 10,
                                         "pixels-below-lines", 10,
                                         NULL);
    }
    
    // Få current cursor position mark
    cursor_mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_buffer_get_iter_at_mark(buffer, &insert, cursor_mark);
    
    // Sikre at vi indsætter på en ny linie
    if (!gtk_text_iter_starts_line(&insert)) {
        gtk_text_buffer_insert(buffer, &insert, "\n", 1);
        // Opdater iterator efter indsætning
        gtk_text_buffer_get_iter_at_mark(buffer, &insert, cursor_mark);
    }
    
    // Tilføj en tekstmark på starten af HR-linjen
    GtkTextMark *start_mark = gtk_text_buffer_create_mark(buffer, NULL, &insert, TRUE);
    
    // Indsæt horizontal rule markør
    gtk_text_buffer_insert(buffer, &insert, "---", 3);
    
    // Få start og slut iteratorer ved hjælp af marks
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_mark(buffer, &start, start_mark);
    end = insert;  // Den nuværende position efter indsætning
    
    // Anvend tag mellem de opdaterede iteratorer
    gtk_text_buffer_apply_tag(buffer, hr_tag, &start, &end);
    
    // Indsæt en ny linie efter HR
    gtk_text_buffer_insert(buffer, &insert, "\n", 1);
    
    // Fjern det midlertidige mark
    gtk_text_buffer_delete_mark(buffer, start_mark);
}

static void on_heading_button_clicked(GtkButton *button, gpointer user_data) {
    // Find heading level fra knappen
    int level = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "heading-level"));
    
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter start, end;
    
    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) {
        // Fjern alle eksisterende heading tags først
        GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);
        for (int i = 1; i <= 6; i++) {
            char tag_name[3];
            g_snprintf(tag_name, sizeof(tag_name), "h%d", i);
            
            GtkTextTag *heading_tag = gtk_text_tag_table_lookup(tag_table, tag_name);
            if (heading_tag) {
                gtk_text_buffer_remove_tag(buffer, heading_tag, &start, &end);
            }
        }
        
        // Anvend nyt tag hvis level > 0
        if (level > 0) {
            char tag_name[3];
            g_snprintf(tag_name, sizeof(tag_name), "h%d", level);
            
            GtkTextTag *heading_tag = gtk_text_tag_table_lookup(tag_table, tag_name);
            if (!heading_tag) {
                // Calculate size based on heading level (larger for H1, smaller for H6)
                double size_factor = 2.0 - ((level - 1) * 0.2);
                
                heading_tag = gtk_text_buffer_create_tag(buffer, tag_name,
                                                      "weight", PANGO_WEIGHT_BOLD,
                                                      "scale", size_factor,
                                                      NULL);
            }
            
            // Anvend tag
            gtk_text_buffer_apply_tag(buffer, heading_tag, &start, &end);
            g_print("Anvendt h%d formatering\n", level);
        }
    } else {
        g_print("Ingen tekst markeret for overskrift\n");
    }
    
    // Gem popup hvis vi er i et popover menu
    GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (popover) {
        gtk_popover_popdown(GTK_POPOVER(popover));
    }
}

static void setup_heading_menu(GtkWidget *heading_button, GtkWidget *text_view) {
    // Opret popover menu til heading styles
    GtkWidget *popover = gtk_popover_new();
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(heading_button), popover);
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    
    // Tilføj muligheder for overskrifter og normal tekst
    const char *labels[] = {"Normal tekst", "Overskrift 1", "Overskrift 2", "Overskrift 3", 
                            "Overskrift 4", "Overskrift 5", "Overskrift 6"};
    
    for (int i = 0; i < 7; i++) {
        GtkWidget *item = gtk_button_new_with_label(labels[i]);
        gtk_widget_set_size_request(item, 120, -1);  // Fast bredde
        
        // Gem heading level som data på knappen (0=Normal, 1=H1, osv.)
        g_object_set_data(G_OBJECT(item), "heading-level", GINT_TO_POINTER(i));
        
        g_signal_connect(item, "clicked", G_CALLBACK(on_heading_button_clicked), text_view);
        gtk_box_append(GTK_BOX(box), item);
    }
    
    gtk_popover_set_child(GTK_POPOVER(popover), box);
}

/**
 * Create a toolbar with formatting options for the text editor
 * 
 * @param text_view The GtkTextView that will be affected by toolbar actions
 * @return The toolbar container widget
 */
GtkWidget* create_toolbar(GtkWidget *text_view) {
    GtkWidget *parent_window = gtk_widget_get_ancestor(text_view, GTK_TYPE_WINDOW);
    GtkWidget *toolbar_container = NULL;
    
    // Find toolbar_container fra UI
    if (parent_window) {
        GtkBuilder *builder = g_object_get_data(G_OBJECT(parent_window), "builder");
        
        if (builder) {
            // Forsøg at finde toolbar_container via builder
            toolbar_container = GTK_WIDGET(gtk_builder_get_object(builder, "toolbar_container"));
            
            if (toolbar_container) {
                g_print("Fandt toolbar_container fra UI\n");
            } else {
                // Hvis vi ikke kan finde containeren, forsøg at finde den på en anden måde
                toolbar_container = GTK_WIDGET(gtk_widget_get_first_child(
                    gtk_widget_get_first_child(parent_window)));
                
                if (GTK_IS_BOX(toolbar_container) && 
                    gtk_widget_has_css_class(toolbar_container, "toolbar")) {
                    g_print("Fandt toolbar_container via widget hierarki\n");
                } else {
                    toolbar_container = NULL;
                    g_warning("Kunne ikke finde toolbar_container i UI\n");
                }
            }
        } else {
            g_warning("Ingen builder fundet i parent_window\n");
        }
    } else {
        g_warning("Intet parent_window fundet for text_view\n");
    }
    
    // Hvis vi stadig ikke har en container, opret et fallback
    if (!toolbar_container) {
        toolbar_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_add_css_class(toolbar_container, "toolbar");
        
        // Tilføj containeren til vinduet
        if (parent_window) {
            // I GTK4, kan vi ikke direkte få content_area fra et window
            // I stedet kan vi finde den første child og se om det har den struktur vi forventer
            GtkWidget *first_child = gtk_widget_get_first_child(parent_window);
            
            if (GTK_IS_BOX(first_child)) {
                // Det første barn kan være AdwToolbarView eller en box
                GtkWidget *box_container = first_child;
                
                // Hvis det er AdwToolbarView, bør vi få fat i dens box
                if (g_type_is_a(G_TYPE_FROM_INSTANCE(first_child), g_type_from_name("AdwToolbarView"))) {
                    box_container = gtk_widget_get_first_child(first_child);
                }
                
                if (GTK_IS_BOX(box_container)) {
                    // Indsæt vores toolbar først i box containeren
                    gtk_box_prepend(GTK_BOX(box_container), toolbar_container);
                    g_print("Tilføjede ny toolbar container til window\n");
                } else {
                    g_critical("Unexpected widget hierarchy in window\n");
                }
            } else {
                g_critical("Kunne ikke finde den forventede widget-struktur i window\n");
            }
        } else {
            g_critical("FEJL: Måtte oprette fallback toolbar container!\n");
        }
    }
    
    // Fjern eksisterende børn
    GtkWidget *child = gtk_widget_get_first_child(toolbar_container);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(toolbar_container), child);
        child = next;
    }
    
    // Opret knapper
    GtkWidget *italic_button = gtk_button_new_with_label("Kursiv");
    GtkWidget *bold_button = gtk_button_new_with_label("Fed");
    GtkWidget *hr_button = gtk_button_new_with_label("Horisontal streg");
    GtkWidget *heading_button = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(heading_button), "Overskrifter");
    
    // Tilføj tooltips
    gtk_widget_set_tooltip_text(italic_button, "Sæt tekst i kursiv");
    gtk_widget_set_tooltip_text(bold_button, "Sæt tekst med fed skrift");
    gtk_widget_set_tooltip_text(hr_button, "Indsæt horisontal streg");
    gtk_widget_set_tooltip_text(heading_button, "Vælg overskriftstype");
    
    // Tilføj signal handlers
    g_signal_connect(italic_button, "clicked", G_CALLBACK(on_italic_button_clicked), text_view);
    g_signal_connect(bold_button, "clicked", G_CALLBACK(on_bold_button_clicked), text_view);
    g_signal_connect(hr_button, "clicked", G_CALLBACK(on_hr_button_clicked), text_view);
    
    // Opsæt heading dropdown menu
    setup_heading_menu(heading_button, text_view);
    
    // Tilføj knapper til toolbar
    gtk_box_append(GTK_BOX(toolbar_container), italic_button);
    gtk_box_append(GTK_BOX(toolbar_container), bold_button);
    
    // Separator
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(separator, 6);
    gtk_widget_set_margin_end(separator, 6);
    gtk_box_append(GTK_BOX(toolbar_container), separator);
    
    // Overskriftsknap
    gtk_box_append(GTK_BOX(toolbar_container), heading_button);
    
    // Separator
    separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start(separator, 6);
    gtk_widget_set_margin_end(separator, 6);
    gtk_box_append(GTK_BOX(toolbar_container), separator);
    
    // Horisontal streg knap
    gtk_box_append(GTK_BOX(toolbar_container), hr_button);
    
    // Sørg for at toolbaren er synlig med god størrelse
    gtk_widget_set_visible(toolbar_container, TRUE);
    gtk_widget_set_hexpand(toolbar_container, TRUE);
    gtk_widget_set_margin_start(toolbar_container, 8);
    gtk_widget_set_margin_end(toolbar_container, 8);
    gtk_widget_set_margin_top(toolbar_container, 6);
    gtk_widget_set_margin_bottom(toolbar_container, 6);
    
    // Gør knapper mere synlige
    child = gtk_widget_get_first_child(toolbar_container);
    while (child) {
        if (GTK_IS_BUTTON(child) || GTK_IS_MENU_BUTTON(child)) {
            gtk_widget_set_margin_start(child, 2);
            gtk_widget_set_margin_end(child, 2);
            gtk_widget_set_size_request(child, 80, 32);
        }
        child = gtk_widget_get_next_sibling(child);
    }
    
    // Debug udskrift
    g_print("Toolbar oprettet med %d knapper\n", 
        g_list_model_get_n_items(gtk_widget_observe_children(toolbar_container)));
    
    return toolbar_container;
}