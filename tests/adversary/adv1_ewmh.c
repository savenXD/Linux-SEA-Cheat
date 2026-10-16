#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

static volatile sig_atomic_t s_running = 1;

static void on_signal(int sig) {
    (void)sig;
    s_running = 0;
}

static void send_client_msg(Display *dpy, Window root, Window win, Atom msg_type,
                            long d0, long d1, long d2, long d3, long d4) {
    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = win;
    ev.xclient.message_type = msg_type;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = d0;
    ev.xclient.data.l[1] = d1;
    ev.xclient.data.l[2] = d2;
    ev.xclient.data.l[3] = d3;
    ev.xclient.data.l[4] = d4;

    XSendEvent(dpy, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
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
    signal(SIGALRM, on_signal);
    if (duration_sec > 0) {
        alarm(duration_sec + 2); /* Hard alarm safety net */
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "[ADV1] Cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    Atom net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom net_wm_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom net_wm_above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    Atom net_wm_sticky = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);
    Atom net_active_window = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", False);

    XSetWindowAttributes swa;
    swa.background_pixel = BlackPixel(dpy, screen);
    swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;

    Window win = XCreateWindow(dpy, root, 0, 0, width, height, 0,
                               DefaultDepth(dpy, screen), InputOutput,
                               DefaultVisual(dpy, screen),
                               CWBackPixel | CWEventMask, &swa);

    /* Set title */
    const char *title = "Adversary 1: EWMH Fullscreen+Above";
    XChangeProperty(dpy, win, net_wm_name, utf8_string, 8, PropModeReplace,
                    (unsigned char *)title, strlen(title));
    XStoreName(dpy, win, title);

    /* Initial _NET_WM_STATE properties before map */
    Atom states[3] = { net_wm_fullscreen, net_wm_above, net_wm_sticky };
    XChangeProperty(dpy, win, net_wm_state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, 3);

    XMapWindow(dpy, win);
    XFlush(dpy);

    /* Send EWMH ClientMessages to WM to ensure full activation */
    send_client_msg(dpy, root, win, net_wm_state, 1, net_wm_fullscreen, 0, 1, 0);
    send_client_msg(dpy, root, win, net_wm_state, 1, net_wm_above, 0, 1, 0);
    send_client_msg(dpy, root, win, net_wm_state, 1, net_wm_sticky, 0, 1, 0);
    send_client_msg(dpy, root, win, net_active_window, 1, CurrentTime, 0, 0, 0);

    printf("[ADV1] Running EWMH adversary (win=0x%lx) for %d seconds...\n", (unsigned long)win, duration_sec);
    fflush(stdout);

    time_t start_time = time(NULL);
    while (s_running) {
        if (duration_sec > 0 && difftime(time(NULL), start_time) >= duration_sec) {
            break;
        }

        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);
        }

        /* Periodically assert activation and state above */
        send_client_msg(dpy, root, win, net_active_window, 1, CurrentTime, 0, 0, 0);
        send_client_msg(dpy, root, win, net_wm_state, 1, net_wm_above, 0, 1, 0);

        usleep(100000); /* 100 ms */
    }

    printf("[ADV1] Exiting cleanly and destroying window.\n");
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
