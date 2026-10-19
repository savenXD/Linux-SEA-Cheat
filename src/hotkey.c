#include "hotkey.h"
#include "config.h"
#include "window.h"
#include "input.h"
#include "ghost.h"
#include "xutil.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KeySym char_to_keysym_lower(char c) {
    if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
    if (c >= 'a' && c <= 'z') return XK_a + (c - 'a');
    if (c >= '0' && c <= '9') return XK_0 + (c - '0');
    return XK_x;
}

static KeySym char_to_keysym_upper(char c) {
    if (c >= 'a' && c <= 'z') c = c - ('a' - 'A');
    if (c >= 'A' && c <= 'Z') return XK_A + (c - 'A');
    return NoSymbol;
}

static Display   *s_hotkey_dpy = NULL;
static Window     s_root = None;
static int        s_xi_opcode = 0;
static GSource   *s_hotkey_source = NULL;
static gint64     s_last_toggle_time = 0;
static gint64     s_last_mode_cycle_time = 0;
static gint64     s_last_ghost_toggle_time = 0;
static gint64     s_last_opacity_dec_time = 0;
static gint64     s_last_opacity_inc_time = 0;
static gint64     s_last_fullscreen_time = 0;
static gint64     s_last_default_size_time = 0;
static gint64     s_last_snap_time = 0;
static gint64     s_last_ghost_write_time = 0;

static bool       s_x_key_down = false;
static bool       s_g_key_down = false;
static bool       s_a_key_down = false;
static bool       s_s_key_down = false;
static bool       s_f_key_down = false;
static bool       s_d_key_down = false;
static bool       s_p_key_down = false;
static bool       s_m_key_down = false;
static bool       s_w_key_down = false;

/* Raw event modifier tracking for resilience against external grabs */
static bool       s_raw_ctrl_down = false;
static bool       s_raw_shift_down = false;
static bool       s_raw_alt_down = false;

/* Active clipboard-key shield state */
static bool       s_clipboard_shield_active = false;
static gint64     s_last_shield_time = 0;
static KeyCode    s_last_shield_keycode = 0;

/* Cinnamon locate-pointer grab management (releases Muffin Control_L grab during overlay typing) */
static GSettings *s_cinnamon_mouse_settings = NULL;
static gboolean   s_orig_locate_pointer = FALSE;
static gboolean   s_managed_locate_pointer = FALSE;

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

static bool is_alt_physically_down(Display *dpy) {
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

    /* 3. Physical hardware keymap is strict source of truth for Alt.
     * Keep internal raw modifier tracker in sync with physical state. */
    s_raw_alt_down = alt_down;
    if (!ctrl_down && s_raw_ctrl_down) ctrl_down = true;
    if (!shift_down && s_raw_shift_down) shift_down = true;

    return (alt_down && !ctrl_down && !shift_down);
}

static void get_hotkey_keycodes(Display *dpy,
                                KeyCode *x_l, KeyCode *x_u,
                                KeyCode *g_l, KeyCode *g_u,
                                KeyCode *a_l, KeyCode *a_u,
                                KeyCode *s_l, KeyCode *s_u,
                                KeyCode *f_l, KeyCode *f_u,
                                KeyCode *d_l, KeyCode *d_u,
                                KeyCode *p_l, KeyCode *p_u,
                                KeyCode *m_l, KeyCode *m_u,
                                KeyCode *w_l, KeyCode *w_u) {
    const AppConfig *cfg = config_get();
    char x_c = (cfg && cfg->toggle_key) ? cfg->toggle_key : 'x';
    char g_c = (cfg && cfg->ghost_key) ? cfg->ghost_key : 'g';
    char p_c = (cfg && cfg->snap_key) ? cfg->snap_key : 'p';
    char m_c = (cfg && cfg->mode_key) ? cfg->mode_key : 'm';
    char w_c = (cfg && cfg->ghost_writer_key) ? cfg->ghost_writer_key : 'w';

    if (x_l) *x_l = XKeysymToKeycode(dpy, char_to_keysym_lower(x_c));
    if (x_u) *x_u = XKeysymToKeycode(dpy, char_to_keysym_upper(x_c));
    if (g_l) *g_l = XKeysymToKeycode(dpy, char_to_keysym_lower(g_c));
    if (g_u) *g_u = XKeysymToKeycode(dpy, char_to_keysym_upper(g_c));
    if (a_l) *a_l = XKeysymToKeycode(dpy, XK_a);
    if (a_u) *a_u = XKeysymToKeycode(dpy, XK_A);
    if (s_l) *s_l = XKeysymToKeycode(dpy, XK_s);
    if (s_u) *s_u = XKeysymToKeycode(dpy, XK_S);
    if (f_l) *f_l = XKeysymToKeycode(dpy, XK_f);
    if (f_u) *f_u = XKeysymToKeycode(dpy, XK_F);
    if (d_l) *d_l = XKeysymToKeycode(dpy, XK_d);
    if (d_u) *d_u = XKeysymToKeycode(dpy, XK_D);
    if (p_l) *p_l = XKeysymToKeycode(dpy, char_to_keysym_lower(p_c));
    if (p_u) *p_u = XKeysymToKeycode(dpy, char_to_keysym_upper(p_c));
    if (m_l) *m_l = XKeysymToKeycode(dpy, char_to_keysym_lower(m_c));
    if (m_u) *m_u = XKeysymToKeycode(dpy, char_to_keysym_upper(m_c));
    if (w_l) *w_l = XKeysymToKeycode(dpy, char_to_keysym_lower(w_c));
    if (w_u) *w_u = XKeysymToKeycode(dpy, char_to_keysym_upper(w_c));
}

