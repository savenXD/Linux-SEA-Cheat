#include "window.h"
#include "browser.h"
#include "config.h"
#include "input.h"
#include "raise.h"
#include "xutil.h"

#include <gdk/gdkx.h>
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

static GtkWidget *s_clock_label = NULL;
static GtkWidget *s_home_clock_label = NULL;
static GtkWidget *s_home_date_label = NULL;

static GtkWidget *s_mode_pill = NULL;
static GtkWidget *s_target_pill = NULL;
static GtkWidget *s_grab_pill = NULL;

static GtkWidget *s_btn_back = NULL;
static GtkWidget *s_btn_reload = NULL;
static GtkWidget *s_btn_home = NULL;
static GtkWidget *s_btn_ghost = NULL;
static GtkWidget *s_btn_ghost_stack = NULL;
static GtkWidget *s_ghost_icon_area = NULL;
static GtkWidget *s_btn_ghost_label = NULL;
static guint      s_ghost_revert_timer = 0;
static GtkWidget *s_btn_snap = NULL;

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
static char       s_current_app[32] = "home";

static const char *s_phone_css =
    "* {"
    "  -GtkScrollbar-has-backward-stepper: 0;"
    "  -GtkScrollbar-has-forward-stepper: 0;"
    "}"
    ".phone-root {"
    "  background-color: #12131c;"
    "  border: 2px solid #3c425c;"
    "  border-radius: 28px;"
    "  padding: 0px;"
    "}"
    ".status-bar {"
    "  padding: 6px 14px 4px 14px;"
    "  background: transparent;"
    "}"
    ".status-clock {"
    "  font-size: 11px;"
    "  font-weight: bold;"
    "  color: #e2e8f0;"
    "  font-family: sans-serif;"
    "}"
    ".status-glyphs {"
    "  font-size: 10px;"
    "  color: #94a3b8;"
    "  margin-left: 6px;"
    "}"
    ".pill {"
    "  font-size: 9.5px;"
    "  font-weight: bold;"
    "  padding: 2px 7px;"
    "  border-radius: 5px;"
    "  border: none;"
    "}"
    ".pill-mirror {"
    "  background-color: #0284c7;"
    "  color: #ffffff;"
    "}"
    ".pill-excl {"
    "  background-color: #ea580c;"
    "  color: #ffffff;"
    "}"
    ".pill-ghost {"
    "  background-color: #7e22ce;"
    "  color: #ffffff;"
    "}"
    ".pill-target-on {"
    "  background-color: #16a34a;"
    "  color: #ffffff;"
    "}"
    ".pill-target-off {"
    "  background-color: #334155;"
    "  color: #94a3b8;"
    "}"
    ".pill-grab-alert {"
    "  background-color: #dc2626;"
    "  color: #ffffff;"
    "}"
    ".header-box {"
    "  padding: 2px 12px 4px 12px;"
    "  background-color: #12131c;"
    "}"
    ".url-entry {"
    "  background-color: rgba(30, 34, 52, 0.95);"
    "  color: #f1f5f9;"
    "  border: 1px solid #434a6d;"
    "  border-radius: 14px;"
    "  padding: 4px 12px;"
    "  font-size: 12px;"
    "  caret-color: #38bdf8;"
    "}"
    ".url-entry:focus {"
    "  background-color: rgba(38, 44, 68, 0.98);"
    "  border-color: #38bdf8;"
    "}"
    ".load-progress {"
    "  min-height: 2px;"
    "  padding: 0;"
    "  margin: 0 12px;"
    "}"
    ".load-progress trough {"
    "  min-height: 2px;"
    "  background-color: transparent;"
    "  border: none;"
    "}"
    ".load-progress progress {"
    "  min-height: 2px;"
    "  background-color: #38bdf8;"
    "  border: none;"
    "}"
    ".home-page {"
    "  background: radial-gradient(circle at 50% 30%, #1e293b 0%, #0b0f19 80%);"
    "}"
    ".home-big-clock {"
    "  font-size: 38px;"
    "  font-weight: 300;"
    "  color: #f8fafc;"
    "  font-family: sans-serif;"
    "}"
    ".home-date {"
    "  font-size: 12px;"
    "  color: #94a3b8;"
    "  font-family: sans-serif;"
    "}"
    ".app-button {"
    "  background: transparent;"
    "  border: none;"
    "  padding: 4px;"
    "}"
    ".app-button:hover {"
    "  background: transparent;"
    "}"
    ".app-label {"
    "  font-size: 11px;"
    "  font-weight: 600;"
    "  color: #e2e8f0;"
    "  margin-top: 6px;"
    "}"
    ".test-input-box {"
    "  padding: 3px 12px 3px 12px;"
    "  background-color: rgba(18, 19, 28, 0.98);"
    "}"
    ".test-input-entry {"
    "  background-color: rgba(30, 34, 52, 0.95);"
    "  color: #f1f5f9;"
    "  border: 1px solid #434a6d;"
    "  border-radius: 8px;"
    "  padding: 4px 10px;"
    "  font-size: 11px;"
    "  caret-color: #38bdf8;"
    "}"
    ".test-input-entry:focus {"
    "  background-color: rgba(38, 44, 68, 0.98);"
    "  border-color: #38bdf8;"
    "}"
    ".bottom-nav {"
    "  padding: 4px 12px 2px 12px;"
    "  background-color: rgba(18, 19, 28, 0.98);"
    "  border-top: 1px solid #23273a;"
    "}"
    ".nav-button {"
    "  background: transparent;"
    "  border: none;"
    "  color: #94a3b8;"
    "  font-size: 16px;"
    "  padding: 3px 10px;"
    "  border-radius: 6px;"
    "}"
    ".nav-button:hover {"
    "  background-color: rgba(255, 255, 255, 0.08);"
    "  color: #f1f5f9;"
    "}"
    ".nav-button:disabled {"
    "  color: #475569;"
    "}"
    ".toast-box {"
    "  background-color: rgba(22, 24, 38, 0.96);"
    "  border: 1px solid #7aa2f7;"
    "  border-radius: 14px;"
    "  padding: 6px 14px;"
    "}"
    ".toast-label {"
    "  color: #7dcfff;"
    "  font-size: 11px;"
    "  font-weight: 600;"
    "}"
    ".home-pill-box {"
    "  padding: 3px 0 6px 0;"
    "  background-color: rgba(18, 19, 28, 0.98);"
    "}"
    ".home-pill {"
    "  min-height: 4px;"
    "  min-width: 110px;"
    "  background-color: #64748b;"
    "  border-radius: 2px;"
    "}"
    ".popup-modal-layer {"
    "  background-color: #12131c;"
    "  border-radius: 20px;"
    "}"
    ".popup-modal-bar {"
    "  padding: 6px 12px;"
    "  background-color: #1e2234;"
    "  border-bottom: 1px solid #3c425c;"
    "}"
    ".popup-modal-title {"
    "  color: #e2e8f0;"
    "  font-size: 11px;"
    "  font-weight: bold;"
    "}"
    ".popup-close-btn {"
    "  color: #ef4444;"
    "  font-size: 14px;"
    "  font-weight: bold;"
    "  background: transparent;"
    "  border: none;"
    "  padding: 0 6px;"
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

    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char time_str[16];
    char date_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M", &tm_buf);
    strftime(date_str, sizeof(date_str), "%A, %B %d", &tm_buf);

    if (s_clock_label) {
        gtk_label_set_text(GTK_LABEL(s_clock_label), time_str);
    }
    if (s_home_clock_label) {
        gtk_label_set_text(GTK_LABEL(s_home_clock_label), time_str);
    }
    if (s_home_date_label) {
        gtk_label_set_text(GTK_LABEL(s_home_date_label), date_str);
    }

    if (s_mode_pill) {
        GhostMode ghost = input_get_ghost_mode();
        KeyMode mode = input_get_key_mode();
        GtkStyleContext *sc = gtk_widget_get_style_context(s_mode_pill);
        gtk_style_context_remove_class(sc, "pill-mirror");
        gtk_style_context_remove_class(sc, "pill-excl");
        gtk_style_context_remove_class(sc, "pill-ghost");

        if (ghost == GHOST_MODE_ON) {
            gtk_label_set_text(GTK_LABEL(s_mode_pill), "[GHOST]");
            gtk_style_context_add_class(sc, "pill-ghost");
        } else if (mode == KEY_MODE_EXCLUSIVE) {
            gtk_label_set_text(GTK_LABEL(s_mode_pill), "[EXCL]");
            gtk_style_context_add_class(sc, "pill-excl");
        } else {
            gtk_label_set_text(GTK_LABEL(s_mode_pill), "[MIRROR]");
            gtk_style_context_add_class(sc, "pill-mirror");
        }
    }

    if (s_target_pill) {
        TypingTarget target = input_get_typing_target();
        GtkStyleContext *sc = gtk_widget_get_style_context(s_target_pill);
        gtk_style_context_remove_class(sc, "pill-target-on");
        gtk_style_context_remove_class(sc, "pill-target-off");

        if (target == TYPING_TARGET_OVERLAY) {
            gtk_label_set_text(GTK_LABEL(s_target_pill), "● OVERLAY");
            gtk_style_context_add_class(sc, "pill-target-on");
        } else {
            gtk_label_set_text(GTK_LABEL(s_target_pill), "○ NONE");
            gtk_style_context_add_class(sc, "pill-target-off");
        }
    }

    if (s_grab_pill) {
        GrabStatus grab = input_get_grab_status();
        if (grab == GRAB_STATUS_FAILED) {
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

static void on_window_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data) {
    (void)user_data;
    const AppConfig *cfg = config_get();
    if (allocation->width != cfg->width || allocation->height != cfg->height) {
        xutil_log("WINDOW", "Enforcing window geometry lock: %dx%d -> %dx%d",
                  allocation->width, allocation->height, cfg->width, cfg->height);
        gtk_window_resize(GTK_WINDOW(widget), cfg->width, cfg->height);
        if (s_xid != None) {
            Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
            XResizeWindow(xdpy, s_xid, cfg->width, cfg->height);
            XFlush(xdpy);
        }
    }
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
    if (s_btn_reload) gtk_button_set_label(GTK_BUTTON(s_btn_reload), is_loading ? "✕" : "⟳");
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

static gboolean on_ghost_icon_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    if (w <= 0 || h <= 0) return FALSE;

    double cx = w / 2.0;
    double cy = h / 2.0;
    double scale = (w < h ? w : h) / 24.0;
    if (scale < 0.6) scale = 0.6;

    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, scale, scale);

    /* Ghost Silhouette */
    cairo_new_path(cr);
    cairo_arc(cr, 0.0, -3.0, 7.5, M_PI, 0.0);
    cairo_line_to(cr, 7.5, 5.0);
    cairo_curve_to(cr, 6.0, 7.5, 4.0, 7.5, 2.5, 5.0);
    cairo_curve_to(cr, 1.0, 7.5, -1.0, 7.5, -2.5, 5.0);
    cairo_curve_to(cr, -4.0, 7.5, -6.0, 7.5, -7.5, 5.0);
    cairo_close_path(cr);

    cairo_set_source_rgba(cr, 0.58, 0.70, 0.98, 0.95);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, 0.85, 0.92, 1.0, 0.9);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* Eyes */
    cairo_set_source_rgba(cr, 0.07, 0.08, 0.12, 1.0);
    cairo_new_path(cr);
    cairo_arc(cr, -2.8, -3.0, 1.4, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_new_path(cr);
    cairo_arc(cr, 2.8, -3.0, 1.4, 0, 2 * M_PI);
    cairo_fill(cr);

    cairo_restore(cr);
    return FALSE;
}

