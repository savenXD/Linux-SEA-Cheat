#include "xutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

void xutil_log(const char *tag, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    printf("[%s] [%s] ", time_str, tag);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void xutil_log_err(const char *tag, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    fprintf(stderr, "[%s] [%s ERROR] ", time_str, tag);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

static int custom_x_error_handler(Display *dpy, XErrorEvent *err) {
    char err_msg[256];
    XGetErrorText(dpy, err->error_code, err_msg, sizeof(err_msg));

    xutil_log_err("X11", "Error code %d (%s), Request: %d, Minor: %d, Resource: 0x%lx",
                  err->error_code, err_msg, err->request_code, err->minor_code,
                  (unsigned long)err->resourceid);

    if (err->error_code == BadAccess) {
        xutil_log("X11", "Notice: BadAccess intercepted. A grab or resource was already claimed by another client.");
    } else if (err->error_code == BadWindow) {
        xutil_log("X11", "Notice: BadWindow intercepted. Target window may have closed.");
    }

    return 0; /* Keep process running */
}

static int custom_x_io_error_handler(Display *dpy) {
    (void)dpy;
    xutil_log_err("X11", "Fatal X I/O error: connection to X server lost. Exiting.");
    exit(1);
}

void xutil_install_error_handlers(void) {
    XSetErrorHandler(custom_x_error_handler);
    XSetIOErrorHandler(custom_x_io_error_handler);
    xutil_log("XUTIL", "Installed robust X11 error handlers (logging non-fatal).");
}