static void trigger_snap(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_snap_time < 200000) {
        xutil_log("HOTKEY", "Deduped snap from %s (<200ms since last trigger)", source_desc);
        return;
    }
    s_last_snap_time = now;
    xutil_log("HOTKEY", "Alt+P triggered via %s -> Taking snap", source_desc);
    overlay_snap();
}

static void trigger_hotkey_toggle(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    /* Dedupe within 150 ms (150,000 microseconds) */
    if (now - s_last_toggle_time < 150000) {
        xutil_log("HOTKEY", "Deduped toggle from %s (<150ms since last trigger)", source_desc);
        return;
    }

    s_last_toggle_time = now;
    xutil_log("HOTKEY", "Alt+X triggered via %s -> Toggling overlay", source_desc);
    input_release_grab();
    overlay_toggle();
}

static void trigger_ghost_toggle(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_ghost_toggle_time < 150000) {
        xutil_log("HOTKEY", "Deduped ghost toggle from %s (<150ms since last trigger)", source_desc);
        return;
    }
    s_last_ghost_toggle_time = now;
    xutil_log("HOTKEY", "Alt+G triggered via %s -> Toggling ghost mode", source_desc);
    input_toggle_ghost_mode();
}

static void trigger_opacity_dec(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_opacity_dec_time < 120000) {
        xutil_log("HOTKEY", "Deduped opacity dec from %s (<120ms since last trigger)", source_desc);
        return;
    }
    s_last_opacity_dec_time = now;
    xutil_log("HOTKEY", "Alt+A triggered via %s -> Decreasing opacity", source_desc);
    overlay_set_opacity(overlay_get_opacity() - 0.10);
}

static void trigger_opacity_inc(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_opacity_inc_time < 120000) {
        xutil_log("HOTKEY", "Deduped opacity inc from %s (<120ms since last trigger)", source_desc);
        return;
    }
    s_last_opacity_inc_time = now;
    xutil_log("HOTKEY", "Alt+S triggered via %s -> Increasing opacity", source_desc);
    overlay_set_opacity(overlay_get_opacity() + 0.10);
}

static void trigger_fullscreen(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_fullscreen_time < 200000) {
        xutil_log("HOTKEY", "Deduped fullscreen from %s (<200ms since last trigger)", source_desc);
        return;
    }
    s_last_fullscreen_time = now;
    xutil_log("HOTKEY", "Alt+F triggered via %s -> Fullscreen mode", source_desc);
    overlay_set_fullscreen();
}

static void trigger_default_size(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_default_size_time < 200000) {
        xutil_log("HOTKEY", "Deduped default size from %s (<200ms since last trigger)", source_desc);
        return;
    }
    s_last_default_size_time = now;
    xutil_log("HOTKEY", "Alt+D triggered via %s -> Default size", source_desc);
    overlay_set_default_size();
}

static void trigger_mode_cycle(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_mode_cycle_time < 150000) {
        xutil_log("HOTKEY", "Deduped mode cycle from %s (<150ms since last trigger)", source_desc);
        return;
    }
    s_last_mode_cycle_time = now;
    xutil_log("HOTKEY", "Alt+M triggered via %s -> Cycling key mode", source_desc);
    input_cycle_key_mode();
}

