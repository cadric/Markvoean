#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmark.h>
#include <gtk/gtk.h>
#include <assert.h>

#include "../include/cmrender.h" // Updated to new renderer
#include "../include/tag_util.h" // For tag name storage

// Test function prototypes
static void test_import_markdown(void);
static void test_export_markdown(void);

int main(int argc, char *argv[]) {
    // Initialize GTK before our tests
    gtk_init();
    
    printf("Running cmark tests...\n");
    
    // Run tests
    test_import_markdown();
    test_export_markdown();
    
    printf("All tests passed!\n");
    return EXIT_SUCCESS;
}

// Test markdown import function
static void test_import_markdown(void) {
    printf("Testing cm_render_markdown_to_buffer()...\n");
    
    // Create a buffer for testing
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    
    // Test basic markdown import
    const char *test_markdown = "**Bold text** and *italic text*\n";
    gboolean result = cm_render_markdown_to_buffer(buffer, test_markdown);
    
    // Verify import succeeded
    assert(result == TRUE);
    
    // Additional import tests can be added here
    
    printf("Import test passed.\n");
    g_object_unref(buffer);
}

// Test markdown export function
static void test_export_markdown(void) {
    printf("Testing cm_render_buffer_to_markdown()...\n");

    // Create a buffer for testing
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);

    // Create tags needed for formatting
    GtkTextTag *bold_tag1 = gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    ensure_tag_name_stored(bold_tag1, "bold");
    GtkTextTag *italic_tag1 = gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    ensure_tag_name_stored(italic_tag1, "italic");
    GtkTextTag *code_tag1 = gtk_text_buffer_create_tag(buffer, "code", "family", "monospace", NULL);
    ensure_tag_name_stored(code_tag1, "code");

    // Test for bold and italic combined
    gtk_text_buffer_set_text(buffer, "This is regular text and this is bold/italic.", -1);

    // Apply formatting: Get iterators for the text to format
    GtkTextIter start_iter_bi, end_iter_bi; // Renamed to avoid conflict
    gtk_text_buffer_get_iter_at_offset(buffer, &start_iter_bi, 27); // "this is bold/italic"
    gtk_text_buffer_get_iter_at_offset(buffer, &end_iter_bi, 46);   // End of "bold/italic"

    // Apply both bold and italic tags
    gtk_text_buffer_apply_tag(buffer, bold_tag1, &start_iter_bi, &end_iter_bi);
    gtk_text_buffer_apply_tag(buffer, italic_tag1, &start_iter_bi, &end_iter_bi);

    // Export the buffer to markdown
    char *exported_bi = cm_render_buffer_to_markdown(buffer); // Renamed to avoid conflict

    // Check specific format: We should have "***bold/italic***" with three stars on each side
    const char *expected_bi = "***bold/italic***";
    assert(strstr(exported_bi, expected_bi) != NULL);
    g_free(exported_bi); // Free memory

    printf("Combined bold/italic format test passed.\n");

    // Clear buffer for next test case
    gtk_text_buffer_set_text(buffer, "", -1);


    // Set up a simple buffer with formatting for another test
    GtkTextIter start_iter_simple, end_iter_simple; // Renamed
    gtk_text_buffer_get_start_iter(buffer, &start_iter_simple);

    // Add some formatted text to the buffer
    // Tags bold_tag1 and italic_tag1 are reused from above, already have names stored.

    gtk_text_buffer_insert(buffer, &start_iter_simple, "Normal text. ", -1);

    // Insert bold text
    gtk_text_buffer_get_end_iter(buffer, &end_iter_simple);
    gtk_text_buffer_insert_with_tags(buffer, &end_iter_simple, "Bold text. ", -1, bold_tag1, NULL);


    // Insert italic text
    gtk_text_buffer_get_end_iter(buffer, &end_iter_simple);
    gtk_text_buffer_insert_with_tags(buffer, &end_iter_simple, "Italic text.", -1, italic_tag1, NULL);


    // Export the buffer to markdown
    char *exported_markdown = cm_render_buffer_to_markdown(buffer);

    // Verify the export result contains the expected text
    assert(exported_markdown != NULL);
    assert(strstr(exported_markdown, "Normal text.") != NULL);
    assert(strstr(exported_markdown, "**Bold text.**") != NULL);
    assert(strstr(exported_markdown, "*Italic text.*") != NULL);

    // Free allocated memory
    g_free(exported_markdown);


    // Test with a code tag
    gtk_text_buffer_set_text(buffer, "", -1); // Clear buffer
    gtk_text_buffer_get_start_iter(buffer, &start_iter_simple);
    gtk_text_buffer_insert_with_tags(buffer, &start_iter_simple, "some code", -1, code_tag1, NULL);
    char *exported_code = cm_render_buffer_to_markdown(buffer);
    assert(exported_code != NULL);
    assert(strcmp(exported_code, "`some code`") == 0);
    g_free(exported_code);
    printf("Code tag export test passed.\n");


    g_object_unref(buffer);

    printf("Export test passed.\n");
}
