/* C ULTRA-MIN TEMPLATE
   Purpose: Per-tab document container for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-17 - ui/tab_document.c
   Created: Document container for tab-based editing
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/ui/tab_document.h>
#include <gtktext/ui/welcome_screen.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/editor/buffer_manager.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations for content synchronization functions */
static gboolean tab_document_sync_wysiwyg_to_source(TabDocument *td);
static gboolean tab_document_sync_source_to_wysiwyg(TabDocument *td);

static void on_buffer_changed(GtkTextBuffer *buffer G_GNUC_UNUSED, gpointer user_data)
{
    TabDocument *td = (TabDocument *)user_data;
    if (td && !td->is_welcome) {
        gboolean was_dirty = td->is_dirty;
        td->is_dirty = TRUE;

        /* Phase 3: Notify callback if dirty state changed */
        if (!was_dirty && td->dirty_state_callback) {
            td->dirty_state_callback(td, TRUE, td->dirty_state_callback_data);
        }
    }
}

static GtkWidget *create_text_editor_widget(TabDocument *td)
{
    /* Create a stack to hold both WYSIWYG and source views */
    GtkWidget *stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(stack), 150);

    /* === WYSIWYG VIEW === */
    /* Create scrolled window for WYSIWYG */
    GtkWidget *wysiwyg_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(wysiwyg_scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

    /* Create WYSIWYG text view */
    td->text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(td->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(td->text_view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(td->text_view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(td->text_view), 10);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(td->text_view), 10);

    td->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(td->text_view));

    /* Add WYSIWYG text view to its scrolled window */
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(wysiwyg_scrolled), td->text_view);

    /* === SOURCE VIEW === */
    /* Create scrolled window for source view */
    GtkWidget *source_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(source_scrolled),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

    /* Create source text view and buffer */
    td->source_buffer = gtk_text_buffer_new(NULL);
    td->source_text_view = gtk_text_view_new_with_buffer(td->source_buffer);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(td->source_text_view), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(td->source_text_view), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(td->source_text_view), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(td->source_text_view), 12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(td->source_text_view), 12);
    gtk_widget_add_css_class(td->source_text_view, "monospace");

    /* Add source text view to its scrolled window */
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(source_scrolled), td->source_text_view);

    /* Add both views to stack */
    gtk_stack_add_named(GTK_STACK(stack), wysiwyg_scrolled, "wysiwyg");
    gtk_stack_add_named(GTK_STACK(stack), source_scrolled, "source");

    /* Start with WYSIWYG view */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "wysiwyg");

    /* Store the stack reference in TabDocument */
    td->scrolled_window = stack;  /* Repurpose this field to store the stack */
    td->original_text_view = wysiwyg_scrolled;  /* Store WYSIWYG scrolled window */

    return stack;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - TabDocument lifecycle and operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

TabDocument *tab_document_new(void)
{
    TabDocument *td = g_new0(TabDocument, 1);

    /* Create document model */
    td->document = gtktext_document_new();

    /* Create UI components */
    td->container = create_text_editor_widget(td);

    /* DocumentManager will be initialized later when we have access to the main window */
    td->doc_manager = NULL;

    /* Connect buffer change signal */
    td->buffer_changed_handler_id = g_signal_connect(td->buffer, "changed",
                                                     G_CALLBACK(on_buffer_changed), td);

    /* Initialize state */
    td->is_dirty = FALSE;
    td->is_welcome = FALSE;
    td->being_destroyed = FALSE;
    td->tab_title = g_strdup(_("Untitled"));

    /* Initialize source view state */
    td->is_source_mode = FALSE;
    /* source_text_view and source_buffer are now initialized in create_text_editor_widget */
    /* scrolled_window and original_text_view are repurposed to store stack and WYSIWYG container */

    /* Phase 3: Initialize callback */
    td->dirty_state_callback = NULL;
    td->dirty_state_callback_data = NULL;

    g_debug("TabDocument created");
    return td;
}

TabDocument *tab_document_new_from_file(const char *file_path)
{
    g_return_val_if_fail(file_path != NULL, NULL);

    TabDocument *td = tab_document_new();

    GError *error = NULL;
    if (!tab_document_load_file(td, file_path, &error)) {
        g_warning("Failed to load file %s: %s", file_path,
                 error ? error->message : "Unknown error");
        g_clear_error(&error);
        tab_document_destroy(td);
        return NULL;
    }

    return td;
}