static void trigger_ghost_write(const char *source_desc) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_ghost_write_time < 200000) {
        xutil_log("HOTKEY", "Deduped ghost write from %s (<200ms)", source_desc);
        return;
    }
    s_last_ghost_write_time = now;
    xutil_log("HOTKEY", "Alt+W triggered via %s -> Ghost Write", source_desc);
    ghost_writer_run();
}

static void handle_raw_key_press(int detail) {
    if (!s_hotkey_dpy) return;
    if (ghost_is_guard_active()) return;

    /* Deduplicate against active clipboard-shield grabs (<120ms) */
    gint64 now = g_get_monotonic_time();
    if (s_clipboard_shield_active && (now - s_last_shield_time < 120000) && ((KeyCode)detail == s_last_shield_keycode)) {
        xutil_log("HOTKEY", "Deduped raw keypress matching shielded keycode %d (<120ms)", detail);
        return;
    }

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

    /* Check if Alt is physically held down (strictly using XQueryKeymap) */
    bool alt_active = is_alt_physically_down(s_hotkey_dpy);
    if (!alt_active) {
        s_x_key_down = false;
        s_g_key_down = false;
        s_a_key_down = false;
        s_s_key_down = false;
        s_f_key_down = false;
        s_d_key_down = false;
        s_p_key_down = false;
        s_m_key_down = false;
        s_w_key_down = false;
    } else {
        KeyCode x_l, x_u, g_l, g_u, a_l, a_u, s_l, s_u, f_l, f_u, d_l, d_u, p_l, p_u, m_l, m_u, w_l, w_u;
        get_hotkey_keycodes(s_hotkey_dpy,
                            &x_l, &x_u, &g_l, &g_u, &a_l, &a_u, &s_l, &s_u,
                            &f_l, &f_u, &d_l, &d_u, &p_l, &p_u, &m_l, &m_u, &w_l, &w_u);

        KeyCode kc = (KeyCode)detail;
        bool is_x = (kc == x_l) || (x_u != 0 && kc == x_u);
        bool is_g = (kc == g_l) || (g_u != 0 && kc == g_u);
        bool is_a = (kc == a_l) || (a_u != 0 && kc == a_u);
        bool is_s = (kc == s_l) || (s_u != 0 && kc == s_u);
        bool is_f = (kc == f_l) || (f_u != 0 && kc == f_u);
        bool is_d = (kc == d_l) || (d_u != 0 && kc == d_u);
        bool is_p = (kc == p_l) || (p_u != 0 && kc == p_u);
        bool is_m = (kc == m_l) || (m_u != 0 && kc == m_u);
        bool is_w = (kc == w_l) || (w_u != 0 && kc == w_u);

        if (is_x) {
            trigger_hotkey_toggle("XInput2 Raw Event");
            return;
        }
        if (is_g) {
            trigger_ghost_toggle("XInput2 Raw Event");
            return;
        }
        if (is_a) {
            trigger_opacity_dec("XInput2 Raw Event");
            return;
        }
        if (is_s) {
            trigger_opacity_inc("XInput2 Raw Event");
            return;
        }
        if (is_f) {
            trigger_fullscreen("XInput2 Raw Event");
            return;
        }
        if (is_d) {
            trigger_default_size("XInput2 Raw Event");
            return;
        }
        if (is_p) {
            trigger_snap("XInput2 Raw Event");
            return;
        }
        if (is_m) {
            trigger_mode_cycle("XInput2 Raw Event");
            return;
        }
        if (is_w) {
            trigger_ghost_write("XInput2 Raw Event");
            return;
        }
    }

    /* Forward non-hotkey key to input subsystem */
    input_handle_raw_key(detail, true);
}

