#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <poll.h>

static volatile sig_atomic_t s_running = 1;
static Display *s_dpy = NULL;
static Window s_win = None;

static void clean_exit(void) {
    if (s_dpy) {
        XUngrabKeyboard(s_dpy, CurrentTime);
        XUngrabPointer(s_dpy, CurrentTime);
        if (s_win != None) {
            XDestroyWindow(s_dpy, s_win);
            s_win = None;
        }
        XFlush(s_dpy);
        XCloseDisplay(s_dpy);
        s_dpy = NULL;
    }
}

static void on_signal(int sig) {
    (void)sig;
    s_running = 0;
    clean_exit();
    _exit(0);
}

int main(int argc, char **argv) {
    int duration_sec = 30;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            duration_sec = atoi(argv[++i]);
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGALRM, on_signal);
    if (duration_sec > 0) {
        alarm(duration_sec + 2); /* Absolute safety deadline */
    }

    s_dpy = XOpenDisplay(NULL);
    if (!s_dpy) {
        fprintf(stderr, "[ADV2] Cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(s_dpy);
    Window root = RootWindow(s_dpy, screen);
    int width = DisplayWidth(s_dpy, screen);
    int height = DisplayHeight(s_dpy, screen);

    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.background_pixel = BlackPixel(s_dpy, screen);
    swa.event_mask = StructureNotifyMask | KeyPressMask;

    s_win = XCreateWindow(s_dpy, root, 0, 0, width, height, 0,
                          DefaultDepth(s_dpy, screen), InputOutput,
                          DefaultVisual(s_dpy, screen),
                          CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);

    Atom net_wm_name = XInternAtom(s_dpy, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(s_dpy, "UTF8_STRING", False);
    const char *title = "Adversary 2: Override-Redirect Aggressive Raise";
    XChangeProperty(s_dpy, s_win, net_wm_name, utf8_string, 8, PropModeReplace,
                    (unsigned char *)title, strlen(title));

    /* Select SubstructureNotify on root to react to any window changes */
    XSelectInput(s_dpy, root, SubstructureNotifyMask);

    XMapRaised(s_dpy, s_win);
    XFlush(s_dpy);

    /* Attempt grabs (ignore if fail, but log) */
    int kb_grab = XGrabKeyboard(s_dpy, s_win, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    int ptr_grab = XGrabPointer(s_dpy, s_win, False,
                                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

    printf("[ADV2] Running aggressive override adversary (win=0x%lx, kb_grab=%d, ptr_grab=%d) for %d sec...\n",
           (unsigned long)s_win, kb_grab, ptr_grab, duration_sec);
    fflush(stdout);

    /* Phase 1 of adversary: Storm burst (rapid raises) to trigger loop guard */
    for (int b = 0; b < 120 && s_running; b++) {
        XRaiseWindow(s_dpy, s_win);
        XFlush(s_dpy);
        usleep(500); /* 0.5 ms */
    }

    /* Phase 2 of adversary: Continuous aggressive raise every 20ms + on ConfigureNotify */
    time_t start_time = time(NULL);
    while (s_running) {
        if (duration_sec > 0 && difftime(time(NULL), start_time) >= duration_sec) {
            break;
        }

        while (XPending(s_dpy) > 0) {
            XEvent ev;
            XNextEvent(s_dpy, &ev);
            if (ev.type == ConfigureNotify && ev.xconfigure.window != s_win) {
                XRaiseWindow(s_dpy, s_win);
                XFlush(s_dpy);
            }
        }

        XRaiseWindow(s_dpy, s_win);
        XFlush(s_dpy);

        usleep(20000); /* 20 ms */
    }

    printf("[ADV2] Exiting cleanly, releasing grabs and closing.\n");
    clean_exit();
    return 0;
}
