#include "window.h"
#include "browser.h"
#include "config.h"
#include "input.h"
#include "raise.h"
#include "xutil.h"

#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <cairo.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>
#include <X11/extensions/shape.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *s_svg_back_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"19\" y1=\"12\" x2=\"5\" y2=\"12\"/>"
    "<polyline points=\"12 19 5 12 12 5\"/>"
    "</svg>";

static const char *s_svg_home_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M3 9.5L12 3l9 6.5V20a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V9.5z\"/>"
    "<polyline points=\"9 22 9 12 15 12 15 22\"/>"
    "</svg>";

static const char *s_svg_ghost_idle =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M9 10h.01M15 10h.01M12 2a8 8 0 0 0-8 8v10l3-2 3 2 2-2 2 2 3-2 3 2V10a8 8 0 0 0-8-8z\"/>"
    "</svg>";

static const char *s_svg_ghost_typing =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#38bdf8\" stroke-width=\"2.6\" stroke-linecap=\"round\">"
    "<circle cx=\"12\" cy=\"12\" r=\"9\" stroke-dasharray=\"28\" stroke-dashoffset=\"10\"/>"
    "<circle cx=\"12\" cy=\"12\" r=\"2.5\" fill=\"#38bdf8\"/>"
    "</svg>";

static const char *s_svg_check_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#22c55e\" stroke-width=\"2.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<polyline points=\"20 6 9 17 4 12\"/>"
    "</svg>";

static const char *s_svg_failed_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#ef4444\" stroke-width=\"2.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";

static const char *s_svg_camera_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e4e4e7\" stroke-width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z\"/>"
    "<circle cx=\"12\" cy=\"13\" r=\"4\"/>"
    "</svg>";

static const char *s_svg_snap_running =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#38bdf8\" stroke-width=\"2.5\" stroke-linecap=\"round\">"
    "<circle cx=\"12\" cy=\"12\" r=\"9\" stroke-dasharray=\"28\" stroke-dashoffset=\"10\"/>"
    "</svg>";

static const char *s_svg_reload_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#a1a1aa\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<polyline points=\"23 4 23 10 17 10\"/>"
    "<path d=\"M20.49 15a9 9 0 1 1-2.12-9.36L23 10\"/>"
    "</svg>";

static const char *s_svg_stop_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#a1a1aa\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";

static const char *s_svg_search_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#71717a\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<circle cx=\"11\" cy=\"11\" r=\"8\"/>"
    "<line x1=\"21\" y1=\"21\" x2=\"16.65\" y2=\"16.65\"/>"
    "</svg>";

static const char *s_svg_close_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#ef4444\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
    "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
    "</svg>";

static const char *s_svg_chatgpt_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<defs>"
    "<linearGradient id=\"cg_grad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">"
    "<stop offset=\"0%\" stop-color=\"#18181b\"/>"
    "<stop offset=\"60%\" stop-color=\"#09261a\"/>"
    "<stop offset=\"100%\" stop-color=\"#043320\"/>"
    "</linearGradient>"
    "<linearGradient id=\"cg_glow\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">"
    "<stop offset=\"0%\" stop-color=\"#10a37f\" stop-opacity=\"0.5\"/>"
    "<stop offset=\"100%\" stop-color=\"#059669\" stop-opacity=\"0.9\"/>"
    "</linearGradient>"
    "</defs>"
    "<rect x=\"1\" y=\"1\" width=\"62\" height=\"62\" rx=\"16\" ry=\"16\" fill=\"url(#cg_grad)\" stroke=\"url(#cg_glow)\" stroke-width=\"1.5\"/>"
    "<g transform=\"translate(32,32) scale(1.15)\">"
    "<g fill=\"none\" stroke=\"#ffffff\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M0,-3.5 C5.5,-14.0 13.5,-10.5 12.0,-4.5 C10.5,1.5 3.5,2.5 0,3.5\"/>"
    "<path d=\"M0,-3.5 C5.5,-14.0 13.5,-10.5 12.0,-4.5 C10.5,1.5 3.5,2.5 0,3.5\" transform=\"rotate(60)\"/>"
    "<path d=\"M0,-3.5 C5.5,-14.0 13.5,-10.5 12.0,-4.5 C10.5,1.5 3.5,2.5 0,3.5\" transform=\"rotate(120)\"/>"
    "<path d=\"M0,-3.5 C5.5,-14.0 13.5,-10.5 12.0,-4.5 C10.5,1.5 3.5,2.5 0,3.5\" transform=\"rotate(180)\"/>"
    "<path d=\"M0,-3.5 C5.5,-14.0 13.5,-10.5 12.0,-4.5 C10.5,1.5 3.5,2.5 0,3.5\" transform=\"rotate(240)\"/>"
    "<path d=\"M0,-3.5 C5.5,-14.0 13.5,-10.5 12.0,-4.5 C10.5,1.5 3.5,2.5 0,3.5\" transform=\"rotate(300)\"/>"
    "</g>"
    "</g>"
    "</svg>";

static const char *s_svg_browser_icon =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">"
    "<defs>"
    "<linearGradient id=\"br_grad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">"
    "<stop offset=\"0%\" stop-color=\"#18181b\"/>"
    "<stop offset=\"60%\" stop-color=\"#0b2342\"/>"
    "<stop offset=\"100%\" stop-color=\"#08305c\"/>"
    "</linearGradient>"
    "<linearGradient id=\"br_glow\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">"
    "<stop offset=\"0%\" stop-color=\"#38bdf8\" stop-opacity=\"0.5\"/>"
    "<stop offset=\"100%\" stop-color=\"#2563eb\" stop-opacity=\"0.9\"/>"
    "</linearGradient>"
    "</defs>"
    "<rect x=\"1\" y=\"1\" width=\"62\" height=\"62\" rx=\"16\" ry=\"16\" fill=\"url(#br_grad)\" stroke=\"url(#br_glow)\" stroke-width=\"1.5\"/>"
    "<g transform=\"translate(32,32) scale(1.05)\">"
    "<g fill=\"none\" stroke=\"#ffffff\" stroke-width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<circle cx=\"0\" cy=\"0\" r=\"14\"/>"
    "<line x1=\"-14\" y1=\"0\" x2=\"14\" y2=\"0\"/>"
    "<ellipse cx=\"0\" cy=\"0\" rx=\"7\" ry=\"14\"/>"
    "<path d=\"M-10,-8 C-5,-5.5 5,-5.5 10,-8\"/>"
    "<path d=\"M-10,8 C-5,5.5 5,5.5 10,8\"/>"
    "</g>"
    "</g>"
    "</svg>";

static const char *s_svg_resize_grip =
    "<svg viewBox=\"0 0 16 16\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M14 14L8 14M14 10L10 14M14 6L6 14\" stroke=\"#71717a\" stroke-width=\"1.5\" stroke-linecap=\"round\"/>"
    "</svg>";

static GtkWidget *create_svg_image(const char *svg_str, int width, int height) {
    if (!svg_str) return gtk_label_new("");
    GInputStream *stream = g_memory_input_stream_new_from_data(svg_str, -1, NULL);
    if (!stream) return gtk_label_new("");
    GdkPixbuf *pb = gdk_pixbuf_new_from_stream_at_scale(stream, width, height, TRUE, NULL, NULL);
    g_object_unref(stream);
    if (!pb) return gtk_label_new("");
    GtkWidget *img = gtk_image_new_from_pixbuf(pb);
    g_object_unref(pb);
    return img;
}

static gboolean s_is_snapping = FALSE;
static guint    s_snap_revert_timer = 0;
static gboolean s_was_visible_during_snap = FALSE;

static GtkWidget *s_window = NULL;
static GtkWidget *s_root_box = NULL;
static GtkWidget *s_overlay = NULL;
static GtkWidget *s_stack = NULL;

static GtkWidget *s_url_entry = NULL;
static GtkWidget *s_progress_bar = NULL;
static GtkWidget *s_header_box = NULL;

static GtkWidget *s_test_entry = NULL;

static GtkWidget *s_mode_pill = NULL;
static GtkWidget *s_target_pill = NULL;
static GtkWidget *s_grab_pill = NULL;

static GtkWidget *s_btn_back = NULL;
static GtkWidget *s_btn_reload = NULL;
static GtkWidget *s_reload_stack = NULL;
static GtkWidget *s_btn_home = NULL;
static GtkWidget *s_btn_ghost = NULL;
static GtkWidget *s_btn_ghost_stack = NULL;
static guint      s_ghost_revert_timer = 0;
static GtkWidget *s_btn_snap = NULL;
static GtkWidget *s_snap_stack = NULL;

static GtkWidget *s_toast_box = NULL;
static GtkWidget *s_toast_label = NULL;
static guint      s_toast_timer_id = 0;

static GtkWidget *s_popup_container = NULL;
static GtkWidget *s_popup_child_view = NULL;

static Window     s_xid = None;
static gboolean   s_visible = FALSE;
static guint      s_clock_timer_id = 0;
static int        s_pos_x = 0;
static int        s_pos_y = 0;
static int        s_win_width = OVERLAY_WIDTH;
static int        s_win_height = OVERLAY_HEIGHT;
static double     s_current_opacity = 1.0;
static GtkWidget *s_opacity_label = NULL;
static char       s_current_app[32] = "home";

typedef enum {
    RESIZE_NONE = 0,
    RESIZE_TOP = 1 << 0,
    RESIZE_BOTTOM = 1 << 1,
    RESIZE_LEFT = 1 << 2,
    RESIZE_RIGHT = 1 << 3,
    MOVE_DRAG = 1 << 4
} DragOperation;

static DragOperation s_active_drag = RESIZE_NONE;
static int s_drag_start_root_x = 0;
static int s_drag_start_root_y = 0;
static int s_drag_start_win_x = 0;
static int s_drag_start_win_y = 0;
static int s_drag_start_win_w = 0;
static int s_drag_start_win_h = 0;
static int s_prev_saved_w = OVERLAY_WIDTH;
static int s_prev_saved_h = OVERLAY_HEIGHT;
static int s_prev_saved_x = 0;
static int s_prev_saved_y = 0;
static gboolean s_is_maximized = FALSE;

