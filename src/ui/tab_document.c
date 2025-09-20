/* C ULTRA-MIN TEMPLATE
   Purpose: Per-tab document container for GTK markdown editor
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.4.3] - 2025-09-19 - ui/tab_document.c
   Changed: Implemented async save API and adopted DM buffer path to avoid double reads
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gtktext/ui/tab_document.h>
#include <gtktext/render/cmrender.h>
#include <gtktext/document/document.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/editor/buffer_manager.h>
#include <gtktext/ui/tab_manager.h>
#include <gtktext/ui/file_actions.h>
#include <gtktext/ui/event_handlers.h>
#include <gtktext/ui/status_manager.h>
#include <gtktext/render/markdown/markdown_engine.h>
#include <gtktext/render/theme_styles.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * TYPES - Internal type definitions
 * ═══════════════════════════════════════════════════════════════════════════════ */

struct _TabDocument {
    /* Document model and management */
    GtktextDocument *document;          /* Document model (GObject) */
    DocumentManager *doc_manager;       /* Document operations */

    /* UI components */
    GtkWidget *text_view;              /* Editor widget */
    GtkWidget *container;             /* Container widget for tab content */
    GtkTextBuffer *buffer;            /* Text buffer */
    GtkWidget *stack;                 /* Stack container (was misleadingly named scrolled_window) */

    /* Tab metadata */
    gchar *file_path;                 /* Full file path */
    gchar *tab_title;                 /* Display title */
    gboolean is_welcome;              /* Is this the welcome tab */
    gboolean being_destroyed;         /* Destruction in progress flag */

    /* Source view state - each tab tracks its own view mode */
    gboolean is_source_mode;          /* TRUE = source view, FALSE = WYSIWYG */
    GtkWidget *source_text_view;      /* Source view widget (when active) */
    GtkTextBuffer *source_buffer;     /* Source buffer (when active) */
    GtkWidget *original_text_view;    /* Original WYSIWYG text view */
    gboolean original_text_view_has_ref; /* TRUE if we took an explicit ref on original_text_view */


    /* State change callbacks */
    TabDocumentDirtyStateCallback dirty_state_callback;
    gpointer dirty_state_callback_data;

    /* Markdown rendering state */
    gchar *pending_markdown_content;  /* Content waiting for deferred rendering */
};

/* Forward declarations for welcome button callbacks */
static void on_welcome_new_clicked(GtkButton *button, gpointer user_data);
static void on_welcome_open_clicked(GtkButton *button, gpointer user_data);

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Internal utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations for content synchronization functions */
static gboolean tab_document_sync_wysiwyg_to_source(TabDocument *td);
static gboolean tab_document_sync_source_to_wysiwyg(TabDocument *td);

/* Callback for when text view is mapped and ready for rendering */
static void on_text_view_mapped(GtkWidget *text_view, gpointer user_data)
{
    TabDocument *td = (TabDocument *)user_data;

    g_debug("Text view mapped, applying deferred markdown rendering");

    if (td && td->pending_markdown_content) {
        /* Block buffer change signals during deferred rendering */

        /* Block markdown engine automatic reparsing during initialization */
        render_set_suppress_reparse(td->buffer, TRUE);
        g_debug("DEFERRED: Suppressed markdown reparse during initialization");

        /* Also block DocumentManager buffer signals */
        if (td->doc_manager) {
            document_manager_block_buffer_signals(td->doc_manager);
            g_debug("DEFERRED: Blocked DocumentManager signals before re-rendering");
        }

        /* Re-render the markdown now that the text view is ready */
        cm_render_markdown_to_buffer(td->buffer, td->pending_markdown_content,
                                    GTK_TEXT_VIEW(td->text_view), NULL);

        /* Unblock buffer change signals */

        /* Unblock DocumentManager buffer signals */
        if (td->doc_manager) {
            document_manager_unblock_buffer_signals(td->doc_manager);
            g_debug("DEFERRED: Unblocked DocumentManager signals after re-rendering");
        }

        /* Update DocumentManager baseline AFTER unblocking signals to ensure state change is properly triggered */
        if (td->doc_manager) {
            /* Update original content to match the rendered buffer content */
            /* This ensures DocumentManager considers the rendered state as the "clean" baseline */
            document_manager_update_baseline(td->doc_manager);
            g_debug("DEFERRED: Updated DocumentManager baseline after markdown rendering");

            /* DocumentManager finalization now handled automatically */
        }

        /* Re-enable markdown engine automatic reparsing after initialization is complete */
        render_set_suppress_reparse(td->buffer, FALSE);
        g_debug("DEFERRED: Re-enabled markdown reparse after initialization complete");

        /* Clean up the pending content */
        g_free(td->pending_markdown_content);
        td->pending_markdown_content = NULL;

        /* Disconnect this one-time callback */
        g_signal_handlers_disconnect_by_func(text_view, G_CALLBACK(on_text_view_mapped), td);

        /* Now that rendering is complete, set the document as clean and trigger status update */
        tab_document_set_modified(td, FALSE);

        g_debug("Deferred markdown rendering completed and document marked as clean");
    }
}



