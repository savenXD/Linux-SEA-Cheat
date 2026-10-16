#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>

int main(void) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    Window root = RootWindow(dpy, DefaultScreen(dpy));
    XSetWindowAttributes attrs;
    attrs.background_pixel = BlackPixel(dpy, DefaultScreen(dpy));

    Window win = XCreateWindow(dpy, root, 0, 400, 500, 500, 0,
                              CopyFromParent, InputOutput, CopyFromParent,
                              CWBackPixel, &attrs);

    XStoreName(dpy, win, "Competitor_Window");
    XMapWindow(dpy, win);
    XRaiseWindow(dpy, win);
    XFlush(dpy);

    printf("Competitor window created with XID 0x%lx\n", (unsigned long)win);
    fflush(stdout);

    /* Keep alive until killed */
    while (1) {
        sleep(1);
    }

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
