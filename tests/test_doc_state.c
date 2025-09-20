/* C ULTRA-MIN TEMPLATE
   Purpose: Unit tests for doc_state module
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.0] - 2025-09-20 - tests/test_doc_state.c
   Added: Comprehensive tests for hash-based document state tracking
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtktext/document/doc_state.h>
#include <gtktext/render/cmrender.h>
#include <gtk/gtk.h>
#include <glib.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * TEST HELPERS - Utility functions for testing
 * ═══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    GtkTextBuffer *buffer;
    DocState state;
} TestFixture;

static void test_fixture_setup(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    fixture->buffer = gtk_text_buffer_new(NULL);
    doc_state_init(&fixture->state, fixture->buffer);
}

static void test_fixture_teardown(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    g_object_unref(fixture->buffer);
}

static void set_buffer_text(GtkTextBuffer *buffer, const char *text)
{
    gtk_text_buffer_set_text(buffer, text ? text : "", -1);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * BASIC STATE TESTS - Core functionality tests
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void test_init_state(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Initial state should be clean with zero hashes */
    g_assert_false(doc_is_dirty(&fixture->state));
    g_assert_cmpuint(fixture->state.saved_hash, ==, 0);
    g_assert_cmpuint(fixture->state.current_hash, ==, 0);
    g_assert_true(fixture->state.buffer == fixture->buffer);
}

static void test_new_document_clean(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* New document should be clean */
    set_buffer_text(fixture->buffer, "");
    doc_mark_loaded_or_new(&fixture->state);

    g_assert_false(doc_is_dirty(&fixture->state));
    g_assert_cmpuint(fixture->state.saved_hash, ==, fixture->state.current_hash);
}