static void handle_raw_key_release(int detail) {
    if (!s_hotkey_dpy) return;

    /* Always track raw modifier releases so physical modifiers never stick */
    if (is_ctrl_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_ctrl_down = false;
    }
    if (is_shift_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_shift_down = false;
    }
    if (is_alt_keycode(s_hotkey_dpy, (KeyCode)detail)) {
        s_raw_alt_down = false;
        s_x_key_down = false;
        s_g_key_down = false;
        s_a_key_down = false;
        s_s_key_down = false;
        s_f_key_down = false;
        s_d_key_down = false;
        s_p_key_down = false;
        s_m_key_down = false;
        s_w_key_down = false;
    }

    if (ghost_is_guard_active()) return;

    /* Deduplicate against active clipboard-shield grabs (<120ms) */
    gint64 now = g_get_monotonic_time();
    if (s_clipboard_shield_active && (now - s_last_shield_time < 120000) && ((KeyCode)detail == s_last_shield_keycode)) {
        return;
    }

    KeyCode x_l, x_u, g_l, g_u, a_l, a_u, s_l, s_u, f_l, f_u, d_l, d_u, p_l, p_u, m_l, m_u, w_l, w_u;
    get_hotkey_keycodes(s_hotkey_dpy,
                        &x_l, &x_u, &g_l, &g_u, &a_l, &a_u, &s_l, &s_u,
                        &f_l, &f_u, &d_l, &d_u, &p_l, &p_u, &m_l, &m_u, &w_l, &w_u);

    KeyCode kc = (KeyCode)detail;
    if (kc == x_l || (x_u != 0 && kc == x_u)) s_x_key_down = false;
    if (kc == g_l || (g_u != 0 && kc == g_u)) s_g_key_down = false;
    if (kc == a_l || (a_u != 0 && kc == a_u)) s_a_key_down = false;
    if (kc == s_l || (s_u != 0 && kc == s_u)) s_s_key_down = false;
    if (kc == f_l || (f_u != 0 && kc == f_u)) s_f_key_down = false;
    if (kc == d_l || (d_u != 0 && kc == d_u)) s_d_key_down = false;
    if (kc == p_l || (p_u != 0 && kc == p_u)) s_p_key_down = false;
    if (kc == m_l || (m_u != 0 && kc == m_u)) s_m_key_down = false;
    if (kc == w_l || (w_u != 0 && kc == w_u)) s_w_key_down = false;

    /* Forward key release to input subsystem */
    input_handle_raw_key(detail, false);
}

static void register_passive_grabs(void);
static void unregister_passive_grabs(void);

void hotkey_install_clipboard_shield(void) {
    if (!s_hotkey_dpy || s_root == None) return;
    if (s_clipboard_shield_active) return;

    /* If Cinnamon's locate-pointer is enabled, Muffin holds a passive grab on Control_L
     * that intercepts Control keys before any other client on root. Temporarily disable it. */
    if (s_cinnamon_mouse_settings) {
        s_orig_locate_pointer = g_settings_get_boolean(s_cinnamon_mouse_settings, "locate-pointer");
        if (s_orig_locate_pointer) {
            g_settings_set_boolean(s_cinnamon_mouse_settings, "locate-pointer", FALSE);
            s_managed_locate_pointer = TRUE;
            g_settings_sync();
            g_usleep(15000); /* 15 ms to allow Muffin to process ungrab */
            xutil_log("HOTKEY", "Temporarily disengaged Cinnamon locate-pointer (releases WM Control_L grab).");
        }
    }

    KeyCode a_l = XKeysymToKeycode(s_hotkey_dpy, XK_a);
    KeyCode a_u = XKeysymToKeycode(s_hotkey_dpy, XK_A);
    KeyCode c_l = XKeysymToKeycode(s_hotkey_dpy, XK_c);
    KeyCode c_u = XKeysymToKeycode(s_hotkey_dpy, XK_C);
    KeyCode v_l = XKeysymToKeycode(s_hotkey_dpy, XK_v);
    KeyCode v_u = XKeysymToKeycode(s_hotkey_dpy, XK_V);
    KeyCode x_l = XKeysymToKeycode(s_hotkey_dpy, XK_x);
    KeyCode x_u = XKeysymToKeycode(s_hotkey_dpy, XK_X);

    KeyCode kcs[] = { a_l, a_u, c_l, c_u, v_l, v_u, x_l, x_u };
    size_t num_kcs = sizeof(kcs) / sizeof(kcs[0]);

    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        unsigned int mods = ControlMask | s_lock_combos[i];
        for (size_t k = 0; k < num_kcs; k++) {
            if (kcs[k] != 0) {
                XGrabKey(s_hotkey_dpy, kcs[k], mods, s_root, False, GrabModeAsync, GrabModeAsync);
            }
        }
    }
    XSync(s_hotkey_dpy, False);
    s_clipboard_shield_active = true;
    xutil_log("HOTKEY", "Installed active clipboard-key shield (Ctrl+A/C/V/X passive grabs engaged).");
}

