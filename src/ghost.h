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

typedef struct {
    gboolean human;
    int speed;                      /* mode: 0 slow, 1 moderate, 2 fast */
    int letter_min_ms, letter_max_ms;
    int lines_min, lines_max;
    int pause_min_ms, pause_max_ms;
    gboolean preserve_layout;
    gboolean safe;                  /* extra safe: typos, thinking pauses, a bit slower */
} GhostTiming;

GhostTiming ghost_timing_default(void);
int ghost_speed_delay_ms(int speed, gboolean human);
int ghost_speed_wpm(int speed, gboolean human);
GhostTiming ghost_get_timing(void);
void ghost_set_timing(GhostTiming timing);
bool ghost_init(void);
void ghost_cleanup(void);

bool ghost_is_guard_active(void);
bool ghost_is_typing(void);
int ghost_eta_remaining_ms(void);   /* -1 when not typing */

void ghost_writer_run(void);
void ghost_writer_cancel(void);

#endif /* GHOST_H */
