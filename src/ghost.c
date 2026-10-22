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
#include <math.h>

static Display  *s_ghost_dpy = NULL;
static bool      s_guard_active = false;
static guint     s_guard_timer_id = 0;
static bool      s_is_typing = false;
static guint     s_typing_timer_id = 0;
static gint64    s_last_trigger_time = 0;

static GhostTiming s_timing;
static gboolean s_timing_loaded = FALSE;

/* Median delay between keystrokes per mode: ~40 / ~105 / ~140 WPM (slow, good
 * typist, fast typist with occasional bursts); sustained speeds far above that
 * are a classic automation signal.
 * Human off means instant (0): characters go out in quick batches. */
static const int k_mode_delay_ms[3] = { 300, 115, 85 };

int ghost_speed_delay_ms(int mode, gboolean human) {
    return human ? k_mode_delay_ms[CLAMP(mode, 0, 2)] : 0;
}

int ghost_speed_wpm(int mode, gboolean human) {
    int d = ghost_speed_delay_ms(mode, human);
    return d ? (int)(12000.0 / d + 0.5) : 0;
}

static GhostTiming validate_timing(GhostTiming t) {
    if (t.speed < 0 || t.speed > 2) t.speed = 0;   /* also resets old slider values */
    int m = ghost_speed_delay_ms(t.speed, t.human);
    t.letter_min_ms = MAX(10, m / 2);
    t.letter_max_ms = MAX(10, m * 2);
    t.lines_min = 3; t.lines_max = 6;
    t.pause_min_ms = 2000; t.pause_max_ms = 7000;
    t.preserve_layout = TRUE;
    t.safe = t.safe ? TRUE : FALSE;
    return t;
}

GhostTiming ghost_timing_default(void) {
    GhostTiming t = { TRUE, 0, 0, 0, 0, 0, 0, 0, TRUE, FALSE };
    return validate_timing(t);
}

GhostTiming ghost_get_timing(void) {
    if (!s_timing_loaded) {
        s_timing = ghost_timing_default();
        GKeyFile *kf = g_key_file_new();
        if (g_key_file_load_from_file(kf, config_get_path(), G_KEY_FILE_NONE, NULL)) {
#define LOAD_TIMING(field) if (g_key_file_has_key(kf, "GhostTiming", #field, NULL)) s_timing.field = g_key_file_get_integer(kf, "GhostTiming", #field, NULL)
            LOAD_TIMING(human); LOAD_TIMING(speed); LOAD_TIMING(safe);
#undef LOAD_TIMING
        }
        g_key_file_unref(kf);
        s_timing = validate_timing(s_timing);
        s_timing_loaded = TRUE;
    }
    return s_timing;
}

void ghost_set_timing(GhostTiming timing) {
    s_timing = validate_timing(timing);
    s_timing_loaded = TRUE;
    GKeyFile *kf = g_key_file_new();
    g_key_file_load_from_file(kf, config_get_path(), G_KEY_FILE_KEEP_COMMENTS, NULL);
#define SAVE_TIMING(field) g_key_file_set_integer(kf, "GhostTiming", #field, s_timing.field)
    SAVE_TIMING(human); SAVE_TIMING(speed); SAVE_TIMING(safe);
#undef SAVE_TIMING
    GError *error = NULL;
    if (!g_key_file_save_to_file(kf, config_get_path(), &error)) {
        overlay_show_toast("Timing applied; could not save settings");
        g_clear_error(&error);
    }
    g_key_file_unref(kf);
}

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
    KeySym      *keymap;
    int          min_kc;
    int          max_kc;
    int          settle_retries;
    GhostTiming  timing;
    AppConfig    config;
    Window       target;
    int          lines_typed, next_pause_lines;
    int          phase;
    KeyMappingResult pending;
    bool         held;
    int          dwell_ms, rest_ms;
    gunichar     last_c;
    double       drift;
    gint64       last_progress_us;
    GArray      *plan;          /* queued corrections (PlanItem) typed before more text */
    bool         count_this;
    int          since_typo;
    gint64       start_us;
    int          burst_left;    /* chars left in a fast burst */
} GhostTypingContext;

enum { PH_CHAR, PH_PRESS, PH_RELEASE };

typedef struct { gunichar c; bool count; } PlanItem;

/* Chance (percent) that an eligible letter is mistyped in extra-safe mode. */
static int s_typo_pct = 3;