void hotkey_remove_clipboard_shield(void) {
    if (!s_hotkey_dpy || s_root == None) return;
    if (!s_clipboard_shield_active) return;

    KeyCode a_l = XKeysymToKeycode(s_hotkey_dpy, XK_a);
    KeyCode a_u = XKeysymToKeycode(s_hotkey_dpy, XK_A);
    KeyCode c_l = XKeysymToKeycode(s_hotkey_dpy, XK_c);
    KeyCode c_u = XKeysymToKeycode(s_hotkey_dpy, XK_C);
    KeyCode v_l = XKeysymToKeycode(s_hotkey_dpy, XK_v);
    KeyCode v_u = XKeysymToKeycode(s_hotkey_dpy, XK_V);
    KeyCode x_l = XKeysymToKeycode(s_hotkey_dpy, XK_x);
    KeyCode x_u = XKeysymToKeycode(s_hotkey_dpy, XK_X);

    KeyCode kcs[] = { a_l, a_u, c_l, c_u, v_l, v_u, x_l, x_u };
    size_t num_kcs = sizeof(kcs) / sizeof(kcs[0]);

    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        unsigned int mods = ControlMask | s_lock_combos[i];
        for (size_t k = 0; k < num_kcs; k++) {
            if (kcs[k] != 0) {
                XUngrabKey(s_hotkey_dpy, kcs[k], mods, s_root);
            }
        }
    }
    XFlush(s_hotkey_dpy);
    s_clipboard_shield_active = false;
    xutil_log("HOTKEY", "Removed active clipboard-key shield (passive grabs released).");

    /* Restore Cinnamon locate-pointer setting */
    if (s_managed_locate_pointer && s_cinnamon_mouse_settings) {
        g_settings_set_boolean(s_cinnamon_mouse_settings, "locate-pointer", s_orig_locate_pointer);
        s_managed_locate_pointer = FALSE;
        g_settings_sync();
        xutil_log("HOTKEY", "Restored Cinnamon locate-pointer setting to %d.", s_orig_locate_pointer);
    }
}

