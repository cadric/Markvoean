#ifndef TAG_UTIL_H
#define TAG_UTIL_H

#include <gtk/gtk.h>

/**
 * @brief Helper function to ensure tag names are stored in GObject data.
 * 
 * This function is useful when creating tags directly with gtk_text_buffer_create_tag
 * instead of using cm_render_get_or_create_base_tag. It stores the tag name in the
 * GObject data for later retrieval with get_tag_name_safe.
 * 
 * @param tag The GtkTextTag that was just created.
 * @param tag_name The name of the tag.
 */
static inline void ensure_tag_name_stored(GtkTextTag *tag, const gchar *name) {
    if (tag && name) {
        if (g_object_get_data(G_OBJECT(tag), "tag-name") == NULL) {
            // Use g_object_set_data_full to ensure g_free is called on the duplicated string
            g_object_set_data_full(G_OBJECT(tag), "tag-name", g_strdup(name), (GDestroyNotify)g_free);
        }
    }
}

#endif // TAG_UTIL_H
