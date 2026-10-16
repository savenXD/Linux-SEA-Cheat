#include "hotkey.h"
#include "window.h"
#include "xutil.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Display   *s_hotkey_dpy = NULL;
static Window     s_root = None;
static int        s_xi_opcode = 0;
static GSource   *s_hotkey_source = NULL;
static gint64     s_last_toggle_time = 0;
static bool       s_x_key_down = false;

/* Raw event modifier tracking for resilience against external grabs */
static bool       s_raw_ctrl_down = false;
static bool       s_raw_shift_down = false;
static bool       s_raw_alt_down = false;

static const unsigned int s_lock_combos[] = {
    0,
    Mod2Mask,                /* NumLock */
    LockMask,                /* CapsLock */
    Mod2Mask | LockMask      /* NumLock + CapsLock */
};
#define NUM_LOCK_COMBOS (sizeof(s_lock_combos) / sizeof(s_lock_combos[0]))

static inline bool is_keycode_down(const char keys[32], KeyCode kc) {
    if (kc == 0) return false;
    return (((unsigned char)keys[kc / 8]) & (1 << (kc % 8))) != 0;
}

static bool is_ctrl_keycode(Display *dpy, KeyCode kc) {
    if (kc == 0) return false;
    if (kc == XKeysymToKeycode(dpy, XK_Control_L) ||
        kc == XKeysymToKeycode(dpy, XK_Control_R)) {
        return true;
    }
    XModifierKeymap *modmap = XGetModifierMapping(dpy);
    if (modmap) {
        for (int i = 0; i < modmap->max_keypermod; i++) {
            if (modmap->modifiermap[ControlMapIndex * modmap->max_keypermod + i] == kc) {
                XFreeModifiermap(modmap);
                return true;
            }
        }
        XFreeModifiermap(modmap);
    }
    return false;
}

static bool is_shift_keycode(Display *dpy, KeyCode kc) {
    if (kc == 0) return false;
    if (kc == XKeysymToKeycode(dpy, XK_Shift_L) ||
        kc == XKeysymToKeycode(dpy, XK_Shift_R)) {
        return true;
    }
    XModifierKeymap *modmap = XGetModifierMapping(dpy);
    if (modmap) {
        for (int i = 0; i < modmap->max_keypermod; i++) {
            if (modmap->modifiermap[ShiftMapIndex * modmap->max_keypermod + i] == kc) {
                XFreeModifiermap(modmap);
                return true;
            }
        }
        XFreeModifiermap(modmap);
    }
    return false;
}

static bool is_alt_keycode(Display *dpy, KeyCode kc) {
    if (kc == 0) return false;
    if (kc == XKeysymToKeycode(dpy, XK_Alt_L) ||
        kc == XKeysymToKeycode(dpy, XK_Alt_R) ||
        kc == XKeysymToKeycode(dpy, XK_Meta_L) ||
        kc == XKeysymToKeycode(dpy, XK_Meta_R) ||
        kc == XKeysymToKeycode(dpy, XK_ISO_Level3_Shift)) {
        return true;
    }
    XModifierKeymap *modmap = XGetModifierMapping(dpy);
    if (modmap) {
        for (int i = 0; i < modmap->max_keypermod; i++) {
            if (modmap->modifiermap[Mod1MapIndex * modmap->max_keypermod + i] == kc) {
                XFreeModifiermap(modmap);
                return true;
            }
        }
        XFreeModifiermap(modmap);
    }
    return false;
}