static void free_ctx(GhostTypingContext *ctx) {
    if (!ctx) return;
    if (ctx->plan) g_array_free(ctx->plan, TRUE);
    g_free(ctx->text);
    g_free(ctx);
}

static GhostTypingContext *s_typing_ctx = NULL;
static void restore_spare_keycode(KeyCode spare_kc, int syms_per_kc);
static void release_held(GhostTypingContext *ctx);

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
        release_held(s_typing_ctx);
        if (s_typing_ctx->shift_kc && s_ghost_dpy) {
            XTestFakeKeyEvent(s_ghost_dpy, s_typing_ctx->shift_kc, False, CurrentTime);
        }
        if (s_typing_ctx->altgr_kc && s_ghost_dpy) {
            XTestFakeKeyEvent(s_ghost_dpy, s_typing_ctx->altgr_kc, False, CurrentTime);
        }
        if (s_typing_ctx->caps_was_on && s_ghost_dpy) {
            XkbLockModifiers(s_ghost_dpy, XkbUseCoreKbd, LockMask, LockMask);
            XSync(s_ghost_dpy, False);
        }
        restore_spare_keycode(s_typing_ctx->spare_kc, s_typing_ctx->syms_per_kc);
        if (s_typing_ctx->keymap) {
            XFree(s_typing_ctx->keymap);
            s_typing_ctx->keymap = NULL;
        }
        free_ctx(s_typing_ctx);
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

/* Rough time to type [from, to): per-char cost of the mode, plus the chunk
 * pause that follows roughly every 4-5 line breaks in human mode. */
static double model_ms(const char *from, const char *to, const GhostTiming *t) {
    long n = 0, lines = 0;
    for (const char *p = from; p < to; p = g_utf8_next_char(p)) { n++; if (*p == '\n') lines++; }
    if (!t->human) return n * 1.0;
    double m = ghost_speed_delay_ms(t->speed, TRUE);
    double per = m * 1.15 + (t->safe ? m * 0.1 + 35.0 : 0.0);
    return n * per + lines * 1000.0;
}

/* Model for what is left, scaled by how the run has actually paced so far. */
int ghost_eta_remaining_ms(void) {
    GhostTypingContext *c = s_typing_ctx;
    if (!s_is_typing || !c || !c->curr_ptr) return -1;
    const char *end = c->text + strlen(c->text);
    double left = model_ms(c->curr_ptr, end, &c->timing);
    if (c->typed_chars >= 25) {
        double done_model = model_ms(c->text, c->curr_ptr, &c->timing);
        double elapsed = (g_get_monotonic_time() - c->start_us) / 1000.0;
        if (done_model > 0) {
            double ratio = CLAMP(elapsed / done_model, 0.6, 1.8);
            double w = MIN(1.0, c->typed_chars / 100.0);
            left *= 1.0 + (ratio - 1.0) * w;
        }
    }
    return (int)left;
}

