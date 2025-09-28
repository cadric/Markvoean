/* C ULTRA-MIN TEMPLATE
   Purpose: Export GtkTextBuffer content and selections back to Markdown
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <glib.h>
#include <gio/gio.h>

#include <gtktext/render/cmrender.h>
#include <gtktext/render/hr_widget.h>

/* STATE */
static GQuark quark_tag_name = 0;

/* HELPERS */
/* Safely get tag name across GTK4 variants */
static const char *get_tag_name_safe(GtkTextTag *tag)
{
  if (!tag) return NULL;
  if (G_UNLIKELY(quark_tag_name == 0))
    quark_tag_name = g_quark_from_static_string("tag-name");

  const char *name = g_object_get_qdata(G_OBJECT(tag), quark_tag_name);
  if (name && *name) return name;

  gchar *prop_name = NULL;
  g_object_get(G_OBJECT(tag), "name", &prop_name, NULL);
  if (prop_name && *prop_name) {
    g_object_set_qdata_full(G_OBJECT(tag), quark_tag_name, g_strdup(prop_name), g_free);
    const char *stored_name = g_object_get_qdata(G_OBJECT(tag), quark_tag_name);
    g_free(prop_name);
    return stored_name;
  }
  g_free(prop_name);
  g_warning("Tag name not found for tag %p.", (void*)tag);
  return NULL;
}

/* IMPLEMENTATION */

