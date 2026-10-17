#include "ghost.h"
#include "config.h"
#include "window.h"
#include "input.h"
#include "hotkey.h"
#include "xutil.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <gdk/gdk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Display  *s_ghost_dpy = NULL;
static bool      s_guard_active = false;
static guint     s_guard_timer_id = 0;
static bool      s_is_typing = false;
static guint     s_typing_timer_id = 0;
static gint64    s_last_trigger_time = 0;

typedef struct {
    KeyCode kc;
    unsigned int mod;
    bool is_spare;
} KeyMappingResult;

typedef struct {
    char        *text;
    const char  *curr_ptr;
    size_t       total_chars;
    size_t       typed_chars;
    char         app_name[128];
    bool         caps_was_on;
    KeyCode      shift_kc;
    KeyCode      altgr_kc;
    KeyCode      spare_kc;
    int          syms_per_kc;
} GhostTypingContext;

static GhostTypingContext *s_typing_ctx = NULL;

static inline bool is_keycode_down(const char keys[32], KeyCode kc) {
    if (kc == 0) return false;
    return (keys[kc / 8] & (1 << (kc % 8))) != 0;
}

bool ghost_init(void) {
    if (s_ghost_dpy) return true;

    s_ghost_dpy = XOpenDisplay(NULL);
    if (!s_ghost_dpy) {
        xutil_log_err("GHOST", "Failed to open dedicated X11 display connection.");
        return false;
    }

    int event_base = 0, error_base = 0, major = 0, minor = 0;
    if (!XTestQueryExtension(s_ghost_dpy, &event_base, &error_base, &major, &minor)) {
        xutil_log_err("GHOST", "XTEST extension not supported by X server.");
        XCloseDisplay(s_ghost_dpy);
        s_ghost_dpy = NULL;
        return false;
    }

    xutil_log("GHOST", "Ghost Writer subsystem initialized on dedicated Display (XTEST %d.%d).", major, minor);
    return true;
}

static gboolean on_guard_finish_timeout(gpointer user_data) {
    (void)user_data;
    s_guard_timer_id = 0;
    s_guard_active = false;
    hotkey_sync_raw_modifiers();
    xutil_log("GHOST", "Self-echo guard disengaged (+25ms settling complete).");
    return G_SOURCE_REMOVE;
}

static void disengage_guard_delayed(void) {
    if (s_ghost_dpy) {
        XSync(s_ghost_dpy, False);
    }
    hotkey_drain_pending_events();

    if (s_guard_timer_id > 0) {
        g_source_remove(s_guard_timer_id);
    }
    s_guard_timer_id = g_timeout_add(25, on_guard_finish_timeout, NULL);
}

void ghost_cleanup(void) {
    if (s_typing_timer_id > 0) {
        g_source_remove(s_typing_timer_id);
        s_typing_timer_id = 0;
    }
    if (s_typing_ctx) {
        g_free(s_typing_ctx->text);
        g_free(s_typing_ctx);
        s_typing_ctx = NULL;
    }
    if (s_guard_timer_id > 0) {
        g_source_remove(s_guard_timer_id);
        s_guard_timer_id = 0;
    }
    s_guard_active = false;
    s_is_typing = false;

    if (s_ghost_dpy) {
        XCloseDisplay(s_ghost_dpy);
        s_ghost_dpy = NULL;
    }
    xutil_log("GHOST", "Ghost Writer subsystem cleaned up.");
}

bool ghost_is_guard_active(void) {
    return s_guard_active;
}

bool ghost_is_typing(void) {
    return s_is_typing;
}

static KeyCode find_spare_keycode(int *syms_per_kc_out) {
    if (!s_ghost_dpy) return 0;
    int min_kc = 0, max_kc = 0;
    XDisplayKeycodes(s_ghost_dpy, &min_kc, &max_kc);

    int syms_per_kc = 0;
    KeySym *mapping = XGetKeyboardMapping(s_ghost_dpy, min_kc, max_kc - min_kc + 1, &syms_per_kc);
    if (!mapping) return 0;

    if (syms_per_kc_out) *syms_per_kc_out = syms_per_kc;

    KeyCode spare = 0;
    for (int kc = max_kc; kc >= min_kc; kc--) {
        bool all_empty = true;
        for (int col = 0; col < syms_per_kc; col++) {
            if (mapping[(kc - min_kc) * syms_per_kc + col] != NoSymbol) {
                all_empty = false;
                break;
            }
        }
        if (all_empty) {
            spare = (KeyCode)kc;
            break;
        }
    }
    XFree(mapping);
    return spare;
}

