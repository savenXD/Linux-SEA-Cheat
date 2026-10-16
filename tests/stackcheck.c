#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>

static unsigned long get_process_ticks(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    unsigned long utime = 0, stime = 0;
    /* Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime */
    if (fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &utime, &stime) == 2) {
        fclose(f);
        return utime + stime;
    }
    fclose(f);
    return 0;
}

static int run_monitor(Window target, int duration_sec, int interval_ms, pid_t pid, double min_percent, double max_cpu) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "[MONITOR] Cannot open display\n");
        return 2;
    }

    Window root = DefaultRootWindow(dpy);
    long clk_tck = sysconf(_SC_CLK_TCK);
    unsigned long start_ticks = (pid > 0) ? get_process_ticks(pid) : 0;
    struct timespec start_time, now_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    long total_samples = 0;
    long topmost_samples = 0;
    long non_topmost_samples = 0;

    useconds_t sleep_usec = (useconds_t)interval_ms * 1000;

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now_time);
        double elapsed = (now_time.tv_sec - start_time.tv_sec) +
                         (now_time.tv_nsec - start_time.tv_nsec) / 1e9;
        if (elapsed >= duration_sec) {
            break;
        }

        Window root_ret, parent_ret;
        Window *children = NULL;
        unsigned int nchildren = 0;

        int is_top = 0;
        if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
            if (nchildren > 0 && children) {
                if (children[nchildren - 1] == target) {
                    is_top = 1;
                } else if (non_topmost_samples < 5) {
                    /* Print first few offenders */
                    fprintf(stderr, "[MONITOR] Non-topmost sample: actual top=0x%lx (target=0x%lx)\n",
                            (unsigned long)children[nchildren - 1], (unsigned long)target);
                }
            }
            if (children) XFree(children);
        }

        total_samples++;
        if (is_top) {
            topmost_samples++;
        } else {
            non_topmost_samples++;
        }

        usleep(sleep_usec);
    }

    clock_gettime(CLOCK_MONOTONIC, &now_time);
    double total_elapsed = (now_time.tv_sec - start_time.tv_sec) +
                           (now_time.tv_nsec - start_time.tv_nsec) / 1e9;

    double percent_topmost = (total_samples > 0) ? ((double)topmost_samples / total_samples) * 100.0 : 0.0;

    double cpu_percent = 0.0;
    if (pid > 0 && clk_tck > 0 && total_elapsed > 0) {
        unsigned long end_ticks = get_process_ticks(pid);
        unsigned long diff_ticks = (end_ticks >= start_ticks) ? (end_ticks - start_ticks) : 0;
        cpu_percent = ((double)diff_ticks / ((double)clk_tck * total_elapsed)) * 100.0;
    }

    XCloseDisplay(dpy);

    printf("MONITOR_RESULT: duration=%.2fs total_samples=%ld topmost_samples=%ld non_topmost=%ld percent=%.2f%% cpu=%.2f%%\n",
           total_elapsed, total_samples, topmost_samples, non_topmost_samples, percent_topmost, cpu_percent);
    fflush(stdout);

    int ok = 1;
    if (percent_topmost < min_percent) {
        fprintf(stderr, "[MONITOR] FAIL: Topmost percentage %.2f%% < required %.2f%%\n", percent_topmost, min_percent);
        ok = 0;
    }
    if (max_cpu > 0.0 && cpu_percent > max_cpu) {
        fprintf(stderr, "[MONITOR] FAIL: CPU usage %.2f%% > max allowed %.2f%%\n", cpu_percent, max_cpu);
        ok = 0;
    }

    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s [-v] <target_xid>\n", argv[0]);
        fprintf(stderr, "  %s --monitor <target_xid> <duration_sec> <interval_ms> [pid] [min_percent] [max_cpu]\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "--monitor") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s --monitor <target_xid> <duration_sec> <interval_ms> [pid] [min_percent] [max_cpu]\n", argv[0]);
            return 2;
        }
        Window target = (Window)strtoul(argv[2], NULL, 0);
        int duration_sec = atoi(argv[3]);
        int interval_ms = atoi(argv[4]);
        pid_t pid = (argc > 5) ? atoi(argv[5]) : 0;
        double min_percent = (argc > 6) ? atof(argv[6]) : 99.0;
        double max_cpu = (argc > 7) ? atof(argv[7]) : 0.0;

        return run_monitor(target, duration_sec, interval_ms, pid, min_percent, max_cpu);
    }

    int verbose = 0;
    const char *xid_str = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else {
            xid_str = argv[i];
        }
    }

    if (!xid_str) {
        fprintf(stderr, "Usage: %s [-v] <target_xid>\n", argv[0]);
        return 2;
    }

    Window target = (Window)strtoul(xid_str, NULL, 0);
    if (target == None) {
        fprintf(stderr, "Invalid window ID: %s\n", xid_str);
        return 2;
    }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 2;
    }

    Window root = DefaultRootWindow(dpy);
    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
        XCloseDisplay(dpy);
        return 2;
    }

    int is_topmost = 0;
    Window topmost = None;
    if (nchildren > 0 && children) {
        topmost = children[nchildren - 1];
        if (topmost == target) {
            is_topmost = 1;
        }
    }

    if (verbose) {
        printf("target=0x%lx topmost=0x%lx is_topmost=%d nchildren=%u\n",
               (unsigned long)target, (unsigned long)topmost, is_topmost, nchildren);
    }

    if (children) {
        XFree(children);
    }
    XCloseDisplay(dpy);

    return is_topmost ? 0 : 1;
}
