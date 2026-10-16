#include "window.h"
#include "xutil.h"
#include <gdk/gdkx.h>
#include <cairo.h>
#include <time.h>
#include <stdio.h>
#include <math.h>

static GtkWidget *s_window = NULL;
static GtkWidget *s_drawing_area = NULL;
static Window     s_xid = None;
static gboolean   s_visible = FALSE;
static guint      s_clock_timer_id = 0;
static int        s_pos_x = 0;
static int        s_pos_y = 0;

static void draw_rounded_rect(cairo_t *cr, double x, double y, double width, double height, double radius) {
    double degrees = M_PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path(cr);
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget;
    (void)user_data;

    GdkScreen *screen = gtk_widget_get_screen(s_window);
    gboolean is_composited = gdk_screen_is_composited(screen);

    if (is_composited) {
        /* Clear background completely to transparent */
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    }

    /* 1. Phone Body (Dark Rounded Rectangle) */
    draw_rounded_rect(cr, 1.0, 1.0, OVERLAY_WIDTH - 2.0, OVERLAY_HEIGHT - 2.0, PHONE_CORNER_RADIUS);
    cairo_set_source_rgba(cr, 0.09, 0.09, 0.13, 0.96);
    cairo_fill_preserve(cr);

    /* Phone Outer Border */
    cairo_set_source_rgba(cr, 0.35, 0.37, 0.48, 0.90);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    /* 2. Top Speaker / Notch */
    double notch_w = 90.0;
    double notch_h = 18.0;
    draw_rounded_rect(cr, (OVERLAY_WIDTH - notch_w) / 2.0, 8.0, notch_w, notch_h, 8.0);
    cairo_set_source_rgb(cr, 0.04, 0.04, 0.06);
    cairo_fill(cr);

    /* Camera lens dot */
    cairo_arc(cr, (OVERLAY_WIDTH / 2.0) + 22.0, 17.0, 3.5, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.20, 0.25, 0.40, 0.85);
    cairo_fill(cr);

    /* 3. Centered Clock */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 44.0);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, time_str, &ext);

    double clock_x = (OVERLAY_WIDTH - ext.width) / 2.0 - ext.x_bearing;
    double clock_y = (OVERLAY_HEIGHT / 2.0) - 20.0;

    cairo_set_source_rgb(cr, 0.88, 0.90, 0.98);
    cairo_move_to(cr, clock_x, clock_y);
    cairo_show_text(cr, time_str);

    /* Subtitle labels */
    cairo_set_font_size(cr, 14.0);
    const char *title = "PhoneOverlay";
    cairo_text_extents(cr, title, &ext);
    cairo_set_source_rgb(cr, 0.55, 0.60, 0.78);
    cairo_move_to(cr, (OVERLAY_WIDTH - ext.width) / 2.0 - ext.x_bearing, clock_y + 36.0);
    cairo_show_text(cr, title);

    cairo_set_font_size(cr, 12.0);
    const char *hotkey_hint = "Ctrl+Shift+Alt+X to toggle";
    cairo_text_extents(cr, hotkey_hint, &ext);
    cairo_set_source_rgb(cr, 0.42, 0.46, 0.58);
    cairo_move_to(cr, (OVERLAY_WIDTH - ext.width) / 2.0 - ext.x_bearing, clock_y + 62.0);
    cairo_show_text(cr, hotkey_hint);

    /* Bottom Home Indicator Bar */
    double home_w = 120.0;
    double home_h = 4.0;
    draw_rounded_rect(cr, (OVERLAY_WIDTH - home_w) / 2.0, OVERLAY_HEIGHT - 16.0, home_w, home_h, 2.0);
    cairo_set_source_rgba(cr, 0.60, 0.65, 0.75, 0.60);
    cairo_fill(cr);

    return FALSE;
}

static gboolean on_clock_tick(gpointer user_data) {
    (void)user_data;
    if (s_visible && s_drawing_area) {
        gtk_widget_queue_draw(s_drawing_area);
    }
    return G_SOURCE_CONTINUE;
}

