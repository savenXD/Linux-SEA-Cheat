#include "window.h"
#include "hotkey.h"
#include "xutil.h"

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static gboolean on_shutdown_signal(gpointer user_data) {
    (void)user_data;
    xutil_log("MAIN", "Shutdown signal received. Performing clean teardown...");

    overlay_hide();
    hotkey_cleanup();
    window_cleanup();

    if (gtk_main_level() > 0) {
        gtk_main_quit();
    }
    xutil_log("MAIN", "Clean exit completed.");
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
    /* Line-buffered stdout/stderr for immediate log visibility */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    xutil_log("MAIN", "=== Starting PhoneOverlay (Phase 1) ===");

    gboolean start_visible = FALSE;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--show") == 0) {
            start_visible = TRUE;
            xutil_log("MAIN", "Flag --show specified: Starting overlay in visible state.");
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  --show    Start with overlay visible (default is hidden)\n");
            printf("  --help    Show this help text\n");
            return 0;
        }
    }

    if (!start_visible) {
        xutil_log("MAIN", "Default mode: Starting hidden (Press Ctrl+Shift+Alt+X to toggle).");
    }

    /* 1. Install non-fatal X error handlers */
    xutil_install_error_handlers();

    /* 2. Initialize GTK */
    gtk_init(&argc, &argv);

    /* 3. Initialize Window (override-redirect, RGBA visual, 360x640 at bottom-left) */
    if (!window_init(start_visible)) {
        xutil_log_err("MAIN", "Failed to initialize overlay window.");
        return 1;
    }

    /* 4. Initialize Global Hotkey System (dedicated Xlib Display, XI2 raw events + passive grab) */
    if (!hotkey_init()) {
        xutil_log_err("MAIN", "Failed to initialize global hotkey system.");
        window_cleanup();
        return 1;
    }

    /* 5. Setup lifecycle signal handling for SIGINT and SIGTERM via GLib */
    g_unix_signal_add(SIGINT, on_shutdown_signal, NULL);
    g_unix_signal_add(SIGTERM, on_shutdown_signal, NULL);
    xutil_log("MAIN", "Installed SIGINT/SIGTERM handlers via g_unix_signal_add.");

    /* 6. Run GLib main loop */
    xutil_log("MAIN", "Entering main loop.");
    gtk_main();

    xutil_log("MAIN", "Exited main loop cleanly.");
    return 0;
}