static const char *s_phone_css =
    "* {"
    "  -GtkScrollbar-has-backward-stepper: 0;"
    "  -GtkScrollbar-has-forward-stepper: 0;"
    "}"
    ".phone-root {"
    "  background-color: #000000;"
    "  border: 2px solid #27272a;"
    "  border-radius: 36px;"
    "  padding: 0px;"
    "}"
    ".status-bar {"
    "  padding: 8px 14px 6px 14px;"
    "  background-color: #000000;"
    "  border-top-left-radius: 34px;"
    "  border-top-right-radius: 34px;"
    "}"
    ".camera-island {"
    "  background-color: #0c0c0e;"
    "  border: 1px solid #1f1f23;"
    "  border-radius: 12px;"
    "  padding: 3px 12px;"
    "}"
    ".camera-lens {"
    "  min-width: 8px;"
    "  min-height: 8px;"
    "  border-radius: 50%;"
    "  background-color: #18181b;"
    "  border: 1px solid #27272a;"
    "}"
    ".camera-speaker {"
    "  min-width: 24px;"
    "  min-height: 3px;"
    "  border-radius: 2px;"
    "  background-color: #27272a;"
    "  margin-left: 6px;"
    "}"
    ".status-badge {"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "  font-size: 10px;"
    "  font-weight: 700;"
    "  color: #71717a;"
    "  letter-spacing: 0.5px;"
    "}"
    ".pill {"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "  font-size: 9.5px;"
    "  font-weight: 700;"
    "  letter-spacing: 0.5px;"
    "  padding: 3px 8px;"
    "  border-radius: 6px;"
    "  border: 1px solid transparent;"
    "}"
    ".pill-mirror {"
    "  background-color: #18181b;"
    "  color: #e4e4e7;"
    "  border: 1px solid #27272a;"
    "}"
    ".pill-excl {"
    "  background-color: #271c12;"
    "  color: #fb923c;"
    "  border: 1px solid #7c2d12;"
    "}"
    ".pill-ghost {"
    "  background-color: #20132e;"
    "  color: #c084fc;"
    "  border: 1px solid #581c87;"
    "}"
    ".pill-target-on {"
    "  background-color: #0e2417;"
    "  color: #4ade80;"
    "  border: 1px solid #166534;"
    "}"
    ".pill-target-off {"
    "  background-color: #141417;"
    "  color: #71717a;"
    "  border: 1px solid #27272a;"
    "}"
    ".pill-grab-alert {"
    "  background-color: #2b1215;"
    "  color: #f87171;"
    "  border: 1px solid #991b1b;"
    "}"
    ".pill-opacity {"
    "  background-color: #141417;"
    "  color: #a1a1aa;"
    "  border: 1px solid #27272a;"
    "  padding: 1px 4px;"
    "}"
    ".opacity-btn {"
    "  color: #71717a;"
    "  font-weight: 700;"
    "  font-size: 11px;"
    "  padding: 0 4px;"
    "}"
    ".opacity-btn:hover {"
    "  color: #f4f4f5;"
    "}"
    ".resize-grip {"
    "  opacity: 0.5;"
    "  padding: 2px;"
    "}"
    ".resize-grip:hover {"
    "  opacity: 1.0;"
    "}"
    ".header-box {"
    "  padding: 4px 12px 6px 12px;"
    "  background-color: #000000;"
    "  border-bottom: 1px solid #1c1c1f;"
    "}"
    ".url-bar-container {"
    "  background-color: #141417;"
    "  border: 1px solid #27272a;"
    "  border-radius: 12px;"
    "  padding: 2px 6px 2px 10px;"
    "}"
    ".url-bar-container:focus-within {"
    "  border-color: #52525b;"
    "  background-color: #18181b;"
    "}"
    ".url-leading-icon {"
    "  opacity: 0.6;"
    "}"
    ".url-entry {"
    "  background-color: transparent;"
    "  color: #f4f4f5;"
    "  border: none;"
    "  box-shadow: none;"
    "  padding: 4px 6px;"
    "  font-size: 12px;"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "  caret-color: #ffffff;"
    "}"
    ".url-entry:focus {"
    "  background-color: transparent;"
    "  border: none;"
    "  box-shadow: none;"
    "}"
    ".url-action-btn {"
    "  background: transparent;"
    "  border: none;"
    "  padding: 4px 6px;"
    "  border-radius: 6px;"
    "}"
    ".url-action-btn:hover {"
    "  background-color: #27272a;"
    "}"
    ".load-progress {"
    "  min-height: 2px;"
    "  padding: 0;"
    "  margin: 0;"
    "}"
    ".load-progress trough {"
    "  min-height: 2px;"
    "  background-color: transparent;"
    "  border: none;"
    "}"
    ".load-progress progress {"
    "  min-height: 2px;"
    "  background-color: #ffffff;"
    "  border: none;"
    "}"
    ".home-page {"
    "  background: radial-gradient(circle at 50% 20%, #141418 0%, #000000 85%);"
    "}"
    ".home-search-bar {"
    "  background-color: #141417;"
    "  border: 1px solid #27272a;"
    "  border-radius: 20px;"
    "  padding: 10px 16px;"
    "  transition: all 120ms ease;"
    "}"
    ".home-search-bar:hover {"
    "  background-color: #1a1a1f;"
    "  border-color: #3f3f46;"
    "}"
    ".home-search-label {"
    "  font-size: 12px;"
    "  color: #71717a;"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "}"
    ".home-section-title {"
    "  font-size: 10px;"
    "  font-weight: 700;"
    "  color: #52525b;"
    "  letter-spacing: 1px;"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "}"
    ".app-button {"
    "  background: transparent;"
    "  border: none;"
    "  padding: 8px 14px;"
    "  border-radius: 18px;"
    "  transition: all 120ms ease;"
    "}"
    ".app-button:hover {"
    "  background-color: rgba(255, 255, 255, 0.05);"
    "}"
    ".app-button:active {"
    "  background-color: rgba(255, 255, 255, 0.1);"
    "}"
    ".app-label {"
    "  font-size: 12px;"
    "  font-weight: 600;"
    "  color: #f4f4f5;"
    "  margin-top: 8px;"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "  letter-spacing: 0.2px;"
    "}"
    ".test-input-box {"
    "  padding: 4px 12px 4px 12px;"
    "  background-color: #000000;"
    "  border-top: 1px solid #18181b;"
    "}"
    ".test-input-entry {"
    "  background-color: #141417;"
    "  color: #f4f4f5;"
    "  border: 1px solid #27272a;"
    "  border-radius: 10px;"
    "  padding: 5px 12px;"
    "  font-size: 11px;"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "  caret-color: #ffffff;"
    "}"
    ".test-input-entry:focus {"
    "  background-color: #18181b;"
    "  border-color: #52525b;"
    "}"
    ".bottom-nav {"
    "  padding: 6px 14px 4px 14px;"
    "  background-color: #000000;"
    "  border-top: 1px solid #18181b;"
    "}"
    ".nav-button {"
    "  background: transparent;"
    "  border: none;"
    "  padding: 6px 12px;"
    "  border-radius: 10px;"
    "  min-height: 36px;"
    "  transition: all 120ms ease;"
    "}"
    ".nav-button:hover {"
    "  background-color: #18181b;"
    "}"
    ".nav-button:active {"
    "  background-color: #27272a;"
    "}"
    ".nav-button:disabled {"
    "  opacity: 0.3;"
    "}"
    ".toast-box {"
    "  background-color: rgba(20, 20, 23, 0.96);"
    "  border: 1px solid #3f3f46;"
    "  border-radius: 16px;"
    "  padding: 8px 18px;"
    "  box-shadow: 0 10px 25px rgba(0, 0, 0, 0.7);"
    "}"
    ".toast-label {"
    "  color: #f4f4f5;"
    "  font-size: 11.5px;"
    "  font-weight: 600;"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "}"
    ".home-pill-box {"
    "  padding: 3px 0 8px 0;"
    "  background-color: #000000;"
    "  border-bottom-left-radius: 34px;"
    "  border-bottom-right-radius: 34px;"
    "}"
    ".home-pill {"
    "  min-height: 4px;"
    "  min-width: 120px;"
    "  background-color: #3f3f46;"
    "  border-radius: 2px;"
    "  transition: background-color 120ms ease;"
    "}"
    ".home-pill:hover {"
    "  background-color: #71717a;"
    "}"
    ".popup-modal-layer {"
    "  background-color: #000000;"
    "  border-radius: 24px;"
    "}"
    ".popup-modal-bar {"
    "  padding: 8px 14px;"
    "  background-color: #121215;"
    "  border-bottom: 1px solid #27272a;"
    "}"
    ".popup-modal-title {"
    "  color: #f4f4f5;"
    "  font-size: 12px;"
    "  font-weight: 600;"
    "  font-family: -apple-system, BlinkMacSystemFont, \"Segoe UI\", Roboto, sans-serif;"
    "}"
    ".popup-close-btn {"
    "  background: transparent;"
    "  border: none;"
    "  padding: 4px 8px;"
    "  border-radius: 6px;"
    "}"
    ".popup-close-btn:hover {"
    "  background-color: rgba(239, 68, 68, 0.18);"
    "}";

static void update_x11_state_properties(void) {
    if (s_xid == None) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    Atom prop_state = XInternAtom(xdpy, "_PHONEOVERLAY_STATE", False);
    Atom prop_text = XInternAtom(xdpy, "_PHONEOVERLAY_ENTRY_TEXT", False);
    Atom prop_url = XInternAtom(xdpy, "_PHONEOVERLAY_URL_TEXT", False);
    Atom prop_app = XInternAtom(xdpy, "_PHONEOVERLAY_APP", False);
    Atom utf8_string = XInternAtom(xdpy, "UTF8_STRING", False);

    char state_str[128];
    snprintf(state_str, sizeof(state_str), "MODE=%s TARGET=%s GHOST=%s GRAB=%s APP=%s",
             input_get_key_mode() == KEY_MODE_EXCLUSIVE ? "EXCLUSIVE" : "MIRROR",
             input_get_typing_target() == TYPING_TARGET_OVERLAY ? "OVERLAY" : "NONE",
             input_get_ghost_mode() == GHOST_MODE_ON ? "GHOST" : "SOLID",
             input_get_grab_status() == GRAB_STATUS_ACTIVE ? "ACTIVE" :
             (input_get_grab_status() == GRAB_STATUS_FAILED ? "FAILED" : "NONE"),
             s_current_app);

    XChangeProperty(xdpy, s_xid, prop_state, utf8_string, 8, PropModeReplace,
                    (unsigned char *)state_str, strlen(state_str));

    const char *text = overlay_get_entry_text();
    XChangeProperty(xdpy, s_xid, prop_text, utf8_string, 8, PropModeReplace,
                    (unsigned char *)text, strlen(text));

    const char *url = s_url_entry ? gtk_entry_get_text(GTK_ENTRY(s_url_entry)) : "";
    XChangeProperty(xdpy, s_xid, prop_url, utf8_string, 8, PropModeReplace,
                    (unsigned char *)url, strlen(url));

    XChangeProperty(xdpy, s_xid, prop_app, utf8_string, 8, PropModeReplace,
                    (unsigned char *)s_current_app, strlen(s_current_app));
}