/* Document manager state change callback that syncs with tab document */
static void on_document_manager_state_changed(DocumentManager *dm, DocumentState old_state,
                                             DocumentState new_state, gpointer user_data)
{
    TabDocument *td = user_data;
    g_return_if_fail(td != NULL);

    /* Sync TabDocument file path and title with DocumentManager */
    const gchar *dm_file_path = document_manager_get_file_path(dm);
    if (dm_file_path && (!td->file_path || g_strcmp0(td->file_path, dm_file_path) != 0)) {
        /* Update TabDocument file path to match DocumentManager */
        g_free(td->file_path);
        td->file_path = g_strdup(dm_file_path);

        /* Update display title based on file path */
        g_free(td->tab_title);
        if (dm_file_path) {
            gchar *basename = g_path_get_basename(dm_file_path);
            td->tab_title = basename;
        } else {
            td->tab_title = g_strdup("Untitled");
        }

        g_debug("TabDocument synced with DocumentManager: file_path=%s, title=%s",
                td->file_path ? td->file_path : "(null)",
                td->tab_title ? td->tab_title : "(null)");
    }

    /* Get dirty state from DocumentManager (single source of truth) */
    gboolean new_dirty = (new_state == DOC_STATE_DIRTY || new_state == DOC_STATE_DRAFT);
    gboolean was_dirty = (old_state == DOC_STATE_DIRTY || old_state == DOC_STATE_DRAFT);

    /* Update status bar through the standard mechanism */
    GtkWidget *window = gtk_widget_get_ancestor(td->container, GTK_TYPE_WINDOW);
    if (window) {
        GtkApplication *app = gtk_window_get_application(GTK_WINDOW(window));
        if (app) {
            const gchar *file_path = document_manager_get_file_path(dm);
            status_manager_update_status_bar_for_state(app, new_state, file_path);
        }
    }

    /* Notify tab document dirty state callback if state changed */
    if (was_dirty != new_dirty && td->dirty_state_callback) {
        td->dirty_state_callback(td, new_dirty, td->dirty_state_callback_data);
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

    /* Enable undo/redo functionality */
    gtk_text_buffer_set_enable_undo(td->buffer, TRUE);

    /* Set up event controllers for zoom and interactions */
    GtkEventController *key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(event_handlers_on_key_pressed), td->text_view);
    gtk_widget_add_controller(td->text_view, key_controller);

    GtkEventController *scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_controller, "scroll", G_CALLBACK(event_handlers_on_scroll_event), td->text_view);
    gtk_widget_add_controller(td->text_view, scroll_controller);

    GtkEventController *motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(motion_controller, "motion", G_CALLBACK(event_handlers_on_text_view_motion), td->text_view);
    gtk_widget_add_controller(td->text_view, motion_controller);

    /* Initialize markdown engine for real-time detection */
    markdown_engine_initialize_buffer(td->buffer, GTK_TEXT_VIEW(td->text_view), NULL);

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

    /* Enable undo/redo functionality */
    gtk_text_buffer_set_enable_undo(td->source_buffer, TRUE);

    td->source_text_view = gtk_text_view_new_with_buffer(td->source_buffer);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(td->source_text_view), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(td->source_text_view), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(td->source_text_view), 12);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(td->source_text_view), 12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(td->source_text_view), 12);
    gtk_widget_add_css_class(td->source_text_view, "monospace");

    /* Set up event controllers for source text view */
    GtkEventController *source_key_controller = gtk_event_controller_key_new();
    g_signal_connect(source_key_controller, "key-pressed", G_CALLBACK(event_handlers_on_key_pressed), td->source_text_view);
    gtk_widget_add_controller(td->source_text_view, source_key_controller);

    GtkEventController *source_scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(source_scroll_controller, "scroll", G_CALLBACK(event_handlers_on_scroll_event), td->source_text_view);
    gtk_widget_add_controller(td->source_text_view, source_scroll_controller);

    /* Add source text view to its scrolled window */
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(source_scrolled), td->source_text_view);

    /* Add both views to stack */
    gtk_stack_add_named(GTK_STACK(stack), wysiwyg_scrolled, "wysiwyg");
    gtk_stack_add_named(GTK_STACK(stack), source_scrolled, "source");

    /* Start with WYSIWYG view */
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "wysiwyg");

    /* Store the stack reference in TabDocument */
    td->stack = stack;
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


    /* Initialize state */
    td->is_welcome = FALSE;
    td->being_destroyed = FALSE;
    td->tab_title = g_strdup(_("Untitled"));
    td->pending_markdown_content = NULL;

    /* Initialize source view state */
    td->is_source_mode = FALSE;
    td->original_text_view_has_ref = FALSE;
    /* source_text_view and source_buffer are now initialized in create_text_editor_widget */
    /* stack stores the GtkStack container, original_text_view stores WYSIWYG scrolled window */

    /* Phase 3: Initialize callback */
    td->dirty_state_callback = NULL;
    td->dirty_state_callback_data = NULL;

    g_debug("TabDocument created");
    return td;
}