static KeyCode find_spare_keycode(const KeySym *mapping, int min_kc, int max_kc, int syms_per_kc) {
    if (!s_ghost_dpy || !mapping) return 0;

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

static bool resolve_keysym_on_layout(KeySym sym, KeyMappingResult *res,
                                     const KeySym *mapping, int min_kc, int max_kc, int actual_syms_per_kc,
                                     KeyCode spare_kc, int syms_per_kc) {
    if (!s_ghost_dpy || !mapping) return false;
    memset(res, 0, sizeof(*res));

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

/* Returns false when the character is to be skipped per config. */
static bool char_to_keysym(gunichar *c, const AppConfig *cfg, KeySym *sym) {
    if (*c == '\b') { *sym = XK_BackSpace; return true; }
    if (*c == '\r') *c = '\n';
    if (*c == '\n') {
        if (g_ascii_strcasecmp(cfg->ghost_newline, "skip") == 0) return false;
        if (g_ascii_strcasecmp(cfg->ghost_newline, "space") == 0) *c = ' ';
    } else if (*c == '\t') {
        if (g_ascii_strcasecmp(cfg->ghost_tab, "skip") == 0) return false;
        if (g_ascii_strcasecmp(cfg->ghost_tab, "space") == 0) *c = ' ';
    }
    if (*c == '\n') *sym = XK_Return;
    else if (*c == '\t') *sym = XK_Tab;
    else {
        *sym = gdk_unicode_to_keyval(*c);
        if (*sym == GDK_KEY_VoidSymbol || *sym == 0)
            *sym = *c < 0x100 ? (KeySym)*c : (KeySym)(0x01000000 | *c);
    }
    return true;
}

static void press_mods(const GhostTypingContext *ctx, unsigned int mod, bool down) {
    if ((mod & ShiftMask) && ctx->shift_kc) XTestFakeKeyEvent(s_ghost_dpy, ctx->shift_kc, down, CurrentTime);
    if ((mod & Mod5Mask) && ctx->altgr_kc) XTestFakeKeyEvent(s_ghost_dpy, ctx->altgr_kc, down, CurrentTime);
}

/* Lets go of whatever key the step machine is holding (cancel / cleanup). */
static void release_held(GhostTypingContext *ctx) {
    if (!ctx || !ctx->held || !s_ghost_dpy) return;
    XTestFakeKeyEvent(s_ghost_dpy, ctx->pending.kc, False, CurrentTime);
    press_mods(ctx, ctx->pending.mod, false);
    if (ctx->pending.is_spare) restore_spare_keycode(ctx->spare_kc, ctx->syms_per_kc);
    ctx->held = false;
    XFlush(s_ghost_dpy);
}

static double rand_lognormal(double median, double sigma) {
    double u1 = g_random_double_range(1e-9, 1.0), u2 = g_random_double();
    return median * exp(sigma * sqrt(-2.0 * log(u1)) * cos(2.0 * G_PI * u2));
}

/* Time from this key-down to the next one. Real typing is log-normal, slower
 * at word/sentence boundaries and after capitals, with the odd hesitation. */
static int next_interval_ms(GhostTypingContext *ctx, gunichar c) {
    const GhostTiming *t = &ctx->timing;
    double m = ghost_speed_delay_ms(t->speed, t->human);
    if (!t->human) return (int)m;
    if (t->safe) m *= 1.1;
    if (c == ' ') ctx->drift = CLAMP(ctx->drift + g_random_double_range(-0.06, 0.06), 0.85, 1.2);
    m *= ctx->drift;
    double d = rand_lognormal(m, 0.32);
    if (c == ' ') d *= 1.2;
    else if (c == '\n') d *= 2.2;
    else if (strchr(",.;:!?", (int)(c < 128 ? c : 'a'))) d *= 1.5;
    else if (g_unichar_isupper(c)) d *= 1.15;
    if (c == ctx->last_c) d *= 0.85;
    /* Good typists type in bursts: a few words noticeably quicker. */
    if (t->speed >= 1) {
        if (ctx->burst_left <= 0 && c == ' ' && g_random_int_range(0, 100) < 10) ctx->burst_left = g_random_int_range(15, 46);
        if (ctx->burst_left > 0) { d *= 0.72; ctx->burst_left--; }
    }
    d = CLAMP(d, MAX(35.0, m * 0.45), m * 4.0);
    if (g_random_int_range(0, 100) < (t->safe ? 6 : 3)) d += g_random_int_range(250, 800);
    if (t->safe) {
        /* Thinking: after some words, sentences and lines. */
        int r = g_random_int_range(0, 100);
        if (c == ' ' && r < 7) d += CLAMP(rand_lognormal(1000.0, 0.4), 500.0, 2600.0);
        else if (strchr(".!?", (int)(c < 128 ? c : 'a')) && c != 0 && r < 25) d += g_random_int_range(600, 1800);
        else if ((c == ',' || c == ';' || c == ':') && r < 12) d += g_random_int_range(300, 900);
        else if (c == '\n' && r < 30) d += g_random_int_range(800, 2500);
    }
    return (int)d;
}

static bool is_overlay_or_child_window(Display *dpy, Window w, Window our_xid) {
    if (!dpy || w == None || our_xid == None) return false;
    if (w == our_xid) return true;

    Window root = None, parent = None, *children = NULL;
    unsigned int nchildren = 0;
    Window curr = w;
    while (curr != None) {
        if (curr == our_xid) return true;
        if (XQueryTree(dpy, curr, &root, &parent, &children, &nchildren) == 0) {
            break;
        }
        if (children) {
            XFree(children);
            children = NULL;
        }
        if (parent == root || parent == curr) break;
        curr = parent;
    }
    return false;
}

static char *normalize_text_for_typing(const char *input) {
    if (!input) return g_strdup("");

    GString *out = g_string_sized_new(strlen(input) + 1);
    const char *p = input;

    while (*p != '\0') {
        gunichar c = g_utf8_get_char(p);
        const char *next_p = g_utf8_next_char(p);

        if (c == '\r') {
            /* If CRLF, skip '\r' so '\n' represents the line break */
            if (*next_p == '\n') {
                p = next_p;
                continue;
            }
            /* Standalone '\r' -> convert to '\n' */
            g_string_append_c(out, '\n');
            p = next_p;
            continue;
        }

        g_string_append_unichar(out, c);
        p = next_p;
    }

    /* Pasted text often ends in a newline. Never send a final Return that could
     * submit a chat/form; internal line breaks are still typed as supplied. */
    while (out->len && out->str[out->len - 1] == '\n') g_string_truncate(out, out->len - 1);
    return g_string_free(out, FALSE);
}

static Window find_target_window(char *app_name_out, size_t app_name_size) {
    if (app_name_out && app_name_size > 0) {
        app_name_out[0] = '\0';
    }
    if (!s_ghost_dpy) return None;

    Window our_xid = overlay_get_xid();
    Window root = DefaultRootWindow(s_ghost_dpy);

    /* 1. Discover current input focus */
    Window focus_win = None;
    int revert_to = 0;
    XGetInputFocus(s_ghost_dpy, &focus_win, &revert_to);

    bool focus_is_valid = (focus_win != None &&
                           focus_win != PointerRoot &&
                           !is_overlay_or_child_window(s_ghost_dpy, focus_win, our_xid));

    /* 2. Query _NET_ACTIVE_WINDOW as candidate / fallback */
    Window active_w = None;
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
                Window cand = *(Window *)prop;
                if (cand != None && cand != our_xid && !is_overlay_or_child_window(s_ghost_dpy, cand, our_xid)) {
                    /* Verify candidate window is alive and viewable */
                    XWindowAttributes attrs;
                    if (XGetWindowAttributes(s_ghost_dpy, cand, &attrs) != 0 &&
                        attrs.map_state == IsViewable) {
                        active_w = cand;
                    }
                }
            }
            XFree(prop);
        }
    }

    Window target_win = None;
    if (focus_is_valid) {
        target_win = focus_win;
    } else if (active_w != None) {
        target_win = active_w;
        /* Re-assert input focus to the active application */
        XSetInputFocus(s_ghost_dpy, target_win, RevertToParent, CurrentTime);
        XSync(s_ghost_dpy, False);
        xutil_log("GHOST", "Assigned focus to active desktop window 0x%lx (_NET_ACTIVE_WINDOW fallback).", (unsigned long)target_win);
    } else {
        return None;
    }

    /* Query WM_CLASS / name for toast message */
    Window probe = (active_w != None) ? active_w : target_win;
    XClassHint ch;
    memset(&ch, 0, sizeof(ch));
    bool got_class = false;
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

    return target_win;
}

