#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cmark.h>
#include <gtk/gtk.h>
#include <assert.h>

#include "gtktext_cmark.h" // Our project's cmark header

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
    printf("Testing import_markdown_to_buffer_cmark()...\n");
    
    // Create a buffer for testing
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    
    // Test basic markdown import
    const char *test_markdown = "**Bold text** and *italic text*\n";
    gboolean result = import_markdown_to_buffer_cmark(buffer, test_markdown);
    
    // Verify import succeeded
    assert(result == TRUE);
    
    // Additional import tests can be added here
    
    printf("Import test passed.\n");
    g_object_unref(buffer);
}

// Test markdown export function
static void test_export_markdown(void) {
    printf("Testing export_buffer_to_markdown_cmark()...\n");
    
    // Create a buffer for testing
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    
    // Create tags needed for formatting
    gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    gtk_text_buffer_create_tag(buffer, "code", "family", "monospace", NULL);
    
    // Test for bold and italic combined
    gtk_text_buffer_set_text(buffer, "This is regular text and this is bold/italic.", -1);
    
    // Apply formatting: Get iterators for the text to format
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_offset(buffer, &start, 27); // "this is bold/italic"
    gtk_text_buffer_get_iter_at_offset(buffer, &end, 46);   // End of "bold/italic"
    
    // Apply both bold and italic tags
    gtk_text_buffer_apply_tag_by_name(buffer, "bold", &start, &end);
    gtk_text_buffer_apply_tag_by_name(buffer, "italic", &start, &end);
    
    // Export the buffer to markdown
    char *exported = export_buffer_to_markdown_cmark(buffer);
    
    // Check specific format: We should have "***bold/italic***" with three stars on each side
    const char *expected = "***bold/italic***";
    assert(strstr(exported, expected) != NULL);
    
    printf("Combined bold/italic format test passed.\n");
    
    // Create a buffer for testing
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    
    // Set up a simple buffer with formatting
    // This would normally be done through cmark import, but we're setting up directly for the test
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    
    // Add some formatted text to the buffer
    GtkTextTag *bold_tag = gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    GtkTextTag *italic_tag = gtk_text_buffer_create_tag(buffer, "italic", "style", PANGO_STYLE_ITALIC, NULL);
    
    gtk_text_buffer_insert(buffer, &start, "Normal text. ", -1);
    
    // Insert bold text
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, "Bold text. ", -1);
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gtk_text_buffer_get_iter_at_offset(buffer, &start, 12); // Position after "Normal text. "
    gtk_text_buffer_get_iter_at_offset(buffer, &end, 23);   // Position after "Bold text. "
    gtk_text_buffer_apply_tag(buffer, bold_tag, &start, &end);
    
    // Export the buffer to markdown
    char *exported_markdown = export_buffer_to_markdown_cmark(buffer);
    
    // Verify the export result contains the expected text
    assert(exported_markdown != NULL);
    assert(strstr(exported_markdown, "Normal text.") != NULL);
    assert(strstr(exported_markdown, "**Bold text.**") != NULL);
    
    // Free allocated memory
    g_free(exported_markdown);
    g_object_unref(buffer);
    
    printf("Export test passed.\n");
}