bool hotkey_is_clipboard_shield_active(void) {
    return s_clipboard_shield_active;
}

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
                } else if (ev.xcookie.evtype == XI_RawButtonPress || ev.xcookie.evtype == XI_RawButtonRelease) {
                    XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
                    Window r_root, r_child;
                    int rx = 0, ry = 0, wx = 0, wy = 0;
                    unsigned int mask = 0;
                    XQueryPointer(s_hotkey_dpy, s_root, &r_root, &r_child, &rx, &ry, &wx, &wy, &mask);
                    input_handle_raw_button(raw->detail, (ev.xcookie.evtype == XI_RawButtonPress), rx, ry);
                }
                XFreeEventData(s_hotkey_dpy, &ev.xcookie);
            }
        } else if (ev.type == KeyPress) {
            if (ghost_is_guard_active()) continue;

            /* 1. Check if event is caught by active clipboard shield */
            if (s_clipboard_shield_active) {
                unsigned int active_mods = ev.xkey.state & (ControlMask | ShiftMask | Mod1Mask);
                if (active_mods == ControlMask) {
                    KeyCode a_l = XKeysymToKeycode(s_hotkey_dpy, XK_a);
                    KeyCode a_u = XKeysymToKeycode(s_hotkey_dpy, XK_A);
                    KeyCode c_l = XKeysymToKeycode(s_hotkey_dpy, XK_c);
                    KeyCode c_u = XKeysymToKeycode(s_hotkey_dpy, XK_C);
                    KeyCode v_l = XKeysymToKeycode(s_hotkey_dpy, XK_v);
                    KeyCode v_u = XKeysymToKeycode(s_hotkey_dpy, XK_V);
                    KeyCode x_l = XKeysymToKeycode(s_hotkey_dpy, XK_x);
                    KeyCode x_u = XKeysymToKeycode(s_hotkey_dpy, XK_X);

                    KeyCode kc = ev.xkey.keycode;
                    if (kc == a_l || (a_u != 0 && kc == a_u)) {
                        s_last_shield_time = g_get_monotonic_time();
                        s_last_shield_keycode = kc;
                        xutil_log("HOTKEY", "Shield grab caught Ctrl+A -> dispatching SelectAll to overlay");
                        overlay_execute_clipboard_action(CLIPBOARD_ACTION_SELECT_ALL);
                        continue;
                    } else if (kc == c_l || (c_u != 0 && kc == c_u)) {
                        s_last_shield_time = g_get_monotonic_time();
                        s_last_shield_keycode = kc;
                        xutil_log("HOTKEY", "Shield grab caught Ctrl+C -> dispatching Copy to overlay");
                        overlay_execute_clipboard_action(CLIPBOARD_ACTION_COPY);
                        continue;
                    } else if (kc == v_l || (v_u != 0 && kc == v_u)) {
                        s_last_shield_time = g_get_monotonic_time();
                        s_last_shield_keycode = kc;
                        xutil_log("HOTKEY", "Shield grab caught Ctrl+V -> dispatching Paste to overlay");
                        overlay_execute_clipboard_action(CLIPBOARD_ACTION_PASTE);
                        continue;
                    } else if (kc == x_l || (x_u != 0 && kc == x_u)) {
                        s_last_shield_time = g_get_monotonic_time();
                        s_last_shield_keycode = kc;
                        xutil_log("HOTKEY", "Shield grab caught Ctrl+X -> dispatching Cut to overlay");
                        overlay_execute_clipboard_action(CLIPBOARD_ACTION_CUT);
                        continue;
                    }
                }
            }

            /* 2. Global hotkeys (Alt+X / G / A / S / F / D / P / M / W) */
            if ((ev.xkey.state & Mod1Mask) == 0 || !is_alt_physically_down(s_hotkey_dpy)) {
                continue;
            }

            KeyCode x_l, x_u, g_l, g_u, a_l, a_u, s_l, s_u, f_l, f_u, d_l, d_u, p_l, p_u, m_l, m_u, w_l, w_u;
            get_hotkey_keycodes(s_hotkey_dpy,
                                &x_l, &x_u, &g_l, &g_u, &a_l, &a_u, &s_l, &s_u,
                                &f_l, &f_u, &d_l, &d_u, &p_l, &p_u, &m_l, &m_u, &w_l, &w_u);

            KeyCode kc = ev.xkey.keycode;
            if (kc == x_l || (x_u != 0 && kc == x_u)) {
                s_x_key_down = true;
                trigger_hotkey_toggle("Passive XGrabKey");
            } else if (kc == g_l || (g_u != 0 && kc == g_u)) {
                s_g_key_down = true;
                trigger_ghost_toggle("Passive XGrabKey");
            } else if (kc == a_l || (a_u != 0 && kc == a_u)) {
                s_a_key_down = true;
                trigger_opacity_dec("Passive XGrabKey");
            } else if (kc == s_l || (s_u != 0 && kc == s_u)) {
                s_s_key_down = true;
                trigger_opacity_inc("Passive XGrabKey");
            } else if (kc == f_l || (f_u != 0 && kc == f_u)) {
                s_f_key_down = true;
                trigger_fullscreen("Passive XGrabKey");
            } else if (kc == d_l || (d_u != 0 && kc == d_u)) {
                s_d_key_down = true;
                trigger_default_size("Passive XGrabKey");
            } else if (kc == p_l || (p_u != 0 && kc == p_u)) {
                s_p_key_down = true;
                trigger_snap("Passive XGrabKey");
            } else if (kc == m_l || (m_u != 0 && kc == m_u)) {
                s_m_key_down = true;
                trigger_mode_cycle("Passive XGrabKey");
            } else if (kc == w_l || (w_u != 0 && kc == w_u)) {
                s_w_key_down = true;
                trigger_ghost_write("Passive XGrabKey");
            }
        } else if (ev.type == KeyRelease) {
            if (ghost_is_guard_active()) continue;

            KeyCode x_l, x_u, g_l, g_u, a_l, a_u, s_l, s_u, f_l, f_u, d_l, d_u, p_l, p_u, m_l, m_u, w_l, w_u;
            get_hotkey_keycodes(s_hotkey_dpy,
                                &x_l, &x_u, &g_l, &g_u, &a_l, &a_u, &s_l, &s_u,
                                &f_l, &f_u, &d_l, &d_u, &p_l, &p_u, &m_l, &m_u, &w_l, &w_u);

            KeyCode kc = ev.xkey.keycode;
            if (kc == x_l || (x_u != 0 && kc == x_u)) {
                s_x_key_down = false;
            } else if (kc == g_l || (g_u != 0 && kc == g_u)) {
                s_g_key_down = false;
            } else if (kc == a_l || (a_u != 0 && kc == a_u)) {
                s_a_key_down = false;
            } else if (kc == s_l || (s_u != 0 && kc == s_u)) {
                s_s_key_down = false;
            } else if (kc == f_l || (f_u != 0 && kc == f_u)) {
                s_f_key_down = false;
            } else if (kc == d_l || (d_u != 0 && kc == d_u)) {
                s_d_key_down = false;
            } else if (kc == p_l || (p_u != 0 && kc == p_u)) {
                s_p_key_down = false;
            } else if (kc == m_l || (m_u != 0 && kc == m_u)) {
                s_m_key_down = false;
            } else if (kc == w_l || (w_u != 0 && kc == w_u)) {
                s_w_key_down = false;
            }
        } else if (ev.type == MappingNotify) {
            XRefreshKeyboardMapping(&ev.xmapping);
            xutil_log("HOTKEY", "Keyboard mapping updated by server. Re-registering grabs.");
            unregister_passive_grabs();
            register_passive_grabs();
            if (s_clipboard_shield_active) {
                hotkey_remove_clipboard_shield();
                hotkey_install_clipboard_shield();
            }
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

    KeyCode x_l, x_u, g_l, g_u, a_l, a_u, s_l, s_u, f_l, f_u, d_l, d_u, p_l, p_u, m_l, m_u, w_l, w_u;
    get_hotkey_keycodes(s_hotkey_dpy,
                        &x_l, &x_u, &g_l, &g_u, &a_l, &a_u, &s_l, &s_u,
                        &f_l, &f_u, &d_l, &d_u, &p_l, &p_u, &m_l, &m_u, &w_l, &w_u);

    KeyCode hotkey_kcs[] = {
        x_l, x_u,
        g_l, g_u,
        a_l, a_u,
        s_l, s_u,
        f_l, f_u,
        d_l, d_u,
        p_l, p_u,
        m_l, m_u,
        w_l, w_u
    };
    size_t num_kcs = sizeof(hotkey_kcs) / sizeof(hotkey_kcs[0]);

    unsigned int base_mods = Mod1Mask; /* Alt */

    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        unsigned int mods = base_mods | s_lock_combos[i];
        for (size_t k = 0; k < num_kcs; k++) {
            if (hotkey_kcs[k] != 0) {
                XGrabKey(s_hotkey_dpy, hotkey_kcs[k], mods, s_root, False, GrabModeAsync, GrabModeAsync);
            }
        }
    }
    XSync(s_hotkey_dpy, False);
    xutil_log("HOTKEY", "Registered passive XGrabKey for Alt+x/g/a/s/f/d/p/m/w across lock variations.");
}