void overlay_update_status_ui(void) {
    if (!s_window) return;

    if (s_mode_pill) {
        GhostMode ghost = input_get_ghost_mode();
        KeyMode mode = input_get_key_mode();
        GtkStyleContext *sc = gtk_widget_get_style_context(s_mode_pill);
        gtk_style_context_remove_class(sc, "pill-mirror");
        gtk_style_context_remove_class(sc, "pill-excl");
        gtk_style_context_remove_class(sc, "pill-ghost");

        if (ghost == GHOST_MODE_ON) {
            gtk_label_set_text(GTK_LABEL(s_mode_pill), "GHOST");
            gtk_style_context_add_class(sc, "pill-ghost");
        } else if (mode == KEY_MODE_EXCLUSIVE) {
            gtk_label_set_text(GTK_LABEL(s_mode_pill), "EXCLUSIVE");
            gtk_style_context_add_class(sc, "pill-excl");
        } else {
            gtk_label_set_text(GTK_LABEL(s_mode_pill), "MIRROR");
            gtk_style_context_add_class(sc, "pill-mirror");
        }
    }

    if (s_target_pill) {
        TypingTarget target = input_get_typing_target();
        GtkStyleContext *sc = gtk_widget_get_style_context(s_target_pill);
        gtk_style_context_remove_class(sc, "pill-target-on");
        gtk_style_context_remove_class(sc, "pill-target-off");

        if (target == TYPING_TARGET_OVERLAY) {
            gtk_label_set_text(GTK_LABEL(s_target_pill), "OVERLAY");
            gtk_style_context_add_class(sc, "pill-target-on");
        } else {
            gtk_label_set_text(GTK_LABEL(s_target_pill), "STANDBY");
            gtk_style_context_add_class(sc, "pill-target-off");
        }
    }

    if (s_grab_pill) {
        GrabStatus grab = input_get_grab_status();
        if (grab == GRAB_STATUS_FAILED) {
            gtk_label_set_text(GTK_LABEL(s_grab_pill), "BLOCKED");
            gtk_widget_show(s_grab_pill);
        } else {
            gtk_widget_hide(s_grab_pill);
        }
    }

    /* Update Back button sensitivity */
    if (s_btn_back) {
        if (strcmp(s_current_app, "home") == 0) {
            gtk_widget_set_sensitive(s_btn_back, FALSE);
        } else {
            gtk_widget_set_sensitive(s_btn_back, TRUE);
        }
    }

    update_x11_state_properties();
}

static gboolean on_clock_tick(gpointer user_data) {
    (void)user_data;
    if (s_visible) {
        overlay_update_status_ui();
    }
    return G_SOURCE_CONTINUE;
}

static void calculate_monitor_position(int *out_x, int *out_y) {
    const AppConfig *cfg = config_get();
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *mon = NULL;

    if (g_ascii_strcasecmp(cfg->monitor, "primary") == 0) {
        mon = gdk_display_get_primary_monitor(display);
    } else {
        int idx = atoi(cfg->monitor);
        int n_mon = gdk_display_get_n_monitors(display);
        if (idx >= 0 && idx < n_mon) {
            mon = gdk_display_get_monitor(display, idx);
        }
    }
    if (!mon) {
        mon = gdk_display_get_primary_monitor(display);
        if (!mon && gdk_display_get_n_monitors(display) > 0) {
            mon = gdk_display_get_monitor(display, 0);
        }
    }

    GdkRectangle geom = {0, 0, 1920, 1080};
    if (mon) {
        gdk_monitor_get_geometry(mon, &geom);
    }

    int w = cfg->width;
    int h = cfg->height;
    int m = cfg->margin;

    int x = geom.x + m;
    int y = geom.y + geom.height - h - m; /* Default bottom-left */

    if (g_ascii_strcasecmp(cfg->corner, "bottom-right") == 0) {
        x = geom.x + geom.width - w - m;
        y = geom.y + geom.height - h - m;
    } else if (g_ascii_strcasecmp(cfg->corner, "top-left") == 0) {
        x = geom.x + m;
        y = geom.y + m;
    } else if (g_ascii_strcasecmp(cfg->corner, "top-right") == 0) {
        x = geom.x + geom.width - w - m;
        y = geom.y + m;
    }

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;

    xutil_log("WINDOW", "Calculated position: (%d, %d) on monitor geom %dx%d+%d+%d (corner=%s, margin=%d)",
              x, y, geom.width, geom.height, geom.x, geom.y, cfg->corner, m);
}

static gboolean on_window_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)user_data;
    GdkScreen *screen = gtk_widget_get_screen(widget);
    if (gdk_screen_is_composited(screen)) {
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    }
    return FALSE; /* Let child widgets render */
}

static void set_window_cursor(GdkCursorType cursor_type) {
    if (!s_window) return;
    GdkWindow *gdk_win = gtk_widget_get_window(s_window);
    if (!gdk_win) return;
    if (cursor_type == (GdkCursorType)-1) {
        gdk_window_set_cursor(gdk_win, NULL);
    } else {
        GdkDisplay *display = gtk_widget_get_display(s_window);
        GdkCursor *cursor = gdk_cursor_new_for_display(display, cursor_type);
        gdk_window_set_cursor(gdk_win, cursor);
        g_object_unref(cursor);
    }
}

void overlay_set_opacity(double opacity) {
    if (opacity < 0.20) opacity = 0.20;
    if (opacity > 1.00) opacity = 1.00;
    s_current_opacity = opacity;
    if (s_window) {
        gtk_widget_set_opacity(s_window, s_current_opacity);
    }
    if (s_opacity_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%%", (int)(s_current_opacity * 100.0 + 0.5));
        gtk_label_set_text(GTK_LABEL(s_opacity_label), buf);
    }
    char toast[64];
    snprintf(toast, sizeof(toast), "Opacity: %d%%", (int)(s_current_opacity * 100.0 + 0.5));
    overlay_show_toast(toast);
}

static void get_monitor_bounds(int *out_w, int *out_h) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *mon = display ? gdk_display_get_primary_monitor(display) : NULL;
    GdkRectangle geom = {0, 0, 1920, 1080};
    if (mon) {
        gdk_monitor_get_geometry(mon, &geom);
    }
    if (out_w) *out_w = geom.width > 0 ? geom.width : 1920;
    if (out_h) *out_h = geom.height > 0 ? geom.height : 1080;
}

double overlay_get_opacity(void) {
    return s_current_opacity;
}

void overlay_set_size(int width, int height) {
    if (!s_window) return;

    int screen_w = 1920, screen_h = 1080;
    get_monitor_bounds(&screen_w, &screen_h);

    if (width < OVERLAY_MIN_WIDTH) width = OVERLAY_MIN_WIDTH;
    if (width > screen_w) width = screen_w;
    if (height < OVERLAY_MIN_HEIGHT) height = OVERLAY_MIN_HEIGHT;
    if (height > screen_h) height = screen_h;

    if (width == s_win_width && height == s_win_height) {
        if (s_xid != None) {
            Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
            XMoveWindow(xdpy, s_xid, s_pos_x, s_pos_y);
            XFlush(xdpy);
        }
        return;
    }

    s_win_width = width;
    s_win_height = height;

    gtk_window_resize(GTK_WINDOW(s_window), s_win_width, s_win_height);
    if (s_root_box) {
        gtk_widget_set_size_request(s_root_box, s_win_width, s_win_height);
    }

    if (s_xid != None) {
        Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
        XFlush(xdpy);
    }
    overlay_queue_draw();
}

void overlay_set_fullscreen(void) {
    if (!s_window) return;
    int mon_x = 0, mon_y = 0, mon_w = 1920, mon_h = 1080;
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *mon = display ? gdk_display_get_primary_monitor(display) : NULL;
    GdkRectangle geom = {0, 0, 1920, 1080};
    if (mon) {
        gdk_monitor_get_geometry(mon, &geom);
    }
    mon_x = geom.x;
    mon_y = geom.y;
    mon_w = geom.width > 0 ? geom.width : 1920;
    mon_h = geom.height > 0 ? geom.height : 1080;

    if (s_win_width == mon_w && s_win_height == mon_h) {
        /* Already fullscreen -> toggle back to previous size */
        int rest_w = (s_prev_saved_w > 0) ? s_prev_saved_w : OVERLAY_WIDTH;
        int rest_h = (s_prev_saved_h > 0) ? s_prev_saved_h : OVERLAY_HEIGHT;
        s_pos_x = s_prev_saved_x;
        s_pos_y = s_prev_saved_y;
        gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        overlay_set_size(rest_w, rest_h);
        s_is_maximized = FALSE;
        overlay_show_toast("Restored size");
    } else {
        s_prev_saved_w = s_win_width;
        s_prev_saved_h = s_win_height;
        s_prev_saved_x = s_pos_x;
        s_prev_saved_y = s_pos_y;

        s_pos_x = mon_x;
        s_pos_y = mon_y;
        gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        overlay_set_size(mon_w, mon_h);
        s_is_maximized = TRUE;
        overlay_show_toast("Fullscreen mode");
    }
}

void overlay_set_default_size(void) {
    if (!s_window) return;
    const AppConfig *cfg = config_get();
    int def_w = (cfg && cfg->width > 0) ? cfg->width : OVERLAY_WIDTH;
    int def_h = (cfg && cfg->height > 0) ? cfg->height : OVERLAY_HEIGHT;

    calculate_monitor_position(&s_pos_x, &s_pos_y);
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
    overlay_set_size(def_w, def_h);
    s_is_maximized = FALSE;
    overlay_show_toast("Default size (360x640)");
}

static void on_window_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data) {
    (void)user_data;
    (void)widget;
    if (allocation->width != s_win_width || allocation->height != s_win_height) {
        s_win_width = allocation->width;
        s_win_height = allocation->height;
        if (s_xid != None) {
            Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
            XResizeWindow(xdpy, s_xid, s_win_width, s_win_height);
            XFlush(xdpy);
        }
    }
}

static gboolean on_opacity_dec_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1) {
        overlay_set_opacity(s_current_opacity - 0.10);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_opacity_inc_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1) {
        overlay_set_opacity(s_current_opacity + 0.10);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_resize_grip_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1) {
        s_active_drag = RESIZE_RIGHT | RESIZE_BOTTOM;
        s_drag_start_root_x = (int)event->x_root;
        s_drag_start_root_y = (int)event->y_root;
        s_drag_start_win_x = s_pos_x;
        s_drag_start_win_y = s_pos_y;
        s_drag_start_win_w = s_win_width;
        s_drag_start_win_h = s_win_height;
        set_window_cursor(GDK_BOTTOM_RIGHT_CORNER);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_resize_grip_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (s_active_drag & (RESIZE_RIGHT | RESIZE_BOTTOM)) {
        int dx = (int)event->x_root - s_drag_start_root_x;
        int dy = (int)event->y_root - s_drag_start_root_y;
        int new_w = s_drag_start_win_w + dx;
        int new_h = s_drag_start_win_h + dy;
        overlay_set_size(new_w, new_h);
        return TRUE;
    }
    set_window_cursor(GDK_BOTTOM_RIGHT_CORNER);
    return FALSE;
}

