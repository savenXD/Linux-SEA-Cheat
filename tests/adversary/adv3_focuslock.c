#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
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

static int error_handler(Display *dpy, XErrorEvent *ev) {
    (void)dpy;
    (void)ev;
    return 0; /* Ignore X errors like BadMatch or BadWindow */
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
        alarm(duration_sec + 2);
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "[ADV3] Cannot open display\n");
        return 1;
    }

    XSetErrorHandler(error_handler);

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    XSetWindowAttributes swa;
    swa.background_pixel = BlackPixel(dpy, screen);
    swa.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask;

    Window win = XCreateWindow(dpy, root, 0, 0, width, height, 0,
                               DefaultDepth(dpy, screen), InputOutput,
                               DefaultVisual(dpy, screen),
                               CWBackPixel | CWEventMask, &swa);

    Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    const char *title = "Adversary 3: FocusLock + XLowerWindow";
    XChangeProperty(dpy, win, net_wm_name, utf8_string, 8, PropModeReplace,
                    (unsigned char *)title, strlen(title));

    XMapRaised(dpy, win);
    XSync(dpy, False);
    usleep(100000); /* 100ms for map to complete */

    printf("[ADV3] Running FocusLock adversary (win=0x%lx) for %d sec...\n", (unsigned long)win, duration_sec);
    fflush(stdout);

    time_t start_time = time(NULL);
    while (s_running) {
        if (duration_sec > 0 && difftime(time(NULL), start_time) >= duration_sec) {
            break;
        }

        /* 1. Grab focus to self */
        XSetInputFocus(dpy, win, RevertToParent, CurrentTime);

        /* 2. Lower all top-level windows it can find (EWMH _NET_CLIENT_LIST) */
        Atom net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;

        if (XGetWindowProperty(dpy, root, net_client_list, 0, 1024, False, XA_WINDOW,
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
            Window *clients = (Window *)prop;
            for (unsigned long i = 0; i < nitems; i++) {
                if (clients[i] != win) {
                    XLowerWindow(dpy, clients[i]);
                }
            }
            XFree(prop);
        } else {
            /* Fallback: Query tree for top-level managed windows */
            Window root_ret, parent_ret;
            Window *children = NULL;
            unsigned int nchildren = 0;
            if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
                if (children) {
                    for (unsigned int i = 0; i < nchildren; i++) {
                        if (children[i] != win) {
                            XWindowAttributes attr;
                            if (XGetWindowAttributes(dpy, children[i], &attr) && !attr.override_redirect) {
                                XLowerWindow(dpy, children[i]);
                            }
                        }
                    }
                    XFree(children);
                }
            }
        }

        XRaiseWindow(dpy, win);
        XFlush(dpy);

        usleep(50000); /* 50 ms */
    }

    printf("[ADV3] Exiting cleanly.\n");
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