char* cm_render_buffer_to_markdown(GtkTextBuffer *buffer)
{
  g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), g_strdup(""));

  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  if (gtk_text_iter_equal(&start, &end)) {
    g_debug("[export] Empty buffer, returning empty string");
    return g_strdup("");
  }

  gint char_count = gtk_text_buffer_get_char_count(buffer);
  if (char_count > 500000) {
    g_warning("[export] Large buffer (%d chars) may impact performance", char_count);
  }

  GString *md = g_string_new("");

  g_autoptr(GSettings) settings = g_settings_new("org.gtk.gtktext");
  g_autofree gchar *heading_format = g_settings_get_string(settings, "heading-format");
  gboolean use_setext = g_strcmp0(heading_format, "setext") == 0;

  GtkTextIter iter;
  gtk_text_buffer_get_start_iter(buffer, &iter);

  gboolean currently_in_bold = FALSE;
  gboolean currently_in_italic = FALSE;
  gboolean currently_in_code = FALSE;
  gboolean currently_in_codeblock = FALSE;
  gboolean currently_in_link = FALSE;
  const char *current_link_url = NULL;
  const char *current_link_title = NULL;
  GString *current_link_text = NULL;
  gboolean currently_in_image = FALSE;
  const char *current_image_url = NULL;
  const char *current_image_title = NULL;
  GString *current_image_alt = NULL;
  gboolean at_line_start = TRUE;

  gboolean currently_in_heading = FALSE;
  int current_heading_level = 0;
  int previous_heading_level = 0;
  GString *current_heading_text = NULL;

  GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

  while(!gtk_text_iter_is_end(&iter)) {
    gunichar current_char = gtk_text_iter_get_char(&iter);

    /* Child anchors (images, hr) */
    GtkTextChildAnchor *child_anchor = gtk_text_iter_get_child_anchor(&iter);
    if (child_anchor) {
      if (g_object_get_data(G_OBJECT(child_anchor), "hr-widget")) {
        g_string_append(md, "---\n");
        gtk_text_iter_forward_char(&iter);
        continue;
      }
      guint widget_count = 0;
      GtkWidget **widgets = gtk_text_child_anchor_get_widgets(child_anchor, &widget_count);
      for (guint i = 0; i < widget_count; i++) {
        GtkWidget *widget = widgets[i];
        if (GTKTEXT_IS_HR_WIDGET(widget)) {
          g_string_append(md, "---\n");
          break;
        }
        const char *image_url = g_object_get_data(G_OBJECT(widget), "image-url");
        const char *image_alt = g_object_get_data(G_OBJECT(widget), "image-alt");
        if (image_url) {
          const char *link_url = g_object_get_data(G_OBJECT(widget), "open-url");
          const char *link_title = NULL;
          if (!link_url || !*link_url || g_strcmp0(link_url, image_url) == 0) {
            GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
            for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
              GtkTextTag *tag = GTK_TEXT_TAG(l->data);
              const char *tag_name = get_tag_name_safe(tag);
              if (tag_name && (g_str_has_prefix(tag_name, "link_") || g_str_has_prefix(tag_name, "link-"))) {
                link_url = g_object_get_data(G_OBJECT(tag), "link-url");
                link_title = g_object_get_data(G_OBJECT(tag), "link-title");
                break;
              }
            }
            g_slist_free(tags_at_iter);
            if (link_url && g_strcmp0(link_url, image_url) == 0) link_url = NULL;
          }
          if (link_url) {
            g_string_append_printf(md, "[![%s](%s)](%s)", image_alt ? image_alt : "", image_url, link_url);
            if (link_title) {
              /* optional title ignored for image-in-link */
            }
          } else {
            g_string_append_printf(md, "![%s](%s)", image_alt ? image_alt : "", image_url);
          }
        }
      }
      g_free(widgets);
      gtk_text_iter_forward_char(&iter);
      continue;
    }

    gboolean iter_is_bold = FALSE;
    gboolean iter_is_italic = FALSE;
    gboolean iter_is_code = FALSE;
    gboolean iter_is_codeblock_char = FALSE;
    gboolean iter_is_link = FALSE;
    const char *link_url = NULL;
    const char *link_title = NULL;
    gboolean iter_is_image = FALSE;
    const char *image_url = NULL;
    const char *image_title = NULL;
    const char *iter_code_info = NULL;
    gboolean iter_is_h1 = FALSE, iter_is_h2 = FALSE, iter_is_h3 = FALSE, iter_is_h4 = FALSE, iter_is_h5 = FALSE, iter_is_h6 = FALSE;
    int iter_blockquote_depth = 0;

    GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
    for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
      GtkTextTag *tag = GTK_TEXT_TAG(l->data);
      const char *tag_name = get_tag_name_safe(tag);
      if (tag_name) {
        if (current_char != '\n' && current_char != ' ') {
          g_debug("cm_export: tag '%s' at char '%c'", tag_name, (char)current_char);
        }
        if (g_strcmp0(tag_name, "bold") == 0) iter_is_bold = TRUE;
        else if (g_strcmp0(tag_name, "italic") == 0) iter_is_italic = TRUE;
        else if (g_strcmp0(tag_name, "code") == 0) {
          iter_is_code = TRUE;
          g_debug("cm_export: code tag at '%c' (in_code=%s)", (char)current_char, currently_in_code ? "TRUE" : "FALSE");
        }
        else if (g_strcmp0(tag_name, "codeblock") == 0 || g_strcmp0(tag_name, "codeblock_indented") == 0) iter_is_codeblock_char = TRUE;
        else if (g_str_has_prefix(tag_name, "link_")) {
          iter_is_link = TRUE;
          link_url = g_object_get_data(G_OBJECT(tag), "link-url");
          link_title = g_object_get_data(G_OBJECT(tag), "link-title");
          g_debug("cm_export: link tag '%s', URL=%s", tag_name, link_url ? link_url : "NULL");
        }
        else if (g_str_has_prefix(tag_name, "image_")) {
          if (g_strcmp0(tag_name, "image_alt_hidden") != 0) {
            const char *u = g_object_get_data(G_OBJECT(tag), "image-url");
            if (u && *u) { iter_is_image = TRUE; image_url = u; image_title = g_object_get_data(G_OBJECT(tag), "image-title"); }
          }
        }
        else if (g_str_has_prefix(tag_name, "codeblock_meta_")) {
          const char *info = g_object_get_data(G_OBJECT(tag), "code-info");
          if (info && !iter_code_info) iter_code_info = info;
        }
        else if (g_strcmp0(tag_name, "h1") == 0) iter_is_h1 = TRUE;
        else if (g_strcmp0(tag_name, "h2") == 0) iter_is_h2 = TRUE;
        else if (g_strcmp0(tag_name, "h3") == 0) iter_is_h3 = TRUE;
        else if (g_strcmp0(tag_name, "h4") == 0) iter_is_h4 = TRUE;
        else if (g_strcmp0(tag_name, "h5") == 0) iter_is_h5 = TRUE;
        else if (g_strcmp0(tag_name, "h6") == 0) iter_is_h6 = TRUE;
        else if (g_str_has_prefix(tag_name, "blockquote")) iter_blockquote_depth++;
      }
    }
    g_slist_free(tags_at_iter);

    if (at_line_start) {
      if (!currently_in_codeblock && iter_blockquote_depth > 0) {
        for (int d = 0; d < iter_blockquote_depth; d++) g_string_append_c(md, '>');
        g_string_append_c(md, ' ');
      }
      GtkTextTag *hr_tag = gtk_text_tag_table_lookup(tag_table, "hr");
      if (hr_tag && gtk_text_iter_has_tag(&iter, hr_tag) && !currently_in_codeblock) {
        g_string_append(md, "---\n");
        GtkTextIter line_end_iter = iter;
        gtk_text_iter_forward_to_line_end(&line_end_iter);
        iter = line_end_iter;
        if (!gtk_text_iter_is_end(&iter)) {
          gtk_text_iter_forward_char(&iter);
          if (!gtk_text_iter_is_end(&iter) && gtk_text_iter_get_char(&iter) == '\n') {
            gtk_text_iter_forward_char(&iter);
          }
        }
        at_line_start = TRUE;
        if (gtk_text_iter_is_end(&iter)) break;
        continue;
      }

      if (!currently_in_codeblock) {
        GtkTextIter tmp = iter;
        while (!gtk_text_iter_is_end(&tmp) && gtk_text_iter_get_char(&tmp) == ' ') {
          g_string_append_c(md, ' ');
          gtk_text_iter_forward_char(&tmp);
        }
        gunichar bullet = gtk_text_iter_get_char(&tmp);
        if (bullet == 0x25CF || bullet == 0x25CB || bullet == 0x25A0) {
          g_string_append(md, "- ");
          gtk_text_iter_forward_char(&tmp);
          if (!gtk_text_iter_is_end(&tmp)) gtk_text_iter_forward_char(&tmp);
          iter = tmp;
          at_line_start = FALSE;
          continue;
        }
        iter = tmp;
        current_char = gtk_text_iter_get_char(&iter);
      }

      if (!currently_in_codeblock) {
        previous_heading_level = current_heading_level;
        if (iter_is_h1) current_heading_level = 1;
        else if (iter_is_h2) current_heading_level = 2;
        else if (iter_is_h3) current_heading_level = 3;
        else if (iter_is_h4) current_heading_level = 4;
        else if (iter_is_h5) current_heading_level = 5;
        else if (iter_is_h6) current_heading_level = 6;
        else current_heading_level = 0;

        if (current_heading_level != previous_heading_level) {
          if (use_setext && previous_heading_level >= 1 && previous_heading_level <= 2 &&
              current_heading_text && current_heading_text->len > 0) {
            g_string_append(md, current_heading_text->str);
            g_string_append_c(md, '\n');
            if (previous_heading_level == 1) {
              for (guint i = 0; i < current_heading_text->len; i++) g_string_append_c(md, '=');
            } else {
              for (guint i = 0; i < current_heading_text->len; i++) g_string_append_c(md, '-');
            }
            g_string_append_c(md, '\n');
          }

          if (current_heading_level >= 1 && current_heading_level <= 6) {
            currently_in_heading = TRUE;
            if (current_heading_text) { g_string_free(current_heading_text, TRUE); current_heading_text = NULL; }
            if (use_setext && current_heading_level <= 2) {
              current_heading_text = g_string_new("");
            } else {
              for (int i = 0; i < current_heading_level; i++) g_string_append_c(md, '#');
              g_string_append_c(md, ' ');
            }
          } else {
            currently_in_heading = FALSE;
            if (current_heading_text) { g_string_free(current_heading_text, TRUE); current_heading_text = NULL; }
          }
        }

        if (current_heading_level == 0 || current_heading_level >= 3) {
          GtkTextTag *codeblock_tag = gtk_text_tag_table_lookup(tag_table, "codeblock");
          GtkTextTag *codeblock_indented_tag = gtk_text_tag_table_lookup(tag_table, "codeblock_indented");
          gboolean is_fenced_block = codeblock_tag && gtk_text_iter_has_tag(&iter, codeblock_tag);
          gboolean is_indented_block = codeblock_indented_tag && gtk_text_iter_has_tag(&iter, codeblock_indented_tag);
          if (is_fenced_block) {
            if (iter_code_info && *iter_code_info) g_string_append_printf(md, "```%s\n", iter_code_info);
            else g_string_append(md, "```\n");
            currently_in_codeblock = TRUE;
          } else if (is_indented_block) {
            currently_in_codeblock = TRUE;
          }
        }
      }
    }

    if (currently_in_codeblock && !iter_is_codeblock_char && current_char != '\n') {
      if (md->len > 0 && md->str[md->len -1] != '\n') g_string_append_c(md, '\n');
      g_string_append(md, "```\n");
      currently_in_codeblock = FALSE;
    }

    if (currently_in_codeblock) {
      g_string_append_unichar(md, current_char);
    } else {
      if (current_char == 0xFFFC || current_char == 0x200B) goto advance_only;

      if (iter_is_bold && iter_is_italic && !currently_in_bold && !currently_in_italic) {
        g_string_append(md, "***");
        currently_in_bold = TRUE; currently_in_italic = TRUE;
      } else if (!iter_is_bold && !iter_is_italic && currently_in_bold && currently_in_italic) {
        g_string_append(md, "***");
        currently_in_bold = FALSE; currently_in_italic = FALSE;
      } else {
        if (iter_is_bold && !currently_in_bold) { g_string_append(md, "**"); currently_in_bold = TRUE; }
        else if (!iter_is_bold && currently_in_bold) { g_string_append(md, "**"); currently_in_bold = FALSE; }
        if (iter_is_italic && !currently_in_italic) { g_string_append(md, "*"); currently_in_italic = TRUE; }
        else if (!iter_is_italic && currently_in_italic) { g_string_append(md, "*"); currently_in_italic = FALSE; }
      }

      if (currently_in_code && !iter_is_code) { g_string_append_c(md, '`'); currently_in_code = FALSE; }
      if (iter_is_code && !currently_in_code) { g_string_append_c(md, '`'); currently_in_code = TRUE; }

      gboolean entering_image_in_link = (iter_is_image && iter_is_link && !currently_in_image && !currently_in_link);
      gboolean leaving_image_in_link = (currently_in_image && currently_in_link && (!iter_is_image || !iter_is_link));

      if (leaving_image_in_link) {
        if (current_image_url) {
          if (current_image_title && *current_image_title) g_string_append_printf(md, "](%s \"%s\")", current_image_url, current_image_title);
          else g_string_append_printf(md, "](%s)", current_image_url);
        } else {
          g_string_append(md, "]()");
        }
        if (current_link_url) {
          if (current_link_title && *current_link_title) g_string_append_printf(md, "](%s \"%s\")", current_link_url, current_link_title);
          else g_string_append_printf(md, "](%s)", current_link_url);
        } else {
          g_string_append(md, "]()");
        }
        if (current_image_alt) { g_string_free(current_image_alt, TRUE); current_image_alt = NULL; }
        if (current_link_text) { g_string_free(current_link_text, TRUE); current_link_text = NULL; }
        currently_in_image = FALSE; currently_in_link = FALSE;
        current_image_url = NULL; current_image_title = NULL; current_link_url = NULL; current_link_title = NULL;
      }
      else if (entering_image_in_link) {
        g_string_append(md, "[![");
        currently_in_link = TRUE; currently_in_image = TRUE;
        current_link_url = link_url; current_link_title = link_title;
        current_image_url = image_url; current_image_title = image_title;
        if (current_link_text) { g_string_free(current_link_text, TRUE); }
        current_link_text = g_string_new("");
        if (current_image_alt) { g_string_free(current_image_alt, TRUE); }
        current_image_alt = g_string_new("");
      }
      else {
        if (currently_in_image && !iter_is_image) {
          if (current_image_url) {
            if (current_image_title && *current_image_title) g_string_append_printf(md, "](%s \"%s\")", current_image_url, current_image_title);
            else g_string_append_printf(md, "](%s)", current_image_url);
          } else {
            g_string_append(md, "]()");
          }
          if (current_image_alt) { g_string_free(current_image_alt, TRUE); current_image_alt = NULL; }
          currently_in_image = FALSE;
        }
        if (iter_is_image && !currently_in_image) {
          g_string_append(md, "![");
          currently_in_image = TRUE; current_image_url = image_url; current_image_title = image_title;
          if (current_image_alt) { g_string_free(current_image_alt, TRUE); }
          current_image_alt = g_string_new("");
        }
        if (currently_in_link && !iter_is_link) {
          if (current_link_url) {
            if (current_link_title && *current_link_title) g_string_append_printf(md, "](%s \"%s\")", current_link_url, current_link_title);
            else g_string_append_printf(md, "](%s)", current_link_url);
          } else {
            g_string_append(md, "]()");
          }
          if (current_link_text) { g_string_free(current_link_text, TRUE); current_link_text = NULL; }
          currently_in_link = FALSE;
        }
        if (iter_is_link && !currently_in_link) {
          g_string_append(md, "[");
          currently_in_link = TRUE; current_link_url = link_url; current_link_title = link_title;
          if (current_link_text) { g_string_free(current_link_text, TRUE); }
          current_link_text = g_string_new("");
        }
      }

      if (currently_in_link && current_char != '\n') {
        if (!current_link_text) current_link_text = g_string_new("");
        g_string_append_unichar(current_link_text, current_char);
      } else if (currently_in_image && current_char != '\n') {
        if (!current_image_alt) current_image_alt = g_string_new("");
        g_string_append_unichar(current_image_alt, current_char);
      } else {
        g_string_append_unichar(md, current_char);
      }
    }

