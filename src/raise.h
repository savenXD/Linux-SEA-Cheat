#ifndef RAISE_H
#define RAISE_H

#include <glib.h>
#include <stdbool.h>
#include <X11/Xlib.h>

gboolean raise_init(void);
void     raise_start(void);
void     raise_stop(void);
void     raise_cleanup(void);

guint64  raise_get_count(void);
guint64  raise_get_trip_count(void);

#endif /* RAISE_H */
