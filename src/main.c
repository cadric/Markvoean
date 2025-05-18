#include <gtk/gtk.h>
#include <adwaita.h>
#include <gdk/gdkkeysyms.h>  // For tastatur-konstanter

#include "toolbar.h"
// #include "markdown.h"  // Tilføjet for at få adgang til markdown-funktionerne
#include "gtktext_cmark.h" // Switched to gtktext_cmark
#include "settings.h"

// Funktionsdeklarationer
// static void setup_markdown_tags(GtkTextBuffer *buffer); // Removed duplicate
static void copy_selected_text_as_markdown(GtkTextView *text_view); // Removed __attribute__((unused))
static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);
static void app_activate(GApplication *application); // Changed G_APPLICATION to GApplication

// Determines the full path for the save file.
static gchar* get_save_file_path(void) {
    const gchar *doc_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
    if (!doc_dir) {
        doc_dir = g_get_home_dir(); // Fallback to home directory if Documents isn't found
    }
    return g_build_filename(doc_dir, "mini_text_editor.md", NULL);
}

// Loads text content from the predefined save file.
static void load_markdown_to_buffer(GtkTextBuffer *buffer) {
    g_autofree gchar *filename = get_save_file_path();
    gchar *content = NULL;
    GError *error = NULL;
    g_print("Loading markdown from file: %s\n", filename);
    if (!g_file_get_contents(filename, &content, NULL, &error)) {
        // If the file doesn't exist, it's not an error, just start with an empty buffer.
        if (error->code != G_FILE_ERROR_NOENT) {
            g_warning("Error loading file: %s", error->message);
        }
        g_clear_error(&error);
        // Ensure buffer is empty if file not found or on error
        gtk_text_buffer_set_text(buffer, "", -1);
        return;
    }
    g_print("File contents: %s\n", content);
    // if (!import_markdown_to_buffer(buffer, content)) {
    if (!import_markdown_to_buffer_cmark(buffer, content)) { // Changed to cmark version
        g_warning("Failed to import markdown to buffer");
    } else {
        g_print("Markdown imported to buffer successfully using cmark.\n");
    }
    g_free(content);
}

// Saves the content of the GtkTextBuffer to the predefined save file as markdown.
static void save_buffer_as_markdown(GtkTextBuffer *buffer) {
    g_autofree gchar *filename = get_save_file_path();
    // char *md = export_buffer_to_markdown(buffer);
    char *md = export_buffer_to_markdown_cmark(buffer); // Changed to cmark version
    GError *error = NULL;
    if (!g_file_set_contents(filename, md, -1, &error)) {
        g_warning("Error saving file: %s", error->message);
        g_clear_error(&error);
    } else {
        g_print("Buffer content saved as markdown to %s using cmark\n", filename);
    }
    g_free(md);
}

// Callback triggered when the text in the GtkTextBuffer changes.
static void on_text_changed(GtkTextBuffer *buffer, G_GNUC_UNUSED gpointer user_data) {
    // Først gemmer vi ændringerne som normalt
    save_buffer_as_markdown(buffer);
    // Udskyd markdown-parsing til næste hovedloop-cyklus - Commented out dynamic processing
    /*
    if (!markdown_update_pending) {
        markdown_update_pending = TRUE;
        g_idle_add(idle_process_markdown_wrapper, buffer);
    }
    */
}

// Callback triggered when the main window requests to be closed.
static gboolean on_window_close_request(G_GNUC_UNUSED GtkWindow *window, gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    save_buffer_as_markdown(buffer);
    return GDK_EVENT_PROPAGATE;
}