static gboolean on_resize_grip_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1 && (s_active_drag != RESIZE_NONE)) {
        s_active_drag = RESIZE_NONE;
        set_window_cursor((GdkCursorType)-1);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_header_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1) {
        if (event->type == GDK_2BUTTON_PRESS) {
            int screen_w = 1920, screen_h = 1080;
            get_monitor_bounds(&screen_w, &screen_h);

            if (!s_is_maximized) {
                s_prev_saved_w = s_win_width;
                s_prev_saved_h = s_win_height;
                s_prev_saved_x = s_pos_x;
                s_prev_saved_y = s_pos_y;

                int max_h = screen_h - 24;
                s_pos_y = 12;
                gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
                overlay_set_size(s_win_width, max_h);
                s_is_maximized = TRUE;
                overlay_show_toast("Maximized height");
            } else {
                s_pos_x = s_prev_saved_x;
                s_pos_y = s_prev_saved_y;
                gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
                overlay_set_size(s_prev_saved_w, s_prev_saved_h);
                s_is_maximized = FALSE;
                overlay_show_toast("Restored size");
            }
            return TRUE;
        }

        s_active_drag = MOVE_DRAG;
        s_drag_start_root_x = (int)event->x_root;
        s_drag_start_root_y = (int)event->y_root;
        s_drag_start_win_x = s_pos_x;
        s_drag_start_win_y = s_pos_y;
        s_drag_start_win_w = s_win_width;
        s_drag_start_win_h = s_win_height;
        set_window_cursor(GDK_FLEUR);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_header_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (s_active_drag == MOVE_DRAG) {
        int dx = (int)event->x_root - s_drag_start_root_x;
        int dy = (int)event->y_root - s_drag_start_root_y;
        s_pos_x = s_drag_start_win_x + dx;
        s_pos_y = s_drag_start_win_y + dy;
        gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        if (s_xid != None) {
            Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
            XMoveWindow(xdpy, s_xid, s_pos_x, s_pos_y);
            XFlush(xdpy);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean on_header_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1 && s_active_drag == MOVE_DRAG) {
        s_active_drag = RESIZE_NONE;
        set_window_cursor((GdkCursorType)-1);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_window_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (s_active_drag != RESIZE_NONE) {
        int dx = (int)event->x_root - s_drag_start_root_x;
        int dy = (int)event->y_root - s_drag_start_root_y;

        if (s_active_drag == MOVE_DRAG) {
            s_pos_x = s_drag_start_win_x + dx;
            s_pos_y = s_drag_start_win_y + dy;
            gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
            if (s_xid != None) {
                Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
                XMoveWindow(xdpy, s_xid, s_pos_x, s_pos_y);
                XFlush(xdpy);
            }
            return TRUE;
        }

        int new_w = s_drag_start_win_w;
        int new_h = s_drag_start_win_h;
        int new_x = s_drag_start_win_x;
        int new_y = s_drag_start_win_y;

        if (s_active_drag & RESIZE_RIGHT) {
            new_w = s_drag_start_win_w + dx;
        } else if (s_active_drag & RESIZE_LEFT) {
            new_w = s_drag_start_win_w - dx;
            if (new_w < OVERLAY_MIN_WIDTH) new_w = OVERLAY_MIN_WIDTH;
            new_x = s_drag_start_win_x + (s_drag_start_win_w - new_w);
        }

        if (s_active_drag & RESIZE_BOTTOM) {
            new_h = s_drag_start_win_h + dy;
        } else if (s_active_drag & RESIZE_TOP) {
            new_h = s_drag_start_win_h - dy;
            if (new_h < OVERLAY_MIN_HEIGHT) new_h = OVERLAY_MIN_HEIGHT;
            new_y = s_drag_start_win_y + (s_drag_start_win_h - new_h);
        }

        if (new_x != s_pos_x || new_y != s_pos_y) {
            s_pos_x = new_x;
            s_pos_y = new_y;
            gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);
        }
        overlay_set_size(new_w, new_h);
        return TRUE;
    }

    int border_px = 8;
    int x = (int)event->x;
    int y = (int)event->y;
    int w = s_win_width;
    int h = s_win_height;

    DragOperation edge = RESIZE_NONE;
    if (x <= border_px) edge |= RESIZE_LEFT;
    else if (x >= w - border_px) edge |= RESIZE_RIGHT;

    if (y <= border_px) edge |= RESIZE_TOP;
    else if (y >= h - border_px) edge |= RESIZE_BOTTOM;

    GdkCursorType cursor_type = (GdkCursorType)-1;
    if ((edge & RESIZE_TOP) && (edge & RESIZE_LEFT)) cursor_type = GDK_TOP_LEFT_CORNER;
    else if ((edge & RESIZE_TOP) && (edge & RESIZE_RIGHT)) cursor_type = GDK_TOP_RIGHT_CORNER;
    else if ((edge & RESIZE_BOTTOM) && (edge & RESIZE_LEFT)) cursor_type = GDK_BOTTOM_LEFT_CORNER;
    else if ((edge & RESIZE_BOTTOM) && (edge & RESIZE_RIGHT)) cursor_type = GDK_BOTTOM_RIGHT_CORNER;
    else if (edge & RESIZE_TOP) cursor_type = GDK_TOP_SIDE;
    else if (edge & RESIZE_BOTTOM) cursor_type = GDK_BOTTOM_SIDE;
    else if (edge & RESIZE_LEFT) cursor_type = GDK_LEFT_SIDE;
    else if (edge & RESIZE_RIGHT) cursor_type = GDK_RIGHT_SIDE;

    set_window_cursor(cursor_type);
    return FALSE;
}

static gboolean on_window_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    input_notify_native_click(event);

    if (event->button == 1) {
        int border_px = 8;
        int x = (int)event->x;
        int y = (int)event->y;
        int w = s_win_width;
        int h = s_win_height;

        DragOperation edge = RESIZE_NONE;
        if (x <= border_px) edge |= RESIZE_LEFT;
        else if (x >= w - border_px) edge |= RESIZE_RIGHT;

        if (y <= border_px) edge |= RESIZE_TOP;
        else if (y >= h - border_px) edge |= RESIZE_BOTTOM;

        if (edge != RESIZE_NONE) {
            s_active_drag = edge;
            s_drag_start_root_x = (int)event->x_root;
            s_drag_start_root_y = (int)event->y_root;
            s_drag_start_win_x = s_pos_x;
            s_drag_start_win_y = s_pos_y;
            s_drag_start_win_w = s_win_width;
            s_drag_start_win_h = s_win_height;
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean on_window_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (event->button == 1 && s_active_drag != RESIZE_NONE) {
        s_active_drag = RESIZE_NONE;
        set_window_cursor((GdkCursorType)-1);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_window_leave_notify(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data) {
    (void)widget;
    (void)event;
    (void)user_data;
    if (s_active_drag == RESIZE_NONE) {
        set_window_cursor((GdkCursorType)-1);
    }
    return FALSE;
}

static gboolean on_widget_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    input_notify_native_click(event);
    return FALSE;
}

static void on_url_entry_activate(GtkEntry *entry, gpointer user_data) {
    (void)user_data;
    const char *text = gtk_entry_get_text(entry);
    if (text && text[0] != '\0') {
        browser_load_in_browser(text);
        gtk_widget_grab_focus(GTK_WIDGET(browser_get_browser_view()));
    }
}

static void on_test_entry_changed(GtkEditable *editable, gpointer user_data) {
    (void)editable;
    (void)user_data;
    update_x11_state_properties();
}

static void on_browser_uri_changed(const char *uri, gpointer user_data) {
    (void)user_data;
    if (s_url_entry && !gtk_widget_has_focus(s_url_entry)) {
        gtk_entry_set_text(GTK_ENTRY(s_url_entry), uri);
    }
    update_x11_state_properties();
}

static void on_browser_progress_changed(double progress, gpointer user_data) {
    (void)user_data;
    if (s_progress_bar) {
        if (progress >= 1.0 || progress <= 0.0) {
            gtk_widget_set_visible(s_progress_bar, FALSE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(s_progress_bar), 0.0);
        } else {
            gtk_widget_set_visible(s_progress_bar, TRUE);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(s_progress_bar), progress);
        }
    }
}

static void on_browser_state_changed(bool is_loading, bool can_back, bool can_forward, gpointer user_data) {
    (void)user_data;
    (void)can_back;
    (void)can_forward;
    if (s_reload_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_reload_stack), is_loading ? "stop" : "reload");
    }
}

void overlay_switch_app(const char *app_name) {
    if (!s_stack || !app_name) return;

    if (g_ascii_strcasecmp(app_name, "chatgpt") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "chatgpt");
        g_strlcpy(s_current_app, "chatgpt", sizeof(s_current_app));
        browser_set_active_view("chatgpt");
        xutil_log("WINDOW", "Switched view to ChatGPT app.");
    } else if (g_ascii_strcasecmp(app_name, "browser") == 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "browser");
        g_strlcpy(s_current_app, "browser", sizeof(s_current_app));
        browser_set_active_view("browser");
        xutil_log("WINDOW", "Switched view to Browser app.");
    } else {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "home");
        g_strlcpy(s_current_app, "home", sizeof(s_current_app));
        browser_set_active_view("browser");
        xutil_log("WINDOW", "Switched view to Home screen.");
    }
    overlay_update_status_ui();
}

const char *overlay_get_current_app(void) {
    return s_current_app;
}

void overlay_go_home(void) {
    overlay_switch_app("home");
}

void overlay_back(void) {
    if (strcmp(s_current_app, "chatgpt") == 0) {
        WebKitWebView *cg_view = browser_get_chatgpt_view();
        if (cg_view && webkit_web_view_can_go_back(cg_view)) {
            webkit_web_view_go_back(cg_view);
        } else {
            overlay_go_home();
        }
    } else if (strcmp(s_current_app, "browser") == 0) {
        WebKitWebView *br_view = browser_get_browser_view();
        if (br_view && webkit_web_view_can_go_back(br_view)) {
            webkit_web_view_go_back(br_view);
        } else {
            overlay_go_home();
        }
    } else {
        overlay_go_home();
    }
}

void overlay_reload(void) {
    if (strcmp(s_current_app, "chatgpt") == 0) {
        WebKitWebView *cg_view = browser_get_chatgpt_view();
        if (cg_view) {
            if (webkit_web_view_is_loading(cg_view)) webkit_web_view_stop_loading(cg_view);
            else webkit_web_view_reload(cg_view);
        }
    } else if (strcmp(s_current_app, "browser") == 0) {
        if (browser_is_loading()) browser_stop();
        else browser_reload();
    } else {
        if (s_window) gtk_widget_queue_draw(s_window);
    }
}

static void on_btn_back_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_back();
}

static void on_btn_reload_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_reload();
}

static void on_btn_home_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_go_home();
}

static void on_btn_snap_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_snap();
}

static void on_btn_ghost_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    ghost_writer_run();
}

static gboolean on_ghost_revert_btn_label(gpointer user_data) {
    (void)user_data;
    s_ghost_revert_timer = 0;
    if (s_btn_ghost_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "idle");
    }
    return G_SOURCE_REMOVE;
}