static void restore_spare_keycode(KeyCode spare_kc, int syms_per_kc) {
    if (!s_ghost_dpy || spare_kc == 0) return;
    KeySym empty_syms[32] = {0};
    XChangeKeyboardMapping(s_ghost_dpy, spare_kc, syms_per_kc, empty_syms, 1);
    XSync(s_ghost_dpy, False);
}

static KeyCode find_altgr_keycode(void) {
    if (!s_ghost_dpy) return 0;
    KeyCode kc = XKeysymToKeycode(s_ghost_dpy, XK_ISO_Level3_Shift);
    if (kc != 0) return kc;
    kc = XKeysymToKeycode(s_ghost_dpy, XK_Mode_switch);
    if (kc != 0) return kc;

    XModifierKeymap *mm = XGetModifierMapping(s_ghost_dpy);
    if (mm) {
        for (int i = 0; i < mm->max_keypermod; i++) {
            KeyCode k = mm->modifiermap[Mod5MapIndex * mm->max_keypermod + i];
            if (k != 0) {
                XFreeModifiermap(mm);
                return k;
            }
        }
        XFreeModifiermap(mm);
    }
    return 0;
}

static bool resolve_keysym_on_layout(KeySym sym, KeyMappingResult *res, KeyCode spare_kc, int syms_per_kc) {
    if (!s_ghost_dpy) return false;
    memset(res, 0, sizeof(*res));

    int min_kc = 0, max_kc = 0;
    XDisplayKeycodes(s_ghost_dpy, &min_kc, &max_kc);

    int actual_syms_per_kc = 0;
    KeySym *mapping = XGetKeyboardMapping(s_ghost_dpy, min_kc, max_kc - min_kc + 1, &actual_syms_per_kc);
    if (!mapping) return false;

    bool found = false;
    /* 1. Level 0: unshifted */
    for (int kc = min_kc; kc <= max_kc; kc++) {
        if ((KeyCode)kc == spare_kc) continue;
        if (mapping[(kc - min_kc) * actual_syms_per_kc + 0] == sym) {
            res->kc = (KeyCode)kc;
            res->mod = 0;
            res->is_spare = false;
            found = true;
            break;
        }
    }

    /* 2. Level 1: Shift */
    if (!found && actual_syms_per_kc > 1) {
        for (int kc = min_kc; kc <= max_kc; kc++) {
            if ((KeyCode)kc == spare_kc) continue;
            if (mapping[(kc - min_kc) * actual_syms_per_kc + 1] == sym) {
                res->kc = (KeyCode)kc;
                res->mod = ShiftMask;
                res->is_spare = false;
                found = true;
                break;
            }
        }
    }

    /* 3. Level 2: AltGr (ISO_Level3_Shift) */
    if (!found && actual_syms_per_kc > 2) {
        for (int kc = min_kc; kc <= max_kc; kc++) {
            if ((KeyCode)kc == spare_kc) continue;
            if (mapping[(kc - min_kc) * actual_syms_per_kc + 2] == sym) {
                res->kc = (KeyCode)kc;
                res->mod = Mod5Mask;
                res->is_spare = false;
                found = true;
                break;
            }
        }
    }

    /* 4. Level 3: Shift + AltGr */
    if (!found && actual_syms_per_kc > 3) {
        for (int kc = min_kc; kc <= max_kc; kc++) {
            if ((KeyCode)kc == spare_kc) continue;
            if (mapping[(kc - min_kc) * actual_syms_per_kc + 3] == sym) {
                res->kc = (KeyCode)kc;
                res->mod = ShiftMask | Mod5Mask;
                res->is_spare = false;
                found = true;
                break;
            }
        }
    }

    XFree(mapping);

    if (found) return true;

    /* Character is not on current layout -> temporarily map spare keycode */
    if (spare_kc != 0) {
        KeySym new_syms[32] = {0};
        new_syms[0] = sym;
        XChangeKeyboardMapping(s_ghost_dpy, spare_kc, syms_per_kc, new_syms, 1);
        XSync(s_ghost_dpy, False);
        res->kc = spare_kc;
        res->mod = 0;
        res->is_spare = true;
        return true;
    }

    return false;
}

