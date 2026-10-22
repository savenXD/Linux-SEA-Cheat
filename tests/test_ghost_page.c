/* Native GTK/XTest regression test. Run in a dedicated X11 test display.
 * Includes the two implementation files to exercise the actual page controls. */
#include "../src/window.c"
#include "../src/ghost.c"
#include <assert.h>

static void pump(int ms) {
    gint64 end = g_get_monotonic_time() + ms * 1000;
    do {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    } while (g_get_monotonic_time() < end);
}

static char *received(Display *display) {
    GString *result = g_string_new(NULL);
    XSync(display, False);
    while (XPending(display)) {
        XEvent event; XNextEvent(display, &event);
        if (event.type == KeyPress) {
            char chars[32]; KeySym sym;
            int len = XLookupString(&event.xkey, chars, sizeof(chars), &sym, NULL);
            if (sym == XK_Return) g_string_append_c(result, '\n');
            else if (sym == XK_Tab) g_string_append_c(result, '\t');
            else if (len) g_string_append_len(result, chars, len);
        }
    }
    return g_string_free(result, FALSE);
}

static void click_widget(GtkWidget *widget) {
    int x, y, root_x, root_y;
    Window child;
    assert(gtk_widget_translate_coordinates(widget, s_window, 0, 0, &x, &y));
    Display *display = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    XTranslateCoordinates(display, s_xid, DefaultRootWindow(display), x, y, &root_x, &root_y, &child);
    XTestFakeMotionEvent(display, -1, root_x + gtk_widget_get_allocated_width(widget) / 2,
        root_y + gtk_widget_get_allocated_height(widget) / 2, 0);
    XTestFakeButtonEvent(display, 1, True, 0); XTestFakeButtonEvent(display, 1, False, 0);
    XFlush(display); pump(100);
}

static GtkWidget *find_label(GtkWidget *root, const char *text) {
    if (GTK_IS_LABEL(root) && strcmp(gtk_label_get_text(GTK_LABEL(root)), text) == 0) return root;
    if (!GTK_IS_CONTAINER(root)) return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *child = children; child && !found; child = child->next) found = find_label(child->data, text);
    g_list_free(children);
    return found;
}

