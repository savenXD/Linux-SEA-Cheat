#ifndef OVERLAY_H
#define OVERLAY_H

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdbool.h>

#define OVERLAY_DEFAULT_WIDTH  360
#define OVERLAY_DEFAULT_HEIGHT 640

typedef struct {
    GtkWidget   *window;
    GtkWidget   *status_label;
    Window       overlay_xid;
    Display     *sub_dpy;      /* Dedicated Xlib connection for dominance & XI2 */
    Display     *gtk_dpy;      /* GDK's underlying Xlib connection */
    int          screen;
    Window       root_window;

    /* Monitor and overlay geometry */
    int          monitor_x;
    int          monitor_y;
    int          monitor_w;
    int          monitor_h;

    int          overlay_x;
    int          overlay_y;
    int          overlay_w;
    int          overlay_h;

    gboolean     is_visible;
    guint        heartbeat_timer_id;
    GSource     *x_source;
} OverlayApp;

/* Function prototypes */
void x11_install_error_handlers(void);
gboolean x11_dom_init(OverlayApp *app);
void x11_dom_cleanup(OverlayApp *app);
gboolean x11_dom_detect_primary_monitor(OverlayApp *app);
void x11_dom_raise_overlay(OverlayApp *app);
void x11_dom_attach_glib(OverlayApp *app);

void ui_create_window(OverlayApp *app);
void ui_update_status(OverlayApp *app, const char *status);

#endif /* OVERLAY_H */
