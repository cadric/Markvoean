/* Simple tests for cm_render_buffer_to_markdown export (headless-safe) */

#include <gtk/gtk.h>
#include <glib.h>
#include <gtktext/render/cmrender.h>

static void test_export_plain(void)
{
  GtkTextBuffer *buf = gtk_text_buffer_new(NULL);
  gtk_text_buffer_set_text(buf, "Hello, world!", -1);
  g_autofree char *md = cm_render_buffer_to_markdown(buf);
  g_assert_nonnull(md);
  g_assert_cmpstr(md, ==, "Hello, world!\n");
  g_object_unref(buf);
}

static void test_export_bold_italic(void)
{
  GtkTextBuffer *buf = gtk_text_buffer_new(NULL);
  GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(buf);
  GtkTextTag *bold = gtk_text_tag_new("bold");
  GtkTextTag *italic = gtk_text_tag_new("italic");
  gtk_text_tag_table_add(tt, bold);
  gtk_text_tag_table_add(tt, italic);

  gtk_text_buffer_set_text(buf, "ABCD", -1);
  GtkTextIter a, b;
  gtk_text_buffer_get_bounds(buf, &a, &b);
  /* Apply bold to AB, italic to CD */
  GtkTextIter mid = a;
  gtk_text_iter_forward_chars(&mid, 2);
  gtk_text_buffer_apply_tag(buf, bold, &a, &mid);
  gtk_text_buffer_apply_tag(buf, italic, &mid, &b);

  g_autofree char *md = cm_render_buffer_to_markdown(buf);
  g_assert_nonnull(md);
  /* Exporter emits inline state transitions on the same line: **AB***CD* */
  g_assert_cmpstr(md, ==, "**AB***CD*\n");

  g_object_unref(buf);
}

int main(int argc, char **argv)
{
  /* Avoid gtk_init to keep headless safe; GtkTextBuffer does not need a display */
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/export/plain", test_export_plain);
  g_test_add_func("/export/bold_italic", test_export_bold_italic);
  return g_test_run();
}
