#include "overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/Xrandr.h>

/* Custom X error handler that logs all errors clearly without crashing */
static int custom_x_error_handler(Display *dpy, XErrorEvent *err) {
    char err_msg[256];
    XGetErrorText(dpy, err->error_code, err_msg, sizeof(err_msg));

    fprintf(stderr, "[X11 ERROR] Code: %d (%s), Request: %d, Minor: %d, ResourceID: 0x%lx\n",
            err->error_code, err_msg, err->request_code, err->minor_code, (unsigned long)err->resourceid);

    /* Explicitly log warnings for critical errors */
    if (err->error_code == BadAccess) {
        fprintf(stderr, "[X11 WARNING] BadAccess encountered! A resource or grab was refused.\n");
    } else if (err->error_code == BadWindow) {
        fprintf(stderr, "[X11 WARNING] BadWindow encountered! Window 0x%lx may have closed.\n",
                (unsigned long)err->resourceid);
    }

    return 0; /* Keep process running */
}

/* Custom X I/O error handler for fatal connection loss */
static int custom_x_io_error_handler(Display *dpy) {
    (void)dpy;
    fprintf(stderr, "[X11 FATAL] Connection to X server lost. Emergency exit.\n");
    exit(1);
}

void x11_install_error_handlers(void) {
    XSetErrorHandler(custom_x_error_handler);
    XSetIOErrorHandler(custom_x_io_error_handler);
}

/* Detect primary monitor bounds using XRandR, with fallback to screen bounds */
gboolean x11_dom_detect_primary_monitor(OverlayApp *app) {
    if (!app->sub_dpy) return FALSE;

    RROutput primary = XRRGetOutputPrimary(app->sub_dpy, app->root_window);
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(app->sub_dpy, app->root_window);
    if (!res) {
        fprintf(stderr, "[XRandR] Unable to retrieve screen resources. Using default screen bounds.\n");
        app->monitor_x = 0;
        app->monitor_y = 0;
        app->monitor_w = DisplayWidth(app->sub_dpy, app->screen);
        app->monitor_h = DisplayHeight(app->sub_dpy, app->screen);
        return TRUE;
    }

    gboolean found = FALSE;
    if (primary != None) {
        XRROutputInfo *out_info = XRRGetOutputInfo(app->sub_dpy, res, primary);
        if (out_info) {
            if (out_info->crtc != None) {
                XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(app->sub_dpy, res, out_info->crtc);
                if (crtc_info) {
                    app->monitor_x = crtc_info->x;
                    app->monitor_y = crtc_info->y;
                    app->monitor_w = (int)crtc_info->width;
                    app->monitor_h = (int)crtc_info->height;
                    found = TRUE;
                    printf("[XRandR] Primary Monitor '%s' geometry: %dx%d+%d+%d\n",
                           out_info->name ? out_info->name : "Unknown",
                           app->monitor_w, app->monitor_h, app->monitor_x, app->monitor_y);
                    XRRFreeCrtcInfo(crtc_info);
                }
            }
            XRRFreeOutputInfo(out_info);
        }
    }

    /* Fallback if no primary output is assigned or has no CRTC */
    if (!found) {
        printf("[XRandR] No primary monitor output configured. Scanning connected outputs...\n");
        for (int i = 0; i < res->noutput; i++) {
            XRROutputInfo *out_info = XRRGetOutputInfo(app->sub_dpy, res, res->outputs[i]);
            if (out_info) {
                if (out_info->connection == RR_Connected && out_info->crtc != None) {
                    XRRCrtcInfo *crtc_info = XRRGetCrtcInfo(app->sub_dpy, res, out_info->crtc);
                    if (crtc_info) {
                        app->monitor_x = crtc_info->x;
                        app->monitor_y = crtc_info->y;
                        app->monitor_w = (int)crtc_info->width;
                        app->monitor_h = (int)crtc_info->height;
                        found = TRUE;
                        printf("[XRandR] Found connected monitor '%s' geometry: %dx%d+%d+%d\n",
                               out_info->name ? out_info->name : "Unknown",
                               app->monitor_w, app->monitor_h, app->monitor_x, app->monitor_y);
                        XRRFreeCrtcInfo(crtc_info);
                        XRRFreeOutputInfo(out_info);
                        break;
                    }
                }
                XRRFreeOutputInfo(out_info);
            }
        }
    }

    if (!found) {
        printf("[XRandR] Fallback to total screen dimensions.\n");
        app->monitor_x = 0;
        app->monitor_y = 0;
        app->monitor_w = DisplayWidth(app->sub_dpy, app->screen);
        app->monitor_h = DisplayHeight(app->sub_dpy, app->screen);
    }

    XRRFreeScreenResources(res);

    /* Calculate bottom-left coordinates for overlay */
    app->overlay_w = OVERLAY_DEFAULT_WIDTH;
    app->overlay_h = OVERLAY_DEFAULT_HEIGHT;
    app->overlay_x = app->monitor_x;
    app->overlay_y = app->monitor_y + app->monitor_h - app->overlay_h;

    printf("[Overlay Layout] Bottom-Left coordinates: x=%d, y=%d (size %dx%d)\n",
           app->overlay_x, app->overlay_y, app->overlay_w, app->overlay_h);

    return TRUE;
}

