/* C ULTRA-MIN TEMPLATE
   Purpose: Image embedding functionality for GTK text views
   Sections: META • TYPES • PUBLIC API
   [1.0.1] - 2025-09-16 - ui/image_embedder.h
   Changed: Extracted image embedding from main.c for better organization
*/

#ifndef GTKTEXT_UI_IMAGE_EMBEDDER_H
#define GTKTEXT_UI_IMAGE_EMBEDDER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* ═══════════════════════════════════════════════════════════════════════════════
 * PUBLIC API - Image embedding functions
 * ═══════════════════════════════════════════════════════════════════════════════ */

/**
 * Scans text buffer for image tags and embeds clickable image widgets
 * @param text_view Text view containing buffer to scan
 */
void image_embedder_embed_images_in_text_view(GtkTextView *text_view);

G_END_DECLS

#endif /* GTKTEXT_UI_IMAGE_EMBEDDER_H */