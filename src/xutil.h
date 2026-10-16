#ifndef XUTIL_H
#define XUTIL_H

#include <X11/Xlib.h>
#include <stdbool.h>

void xutil_install_error_handlers(void);
void xutil_log(const char *tag, const char *fmt, ...);
void xutil_log_err(const char *tag, const char *fmt, ...);

#endif /* XUTIL_H */