void tab_document_initialize_document_manager(TabDocument *td, GtkWindow *window)
{
    g_return_if_fail(td != NULL);
    g_return_if_fail(window != NULL);
    g_return_if_fail(td->buffer != NULL);

    g_debug("INITIALIZING DocumentManager for TabDocument, file_path=%s", td->file_path ? td->file_path : "(null)");

    /* Only initialize if not already done */
    if (td->doc_manager != NULL) {
        g_debug("DocumentManager already initialized for this tab");
        return;
    }

    /* DocumentManager will now handle all buffer change signals directly */

    /* Create DocumentManager for this document tab (without state callback initially) */
    td->doc_manager = document_manager_new(td->buffer, window);
    if (td->doc_manager) {
        g_debug("DocumentManager initialized for document tab");

        /* If this tab has a file path, tell the DocumentManager about it */
        if (td->file_path) {
            GError *error = NULL;
            /* Adopt existing buffer/content without re-reading from disk */
            /* DocumentManager now handles signal blocking internally */
            if (!document_manager_adopt_current_buffer(td->doc_manager, td->file_path, &error)) {
                g_warning("Failed to tell DocumentManager about file path %s: %s",
                         td->file_path, error ? error->message : "Unknown error");
                g_clear_error(&error);
            } else {
                g_debug("DocumentManager now knows about file: %s", td->file_path);
            }

            /* DocumentManager adopt function now handles finalization automatically */
        } else {
            /* For new documents, DocumentManager initialization is handled automatically */
        }

        /* IMPORTANT: Set up state change callback AFTER all file operations complete */
        /* This prevents status bar updates during file loading that would show "Modified" */
        GtkApplication *app = gtk_window_get_application(window);
        if (app) {
            /* Force DocumentManager to check its final state before connecting callback */
            DocumentState current_state = document_manager_get_state(td->doc_manager);
            g_debug("DocumentManager final state before callback setup: %d", current_state);

            g_signal_connect(td->doc_manager, "state-changed",
                            G_CALLBACK(on_document_manager_state_changed), td);
            g_debug("Connected to state-changed signal for TabDocument DocumentManager");

            /* Immediately update status bar with the correct current state */
            const gchar *file_path = document_manager_get_file_path(td->doc_manager);
            status_manager_update_status_bar_for_state(app, current_state, file_path);
            g_debug("Manually updated status bar to correct state: %d", current_state);
        }
    } else {
        g_warning("Failed to create DocumentManager for document tab");
    }

    /* DocumentManager is now ready to handle all buffer changes */

    /* DocumentManager is now the single source of truth for dirty state */
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

    /* Create welcome screen content */
    GtkWidget *welcome_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_halign(welcome_content, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(welcome_content, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(welcome_content, 48);
    gtk_widget_set_margin_bottom(welcome_content, 48);
    gtk_widget_set_margin_start(welcome_content, 48);
    gtk_widget_set_margin_end(welcome_content, 48);

    /* App title */
    GtkWidget *title = gtk_label_new(_("IFG"));
    gtk_widget_add_css_class(title, "title-1");
    gtk_box_append(GTK_BOX(welcome_content), title);

    /* Subtitle */
    GtkWidget *subtitle = gtk_label_new(_("It format good"));
    gtk_widget_add_css_class(subtitle, "title-3");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_box_append(GTK_BOX(welcome_content), subtitle);

    /* Button container */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(button_box, 24);

    /* New Document button */
    GtkWidget *new_button = gtk_button_new_with_label(_("New Document"));
    gtk_widget_add_css_class(new_button, "suggested-action");
    gtk_widget_add_css_class(new_button, "pill");
    gtk_widget_set_size_request(new_button, 140, -1);
    g_object_set_data(G_OBJECT(new_button), "welcome-action", "new");
    g_object_set_data(G_OBJECT(new_button), "tab-document", td);
    g_signal_connect(new_button, "clicked", G_CALLBACK(on_welcome_new_clicked), NULL);
    gtk_box_append(GTK_BOX(button_box), new_button);

    /* Open File button */
    GtkWidget *open_button = gtk_button_new_with_label(_("Open File"));
    gtk_widget_add_css_class(open_button, "pill");
    gtk_widget_set_size_request(open_button, 140, -1);
    g_object_set_data(G_OBJECT(open_button), "welcome-action", "open");
    g_object_set_data(G_OBJECT(open_button), "tab-document", td);
    g_signal_connect(open_button, "clicked", G_CALLBACK(on_welcome_open_clicked), NULL);
    gtk_box_append(GTK_BOX(button_box), open_button);

    gtk_box_append(GTK_BOX(welcome_content), button_box);
    gtk_box_append(GTK_BOX(td->container), welcome_content);

    td->is_welcome = TRUE;
    td->tab_title = g_strdup(_("Welcome"));

    /* Initialize source view state - welcome tabs don't have source view */
    td->is_source_mode = FALSE;
    td->source_text_view = NULL;
    td->source_buffer = NULL;
    td->stack = NULL;
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
    /* CRITICAL: Disconnect deferred render handler to prevent UAF if widget maps later */
    if (td->text_view && G_IS_OBJECT(td->text_view)) {
        g_signal_handlers_disconnect_by_func(td->text_view, G_CALLBACK(on_text_view_mapped), td);
        g_debug("Disconnected deferred render signal to prevent UAF");
    }


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

    /*
     * CRITICAL FIX: Only unref original_text_view if we actually took a ref.
     * The flag tracks whether we have an explicit ref that needs cleanup.
     */
    if (td->original_text_view_has_ref && td->original_text_view && G_IS_OBJECT(td->original_text_view)) {
        g_debug("Unreffing original_text_view (explicit ref from source mode)");
        g_object_unref(td->original_text_view);
    }
    td->original_text_view = NULL;

    /* Step 6: Clear all pointers to prevent accidental access */
    td->buffer = NULL;
    td->text_view = NULL;
    td->stack = NULL;
    td->source_text_view = NULL;

    /* Step 7: Free strings */
    g_free(td->file_path);
    g_free(td->tab_title);
    g_free(td->pending_markdown_content);
    td->file_path = NULL;
    td->tab_title = NULL;
    td->pending_markdown_content = NULL;

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

    /* DocumentManager is required for file loading */
    if (!td->doc_manager) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "DocumentManager is required for file operations");
        return FALSE;
    }

    /* DocumentManager handles file loading and buffer management */
    if (!document_manager_open_file(td->doc_manager, file_path, error)) {
        return FALSE;
    }
    g_debug("DocumentManager loaded file: %s", file_path);

    /* Handle markdown rendering for both DocumentManager and fallback cases */
    if (g_str_has_suffix(file_path, ".md")) {
        render_set_suppress_reparse(td->buffer, TRUE);
        g_debug("LOAD: Suppressed markdown reparse for entire file loading process");

        /* Get buffer content for markdown rendering */
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(td->buffer, &start, &end);
        g_autofree char *buffer_contents = gtk_text_buffer_get_text(td->buffer, &start, &end, FALSE);

        /* Store original content for later rendering */
        g_free(td->pending_markdown_content);
        td->pending_markdown_content = g_strdup(buffer_contents);

        /* Try to render immediately, but also set up deferred rendering */
        cm_render_markdown_to_buffer(td->buffer, buffer_contents,
                                    GTK_TEXT_VIEW(td->text_view), NULL);

        /* Restore suppression flag as cmrender clears it internally */
        render_set_suppress_reparse(td->buffer, TRUE);
        g_debug("LOAD: Restored markdown reparse suppression after initial rendering");

        /* Set up callback for when text view becomes mapped */
        if (!gtk_widget_get_mapped(GTK_WIDGET(td->text_view))) {
            g_signal_connect_after(td->text_view, "map",
                                  G_CALLBACK(on_text_view_mapped), td);
        }

        /* Note: suppression flag will be cleared by the deferred rendering callback */
    }

    /* Update document metadata */
    g_free(td->file_path);
    td->file_path = g_strdup(file_path);

    g_free(td->tab_title);
    g_autofree char *basename = g_path_get_basename(file_path);
    td->tab_title = g_steal_pointer(&basename);

    /* Update DocumentManager with file path (avoid double read) */
    if (td->doc_manager) {
        document_manager_adopt_current_buffer(td->doc_manager, file_path, NULL);
    }

    /* Note: tab_document_set_modified(td, FALSE) will be called after deferred rendering completes */

    g_debug("Loaded file: %s", file_path);
    return TRUE;
}