// Konverterer valgt tekst til markdown og kopierer til udklipsholderen
static void copy_selected_text_as_markdown(GtkTextView *text_view) {
    g_print("DEBUG: copy_selected_text_as_markdown function started\n");

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter start_sel, end_sel;

    if (!gtk_text_buffer_get_selection_bounds(buffer, &start_sel, &end_sel)) {
        g_print("DEBUG: No text selected for copy\n");
        return;
    }
    g_print("DEBUG: Text selection found\n");

    GString *md = g_string_new("");
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(buffer, &iter, gtk_text_iter_get_offset(&start_sel));

    gboolean currently_in_bold = FALSE;
    gboolean currently_in_italic = FALSE;
    gboolean currently_in_code = FALSE;
    gboolean currently_in_codeblock = FALSE;
    
    GtkTextIter temp_line_start_iter = iter;
    gtk_text_iter_set_line_offset(&temp_line_start_iter, 0);
    gboolean at_line_start = gtk_text_iter_equal(&iter, &temp_line_start_iter);

    GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

    while(gtk_text_iter_compare(&iter, &end_sel) < 0) {
        gunichar current_char = gtk_text_iter_get_char(&iter);

        gboolean iter_is_bold = FALSE;
        gboolean iter_is_italic = FALSE;
        gboolean iter_is_code = FALSE;
        gboolean iter_is_codeblock_char = FALSE;
        gboolean iter_is_h1 = FALSE, iter_is_h2 = FALSE, iter_is_h3 = FALSE, iter_is_h4 = FALSE, iter_is_h5 = FALSE, iter_is_h6 = FALSE;

        GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
        for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
            GtkTextTag *tag = GTK_TEXT_TAG(l->data);
            gchar* tag_name = NULL;
            g_object_get(tag, "name", &tag_name, NULL);
            if (tag_name) {
                if (g_strcmp0(tag_name, "bold") == 0) iter_is_bold = TRUE;
                else if (g_strcmp0(tag_name, "italic") == 0) iter_is_italic = TRUE;
                else if (g_strcmp0(tag_name, "code") == 0) iter_is_code = TRUE;
                else if (g_strcmp0(tag_name, "codeblock") == 0) iter_is_codeblock_char = TRUE;
                else if (g_strcmp0(tag_name, "h1") == 0) iter_is_h1 = TRUE;
                else if (g_strcmp0(tag_name, "h2") == 0) iter_is_h2 = TRUE;
                else if (g_strcmp0(tag_name, "h3") == 0) iter_is_h3 = TRUE;
                else if (g_strcmp0(tag_name, "h4") == 0) iter_is_h4 = TRUE;
                else if (g_strcmp0(tag_name, "h5") == 0) iter_is_h5 = TRUE;
                else if (g_strcmp0(tag_name, "h6") == 0) iter_is_h6 = TRUE;
                g_free(tag_name);
            }
        }
        g_slist_free(tags_at_iter);

        if (at_line_start) {
            GtkTextTag *hr_tag = gtk_text_tag_table_lookup(tag_table, "hr");
            if (hr_tag && gtk_text_iter_has_tag(&iter, hr_tag) && !currently_in_codeblock) {
                g_string_append(md, "---\\n");
                
                GtkTextIter line_end_iter = iter;
                gtk_text_iter_forward_to_line_end(&line_end_iter);
                iter = line_end_iter; 

                if (gtk_text_iter_compare(&iter, &end_sel) < 0) {
                     gtk_text_iter_forward_char(&iter); 
                     if (gtk_text_iter_compare(&iter, &end_sel) < 0 && gtk_text_iter_get_char(&iter) == '\n') { // Corrected from '\\n'
                         gtk_text_iter_forward_char(&iter); 
                     }
                }
                at_line_start = TRUE; 
                if (gtk_text_iter_compare(&iter, &end_sel) >= 0) break;
                continue;
            }

            if (!currently_in_codeblock) {
                gboolean heading_started_here = FALSE;
                if (iter_is_h1) { g_string_append(md, "# "); heading_started_here = TRUE; }
                else if (iter_is_h2) { g_string_append(md, "## "); heading_started_here = TRUE; }
                else if (iter_is_h3) { g_string_append(md, "### "); heading_started_here = TRUE; }
                else if (iter_is_h4) { g_string_append(md, "#### "); heading_started_here = TRUE; }
                else if (iter_is_h5) { g_string_append(md, "##### "); heading_started_here = TRUE; }
                else if (iter_is_h6) { g_string_append(md, "###### "); heading_started_here = TRUE; }

                if (!heading_started_here) { 
                    GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");
                    if (codeblock_tag && gtk_text_iter_has_tag(&iter, codeblock_tag)) {
                        g_string_append(md, "```\n"); // Corrected from "```\\n"
                        currently_in_codeblock = TRUE;
                    }
                }
            }
        }

        if (currently_in_codeblock && !iter_is_codeblock_char && current_char != '\n') { // Corrected from '\\n'
            if (md->str[md->len -1] != '\n') {
                g_string_append_c(md, '\n');
            }
            g_string_append(md, "```\n"); // Corrected from "```\\n"
            currently_in_codeblock = FALSE;
        }
        
        if (currently_in_codeblock) {
            g_string_append_unichar(md, current_char);
        } else {
            // Inline formatting
            if (iter_is_bold && iter_is_italic && !currently_in_bold && !currently_in_italic) {
                g_string_append(md, "***");
                currently_in_bold = TRUE;
                currently_in_italic = TRUE;
            } else if (!iter_is_bold && !iter_is_italic && currently_in_bold && currently_in_italic) {
                g_string_append(md, "***");
                currently_in_bold = FALSE;
                currently_in_italic = FALSE;
            } else {
                if (iter_is_bold && !currently_in_bold) {
                    g_string_append(md, "**");
                    currently_in_bold = TRUE;
                } else if (!iter_is_bold && currently_in_bold) {
                    g_string_append(md, "**");
                    currently_in_bold = FALSE;
                }

                if (iter_is_italic && !currently_in_italic) {
                    g_string_append(md, "*");
                    currently_in_italic = TRUE;
                } else if (!iter_is_italic && currently_in_italic) {
                    g_string_append(md, "*");
                    currently_in_italic = FALSE;
                }
            }

            // Handle inline code state changes BEFORE appending the character
            if (currently_in_code && !iter_is_code) { // Leaving a code span
                g_string_append_c(md, '`');
                currently_in_code = FALSE;
            }
            if (iter_is_code && !currently_in_code) { // Entering a code span
                g_string_append_c(md, '`');
                currently_in_code = TRUE;
            }
            
            g_string_append_unichar(md, current_char); // Append the character itself
            
            // Old inline code closing logic removed, it's handled by the checks above
            // or by the final check after the loop.
        }

        if (current_char == '\n') {
            at_line_start = TRUE;
            if (currently_in_codeblock) {
                // Check if the *next* char (if any) still has codeblock tag.
                // If not, this newline might be the end of the codeblock content.
                GtkTextIter next_char_iter = iter;
                gtk_text_iter_forward_char(&next_char_iter); // Look ahead
                if (gtk_text_iter_compare(&next_char_iter, &end_sel) >= 0 || // End of selection
                    !gtk_text_iter_has_tag(&next_char_iter, gtk_text_tag_table_lookup(tag_table, "codeblock"))) {
                    // This newline is the last line of the code block content, or selection ends.
                    // The fence will be added at loop end or when tag disappears.
                    // If the newline itself means end of block (e.g. selection ends here, or next line no tag)
                    // then we need to close the block.
                    if (md->str[md->len -1] != '\n') { // md already has the current_char = \n
                        // This condition is tricky. The current_char is \n.
                        // We need to check if the char *before* it was \n.
                        // Let's assume the newline was appended.
                    }
                    // If the selection ends right after this newline, the after-loop logic handles it.
                    // If next line has no codeblock tag, the logic at start of loop for non-codeblock char handles it.
                }
            }
        } else {
            at_line_start = FALSE;
        }
        
        gtk_text_iter_forward_char(&iter);
    }

    if (currently_in_code) {
        g_string_append_c(md, '`');
    }
    if (currently_in_codeblock) {
        if (md->len > 0 && md->str[md->len -1] != '\n') {
            g_string_append_c(md, '\n');
        }
        g_string_append(md, "```" "\n");
    }
    // Ensure bold/italic are closed if selection ends mid-format
    if (currently_in_bold && currently_in_italic) g_string_append(md, "***");
    else if (currently_in_bold) g_string_append(md, "**");
    else if (currently_in_italic) g_string_append(md, "*");


    GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(text_view));
    gdk_clipboard_set_text(clipboard, md->str);
    g_print("Copied to clipboard as markdown: %s\n", md->str);
    g_string_free(md, TRUE);
}

