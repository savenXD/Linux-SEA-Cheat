#include "input.h"
#include "window.h"
#include "browser.h"
#include "hotkey.h"
#include "config.h"
#include "ghost.h"
#include "xutil.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDLE_TIMEOUT_SECONDS 60
#define POINTER_FALLBACK_MS  35

static KeyMode      s_key_mode = KEY_MODE_MIRROR;
static TypingTarget s_typing_target = TYPING_TARGET_NONE;
static GhostMode    s_ghost_mode = GHOST_MODE_OFF;
static GrabStatus   s_grab_status = GRAB_STATUS_NONE;

static guint        s_idle_timer_id = 0;
static guint        s_fallback_timer_id = 0;
static gint64       s_last_native_click_time = 0;
static int          s_pending_fallback_button = 0;
static int          s_pending_fallback_root_x = 0;
static int          s_pending_fallback_root_y = 0;

static void acquire_exclusive_grab(void);
static void release_exclusive_grab(void);
static void restart_idle_timer(void);

gboolean input_init(void) {
    s_key_mode = KEY_MODE_MIRROR;
    s_typing_target = TYPING_TARGET_NONE;
    s_ghost_mode = GHOST_MODE_OFF;
    s_grab_status = GRAB_STATUS_NONE;
    s_idle_timer_id = 0;
    s_fallback_timer_id = 0;
    s_last_native_click_time = 0;

    xutil_log("INPUT", "Input subsystem initialized (Default: MIRROR mode, SOLID, typing target: NONE).");
    return TRUE;
}

static gboolean on_idle_timeout(gpointer user_data) {
    (void)user_data;
    xutil_log("INPUT", "60-second idle timeout expired. Releasing typing target and grabs.");
    s_idle_timer_id = 0;
    input_set_typing_target(TYPING_TARGET_NONE);
    return G_SOURCE_REMOVE;
}

static void restart_idle_timer(void) {
    if (s_idle_timer_id > 0) {
        g_source_remove(s_idle_timer_id);
    }
    s_idle_timer_id = g_timeout_add_seconds(IDLE_TIMEOUT_SECONDS, on_idle_timeout, NULL);
}

KeyMode input_get_key_mode(void) {
    return s_key_mode;
}

void input_set_key_mode(KeyMode mode) {
    if (s_key_mode == mode) return;
    s_key_mode = mode;
    xutil_log("INPUT", "Key mode set to: %s", mode == KEY_MODE_EXCLUSIVE ? "EXCLUSIVE" : "MIRROR");

    if (mode == KEY_MODE_EXCLUSIVE) {
        if (s_typing_target == TYPING_TARGET_OVERLAY) {
            acquire_exclusive_grab();
        }
    } else {
        if (s_grab_status == GRAB_STATUS_ACTIVE) {
            release_exclusive_grab();
        }
    }
    overlay_queue_draw();
}

void input_cycle_key_mode(void) {
    if (s_key_mode == KEY_MODE_MIRROR) {
        input_set_key_mode(KEY_MODE_EXCLUSIVE);
    } else {
        input_set_key_mode(KEY_MODE_MIRROR);
    }
}

TypingTarget input_get_typing_target(void) {
    return s_typing_target;
}

void input_set_typing_target(TypingTarget target) {
    if (s_typing_target == target) return;
    s_typing_target = target;
    xutil_log("INPUT", "Typing target set to: %s", target == TYPING_TARGET_OVERLAY ? "OVERLAY" : "NONE");

    if (target == TYPING_TARGET_OVERLAY) {
        const AppConfig *cfg = config_get();
        if (cfg && cfg->shield_clipboard_keys) {
            hotkey_install_clipboard_shield();
        }
        if (s_key_mode == KEY_MODE_EXCLUSIVE) {
            acquire_exclusive_grab();
        }
        restart_idle_timer();

        GtkWidget *win = overlay_get_window_widget();
        GtkWidget *cur = win ? gtk_window_get_focus(GTK_WINDOW(win)) : NULL;
        if (!cur) {
            GtkWidget *test_entry = overlay_get_test_entry();
            if (test_entry) {
                gtk_widget_grab_focus(test_entry);
            }
        }
    } else {
        hotkey_remove_clipboard_shield();
        if (s_grab_status == GRAB_STATUS_ACTIVE) {
            release_exclusive_grab();
        }
        if (s_idle_timer_id > 0) {
            g_source_remove(s_idle_timer_id);
            s_idle_timer_id = 0;
        }
    }
    overlay_queue_draw();
}