static gboolean on_ghost_revert_btn_label(gpointer user_data) {
    (void)user_data;
    s_ghost_revert_timer = 0;
    if (s_btn_ghost_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "icon");
    }
    return G_SOURCE_REMOVE;
}

void overlay_set_ghost_button_state(GhostButtonState state) {
    if (!s_btn_ghost_stack || !s_btn_ghost_label) return;

    if (s_ghost_revert_timer > 0) {
        g_source_remove(s_ghost_revert_timer);
        s_ghost_revert_timer = 0;
    }

    switch (state) {
        case GHOST_BTN_IDLE:
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "icon");
            break;
        case GHOST_BTN_TYPING:
            gtk_label_set_text(GTK_LABEL(s_btn_ghost_label), "⏳");
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "label");
            break;
        case GHOST_BTN_DONE:
            gtk_label_set_text(GTK_LABEL(s_btn_ghost_label), "✓");
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "label");
            s_ghost_revert_timer = g_timeout_add(1200, on_ghost_revert_btn_label, NULL);
            break;
        case GHOST_BTN_FAILED:
            gtk_label_set_text(GTK_LABEL(s_btn_ghost_label), "✗");
            gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "label");
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

static void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2.0);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

static gboolean on_chatgpt_icon_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget; (void)user_data;

    /* Rounded icon body */
    draw_rounded_rect(cr, 1.0, 1.0, 62.0, 62.0, 16.0);
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 64, 64);
    cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.063, 0.639, 0.498); /* #10a37f */
    cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.020, 0.588, 0.412); /* #059669 */
    cairo_set_source(cr, pat);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(pat);

    /* Subtle border highlight */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);

    /* OpenAI Rosette Glyph */
    cairo_save(cr);
    cairo_translate(cr, 32.0, 32.0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 2.4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    for (int i = 0; i < 6; i++) {
        cairo_save(cr);
        cairo_rotate(cr, i * (M_PI / 3.0));
        cairo_move_to(cr, 0.0, -3.5);
        cairo_curve_to(cr, 5.5, -14.0, 13.5, -10.5, 12.0, -4.5);
        cairo_curve_to(cr, 10.5, 1.5, 3.5, 2.5, 0.0, 3.5);
        cairo_stroke(cr);
        cairo_restore(cr);
    }
    cairo_restore(cr);

    return TRUE;
}

