#include <glib.h>
#include <gtk/gtk.h>
#include "cmrender.h"

static char* render_export(const char *markdown) {
    GtkTextBuffer *buffer = gtk_text_buffer_new(NULL);
    g_assert_nonnull(buffer);
    // Pass NULL text_view and soup_session. Images aren't involved in these tests.
    gboolean ok = cm_render_markdown_to_buffer(buffer, markdown, NULL, NULL);
    g_assert_true(ok);
    char *out = cm_render_buffer_to_markdown(buffer);
    g_assert_nonnull(out);
    g_object_unref(buffer);
    return out; // caller frees
}

static void test_ordered_start_and_gap(void) {
    const char *input =
        "3. Item three\n"
        "1. Item four\n"
        "1. Item five\n"
        "\n"
        "*Rendered output:*\n"
        "3. Item three\n"
        "4. Item four\n"
        "5. Item five\n";

    const char *expected =
        "3. Item three\n"
        "4. Item four\n"
        "5. Item five\n"
        "\n"
        "*Rendered output:*\n"
        "3. Item three\n"
        "4. Item four\n"
        "5. Item five\n";

    g_autofree char *out = render_export(input);
    g_assert_cmpstr(out, ==, expected);
}

static void test_nested_mixed_lists(void) {
    const char *input =
        "1. First level ordered\n"
        "    - First level unordered\n"
        "        1. Second level ordered\n"
        "        2. Second level ordered, cont\xE2\x80\x99d\n"
        "    - Another nested unordered\n"
        "2. Second item of first level ordered\n"
        "\n"
        "- Top-level unordered\n"
        "    1. Ordered sublist\n"
        "        - Unordered sub-sublist\n"
        "    2. Another ordered sublist\n"
        "- Second top-level unordered\n";

    const char *expected = input;
    g_autofree char *out = render_export(input);
    g_assert_cmpstr(out, ==, expected);
}

static void test_tight_vs_loose(void) {
    const char *input =
        "- Tight item A\n"
        "- Tight item B\n"
        "- Tight item C\n"
        "\n"
        "A *loose* list has blank lines between items:\n"
        "\n"
        "- Loose item A\n"
        "\n"
        "- Loose item B\n"
        "\n"
        "- Loose item C\n";

    const char *expected = input;
    g_autofree char *out = render_export(input);
    g_assert_cmpstr(out, ==, expected);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/lists/ordered_start_and_gap", test_ordered_start_and_gap);
    g_test_add_func("/lists/nested_mixed", test_nested_mixed_lists);
    g_test_add_func("/lists/tight_vs_loose", test_tight_vs_loose);
    return g_test_run();
}
