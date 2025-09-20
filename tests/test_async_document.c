/* C ULTRA-MIN TEMPLATE
   Purpose: GLib tests for DocumentManager async open/save operations
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-19 - tests/test_async_document.c
   Phase 2: Comprehensive async I/O testing
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <gtktext/document/document_manager.h>
#include <gtktext/render/safe_helpers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * META - Test constants and forward declarations
 * ═══════════════════════════════════════════════════════════════════════════════ */

#define TEST_TEXT_CONTENT "# Test Document\n\nThis is a test document for async operations.\n"
#define TEST_TEXT_MODIFIED "# Modified Test Document\n\nThis document has been modified.\n"
#define TEST_FILE_PATH "/tmp/test_async_document.md"
#define TEST_SAVE_PATH "/tmp/test_async_save.md"

/* Test context for async operations */
typedef struct {
    GMainLoop *loop;
    DocumentManager *dm;
    GtkTextBuffer *buffer;
    GtkWindow *window;
    gboolean operation_completed;
    gboolean operation_success;
    GError *operation_error;
    gchar *test_content;
} AsyncTestContext;

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Test utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static gboolean timeout_quit_loop(gpointer data)
{
    g_main_loop_quit((GMainLoop*)data);
    return G_SOURCE_REMOVE;
}

static void async_test_context_free(AsyncTestContext *ctx)
{
    if (!ctx) return;

    if (ctx->loop) {
        g_main_loop_unref(ctx->loop);
    }
    if (ctx->dm) {
        document_manager_free(ctx->dm);
    }
    if (ctx->buffer) {
        g_object_unref(ctx->buffer);
    }
    if (ctx->window) {
        gtk_window_destroy(ctx->window);
    }
    if (ctx->operation_error) {
        g_error_free(ctx->operation_error);
    }
    g_free(ctx->test_content);
    g_free(ctx);
}

static AsyncTestContext *async_test_context_new(void)
{
    AsyncTestContext *ctx = g_new0(AsyncTestContext, 1);

    ctx->loop = g_main_loop_new(NULL, FALSE);
    ctx->buffer = gtk_text_buffer_new(NULL);
    ctx->window = GTK_WINDOW(gtk_window_new());
    ctx->dm = document_manager_new(ctx->buffer, ctx->window);

    return ctx;
}

static void create_test_file(const gchar *path, const gchar *content)
{
    GError *error = NULL;
    if (!g_file_set_contents(path, content, -1, &error)) {
        g_test_fail_printf("Failed to create test file: %s", error->message);
        g_error_free(error);
    }
}

static void cleanup_test_file(const gchar *path)
{
    unlink(path);
}

