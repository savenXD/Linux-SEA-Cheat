#ifndef WINDOW_H
#define WINDOW_H

#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <stdbool.h>

#define OVERLAY_WIDTH        360
#define OVERLAY_HEIGHT       640
#define OVERLAY_MARGIN       12
#define PHONE_CORNER_RADIUS  28.0

gboolean window_init(gboolean start_visible);
void window_cleanup(void);

void overlay_show(void);
void overlay_hide(void);
void overlay_toggle(void);
void overlay_reposition(void);
Window overlay_get_xid(void);
gboolean overlay_is_visible(void);

void overlay_get_geometry(int *x, int *y, int *w, int *h);
void overlay_queue_draw(void);
void overlay_set_ghost(gboolean ghost);
GtkWidget *overlay_get_entry_widget(void);
const char *overlay_get_entry_text(void);
void overlay_clear_entry_text(void);
void overlay_set_entry_text(const char *text);

#endif /* WINDOW_H */