/* Dedicated GSource for integrating the secondary Xlib Display into GLib */
typedef struct {
    GSource     source;
    OverlayApp *app;
    GPollFD     poll_fd;
} XDisplaySource;

static gboolean x_display_source_prepare(GSource *source, gint *timeout) {
    XDisplaySource *xsrc = (XDisplaySource *)source;
    *timeout = -1;
    return (XPending(xsrc->app->sub_dpy) > 0);
}

static gboolean x_display_source_check(GSource *source) {
    XDisplaySource *xsrc = (XDisplaySource *)source;
    if (xsrc->poll_fd.revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) {
        return TRUE;
    }
    return (XPending(xsrc->app->sub_dpy) > 0);
}

static gboolean x_display_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
    (void)callback;
    (void)user_data;
    XDisplaySource *xsrc = (XDisplaySource *)source;
    OverlayApp *app = xsrc->app;

    while (XPending(app->sub_dpy) > 0) {
        XEvent ev;
        XNextEvent(app->sub_dpy, &ev);

        if (!app->is_visible || app->overlay_xid == None) {
            continue;
        }

        switch (ev.type) {
            case ConfigureNotify:
                if (ev.xconfigure.window != app->overlay_xid) {
                    /* Another window was configured, resized, moved, or raised */
                    x11_dom_raise_overlay(app);
                }
                break;

            case MapNotify:
                if (ev.xmap.window != app->overlay_xid) {
                    /* Another window was mapped */
                    x11_dom_raise_overlay(app);
                }
                break;

            case CirculateNotify:
                if (ev.xcirculate.window != app->overlay_xid) {
                    x11_dom_raise_overlay(app);
                }
                break;

            default:
                break;
        }
    }

    return G_SOURCE_CONTINUE;
}

static GSourceFuncs x_source_funcs = {
    x_display_source_prepare,
    x_display_source_check,
    x_display_source_dispatch,
    NULL, NULL, NULL
};

/* Raise overlay to the top of the X server stacking order */
void x11_dom_raise_overlay(OverlayApp *app) {
    if (!app->sub_dpy || app->overlay_xid == None || !app->is_visible) {
        return;
    }
    XRaiseWindow(app->sub_dpy, app->overlay_xid);
    XFlush(app->sub_dpy);
}

/* Heartbeat timer (every 500ms) to ensure overlay stays atop aggressive restackers */
static gboolean dominance_heartbeat_cb(gpointer user_data) {
    OverlayApp *app = (OverlayApp *)user_data;
    if (!app->is_visible || app->overlay_xid == None || !app->sub_dpy) {
        return G_SOURCE_CONTINUE;
    }

    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (XQueryTree(app->sub_dpy, app->root_window, &root_ret, &parent_ret, &children, &nchildren)) {
        if (nchildren > 0 && children[nchildren - 1] != app->overlay_xid) {
            /* Overlay is not the topmost window; re-assert dominance */
            XRaiseWindow(app->sub_dpy, app->overlay_xid);
            XFlush(app->sub_dpy);
        }
        if (children) {
            XFree(children);
        }
    }

    return G_SOURCE_CONTINUE;
}

/* Attach the dedicated Xlib connection to the GLib main loop */
void x11_dom_attach_glib(OverlayApp *app) {
    GSource *source = g_source_new(&x_source_funcs, sizeof(XDisplaySource));
    XDisplaySource *xsrc = (XDisplaySource *)source;
    xsrc->app = app;
    xsrc->poll_fd.fd = ConnectionNumber(app->sub_dpy);
    xsrc->poll_fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    xsrc->poll_fd.revents = 0;

    g_source_add_poll(source, &xsrc->poll_fd);
    g_source_attach(source, NULL);
    app->x_source = source;

    /* Start dominance heartbeat */
    app->heartbeat_timer_id = g_timeout_add(500, dominance_heartbeat_cb, app);
    printf("[Dominance] GLib X11 event source and 500ms heartbeat attached.\n");
}

/* Initialize dominance connection and select root events */
gboolean x11_dom_init(OverlayApp *app) {
    app->sub_dpy = XOpenDisplay(NULL);
    if (!app->sub_dpy) {
        fprintf(stderr, "[X11 ERROR] Cannot open dedicated Xlib display connection!\n");
        return FALSE;
    }

    app->screen = DefaultScreen(app->sub_dpy);
    app->root_window = RootWindow(app->sub_dpy, app->screen);

    /* Select SubstructureNotifyMask on Root Window to monitor all window restacks */
    XSelectInput(app->sub_dpy, app->root_window, SubstructureNotifyMask);
    XSync(app->sub_dpy, False);

    x11_dom_detect_primary_monitor(app);

    return TRUE;
}

/* Cleanup dominance resources */
void x11_dom_cleanup(OverlayApp *app) {
    if (app->heartbeat_timer_id > 0) {
        g_source_remove(app->heartbeat_timer_id);
        app->heartbeat_timer_id = 0;
    }

    if (app->x_source) {
        g_source_destroy(app->x_source);
        g_source_unref(app->x_source);
        app->x_source = NULL;
    }

    if (app->sub_dpy) {
        XCloseDisplay(app->sub_dpy);
        app->sub_dpy = NULL;
    }
}