static bool inject_character(gunichar c, const AppConfig *cfg, KeyCode shift_kc, KeyCode altgr_kc, KeyCode spare_kc, int syms_per_kc) {
    if (c == '\n') {
        if (g_ascii_strcasecmp(cfg->ghost_newline, "skip") == 0) return true;
        if (g_ascii_strcasecmp(cfg->ghost_newline, "space") == 0) c = ' ';
    } else if (c == '\t') {
        if (g_ascii_strcasecmp(cfg->ghost_tab, "skip") == 0) return true;
        if (g_ascii_strcasecmp(cfg->ghost_tab, "space") == 0) c = ' ';
    }

    KeySym sym = NoSymbol;
    if (c == '\n') {
        sym = XK_Return;
    } else if (c == '\t') {
        sym = XK_Tab;
    } else {
        sym = gdk_unicode_to_keyval(c);
        if (sym == GDK_KEY_VoidSymbol || sym == 0) {
            if (c < 0x100) sym = (KeySym)c;
            else sym = (KeySym)(0x01000000 | c);
        }
    }

    KeyMappingResult res;
    if (!resolve_keysym_on_layout(sym, &res, spare_kc, syms_per_kc)) {
        return false;
    }

    if (res.mod & ShiftMask) {
        if (shift_kc) XTestFakeKeyEvent(s_ghost_dpy, shift_kc, True, 1);
    }
    if (res.mod & Mod5Mask) {
        if (altgr_kc) XTestFakeKeyEvent(s_ghost_dpy, altgr_kc, True, 1);
    }

    XTestFakeKeyEvent(s_ghost_dpy, res.kc, True, 1);
    XTestFakeKeyEvent(s_ghost_dpy, res.kc, False, 2);

    if (res.mod & Mod5Mask) {
        if (altgr_kc) XTestFakeKeyEvent(s_ghost_dpy, altgr_kc, False, 1);
    }
    if (res.mod & ShiftMask) {
        if (shift_kc) XTestFakeKeyEvent(s_ghost_dpy, shift_kc, False, 1);
    }

    if (res.is_spare) {
        restore_spare_keycode(spare_kc, syms_per_kc);
    }

    return true;
}

static Window find_target_window(char *app_name_out, size_t app_name_size) {
    if (app_name_out && app_name_size > 0) {
        app_name_out[0] = '\0';
    }
    if (!s_ghost_dpy) return None;

    Window focus_win = None;
    int revert_to = 0;
    XGetInputFocus(s_ghost_dpy, &focus_win, &revert_to);

    if (focus_win == None || focus_win == PointerRoot) {
        return None;
    }

    Window our_xid = overlay_get_xid();
    if (our_xid != None) {
        if (focus_win == our_xid) return None;

        Window root = None, parent = None, *children = NULL;
        unsigned int nchildren = 0;
        Window curr = focus_win;
        while (curr != None) {
            if (curr == our_xid) return None;
            if (XQueryTree(s_ghost_dpy, curr, &root, &parent, &children, &nchildren) == 0) {
                break;
            }
            if (children) {
                XFree(children);
                children = NULL;
            }
            if (parent == root || parent == curr) break;
            curr = parent;
        }
    }

    /* Query _NET_ACTIVE_WINDOW for window metadata */
    Window target_app_win = focus_win;
    Window root = DefaultRootWindow(s_ghost_dpy);

    Atom net_active = XInternAtom(s_ghost_dpy, "_NET_ACTIVE_WINDOW", True);
    if (net_active != None) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems = 0, bytes_after = 0;
        unsigned char *prop = NULL;
        if (XGetWindowProperty(s_ghost_dpy, root, net_active, 0, 1, False,
                               XA_WINDOW, &actual_type, &actual_format,
                               &nitems, &bytes_after, &prop) == Success && prop) {
            if (nitems > 0) {
                Window active_w = *(Window *)prop;
                if (active_w != None && active_w != our_xid) {
                    target_app_win = active_w;
                }
            }
            XFree(prop);
        }
    }

    XClassHint ch;
    memset(&ch, 0, sizeof(ch));
    bool got_class = false;
    Window probe = target_app_win;
    while (probe != None && probe != root) {
        if (XGetClassHint(s_ghost_dpy, probe, &ch) != 0) {
            got_class = true;
            break;
        }
        Window r2, p2, *c2 = NULL;
        unsigned int nc2 = 0;
        if (XQueryTree(s_ghost_dpy, probe, &r2, &p2, &c2, &nc2) == 0) break;
        if (c2) XFree(c2);
        if (p2 == probe || p2 == root) break;
        probe = p2;
    }

    if (got_class) {
        if (app_name_out && app_name_size > 0) {
            const char *name = ch.res_class ? ch.res_class : (ch.res_name ? ch.res_name : "app");
            snprintf(app_name_out, app_name_size, "%s", name);
        }
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
    } else if (app_name_out && app_name_size > 0) {
        snprintf(app_name_out, app_name_size, "app");
    }

    return focus_win;
}