static gboolean on_browser_icon_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget; (void)user_data;

    /* Rounded icon body */
    draw_rounded_rect(cr, 1.0, 1.0, 62.0, 62.0, 16.0);
    cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, 64, 64);
    cairo_pattern_add_color_stop_rgb(pat, 0.0, 0.231, 0.510, 0.965); /* #3b82f6 */
    cairo_pattern_add_color_stop_rgb(pat, 1.0, 0.114, 0.306, 0.847); /* #1d4ed8 */
    cairo_set_source(cr, pat);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(pat);

    /* Subtle border highlight */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.25);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);

    /* Globe / Compass Glyph */
    cairo_save(cr);
    cairo_translate(cr, 32.0, 32.0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_set_line_width(cr, 2.2);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    /* Outer globe circle */
    cairo_arc(cr, 0, 0, 14.0, 0, 2.0 * M_PI);
    cairo_stroke(cr);

    /* Equator */
    cairo_move_to(cr, -14.0, 0.0);
    cairo_line_to(cr, 14.0, 0.0);
    cairo_stroke(cr);

    /* Central vertical meridian ellipse */
    cairo_save(cr);
    cairo_scale(cr, 0.48, 1.0);
    cairo_arc(cr, 0, 0, 14.0, 0, 2.0 * M_PI);
    cairo_restore(cr);
    cairo_stroke(cr);

    /* Upper and lower latitude arcs */
    cairo_move_to(cr, -10.0, -8.0);
    cairo_curve_to(cr, -5.0, -5.5, 5.0, -5.5, 10.0, -8.0);
    cairo_stroke(cr);

    cairo_move_to(cr, -10.0, 8.0);
    cairo_curve_to(cr, -5.0, 5.5, 5.0, 5.5, 10.0, 8.0);
    cairo_stroke(cr);

    cairo_restore(cr);

    return TRUE;
}

