#ifndef GHOST_H
#define GHOST_H

#include <stdbool.h>
#include <glib.h>
#include <X11/Xlib.h>

typedef enum {
    GHOST_BTN_IDLE,
    GHOST_BTN_TYPING,
    GHOST_BTN_DONE,
    GHOST_BTN_FAILED
} GhostButtonState;

bool ghost_init(void);
void ghost_cleanup(void);

bool ghost_is_guard_active(void);
bool ghost_is_typing(void);

void ghost_writer_run(void);
void ghost_writer_cancel(void);

#endif /* GHOST_H */