static gboolean on_ghost_typing_step(gpointer user_data) {
    GhostTypingContext *ctx = (GhostTypingContext *)user_data;
    if (!ctx || !s_is_typing) return G_SOURCE_REMOVE;

    const AppConfig *cfg = config_get();

    if (!ctx->curr_ptr || *ctx->curr_ptr == '\0') {
        /* Finished typing all characters */
        s_is_typing = false;
        s_typing_timer_id = 0;

        if (ctx->caps_was_on) {
            XkbLockModifiers(s_ghost_dpy, XkbUseCoreKbd, LockMask, LockMask);
            XSync(s_ghost_dpy, False);
        }
        restore_spare_keycode(ctx->spare_kc, ctx->syms_per_kc);
        disengage_guard_delayed();

        overlay_set_ghost_button_state(GHOST_BTN_DONE);

        char toast_msg[256];
        if (ctx->app_name[0] != '\0' && strcmp(ctx->app_name, "app") != 0) {
            snprintf(toast_msg, sizeof(toast_msg), "Typed %zu chars in %s", ctx->typed_chars, ctx->app_name);
        } else {
            snprintf(toast_msg, sizeof(toast_msg), "Typed %zu chars", ctx->typed_chars);
        }
        overlay_show_toast(toast_msg);
        xutil_log("GHOST", "Finished typing %zu characters.", ctx->typed_chars);

        g_free(ctx->text);
        g_free(ctx);
        s_typing_ctx = NULL;
        return G_SOURCE_REMOVE;
    }

    gunichar c = g_utf8_get_char(ctx->curr_ptr);
    inject_character(c, cfg, ctx->shift_kc, ctx->altgr_kc, ctx->spare_kc, ctx->syms_per_kc);
    ctx->typed_chars++;
    ctx->curr_ptr = g_utf8_next_char(ctx->curr_ptr);

    XFlush(s_ghost_dpy);
    return G_SOURCE_CONTINUE;
}

void ghost_writer_cancel(void) {
    if (!s_is_typing) return;

    xutil_log("GHOST", "Cancelling active Ghost Writer typing.");
    if (s_typing_timer_id > 0) {
        g_source_remove(s_typing_timer_id);
        s_typing_timer_id = 0;
    }

    if (s_typing_ctx) {
        if (s_typing_ctx->shift_kc) {
            XTestFakeKeyEvent(s_ghost_dpy, s_typing_ctx->shift_kc, False, CurrentTime);
        }
        if (s_typing_ctx->altgr_kc) {
            XTestFakeKeyEvent(s_ghost_dpy, s_typing_ctx->altgr_kc, False, CurrentTime);
        }
        if (s_typing_ctx->caps_was_on) {
            XkbLockModifiers(s_ghost_dpy, XkbUseCoreKbd, LockMask, LockMask);
            XSync(s_ghost_dpy, False);
        }
        restore_spare_keycode(s_typing_ctx->spare_kc, s_typing_ctx->syms_per_kc);
        g_free(s_typing_ctx->text);
        g_free(s_typing_ctx);
        s_typing_ctx = NULL;
    }

    disengage_guard_delayed();
    s_is_typing = false;

    overlay_set_ghost_button_state(GHOST_BTN_FAILED);
    overlay_show_toast("Typing cancelled");
}