static bool are_modifiers_physically_down(Display *dpy) {
    char keys[32];
    XQueryKeymap(dpy, keys);

    bool ctrl_down = false;
    bool shift_down = false;
    bool alt_down = false;

    /* 1. Check via full XModifierKeymap */
    XModifierKeymap *modmap = XGetModifierMapping(dpy);
    if (modmap) {
        for (int i = 0; i < modmap->max_keypermod; i++) {
            KeyCode kc_ctrl = modmap->modifiermap[ControlMapIndex * modmap->max_keypermod + i];
            if (is_keycode_down(keys, kc_ctrl)) ctrl_down = true;

            KeyCode kc_shift = modmap->modifiermap[ShiftMapIndex * modmap->max_keypermod + i];
            if (is_keycode_down(keys, kc_shift)) shift_down = true;

            KeyCode kc_alt = modmap->modifiermap[Mod1MapIndex * modmap->max_keypermod + i];
            if (is_keycode_down(keys, kc_alt)) alt_down = true;
        }
        XFreeModifiermap(modmap);
    }

    /* 2. Direct keysym lookup fallbacks */
    if (!ctrl_down) {
        ctrl_down = is_keycode_down(keys, XKeysymToKeycode(dpy, XK_Control_L)) ||
                    is_keycode_down(keys, XKeysymToKeycode(dpy, XK_Control_R));
    }
    if (!shift_down) {
        shift_down = is_keycode_down(keys, XKeysymToKeycode(dpy, XK_Shift_L)) ||
                     is_keycode_down(keys, XKeysymToKeycode(dpy, XK_Shift_R));
    }
    if (!alt_down) {
        alt_down = is_keycode_down(keys, XKeysymToKeycode(dpy, XK_Alt_L)) ||
                   is_keycode_down(keys, XKeysymToKeycode(dpy, XK_Alt_R)) ||
                   is_keycode_down(keys, XKeysymToKeycode(dpy, XK_Meta_L)) ||
                   is_keycode_down(keys, XKeysymToKeycode(dpy, XK_Meta_R));
    }

    /* 3. Combined check with internal raw event tracker */
    if (!ctrl_down && s_raw_ctrl_down) ctrl_down = true;
    if (!shift_down && s_raw_shift_down) shift_down = true;
    if (!alt_down && s_raw_alt_down) alt_down = true;

    return (ctrl_down && shift_down && alt_down);
}

static void trigger_hotkey_toggle(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    /* Dedupe within 150 ms (150,000 microseconds) */
    if (now - s_last_toggle_time < 150000) {
        xutil_log("HOTKEY", "Deduped toggle from %s (<150ms since last trigger)", source_desc);
        return;
    }

    s_last_toggle_time = now;
    xutil_log("HOTKEY", "Ctrl+Shift+Alt+X triggered via %s -> Toggling overlay", source_desc);
    overlay_toggle();
}

static void handle_raw_key_press(int detail) {
    if (!s_hotkey_dpy) return;

    /* Track raw modifier keys */
    if (is_ctrl_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_ctrl_down = true;
    }
    if (is_shift_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_shift_down = true;
    }
    if (is_alt_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_alt_down = true;
    }

    /* Check keycode for X (both lowercase and uppercase keysyms) */
    KeyCode x_kc_lower = XKeysymToKeycode(s_hotkey_dpy, XK_x);
    KeyCode x_kc_upper = XKeysymToKeycode(s_hotkey_dpy, XK_X);
    bool is_x = ((KeyCode)detail == x_kc_lower) || (x_kc_upper != 0 && (KeyCode)detail == x_kc_upper);

    if (!is_x) {
        return;
    }

    /* Only proceed if Control, Shift, and Alt are down */
    if (!are_modifiers_physically_down(s_hotkey_dpy)) {
        return;
    }

    if (s_x_key_down) {
        /* Auto-repeat suppression: do not toggle again until 'x' is released */
        return;
    }
    s_x_key_down = true;

    trigger_hotkey_toggle("XInput2 Raw Event");
}

static void handle_raw_key_release(int detail) {
    if (!s_hotkey_dpy) return;

    if (is_ctrl_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_ctrl_down = false;
    }
    if (is_shift_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_shift_down = false;
    }
    if (is_alt_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_alt_down = false;
    }

    KeyCode x_kc_lower = XKeysymToKeycode(s_hotkey_dpy, XK_x);
    KeyCode x_kc_upper = XKeysymToKeycode(s_hotkey_dpy, XK_X);
    if ((KeyCode)detail == x_kc_lower || (x_kc_upper != 0 && (KeyCode)detail == x_kc_upper)) {
        s_x_key_down = false;
    }
}

static void register_passive_grabs(void);
static void unregister_passive_grabs(void);