static void test_loaded_document_clean(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Loaded document should be clean */
    set_buffer_text(fixture->buffer, "# Test Document\n\nSome content.");
    doc_mark_loaded_or_new(&fixture->state);

    g_assert_false(doc_is_dirty(&fixture->state));
    g_assert_cmpuint(fixture->state.saved_hash, ==, fixture->state.current_hash);
    g_assert_cmpuint(fixture->state.saved_hash, !=, 0);  /* Non-empty content has non-zero hash */
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * USER MUTATION TESTS - Testing state changes on user input
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void test_typing_makes_dirty(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Start with clean document */
    set_buffer_text(fixture->buffer, "Original text");
    doc_mark_loaded_or_new(&fixture->state);
    g_assert_false(doc_is_dirty(&fixture->state));

    /* Typing should make it dirty */
    set_buffer_text(fixture->buffer, "Original text modified");
    doc_on_user_mutation(&fixture->state);

    g_assert_true(doc_is_dirty(&fixture->state));
    g_assert_cmpuint(fixture->state.current_hash, !=, fixture->state.saved_hash);
}

static void test_delete_to_original_clean(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Start with clean document */
    const char *original = "Original text";
    set_buffer_text(fixture->buffer, original);
    doc_mark_loaded_or_new(&fixture->state);
    uint64_t original_hash = fixture->state.saved_hash;

    /* Modify and become dirty */
    set_buffer_text(fixture->buffer, "Modified text");
    doc_on_user_mutation(&fixture->state);
    g_assert_true(doc_is_dirty(&fixture->state));

    /* Delete back to original should make clean */
    set_buffer_text(fixture->buffer, original);
    doc_recompute_state_from_buffer(&fixture->state);

    g_assert_false(doc_is_dirty(&fixture->state));
    g_assert_cmpuint(fixture->state.current_hash, ==, original_hash);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * SAVE/LOAD TESTS - Testing save and load operations
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void test_save_makes_clean(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Start dirty */
    set_buffer_text(fixture->buffer, "Initial");
    doc_mark_loaded_or_new(&fixture->state);

    set_buffer_text(fixture->buffer, "Modified");
    doc_on_user_mutation(&fixture->state);
    g_assert_true(doc_is_dirty(&fixture->state));

    /* Save should make clean */
    doc_on_saved(&fixture->state);

    g_assert_false(doc_is_dirty(&fixture->state));
    g_assert_cmpuint(fixture->state.current_hash, ==, fixture->state.saved_hash);
}

static void test_reload_makes_clean(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Start clean, then modify */
    set_buffer_text(fixture->buffer, "Original");
    doc_mark_loaded_or_new(&fixture->state);

    set_buffer_text(fixture->buffer, "Modified");
    doc_on_user_mutation(&fixture->state);
    g_assert_true(doc_is_dirty(&fixture->state));

    /* Reload from disk (simulate) */
    set_buffer_text(fixture->buffer, "Reloaded content");
    doc_mark_loaded_or_new(&fixture->state);

    g_assert_false(doc_is_dirty(&fixture->state));
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * HASH CONSISTENCY TESTS - Testing hash computation
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void test_identical_content_same_hash(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Same content should produce same hash */
    set_buffer_text(fixture->buffer, "Test content");
    uint64_t hash1 = doc_compute_hash(fixture->buffer);

    set_buffer_text(fixture->buffer, "Test content");
    uint64_t hash2 = doc_compute_hash(fixture->buffer);

    g_assert_cmpuint(hash1, ==, hash2);
    g_assert_cmpuint(hash1, !=, 0);
}

static void test_different_content_different_hash(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Different content should produce different hashes */
    set_buffer_text(fixture->buffer, "Content A");
    uint64_t hash1 = doc_compute_hash(fixture->buffer);

    set_buffer_text(fixture->buffer, "Content B");
    uint64_t hash2 = doc_compute_hash(fixture->buffer);

    g_assert_cmpuint(hash1, !=, hash2);
}

static void test_empty_content_hash(TestFixture *fixture, gconstpointer user_data)
{
    (void)user_data;

    /* Empty content should have consistent hash */
    set_buffer_text(fixture->buffer, "");
    uint64_t hash1 = doc_compute_hash(fixture->buffer);

    set_buffer_text(fixture->buffer, "");
    uint64_t hash2 = doc_compute_hash(fixture->buffer);

    g_assert_cmpuint(hash1, ==, hash2);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * TEXT HASH UTILITY TESTS - Testing utility functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void test_text_hash_utility(void)
{
    /* Test text hash utility function */
    const char *text = "Hello, World!";
    uint64_t hash1 = doc_compute_text_hash(text, 0);  /* Auto-detect length */
    uint64_t hash2 = doc_compute_text_hash(text, strlen(text));  /* Explicit length */

    g_assert_cmpuint(hash1, ==, hash2);
    g_assert_cmpuint(hash1, !=, 0);

    /* NULL text should work */
    uint64_t null_hash = doc_compute_text_hash(NULL, 0);
    uint64_t empty_hash = doc_compute_text_hash("", 0);
    g_assert_cmpuint(null_hash, ==, empty_hash);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * TEST REGISTRATION - Register all tests
 * ═══════════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    gtk_test_init(&argc, &argv, NULL);

    /* Basic state tests */
    g_test_add("/doc_state/init", TestFixture, NULL,
               test_fixture_setup, test_init_state, test_fixture_teardown);

    g_test_add("/doc_state/new_document_clean", TestFixture, NULL,
               test_fixture_setup, test_new_document_clean, test_fixture_teardown);

    g_test_add("/doc_state/loaded_document_clean", TestFixture, NULL,
               test_fixture_setup, test_loaded_document_clean, test_fixture_teardown);

    /* User mutation tests */
    g_test_add("/doc_state/typing_makes_dirty", TestFixture, NULL,
               test_fixture_setup, test_typing_makes_dirty, test_fixture_teardown);

    g_test_add("/doc_state/delete_to_original_clean", TestFixture, NULL,
               test_fixture_setup, test_delete_to_original_clean, test_fixture_teardown);

    /* Save/load tests */
    g_test_add("/doc_state/save_makes_clean", TestFixture, NULL,
               test_fixture_setup, test_save_makes_clean, test_fixture_teardown);

    g_test_add("/doc_state/reload_makes_clean", TestFixture, NULL,
               test_fixture_setup, test_reload_makes_clean, test_fixture_teardown);

    /* Hash consistency tests */
    g_test_add("/doc_state/identical_content_same_hash", TestFixture, NULL,
               test_fixture_setup, test_identical_content_same_hash, test_fixture_teardown);

    g_test_add("/doc_state/different_content_different_hash", TestFixture, NULL,
               test_fixture_setup, test_different_content_different_hash, test_fixture_teardown);

    g_test_add("/doc_state/empty_content_hash", TestFixture, NULL,
               test_fixture_setup, test_empty_content_hash, test_fixture_teardown);

    /* Utility tests */
    g_test_add_func("/doc_state/text_hash_utility", test_text_hash_utility);

    return g_test_run();
}