TabDocument *tab_document_new_welcome(void)
{
    TabDocument *td = g_new0(TabDocument, 1);

    /* Welcome tab doesn't need document model */
    td->document = NULL;
    td->doc_manager = NULL;
    td->buffer = NULL;
    td->text_view = NULL;

    /* Create welcome screen widget */
    td->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    /* Welcome screen content will be added by the welcome_screen module */

    td->is_welcome = TRUE;
    td->is_dirty = FALSE;
    td->tab_title = g_strdup(_("Welcome"));

    /* Initialize source view state - welcome tabs don't have source view */
    td->is_source_mode = FALSE;
    td->source_text_view = NULL;
    td->source_buffer = NULL;
    td->scrolled_window = NULL;
    td->original_text_view = NULL;

    /* Phase 3: Initialize callback */
    td->dirty_state_callback = NULL;
    td->dirty_state_callback_data = NULL;

    g_debug("Welcome TabDocument created");
    return td;
}

void tab_document_destroy(TabDocument *td)
{
    if (!td) return;

    /* Critical: Prevent double-destruction */
    if (td->being_destroyed) {
        g_debug("TabDocument already being destroyed, skipping");
        return;
    }
    td->being_destroyed = TRUE;

    g_debug("Starting TabDocument destruction");

    /* Step 1: Immediately disconnect all signals to prevent further callbacks */
    if (td->buffer_changed_handler_id > 0 && td->buffer && G_IS_OBJECT(td->buffer)) {
        if (g_signal_handler_is_connected(td->buffer, td->buffer_changed_handler_id)) {
            g_signal_handler_disconnect(td->buffer, td->buffer_changed_handler_id);
            g_debug("Disconnected buffer signal");
        }
    }
    td->buffer_changed_handler_id = 0;

    /* Step 2: Clear callback to prevent any calls during destruction */
    td->dirty_state_callback = NULL;
    td->dirty_state_callback_data = NULL;

    /* Step 3: Clean up document manager first (may have timers/async operations) */
    if (td->doc_manager) {
        g_debug("Freeing document manager");
        document_manager_free(td->doc_manager);
        td->doc_manager = NULL;
    }

    /* Step 4: Clean up document model */
    if (td->document && G_IS_OBJECT(td->document)) {
        g_debug("Unreffing document");
        g_object_unref(td->document);
        td->document = NULL;
    }

    /* Step 5: Clean up source view state */
    if (td->source_buffer && G_IS_OBJECT(td->source_buffer)) {
        g_debug("Unreffing source buffer");
        g_object_unref(td->source_buffer);
        td->source_buffer = NULL;
    }
    /* CRITICAL FIX: Don't unref original_text_view - container owns it */
    /* The widget hierarchy will be destroyed by GTK when tab is closed */
    td->original_text_view = NULL;

    /* Step 6: Clear all pointers to prevent accidental access */
    td->buffer = NULL;
    td->text_view = NULL;
    td->scrolled_window = NULL;
    td->source_text_view = NULL;

    /* Step 7: Free strings */
    g_free(td->file_path);
    g_free(td->tab_title);
    td->file_path = NULL;
    td->tab_title = NULL;

    /* Note: container widget will be destroyed by GTK when tab is closed */
    td->container = NULL;

    g_free(td);
    g_debug("TabDocument destroyed successfully");
}

gboolean tab_document_load_file(TabDocument *td, const char *file_path, GError **error)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(file_path != NULL, FALSE);
    g_return_val_if_fail(!td->is_welcome, FALSE);

    /* Load file contents */
    g_autofree char *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(file_path, &contents, &length, error)) {
        return FALSE;
    }

    /* Block buffer changed signal during load */
    if (td->buffer_changed_handler_id > 0) {
        g_signal_handler_block(td->buffer, td->buffer_changed_handler_id);
    }

    /* Set content in buffer */
    gtk_text_buffer_set_text(td->buffer, contents, -1);

    /* Render markdown if applicable */
    if (g_str_has_suffix(file_path, ".md")) {
        cm_render_markdown_to_buffer(td->buffer, contents,
                                    GTK_TEXT_VIEW(td->text_view), NULL);
    }

    /* Unblock signal */
    if (td->buffer_changed_handler_id > 0) {
        g_signal_handler_unblock(td->buffer, td->buffer_changed_handler_id);
    }

    /* Update document metadata */
    g_free(td->file_path);
    td->file_path = g_strdup(file_path);

    g_free(td->tab_title);
    td->tab_title = g_path_get_basename(file_path);

    /* Phase 3: Use set_modified to trigger callback */
    tab_document_set_modified(td, FALSE);

    /* Update DocumentManager with file path */
    if (td->doc_manager) {
        document_manager_open_file(td->doc_manager, file_path, NULL);
    }

    g_debug("Loaded file: %s", file_path);
    return TRUE;
}