static void on_app_icon_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    const char *app = (const char *)user_data;
    overlay_switch_app(app);
}

static GtkWidget *create_app_icon_widget(const char *label_text, const char *app_id) {
    GtkWidget *btn = gtk_button_new();
    gtk_widget_set_can_focus(btn, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "app-button");

    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *da = gtk_drawing_area_new();
    gtk_widget_set_size_request(da, 64, 64);
    if (strcmp(app_id, "chatgpt") == 0) {
        g_signal_connect(da, "draw", G_CALLBACK(on_chatgpt_icon_draw), NULL);
    } else {
        g_signal_connect(da, "draw", G_CALLBACK(on_browser_icon_draw), NULL);
    }
    gtk_box_pack_start(GTK_BOX(col), da, FALSE, FALSE, 0);

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

    /* Centered clock and date widget */
    GtkWidget *clock_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(clock_box, 56);
    gtk_widget_set_margin_bottom(clock_box, 40);

    s_home_clock_label = gtk_label_new("12:00");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_home_clock_label), "home-big-clock");
    gtk_box_pack_start(GTK_BOX(clock_box), s_home_clock_label, FALSE, FALSE, 0);

    s_home_date_label = gtk_label_new("Saturday, October 17");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_home_date_label), "home-date");
    gtk_box_pack_start(GTK_BOX(clock_box), s_home_date_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), clock_box, FALSE, FALSE, 0);

    /* Grid for the two apps: ChatGPT and Browser */
    GtkWidget *apps_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 42);
    gtk_widget_set_halign(apps_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(apps_box, GTK_ALIGN_START);

    GtkWidget *btn_chatgpt = create_app_icon_widget("ChatGPT", "chatgpt");
    gtk_box_pack_start(GTK_BOX(apps_box), btn_chatgpt, FALSE, FALSE, 0);

    GtkWidget *btn_browser = create_app_icon_widget("Browser", "browser");
    gtk_box_pack_start(GTK_BOX(apps_box), btn_browser, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), apps_box, TRUE, TRUE, 0);

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

    GtkWidget *btn_close = gtk_button_new_with_label("✕");
    gtk_widget_set_can_focus(btn_close, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_close), "popup-close-btn");
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
    gtk_window_set_default_size(GTK_WINDOW(s_window), cfg->width, cfg->height);
    gtk_widget_set_size_request(s_window, cfg->width, cfg->height);
    gtk_window_set_resizable(GTK_WINDOW(s_window), FALSE);
    gtk_widget_set_opacity(s_window, cfg->opacity);

    /* Enforce size locking hints on GTK window */
    GdkGeometry hints;
    memset(&hints, 0, sizeof(hints));
    hints.min_width = cfg->width;
    hints.max_width = cfg->width;
    hints.min_height = cfg->height;
    hints.max_height = cfg->height;
    gtk_window_set_geometry_hints(GTK_WINDOW(s_window), s_window, &hints,
                                  GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);

    /* Connect size-allocate guard */
    g_signal_connect(s_window, "size-allocate", G_CALLBACK(on_window_size_allocate), NULL);

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
    gtk_widget_set_size_request(s_root_box, cfg->width, cfg->height);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_root_box), "phone-root");
    gtk_container_add(GTK_CONTAINER(s_window), s_root_box);

    /* --- Status Bar --- */
    GtkWidget *status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(status_bar), "status-bar");
    gtk_box_pack_start(GTK_BOX(s_root_box), status_bar, FALSE, FALSE, 0);

    /* Left: Clock and status glyphs */
    s_clock_label = gtk_label_new("12:00");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_clock_label), "status-clock");
    gtk_box_pack_start(GTK_BOX(status_bar), s_clock_label, FALSE, FALSE, 0);

    GtkWidget *glyphs_label = gtk_label_new("📶 100% ⚡");
    gtk_style_context_add_class(gtk_widget_get_style_context(glyphs_label), "status-glyphs");
    gtk_box_pack_start(GTK_BOX(status_bar), glyphs_label, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(status_bar), spacer, TRUE, TRUE, 0);

    /* Grab Alert Pill */
    s_grab_pill = gtk_label_new("[!GRAB]");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_grab_pill), "pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_grab_pill), "pill-grab-alert");
    gtk_box_pack_start(GTK_BOX(status_bar), s_grab_pill, FALSE, FALSE, 2);
    gtk_widget_set_no_show_all(s_grab_pill, TRUE);
    gtk_widget_hide(s_grab_pill);

    /* Mode Pill (clickable) */
    GtkWidget *mode_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(mode_eb), FALSE);
    s_mode_pill = gtk_label_new("[MIRROR]");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_mode_pill), "pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_mode_pill), "pill-mirror");
    gtk_container_add(GTK_CONTAINER(mode_eb), s_mode_pill);
    g_signal_connect(mode_eb, "button-press-event", G_CALLBACK(on_mode_pill_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(status_bar), mode_eb, FALSE, FALSE, 2);

    /* Target Pill (clickable) */
    GtkWidget *target_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(target_eb), FALSE);
    s_target_pill = gtk_label_new("○ NONE");
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

    GtkWidget *url_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    s_url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_url_entry), "Search or enter website...");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_url_entry), "url-entry");
    g_signal_connect(s_url_entry, "activate", G_CALLBACK(on_url_entry_activate), NULL);
    g_signal_connect(s_url_entry, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(url_row), s_url_entry, TRUE, TRUE, 0);

    /* Compact Reload Button in Browser URL Row */
    s_btn_reload = gtk_button_new_with_label("⟳");
    gtk_widget_set_can_focus(s_btn_reload, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_reload), "nav-button");
    gtk_widget_set_tooltip_text(s_btn_reload, "Reload page");
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
    gtk_entry_set_placeholder_text(GTK_ENTRY(s_test_entry), "Type here, then tap Ghost Writer");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_test_entry), "test-input-entry");
    g_signal_connect(s_test_entry, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    g_signal_connect(s_test_entry, "changed", G_CALLBACK(on_test_entry_changed), NULL);
    gtk_box_pack_start(GTK_BOX(test_box), s_test_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(s_root_box), test_box, FALSE, FALSE, 0);

    /* --- Bottom Navigation Bar --- */
    GtkWidget *bottom_nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(bottom_nav), "bottom-nav");
    gtk_box_pack_start(GTK_BOX(s_root_box), bottom_nav, FALSE, FALSE, 0);

    s_btn_back = gtk_button_new_with_label("←");
    gtk_widget_set_can_focus(s_btn_back, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_back), "nav-button");
    gtk_widget_set_sensitive(s_btn_back, FALSE);
    g_signal_connect(s_btn_back, "clicked", G_CALLBACK(on_btn_back_clicked), NULL);
    g_signal_connect(s_btn_back, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_back, TRUE, TRUE, 0);

    s_btn_home = gtk_button_new_with_label("⌂");
    gtk_widget_set_can_focus(s_btn_home, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_home), "nav-button");
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

    s_ghost_icon_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_ghost_icon_area, 22, 22);
    g_signal_connect(s_ghost_icon_area, "draw", G_CALLBACK(on_ghost_icon_draw), NULL);
    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), s_ghost_icon_area, "icon");

    s_btn_ghost_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_ghost_label), "nav-button");
    gtk_stack_add_named(GTK_STACK(s_btn_ghost_stack), s_btn_ghost_label, "label");

    gtk_container_add(GTK_CONTAINER(s_btn_ghost), s_btn_ghost_stack);
    gtk_stack_set_visible_child_name(GTK_STACK(s_btn_ghost_stack), "icon");

    g_signal_connect(s_btn_ghost, "clicked", G_CALLBACK(on_btn_ghost_clicked), NULL);
    g_signal_connect(s_btn_ghost, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_ghost, TRUE, TRUE, 0);

    /* Snap Button (Phase 6) */
    s_btn_snap = gtk_button_new_with_label("📷");
    gtk_widget_set_can_focus(s_btn_snap, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(s_btn_snap), "nav-button");
    gtk_widget_set_tooltip_text(s_btn_snap, "Capture Screen (Snap)");
    gtk_widget_set_sensitive(s_btn_snap, TRUE);
    g_signal_connect(s_btn_snap, "clicked", G_CALLBACK(on_btn_snap_clicked), NULL);
    g_signal_connect(s_btn_snap, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(bottom_nav), s_btn_snap, TRUE, TRUE, 0);

    /* Bottom Home Indicator Pill */
    GtkWidget *home_pill_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(home_pill_box), "home-pill-box");
    GtkWidget *home_pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(home_pill), "home-pill");
    gtk_widget_set_halign(home_pill, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(home_pill_box), home_pill, FALSE, FALSE, 0);

    GtkWidget *home_pill_eb = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(home_pill_eb), FALSE);
    gtk_container_add(GTK_CONTAINER(home_pill_eb), home_pill_box);
    g_signal_connect(home_pill_eb, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);
    gtk_box_pack_start(GTK_BOX(s_root_box), home_pill_eb, FALSE, FALSE, 0);

    /* Click listener on main window */
    gtk_widget_add_events(s_window, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(s_window, "button-press-event", G_CALLBACK(on_widget_button_press), NULL);

    /* Realize widget to obtain underlying XID */
    gtk_widget_realize(s_window);
    GdkWindow *gdk_win = gtk_widget_get_window(s_window);
    s_xid = gdk_x11_window_get_xid(gdk_win);

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, cfg->width, cfg->height);

    /* Enforce size hints at X11 level */
    XSizeHints *sh = XAllocSizeHints();
    if (sh) {
        sh->flags = PMinSize | PMaxSize;
        sh->min_width = sh->max_width = cfg->width;
        sh->min_height = sh->max_height = cfg->height;
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

    const AppConfig *cfg = config_get();
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, cfg->width, cfg->height);
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

    const AppConfig *cfg = config_get();
    calculate_monitor_position(&s_pos_x, &s_pos_y);

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, cfg->width, cfg->height);
    XFlush(xdpy);

    overlay_update_status_ui();
    xutil_log("WINDOW", "Overlay repositioned to (%d, %d)", s_pos_x, s_pos_y);
}