// Callback for tastaturgenvej (Ctrl+C)
static gboolean on_key_pressed(G_GNUC_UNUSED GtkEventControllerKey *controller,
                               guint keyval,
                               G_GNUC_UNUSED guint keycode,
                               GdkModifierType state,
                               gpointer user_data) {
    GtkTextView *text_view = GTK_TEXT_VIEW(user_data);

    // Detect Ctrl+C
    if (keyval == GDK_KEY_c && (state & GDK_CONTROL_MASK)) {
        g_print("DEBUG: Ctrl+C detected\n");
        copy_selected_text_as_markdown(text_view);
        return TRUE; // Event handled
    }

    return FALSE; // Pass event to other handlers
}


static void app_activate(GApplication *application) {
    GtkApplication *app = GTK_APPLICATION(application);
    GtkBuilder *builder = gtk_builder_new_from_file("../ui/main_window.ui");
    if (!builder) {
        g_critical("Failed to load UI file main_window.ui");
        return;
    }

    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    if (!window) {
        g_critical("Failed to get main_window from UI");
        g_object_unref(builder);
        return;
    }
    gtk_window_set_application(GTK_WINDOW(window), GTK_APPLICATION(app));

    GtkWidget *text_view = GTK_WIDGET(gtk_builder_get_object(builder, "text_view"));
    if (!text_view) {
        g_critical("Failed to get text_view from UI");
        g_object_unref(builder);
        return;
    }
    
    // Opret toolbar og tilføj til container
    GtkWidget *toolbar_container = GTK_WIDGET(gtk_builder_get_object(builder, "toolbar_container"));
    if (!toolbar_container) {
        g_critical("Failed to get toolbar_container from UI");
        g_object_unref(builder);
        return;
    }
    
    // Tilføj lidt CSS styling til toolbaren
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, 
        ".toolbar { background-color: @theme_bg_color; border-bottom: 1px solid @borders; padding: 8px; margin: 4px; }"
        ".toolbar button { padding: 4px 8px; min-height: 24px; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    
    // Sørg for at builder associeres med window, så vi kan få det fra ethvert widget
    // der er forbundet med vinduet
    g_object_set_data(G_OBJECT(window), "builder", builder);
    g_object_ref(builder); // Hold en reference til builder
    
    // Opret toolbar og tilføj til UI
    GtkWidget *toolbar = create_toolbar(text_view);
    
    // Sørg for at toolbar er synlig og korrekt tilføjet
    if (toolbar && toolbar_container) {
        gtk_widget_set_visible(toolbar_container, TRUE);
        g_print("Toolbar er nu synlig og aktiv\n");
    }

    // Opsæt key controller til at detektere Ctrl+C
    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(on_key_pressed), text_view);
    gtk_widget_add_controller(text_view, key_controller);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    load_markdown_to_buffer(buffer);
    g_signal_connect(buffer, "changed", G_CALLBACK(on_text_changed), NULL);

    // Tilføj signal for window close
    g_signal_connect(window, "close-request", G_CALLBACK(on_window_close_request), text_view);

    // Når vinduet bliver ødelagt, så frigiv referencen til builder
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(g_object_unref), builder);

    gtk_window_present(GTK_WINDOW(window));
    // Vi frigiver ikke builder her, da vi gemmer en reference i window-objektet
    // Den frigives, når window ødelægges
}

int main (int argc, char *argv[]) {
  g_autoptr (AdwApplication) app = NULL;
  int status;

  app = adw_application_new ("com.example.MiniTextEditor", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect (app, "activate", G_CALLBACK (app_activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);

  return status;
}