gboolean tab_document_save(TabDocument *td, GError **error)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(!td->is_welcome, FALSE);

    if (!td->file_path) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No file path set for document");
        return FALSE;
    }

    return tab_document_save_as(td, td->file_path, error);
}

gboolean tab_document_save_as(TabDocument *td, const char *file_path, GError **error)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(file_path != NULL, FALSE);
    g_return_val_if_fail(!td->is_welcome, FALSE);

    /* Get buffer text */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(td->buffer, &start, &end);
    g_autofree char *text = gtk_text_buffer_get_text(td->buffer, &start, &end, FALSE);

    /* Write to file */
    if (!g_file_set_contents(file_path, text, -1, error)) {
        return FALSE;
    }

    /* Update metadata */
    g_free(td->file_path);
    td->file_path = g_strdup(file_path);

    g_free(td->tab_title);
    td->tab_title = g_path_get_basename(file_path);

    /* Phase 3: Use set_modified to trigger callback */
    tab_document_set_modified(td, FALSE);

    /* Update DocumentManager - simplified for now */
    /* TODO: Properly integrate with DocumentManager's async save API */

    g_debug("Saved file: %s", file_path);
    return TRUE;
}

const char *tab_document_get_display_title(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, NULL);
    return td->tab_title;
}

const char *tab_document_get_file_path(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, NULL);
    return td->file_path;
}

gboolean tab_document_get_modified(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    return td->is_dirty;
}

void tab_document_set_modified(TabDocument *td, gboolean modified)
{
    g_return_if_fail(td != NULL);

    gboolean was_dirty = td->is_dirty;
    td->is_dirty = modified;

    /* Phase 3: Notify callback if dirty state changed */
    if (was_dirty != modified && td->dirty_state_callback) {
        td->dirty_state_callback(td, modified, td->dirty_state_callback_data);
    }
}

GtkWidget *tab_document_get_widget(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, NULL);
    return td->container;
}

GtkTextBuffer *tab_document_get_buffer(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, NULL);
    return td->buffer;
}

GtkWidget *tab_document_get_text_view(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, NULL);
    return td->text_view;
}

