#include "overlay.h"
#include <stdio.h>

static void on_test_button_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    OverlayApp *app = (OverlayApp *)user_data;
    static int click_count = 0;
    click_count++;

    char buf[128];
    snprintf(buf, sizeof(buf), "Clicked: %d times (Focus Untouched)", click_count);
    ui_update_status(app, buf);
    printf("[UI Event] Overlay test button clicked %d times!\n", click_count);
}

static void on_test_btn_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data) {
    (void)widget;
    OverlayApp *app = (OverlayApp *)user_data;
    int btn_screen_x = app->overlay_x + allocation->x + allocation->width / 2;
    int btn_screen_y = app->overlay_y + allocation->y + allocation->height / 2;
    printf("[UI] Test Button Center: (%d, %d)\n", btn_screen_x, btn_screen_y);
}

void ui_update_status(OverlayApp *app, const char *status) {
    if (app->status_label) {
        gtk_label_set_text(GTK_LABEL(app->status_label), status);
    }
}

void ui_create_window(OverlayApp *app) {
    /* Create GTK_WINDOW_POPUP -> Sets override_redirect = True at X11 level */
    app->window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_default_size(GTK_WINDOW(app->window), app->overlay_w, app->overlay_h);
    gtk_widget_set_size_request(app->window, app->overlay_w, app->overlay_h);
    gtk_window_set_resizable(GTK_WINDOW(app->window), FALSE);

    /* Enforce ZERO focus stealing at GTK layer */
    gtk_window_set_accept_focus(GTK_WINDOW(app->window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(app->window), FALSE);

    /* Realize widget to obtain underlying GdkWindow and XID */
    gtk_widget_realize(app->window);
    GdkWindow *gdk_win = gtk_widget_get_window(app->window);
    app->overlay_xid = gdk_x11_window_get_xid(gdk_win);
    app->gtk_dpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());

    printf("[UI] Created GTK_WINDOW_POPUP with XID 0x%lx (%lu)\n",
           (unsigned long)app->overlay_xid, (unsigned long)app->overlay_xid);

    /* Position window at bottom-left of primary monitor via raw Xlib */
    XMoveResizeWindow(app->sub_dpy, app->overlay_xid,
                      app->overlay_x, app->overlay_y,
                      app->overlay_w, app->overlay_h);

    /* Set raw XWMHints: input = False to ensure X11 knows this window never accepts keyboard focus */
    XWMHints *hints = XGetWMHints(app->sub_dpy, app->overlay_xid);
    if (!hints) {
        hints = XAllocWMHints();
    }
    if (hints) {
        hints->flags |= InputHint;
        hints->input = False;
        XSetWMHints(app->sub_dpy, app->overlay_xid, hints);
        XFree(hints);
    }

    /* Apply Phone-Like CSS Styling */
    GtkCssProvider *css = gtk_css_provider_new();
    const char *style =
        "* { font-family: sans-serif; }"
        "window { background-color: #181825; color: #cdd6f4; border: 2px solid #313244; border-radius: 12px; }"
        ".header-bar { background-color: #11111b; padding: 8px 12px; border-bottom: 1px solid #313244; }"
        ".title { font-weight: bold; font-size: 13px; color: #89b4fa; }"
        ".status-pill { background-color: #313244; color: #a6adc8; border-radius: 6px; padding: 2px 6px; font-size: 11px; }"
        ".content-box { padding: 16px; }"
        ".info-box { background-color: #1e1e2e; border: 1px solid #313244; border-radius: 8px; padding: 12px; margin-bottom: 12px; }"
        "button { background-color: #89b4fa; color: #11111b; font-weight: bold; border-radius: 8px; padding: 8px 12px; border: none; }"
        "button:hover { background-color: #b4befe; }"
        "button:active { background-color: #74c7ec; }";

    gtk_css_provider_load_from_data(css, style, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(css);

    /* Main Container (vertical box) */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    /* Phone Header Bar */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "header-bar");
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    GtkWidget *title_lbl = gtk_label_new("PhoneOverlay");
    gtk_style_context_add_class(gtk_widget_get_style_context(title_lbl), "title");
    gtk_box_pack_start(GTK_BOX(header), title_lbl, FALSE, FALSE, 0);

    GtkWidget *mode_pill = gtk_label_new("Phase 1: Foundation");
    gtk_style_context_add_class(gtk_widget_get_style_context(mode_pill), "status-pill");
    gtk_box_pack_end(GTK_BOX(header), mode_pill, FALSE, FALSE, 0);

    /* Content Area */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(content), "content-box");
    gtk_box_pack_start(GTK_BOX(vbox), content, TRUE, TRUE, 0);

    /* Information Card */
    GtkWidget *info_card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(info_card), "info-box");
    gtk_box_pack_start(GTK_BOX(content), info_card, FALSE, FALSE, 0);

    char geom_str[128];
    snprintf(geom_str, sizeof(geom_str), "Geometry: %dx%d @ (%d,%d)",
             app->overlay_w, app->overlay_h, app->overlay_x, app->overlay_y);
    GtkWidget *geom_lbl = gtk_label_new(geom_str);
    gtk_label_set_xalign(GTK_LABEL(geom_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(info_card), geom_lbl, FALSE, FALSE, 0);

    GtkWidget *or_lbl = gtk_label_new("Type: Override-Redirect (X11 Direct)");
    gtk_label_set_xalign(GTK_LABEL(or_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(info_card), or_lbl, FALSE, FALSE, 0);

    GtkWidget *focus_lbl = gtk_label_new("Focus: Non-stealing (InputHint=0)");
    gtk_label_set_xalign(GTK_LABEL(focus_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(info_card), focus_lbl, FALSE, FALSE, 0);

    /* Status Label */
    app->status_label = gtk_label_new("Status: Ready (Dominance Active)");
    gtk_label_set_xalign(GTK_LABEL(app->status_label), 0.0);
    gtk_box_pack_start(GTK_BOX(info_card), app->status_label, FALSE, FALSE, 0);

    /* Interactive Test Button */
    GtkWidget *test_btn = gtk_button_new_with_label("Click Test (Focus Safe)");
    g_signal_connect(test_btn, "clicked", G_CALLBACK(on_test_button_clicked), app);
    g_signal_connect(test_btn, "size-allocate", G_CALLBACK(on_test_btn_allocate), app);
    gtk_box_pack_start(GTK_BOX(content), test_btn, FALSE, FALSE, 0);

    gtk_widget_show_all(app->window);
    app->is_visible = TRUE;

    /* Re-enforce position and top stacking */
    XMoveResizeWindow(app->sub_dpy, app->overlay_xid,
                      app->overlay_x, app->overlay_y,
                      app->overlay_w, app->overlay_h);
    x11_dom_raise_overlay(app);
}
