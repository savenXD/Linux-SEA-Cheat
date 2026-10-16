#include "window.h"
#include "input.h"
#include "raise.h"
#include "xutil.h"
#include <gdk/gdkx.h>
#include <cairo.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <X11/extensions/shape.h>

static GtkWidget *s_window = NULL;
static GtkWidget *s_drawing_area = NULL;
static GtkWidget *s_test_entry = NULL;
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

static void update_x11_state_properties(void) {
    if (s_xid == None) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    Atom prop_state = XInternAtom(xdpy, "_PHONEOVERLAY_STATE", False);
    Atom prop_text = XInternAtom(xdpy, "_PHONEOVERLAY_ENTRY_TEXT", False);
    Atom utf8_string = XInternAtom(xdpy, "UTF8_STRING", False);

    char state_str[128];
    snprintf(state_str, sizeof(state_str), "MODE=%s TARGET=%s GHOST=%s GRAB=%s",
             input_get_key_mode() == KEY_MODE_EXCLUSIVE ? "EXCLUSIVE" : "MIRROR",
             input_get_typing_target() == TYPING_TARGET_OVERLAY ? "OVERLAY" : "NONE",
             input_get_ghost_mode() == GHOST_MODE_ON ? "GHOST" : "SOLID",
             input_get_grab_status() == GRAB_STATUS_ACTIVE ? "ACTIVE" :
             (input_get_grab_status() == GRAB_STATUS_FAILED ? "FAILED" : "NONE"));

    XChangeProperty(xdpy, s_xid, prop_state, utf8_string, 8, PropModeReplace,
                    (unsigned char *)state_str, strlen(state_str));

    const char *text = overlay_get_entry_text();
    XChangeProperty(xdpy, s_xid, prop_text, utf8_string, 8, PropModeReplace,
                    (unsigned char *)text, strlen(text));
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

    /* 3. Status Badges Pill Bar (y=34 to 56) */
    GhostMode ghost = input_get_ghost_mode();
    KeyMode mode = input_get_key_mode();
    TypingTarget target = input_get_typing_target();
    GrabStatus grab = input_get_grab_status();

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 10.5);

    /* Badge 1: Key / Ghost Mode */
    const char *mode_str = "[MIRROR]";
    if (ghost == GHOST_MODE_ON) {
        mode_str = "[GHOST]";
        cairo_set_source_rgba(cr, 0.58, 0.18, 0.85, 0.90); /* Purple */
    } else if (mode == KEY_MODE_EXCLUSIVE) {
        mode_str = "[EXCL]";
        cairo_set_source_rgba(cr, 0.90, 0.45, 0.05, 0.90); /* Orange */
    } else {
        mode_str = "[MIRROR]";
        cairo_set_source_rgba(cr, 0.08, 0.55, 0.85, 0.90); /* Cyan / Blue */
    }
    draw_rounded_rect(cr, 20.0, 34.0, 70.0, 22.0, 5.0);
    cairo_fill(cr);

    cairo_text_extents_t ext;
    cairo_text_extents(cr, mode_str, &ext);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, 20.0 + (70.0 - ext.width) / 2.0 - ext.x_bearing, 34.0 + 15.0);
    cairo_show_text(cr, mode_str);

    /* Badge 2: Typing Target */
    const char *target_str = (target == TYPING_TARGET_OVERLAY) ? "● TYPING: OVERLAY" : "○ TYPING: NONE";
    if (target == TYPING_TARGET_OVERLAY) {
        cairo_set_source_rgba(cr, 0.12, 0.55, 0.25, 0.90); /* Emerald Green */
    } else {
        cairo_set_source_rgba(cr, 0.22, 0.25, 0.33, 0.85); /* Slate Gray */
    }
    draw_rounded_rect(cr, 96.0, 34.0, 146.0, 22.0, 5.0);
    cairo_fill(cr);

    cairo_text_extents(cr, target_str, &ext);
    if (target == TYPING_TARGET_OVERLAY) {
        cairo_set_source_rgb(cr, 0.88, 1.0, 0.88);
    } else {
        cairo_set_source_rgb(cr, 0.65, 0.70, 0.80);
    }
    cairo_move_to(cr, 96.0 + (146.0 - ext.width) / 2.0 - ext.x_bearing, 34.0 + 15.0);
    cairo_show_text(cr, target_str);

    /* Badge 3: Grab Failed Warning */
    if (grab == GRAB_STATUS_FAILED) {
        cairo_set_source_rgba(cr, 0.85, 0.15, 0.15, 0.95); /* Alert Red */
        draw_rounded_rect(cr, 248.0, 34.0, 92.0, 22.0, 5.0);
        cairo_fill(cr);

        const char *grab_str = "[!GRAB]";
        cairo_text_extents(cr, grab_str, &ext);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_move_to(cr, 248.0 + (92.0 - ext.width) / 2.0 - ext.x_bearing, 34.0 + 15.0);
        cairo_show_text(cr, grab_str);
    }

    /* 4. Clock and Title */
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 42.0);
    cairo_text_extents(cr, time_str, &ext);
    double clock_x = (OVERLAY_WIDTH - ext.width) / 2.0 - ext.x_bearing;
    double clock_y = 190.0;
    cairo_set_source_rgb(cr, 0.88, 0.90, 0.98);
    cairo_move_to(cr, clock_x, clock_y);
    cairo_show_text(cr, time_str);

    cairo_set_font_size(cr, 13.0);
    const char *title = "PhoneOverlay";
    cairo_text_extents(cr, title, &ext);
    cairo_set_source_rgb(cr, 0.55, 0.60, 0.78);
    cairo_move_to(cr, (OVERLAY_WIDTH - ext.width) / 2.0 - ext.x_bearing, clock_y + 26.0);
    cairo_show_text(cr, title);

    cairo_set_font_size(cr, 11.0);
    const char *hotkey_hint = "Ctrl+Shift+Alt+X to toggle";
    cairo_text_extents(cr, hotkey_hint, &ext);
    cairo_set_source_rgb(cr, 0.42, 0.46, 0.58);
    cairo_move_to(cr, (OVERLAY_WIDTH - ext.width) / 2.0 - ext.x_bearing, clock_y + 46.0);
    cairo_show_text(cr, hotkey_hint);

    /* 5. Test Widget Label and Instructions */
    cairo_set_font_size(cr, 12.0);
    cairo_set_source_rgb(cr, 0.55, 0.75, 1.0);
    cairo_move_to(cr, 30.0, 395.0);
    cairo_show_text(cr, "Type test:");

    cairo_set_font_size(cr, 10.0);
    cairo_set_source_rgb(cr, 0.50, 0.55, 0.68);
    cairo_move_to(cr, 30.0, 412.0);
    cairo_show_text(cr, "Ctrl+Shift+Alt+M: Mirror/Excl  |  +G: Ghost");

    /* Sub-entry hints */
    cairo_set_font_size(cr, 10.0);
    cairo_set_source_rgb(cr, 0.40, 0.45, 0.55);
    cairo_move_to(cr, 30.0, 485.0);
    cairo_show_text(cr, "Click overlay to target typing; click outside to release.");

    /* 6. Bottom Home Indicator Bar */
    double home_w = 120.0;
    double home_h = 4.0;
    draw_rounded_rect(cr, (OVERLAY_WIDTH - home_w) / 2.0, OVERLAY_HEIGHT - 16.0, home_w, home_h, 2.0);
    cairo_set_source_rgba(cr, 0.60, 0.65, 0.75, 0.60);
    cairo_fill(cr);

    /* Synchronize X11 properties */
    update_x11_state_properties();

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