/* Phase 3: State change callback management */
void tab_document_set_dirty_state_callback(TabDocument *td,
                                           TabDocumentDirtyStateCallback callback,
                                           gpointer user_data)
{
    g_return_if_fail(td != NULL);

    td->dirty_state_callback = callback;
    td->dirty_state_callback_data = user_data;

    g_debug("Set dirty state callback for TabDocument");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * Source view mode management
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean tab_document_get_source_mode(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    return td->is_source_mode;
}

void tab_document_set_source_mode_state(TabDocument *td, gboolean is_source_mode,
                                        GtkWidget *source_text_view, GtkTextBuffer *source_buffer,
                                        GtkWidget *scrolled_window, GtkWidget *original_text_view)
{
    g_return_if_fail(td != NULL);

    td->is_source_mode = is_source_mode;
    td->source_text_view = source_text_view;

    /* Handle source buffer reference */
    if (td->source_buffer && G_IS_OBJECT(td->source_buffer)) {
        g_object_unref(td->source_buffer);
    }
    td->source_buffer = source_buffer;
    if (source_buffer && G_IS_OBJECT(source_buffer)) {
        g_object_ref(source_buffer);
    }

    td->scrolled_window = scrolled_window;

    /* Store reference to original text view if switching to source mode */
    if (is_source_mode && original_text_view && GTK_IS_WIDGET(original_text_view) && G_IS_OBJECT(original_text_view)) {
        /* Clear any existing reference */
        if (td->original_text_view && G_IS_OBJECT(td->original_text_view)) {
            g_object_unref(td->original_text_view);
        }
        td->original_text_view = original_text_view;
        g_object_ref(original_text_view);  /* Keep reference */
    }

    g_debug("TabDocument source mode set to: %s", is_source_mode ? "source" : "WYSIWYG");
}

void tab_document_clear_source_mode_state(TabDocument *td)
{
    g_return_if_fail(td != NULL);

    td->is_source_mode = FALSE;
    td->source_text_view = NULL;

    /* Clean up source buffer */
    if (td->source_buffer && G_IS_OBJECT(td->source_buffer)) {
        g_object_unref(td->source_buffer);
    }
    td->source_buffer = NULL;

    /* Clean up original text view reference */
    if (td->original_text_view && G_IS_OBJECT(td->original_text_view)) {
        g_object_unref(td->original_text_view);
    }
    td->original_text_view = NULL;
    td->scrolled_window = NULL;

    g_debug("TabDocument source mode state cleared");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * Stack-based source view management - new approach
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean tab_document_switch_to_source_view(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(!td->is_welcome, FALSE);
    g_return_val_if_fail(td->scrolled_window != NULL, FALSE);

    if (td->is_source_mode) {
        return TRUE; /* Already in source mode */
    }

    /* Get stack container */
    GtkStack *stack = GTK_STACK(td->scrolled_window);
    if (!GTK_IS_STACK(stack)) {
        g_warning("Tab document container is not a stack");
        return FALSE;
    }

    /* Sync content from WYSIWYG to source */
    if (!tab_document_sync_wysiwyg_to_source(td)) {
        g_warning("Failed to sync WYSIWYG content to source view");
        return FALSE;
    }

    /* Switch to source view */
    gtk_stack_set_visible_child_name(stack, "source");
    td->is_source_mode = TRUE;

    g_debug("TabDocument switched to source view");
    return TRUE;
}

gboolean tab_document_switch_to_wysiwyg_view(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(!td->is_welcome, FALSE);
    g_return_val_if_fail(td->scrolled_window != NULL, FALSE);

    if (!td->is_source_mode) {
        return TRUE; /* Already in WYSIWYG mode */
    }

    /* Get stack container */
    GtkStack *stack = GTK_STACK(td->scrolled_window);
    if (!GTK_IS_STACK(stack)) {
        g_warning("Tab document container is not a stack");
        return FALSE;
    }

    /* Sync content from source to WYSIWYG */
    if (!tab_document_sync_source_to_wysiwyg(td)) {
        g_warning("Failed to sync source content to WYSIWYG view");
        return FALSE;
    }

    /* Switch to WYSIWYG view */
    gtk_stack_set_visible_child_name(stack, "wysiwyg");
    td->is_source_mode = FALSE;

    g_debug("TabDocument switched to WYSIWYG view");
    return TRUE;
}

static gboolean tab_document_sync_wysiwyg_to_source(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(td->buffer != NULL, FALSE);
    g_return_val_if_fail(td->source_buffer != NULL, FALSE);

    /* Get WYSIWYG content as markdown */
    g_autofree char *markdown = cm_render_buffer_to_markdown(td->buffer);
    if (!markdown) {
        g_warning("Failed to export WYSIWYG buffer to markdown");
        return FALSE;
    }

    /* Set markdown content in source buffer */
    gtk_text_buffer_set_text(td->source_buffer, markdown, -1);

    g_debug("Synced WYSIWYG content to source view");
    return TRUE;
}

static gboolean tab_document_sync_source_to_wysiwyg(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(td->buffer != NULL, FALSE);
    g_return_val_if_fail(td->source_buffer != NULL, FALSE);
    g_return_val_if_fail(td->text_view != NULL, FALSE);

    /* Get source content */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(td->source_buffer, &start, &end);
    g_autofree char *source_markdown = gtk_text_buffer_get_text(td->source_buffer, &start, &end, FALSE);

    if (!source_markdown) {
        source_markdown = g_strdup("");
    }

    /* Clear WYSIWYG buffer and set source content */
    gtk_text_buffer_set_text(td->buffer, source_markdown, -1);

    /* Re-render markdown in WYSIWYG view */
    cm_render_markdown_to_buffer(td->buffer, source_markdown, GTK_TEXT_VIEW(td->text_view), NULL);

    g_debug("Synced source content to WYSIWYG view");
    return TRUE;
}