static char typo_neighbor(char c) {
    static const char *near[26] = {
        "qsz", "vgn", "xdv", "sefc", "wrd", "drgv", "fthb", "gyjn", "uok", "hukm", "jil", "kop", "nj",
        "bhm", "ipl", "ol", "wa", "etf", "awdx", "ryg", "yij", "cfb", "qes", "zsc", "tuh", "asx" };
    const char *n = near[c - 'a'];
    return n[g_random_int_range(0, (int)strlen(n))];
}

/* Next thing to type: queued corrections first, then text. In extra-safe mode
 * a lowercase letter is occasionally mistyped, then (after 0-2 more letters)
 * noticed, backspaced and retyped. Returns 0 at end. */
static gunichar take_next(GhostTypingContext *ctx) {
    ctx->count_this = true;
    if (ctx->plan->len) {
        PlanItem it = g_array_index(ctx->plan, PlanItem, 0);
        g_array_remove_index(ctx->plan, 0);
        ctx->count_this = it.count;
        return it.c;
    }
    if (!ctx->curr_ptr || !*ctx->curr_ptr) return 0;
    gunichar c = g_utf8_get_char(ctx->curr_ptr);
    ctx->curr_ptr = g_utf8_next_char(ctx->curr_ptr);
    ctx->since_typo++;
    if (ctx->timing.human && ctx->timing.safe && c >= 'a' && c <= 'z' &&
        ctx->since_typo > 6 && g_random_int_range(0, 100) < s_typo_pct) {
        gunichar extras[2]; int n = 0, want = g_random_int_range(0, 3);
        while (n < want && *ctx->curr_ptr) {
            gunichar e = g_utf8_get_char(ctx->curr_ptr);
            if (e < 'a' || e > 'z') break;
            extras[n++] = e;
            ctx->curr_ptr = g_utf8_next_char(ctx->curr_ptr);
        }
        PlanItem it;
        for (int i = 0; i < n; i++) { it = (PlanItem){ extras[i], false }; g_array_append_val(ctx->plan, it); }
        for (int i = 0; i <= n; i++) { it = (PlanItem){ '\b', false }; g_array_append_val(ctx->plan, it); }
        it = (PlanItem){ c, true }; g_array_append_val(ctx->plan, it);
        for (int i = 0; i < n; i++) { it = (PlanItem){ extras[i], true }; g_array_append_val(ctx->plan, it); }
        ctx->since_typo = 0;
        ctx->count_this = false;
        return (gunichar)typo_neighbor((char)c);
    }
    return c;
}