advance_only:
    if (current_char == '\n') {
      at_line_start = TRUE;
      if (currently_in_heading && current_heading_level <= 2 && use_setext) {
        at_line_start = TRUE;
      }
    } else {
      at_line_start = FALSE;
    }
    gtk_text_iter_forward_char(&iter);
  }

  if (currently_in_link) {
    if (current_link_url) {
      if (current_link_title && *current_link_title) g_string_append_printf(md, "](%s \"%s\")", current_link_url, current_link_title);
      else g_string_append_printf(md, "](%s)", current_link_url);
    } else {
      g_string_append(md, "]()");
    }
    if (current_link_text) { g_string_free(current_link_text, TRUE); current_link_text = NULL; }
  }
  if (currently_in_image) {
    if (current_image_url) {
      if (current_image_title && *current_image_title) g_string_append_printf(md, "](%s \"%s\")", current_image_url, current_image_title);
      else g_string_append_printf(md, "](%s)", current_image_url);
    } else {
      g_string_append(md, "]()");
    }
    if (current_image_alt) { g_string_free(current_image_alt, TRUE); current_image_alt = NULL; }
  }
  if (currently_in_code) g_string_append_c(md, '`');
  if (currently_in_codeblock) {
    if (md->len > 0 && md->str[md->len -1] != '\n') g_string_append_c(md, '\n');
    g_string_append(md, "```\n");
  }

  if (use_setext && current_heading_level >= 1 && current_heading_level <= 2 &&
      current_heading_text && current_heading_text->len > 0) {
    g_string_append(md, current_heading_text->str);
    g_string_append_c(md, '\n');
    if (current_heading_level == 1) { for (guint i = 0; i < current_heading_text->len; i++) g_string_append_c(md, '='); }
    else { for (guint i = 0; i < current_heading_text->len; i++) g_string_append_c(md, '-'); }
    g_string_append_c(md, '\n');
  }
  if (current_heading_text) { g_string_free(current_heading_text, TRUE); current_heading_text = NULL; }

  if (currently_in_bold && currently_in_italic) g_string_append(md, "***");
  else if (currently_in_bold) g_string_append(md, "**");
  else if (currently_in_italic) g_string_append(md, "*");

  if (md->len > 0 && md->str[md->len-1] != '\n') g_string_append_c(md, '\n');
  return g_string_free(md, FALSE);
}

