/* C ULTRA-MIN TEMPLATE
   Purpose: Theme-aware styling for markdown rendering
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - render/visual/theme_styles.c
   Changed: Extracted theme styling from cmrender.c for better organization
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
 * Update a single blockquote tag with theme colors
 */
void theme_styles_update_blockquote_tag(GtkTextTag *tag, gpointer user_data)
{
    (void)user_data;

    if (!GTK_IS_TEXT_TAG(tag)) return;

    gchar *tag_name = NULL;
    g_object_get(tag, "name", &tag_name, NULL);
    if (!tag_name || !g_str_has_prefix(tag_name, "blockquote")) {
        g_free(tag_name);
        return;
    }
    g_free(tag_name);

    /* Set theme-aware background color for blockquotes */
    GdkRGBA bg_color;
    if (theme_styles_get_color_with_alpha(NULL, "theme_bg_color", 0.1, &bg_color)) {
        g_object_set(tag, "background-rgba", &bg_color, NULL);
    }

    /* Set theme-aware left margin */
    g_object_set(tag, "left-margin", 20, NULL);
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

    /* Update all blockquote tags */
    gtk_text_tag_table_foreach(table, theme_styles_update_blockquote_tag, NULL);

    /* TODO: Add other theme-dependent tag updates here as needed */
}