void overlay_set_ghost_button_state(GhostButtonState state) {
    if (!s_btn_ghost_stack) return;

    if (s_ghost_revert_timer > 0) {
        g_source_remove(s_ghost_revert_timer);
        s_ghost_revert_timer = 0;
    }

    switch (state) {
        case GHOST_BTN_IDLE:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "idle");
            break;
        case GHOST_BTN_TYPING:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "typing");
            break;
        case GHOST_BTN_DONE:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "done");
            s_ghost_revert_timer = g_timeout_add(1200, on_ghost_revert_btn_label, NULL);
            break;
        case GHOST_BTN_FAILED:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "failed");
            s_ghost_revert_timer = g_timeout_add(1200, on_ghost_revert_btn_label, NULL);
            break;
    }
}

static gboolean on_toast_dismiss_timeout(gpointer user_data) {
    (void)user_data;
    s_toast_timer_id = 0;
    if (s_toast_box) {
        gtk_widget_hide(s_toast_box);
    }
    return G_SOURCE_REMOVE;
}

void overlay_show_toast(const char *msg) {
    if (!s_toast_box || !s_toast_label || !msg) return;

    gtk_label_set_text(GTK_LABEL(s_toast_label), msg);
    gtk_widget_show(s_toast_box);

    if (s_toast_timer_id > 0) {
        g_source_remove(s_toast_timer_id);
    }
    s_toast_timer_id = g_timeout_add(2200, on_toast_dismiss_timeout, NULL);
}

static gboolean on_mode_pill_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)event; (void)user_data;
    input_cycle_key_mode();
    return TRUE;
}

static gboolean on_target_pill_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)event; (void)user_data;
    if (input_get_typing_target() == TYPING_TARGET_OVERLAY) {
        input_set_typing_target(TYPING_TARGET_NONE);
    } else {
        input_set_typing_target(TYPING_TARGET_OVERLAY);
    }
    return TRUE;
}


static void on_app_icon_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    const char *app = (const char *)user_data;
    overlay_switch_app(app);
}

static void on_home_search_clicked(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)widget; (void)event; (void)user_data;
    overlay_switch_app("browser");
    if (s_url_entry) {
        gtk_widget_grab_focus(s_url_entry);
    }
}

static GtkWidget *create_app_icon_widget(const char *label_text, const char *app_id) {
    GtkWidget *btn = gtk_button_new();
    gtk_widget_set_can_focus(btn, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "app-button");

    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(col, GTK_ALIGN_CENTER);

    GtkWidget *img = NULL;
    if (strcmp(app_id, "chatgpt") == 0) {
        img = create_svg_image(s_svg_chatgpt_icon, 64, 64);
    } else {
        img = create_svg_image(s_svg_browser_icon, 64, 64);
    }
    gtk_box_pack_start(GTK_BOX(col), img, FALSE, FALSE, 0);

    GtkWidget *lbl = gtk_label_new(label_text);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "app-label");
    gtk_box_pack_start(GTK_BOX(col), lbl, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(btn), col);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_app_icon_clicked), (gpointer)app_id);
    g_signal_connect(btn, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);

    return btn;
}

static GtkWidget *create_home_page(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "home-page");

    /* Top spacing */
    GtkWidget *top_spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(top_spacer, -1, 32);
    gtk_box_pack_start(GTK_BOX(box), top_spacer, FALSE, FALSE, 0);

    /* Search Bar Launcher */
    GtkWidget *search_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(search_eb), FALSE);
    gtk_widget_set_margin_start(search_eb, 20);
    gtk_widget_set_margin_end(search_eb, 20);
    gtk_widget_set_margin_bottom(search_eb, 42);

    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(search_box), "home-search-bar");

    GtkWidget *search_icon = create_svg_image(s_svg_search_icon, 16, 16);
    gtk_box_pack_start(GTK_BOX(search_box), search_icon, FALSE, FALSE, 0);

    GtkWidget *search_lbl = gtk_label_new("Search or type URL...");
    gtk_style_context_add_class(gtk_widget_get_style_context(search_lbl), "home-search-label");
    gtk_box_pack_start(GTK_BOX(search_box), search_lbl, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(search_eb), search_box);
    g_signal_connect(search_eb, "button-press-event", G_CALLBACK(on_home_search_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(box), search_eb, FALSE, FALSE, 0);

    /* Section Header */
    GtkWidget *sec_lbl = gtk_label_new("APPLICATIONS");
    gtk_style_context_add_class(gtk_widget_get_style_context(sec_lbl), "home-section-title");
    gtk_widget_set_halign(sec_lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start(sec_lbl, 24);
    gtk_widget_set_margin_bottom(sec_lbl, 16);
    gtk_box_pack_start(GTK_BOX(box), sec_lbl, FALSE, FALSE, 0);

    /* App Icons Grid */
    GtkWidget *apps_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 44);
    gtk_widget_set_halign(apps_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(apps_box, GTK_ALIGN_START);

    GtkWidget *btn_chatgpt = create_app_icon_widget("ChatGPT", "chatgpt");
    gtk_box_pack_start(GTK_BOX(apps_box), btn_chatgpt, FALSE, FALSE, 0);

    GtkWidget *btn_browser = create_app_icon_widget("Browser", "browser");
    gtk_box_pack_start(GTK_BOX(apps_box), btn_browser, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), apps_box, FALSE, FALSE, 0);

    /* Flexible bottom space */
    GtkWidget *expander = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(box), expander, TRUE, TRUE, 0);

    return box;
}

static void on_popup_close_btn_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn; (void)user_data;
    overlay_close_popup_modal();
}

void overlay_show_popup_modal(GtkWidget *popup_view) {
    if (!s_overlay || !popup_view) return;

    if (s_popup_container) {
        overlay_close_popup_modal();
    }

    s_popup_child_view = popup_view;

    s_popup_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_popup_container), "popup-modal-layer");

    /* Header bar with title and close button */
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "popup-modal-bar");

    GtkWidget *lbl = gtk_label_new("Popup / Sign In");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "popup-modal-title");
    gtk_box_pack_start(GTK_BOX(bar), lbl, FALSE, FALSE, 8);

    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(bar), spacer, TRUE, TRUE, 0);

    GtkWidget *btn_close = gtk_button_new();
    gtk_widget_set_can_focus(btn_close, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close), "popup-close-btn");
    GtkWidget *img_close = create_svg_image(s_svg_close_icon, 16, 16);
    gtk_container_add(GTK_CONTAINER(btn_close), img_close);
    g_signal_connect(btn_close, "clicked", G_CALLBACK(on_popup_close_btn_clicked), NULL);
    g_signal_connect(btn_close, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bar), btn_close, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(s_popup_container), bar, FALSE, FALSE, 0);

    /* Scrolled window for popup view */
    const AppConfig *cfg = config_get();
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(scroll), FALSE);
    gtk_widget_set_size_request(scroll, cfg->width, -1);
    gtk_container_add(GTK_CONTAINER(scroll), popup_view);
    gtk_box_pack_start(GTK_BOX(s_popup_container), scroll, TRUE, TRUE, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(s_overlay), s_popup_container);
    gtk_widget_show_all(s_popup_container);
    xutil_log("WINDOW", "Displayed popup modal layer inside phone overlay.");
}

void overlay_close_popup_modal(void) {
    if (s_popup_container) {
        gtk_widget_destroy(s_popup_container);
        s_popup_container = NULL;
        s_popup_child_view = NULL;
        browser_clear_active_popup();
        xutil_log("WINDOW", "Closed popup modal layer.");
    }
}

gboolean overlay_has_popup_modal(void) {
    return (s_popup_container != NULL);
}

GtkWidget *overlay_get_popup_child_view(void) {
    return s_popup_child_view;
}

static void on_browser_popup_created(GtkWidget *popup_view, gpointer user_data) {
    (void)user_data;
    overlay_show_popup_modal(popup_view);
}

static void on_browser_popup_closed(GtkWidget *popup_view, gpointer user_data) {
    (void)user_data;
    (void)popup_view;
    overlay_close_popup_modal();
}

static void on_browser_divert(const char *uri, gpointer user_data) {
    (void)user_data;
    if (uri && uri[0] != '\0') {
        xutil_log("WINDOW", "Diverting external URI '%s' to Browser app.", uri);
        browser_load_in_browser(uri);
        overlay_switch_app("browser");
    }
}

void overlay_execute_clipboard_action(ClipboardAction action) {
    if (!s_window) return;

    GtkWidget *focused = gtk_window_get_focus(GTK_WINDOW(s_window));
    xutil_log("WINDOW", "Executing clipboard action %d (focused widget=%s, current_app=%s)",
              action, focused ? G_OBJECT_TYPE_NAME(focused) : "NULL", s_current_app);

    if (focused && GTK_IS_EDITABLE(focused)) {
        switch (action) {
            case CLIPBOARD_ACTION_SELECT_ALL:
                gtk_editable_select_region(GTK_EDITABLE(focused), 0, -1);
                break;
            case CLIPBOARD_ACTION_COPY:
                gtk_editable_copy_clipboard(GTK_EDITABLE(focused));
                break;
            case CLIPBOARD_ACTION_PASTE:
                gtk_editable_paste_clipboard(GTK_EDITABLE(focused));
                break;
            case CLIPBOARD_ACTION_CUT:
                gtk_editable_cut_clipboard(GTK_EDITABLE(focused));
                break;
        }
    } else if (strcmp(s_current_app, "home") == 0 && s_test_entry) {
        switch (action) {
            case CLIPBOARD_ACTION_SELECT_ALL:
                gtk_editable_select_region(GTK_EDITABLE(s_test_entry), 0, -1);
                break;
            case CLIPBOARD_ACTION_COPY:
                gtk_editable_copy_clipboard(GTK_EDITABLE(s_test_entry));
                break;
            case CLIPBOARD_ACTION_PASTE:
                gtk_editable_paste_clipboard(GTK_EDITABLE(s_test_entry));
                break;
            case CLIPBOARD_ACTION_CUT:
                gtk_editable_cut_clipboard(GTK_EDITABLE(s_test_entry));
                break;
        }
    } else {
        if (action == CLIPBOARD_ACTION_PASTE) {
            browser_paste();
        } else {
            const char *cmd = NULL;
            switch (action) {
                case CLIPBOARD_ACTION_SELECT_ALL: cmd = WEBKIT_EDITING_COMMAND_SELECT_ALL; break;
                case CLIPBOARD_ACTION_COPY:       cmd = WEBKIT_EDITING_COMMAND_COPY; break;
                case CLIPBOARD_ACTION_CUT:        cmd = WEBKIT_EDITING_COMMAND_CUT; break;
                default: break;
            }
            if (cmd) {
                browser_execute_editing_command(cmd);
            }
        }
    }
    update_x11_state_properties();
}