void ghost_writer_run(void) {
    gint64 now = g_get_monotonic_time();
    if (now - s_last_trigger_time < 200000) {
        xutil_log("GHOST", "Debounced double trigger (<200ms).");
        return;
    }
    s_last_trigger_time = now;

    if (s_is_typing) {
        ghost_writer_cancel();
        return;
    }

    if (!ghost_init()) {
        overlay_set_ghost_button_state(GHOST_BTN_FAILED);
        overlay_show_toast("Ghost init failed");
        return;
    }

    /* 1. Retrieve text from overlay test entry */
    const char *text = overlay_get_test_entry_text();
    if (!text || text[0] == '\0') {
        xutil_log("GHOST", "Triggered with empty input box.");
        overlay_set_ghost_button_state(GHOST_BTN_FAILED);
        overlay_show_toast("Nothing to type");
        return;
    }

    const AppConfig *cfg = config_get();
    size_t char_count = g_utf8_strlen(text, -1);
    int max_chars = (cfg->ghost_max_chars > 0) ? cfg->ghost_max_chars : 2000;
    if ((int)char_count > max_chars) {
        xutil_log("GHOST", "Text exceeds limit (%zu > %d chars).", char_count, max_chars);
        overlay_set_ghost_button_state(GHOST_BTN_FAILED);
        char toast_err[128];
        snprintf(toast_err, sizeof(toast_err), "Text too long (max %d)", max_chars);
        overlay_show_toast(toast_err);
        return;
    }

    /* 2. Release grabs and set typing target to NONE */
    input_release_grab();
    hotkey_remove_clipboard_shield();
    input_set_typing_target(TYPING_TARGET_NONE);

    /* 3. Find target focused window */
    char app_name[128] = {0};
    Window target_win = find_target_window(app_name, sizeof(app_name));
    if (target_win == None) {
        xutil_log("GHOST", "No valid external focus window found.");
        overlay_set_ghost_button_state(GHOST_BTN_FAILED);
        overlay_show_toast("No focused app");
        return;
    }

    xutil_log("GHOST", "Target window identified: 0x%lx (App: '%s'), chars to type: %zu",
              (unsigned long)target_win, app_name, char_count);

    /* 4. Prepare CapsLock state */
    XkbStateRec xkb_state;
    memset(&xkb_state, 0, sizeof(xkb_state));
    XkbGetState(s_ghost_dpy, XkbUseCoreKbd, &xkb_state);
    bool caps_was_on = (xkb_state.locked_mods & LockMask) != 0;
    if (caps_was_on) {
        XkbLockModifiers(s_ghost_dpy, XkbUseCoreKbd, LockMask, 0);
        XSync(s_ghost_dpy, False);
        xutil_log("GHOST", "Temporarily disengaged CapsLock.");
    }

    /* 5. Prepare modifier and spare keycodes */
    KeyCode shift_kc = XKeysymToKeycode(s_ghost_dpy, XK_Shift_L);
    if (shift_kc == 0) shift_kc = XKeysymToKeycode(s_ghost_dpy, XK_Shift_R);
    KeyCode altgr_kc = find_altgr_keycode();
    int syms_per_kc = 0;
    KeyCode spare_kc = find_spare_keycode(&syms_per_kc);

    /* 6. Allow ~60ms settling time so physical hotkey keys (Ctrl+Shift+Alt+W) are released */
    g_usleep(60000);

    /* Release any held Control / Alt / Meta keys to prevent hotkey bleed */
    KeyCode ctrl_l = XKeysymToKeycode(s_ghost_dpy, XK_Control_L);
    KeyCode ctrl_r = XKeysymToKeycode(s_ghost_dpy, XK_Control_R);
    KeyCode alt_l  = XKeysymToKeycode(s_ghost_dpy, XK_Alt_L);
    KeyCode alt_r  = XKeysymToKeycode(s_ghost_dpy, XK_Alt_R);
    KeyCode meta_l = XKeysymToKeycode(s_ghost_dpy, XK_Meta_L);
    KeyCode meta_r = XKeysymToKeycode(s_ghost_dpy, XK_Meta_R);
    if (ctrl_l) XTestFakeKeyEvent(s_ghost_dpy, ctrl_l, False, 0);
    if (ctrl_r) XTestFakeKeyEvent(s_ghost_dpy, ctrl_r, False, 0);
    if (alt_l)  XTestFakeKeyEvent(s_ghost_dpy, alt_l,  False, 0);
    if (alt_r)  XTestFakeKeyEvent(s_ghost_dpy, alt_r,  False, 0);
    if (meta_l) XTestFakeKeyEvent(s_ghost_dpy, meta_l, False, 0);
    if (meta_r) XTestFakeKeyEvent(s_ghost_dpy, meta_r, False, 0);
    XFlush(s_ghost_dpy);

    /* Wait briefly (up to 120ms) for physical trigger keys (Ctrl, Alt) to release */
    for (int i = 0; i < 24; i++) {
        char keys[32] = {0};
        XQueryKeymap(s_ghost_dpy, keys);
        bool mod_down = (ctrl_l && is_keycode_down(keys, ctrl_l)) ||
                        (ctrl_r && is_keycode_down(keys, ctrl_r)) ||
                        (alt_l  && is_keycode_down(keys, alt_l))  ||
                        (alt_r  && is_keycode_down(keys, alt_r));
        if (!mod_down) break;
        g_usleep(5000); /* 5ms */
    }

    /* 7. Engage self-echo guard BEFORE first event */
    if (s_guard_timer_id > 0) {
        g_source_remove(s_guard_timer_id);
        s_guard_timer_id = 0;
    }
    s_guard_active = true;
    xutil_log("GHOST", "Self-echo guard ENGAGED.");

    /* 7. Dispatch typing: Instant or Delayed */
    if (cfg->ghost_delay_ms <= 0) {
        overlay_set_ghost_button_state(GHOST_BTN_TYPING);

        size_t typed = 0;
        const char *p = text;
        while (*p != '\0') {
            gunichar c = g_utf8_get_char(p);
            inject_character(c, cfg, shift_kc, altgr_kc, spare_kc, syms_per_kc);
            typed++;
            p = g_utf8_next_char(p);

            if ((typed % 16) == 0) {
                XFlush(s_ghost_dpy);
                XSync(s_ghost_dpy, False);
            }
        }
        XFlush(s_ghost_dpy);
        XSync(s_ghost_dpy, False);

        if (caps_was_on) {
            XkbLockModifiers(s_ghost_dpy, XkbUseCoreKbd, LockMask, LockMask);
            XSync(s_ghost_dpy, False);
            xutil_log("GHOST", "Restored CapsLock to ON.");
        }
        restore_spare_keycode(spare_kc, syms_per_kc);
        disengage_guard_delayed();

        overlay_set_ghost_button_state(GHOST_BTN_DONE);

        char toast_msg[256];
        if (app_name[0] != '\0' && strcmp(app_name, "app") != 0) {
            snprintf(toast_msg, sizeof(toast_msg), "Typed %zu chars in %s", typed, app_name);
        } else {
            snprintf(toast_msg, sizeof(toast_msg), "Typed %zu chars", typed);
        }
        overlay_show_toast(toast_msg);
        xutil_log("GHOST", "Successfully injected %zu characters into target window.", typed);
    } else {
        /* Delayed mode state machine */
        s_is_typing = true;
        overlay_set_ghost_button_state(GHOST_BTN_TYPING);

        s_typing_ctx = g_new0(GhostTypingContext, 1);
        s_typing_ctx->text = g_strdup(text);
        s_typing_ctx->curr_ptr = s_typing_ctx->text;
        s_typing_ctx->total_chars = char_count;
        s_typing_ctx->typed_chars = 0;
        g_strlcpy(s_typing_ctx->app_name, app_name, sizeof(s_typing_ctx->app_name));
        s_typing_ctx->caps_was_on = caps_was_on;
        s_typing_ctx->shift_kc = shift_kc;
        s_typing_ctx->altgr_kc = altgr_kc;
        s_typing_ctx->spare_kc = spare_kc;
        s_typing_ctx->syms_per_kc = syms_per_kc;

        s_typing_timer_id = g_timeout_add(cfg->ghost_delay_ms, on_ghost_typing_step, s_typing_ctx);
        xutil_log("GHOST", "Started delayed typing timer (interval %d ms).", cfg->ghost_delay_ms);
    }
}