static gchar *read_test_file(const gchar *path)
{
    GError *error = NULL;
    gchar *content = NULL;

    if (!g_file_get_contents(path, &content, NULL, &error)) {
        g_test_fail_printf("Failed to read test file: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    return content;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HANDLERS - Async callback handlers for tests
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void async_open_callback(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    AsyncTestContext *ctx = (AsyncTestContext *)user_data;
    DocumentManager *dm = GTKTEXT_DOCUMENT_MANAGER(source_object);

    ctx->operation_success = document_manager_open_finish(dm, result, &ctx->operation_error);
    ctx->operation_completed = TRUE;

    if (ctx->loop && g_main_loop_is_running(ctx->loop)) {
        g_main_loop_quit(ctx->loop);
    }
}

static void async_save_callback(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    AsyncTestContext *ctx = (AsyncTestContext *)user_data;
    DocumentManager *dm = GTKTEXT_DOCUMENT_MANAGER(source_object);

    ctx->operation_success = document_manager_save_finish(dm, result, &ctx->operation_error);
    ctx->operation_completed = TRUE;

    if (ctx->loop && g_main_loop_is_running(ctx->loop)) {
        g_main_loop_quit(ctx->loop);
    }
}

static void async_save_as_callback(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    AsyncTestContext *ctx = (AsyncTestContext *)user_data;
    DocumentManager *dm = GTKTEXT_DOCUMENT_MANAGER(source_object);

    ctx->operation_success = document_manager_save_as_finish(dm, result, &ctx->operation_error);
    ctx->operation_completed = TRUE;

    if (ctx->loop && g_main_loop_is_running(ctx->loop)) {
        g_main_loop_quit(ctx->loop);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * TEST CASES - Individual test functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void test_document_manager_async_open_success(void)
{
    g_test_summary("Test successful async file opening");

    AsyncTestContext *ctx = async_test_context_new();
    create_test_file(TEST_FILE_PATH, TEST_TEXT_CONTENT);

    /* Start async open operation */
    document_manager_open_async(ctx->dm, TEST_FILE_PATH, NULL, async_open_callback, ctx);

    /* Wait for completion with timeout */
    guint timeout_id = g_timeout_add_seconds(5, timeout_quit_loop, ctx->loop);
    g_main_loop_run(ctx->loop);
    g_source_remove(timeout_id);

    /* Verify results */
    g_assert_true(ctx->operation_completed);
    g_assert_true(ctx->operation_success);
    g_assert_no_error(ctx->operation_error);

    /* Verify document state */
    g_assert_cmpint(document_manager_get_state(ctx->dm), ==, DOC_STATE_CLEAN);
    g_assert_false(document_manager_is_untitled(ctx->dm));

    const gchar *file_path = document_manager_get_file_path(ctx->dm);
    g_assert_nonnull(file_path);
    g_assert_cmpstr(file_path, ==, TEST_FILE_PATH);

    /* Verify buffer content */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ctx->buffer, &start, &end);
    gchar *buffer_text = gtk_text_buffer_get_text(ctx->buffer, &start, &end, FALSE);
    g_assert_cmpstr(buffer_text, ==, TEST_TEXT_CONTENT);
    g_free(buffer_text);

    cleanup_test_file(TEST_FILE_PATH);
    async_test_context_free(ctx);
}

static void test_document_manager_async_open_nonexistent(void)
{
    g_test_summary("Test async open of nonexistent file");

    AsyncTestContext *ctx = async_test_context_new();

    /* Ensure file doesn't exist */
    cleanup_test_file("/tmp/nonexistent_file.md");

    /* Start async open operation */
    document_manager_open_async(ctx->dm, "/tmp/nonexistent_file.md", NULL, async_open_callback, ctx);

    /* Wait for completion */
    guint timeout_id = g_timeout_add_seconds(5, timeout_quit_loop, ctx->loop);
    g_main_loop_run(ctx->loop);
    g_source_remove(timeout_id);

    /* Verify results */
    g_assert_true(ctx->operation_completed);
    g_assert_false(ctx->operation_success);
    g_assert_error(ctx->operation_error, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_NOT_FOUND);

    async_test_context_free(ctx);
}

static void test_document_manager_async_save_as_success(void)
{
    g_test_summary("Test successful async save as operation");

    AsyncTestContext *ctx = async_test_context_new();
    cleanup_test_file(TEST_SAVE_PATH);

    /* Set buffer content */
    gtk_text_buffer_set_text(ctx->buffer, TEST_TEXT_MODIFIED, -1);

    /* Debug: Check what the buffer contains before save */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ctx->buffer, &start, &end);
    gchar *buffer_text = gtk_text_buffer_get_text(ctx->buffer, &start, &end, FALSE);
    g_debug("Buffer content before save: [%s]", buffer_text);
    g_free(buffer_text);

    /* Start async save as operation */
    document_manager_save_as_async(ctx->dm, TEST_SAVE_PATH, NULL, async_save_as_callback, ctx);

    /* Wait for completion */
    guint timeout_id = g_timeout_add_seconds(5, timeout_quit_loop, ctx->loop);
    g_main_loop_run(ctx->loop);
    g_source_remove(timeout_id);

    /* Verify results */
    g_assert_true(ctx->operation_completed);
    g_assert_true(ctx->operation_success);
    g_assert_no_error(ctx->operation_error);

    /* Verify document state */
    g_assert_cmpint(document_manager_get_state(ctx->dm), ==, DOC_STATE_CLEAN);
    g_assert_false(document_manager_is_untitled(ctx->dm));

    const gchar *file_path = document_manager_get_file_path(ctx->dm);
    g_assert_nonnull(file_path);
    g_assert_cmpstr(file_path, ==, TEST_SAVE_PATH);

    /* Verify file was actually saved - debug read */
    gchar *saved_content = read_test_file(TEST_SAVE_PATH);
    g_assert_nonnull(saved_content);
    g_debug("Saved file content: [%s] (length=%zu)", saved_content, strlen(saved_content));
    g_debug("Expected content: [%s] (length=%zu)", TEST_TEXT_MODIFIED, strlen(TEST_TEXT_MODIFIED));

    /* Check that content matches what we expect */
    g_assert_cmpstr(saved_content, ==, TEST_TEXT_MODIFIED);

    g_free(saved_content);

    cleanup_test_file(TEST_SAVE_PATH);
    async_test_context_free(ctx);
}

static void test_document_manager_async_save_readonly(void)
{
    g_test_summary("Test async save to readonly location");

    AsyncTestContext *ctx = async_test_context_new();

    /* Set buffer content */
    gtk_text_buffer_set_text(ctx->buffer, TEST_TEXT_CONTENT, -1);

    /* Try to save to readonly location (assuming /root is not writable) */
    document_manager_save_as_async(ctx->dm, "/root/readonly_test.md", NULL, async_save_as_callback, ctx);

    /* Wait for completion */
    guint timeout_id = g_timeout_add_seconds(5, timeout_quit_loop, ctx->loop);
    g_main_loop_run(ctx->loop);
    g_source_remove(timeout_id);

    /* Verify results */
    g_assert_true(ctx->operation_completed);
    g_assert_false(ctx->operation_success);
    g_assert_error(ctx->operation_error, GTKTEXT_DOCUMENT_ERROR, GTKTEXT_DOCUMENT_ERROR_READONLY);

    async_test_context_free(ctx);
}

static void test_document_manager_open_then_save_cycle(void)
{
    g_test_summary("Test complete open -> modify -> save cycle");

    AsyncTestContext *ctx = async_test_context_new();
    create_test_file(TEST_FILE_PATH, TEST_TEXT_CONTENT);

    /* Step 1: Open file */
    document_manager_open_async(ctx->dm, TEST_FILE_PATH, NULL, async_open_callback, ctx);

    guint timeout_id = g_timeout_add_seconds(5, timeout_quit_loop, ctx->loop);
    g_main_loop_run(ctx->loop);
    g_source_remove(timeout_id);

    g_assert_true(ctx->operation_completed);
    g_assert_true(ctx->operation_success);
    g_assert_cmpint(document_manager_get_state(ctx->dm), ==, DOC_STATE_CLEAN);

    /* DocumentManager finalization now happens automatically during open operation */

    /* Process any pending events */
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }

    /* Step 2: Modify content - allow buffer signal processing */
    gtk_text_buffer_set_text(ctx->buffer, TEST_TEXT_MODIFIED, -1);

    /* Process pending events to ensure DocumentManager sees the change */
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }

    /* Wait for debounce timer to expire (500ms + margin) */
    g_timeout_add(600, timeout_quit_loop, ctx->loop);
    g_main_loop_run(ctx->loop);

    g_assert_cmpint(document_manager_get_state(ctx->dm), ==, DOC_STATE_DIRTY);

    /* Step 3: Save changes */
    ctx->operation_completed = FALSE;
    ctx->operation_success = FALSE;
    document_manager_save_async(ctx->dm, NULL, async_save_callback, ctx);

    timeout_id = g_timeout_add_seconds(5, timeout_quit_loop, ctx->loop);
    g_main_loop_run(ctx->loop);
    g_source_remove(timeout_id);

    g_assert_true(ctx->operation_completed);
    g_assert_true(ctx->operation_success);
    g_assert_cmpint(document_manager_get_state(ctx->dm), ==, DOC_STATE_CLEAN);

    /* Verify file was updated */
    gchar *saved_content = read_test_file(TEST_FILE_PATH);
    g_assert_nonnull(saved_content);
    g_assert_cmpstr(saved_content, ==, TEST_TEXT_MODIFIED);
    g_free(saved_content);

    cleanup_test_file(TEST_FILE_PATH);
    async_test_context_free(ctx);
}

static void test_safe_helpers_basic_validation(void)
{
    g_test_summary("Test safe helpers basic validation functions");

    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    GtkTextIter iter;
    GError *error = NULL;

    /* Test buffer validation */
    g_assert_true(safe_buffer_is_valid(buffer, &error));
    g_assert_no_error(error);

    g_assert_false(safe_buffer_is_valid(NULL, &error));
    g_assert_error(error, GTKTEXT_RENDER_ERROR, GTKTEXT_RENDER_ERROR_INVALID_BUFFER);
    g_clear_error(&error);

    /* Test iterator validation */
    gtk_text_buffer_get_start_iter(buffer, &iter);
    g_assert_true(safe_iter_is_valid(buffer, &iter, &error));
    g_assert_no_error(error);

    /* Test safe text insertion */
    SafeRenderResult result = safe_buffer_insert_text(buffer, &iter, "Hello World", -1);
    g_assert_true(result.success);
    g_assert_null(result.error);

    /* Verify text was inserted */
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    g_assert_cmpstr(text, ==, "Hello World");
    g_free(text);

    g_object_unref(buffer);
}

static void test_safe_helpers_tag_operations(void)
{
    g_test_summary("Test safe helpers tag creation and application");

    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    GtkTextIter start, end;

    /* Insert some text */
    gtk_text_buffer_set_text(buffer, "Test text for tagging", -1);
    gtk_text_buffer_get_bounds(buffer, &start, &end);

    /* Test safe tag creation */
    GtkTextTag *tag = NULL;
    SafeRenderResult result = safe_buffer_get_or_create_tag(buffer, "test-tag", &tag);
    g_assert_true(result.success);
    g_assert_nonnull(tag);
    g_assert_true(GTK_IS_TEXT_TAG(tag));

    /* Test safe tag application */
    result = safe_buffer_apply_tag(buffer, tag, &start, &end);
    g_assert_true(result.success);

    /* Test tag application by name */
    result = safe_buffer_apply_tag_by_name(buffer, "another-tag", &start, &end);
    g_assert_true(result.success);

    g_object_unref(buffer);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * WIRING - Test suite setup and registration
 * ═══════════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    /* Initialize GTK for testing */
    gtk_init();

    /* Register async DocumentManager tests */
    g_test_add_func("/async/document_manager/open_success",
                    test_document_manager_async_open_success);
    g_test_add_func("/async/document_manager/open_nonexistent",
                    test_document_manager_async_open_nonexistent);
    g_test_add_func("/async/document_manager/save_as_success",
                    test_document_manager_async_save_as_success);
    g_test_add_func("/async/document_manager/save_readonly",
                    test_document_manager_async_save_readonly);
    g_test_add_func("/async/document_manager/open_save_cycle",
                    test_document_manager_open_then_save_cycle);

    /* Register safe helpers tests */
    g_test_add_func("/safe_helpers/basic_validation",
                    test_safe_helpers_basic_validation);
    g_test_add_func("/safe_helpers/tag_operations",
                    test_safe_helpers_tag_operations);

    /* Run all tests */
    int result = g_test_run();

    return result;
}