static void apply_entry_styles(GtkWidget *entry) {
    GtkCssProvider *provider = gtk_css_provider_new();
    const char *css =
        "entry {"
        "  background: rgba(22, 24, 38, 0.95);"
        "  color: #f0f2f8;"
        "  border: 1px solid #4a5270;"
        "  border-radius: 8px;"
        "  padding: 6px 12px;"
        "  font-size: 14px;"
        "  font-family: monospace;"
        "  caret-color: #64b5f6;"
        "}"
        "entry:focus {"
        "  border-color: #42a5f5;"
        "  background: rgba(28, 32, 50, 0.98);"
        "}";
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(entry);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static gboolean on_widget_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    input_notify_native_click(event);
    return FALSE; /* Let normal GTK handling continue */
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

    /* 5. Content Hierarchy: GtkOverlay containing GtkDrawingArea and GtkEntry */
    GtkWidget *overlay_container = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(s_window), overlay_container);

    s_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_drawing_area, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    g_signal_connect(s_drawing_area, "draw", G_CALLBACK(on_draw), NULL);
    gtk_container_add(GTK_CONTAINER(overlay_container), s_drawing_area);

    /* Fixed container for positioning the temporary test entry */
    GtkWidget *fixed = gtk_fixed_new();
    s_test_entry = gtk_entry_new();
    gtk_widget_set_size_request(s_test_entry, 300, 38);
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_test_entry), "Type test here...");
    apply_entry_styles(s_test_entry);
    gtk_fixed_put(GTK_FIXED(fixed), s_test_entry, 30, 424);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay_container), fixed);

    /* Connect native button click events */
    gtk_widget_add_events(s_window, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(s_window, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);

    gtk_widget_add_events(s_drawing_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(s_drawing_area, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);

    g_signal_connect(s_test_entry, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);

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

    Atom net_wm_name = XInternAtom(xdpy, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(xdpy, "UTF8_STRING", False);
    XChangeProperty(xdpy, s_xid, net_wm_name, utf8_string, 8, PropModeReplace,
                    (unsigned char *)"PhoneOverlay", 12);
    XStoreName(xdpy, s_xid, "PhoneOverlay");

    update_x11_state_properties();

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

    /* Start the aggressive restack watcher only while visible */
    raise_start();
}

void overlay_hide(void) {
    if (!s_window) return;

    /* Stop restack watcher immediately to ensure zero CPU while hidden */
    raise_stop();

    /* Panic ungrab: release keyboard grab and clear typing target */
    input_release_grab();
    input_notify_overlay_hidden();

    gtk_widget_hide(s_window);
    s_visible = FALSE;
    xutil_log("WINDOW", "Overlay hidden");
}

void overlay_reposition(void) {
    if (!s_window || s_xid == None) return;

    calculate_primary_monitor_position();
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, OVERLAY_WIDTH, OVERLAY_HEIGHT);
    XFlush(xdpy);

    if (s_drawing_area) {
        gtk_widget_queue_draw(s_drawing_area);
    }
    xutil_log("WINDOW", "Overlay repositioned to (%d, %d)", s_pos_x, s_pos_y);
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

void overlay_get_geometry(int *x, int *y, int *w, int *h) {
    if (x) *x = s_pos_x;
    if (y) *y = s_pos_y;
    if (w) *w = OVERLAY_WIDTH;
    if (h) *h = OVERLAY_HEIGHT;
}

void overlay_queue_draw(void) {
    if (s_drawing_area) {
        gtk_widget_queue_draw(s_drawing_area);
    }
    update_x11_state_properties();
}

void overlay_set_ghost(gboolean ghost) {
    if (s_xid == None) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    if (ghost) {
        /* Clear input shape: all mouse clicks pass straight through */
        XShapeCombineRectangles(xdpy, s_xid, ShapeInput, 0, 0, NULL, 0, ShapeSet, YXBanded);
        xutil_log("WINDOW", "Applied ShapeInput 0 rects: overlay is click-through (GHOST).");
    } else {
        /* Reset input shape: restores normal solid bounding rectangle */
        XShapeCombineMask(xdpy, s_xid, ShapeInput, 0, 0, None, ShapeSet);
        xutil_log("WINDOW", "Restored default ShapeInput mask: overlay is SOLID.");
    }
    XFlush(xdpy);
    overlay_queue_draw();
}

GtkWidget *overlay_get_entry_widget(void) {
    return s_test_entry;
}

const char *overlay_get_entry_text(void) {
    if (!s_test_entry) return "";
    return gtk_entry_get_text(GTK_ENTRY(s_test_entry));
}

void overlay_clear_entry_text(void) {
    if (s_test_entry) {
        gtk_entry_set_text(GTK_ENTRY(s_test_entry), "");
        overlay_queue_draw();
    }
}

void overlay_set_entry_text(const char *text) {
    if (s_test_entry && text) {
        gtk_entry_set_text(GTK_ENTRY(s_test_entry), text);
        overlay_queue_draw();
    }
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