static gboolean on_ghost_typing_step(gpointer user_data);
static void finish_typing(GhostTypingContext *ctx) {
    XFlush(s_ghost_dpy);
    XSync(s_ghost_dpy, False);
    s_is_typing = false;
    s_typing_timer_id = 0;
    if (ctx->caps_was_on) {
        XkbLockModifiers(s_ghost_dpy, XkbUseCoreKbd, LockMask, LockMask);
        XSync(s_ghost_dpy, False);
    }
    if (ctx->keymap) { XFree(ctx->keymap); ctx->keymap = NULL; }
    disengage_guard_delayed();
    overlay_ghost_progress(ctx->typed_chars, ctx->total_chars);
    overlay_set_ghost_button_state(GHOST_BTN_DONE);

    char toast_msg[256];
    if (ctx->app_name[0] != '\0' && strcmp(ctx->app_name, "app") != 0)
        snprintf(toast_msg, sizeof(toast_msg), "Typed %zu chars in %s", ctx->typed_chars, ctx->app_name);
    else
        snprintf(toast_msg, sizeof(toast_msg), "Typed %zu chars", ctx->typed_chars);
    overlay_show_toast(toast_msg);
    xutil_log("GHOST", "Finished typing %zu characters.", ctx->typed_chars);
    free_ctx(ctx);
    s_typing_ctx = NULL;
}

#define INSTANT_BATCH 16

/* Human off: no dwell or jitter, characters leave in small batches so the
 * main loop and the target app keep breathing. A character that needs the
 * spare-keycode remap goes through the phased path so the target can settle. */
static gboolean instant_step(GhostTypingContext *ctx) {
    Window focus; int revert;
    XGetInputFocus(s_ghost_dpy, &focus, &revert);
    if (focus != ctx->target) {
        ghost_writer_cancel();
        overlay_show_toast("Stopped: focus changed");
        return G_SOURCE_REMOVE;
    }
    int next_ms = 2;
    for (int n = 0; n < INSTANT_BATCH; n++) {
        gunichar c = 0;
        KeySym sym = NoSymbol;
        while (ctx->curr_ptr && *ctx->curr_ptr) {
            c = g_utf8_get_char(ctx->curr_ptr);
            ctx->curr_ptr = g_utf8_next_char(ctx->curr_ptr);
            if (char_to_keysym(&c, &ctx->config, &sym)) break;
            ctx->typed_chars++;
            c = 0;
        }
        if (!c) { finish_typing(ctx); return G_SOURCE_REMOVE; }
        if (!resolve_keysym_on_layout(sym, &ctx->pending, ctx->keymap, ctx->min_kc, ctx->max_kc,
                                      ctx->syms_per_kc, ctx->spare_kc, ctx->syms_per_kc)) {
            ghost_writer_cancel();
            overlay_show_toast("Stopped: unsupported character");
            return G_SOURCE_REMOVE;
        }
        if (ctx->pending.is_spare) {
            ctx->held = true;
            ctx->dwell_ms = 8; ctx->rest_ms = 1;
            ctx->phase = PH_PRESS;
            next_ms = 35;
            break;
        }
        press_mods(ctx, ctx->pending.mod, true);
        XTestFakeKeyEvent(s_ghost_dpy, ctx->pending.kc, True, CurrentTime);
        XTestFakeKeyEvent(s_ghost_dpy, ctx->pending.kc, False, CurrentTime);
        press_mods(ctx, ctx->pending.mod, false);
        ctx->typed_chars++;
    }
    XFlush(s_ghost_dpy);
    gint64 now = g_get_monotonic_time();
    if (now - ctx->last_progress_us > 120000) {
        ctx->last_progress_us = now;
        overlay_ghost_progress(ctx->typed_chars, ctx->total_chars);
    }
    s_typing_timer_id = g_timeout_add(next_ms, on_ghost_typing_step, ctx);
    return G_SOURCE_REMOVE;
}

