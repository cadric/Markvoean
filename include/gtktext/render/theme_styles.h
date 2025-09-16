/* C ULTRA-MIN TEMPLATE
   Purpose: Theme-aware styling for markdown rendering
   Sections: META • TYPES • STATE • HELPERS • HANDLERS • WIRING • LIFECYCLE
   [1.0.1] - 2025-09-16 - render/theme_styles.h
   Changed: Extracted theme styling from cmrender.c for better organization
*/

#ifndef GTKTEXT_RENDER_THEME_STYLES_H
#define GTKTEXT_RENDER_THEME_STYLES_H

#include <gtk/gtk.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Theme-aware styling functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Get theme-aware color with alpha transparency
 * @param widget Widget for theme context
 * @param color_name Color name to look up
 * @param alpha Alpha transparency (0.0-1.0)
 * @param result Output color structure
 * @return TRUE if color found, FALSE if fallback used
 */
gboolean theme_styles_get_color_with_alpha(GtkWidget *widget, const char *color_name,
                                           gdouble alpha, GdkRGBA *result);

/**
 * Update all theme-dependent tags in buffer
 * @param buffer Text buffer containing tags to update
 */
void theme_styles_update_theme_dependent_tags(GtkTextBuffer *buffer);

/**
 * Update a single blockquote tag with theme colors
 * @param tag Tag to update
 * @param user_data Optional user data
 */
void theme_styles_update_blockquote_tag(GtkTextTag *tag, gpointer user_data);

G_END_DECLS

#endif /* GTKTEXT_RENDER_THEME_STYLES_H */