void overlay_apply_config(void) {
    if (!s_window || s_xid == None) return;

    const AppConfig *cfg = config_get();
    calculate_monitor_position(&s_pos_x, &s_pos_y);

    gtk_window_resize(GTK_WINDOW(s_window), cfg->width, cfg->height);
    gtk_widget_set_size_request(s_window, cfg->width, cfg->height);
    gtk_widget_set_opacity(s_window, cfg->opacity);
    gtk_window_move(GTK_WINDOW(s_window), s_pos_x, s_pos_y);

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XMoveResizeWindow(xdpy, s_xid, s_pos_x, s_pos_y, cfg->width, cfg->height);
    XFlush(xdpy);

    overlay_update_status_ui();
    xutil_log("WINDOW", "Applied config: %dx%d, opacity=%.2f, pos=(%d,%d)",
              cfg->width, cfg->height, cfg->opacity, s_pos_x, s_pos_y);
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
    const AppConfig *cfg = config_get();
    if (x) *x = s_pos_x;
    if (y) *y = s_pos_y;
    if (w) *w = cfg ? cfg->width : OVERLAY_WIDTH;
    if (h) *h = cfg ? cfg->height : OVERLAY_HEIGHT;
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
    if (s_btn_snap) {
        gtk_button_set_label(GTK_BUTTON(s_btn_snap), "📷");
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

        if (s_btn_snap) {
            gtk_button_set_label(GTK_BUTTON(s_btn_snap), "✓");
            if (s_snap_revert_timer > 0) {
                g_source_remove(s_snap_revert_timer);
            }
            s_snap_revert_timer = g_timeout_add(1500, on_snap_revert_btn_label, NULL);
        }
    } else {
        xutil_log("WINDOW", "Failed to capture screen pixbuf!");
        if (s_btn_snap) {
            gtk_button_set_label(GTK_BUTTON(s_btn_snap), "📷");
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

    if (s_btn_snap) {
        gtk_button_set_label(GTK_BUTTON(s_btn_snap), "⏳");
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