static GtkWidget *button_with_label(GtkWidget *root, const char *text) {
    GtkWidget *widget = find_label(root, text);
    while (widget && !GTK_IS_BUTTON(widget)) widget = gtk_widget_get_parent(widget);
    assert(widget);
    return widget;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);
    assert(config_init()); assert(input_init()); assert(window_init(FALSE));
    overlay_show(); overlay_toggle_ghost_input(); pump(300);
    assert(strcmp(overlay_get_current_app(), "ghost") == 0);
    assert(gtk_widget_is_visible(s_test_entry));
    assert(gtk_widget_get_allocated_width(s_test_entry) > 200);
    assert(gtk_widget_get_allocated_height(s_test_entry) >= 60);
    assert(gtk_widget_get_allocated_width(s_window) <= 360);
    assert(gtk_widget_get_allocated_height(s_window) <= 640);
    assert(gtk_switch_get_active(GTK_SWITCH(s_ghost_human)));
    assert(ghost_get_timing().speed == 0);
    /* Human modes stay in real-typist range; human off is instant. */
    assert(ghost_speed_wpm(0, TRUE) >= 35 && ghost_speed_wpm(0, TRUE) <= 45);
    assert(ghost_speed_wpm(1, TRUE) >= 95 && ghost_speed_wpm(1, TRUE) <= 115);
    assert(ghost_speed_wpm(2, TRUE) >= 130 && ghost_speed_wpm(2, TRUE) <= 155);
    assert(ghost_speed_delay_ms(2, FALSE) == 0);
    /* Editor grows with its text but is capped. */
    int h0 = gtk_widget_get_allocated_height(s_test_entry);
    overlay_set_entry_text("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20"); pump(200);
    int h1 = gtk_widget_get_allocated_height(s_test_entry);
    assert(h1 > h0);
    GtkWidget *editor_scroll = gtk_widget_get_parent(s_test_entry);
    assert(gtk_widget_get_allocated_height(editor_scroll) <= 185);
    overlay_set_entry_text(""); pump(100);
    const char *code = "if (ok) {\n\t  hello(123);\n}\n";
    overlay_set_entry_text(code);
    assert(strcmp(overlay_get_test_entry_text(), code) == 0);
    overlay_toggle_ghost_input(); overlay_toggle_ghost_input();
    assert(!ghost_is_typing() && !s_ghost_countdown);
    overlay_execute_clipboard_action(CLIPBOARD_ACTION_SELECT_ALL);
    GtkTextIter begin, end;
    assert(gtk_text_buffer_get_selection_bounds(gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_test_entry)), &begin, &end));
    assert(gtk_widget_get_visible(s_ghost_mode_row));
    click_widget(s_ghost_modes[2]);
    assert(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s_ghost_modes[2])) && ghost_get_timing().speed == 2);
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), FALSE); pump(50);
    assert(!ghost_get_timing().human && !gtk_widget_get_visible(s_ghost_mode_row));
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), TRUE); pump(50);
    assert(gtk_widget_get_visible(s_ghost_mode_row));
    click_widget(s_ghost_modes[0]);
    assert(ghost_get_timing().speed == 0);
    GdkPixbuf *shot = gdk_pixbuf_get_from_window(gtk_widget_get_window(s_window), 0, 0,
        gtk_widget_get_allocated_width(s_window), gtk_widget_get_allocated_height(s_window));
    assert(shot); gdk_pixbuf_save(shot, "/tmp/ghost-page-test.png", "png", NULL, NULL); g_object_unref(shot);
    Display *display = XOpenDisplay(NULL); assert(display);
    Window target = XCreateSimpleWindow(display, DefaultRootWindow(display), 450, 20, 200, 200, 0, 0, 0);
    XSelectInput(display, target, KeyPressMask); XMapWindow(display, target); XSync(display, False);
    XSetInputFocus(display, target, RevertToParent, CurrentTime); XSync(display, False);
    /* Short deterministic intervals keep this test fast; production defaults were checked above. */
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), FALSE);
    on_ghost_start(NULL, NULL); assert(s_ghost_countdown);
    pump(500); assert(!ghost_is_typing());
    char *output = received(display); assert(!*output); g_free(output);
    pump(4000); assert(!ghost_is_typing());
    output = received(display);
    g_print("Received: %s\n", output);
    assert(strcmp(output, "if (ok) {\n\t  hello(123);\n}") == 0); g_free(output);
    assert(strcmp(overlay_get_test_entry_text(), code) == 0);
    pump(50); assert(!ghost_is_guard_active());
    /* A single-line payload never sends Return, even with pasted CRLF endings. */
    overlay_set_entry_text("do not submit\r\n\r\n");
    ghost_writer_run(); pump(1500);
    assert(!ghost_is_typing());
    output = received(display); assert(strcmp(output, "do not submit") == 0); g_free(output);
    pump(50);
    /* Stop during a chunk pause and ensure no trailing text leaks through. */
    overlay_set_entry_text("a\nb");
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s_ghost_modes[2]), TRUE);
    ghost_settings_save();
    s_timing.lines_min = s_timing.lines_max = 1;
    s_timing.pause_min_ms = s_timing.pause_max_ms = 1000;
    ghost_writer_run(); pump(900);
    assert(ghost_is_typing()); ghost_writer_cancel(); pump(1100);
    output = received(display); assert(strcmp(output, "a\n") == 0); g_free(output);
    assert(!ghost_is_guard_active());
    /* Focus changes cancel before another character is injected. */
    ghost_writer_run(); pump(900);
    XSetInputFocus(display, DefaultRootWindow(display), RevertToParent, CurrentTime); XSync(display, False);
    pump(1100); assert(!ghost_is_typing());
    output = received(display); assert(strcmp(output, "a\n") == 0); g_free(output);
    on_ghost_start(NULL, NULL); assert(s_ghost_countdown);
    on_ghost_start(NULL, NULL); assert(!s_ghost_countdown);
    overlay_set_entry_text(""); on_ghost_start(NULL, NULL); assert(!s_ghost_countdown);
    /* Instant mode (human off): a long text finishes fast, in order, without stalling the UI. */
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), FALSE);
    XSetInputFocus(display, target, RevertToParent, CurrentTime); XSync(display, False);
    GString *big = g_string_new(NULL);
    for (int i = 0; i < 40; i++) g_string_append(big, "line of text, number 1234567890\n");
    g_string_append(big, "end");
    overlay_set_entry_text(big->str);
    gint64 t0 = g_get_monotonic_time();
    ghost_writer_run();
    while (ghost_is_typing() && g_get_monotonic_time() - t0 < 6000000) pump(10);
    gint64 took = (g_get_monotonic_time() - t0) / 1000;
    assert(!ghost_is_typing());
    output = received(display);
    g_print("instant: %zu chars in %lld ms\n", strlen(output), (long long)took);
    assert(strcmp(output, big->str) == 0);
    assert(took < 2500);
    g_free(output); g_string_free(big, TRUE);
    pump(50);
    /* Human timing: every key is held (dwell) and gaps vary, measured from X event timestamps. */
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s_ghost_modes[2]), TRUE);
    ghost_settings_save();
    s_timing.lines_min = s_timing.lines_max = 100;
    XSelectInput(display, target, KeyPressMask | KeyReleaseMask);
    XSetInputFocus(display, target, RevertToParent, CurrentTime); XSync(display, False);
    overlay_set_entry_text("the quick brown fox");
    ghost_writer_run(); pump(5000); assert(!ghost_is_typing());
    unsigned long press[64], rel[64]; int np = 0, nr = 0;
    XSync(display, False);
    while (XPending(display)) {
        XEvent ev; XNextEvent(display, &ev);
        if (ev.type == KeyPress && np < 64) press[np++] = ev.xkey.time;
        if (ev.type == KeyRelease && nr < 64) rel[nr++] = ev.xkey.time;
    }
    assert(np == 19 && nr == 19);
    unsigned long min_dwell = 1000000, min_gap = 1000000, max_gap = 0;
    for (int i = 0; i < np; i++) {
        unsigned long d = rel[i] - press[i]; if (d < min_dwell) min_dwell = d;
        if (i) { unsigned long g = press[i] - press[i - 1]; if (g < min_gap) min_gap = g; if (g > max_gap) max_gap = g; }
    }
    g_print("human dwell min %lums, gap %lu..%lums\n", min_dwell, min_gap, max_gap);
    assert(min_dwell >= 25);
    assert(max_gap > min_gap + 20);
    /* Extra safe: mistakes are made, noticed, backspaced and corrected; final text is exact. */
    assert(gtk_widget_is_visible(s_ghost_safe));
    gtk_switch_set_active(GTK_SWITCH(s_ghost_safe), TRUE);
    assert(ghost_get_timing().safe);
    s_typo_pct = 100;
    XSelectInput(display, target, KeyPressMask);
    XSetInputFocus(display, target, RevertToParent, CurrentTime); XSync(display, False);
    const char *safe_text = "the quick brown fox jumps over";
    overlay_set_entry_text(safe_text);
    s_timing.lines_min = s_timing.lines_max = 100;
    ghost_writer_run();
    gint64 s0 = g_get_monotonic_time();
    while (ghost_is_typing() && g_get_monotonic_time() - s0 < 60000000) pump(20);
    assert(!ghost_is_typing());
    XSync(display, False);
    GString *shown = g_string_new(NULL); int backspaces = 0;
    while (XPending(display)) {
        XEvent ev; XNextEvent(display, &ev);
        if (ev.type != KeyPress) continue;
        char chars[8]; KeySym sym;
        int len = XLookupString(&ev.xkey, chars, sizeof(chars), &sym, NULL);
        if (sym == XK_BackSpace) { backspaces++; if (shown->len) g_string_truncate(shown, shown->len - 1); }
        else if (len) g_string_append_len(shown, chars, len);
    }
    g_print("safe: %d backspaces, final '%s' in %lld ms\n", backspaces, shown->str, (long long)((g_get_monotonic_time() - s0) / 1000));
    assert(backspaces >= 2);
    assert(strcmp(shown->str, safe_text) == 0);
    g_string_free(shown, TRUE);
    s_typo_pct = 3;
    gtk_switch_set_active(GTK_SWITCH(s_ghost_safe), FALSE);
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), FALSE); pump(50);
    assert(!gtk_widget_is_visible(s_ghost_safe));
    gtk_switch_set_active(GTK_SWITCH(s_ghost_human), TRUE);
    XSelectInput(display, target, KeyPressMask);
    if (s_toast_timer_id) { g_source_remove(s_toast_timer_id); s_toast_timer_id = 0; }
    gtk_widget_hide(s_toast_box);
    shot = gdk_pixbuf_get_from_window(gtk_widget_get_window(s_window), 0, 0,
        gtk_widget_get_allocated_width(s_window), gtk_widget_get_allocated_height(s_window));
    gdk_pixbuf_save(shot, "/tmp/ghost-page-default.png", "png", NULL, NULL); g_object_unref(shot);
    overlay_go_home(); pump(400);
    assert(gtk_widget_get_allocated_height(s_window) <= OVERLAY_COMPACT_HEIGHT);
    shot = gdk_pixbuf_get_from_window(gtk_widget_get_window(s_window), 0, 0,
        gtk_widget_get_allocated_width(s_window), gtk_widget_get_allocated_height(s_window));
    gdk_pixbuf_save(shot, "/tmp/ghost-home-redesign.png", "png", NULL, NULL); g_object_unref(shot);
    GtkWidget *home_page = gtk_stack_get_child_by_name(GTK_STACK(s_stack), "home");
    click_widget(button_with_label(home_page, "Ghost Typer")); pump(300);
    assert(strcmp(overlay_get_current_app(), "ghost") == 0);
    overlay_set_entry_text("clear this");
    XDestroyWindow(display, target); XCloseDisplay(display);
    window_cleanup(); ghost_cleanup(); input_cleanup(); config_cleanup();
    g_print("PASS: page navigation, layout, editor, speed slider/human switch, countdown, exact multiline injection, no final Enter, chunk pauses, cancellation, focus protection, human key dwell/gap timing and home launcher\n");
    return 0;
}