void tab_document_load_file_async(TabDocument *td,
                                  const char *file_path,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    g_return_if_fail(td != NULL);
    g_return_if_fail(file_path != NULL);
    g_return_if_fail(!td->is_welcome);

    /* DocumentManager is required for async file loading */
    if (!td->doc_manager) {
        GTask *task = g_task_new(td, cancellable, callback, user_data);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                               "DocumentManager is required for file operations");
        g_object_unref(task);
        return;
    }

    /* Delegate to DocumentManager's async open implementation */
    document_manager_open_async(td->doc_manager, file_path, cancellable, callback, user_data);
}

gboolean tab_document_load_file_finish(TabDocument *td, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(G_IS_ASYNC_RESULT(result), FALSE);

    /* DocumentManager is required */
    if (!td->doc_manager) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "DocumentManager is required for file operations");
        return FALSE;
    }

    /* Handle DocumentManager async result */
    gboolean ok = document_manager_open_finish(td->doc_manager, result, error);
    if (ok) {
        /* Update TabDocument metadata after successful load */
        const gchar *loaded_path = document_manager_get_file_path(td->doc_manager);
        if (loaded_path) {
            g_free(td->file_path);
            td->file_path = g_strdup(loaded_path);

            g_free(td->tab_title);
            g_autofree char *basename = g_path_get_basename(td->file_path);
            td->tab_title = g_steal_pointer(&basename);

            /* Handle markdown rendering for loaded content - use deferred rendering */
            if (g_str_has_suffix(loaded_path, ".md")) {
                render_set_suppress_reparse(td->buffer, TRUE);
                g_debug("ASYNC: Suppressed markdown reparse for async file loading");

                /* Get buffer content for markdown rendering */
                GtkTextIter start, end;
                gtk_text_buffer_get_bounds(td->buffer, &start, &end);
                g_autofree char *buffer_contents = gtk_text_buffer_get_text(td->buffer, &start, &end, FALSE);

                /* Store original content for deferred rendering */
                g_free(td->pending_markdown_content);
                td->pending_markdown_content = g_strdup(buffer_contents);

                /* Render immediately for initial display */
                cm_render_markdown_to_buffer(td->buffer, buffer_contents,
                                            GTK_TEXT_VIEW(td->text_view), NULL);

                /* Apply theme colors to rendered tags */
                theme_styles_update_theme_dependent_tags(td->buffer);
                g_debug("ASYNC: Applied theme styles to rendered markdown tags");

                /* Restore suppression flag as cmrender clears it internally */
                render_set_suppress_reparse(td->buffer, TRUE);
                g_debug("ASYNC: Restored markdown reparse suppression after initial rendering");

                /* Set up callback for when text view becomes mapped */
                if (!gtk_widget_get_mapped(GTK_WIDGET(td->text_view))) {
                    g_signal_connect(td->text_view, "map",
                                   G_CALLBACK(on_text_view_mapped), td);
                    g_debug("ASYNC: Connected deferred rendering callback for unmapped text view");
                } else {
                    /* Text view is already mapped, apply deferred rendering now */
                    on_text_view_mapped(GTK_WIDGET(td->text_view), td);
                    g_debug("ASYNC: Applied deferred rendering immediately for mapped text view");
                }
            }
        }
    }
    return ok;
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

    /* Start async save to avoid blocking UI */
    tab_document_save_as_async(td, td->file_path, NULL, NULL, NULL);
    return TRUE; /* indicates save started */
}

