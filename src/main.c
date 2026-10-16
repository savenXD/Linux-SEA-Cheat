#include "overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static OverlayApp g_app;

static void signal_handler(int sig) {
    printf("\n[Signal] Caught signal %d. Shutting down cleanly...\n", sig);
    if (gtk_main_level() > 0) {
        gtk_main_quit();
    }
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

int main(int argc, char **argv) {
    /* Ensure immediate flushing of stdout/stderr even when redirected to file/pipe */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("===========================================\n");
    printf(" Starting PhoneOverlay (Phase 1: Foundation)\n");
    printf("===========================================\n");

    /* Install robust X error handlers early */
    x11_install_error_handlers();
    setup_signals();

    /* Initialize GTK */
    gtk_init(&argc, &argv);

    /* Initialize X11 dominance module with separate Display connection */
    memset(&g_app, 0, sizeof(g_app));
    if (!x11_dom_init(&g_app)) {
        fprintf(stderr, "[Fatal] Failed to initialize X11 dominance module.\n");
        return 1;
    }

    /* Create the override-redirect phone-styled overlay */
    ui_create_window(&g_app);

    /* Attach the dedicated X11 connection & heartbeat to the GLib main loop */
    x11_dom_attach_glib(&g_app);

    printf("[Main] Overlay active. Running GLib main loop...\n");
    gtk_main();

    printf("[Main] Cleaning up resources...\n");
    x11_dom_cleanup(&g_app);
    if (g_app.window) {
        gtk_widget_destroy(g_app.window);
    }

    printf("[Main] Exited cleanly.\n");
    return 0;
}