gboolean window_init(gboolean start_visible) {
    const AppConfig *cfg = config_get();

    /* 1. Create GTK_WINDOW_POPUP -> Sets override_redirect at X11 level */
    s_window = gtk_window_new(GTK_WINDOW_POPUP);
    s_win_width = cfg->width;
    s_win_height = cfg->height;
    s_current_opacity = cfg->opacity;

    gtk_window_set_default_size(GTK_WINDOW(s_window), s_win_width, s_win_height);
    gtk_widget_set_size_request(s_window, OVERLAY_MIN_WIDTH, OVERLAY_MIN_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(s_window), TRUE);
    gtk_widget_set_opacity(s_window, s_current_opacity);

    GdkScreen *screen = gtk_widget_get_screen(s_window);
    int screen_w = 1920, screen_h = 1080;
    get_monitor_bounds(&screen_w, &screen_h);

    /* Enforce size hints on GTK window (relaxed for full resize) */
    GdkGeometry hints;
    memset(&hints, 0, sizeof(hints));
    hints.min_width = OVERLAY_MIN_WIDTH;
    hints.max_width = screen_w;
    hints.min_height = OVERLAY_MIN_HEIGHT;
    hints.max_height = screen_h;
    gtk_window_set_geometry_hints(GTK_WINDOW(s_window), s_window, &hints,
                                  GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);

    /* Connect size-allocate guard */
    g_signal_connect(s_window, "size-allocate", G_CALLBACK(on_window_size_allocate), NULL);

    /* 2. Configure RGBA visual for transparent rounded corners */
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(s_window, visual);
        xutil_log("WINDOW", "RGBA visual supported and compositor active. Transparency enabled.");
    } else {
        xutil_log("WINDOW", "RGBA visual or compositor not available. Falling back to opaque.");
    }
    gtk_widget_set_app_paintable(s_window, TRUE);
    g_signal_connect(s_window, "draw", G_CALLBACK(on_window_draw), NULL);

    /* 3. Non-focus stealing policy & taskbar/pager hints */
    gtk_window_set_accept_focus(GTK_WINDOW(s_window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(s_window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(s_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(s_window), TRUE);

    /* 4. Position window according to configuration */
    calculate_monitor_position(&s_pos_x, &s_pos_y);
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);

    /* Apply CSS styles */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, s_phone_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* 5. Construct Phone UI Layout */
    s_root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(s_root_box, s_win_width, s_win_height);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_root_box), "phone-root");
    gtk_container_add(GTK_CONTAINER(s_window), s_root_box);

    /* --- Status Bar --- */
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(status_bar), "status-bar");
    gtk_box_pack_start(GTK_BOX(s_root_box), status_bar, FALSE, FALSE, 0);

    /* Left: Opacity Control Pill */
    GtkWidget *opacity_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    gtk_style_context_add_class(gtk_widget_get_style_context(opacity_box), "pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(opacity_box), "pill-opacity");

    GtkWidget *lbl_op_dec = gtk_label_new("－");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_op_dec), "opacity-btn");
    GtkWidget *eb_op_dec = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb_op_dec), FALSE);
    gtk_widget_set_tooltip_text(eb_op_dec, "Decrease Opacity (-10%)");
    gtk_container_add(GTK_CONTAINER(eb_op_dec), lbl_op_dec);
    g_signal_connect(eb_op_dec, "button-press-event", G_CALLBACK(on_opacity_dec_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(opacity_box), eb_op_dec, FALSE, FALSE, 0);

    char op_str[16];
    snprintf(op_str, sizeof(op_str), "%d%%", (int)(s_current_opacity * 100.0 + 0.5));
    s_opacity_label = gtk_label_new(op_str);
    gtk_widget_set_tooltip_text(s_opacity_label, "Window Opacity");
    gtk_box_pack_start(GTK_BOX(opacity_box), s_opacity_label, FALSE, FALSE, 2);

    GtkWidget *lbl_op_inc = gtk_label_new("＋");
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl_op_inc), "opacity-btn");
    GtkWidget *eb_op_inc = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb_op_inc), FALSE);
    gtk_widget_set_tooltip_text(eb_op_inc, "Increase Opacity (+10%)");
    gtk_container_add(GTK_CONTAINER(eb_op_inc), lbl_op_inc);
    g_signal_connect(eb_op_inc, "button-press-event", G_CALLBACK(on_opacity_inc_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(opacity_box), eb_op_inc, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(status_bar), opacity_box, FALSE, FALSE, 0);

    /* Center: Hardware Camera Island (Draggable header) */
    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(status_bar), center_box, TRUE, TRUE, 0);

    GtkWidget *island = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(island), "camera-island");
    gtk_widget_set_halign(island, GTK_ALIGN_CENTER);

    GtkWidget *lens = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(lens), "camera-lens");
    gtk_box_pack_start(GTK_BOX(island), lens, FALSE, FALSE, 0);

    GtkWidget *speaker = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(speaker), "camera-speaker");
    gtk_box_pack_start(GTK_BOX(island), speaker, FALSE, FALSE, 0);

    GtkWidget *header_drag_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(header_drag_eb), FALSE);
    gtk_widget_set_tooltip_text(header_drag_eb, "Drag to move | Double-click to expand");
    gtk_container_add(GTK_CONTAINER(header_drag_eb), island);
    g_signal_connect(header_drag_eb, "button-press-event", G_CALLBACK(on_header_button_press), NULL);
    g_signal_connect(header_drag_eb, "motion-notify-event", G_CALLBACK(on_header_motion_notify), NULL);
    g_signal_connect(header_drag_eb, "button-release-event", G_CALLBACK(on_header_button_release), NULL);

    gtk_box_pack_start(GTK_BOX(center_box), header_drag_eb, TRUE, TRUE, 0);

    /* Right: Mode & Target Status Pills */
    /* Grab Alert Pill */
    s_grab_pill = gtk_label_new("BLOCKED");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_grab_pill), "pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_grab_pill), "pill-grab-alert");
    gtk_box_pack_start(GTK_BOX(status_bar), s_grab_pill, FALSE, FALSE, 2);
    gtk_widget_set_no_show_all(s_grab_pill, TRUE);
    gtk_widget_hide(s_grab_pill);

    /* Mode Pill (clickable) */
    GtkWidget *mode_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(mode_eb), FALSE);
    s_mode_pill = gtk_label_new("MIRROR");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_mode_pill), "pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_mode_pill), "pill-mirror");
    gtk_container_add(GTK_CONTAINER(mode_eb), s_mode_pill);
    g_signal_connect(mode_eb, "button-press-event", G_CALLBACK(on_mode_pill_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(status_bar), mode_eb, FALSE, FALSE, 2);

    /* Target Pill (clickable) */
    GtkWidget *target_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(target_eb), FALSE);
    s_target_pill = gtk_label_new("STANDBY");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_target_pill), "pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_target_pill), "pill-target-off");
    gtk_container_add(GTK_CONTAINER(target_eb), s_target_pill);
    g_signal_connect(target_eb, "button-press-event", G_CALLBACK(on_target_pill_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(status_bar), target_eb, FALSE, FALSE, 2);

    /* --- Central Viewport: GtkOverlay wrapping GtkStack --- */
    s_overlay = gtk_overlay_new();
    gtk_box_pack_start(GTK_BOX(s_root_box), s_overlay, TRUE, TRUE, 0);

    s_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(s_stack), 180);
    gtk_container_add(GTK_CONTAINER(s_overlay), s_stack);

    /* Toast Notification Container (Phase 7) */
    s_toast_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(s_toast_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(s_toast_box, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(s_toast_box, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_toast_box), "toast-box");
    gtk_widget_set_no_show_all(s_toast_box, TRUE);

    s_toast_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_toast_label), "toast-label");
    gtk_box_pack_start(GTK_BOX(s_toast_box), s_toast_label, TRUE, TRUE, 0);
    gtk_overlay_add_overlay(GTK_OVERLAY(s_overlay), s_toast_box);

    /* 1. Page: Home Screen */
    GtkWidget *home_page = create_home_page();
    gtk_stack_add_named(GTK_STACK(s_stack), home_page, "home");

    /* 2. Page: ChatGPT App (Full-screen WebKit inside phone, no URL bar) */
    GtkWidget *cg_widget = browser_get_chatgpt_widget();
    if (cg_widget) {
        gtk_widget_add_events(cg_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
        g_signal_connect(cg_widget, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
        gtk_stack_add_named(GTK_STACK(s_stack), cg_widget, "chatgpt");
    }

    /* 3. Page: Browser App (Header Box + WebKit View) */
    GtkWidget *browser_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    s_header_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_header_box), "header-box");
    gtk_box_pack_start(GTK_BOX(browser_box), s_header_box, FALSE, FALSE, 0);

    GtkWidget *url_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(url_row), "url-bar-container");

    GtkWidget *url_icon = create_svg_image(s_svg_search_icon, 14, 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(url_icon), "url-leading-icon");
    gtk_box_pack_start(GTK_BOX(url_row), url_icon, FALSE, FALSE, 2);

    s_url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_url_entry), "Search or enter website...");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_url_entry), "url-entry");
    g_signal_connect(s_url_entry, "activate", G_CALLBACK(on_url_entry_activate), NULL);
    g_signal_connect(s_url_entry, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(url_row), s_url_entry, TRUE, TRUE, 0);

    /* Compact Reload Button in Browser URL Row */
    s_btn_reload = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_reload, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_reload), "url-action-btn");
    gtk_widget_set_tooltip_text(s_btn_reload, "Reload page");

    s_reload_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_reload_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(s_reload_stack), 100);

    GtkWidget *img_reload = create_svg_image(s_svg_reload_icon, 16, 16);
    GtkWidget *img_stop = create_svg_image(s_svg_stop_icon, 16, 16);
    gtk_stack_add_named(GTK_STACK(s_reload_stack), img_reload, "reload");
    gtk_stack_add_named(GTK_STACK(s_reload_stack), img_stop, "stop");
    gtk_container_add(GTK_CONTAINER(s_btn_reload), s_reload_stack);
    gtk_stack_set_visible_child_name(GTK_STACK(s_reload_stack), "reload");

    g_signal_connect(s_btn_reload, "clicked", G_CALLBACK(on_btn_reload_clicked), NULL);
    g_signal_connect(s_btn_reload, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(url_row), s_btn_reload, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(s_header_box), url_row, FALSE, FALSE, 0);

    s_progress_bar = gtk_progress_bar_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(s_progress_bar), "load-progress");
    gtk_widget_set_no_show_all(s_progress_bar, TRUE);
    gtk_widget_hide(s_progress_bar);
    gtk_box_pack_start(GTK_BOX(s_header_box), s_progress_bar, FALSE, FALSE, 0);

    GtkWidget *br_widget = browser_get_browser_widget();
    if (br_widget) {
        gtk_widget_add_events(br_widget, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
        g_signal_connect(br_widget, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
        gtk_box_pack_start(GTK_BOX(browser_box), br_widget, TRUE, TRUE, 0);
    }
    gtk_stack_add_named(GTK_STACK(s_stack), browser_box, "browser");

    /* Default view is HOME SCREEN */
    gtk_stack_set_visible_child_name(GTK_STACK(s_stack), "home");
    g_strlcpy(s_current_app, "home", sizeof(s_current_app));

    /* Connect Browser Callbacks */
    browser_set_uri_callback(on_browser_uri_changed, NULL);
    browser_set_progress_callback(on_browser_progress_changed, NULL);
    browser_set_state_callback(on_browser_state_changed, NULL);
    browser_set_popup_callbacks(on_browser_popup_created, on_browser_popup_closed, NULL);
    browser_set_divert_callback(on_browser_divert, NULL);

    /* --- Persistent Test Input Box (Visible across all views) --- */
    GtkWidget *test_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(test_box), "test-input-box");

    s_test_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_test_entry), "Type text, then tap Ghost Writer...");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_test_entry), "test-input-entry");
    g_signal_connect(s_test_entry, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    g_signal_connect(s_test_entry, "changed", G_CALLBACK(on_test_entry_changed), NULL);
    gtk_box_pack_start(GTK_BOX(test_box), s_test_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(s_root_box), test_box, FALSE, FALSE, 0);

    /* --- Bottom Navigation Bar --- */
    GtkWidget *bottom_nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(bottom_nav), "bottom-nav");
    gtk_box_pack_start(GTK_BOX(s_root_box), bottom_nav, FALSE, FALSE, 0);

    s_btn_back = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_back, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_back), "nav-button");
    gtk_widget_set_sensitive(s_btn_back, FALSE);
    GtkWidget *img_back = create_svg_image(s_svg_back_icon, 20, 20);
    gtk_container_add(GTK_CONTAINER(s_btn_back), img_back);
    g_signal_connect(s_btn_back, "clicked", G_CALLBACK(on_btn_back_clicked), NULL);
    g_signal_connect(s_btn_back, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_back, TRUE, TRUE, 0);

    s_btn_home = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_home, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_home), "nav-button");
    GtkWidget *img_home = create_svg_image(s_svg_home_icon, 20, 20);
    gtk_container_add(GTK_CONTAINER(s_btn_home), img_home);
    g_signal_connect(s_btn_home, "clicked", G_CALLBACK(on_btn_home_clicked), NULL);
    g_signal_connect(s_btn_home, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_home, TRUE, TRUE, 0);

    /* Ghost Writer Button (Phase 7) */
    s_btn_ghost = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_ghost, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_ghost), "nav-button");
    gtk_widget_set_tooltip_text(s_btn_ghost, "Ghost Writer (type into active app)");

    s_btn_ghost_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_btn_ghost_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(s_btn_ghost_stack), 100);

    GtkWidget *img_ghost_idle = create_svg_image(s_svg_ghost_idle, 20, 20);
    GtkWidget *img_ghost_typing = create_svg_image(s_svg_ghost_typing, 20, 20);
    GtkWidget *img_ghost_done = create_svg_image(s_svg_check_icon, 20, 20);
    GtkWidget *img_ghost_failed = create_svg_image(s_svg_failed_icon, 20, 20);

    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), img_ghost_idle, "idle");
    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), img_ghost_typing, "typing");
    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), img_ghost_done, "done");
    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), img_ghost_failed, "failed");

    gtk_container_add(GTK_CONTAINER(s_btn_ghost), s_btn_ghost_stack);
    gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "idle");

    g_signal_connect(s_btn_ghost, "clicked", G_CALLBACK(on_btn_ghost_clicked), NULL);
    g_signal_connect(s_btn_ghost, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_ghost, TRUE, TRUE, 0);

    /* Snap Button (Phase 6) */
    s_btn_snap = gtk_button_new();
    gtk_widget_set_can_focus(s_btn_snap, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_snap), "nav-button");
    gtk_widget_set_tooltip_text(s_btn_snap, "Capture Screen (Snap)");
    gtk_widget_set_sensitive(s_btn_snap, TRUE);

    s_snap_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_snap_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(s_snap_stack), 100);

    GtkWidget *img_snap_cam = create_svg_image(s_svg_camera_icon, 20, 20);
    GtkWidget *img_snap_running = create_svg_image(s_svg_snap_running, 20, 20);
    GtkWidget *img_snap_done = create_svg_image(s_svg_check_icon, 20, 20);

    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_cam, "camera");
    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_running, "snapping");
    gtk_stack_add_named(GTK_STACK(s_snap_stack), img_snap_done, "done");

    gtk_container_add(GTK_CONTAINER(s_btn_snap), s_snap_stack);
    gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "camera");

    g_signal_connect(s_btn_snap, "clicked", G_CALLBACK(on_btn_snap_clicked), NULL);
    g_signal_connect(s_btn_snap, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_snap, TRUE, TRUE, 0);

    /* Bottom Home Indicator Row with Corner Resize Grip */
    GtkWidget *home_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(home_row), "home-pill-box");

    GtkWidget *home_left_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(home_left_spacer, 24, -1);
    gtk_box_pack_start(GTK_BOX(home_row), home_left_spacer, FALSE, FALSE, 0);

    GtkWidget *home_pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(home_pill), "home-pill");
    gtk_widget_set_halign(home_pill, GTK_ALIGN_CENTER);

    GtkWidget *home_pill_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(home_pill_eb), FALSE);
    gtk_container_add(GTK_CONTAINER(home_pill_eb), home_pill);
    g_signal_connect(home_pill_eb, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    g_signal_connect_swapped(home_pill_eb, "button-press-event", G_CALLBACK(overlay_go_home), NULL);
    gtk_box_pack_start(GTK_BOX(home_row), home_pill_eb, TRUE, TRUE, 0);

    /* Corner Resize Grip at bottom right */
    GtkWidget *resize_grip = create_svg_image(s_svg_resize_grip, 14, 14);
    GtkWidget *resize_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(resize_eb), FALSE);
    gtk_widget_set_tooltip_text(resize_eb, "Drag to Resize");
    gtk_style_context_add_class(gtk_widget_get_style_context(resize_eb), "resize-grip");
    gtk_container_add(GTK_CONTAINER(resize_eb), resize_grip);
    g_signal_connect(resize_eb, "button-press-event", G_CALLBACK(on_resize_grip_press), NULL);
    g_signal_connect(resize_eb, "button-release-event", G_CALLBACK(on_resize_grip_release), NULL);
    g_signal_connect(resize_eb, "motion-notify-event", G_CALLBACK(on_resize_grip_motion), NULL);
    gtk_box_pack_end(GTK_BOX(home_row), resize_eb, FALSE, FALSE, 6);

    gtk_box_pack_start(GTK_BOX(s_root_box), home_row, FALSE, FALSE, 0);

    /* Click and motion listeners on main window for edge resizing */
    gtk_widget_add_events(s_window, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                    GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(s_window, "button-press-event", G_CALLBACK(on_window_button_press), NULL);
    g_signal_connect(s_window, "button-release-event", G_CALLBACK(on_window_button_release), NULL);
    g_signal_connect(s_window, "motion-notify-event", G_CALLBACK(on_window_motion_notify), NULL);
    g_signal_connect(s_window, "leave-notify-event", G_CALLBACK(on_window_leave_notify), NULL);

    /* Realize widget to obtain underlying XID */
    gtk_widget_realize(s_window);
    GdkWindow *gdk_win = gtk_widget_get_window(s_window);
    s_xid = gdk_x11_window_get_xid(gdk_win);

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);

    /* Enforce size hints at X11 level (relaxed for full resize) */
    XSizeHints *sh = XAllocSizeHints();
    if (sh) {
        sh->flags = PMinSize | PMaxSize;
        sh->min_width = OVERLAY_MIN_WIDTH;
        sh->max_width = screen_w;
        sh->min_height = OVERLAY_MIN_HEIGHT;
        sh->max_height = screen_h;
        XSetWMNormalHints(xdpy, s_xid, sh);
        XFree(sh);
    }

    /* Ensure X11 input hint is FALSE */
    XWMHints *wm_hints = XGetWMHints(xdpy, s_xid);
    if (!wm_hints) wm_hints = XAllocWMHints();
    if (wm_hints) {
        wm_hints->flags |= InputHint;
        wm_hints->input = False;
        XSetWMHints(xdpy, s_xid, wm_hints);
        XFree(wm_hints);
    }

    Atom net_wm_name = XInternAtom(xdpy, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(xdpy, "UTF8_STRING", False);
    XChangeProperty(xdpy, s_xid, net_wm_name, utf8_string, 8, PropModeReplace,
                    (unsigned char *)"PhoneOverlay", 12);
    XStoreName(xdpy, s_xid, "PhoneOverlay");

    update_x11_state_properties();

    xutil_log("WINDOW", "Created override-redirect overlay XID 0x%lx (%lu)",
              (unsigned long)s_xid, (unsigned long)s_xid);

    /* Refresh status & clock every second */
    s_clock_timer_id = g_timeout_add_seconds(1, on_clock_tick, NULL);

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
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
    XMapRaised(xdpy, s_xid);
    XFlush(xdpy);

    s_visible = TRUE;
    xutil_log("WINDOW", "Overlay shown (mapped raised at (%d, %d))", s_pos_x, s_pos_y);

    browser_resume();
    overlay_update_status_ui();
    raise_start();
}