gboolean tab_document_save_as(TabDocument *td, const char *file_path, GError **error)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(file_path != NULL, FALSE);
    g_return_val_if_fail(!td->is_welcome, FALSE);

    /* Make a defensive copy to protect against parameter corruption */
    g_autofree char *safe_file_path = g_strdup(file_path);
    g_debug("tab_document_save_as: Starting with file_path='%s'", safe_file_path);

    /* Use DocumentManager to handle the save operation */
    if (td->doc_manager) {
        /* Note: DocumentManager now uses async API. This sync wrapper is deprecated. */
        /* For now, call the deprecated sync API to maintain compatibility */
        if (!document_manager_save_as(td->doc_manager, safe_file_path, NULL, NULL)) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "DocumentManager failed to save file: %s", safe_file_path);
            return FALSE;
        }
        g_debug("DocumentManager saved file: %s", safe_file_path);
    } else {
        /* Fallback: manual save if no DocumentManager */
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(td->buffer, &start, &end);
        g_autofree char *text = gtk_text_buffer_get_text(td->buffer, &start, &end, FALSE);

        if (!g_file_set_contents(safe_file_path, text, -1, error)) {
            return FALSE;
        }
        g_debug("Manual save completed: %s", safe_file_path);
    }

    /* Update TabDocument metadata */
    g_free(td->file_path);
    td->file_path = g_strdup(safe_file_path);

    g_free(td->tab_title);
    g_autofree char *basename = g_path_get_basename(td->file_path);
    td->tab_title = g_steal_pointer(&basename);

    /* Phase 3: Use set_modified to trigger callback */
    tab_document_set_modified(td, FALSE);

    /* Verify DocumentManager state after save */
    if (td->doc_manager) {
        DocumentState state = document_manager_get_state(td->doc_manager);
        g_debug("DocumentManager state after save: %d", state);
    }

    g_debug("Saved file: %s", safe_file_path);
    return TRUE;
}