char* cm_render_selection_to_markdown(GtkTextBuffer *buffer, const GtkTextIter *start_iter, const GtkTextIter *end_iter)
{
  g_return_val_if_fail(GTK_IS_TEXT_BUFFER(buffer), g_strdup(""));
  g_return_val_if_fail(start_iter != NULL, g_strdup(""));
  g_return_val_if_fail(end_iter != NULL, g_strdup(""));

  if (gtk_text_iter_equal(start_iter, end_iter)) {
    g_debug("[export] Empty selection, returning empty string");
    return g_strdup("");
  }

  GtkTextIter start_copy = *start_iter;
  GtkTextIter end_copy = *end_iter;
  gtk_text_iter_order(&start_copy, &end_copy);

  g_autoptr(GSettings) settings = g_settings_new("org.gtk.gtktext");
  g_autofree gchar *heading_format = g_settings_get_string(settings, "heading-format");
  gboolean use_setext = g_strcmp0(heading_format, "setext") == 0;

  GString *md = g_string_new("");
  GtkTextIter iter = start_copy;

  gboolean currently_in_bold = FALSE;
  gboolean currently_in_italic = FALSE;
  gboolean currently_in_code = FALSE;
  gboolean currently_in_codeblock = FALSE;
  gboolean currently_in_link = FALSE;
  const char *current_link_url = NULL;
  const char *current_link_title = NULL;
  GString *current_link_text = NULL;
  gboolean at_line_start = TRUE;

  int current_heading_level = 0;
  GString *current_heading_text = NULL;

  GtkTextTagTable *tag_table = gtk_text_buffer_get_tag_table(buffer);

  while (!gtk_text_iter_equal(&iter, &end_copy)) {
    gunichar current_char = gtk_text_iter_get_char(&iter);

    GtkTextChildAnchor *child_anchor = gtk_text_iter_get_child_anchor(&iter);
    if (child_anchor) {
      if (g_object_get_data(G_OBJECT(child_anchor), "hr-widget")) {
        g_string_append(md, "---\n");
        gtk_text_iter_forward_char(&iter);
        continue;
      }
      guint widget_count = 0; GtkWidget **widgets = gtk_text_child_anchor_get_widgets(child_anchor, &widget_count);
      for (guint i = 0; i < widget_count; i++) {
        GtkWidget *widget = widgets[i];
        if (GTKTEXT_IS_HR_WIDGET(widget)) { g_string_append(md, "---\n"); break; }
      }
      g_free(widgets);
      gtk_text_iter_forward_char(&iter);
      continue;
    }

    gboolean iter_is_bold = FALSE;
    gboolean iter_is_italic = FALSE;
    gboolean iter_is_code = FALSE;
    gboolean iter_is_codeblock_char = FALSE;
    /* link tags ignored for selection export */
    const char *iter_code_info = NULL;

    GSList *tags_at_iter = gtk_text_iter_get_tags(&iter);
    for (GSList *l = tags_at_iter; l != NULL; l = l->next) {
      GtkTextTag *tag = GTK_TEXT_TAG(l->data);
      const char *tag_name = get_tag_name_safe(tag);
      if (tag_name) {
        if (g_strcmp0(tag_name, "bold") == 0) iter_is_bold = TRUE;
        else if (g_strcmp0(tag_name, "italic") == 0) iter_is_italic = TRUE;
        else if (g_strcmp0(tag_name, "code") == 0) iter_is_code = TRUE;
        else if (g_strcmp0(tag_name, "codeblock") == 0 || g_strcmp0(tag_name, "codeblock_indented") == 0) iter_is_codeblock_char = TRUE;
        /* ignore link_ tags for selection export */
        else if (g_str_has_prefix(tag_name, "codeblock_meta_")) { const char *info = g_object_get_data(G_OBJECT(tag), "code-info"); if (info && !iter_code_info) iter_code_info = info; }
        else if (g_strcmp0(tag_name, "h1") == 0) current_heading_level = 1;
        else if (g_strcmp0(tag_name, "h2") == 0) current_heading_level = 2;
      }
    }
    g_slist_free(tags_at_iter);

    if (at_line_start) {
      GtkTextTag *hr_tag = gtk_text_tag_table_lookup(tag_table, "hr");
      if (hr_tag && gtk_text_iter_has_tag(&iter, hr_tag) && !currently_in_codeblock) {
        g_string_append(md, "---\n");
        GtkTextIter line_end_iter = iter; gtk_text_iter_forward_to_line_end(&line_end_iter); iter = line_end_iter;
        if (!gtk_text_iter_is_end(&iter)) { gtk_text_iter_forward_char(&iter); if (!gtk_text_iter_is_end(&iter) && gtk_text_iter_get_char(&iter) == '\n') gtk_text_iter_forward_char(&iter); }
        at_line_start = TRUE; if (gtk_text_iter_is_end(&iter)) break; continue;
      }

      /* heading state for selection export not used */
    }

    if (currently_in_codeblock && !iter_is_codeblock_char && current_char != '\n') {
      if (md->len > 0 && md->str[md->len -1] != '\n') g_string_append_c(md, '\n');
      g_string_append(md, "```\n"); currently_in_codeblock = FALSE;
    }

    if (currently_in_codeblock) {
      g_string_append_unichar(md, current_char);
    } else {
      if (current_char == 0xFFFC || current_char == 0x200B) goto advance_only2;

      if (iter_is_bold && !currently_in_bold) { g_string_append(md, "**"); currently_in_bold = TRUE; }
      else if (!iter_is_bold && currently_in_bold) { g_string_append(md, "**"); currently_in_bold = FALSE; }
      if (iter_is_italic && !currently_in_italic) { g_string_append(md, "*"); currently_in_italic = TRUE; }
      else if (!iter_is_italic && currently_in_italic) { g_string_append(md, "*"); currently_in_italic = FALSE; }

      if (currently_in_code && !iter_is_code) { g_string_append_c(md, '`'); currently_in_code = FALSE; }
      if (iter_is_code && !currently_in_code) { g_string_append_c(md, '`'); currently_in_code = TRUE; }

      g_string_append_unichar(md, current_char);
    }

advance_only2:
    if (current_char == '\n') at_line_start = TRUE; else at_line_start = FALSE;
    gtk_text_iter_forward_char(&iter);
  }

  if (currently_in_link) {
    if (current_link_url) {
      if (current_link_title && *current_link_title) g_string_append_printf(md, "](%s \"%s\")", current_link_url, current_link_title);
      else g_string_append_printf(md, "](%s)", current_link_url);
    }
    if (current_link_text) { g_string_free(current_link_text, TRUE); current_link_text = NULL; }
  }
  if (currently_in_code) g_string_append_c(md, '`');
  if (currently_in_codeblock) { if (md->len > 0 && md->str[md->len -1] != '\n') g_string_append_c(md, '\n'); g_string_append(md, "```\n"); }

  if (use_setext && current_heading_level >= 1 && current_heading_level <= 2 && current_heading_text && current_heading_text->len > 0) {
    g_string_append(md, current_heading_text->str);
    g_string_append_c(md, '\n');
    if (current_heading_level == 1) { for (guint i = 0; i < current_heading_text->len; i++) g_string_append_c(md, '='); }
    else { for (guint i = 0; i < current_heading_text->len; i++) g_string_append_c(md, '-'); }
    g_string_append_c(md, '\n');
  }
  if (current_heading_text) { g_string_free(current_heading_text, TRUE); current_heading_text = NULL; }

  if (currently_in_bold && currently_in_italic) g_string_append(md, "***");
  else if (currently_in_bold) g_string_append(md, "**");
  else if (currently_in_italic) g_string_append(md, "*");

  if (md->len > 0 && md->str[md->len-1] != '\n') g_string_append_c(md, '\n');
  return g_string_free(md, FALSE);
}