void overlay_hide(void) {
    if (!s_window) return;

    raise_stop();
    input_release_grab();
    input_notify_overlay_hidden();
    browser_pause();

    gtk_widget_hide(s_window);
    s_visible = FALSE;
    xutil_log("WINDOW", "Overlay hidden");
}

void overlay_reposition(void) {
    if (!s_window || s_xid == None) return;

    calculate_monitor_position(&s_pos_x, &s_pos_y);

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
    XFlush(xdpy);

    overlay_update_status_ui();
    xutil_log("WINDOW", "Overlay repositioned to (%d, %d)", s_pos_x, s_pos_y);
}

void overlay_apply_config(void) {
    if (!s_window || s_xid == None) return;

    const AppConfig *cfg = config_get();
    calculate_monitor_position(&s_pos_x, &s_pos_y);

    s_win_width = cfg->width;
    s_win_height = cfg->height;
    s_current_opacity = cfg->opacity;

    gtk_window_resize(GTK_WINDOW(s_window), s_win_width, s_win_height);
    if (s_root_box) {
        gtk_widget_set_size_request(s_root_box, s_win_width, s_win_height);
    }
    gtk_widget_set_opacity(s_window, s_current_opacity);
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);

    if (s_opacity_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d%%", (int)(s_current_opacity * 100.0 + 0.5));
        gtk_label_set_text(GTK_LABEL(s_opacity_label), buf);
    }

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, s_win_width, s_win_height);
    XFlush(xdpy);

    overlay_update_status_ui();
    xutil_log("WINDOW", "Applied config: %dx%d, opacity=%.2f, pos=(%d,%d)",
              s_win_width, s_win_height, s_current_opacity, s_pos_x, s_pos_y);
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
    if (w) *w = s_win_width;
    if (h) *h = s_win_height;
}

