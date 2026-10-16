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
Window overlay_get_xid(void);
gboolean overlay_is_visible(void);

#endif /* WINDOW_H */
