#include "window.h"
#include "hotkey.h"
#include "raise.h"
#include "input.h"
#include "config.h"
#include "browser.h"
#include "ghost.h"
#include "xutil.h"

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int         s_ipc_listen_fd = -1;
static GIOChannel *s_ipc_channel = NULL;
static guint       s_ipc_watch_id = 0;
static char        s_ipc_sock_path[1024] = {0};

static void get_ipc_socket_path(char *out, size_t size) {
    const char *data_dir = g_get_user_data_dir();
    snprintf(out, size, "%s/phoneoverlay/ipc.sock", data_dir);
}

static bool send_ipc_command(const char *cmd, char *response_out, size_t response_size) {
    char sock_path[1024];
    get_ipc_socket_path(sock_path, sizeof(sock_path));

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, sock_path, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    /* Send command line */
    size_t len = strlen(cmd);
    if (write(fd, cmd, len) < 0) {
        close(fd);
        return false;
    }
    if (len == 0 || cmd[len - 1] != '\n') {
        if (write(fd, "\n", 1) < 0) {
            close(fd);
            return false;
        }
    }

    /* Read response */
    if (response_out && response_size > 0) {
        ssize_t n = read(fd, response_out, response_size - 1);
        if (n > 0) response_out[n] = '\0';
        else response_out[0] = '\0';
    }

    close(fd);
    return true;
}

static gboolean on_ipc_client_connected(GIOChannel *source, GIOCondition condition, gpointer user_data) {
    (void)source;
    (void)condition;
    (void)user_data;

    if (s_ipc_listen_fd < 0) return G_SOURCE_REMOVE;

    int client_fd = accept(s_ipc_listen_fd, NULL, NULL);
    if (client_fd < 0) return G_SOURCE_CONTINUE;

    char buf[1024] = {0};
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        char *line = g_strstrip(buf);
        xutil_log("IPC", "Received command: '%s'", line);

        char resp[512] = "OK\n";

        if (g_ascii_strcasecmp(line, "TOGGLE") == 0) {
            overlay_toggle();
        } else if (g_ascii_strcasecmp(line, "SHOW") == 0) {
            overlay_show();
        } else if (g_ascii_strcasecmp(line, "HIDE") == 0) {
            overlay_hide();
        } else if (g_ascii_strcasecmp(line, "MODE mirror") == 0) {
            input_set_key_mode(KEY_MODE_MIRROR);
        } else if (g_ascii_strcasecmp(line, "MODE exclusive") == 0) {
            input_set_key_mode(KEY_MODE_EXCLUSIVE);
        } else if (g_ascii_strcasecmp(line, "MODE") == 0) {
            input_cycle_key_mode();
        } else if (g_ascii_strcasecmp(line, "GHOST on") == 0) {
            input_set_ghost_mode(GHOST_MODE_ON);
        } else if (g_ascii_strcasecmp(line, "GHOST off") == 0) {
            input_set_ghost_mode(GHOST_MODE_OFF);
        } else if (g_ascii_strcasecmp(line, "GHOST") == 0) {
            input_toggle_ghost_mode();
        } else if (g_str_has_prefix(line, "LOAD ")) {
            const char *url = line + 5;
            while (*url == ' ') url++;
            overlay_switch_app("browser");
            browser_load_in_browser(url);
        } else if (g_str_has_prefix(line, "APP ")) {
            const char *app = line + 4;
            while (*app == ' ') app++;
            overlay_switch_app(app);
        } else if (g_ascii_strcasecmp(line, "BACK") == 0) {
            overlay_back();
        } else if (g_ascii_strcasecmp(line, "HOME") == 0) {
            overlay_go_home();
        } else if (g_ascii_strcasecmp(line, "RELOAD") == 0) {
            overlay_reload();
        } else if (g_ascii_strcasecmp(line, "GET_TEST_ENTRY") == 0) {
            snprintf(resp, sizeof(resp), "%s\n", overlay_get_test_entry_text());
        } else if (g_ascii_strcasecmp(line, "CLEAR_TEST_ENTRY") == 0) {
            overlay_clear_entry_text();
        } else if (g_str_has_prefix(line, "SET_TEST_ENTRY")) {
            const char *txt = line + 14;
            while (*txt == ' ') txt++;
            overlay_set_entry_text(txt);
        } else if (g_ascii_strcasecmp(line, "HAS_MODAL") == 0) {
            snprintf(resp, sizeof(resp), "%s\n", overlay_has_popup_modal() ? "TRUE" : "FALSE");
        } else if (g_ascii_strcasecmp(line, "CLOSE_MODAL") == 0) {
            overlay_close_popup_modal();
        } else if (g_ascii_strcasecmp(line, "SNAP") == 0) {
            overlay_snap();
        } else if (g_ascii_strcasecmp(line, "GHOST_WRITE") == 0) {
            ghost_writer_run();
        } else if (g_ascii_strcasecmp(line, "PASTE") == 0) {
            overlay_execute_clipboard_action(CLIPBOARD_ACTION_PASTE);
        } else if (g_ascii_strcasecmp(line, "RELOAD_CONFIG") == 0) {
            config_reload();
            overlay_apply_config();
            hotkey_reload_grabs();
        } else if (g_ascii_strcasecmp(line, "STATUS") == 0) {
            snprintf(resp, sizeof(resp), "MODE=%s TARGET=%s GHOST=%s GRAB=%s VISIBLE=%s APP=%s\n",
                     input_get_key_mode() == KEY_MODE_EXCLUSIVE ? "EXCLUSIVE" : "MIRROR",
                     input_get_typing_target() == TYPING_TARGET_OVERLAY ? "OVERLAY" : "NONE",
                     input_get_ghost_mode() == GHOST_MODE_ON ? "GHOST" : "SOLID",
                     input_get_grab_status() == GRAB_STATUS_ACTIVE ? "ACTIVE" :
                     (input_get_grab_status() == GRAB_STATUS_FAILED ? "FAILED" : "NONE"),
                     overlay_is_visible() ? "TRUE" : "FALSE",
                     overlay_get_current_app());
        } else {
            snprintf(resp, sizeof(resp), "UNKNOWN_COMMAND\n");
        }

        ssize_t w = write(client_fd, resp, strlen(resp));
        (void)w;
    }

    close(client_fd);
    return G_SOURCE_CONTINUE;
}