/* One key = three timer phases: CHAR (choose + modifiers down), PRESS (key
 * down), RELEASE (key up). The gap between PRESS and RELEASE is the dwell
 * time; instant down/up pairs are a trivial automation giveaway. */
static gboolean on_ghost_typing_step(gpointer user_data) {
    GhostTypingContext *ctx = (GhostTypingContext *)user_data;
    if (!ctx || !s_is_typing) return G_SOURCE_REMOVE;
    s_typing_timer_id = 0;
    int next_ms = 1;

    if (ctx->phase == PH_CHAR && !ctx->timing.human) return instant_step(ctx);
    if (ctx->phase == PH_CHAR) {
        Window focus; int revert;
        XGetInputFocus(s_ghost_dpy, &focus, &revert);
        if (focus != ctx->target) {
            ghost_writer_cancel();
            overlay_show_toast("Stopped: focus changed");
            return G_SOURCE_REMOVE;
        }
        gunichar c = 0;
        KeySym sym = NoSymbol;
        while ((c = take_next(ctx))) {
            if (char_to_keysym(&c, &ctx->config, &sym)) break;
            if (ctx->count_this) ctx->typed_chars++;
        }
        if (!c) { finish_typing(ctx); return G_SOURCE_REMOVE; }

        if (!resolve_keysym_on_layout(sym, &ctx->pending, ctx->keymap, ctx->min_kc, ctx->max_kc,
                                      ctx->syms_per_kc, ctx->spare_kc, ctx->syms_per_kc)) {
            ghost_writer_cancel();
            overlay_show_toast("Stopped: unsupported character");
            return G_SOURCE_REMOVE;
        }
        int total = next_interval_ms(ctx, c);
        int lead = 0;
        if (ctx->timing.human) {
            ctx->dwell_ms = (int)CLAMP(rand_lognormal(MIN(80.0, total * 0.7), 0.25), 35.0, 170.0);
            if (ctx->pending.mod) lead = g_random_int_range(18, 46);
        } else {
            ctx->dwell_ms = 8;
            if (ctx->pending.mod) lead = 5;
        }
        if (ctx->pending.is_spare) lead = MAX(lead, 35);   /* let clients absorb MappingNotify */
        if (ctx->dwell_ms > total * 7 / 10) ctx->dwell_ms = MAX(2, total * 7 / 10);
        ctx->rest_ms = MAX(1, total - lead - ctx->dwell_ms);
        ctx->last_c = c;
        if (c == '\n' && ctx->count_this) ctx->lines_typed++;

        ctx->held = true;           /* modifiers/spare mapping now owed a release */
        press_mods(ctx, ctx->pending.mod, true);
        ctx->phase = PH_PRESS;
        next_ms = lead;
        if (lead > 0) { XFlush(s_ghost_dpy); }
        else goto press;
    } else if (ctx->phase == PH_PRESS) {
press:
        XTestFakeKeyEvent(s_ghost_dpy, ctx->pending.kc, True, CurrentTime);
        XFlush(s_ghost_dpy);
        ctx->phase = PH_RELEASE;
        next_ms = ctx->dwell_ms;
    } else {
        release_held(ctx);
        if (ctx->count_this) ctx->typed_chars++;
        ctx->phase = PH_CHAR;
        next_ms = ctx->rest_ms;
        /* Noticing a mistake takes a moment before the first backspace. */
        if (ctx->plan->len && g_array_index(ctx->plan, PlanItem, 0).c == '\b' && ctx->last_c != '\b')
            next_ms += g_random_int_range(200, 600);
        if (ctx->timing.human && ctx->lines_typed >= ctx->next_pause_lines && *ctx->curr_ptr) {
            next_ms += g_random_int_range(ctx->timing.pause_min_ms, ctx->timing.pause_max_ms + 1);
            ctx->lines_typed = 0;
            ctx->next_pause_lines = g_random_int_range(ctx->timing.lines_min, ctx->timing.lines_max + 1);
        }
        gint64 now = g_get_monotonic_time();
        if (now - ctx->last_progress_us > 120000) {
            ctx->last_progress_us = now;
            overlay_ghost_progress(ctx->typed_chars, ctx->total_chars);
        }
    }
    s_typing_timer_id = g_timeout_add(MAX(next_ms, 1), on_ghost_typing_step, ctx);
    return G_SOURCE_REMOVE;
}