GhostMode input_get_ghost_mode(void) {
    return s_ghost_mode;
}

void input_set_ghost_mode(GhostMode ghost) {
    if (s_ghost_mode == ghost) return;
    s_ghost_mode = ghost;

    if (ghost == GHOST_MODE_ON) {
        xutil_log("INPUT", "Ghost mode ENABLED (transparent click-through).");
        if (s_typing_target == TYPING_TARGET_OVERLAY) {
            input_set_typing_target(TYPING_TARGET_NONE);
        }
        overlay_set_ghost(TRUE);
    } else {
        xutil_log("INPUT", "Ghost mode DISABLED (solid overlay restored).");
        overlay_set_ghost(FALSE);
    }
    overlay_queue_draw();
}

void input_toggle_ghost_mode(void) {
    if (s_ghost_mode == GHOST_MODE_OFF) {
        input_set_ghost_mode(GHOST_MODE_ON);
    } else {
        input_set_ghost_mode(GHOST_MODE_OFF);
    }
}

GrabStatus input_get_grab_status(void) {
    return s_grab_status;
}

static void acquire_exclusive_grab(void) {
    Window xid = overlay_get_xid();
    if (xid == None) return;

    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    int ret = XGrabKeyboard(xdpy, xid, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    XFlush(xdpy);

    if (ret == GrabSuccess) {
        s_grab_status = GRAB_STATUS_ACTIVE;
        xutil_log("INPUT", "Exclusive keyboard grab ACQUIRED on overlay XID 0x%lx.", (unsigned long)xid);
        restart_idle_timer();
    } else {
        s_grab_status = GRAB_STATUS_FAILED;
        xutil_log_err("INPUT", "Exclusive keyboard grab FAILED (code=%d, e.g. AlreadyGrabbed). Falling back to MIRROR.", ret);
    }
    overlay_queue_draw();
}

static void release_exclusive_grab(void) {
    if (s_grab_status == GRAB_STATUS_ACTIVE) {
        Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
        XUngrabKeyboard(xdpy, CurrentTime);
        XFlush(xdpy);
        s_grab_status = GRAB_STATUS_NONE;
        xutil_log("INPUT", "Exclusive keyboard grab RELEASED.");
    } else if (s_grab_status == GRAB_STATUS_FAILED) {
        s_grab_status = GRAB_STATUS_NONE;
    }

    if (s_idle_timer_id > 0) {
        g_source_remove(s_idle_timer_id);
        s_idle_timer_id = 0;
    }
    overlay_queue_draw();
}

void input_release_grab(void) {
    release_exclusive_grab();
}

void input_notify_native_click(GdkEventButton *event) {
    (void)event;
    s_last_native_click_time = g_get_monotonic_time();

    if (s_fallback_timer_id > 0) {
        g_source_remove(s_fallback_timer_id);
        s_fallback_timer_id = 0;
    }

    if (s_ghost_mode == GHOST_MODE_ON) return;

    input_set_typing_target(TYPING_TARGET_OVERLAY);
    restart_idle_timer();
}

void input_notify_overlay_hidden(void) {
    hotkey_remove_clipboard_shield();
    input_set_typing_target(TYPING_TARGET_NONE);
    release_exclusive_grab();
}

static gboolean on_pointer_fallback_check(gpointer user_data) {
    (void)user_data;
    s_fallback_timer_id = 0;

    gint64 now = g_get_monotonic_time();
    /* If no native event reached us within 30ms (30,000 microseconds) */
    if (now - s_last_native_click_time > 30000) {
        xutil_log("INPUT", "Pointer-grab fallback engaged (foreign pointer grab detected). Synthesizing click.");

        int ox, oy, ow, oh;
        overlay_get_geometry(&ox, &oy, &ow, &oh);
        int rel_x = s_pending_fallback_root_x - ox;
        int rel_y = s_pending_fallback_root_y - oy;

        GtkWidget *target_widget = NULL;
        int target_x = rel_x;
        int target_y = rel_y;

        const char *curr_app = overlay_get_current_app();
        bool is_chatgpt = (curr_app && g_ascii_strcasecmp(curr_app, "chatgpt") == 0);

        if (overlay_has_popup_modal()) {
            if (rel_y < 65) {
                target_widget = overlay_get_window_widget();
            } else {
                target_widget = overlay_get_popup_child_view();
                target_y = rel_y - 65;
            }
        } else if (rel_y >= oh - 80 && rel_y <= oh - 40) {
            /* Test input entry area */
            target_widget = overlay_get_test_entry();
            target_y = rel_y - (oh - 80);
        } else if (rel_y > oh - 40) {
            /* Bottom controls */
            target_widget = overlay_get_window_widget();
        } else if (is_chatgpt) {
            if (rel_y < 28) {
                /* Top status bar */
                target_widget = overlay_get_window_widget();
            } else {
                /* ChatGPT center viewport (starts immediately under status bar) */
                target_widget = GTK_WIDGET(browser_get_active_view());
                target_y = rel_y - 28;
            }
        } else if (rel_y < 70) {
            /* Top area: URL entry if in browser */
            target_widget = overlay_get_url_entry();
            if (!target_widget || !gtk_widget_is_visible(target_widget)) {
                target_widget = overlay_get_test_entry();
            }
            target_y = (rel_y > 28) ? (rel_y - 28) : rel_y;
        } else {
            /* Center viewport */
            target_widget = GTK_WIDGET(browser_get_active_view());
            target_y = rel_y - 70;
        }

        if (target_widget && gtk_widget_get_window(target_widget)) {
            GdkDisplay *display = gdk_display_get_default();
            GdkSeat *seat = gdk_display_get_default_seat(display);
            GdkDevice *pointer = gdk_seat_get_pointer(seat);
            guint32 ev_time = (guint32)(g_get_monotonic_time() / 1000);

            GdkEvent *ev_press = gdk_event_new(GDK_BUTTON_PRESS);
            ev_press->button.window = g_object_ref(gtk_widget_get_window(target_widget));
            ev_press->button.send_event = FALSE;
            ev_press->button.time = ev_time;
            ev_press->button.x = target_x;
            ev_press->button.y = target_y;
            ev_press->button.x_root = s_pending_fallback_root_x;
            ev_press->button.y_root = s_pending_fallback_root_y;
            ev_press->button.button = s_pending_fallback_button;
            ev_press->button.state = GDK_BUTTON1_MASK;
            gdk_event_set_device(ev_press, pointer);

            gtk_widget_event(target_widget, ev_press);
            gdk_event_free(ev_press);

            GdkEvent *ev_rel = gdk_event_new(GDK_BUTTON_RELEASE);
            ev_rel->button.window = g_object_ref(gtk_widget_get_window(target_widget));
            ev_rel->button.send_event = FALSE;
            ev_rel->button.time = ev_time;
            ev_rel->button.x = target_x;
            ev_rel->button.y = target_y;
            ev_rel->button.x_root = s_pending_fallback_root_x;
            ev_rel->button.y_root = s_pending_fallback_root_y;
            ev_rel->button.button = s_pending_fallback_button;
            ev_rel->button.state = 0;
            gdk_event_set_device(ev_rel, pointer);

            gtk_widget_event(target_widget, ev_rel);
            gdk_event_free(ev_rel);

            gtk_widget_grab_focus(target_widget);
        }

        input_set_typing_target(TYPING_TARGET_OVERLAY);
        restart_idle_timer();
    }
    return G_SOURCE_REMOVE;
}

void input_handle_raw_button(int button, bool is_press, int root_x, int root_y) {
    int ox, oy, ow, oh;
    overlay_get_geometry(&ox, &oy, &ow, &oh);

    bool is_inside = (root_x >= ox && root_x < ox + ow &&
                      root_y >= oy && root_y < oy + oh);

    if (is_inside) {
        if (s_ghost_mode == GHOST_MODE_ON) {
            return;
        }

        if (is_press) {
            if (button >= 1 && button <= 3) {
                s_pending_fallback_button = button;
                s_pending_fallback_root_x = root_x;
                s_pending_fallback_root_y = root_y;

                if (s_fallback_timer_id == 0) {
                    s_fallback_timer_id = g_timeout_add(POINTER_FALLBACK_MS, on_pointer_fallback_check, NULL);
                }
            }

            input_set_typing_target(TYPING_TARGET_OVERLAY);
            restart_idle_timer();
        }
    } else {
        if (is_press && button >= 1 && button <= 3) {
            input_set_typing_target(TYPING_TARGET_NONE);
        }
    }
}

static GdkModifierType get_current_modifiers(void) {
    Display *xdpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    Window root = DefaultRootWindow(xdpy);
    Window root_ret, child_ret;
    int rx, ry, wx, wy;
    unsigned int mask = 0;

    XQueryPointer(xdpy, root, &root_ret, &child_ret, &rx, &ry, &wx, &wy, &mask);

    GdkModifierType state = 0;
    if (mask & ShiftMask)   state |= GDK_SHIFT_MASK;
    if (mask & LockMask)    state |= GDK_LOCK_MASK;
    if (mask & ControlMask) state |= GDK_CONTROL_MASK;
    if (mask & Mod1Mask)    state |= GDK_MOD1_MASK;
    if (mask & Mod2Mask)    state |= GDK_MOD2_MASK;
    if (mask & Mod5Mask)    state |= GDK_MOD5_MASK;

    return state;
}

void input_handle_raw_key(int keycode, bool is_press) {
    if (ghost_is_guard_active()) return;

    /* If typing target is not overlay, keys go only to the focused desktop app */
    if (s_typing_target != TYPING_TARGET_OVERLAY) {
        return;
    }

    restart_idle_timer();

    /* Find target widget: check focused widget in overlay window */
    GtkWidget *target = NULL;
    GtkWidget *win = overlay_get_window_widget();
    if (win) {
        GtkWidget *f = gtk_window_get_focus(GTK_WINDOW(win));
        if (f && (GTK_IS_EDITABLE(f) || WEBKIT_IS_WEB_VIEW(f))) {
            target = f;
        }
    }
    if (!target) {
        target = overlay_get_test_entry();
    }
    if (!target) {
        target = overlay_get_entry_widget();
    }
    if (!target || !gtk_widget_get_window(target)) {
        target = GTK_WIDGET(browser_get_active_view());
    }
    if (!target || !gtk_widget_get_window(target)) {
        return;
    }

    GdkDisplay *display = gdk_display_get_default();
    GdkKeymap *keymap = gdk_keymap_get_for_display(display);
    GdkModifierType state = get_current_modifiers();

    guint keyval = 0;
    gdk_keymap_translate_keyboard_state(keymap, keycode, state, 0, &keyval, NULL, NULL, NULL);

    if (keyval == 0) return;

    /* Don't send solitary modifier key releases to entry/browser */
    if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
        keyval == GDK_KEY_Shift_L   || keyval == GDK_KEY_Shift_R ||
        keyval == GDK_KEY_Alt_L     || keyval == GDK_KEY_Alt_R ||
        keyval == GDK_KEY_Meta_L    || keyval == GDK_KEY_Meta_R) {
        return;
    }

    gunichar c = gdk_keyval_to_unicode(keyval);
    char str[7] = {0};
    if (c > 0) {
        g_unichar_to_utf8(c, str);
    } else if (keyval == GDK_KEY_BackSpace) {
        str[0] = '\b';
    } else if (keyval == GDK_KEY_Return) {
        str[0] = '\r';
    } else if (keyval == GDK_KEY_Tab) {
        str[0] = '\t';
    }

    GdkSeat *seat = gdk_display_get_default_seat(display);
    GdkDevice *keyboard = gdk_seat_get_keyboard(seat);
    guint32 ev_time = (guint32)(g_get_monotonic_time() / 1000);

    GdkEvent *ev = gdk_event_new(is_press ? GDK_KEY_PRESS : GDK_KEY_RELEASE);
    ev->key.window = g_object_ref(gtk_widget_get_window(target));
    ev->key.send_event = FALSE;
    ev->key.time = ev_time;
    ev->key.state = state;
    ev->key.keyval = keyval;
    ev->key.hardware_keycode = keycode;
    ev->key.length = strlen(str);
    ev->key.string = g_strdup(str);
    gdk_event_set_device(ev, keyboard);

    gtk_widget_event(target, ev);
    gdk_event_free(ev);

    overlay_update_status_ui();
}

void input_cleanup(void) {
    if (s_fallback_timer_id > 0) {
        g_source_remove(s_fallback_timer_id);
        s_fallback_timer_id = 0;
    }
    if (s_idle_timer_id > 0) {
        g_source_remove(s_idle_timer_id);
        s_idle_timer_id = 0;
    }
    hotkey_remove_clipboard_shield();
    release_exclusive_grab();
    xutil_log("INPUT", "Input subsystem cleaned up.");
}
