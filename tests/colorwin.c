#include <X11/Xlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

static volatile int s_running = 1;
static void handle_sig(int sig) {
    (void)sig;
    s_running = 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGTERM, handle_sig);
    signal(SIGINT, handle_sig);

    Display *d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "colorwin: cannot open X display\n");
        return 1;
    }

    int s = DefaultScreen(d);
    int w = DisplayWidth(d, s);
    int h = DisplayHeight(d, s);

    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixel = 0x00FF00; /* pure green */

    Window win = XCreateWindow(d, RootWindow(d, s), 0, 0, w, h, 0,
                               DefaultDepth(d, s), InputOutput,
                               DefaultVisual(d, s),
                               CWOverrideRedirect | CWBackPixel, &attr);

    XSelectInput(d, win, ExposureMask);
    XMapRaised(d, win);
    XFlush(d);

    printf("COLORWIN_READY: XID=0x%lx size=%dx%d\n", (unsigned long)win, w, h);
    fflush(stdout);

    while (s_running) {
        XEvent ev;
        while (XPending(d)) {
            XNextEvent(d, &ev);
        }
        usleep(50000);
    }

    XDestroyWindow(d, win);
    XCloseDisplay(d);
    return 0;
}