void ghost_writer_cancel(void) {
    if (!s_is_typing) return;

    xutil_log("GHOST", "Cancelling active Ghost Writer typing.");
    if (s_typing_timer_id > 0) {
        g_source_remove(s_typing_timer_id);
        s_typing_timer_id = 0;
    }

    if (s_typing_ctx) {
        release_held(s_typing_ctx);
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
        if (s_typing_ctx->keymap) {
            XFree(s_typing_ctx->keymap);
            s_typing_ctx->keymap = NULL;
        }
        free_ctx(s_typing_ctx);
        s_typing_ctx = NULL;
    }

    disengage_guard_delayed();
    s_is_typing = false;

    overlay_set_ghost_button_state(GHOST_BTN_FAILED);
    overlay_show_toast("Typing cancelled");
}

static gboolean on_ghost_typing_settle(gpointer user_data) {
    GhostTypingContext *ctx = (GhostTypingContext *)user_data;
    if (!ctx || !s_is_typing) return G_SOURCE_REMOVE;

    KeyCode ctrl_l = XKeysymToKeycode(s_ghost_dpy, XK_Control_L);
    KeyCode ctrl_r = XKeysymToKeycode(s_ghost_dpy, XK_Control_R);
    KeyCode alt_l  = XKeysymToKeycode(s_ghost_dpy, XK_Alt_L);
    KeyCode alt_r  = XKeysymToKeycode(s_ghost_dpy, XK_Alt_R);

    char keys[32] = {0};
    XQueryKeymap(s_ghost_dpy, keys);
    bool mod_down = (ctrl_l && is_keycode_down(keys, ctrl_l)) ||
                    (ctrl_r && is_keycode_down(keys, ctrl_r)) ||
                    (alt_l  && is_keycode_down(keys, alt_l))  ||
                    (alt_r  && is_keycode_down(keys, alt_r));

    ctx->settle_retries++;
    if (mod_down && ctx->settle_retries < 24) {
        /* Still physically held (e.g. user or xdotool holding Alt), wait another 10ms */
        return G_SOURCE_CONTINUE;
    }

    s_typing_timer_id = 0;

    /* Release any held Control / Alt / Meta keys to prevent hotkey bleed */
    KeyCode meta_l = XKeysymToKeycode(s_ghost_dpy, XK_Meta_L);
    KeyCode meta_r = XKeysymToKeycode(s_ghost_dpy, XK_Meta_R);
    if (ctrl_l) XTestFakeKeyEvent(s_ghost_dpy, ctrl_l, False, 0);
    if (ctrl_r) XTestFakeKeyEvent(s_ghost_dpy, ctrl_r, False, 0);
    if (alt_l)  XTestFakeKeyEvent(s_ghost_dpy, alt_l,  False, 0);
    if (alt_r)  XTestFakeKeyEvent(s_ghost_dpy, alt_r,  False, 0);
    if (meta_l) XTestFakeKeyEvent(s_ghost_dpy, meta_l, False, 0);
    if (meta_r) XTestFakeKeyEvent(s_ghost_dpy, meta_r, False, 0);
    XFlush(s_ghost_dpy);
    XSync(s_ghost_dpy, False);

    s_typing_timer_id = g_timeout_add(g_random_int_range(120, 350), on_ghost_typing_step, ctx);
    return G_SOURCE_REMOVE;
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
    const char *raw_text = overlay_get_test_entry_text();
    if (!raw_text || raw_text[0] == '\0') {
        xutil_log("GHOST", "Triggered with empty input box.");
        overlay_set_ghost_button_state(GHOST_BTN_FAILED);
        overlay_show_toast("Nothing to type");
        return;
    }

    char *text = normalize_text_for_typing(raw_text);
    if (!text || text[0] == '\0') {
        xutil_log("GHOST", "Triggered with empty normalized text.");
        g_free(text);
        overlay_set_ghost_button_state(GHOST_BTN_FAILED);
        overlay_show_toast("Nothing to type");
        return;
    }

    const AppConfig *cfg = config_get();
    size_t char_count = g_utf8_strlen(text, -1);
    int max_chars = (cfg->ghost_max_chars > 0) ? cfg->ghost_max_chars : 1000000;
    if ((int)char_count > max_chars) {
        xutil_log("GHOST", "Text exceeds limit (%zu > %d chars).", char_count, max_chars);
        g_free(text);
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
        g_free(text);
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

    /* 5. Prepare modifier and spare keycodes using a single XGetKeyboardMapping query */
    int min_kc = 0, max_kc = 0;
    XDisplayKeycodes(s_ghost_dpy, &min_kc, &max_kc);
    int syms_per_kc = 0;
    KeySym *mapping = XGetKeyboardMapping(s_ghost_dpy, min_kc, max_kc - min_kc + 1, &syms_per_kc);
    if (!mapping) {
        xutil_log_err("GHOST", "Failed to retrieve keyboard mapping.");
        if (caps_was_on) {
            XkbLockModifiers(s_ghost_dpy, XkbUseCoreKbd, LockMask, LockMask);
            XSync(s_ghost_dpy, False);
        }
        g_free(text);
        overlay_set_ghost_button_state(GHOST_BTN_FAILED);
        overlay_show_toast("Keymap query failed");
        return;
    }

    KeyCode shift_kc = XKeysymToKeycode(s_ghost_dpy, XK_Shift_L);
    if (shift_kc == 0) shift_kc = XKeysymToKeycode(s_ghost_dpy, XK_Shift_R);
    KeyCode altgr_kc = find_altgr_keycode();
    KeyCode spare_kc = find_spare_keycode(mapping, min_kc, max_kc, syms_per_kc);

    /* 6. Setup typing context */
    s_is_typing = true;
    overlay_set_ghost_button_state(GHOST_BTN_TYPING);

    s_typing_ctx = g_new0(GhostTypingContext, 1);
    s_typing_ctx->timing = ghost_get_timing();
    s_typing_ctx->config = *cfg;
    if (s_typing_ctx->timing.preserve_layout) {
        g_strlcpy(s_typing_ctx->config.ghost_newline, "return", sizeof(s_typing_ctx->config.ghost_newline));
        g_strlcpy(s_typing_ctx->config.ghost_tab, "tab", sizeof(s_typing_ctx->config.ghost_tab));
    }
    s_typing_ctx->target = target_win;
    s_typing_ctx->drift = 1.0;
    s_typing_ctx->start_us = g_get_monotonic_time();
    s_typing_ctx->plan = g_array_new(FALSE, FALSE, sizeof(PlanItem));
    s_typing_ctx->since_typo = 100;
    s_typing_ctx->next_pause_lines = g_random_int_range(s_typing_ctx->timing.lines_min, s_typing_ctx->timing.lines_max + 1);
    s_typing_ctx->text = text; /* takes ownership of text */
    s_typing_ctx->curr_ptr = s_typing_ctx->text;
    s_typing_ctx->total_chars = char_count;
    s_typing_ctx->typed_chars = 0;
    g_strlcpy(s_typing_ctx->app_name, app_name, sizeof(s_typing_ctx->app_name));
    s_typing_ctx->caps_was_on = caps_was_on;
    s_typing_ctx->shift_kc = shift_kc;
    s_typing_ctx->altgr_kc = altgr_kc;
    s_typing_ctx->spare_kc = spare_kc;
    s_typing_ctx->syms_per_kc = syms_per_kc;
    s_typing_ctx->keymap = mapping;
    s_typing_ctx->min_kc = min_kc;
    s_typing_ctx->max_kc = max_kc;

    /* 7. Engage self-echo guard BEFORE first event */
    if (s_guard_timer_id > 0) {
        g_source_remove(s_guard_timer_id);
        s_guard_timer_id = 0;
    }
    s_guard_active = true;
    xutil_log("GHOST", "Self-echo guard ENGAGED.");

    /* 8. Schedule settling timer (non-blocking 10ms check) */
    s_typing_timer_id = g_timeout_add(10, on_ghost_typing_settle, s_typing_ctx);
    xutil_log("GHOST", "Started non-blocking ghost typing pipeline for %zu chars into '%s'.", char_count, app_name);
}
