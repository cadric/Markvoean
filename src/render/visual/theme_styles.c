/* C ULTRA-MIN TEMPLATE
   Purpose: Theme-aware styling for markdown rendering
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.3.8] - 2025-09-18 - render/visual/theme_styles.c
   Changed: Enhanced to support theme updates for all markdown elements (code, headings, hr, etc.)
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <adwaita.h>
#include <glib.h>

#include <gtktext/render/theme_styles.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 * HELPERS - Theme color and styling utilities
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Get theme-aware color with alpha transparency
 */
gboolean theme_styles_get_color_with_alpha(GtkWidget *widget, const char *color_name,
                                           gdouble alpha, GdkRGBA *result)
{
    (void)widget; // Unused parameter - keeping for API compatibility

    // Use AdwStyleManager for theme detection
    AdwStyleManager *sm = adw_style_manager_get_default();
    gboolean prefer_dark = FALSE;
    if (sm) {
        AdwColorScheme cs = adw_style_manager_get_color_scheme(sm);
        prefer_dark = (cs == ADW_COLOR_SCHEME_FORCE_DARK || cs == ADW_COLOR_SCHEME_PREFER_DARK);
    }

    // Define theme-aware colors based on common GTK theme color names
    if (g_strcmp0(color_name, "theme_fg_color") == 0 || g_strcmp0(color_name, "foreground") == 0) {
        if (prefer_dark) {
            gdk_rgba_parse(result, "#ffffff");
        } else {
            gdk_rgba_parse(result, "#000000");
        }
        result->alpha = alpha;
        return TRUE;
    } else if (g_strcmp0(color_name, "theme_bg_color") == 0 || g_strcmp0(color_name, "background") == 0) {
        if (prefer_dark) {
            gdk_rgba_parse(result, "#242424");
        } else {
            gdk_rgba_parse(result, "#ffffff");
        }
        result->alpha = alpha;
        return TRUE;
    } else if (g_strcmp0(color_name, "theme_selected_bg_color") == 0 || g_strcmp0(color_name, "accent") == 0) {
        if (prefer_dark) {
            gdk_rgba_parse(result, "#78aeed");
        } else {
            gdk_rgba_parse(result, "#3584e4");
        }
        result->alpha = alpha;
        return TRUE;
    }

    // Use blue fallback colors instead of grey ones
    if (prefer_dark) {
        /* Dark mode: use blue_4 (#1c71d8) equivalent */
        result->red = 28.0/255.0;
        result->green = 113.0/255.0;
        result->blue = 216.0/255.0;
        result->alpha = alpha;
    } else {
        /* Light mode: use blue_1 (#99c1f1) equivalent */
        result->red = 153.0/255.0;
        result->green = 193.0/255.0;
        result->blue = 241.0/255.0;
        result->alpha = alpha;
    }
    return FALSE; /* Indicate fallback was used */
}

/**
 * Update a single tag with theme colors based on its type
 */
static void theme_styles_update_single_tag(GtkTextTag *tag, gpointer user_data)
{
    (void)user_data;

    if (!GTK_IS_TEXT_TAG(tag)) return;

    gchar *tag_name = NULL;
    g_object_get(tag, "name", &tag_name, NULL);
    if (!tag_name) return;

    GdkRGBA color;

    /* Update blockquote tags */
    if (g_str_has_prefix(tag_name, "blockquote")) {
        if (theme_styles_get_color_with_alpha(NULL, "accent", 0.1, &color)) {
            g_object_set(tag, "background-rgba", &color, NULL);
        }
    }
    /* Update code tags (inline code) */
    else if (g_strcmp0(tag_name, "code") == 0) {
        if (theme_styles_get_color_with_alpha(NULL, "theme_bg_color", 0.8, &color)) {
            g_object_set(tag, "background-rgba", &color, NULL);
        }
    }
    /* Update code block tags */
    else if (g_strcmp0(tag_name, "codeblock") == 0 || g_strcmp0(tag_name, "codeblock_indented") == 0) {
        if (theme_styles_get_color_with_alpha(NULL, "theme_bg_color", 0.6, &color)) {
            g_object_set(tag, "background-rgba", &color, NULL);
        }
    }
    /* Update horizontal rule tags */
    else if (g_strcmp0(tag_name, "hr") == 0) {
        if (theme_styles_get_color_with_alpha(NULL, "theme_fg_color", 0.3, &color)) {
            g_object_set(tag, "foreground-rgba", &color, NULL);
        }
    }
    /* Update heading tags with accent colors */
    else if (g_str_has_prefix(tag_name, "h") && g_utf8_strlen(tag_name, -1) == 2 &&
             tag_name[1] >= '1' && tag_name[1] <= '6') {
        if (theme_styles_get_color_with_alpha(NULL, "accent", 1.0, &color)) {
            g_object_set(tag, "foreground-rgba", &color, NULL);
        }
    }

    g_free(tag_name);
}

/**
 * Update a single blockquote tag with theme colors (legacy function for compatibility)
 */
void theme_styles_update_blockquote_tag(GtkTextTag *tag, gpointer user_data)
{
    theme_styles_update_single_tag(tag, user_data);
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Theme update functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Update all theme-dependent tags in buffer
 */
void theme_styles_update_theme_dependent_tags(GtkTextBuffer *buffer)
{
    g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));

    GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
    if (!table) return;

    /* Update all theme-dependent tags (blockquotes, code, headings, hr, etc.) */
    gtk_text_tag_table_foreach(table, theme_styles_update_single_tag, NULL);
}