static void calculate_primary_monitor_position(void) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *primary_mon = gdk_display_get_primary_monitor(display);
    if (!primary_mon) {
        xutil_log("WINDOW", "Primary monitor not flagged. Falling back to monitor 0.");
        primary_mon = gdk_display_get_monitor(display, 0);
    }

    GdkRectangle geom;
    gdk_monitor_get_geometry(primary_mon, &geom);

    s_pos_x = geom.x + OVERLAY_MARGIN;
    s_pos_y = geom.y + geom.height - OVERLAY_HEIGHT - OVERLAY_MARGIN;

    xutil_log("WINDOW", "Primary monitor geometry: %dx%d+%d+%d. Target overlay position: (%d, %d)",
              geom.width, geom.height, geom.x, geom.y, s_pos_x, s_pos_y);
}

gboolean window_init(gboolean start_visible) {
    /* 1. Create GTK_WINDOW_POPUP -> Sets override_redirect at X11 level */
    s_window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_default_size(GTK_WINDOW(s_window), OVERLAY_WIDTH, OVERLAY_HEIGHT);
    gtk_widget_set_size_request(s_window, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(s_window), FALSE);

    /* 2. Configure RGBA visual for transparent rounded corners */
    GdkScreen *screen = gtk_widget_get_screen(s_window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(s_window, visual);
        xutil_log("WINDOW", "RGBA visual supported and compositor active. Transparency enabled.");
    } else {
        xutil_log("WINDOW", "RGBA visual or compositor not available. Falling back to opaque.");
    }
    gtk_widget_set_app_paintable(s_window, TRUE);

    /* 3. Non-focus stealing policy & taskbar/pager hints */
    gtk_window_set_accept_focus(GTK_WINDOW(s_window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(s_window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(s_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(s_window), TRUE);

    /* 4. Position at bottom-left of primary monitor with 12px margins */
    calculate_primary_monitor_position();
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);

    /* 5. Content: GtkDrawingArea with Cairo Phone Frame & Clock */
    s_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_drawing_area, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    g_signal_connect(s_drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    gtk_container_add(GTK_CONTAINER(s_window), s_drawing_area);

    /* Realize widget to obtain underlying XID */
    gtk_widget_realize(s_window);
    GdkWindow *gdk_win = gtk_widget_get_window(s_window);
    s_xid = gdk_x11_window_get_xid(gdk_win);

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, OVERLAY_WIDTH, OVERLAY_HEIGHT);

    /* Ensure X11 input hint is FALSE */
    XWMHints *hints = XGetWMHints(xdpy, s_xid);
    if (!hints) hints = XAllocWMHints();
    if (hints) {
        hints->flags |= InputHint;
        hints->input = False;
        XSetWMHints(xdpy, s_xid, hints);
        XFree(hints);
    }

    xutil_log("WINDOW", "Created override-redirect overlay XID 0x%lx (%lu)",
              (unsigned long)s_xid, (unsigned long)s_xid);

    /* 6. Refresh clock every second */
    s_clock_timer_id = g_timeout_add_seconds(1, on_clock_tick, s_drawing_area);

    if (start_visible) {
        overlay_show();
    } else {
        overlay_hide();
    }

    return TRUE;
}

void overlay_show(void) {
    if (!s_window) return;

    gtk_widget_show_all(s_window);
    gtk_widget_realize(s_window);

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    XMapRaised(xdpy, s_xid);
    XFlush(xdpy);

    s_visible = TRUE;
    xutil_log("WINDOW", "Overlay shown (mapped raised at (%d, %d))", s_pos_x, s_pos_y);
    if (s_drawing_area) {
        gtk_widget_queue_draw(s_drawing_area);
    }
}

void overlay_hide(void) {
    if (!s_window) return;

    gtk_widget_hide(s_window);
    s_visible = FALSE;
    xutil_log("WINDOW", "Overlay hidden");
}

void overlay_toggle(void) {
    if (s_visible) {
        overlay_hide();
    } else {
        overlay_show();
    }
}

Window overlay_get_xid(void) {
    return s_xid;
}

gboolean overlay_is_visible(void) {
    return s_visible;
}

void window_cleanup(void) {
    if (s_clock_timer_id > 0) {
        g_source_remove(s_clock_timer_id);
        s_clock_timer_id = 0;
    }
    if (s_window) {
        gtk_widget_destroy(s_window);
        s_window = NULL;
    }
    s_xid = None;
    s_visible = FALSE;
    xutil_log("WINDOW", "Window cleaned up.");
}