static void unregister_passive_grabs(void) {
    if (!s_hotkey_dpy || s_root == None) return;

    KeyCode x_l, x_u, g_l, g_u, a_l, a_u, s_l, s_u, f_l, f_u, d_l, d_u, p_l, p_u, m_l, m_u, w_l, w_u;
    get_hotkey_keycodes(s_hotkey_dpy,
                        &x_l, &x_u, &g_l, &g_u, &a_l, &a_u, &s_l, &s_u,
                        &f_l, &f_u, &d_l, &d_u, &p_l, &p_u, &m_l, &m_u, &w_l, &w_u);

    KeyCode hotkey_kcs[] = {
        x_l, x_u,
        g_l, g_u,
        a_l, a_u,
        s_l, s_u,
        f_l, f_u,
        d_l, d_u,
        p_l, p_u,
        m_l, m_u,
        w_l, w_u
    };
    size_t num_kcs = sizeof(hotkey_kcs) / sizeof(hotkey_kcs[0]);

    unsigned int base_mods = Mod1Mask; /* Alt */

    for (size_t i = 0; i < NUM_LOCK_COMBOS; i++) {
        unsigned int mods = base_mods | s_lock_combos[i];
        for (size_t k = 0; k < num_kcs; k++) {
            if (hotkey_kcs[k] != 0) {
                XUngrabKey(s_hotkey_dpy, hotkey_kcs[k], mods, s_root);
            }
        }
    }
    XFlush(s_hotkey_dpy);
    xutil_log("HOTKEY", "Released all passive XGrabKey grabs.");
}

void hotkey_reload_grabs(void) {
    if (!s_hotkey_dpy || s_root == None) return;
    unregister_passive_grabs();
    register_passive_grabs();
    if (s_clipboard_shield_active) {
        hotkey_remove_clipboard_shield();
        hotkey_install_clipboard_shield();
    }
    xutil_log("HOTKEY", "Reloaded passive grabs based on updated configuration.");
}

