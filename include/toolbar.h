#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a toolbar with formatting options for the text editor
 * 
 * @param text_view The GtkTextView that will be affected by toolbar actions
 * @return The toolbar container widget
 */
GtkWidget* create_toolbar(GtkWidget *text_view);

#ifdef __cplusplus
}
#endif

#endif // TOOLBAR_H