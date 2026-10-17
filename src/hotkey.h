#ifndef HOTKEY_H
#define HOTKEY_H

#include <glib.h>
#include <stdbool.h>

gboolean hotkey_init(void);
void     hotkey_cleanup(void);
void     hotkey_reload_grabs(void);

void     hotkey_install_clipboard_shield(void);
void     hotkey_remove_clipboard_shield(void);
bool     hotkey_is_clipboard_shield_active(void);
void     hotkey_drain_pending_events(void);
void     hotkey_sync_raw_modifiers(void);

#endif /* HOTKEY_H */