gboolean hotkey_init(void) {
    /* Initialize Cinnamon mouse settings to manage locate-pointer grab during overlay typing */
    GSettingsSchemaSource *src = g_settings_schema_source_get_default();
    if (src) {
        GSettingsSchema *schema = g_settings_schema_source_lookup(src, "org.cinnamon.desktop.peripherals.mouse", TRUE);
        if (schema) {
            s_cinnamon_mouse_settings = g_settings_new("org.cinnamon.desktop.peripherals.mouse");
            g_settings_schema_unref(schema);
        }
    }

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

    /* 3. Select XI_RawKeyPress, XI_RawKeyRelease, XI_RawButtonPress, XI_RawButtonRelease on root window */
    XIEventMask mask;
    unsigned char mask_bits[XIMaskLen(XI_RawButtonRelease)] = {0};
    mask.deviceid = XIAllMasterDevices;
    mask.mask_len = sizeof(mask_bits);
    mask.mask = mask_bits;
    XISetMask(mask_bits, XI_RawKeyPress);
    XISetMask(mask_bits, XI_RawKeyRelease);
    XISetMask(mask_bits, XI_RawButtonPress);
    XISetMask(mask_bits, XI_RawButtonRelease);

    if (XISelectEvents(s_hotkey_dpy, s_root, &mask, 1) != Success) {
        xutil_log_err("HOTKEY", "Failed to select XI2 raw events on root window!");
        XCloseDisplay(s_hotkey_dpy);
        s_hotkey_dpy = NULL;
        return FALSE;
    }
    XSync(s_hotkey_dpy, False);
    xutil_log("HOTKEY", "Selected XI_RawKeyPress/Release and XI_RawButtonPress/Release on root window.");

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

    hotkey_remove_clipboard_shield();
    unregister_passive_grabs();

    if (s_hotkey_source) {
        g_source_destroy(s_hotkey_source);
        g_source_unref(s_hotkey_source);
        s_hotkey_source = NULL;
    }

    if (s_cinnamon_mouse_settings) {
        g_object_unref(s_cinnamon_mouse_settings);
        s_cinnamon_mouse_settings = NULL;
    }

    XCloseDisplay(s_hotkey_dpy);
    s_hotkey_dpy = NULL;
    s_root = None;
    xutil_log("HOTKEY", "Hotkey system cleaned up.");
}

void hotkey_sync_raw_modifiers(void) {
    if (!s_hotkey_dpy) return;
    char keys[32] = {0};
    XQueryKeymap(s_hotkey_dpy, keys);

    KeyCode ctrl_l = XKeysymToKeycode(s_hotkey_dpy, XK_Control_L);
    KeyCode ctrl_r = XKeysymToKeycode(s_hotkey_dpy, XK_Control_R);
    s_raw_ctrl_down = (ctrl_l && is_keycode_down(keys, ctrl_l)) || (ctrl_r && is_keycode_down(keys, ctrl_r));

    KeyCode shift_l = XKeysymToKeycode(s_hotkey_dpy, XK_Shift_L);
    KeyCode shift_r = XKeysymToKeycode(s_hotkey_dpy, XK_Shift_R);
    s_raw_shift_down = (shift_l && is_keycode_down(keys, shift_l)) || (shift_r && is_keycode_down(keys, shift_r));

    KeyCode alt_l = XKeysymToKeycode(s_hotkey_dpy, XK_Alt_L);
    KeyCode alt_r = XKeysymToKeycode(s_hotkey_dpy, XK_Alt_R);
    KeyCode meta_l = XKeysymToKeycode(s_hotkey_dpy, XK_Meta_L);
    KeyCode meta_r = XKeysymToKeycode(s_hotkey_dpy, XK_Meta_R);
    s_raw_alt_down = (alt_l && is_keycode_down(keys, alt_l)) || (alt_r && is_keycode_down(keys, alt_r)) ||
                     (meta_l && is_keycode_down(keys, meta_l)) || (meta_r && is_keycode_down(keys, meta_r));

    s_x_key_down = false;
    s_g_key_down = false;
    s_a_key_down = false;
    s_s_key_down = false;
    s_f_key_down = false;
    s_d_key_down = false;
    s_p_key_down = false;
    s_m_key_down = false;
    s_w_key_down = false;
}

void hotkey_drain_pending_events(void) {
    if (!s_hotkey_dpy) return;
    XSync(s_hotkey_dpy, False);
    while (XPending(s_hotkey_dpy) > 0) {
        XEvent ev;
        XNextEvent(s_hotkey_dpy, &ev);
    }
    hotkey_sync_raw_modifiers();
}