gboolean tab_document_save_as_sync(TabDocument *td, const char *file_path, GError **error)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(file_path != NULL, FALSE);

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "Synchronous save not yet implemented - DocumentManager uses async API");
    return FALSE;
}

/* Save task payload */
typedef struct {
    char *path;
    char *content;
} SaveTaskData;

static void save_task_data_free(SaveTaskData *p)
{
    if (!p) return;
    g_free(p->path);
    g_free(p->content);
    g_free(p);
}

static void save_as_task_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
    (void)source_object;
    SaveTaskData *data = (SaveTaskData *)task_data;

    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Save cancelled");
        return;
    }

    gsize len = strlen(data->content);
    GError *io_error = NULL;
    if (!atomic_write_file(data->path, data->content, len, &io_error)) {
        g_task_return_error(task, io_error);
        return;
    }

    /* Success */
    g_task_return_boolean(task, TRUE);
}

void tab_document_save_as_async(TabDocument *td,
                                const char *file_path,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    g_return_if_fail(td != NULL);
    g_return_if_fail(file_path != NULL);

    /* Use DocumentManager async API for unified I/O handling */
    if (td->doc_manager) {
        /* Delegate to DocumentManager's async save_as implementation */
        document_manager_save_as_async(td->doc_manager, file_path, cancellable, callback, user_data);
    } else {
        /* Fallback for case without DocumentManager (should be rare) */
        g_autofree char *content = cm_render_buffer_to_markdown(td->buffer);
        if (!content) {
            GTask *task = g_task_new(td, cancellable, callback, user_data);
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to retrieve buffer content");
            g_object_unref(task);
            return;
        }

        SaveTaskData *data = g_new0(SaveTaskData, 1);
        data->path = g_strdup(file_path);
        data->content = g_strdup(content);

        GTask *task = g_task_new(td, cancellable, callback, user_data);
        g_task_set_task_data(task, data, (GDestroyNotify)save_task_data_free);

        g_task_run_in_thread(task, save_as_task_thread);
        g_object_unref(task);
    }
}

gboolean tab_document_save_as_finish(TabDocument *td, GAsyncResult *result, GError **error)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(G_IS_ASYNC_RESULT(result), FALSE);

    gboolean ok;

    /* Handle DocumentManager async result */
    if (td->doc_manager) {
        ok = document_manager_save_as_finish(td->doc_manager, result, error);
        /* DocumentManager handles the file path adoption internally */
    } else {
        /* Handle fallback GTask result */
        g_return_val_if_fail(G_IS_TASK(result), FALSE);
        ok = g_task_propagate_boolean(G_TASK(result), error);

        if (ok) {
            /* For fallback case, adopt current buffer for path */
            SaveTaskData *data = (SaveTaskData *)g_task_get_task_data(G_TASK(result));
            if (data && td->doc_manager) {
                document_manager_adopt_current_buffer(td->doc_manager, data->path, NULL);
            }
        }
    }

    if (!ok) return FALSE;

    /* Update TabDocument metadata */
    if (td->doc_manager) {
        /* DocumentManager case - get path from DocumentManager */
        const gchar *saved_path = document_manager_get_file_path(td->doc_manager);
        if (saved_path) {
            g_free(td->file_path);
            td->file_path = g_strdup(saved_path);

            g_free(td->tab_title);
            g_autofree char *basename = g_path_get_basename(td->file_path);
            td->tab_title = g_steal_pointer(&basename);
        }
    } else {
        /* Fallback case - get path from task data */
        SaveTaskData *data = (SaveTaskData *)g_task_get_task_data(G_TASK(result));
        if (data) {
            g_free(td->file_path);
            td->file_path = g_strdup(data->path);

            g_free(td->tab_title);
            g_autofree char *basename = g_path_get_basename(td->file_path);
            td->tab_title = g_steal_pointer(&basename);
        }
    }

    /* Mark as clean via standard API (also triggers callbacks) */
    tab_document_set_modified(td, FALSE);

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
    if (!td->doc_manager) return FALSE;

    DocumentState state = document_manager_get_state(td->doc_manager);
    return (state == DOC_STATE_DIRTY || state == DOC_STATE_DRAFT);
}