static bool ipc_server_init(void) {
    get_ipc_socket_path(s_ipc_sock_path, sizeof(s_ipc_sock_path));

    char *dir = g_path_get_dirname(s_ipc_sock_path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    /* Test if socket is actively listening */
    int test_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (test_fd >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        g_strlcpy(addr.sun_path, s_ipc_sock_path, sizeof(addr.sun_path));
        if (connect(test_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            close(test_fd);
            xutil_log_err("MAIN", "Another instance of PhoneOverlay is already running on '%s'.", s_ipc_sock_path);
            return false;
        }
        close(test_fd);
    }
    unlink(s_ipc_sock_path);

    s_ipc_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s_ipc_listen_fd < 0) {
        xutil_log_err("MAIN", "Failed to create IPC socket: %s", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, s_ipc_sock_path, sizeof(addr.sun_path));

    if (bind(s_ipc_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        xutil_log_err("MAIN", "Failed to bind IPC socket to '%s': %s", s_ipc_sock_path, strerror(errno));
        close(s_ipc_listen_fd);
        s_ipc_listen_fd = -1;
        return false;
    }

    if (listen(s_ipc_listen_fd, 5) < 0) {
        xutil_log_err("MAIN", "Failed to listen on IPC socket: %s", strerror(errno));
        close(s_ipc_listen_fd);
        s_ipc_listen_fd = -1;
        return false;
    }

    s_ipc_channel = g_io_channel_unix_new(s_ipc_listen_fd);
    s_ipc_watch_id = g_io_add_watch(s_ipc_channel, G_IO_IN | G_IO_HUP, on_ipc_client_connected, NULL);

    xutil_log("MAIN", "IPC socket listening on '%s'.", s_ipc_sock_path);
    return true;
}

static void ipc_server_cleanup(void) {
    if (s_ipc_watch_id > 0) {
        g_source_remove(s_ipc_watch_id);
        s_ipc_watch_id = 0;
    }
    if (s_ipc_channel) {
        g_io_channel_unref(s_ipc_channel);
        s_ipc_channel = NULL;
    }
    if (s_ipc_listen_fd >= 0) {
        close(s_ipc_listen_fd);
        s_ipc_listen_fd = -1;
    }
    if (s_ipc_sock_path[0] != '\0') {
        unlink(s_ipc_sock_path);
    }
}

static gboolean on_sighup_signal(gpointer user_data) {
    (void)user_data;
    xutil_log("MAIN", "SIGHUP received. Reloading configuration...");
    config_reload();
    overlay_apply_config();
    hotkey_reload_grabs();
    return G_SOURCE_CONTINUE;
}

static gboolean on_shutdown_signal(gpointer user_data) {
    (void)user_data;
    xutil_log("MAIN", "Shutdown signal received. Performing clean teardown...");

    overlay_hide();
    ghost_cleanup();
    input_cleanup();
    raise_cleanup();
    hotkey_cleanup();
    browser_cleanup();
    window_cleanup();
    config_cleanup();
    ipc_server_cleanup();

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

    /* Check CLI arguments first */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--toggle") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("TOGGLE", resp, sizeof(resp))) {
                printf("PhoneOverlay: Toggled visibility.\n");
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--hide") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("HIDE", resp, sizeof(resp))) {
                printf("PhoneOverlay: Overlay hidden.\n");
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--mode") == 0) {
            char cmd[64] = "MODE";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                snprintf(cmd, sizeof(cmd), "MODE %s", argv[++i]);
            }
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                printf("PhoneOverlay: %s\n", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--ghost") == 0) {
            char cmd[64] = "GHOST";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                snprintf(cmd, sizeof(cmd), "GHOST %s", argv[++i]);
            }
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                printf("PhoneOverlay: %s\n", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--load") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --load requires a URL argument.\n");
                return 1;
            }
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "LOAD %s", argv[++i]);
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                printf("PhoneOverlay: Loaded '%s'.\n", argv[i]);
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--app") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --app requires an app name (home, chatgpt, browser).\n");
                return 1;
            }
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "APP %s", argv[++i]);
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                printf("PhoneOverlay: Switched to '%s'.\n", argv[i]);
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--back") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("BACK", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--home") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("HOME", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--reload") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("RELOAD", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--get-entry") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("GET_TEST_ENTRY", resp, sizeof(resp))) {
                printf("%s", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--set-entry") == 0 && i + 1 < argc) {
            char cmd[2048];
            snprintf(cmd, sizeof(cmd), "SET_TEST_ENTRY %s", argv[i + 1]);
            char resp[256] = {0};
            if (send_ipc_command(cmd, resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--clear-entry") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("CLEAR_TEST_ENTRY", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--has-modal") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("HAS_MODAL", resp, sizeof(resp))) {
                printf("%s", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--close-modal") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("CLOSE_MODAL", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--snap") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("SNAP", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--ghost-write") == 0 || strcmp(argv[i], "--ghost-writer") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("GHOST_WRITE", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--paste") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("PASTE", resp, sizeof(resp))) {
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--reload-config") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("RELOAD_CONFIG", resp, sizeof(resp))) {
                printf("PhoneOverlay: Configuration reloaded.\n");
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--status") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("STATUS", resp, sizeof(resp))) {
                printf("%s", resp);
                return 0;
            } else {
                fprintf(stderr, "Error: PhoneOverlay is not running.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  --show             Start with overlay visible (or show running overlay)\n");
            printf("  --hide             Hide running overlay\n");
            printf("  --toggle           Toggle visibility of running overlay\n");
            printf("  --mode [m|e]       Set or cycle typing mode (mirror / exclusive)\n");
            printf("  --ghost [on|off]   Set or toggle GHOST click-through mode\n");
            printf("  --app <name>       Switch app view (home, chatgpt, browser)\n");
            printf("  --load <url>       Load URL in browser\n");
            printf("  --back             Navigate back in current app (or home if at start)\n");
            printf("  --home             Return to two-app home screen\n");
            printf("  --reload           Reload active webview\n");
            printf("  --get-entry        Print current text from Test Input\n");
            printf("  --has-modal        Query if an internal modal popup is displayed\n");
            printf("  --close-modal      Close internal modal popup if displayed\n");
            printf("  --snap             Capture screenshot (excluding overlay) to clipboard & disk\n");
            printf("  --ghost-write      Trigger Ghost Writer keyboard injection\n");
            printf("  --paste            Trigger paste into overlay (entry or active webview)\n");
            printf("  --reload-config    Reload configuration from config.ini\n");
            printf("  --status           Query running overlay status\n");
            printf("  --help, -h         Show this help text\n");
            return 0;
        }
    }

    /* Check --show for already running instance */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--show") == 0) {
            char resp[256] = {0};
            if (send_ipc_command("SHOW", resp, sizeof(resp))) {
                printf("PhoneOverlay: Overlay shown.\n");
                return 0;
            }
            /* Not running yet; proceed to launch daemon with start_visible = TRUE */
            break;
        }
    }

    xutil_log("MAIN", "=== Starting PhoneOverlay (Phase 5) ===");

    /* 1. Install non-fatal X error handlers */
    xutil_install_error_handlers();

    /* 2. Initialize GTK */
    gtk_init(&argc, &argv);

    /* 3. Initialize Configuration */
    if (!config_init()) {
        xutil_log_err("MAIN", "Failed to initialize configuration.");
        return 1;
    }

    const AppConfig *cfg = config_get();
    gboolean start_visible = cfg->start_visible;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--show") == 0) {
            start_visible = TRUE;
        }
    }

    /* 4. Single-instance IPC socket setup */
    if (!ipc_server_init()) {
        config_cleanup();
        return 1;
    }

    /* 5. Initialize Input Subsystem */
    if (!input_init()) {
        xutil_log_err("MAIN", "Failed to initialize input subsystem.");
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }
    input_set_key_mode(cfg->default_key_mode);

    /* 5b. Initialize Ghost Writer Subsystem */
    if (!ghost_init()) {
        xutil_log_err("MAIN", "Failed to initialize Ghost Writer subsystem.");
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 6. Initialize WebKitGTK Browser Subsystem */
    if (!browser_init()) {
        xutil_log_err("MAIN", "Failed to initialize browser subsystem.");
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 7. Initialize Restack Watcher */
    if (!raise_init()) {
        xutil_log_err("MAIN", "Failed to initialize restack watcher.");
        browser_cleanup();
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 8. Initialize Window */
    if (!window_init(start_visible)) {
        xutil_log_err("MAIN", "Failed to initialize overlay window.");
        raise_cleanup();
        browser_cleanup();
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 9. Initialize Global Hotkey System */
    if (!hotkey_init()) {
        xutil_log_err("MAIN", "Failed to initialize global hotkey system.");
        window_cleanup();
        raise_cleanup();
        browser_cleanup();
        input_cleanup();
        ipc_server_cleanup();
        config_cleanup();
        return 1;
    }

    /* 10. Setup UNIX signal handlers */
    g_unix_signal_add(SIGINT, on_shutdown_signal, NULL);
    g_unix_signal_add(SIGTERM, on_shutdown_signal, NULL);
    g_unix_signal_add(SIGHUP, on_sighup_signal, NULL);
    xutil_log("MAIN", "Installed SIGINT/SIGTERM/SIGHUP handlers via g_unix_signal_add.");

    if (!start_visible) {
        printf("\n========================================================================\n");
        printf(" PhoneOverlay started in BACKGROUND mode (default: HIDDEN)\n");
        printf(" - Press [Ctrl] + [Shift] + [Alt] + [%c] to toggle visibility.\n", cfg->toggle_key);
        printf(" - Press [Ctrl] + [Shift] + [Alt] + [%c] to cycle MIRROR / EXCLUSIVE.\n", cfg->mode_key);
        printf(" - Press [Ctrl] + [Shift] + [Alt] + [%c] to toggle GHOST mode.\n", cfg->ghost_key);
        printf(" - CLI control: %s --toggle | --show | --hide | --status\n", argv[0]);
        printf(" - Press [Ctrl] + [C] to exit.\n");
        printf("========================================================================\n\n");
    }

    /* 11. Run GLib main loop */
    xutil_log("MAIN", "Entering main loop.");
    gtk_main();

    xutil_log("MAIN", "Exited main loop cleanly.");
    return 0;
}