static void hotkey_process_events(void) {
    if (!s_hotkey_dpy) return;

    while (XPending(s_hotkey_dpy) > 0) {
        XEvent ev;
        XNextEvent(s_hotkey_dpy, &ev);

        if (ev.type == GenericEvent && ev.xcookie.extension == s_xi_opcode) {
            if (XGetEventData(s_hotkey_dpy, &ev.xcookie)) {
                if (ev.xcookie.evtype == XI_RawKeyPress) {
                    XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
                    handle_raw_key_press(raw->detail);
                } else if (ev.xcookie.evtype == XI_RawKeyRelease) {
                    XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
                    handle_raw_key_release(raw->detail);
                }
                XFreeEventData(s_hotkey_dpy, &ev.xcookie);
            }
        } else if (ev.type == KeyPress) {
            /* Fired by passive XGrabKey */
            KeyCode x_kc_lower = XKeysymToKeycode(s_hotkey_dpy, XK_x);
            KeyCode x_kc_upper = XKeysymToKeycode(s_hotkey_dpy, XK_X);
            if (ev.xkey.keycode == x_kc_lower || (x_kc_upper != 0 && ev.xkey.keycode == x_kc_upper)) {
                s_x_key_down = true;
                trigger_hotkey_toggle("Passive XGrabKey");
            }
        } else if (ev.type == KeyRelease) {
            KeyCode x_kc_lower = XKeysymToKeycode(s_hotkey_dpy, XK_x);
            KeyCode x_kc_upper = XKeysymToKeycode(s_hotkey_dpy, XK_X);
            if (ev.xkey.keycode == x_kc_lower || (x_kc_upper != 0 && ev.xkey.keycode == x_kc_upper)) {
                s_x_key_down = false;
            }
        } else if (ev.type == MappingNotify) {
            XRefreshKeyboardMapping(&ev.xmapping);
            xutil_log("HOTKEY", "Keyboard mapping updated by server. Re-registering grabs.");
            unregister_passive_grabs();
            register_passive_grabs();
        }
    }
}

/* Custom GSource to integrate secondary Display socket FD into GLib */
typedef struct {
    GSource     source;
    Display    *dpy;
    GPollFD     poll_fd;
} HotkeyGSource;

static gboolean hotkey_source_prepare(GSource *source, gint *timeout) {
    HotkeyGSource *hsrc = (HotkeyGSource *)source;
    *timeout = -1;
    return (XPending(hsrc->dpy) > 0);
}

static gboolean hotkey_source_check(GSource *source) {
    HotkeyGSource *hsrc = (HotkeyGSource *)source;
    if (hsrc->poll_fd.revents & (G_IO_IN | G_IO_HUP | G_IO_ERR)) {
        return TRUE;
    }
    return (XPending(hsrc->dpy) > 0);
}

static gboolean hotkey_source_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
    (void)source;
    (void)callback;
    (void)user_data;
    hotkey_process_events();
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs s_hotkey_source_funcs = {
    hotkey_source_prepare,
    hotkey_source_check,
    hotkey_source_dispatch,
    NULL, NULL, NULL
};

static void register_passive_grabs(void) {
    if (!s_hotkey_dpy || s_root == None) return;

    KeyCode x_kc = XKeysymToKeycode(s_hotkey_dpy, XK_x);
    if (x_kc == 0) {
        xutil_log_err("HOTKEY", "Unable to resolve keycode for XK_x!");
        return;
    }

    unsigned int base_mods = ControlMask | ShiftMask | Mod1Mask;

    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        unsigned int mods = base_mods | s_lock_combos[i];
        XGrabKey(s_hotkey_dpy, x_kc, mods, s_root, False, GrabModeAsync, GrabModeAsync);
    }
    XSync(s_hotkey_dpy, False);
    xutil_log("HOTKEY", "Registered passive XGrabKey for Ctrl+Shift+Alt+X (keycode %d) across lock variations.", x_kc);
}