void tab_document_set_modified(TabDocument *td, gboolean modified)
{
    g_return_if_fail(td != NULL);

    /* DocumentManager is now the single source of truth for dirty state.
     * This function is now mostly a no-op, but we keep it for API compatibility.
     * Real state changes happen through DocumentManager's buffer change handlers. */

    g_debug("TabDocument set_modified called: modified=%s (DocumentManager handles state)",
            modified ? "TRUE" : "FALSE");

    /* Note: We don't manually call the dirty state callback here because
     * DocumentManager will trigger on_document_manager_state_changed which
     * will call the callback when state actually changes. */
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
                                        GtkWidget *stack_container, GtkWidget *original_text_view)
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

    td->stack = stack_container;

    /* Store reference to original text view if switching to source mode */
    if (is_source_mode && original_text_view && GTK_IS_WIDGET(original_text_view) && G_IS_OBJECT(original_text_view)) {
        /* Clear any existing reference */
        if (td->original_text_view_has_ref && td->original_text_view && G_IS_OBJECT(td->original_text_view)) {
            g_object_unref(td->original_text_view);
        }
        td->original_text_view = original_text_view;
        g_object_ref(original_text_view);  /* Keep reference */
        td->original_text_view_has_ref = TRUE;
    }

    g_debug("TabDocument source mode set to: %s", is_source_mode ? "source" : "WYSIWYG");
}