void overlay_queue_draw(void) {
    if (s_window) {
        gtk_widget_queue_draw(s_window);
    }
    overlay_update_status_ui();
}

void overlay_set_ghost(gboolean ghost) {
    if (s_xid == None) return;
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    if (ghost) {
        XShapeCombineRectangles(xdpy, s_xid, ShapeInput, 0, 0, NULL, 0, ShapeSet, YXBanded);
        xutil_log("WINDOW", "Applied ShapeInput 0 rects: overlay is click-through (GHOST).");
    } else {
        XShapeCombineMask(xdpy, s_xid, ShapeInput, 0, 0, None, ShapeSet);
        xutil_log("WINDOW", "Restored default ShapeInput mask: overlay is SOLID.");
    }
    XFlush(xdpy);
    overlay_update_status_ui();
}

GtkWidget *overlay_get_window_widget(void) {
    return s_window;
}

GtkWidget *overlay_get_entry_widget(void) {
    if (strcmp(s_current_app, "browser") == 0) {
        if (s_url_entry && gtk_widget_has_focus(s_url_entry)) {
            return s_url_entry;
        }
    }
    return s_test_entry;
}

GtkWidget *overlay_get_test_entry(void) {
    return s_test_entry;
}

GtkWidget *overlay_get_url_entry(void) {
    return s_url_entry;
}

const char *overlay_get_test_entry_text(void) {
    if (!s_test_entry) return "";
    return gtk_entry_get_text(GTK_ENTRY(s_test_entry));
}

const char *overlay_get_entry_text(void) {
    if (strcmp(s_current_app, "browser") == 0) {
        if (s_url_entry && gtk_widget_has_focus(s_url_entry)) {
            return gtk_entry_get_text(GTK_ENTRY(s_url_entry));
        }
        if (s_test_entry && gtk_widget_has_focus(s_test_entry)) {
            return gtk_entry_get_text(GTK_ENTRY(s_test_entry));
        }
        if (s_url_entry) {
            return gtk_entry_get_text(GTK_ENTRY(s_url_entry));
        }
    }
    if (!s_test_entry) return "";
    return gtk_entry_get_text(GTK_ENTRY(s_test_entry));
}

void overlay_clear_entry_text(void) {
    if (s_test_entry) {
        gtk_entry_set_text(GTK_ENTRY(s_test_entry), "");
        overlay_update_status_ui();
    }
}

void overlay_set_entry_text(const char *text) {
    if (s_test_entry && text) {
        gtk_entry_set_text(GTK_ENTRY(s_test_entry), text);
        overlay_update_status_ui();
    }
}

static void save_and_prune_snap(GdkPixbuf *pb, const AppConfig *cfg) {
    const char *pics_dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    char *dir_buf = NULL;
    if (pics_dir && pics_dir[0] != '\0') {
        dir_buf = g_build_filename(pics_dir, "phoneoverlay", NULL);
    } else {
        dir_buf = g_build_filename(g_get_home_dir(), "Pictures", "phoneoverlay", NULL);
    }

    if (g_mkdir_with_parents(dir_buf, 0755) != 0) {
        xutil_log("WINDOW", "Failed to create directory %s: %s", dir_buf, g_strerror(errno));
        g_free(dir_buf);
        return;
    }

    GDateTime *now = g_date_time_new_now_local();
    char *dt_str = g_date_time_format(now, "%Y%m%d-%H%M%S");
    g_date_time_unref(now);

    char *base_name = g_strdup_printf("snap-%s.png", dt_str ? dt_str : "unknown");
    char *filepath = g_build_filename(dir_buf, base_name, NULL);
    g_free(base_name);

    int suffix = 1;
    while (g_file_test(filepath, G_FILE_TEST_EXISTS)) {
        g_free(filepath);
        base_name = g_strdup_printf("snap-%s-%d.png", dt_str ? dt_str : "unknown", suffix++);
        filepath = g_build_filename(dir_buf, base_name, NULL);
        g_free(base_name);
    }
    g_free(dt_str);

    GError *err = NULL;
    if (gdk_pixbuf_save(pb, filepath, "png", &err, "compression", "9", NULL)) {
        xutil_log("WINDOW", "Saved screenshot to %s", filepath);
        unlink("/tmp/phoneoverlay_latest_snap.png");
        if (symlink(filepath, "/tmp/phoneoverlay_latest_snap.png") != 0) {
            gchar *data = NULL;
            gsize len = 0;
            if (g_file_get_contents(filepath, &data, &len, NULL)) {
                g_file_set_contents("/tmp/phoneoverlay_latest_snap.png", data, len, NULL);
                g_free(data);
            }
        }
    } else {
        xutil_log("WINDOW", "Failed to save screenshot %s: %s", filepath, err ? err->message : "unknown");
        if (err) g_error_free(err);
    }
    g_free(filepath);

    /* Prune old files exceeding snap_keep_count */
    int keep_count = (cfg->snap_keep_count > 0) ? cfg->snap_keep_count : 20;
    GDir *dir = g_dir_open(dir_buf, 0, NULL);
    if (dir) {
        GPtrArray *files = g_ptr_array_new_with_free_func(g_free);
        const char *name = NULL;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (g_str_has_prefix(name, "snap-") && g_str_has_suffix(name, ".png")) {
                g_ptr_array_add(files, g_strdup(name));
            }
        }
        g_dir_close(dir);

        g_ptr_array_sort(files, (GCompareFunc)g_strcmp0);

        if ((int)files->len > keep_count) {
            int to_delete = (int)files->len - keep_count;
            for (int i = 0; i < to_delete; i++) {
                char *old_path = g_build_filename(dir_buf, (const char *)g_ptr_array_index(files, i), NULL);
                g_unlink(old_path);
                xutil_log("WINDOW", "Pruned old screenshot: %s", old_path);
                g_free(old_path);
            }
        }
        g_ptr_array_unref(files);
    }
    g_free(dir_buf);
}

void overlay_capture_begin(void) {
    if (s_visible && s_xid != None) {
        raise_stop();
        Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XUnmapWindow(xdpy, s_xid);
        XSync(xdpy, False);
        xutil_log("WINDOW", "overlay_capture_begin: unmapped overlay XID 0x%lx and paused restack watcher", (unsigned long)s_xid);
    }
}

void overlay_capture_end(void) {
    if (s_was_visible_during_snap && s_xid != None) {
        Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XMapRaised(xdpy, s_xid);
        XSync(xdpy, False);
        raise_start();
        xutil_log("WINDOW", "overlay_capture_end: remapped overlay XID 0x%lx and resumed restack watcher", (unsigned long)s_xid);
    }
}

static gboolean on_snap_revert_btn_label(gpointer user_data) {
    (void)user_data;
    if (s_snap_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "camera");
    }
    s_snap_revert_timer = 0;
    return G_SOURCE_REMOVE;
}

static gboolean on_snap_timeout(gpointer user_data) {
    (void)user_data;
    const AppConfig *cfg = config_get();

    int rx = 0, ry = 0, rw = 0, rh = 0;
    GdkWindow *root = gdk_get_default_root_window();

    if (g_ascii_strcasecmp(cfg->snap_scope, "all") == 0) {
        rx = 0;
        ry = 0;
        rw = gdk_window_get_width(root);
        rh = gdk_window_get_height(root);
    } else {
        GdkDisplay *display = gdk_display_get_default();
        GdkMonitor *mon = gdk_display_get_primary_monitor(display);
        if (!mon && s_window && gtk_widget_get_window(s_window)) {
            mon = gdk_display_get_monitor_at_window(display, gtk_widget_get_window(s_window));
        }
        if (!mon) {
            mon = gdk_display_get_monitor(display, 0);
        }
        if (mon) {
            GdkRectangle geom;
            gdk_monitor_get_geometry(mon, &geom);
            rx = geom.x;
            ry = geom.y;
            rw = geom.width;
            rh = geom.height;
        } else {
            rx = 0;
            ry = 0;
            rw = gdk_window_get_width(root);
            rh = gdk_window_get_height(root);
        }
    }

    /* Capture screenshot from root window */
    GdkPixbuf *pb = gdk_pixbuf_get_from_window(root, rx, ry, rw, rh);

    /* Immediately restore overlay */
    overlay_capture_end();

    if (pb) {
        xutil_log("WINDOW", "Captured snap: %dx%d at (%d, %d)", rw, rh, rx, ry);

        /* Set to X11 CLIPBOARD */
        GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_image(clip, pb);
        gtk_clipboard_store(clip);

        /* Save to disk if enabled */
        if (cfg->snap_save) {
            save_and_prune_snap(pb, cfg);
        }

        g_object_unref(pb);

        if (s_snap_stack) {
            gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "done");
            if (s_snap_revert_timer > 0) {
                g_source_remove(s_snap_revert_timer);
            }
            s_snap_revert_timer = g_timeout_add(1500, on_snap_revert_btn_label, NULL);
        }
    } else {
        xutil_log("WINDOW", "Failed to capture screen pixbuf!");
        if (s_snap_stack) {
            gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "camera");
        }
    }

    s_is_snapping = FALSE;
    return G_SOURCE_REMOVE;
}

void overlay_snap(void) {
    if (s_is_snapping) {
        xutil_log("WINDOW", "overlay_snap: capture already in progress; debounced.");
        return;
    }

    s_is_snapping = TRUE;
    const AppConfig *cfg = config_get();

    if (s_snap_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_snap_stack), "snapping");
    }

    s_was_visible_during_snap = s_visible;

    if (s_was_visible_during_snap) {
        overlay_capture_begin();
        int delay = cfg->snap_delay_ms;
        if (delay < 60) delay = 60;
        g_timeout_add(delay, on_snap_timeout, NULL);
    } else {
        g_idle_add(on_snap_timeout, NULL);
    }
}

void window_cleanup(void) {
    if (s_clock_timer_id > 0) {
        g_source_remove(s_clock_timer_id);
        s_clock_timer_id = 0;
    }
    if (s_snap_revert_timer > 0) {
        g_source_remove(s_snap_revert_timer);
        s_snap_revert_timer = 0;
    }
    if (s_ghost_revert_timer > 0) {
        g_source_remove(s_ghost_revert_timer);
        s_ghost_revert_timer = 0;
    }
    if (s_toast_timer_id > 0) {
        g_source_remove(s_toast_timer_id);
        s_toast_timer_id = 0;
    }
    overlay_close_popup_modal();
    if (s_window) {
        gtk_widget_destroy(s_window);
        s_window = NULL;
    }
    s_xid = None;
    s_visible = FALSE;
    xutil_log("WINDOW", "Window cleaned up.");
}