static void unregister_passive_grabs(void) {
    if (!s_hotkey_dpy || s_root == None) return;

    KeyCode x_kc = XKeysymToKeycode(s_hotkey_dpy, XK_x);
    if (x_kc == 0) return;

    unsigned int base_mods = ControlMask | ShiftMask | Mod1Mask;

    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        unsigned int mods = base_mods | s_lock_combos[i];
        XUngrabKey(s_hotkey_dpy, x_kc, mods, s_root);
    }
    XFlush(s_hotkey_dpy);
    xutil_log("HOTKEY", "Released all passive XGrabKey grabs.");
}

gboolean hotkey_init(void) {
    /* 1. Open dedicated SECOND Display connection */
    s_hotkey_dpy = XOpenDisplay(NULL);
    if (!s_hotkey_dpy) {
        xutil_log_err("HOTKEY", "Failed to open dedicated X11 display connection for hotkeys!");
        return FALSE;
    }

    int screen = DefaultScreen(s_hotkey_dpy);
    s_root = RootWindow(s_hotkey_dpy, screen);

    /* 2. Verify XInput 2.2+ extension support */
    int xi_event, xi_error;
    if (!XQueryExtension(s_hotkey_dpy, "XInputExtension", &s_xi_opcode, &xi_event, &xi_error)) {
        xutil_log_err("HOTKEY", "XInputExtension not supported on this X server!");
        XCloseDisplay(s_hotkey_dpy);
        s_hotkey_dpy = NULL;
        return FALSE;
    }

    int major = 2, minor = 2;
    if (XIQueryVersion(s_hotkey_dpy, &major, &minor) != Success) {
        xutil_log_err("HOTKEY", "XInput2 version 2.2+ required, but server reported %d.%d", major, minor);
        XCloseDisplay(s_hotkey_dpy);
        s_hotkey_dpy = NULL;
        return FALSE;
    }
    xutil_log("HOTKEY", "XInput2 initialized (version %d.%d, opcode %d).", major, minor, s_xi_opcode);

    /* 3. Select XI_RawKeyPress and XI_RawKeyRelease on root window for XIAllMasterDevices */
    XIEventMask mask;
    unsigned char mask_bits[XIMaskLen(XI_RawKeyRelease)] = {0};
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(mask_bits);
    mask.mask = mask_bits;
    XISetMask(mask_bits, XI_RawKeyPress);
    XISetMask(mask_bits, XI_RawKeyRelease);

    if (XISelectEvents(s_hotkey_dpy, s_root, &mask, 1) != Success) {
        xutil_log_err("HOTKEY", "Failed to select XI2 raw events on root window!");
        XCloseDisplay(s_hotkey_dpy);
        s_hotkey_dpy = NULL;
        return FALSE;
    }
    XSync(s_hotkey_dpy, False);
    xutil_log("HOTKEY", "Selected XI_RawKeyPress and XI_RawKeyRelease on root window.");

    /* 4. Register passive XGrabKey on root window */
    register_passive_grabs();

    /* 5. Integrate secondary Display socket into GLib main loop */
    GSource *source = g_source_new(&s_hotkey_source_funcs, sizeof(HotkeyGSource));
    HotkeyGSource *hsrc = (HotkeyGSource *)source;
    hsrc->dpy = s_hotkey_dpy;
    hsrc->poll_fd.fd = ConnectionNumber(s_hotkey_dpy);
    hsrc->poll_fd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    hsrc->poll_fd.revents = 0;

    g_source_add_poll(source, &hsrc->poll_fd);
    g_source_attach(source, NULL);
    s_hotkey_source = source;

    xutil_log("HOTKEY", "Integrated dedicated Display FD %d into GLib main loop.", hsrc->poll_fd.fd);
    return TRUE;
}

void hotkey_cleanup(void) {
    if (!s_hotkey_dpy) return;

    unregister_passive_grabs();

    if (s_hotkey_source) {
        g_source_destroy(s_hotkey_source);
        g_source_unref(s_hotkey_source);
        s_hotkey_source = NULL;
    }

    XCloseDisplay(s_hotkey_dpy);
    s_hotkey_dpy = NULL;
    s_root = None;
    xutil_log("HOTKEY", "Hotkey system cleaned up.");
}