void tab_document_clear_source_mode_state(TabDocument *td)
{
    g_return_if_fail(td != NULL);

    /* We now use the explicit ref flag instead of mode state */

    td->is_source_mode = FALSE;
    td->source_text_view = NULL;

    /* Clean up source buffer */
    if (td->source_buffer && G_IS_OBJECT(td->source_buffer)) {
        g_object_unref(td->source_buffer);
    }
    td->source_buffer = NULL;

    /* Clean up original text view reference (only if we took a ref) */
    if (td->original_text_view_has_ref && td->original_text_view && G_IS_OBJECT(td->original_text_view)) {
        g_object_unref(td->original_text_view);
    }
    td->original_text_view = NULL;
    td->original_text_view_has_ref = FALSE;
    td->stack = NULL;

    g_debug("TabDocument source mode state cleared");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * Stack-based source view management - new approach
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean tab_document_switch_to_source_view(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(!td->is_welcome, FALSE);
    g_return_val_if_fail(td->stack != NULL, FALSE);

    if (td->is_source_mode) {
        return TRUE; /* Already in source mode */
    }

    /* Get stack container */
    GtkStack *stack = GTK_STACK(td->stack);
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
    g_return_val_if_fail(td->stack != NULL, FALSE);

    if (!td->is_source_mode) {
        return TRUE; /* Already in WYSIWYG mode */
    }

    /* Get stack container */
    GtkStack *stack = GTK_STACK(td->stack);
    if (!GTK_IS_STACK(stack)) {
        g_warning("Tab document container is not a stack");
        return FALSE;
    }

    /* Sync content from source to WYSIWYG */
    if (!tab_document_sync_source_to_wysiwyg(td)) {
        g_warning("Failed to sync source content to WYSIWYG view");
        return FALSE;
    }

    /* Source buffer change tracking removed - DocumentManager handles all state */

    /* Release explicit ref grabbed when entering source mode */
    if (td->original_text_view_has_ref && td->original_text_view && G_IS_OBJECT(td->original_text_view)) {
        g_object_unref(td->original_text_view); /* prevent leak on mode toggle */
        g_debug("Unreffed original_text_view on switch to WYSIWYG");
        td->original_text_view_has_ref = FALSE;
        td->original_text_view = NULL;
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

    g_debug("Starting WYSIWYG to source sync");

    /* Cancel any pending reparse operations to prevent interference */
    cancel_pending_reparse_markdown(td->buffer);

    /* Block all buffer change signals during sync to prevent false dirty state */

    if (td->doc_manager) {
        document_manager_block_buffer_signals(td->doc_manager);
        g_debug("Blocked DocumentManager buffer signals during sync");
    }

    /* Get WYSIWYG content as markdown */
    g_autofree char *markdown = cm_render_buffer_to_markdown(td->buffer);
    if (!markdown) {
        g_warning("Failed to export WYSIWYG buffer to markdown");

        /* Unblock signals before returning */
        if (td->doc_manager) {
            document_manager_unblock_buffer_signals(td->doc_manager);
        }
        return FALSE;
    }

    /* Set markdown content in source buffer while preserving undo history */
    /* Important: Use delete+insert instead of set_text to preserve undo history */
    /* This allows users to undo through view switches, maintaining editing context */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(td->source_buffer, &start, &end);
    gtk_text_buffer_delete(td->source_buffer, &start, &end);
    gtk_text_buffer_get_start_iter(td->source_buffer, &start);
    gtk_text_buffer_insert(td->source_buffer, &start, markdown, -1);

    /* Note: Do not update baseline here - view switching should preserve dirty state */
    /* Baseline updates should only happen during actual file operations (load/save) */

    /* Unblock buffer change signals */

    if (td->doc_manager) {
        document_manager_unblock_buffer_signals(td->doc_manager);
        g_debug("Unblocked DocumentManager buffer signals after sync");
    }

    g_debug("Synced WYSIWYG content to source view without triggering dirty state");
    return TRUE;
}

static gboolean tab_document_sync_source_to_wysiwyg(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    g_return_val_if_fail(td->buffer != NULL, FALSE);
    g_return_val_if_fail(td->source_buffer != NULL, FALSE);
    g_return_val_if_fail(td->text_view != NULL, FALSE);

    g_debug("Starting source to WYSIWYG sync");

    /* Cancel any pending reparse operations to prevent interference */
    cancel_pending_reparse_markdown(td->buffer);

    /* Block all buffer change signals during sync to prevent false dirty state */

    if (td->doc_manager) {
        document_manager_block_buffer_signals(td->doc_manager);
        g_debug("Blocked DocumentManager buffer signals during sync");
    }

    /* Get source content */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(td->source_buffer, &start, &end);
    g_autofree char *source_markdown = gtk_text_buffer_get_text(td->source_buffer, &start, &end, FALSE);

    if (!source_markdown) {
        source_markdown = g_strdup("");
    }

    /* Re-render markdown in WYSIWYG view */
    cm_render_markdown_to_buffer(td->buffer, source_markdown, GTK_TEXT_VIEW(td->text_view), NULL);

    /* Note: Do not update baseline here - view switching should preserve dirty state */
    /* Baseline updates should only happen during actual file operations (load/save) */

    /* Unblock buffer change signals */

    if (td->doc_manager) {
        document_manager_unblock_buffer_signals(td->doc_manager);
        g_debug("Unblocked DocumentManager buffer signals after sync");
    }

    g_debug("Synced source content to WYSIWYG view without triggering dirty state");
    return TRUE;
}


/* ═══════════════════════════════════════════════════════════════════════════════
 * WELCOME SCREEN CALLBACKS - Handle welcome screen button clicks
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void on_welcome_new_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;  // Unused parameter
    TabDocument *td = g_object_get_data(G_OBJECT(button), "tab-document");
    if (!td) {
        g_warning("TabDocument not found for welcome new button");
        return;
    }

    g_debug("Welcome 'New Document' button clicked");

    /* Find the app through widget hierarchy */
    GtkWidget *window = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
    if (!window) {
        g_warning("Could not find window for welcome new action");
        return;
    }

    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(window));
    if (!app) {
        g_warning("Could not find application for welcome new action");
        return;
    }

    /* Get tab manager and create new document tab */
    TabManager *tm = gtktext_get_tab_manager(app);
    if (tm) {
        tab_manager_new_document(tm, _("Untitled"));
        g_debug("Created new document from welcome screen");
    }
}

static void on_welcome_open_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;  // Unused parameter
    TabDocument *td = g_object_get_data(G_OBJECT(button), "tab-document");
    if (!td) {
        g_warning("TabDocument not found for welcome open button");
        return;
    }

    g_debug("Welcome 'Open File' button clicked");

    /* Find the app through widget hierarchy */
    GtkWidget *window = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
    if (!window) {
        g_warning("Could not find window for welcome open action");
        return;
    }

    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(window));
    if (!app) {
        g_warning("Could not find application for welcome open action");
        return;
    }

    /* Trigger the existing open action */
    GAction *open_action = g_action_map_lookup_action(G_ACTION_MAP(app), "open");
    if (open_action) {
        g_action_activate(open_action, NULL);
        g_debug("Triggered open action from welcome screen");
    } else {
        g_warning("Could not find 'open' action");
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * INTERNAL ACCESSORS - Functions for accessing internal fields
 * ═══════════════════════════════════════════════════════════════════════════════ */

gboolean tab_document_is_being_destroyed(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, FALSE);
    return td->being_destroyed;
}

DocumentManager *tab_document_get_document_manager(TabDocument *td)
{
    g_return_val_if_fail(td != NULL, NULL);
    return td->doc_manager;
}
