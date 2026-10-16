#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>

static Window get_toplevel_root_child(Display *dpy, Window root, Window w) {
    Window curr = w;
    while (1) {
        Window root_ret, parent_ret;
        Window *children = NULL;
        unsigned int nchildren = 0;
        if (!XQueryTree(dpy, curr, &root_ret, &parent_ret, &children, &nchildren)) {
            if (children) XFree(children);
            return None;
        }
        if (children) XFree(children);
        if (parent_ret == root || parent_ret == None) {
            return curr;
        }
        curr = parent_ret;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <top_xid> <bottom_xid>\n", argv[0]);
        return 1;
    }

    Window top_arg = (Window)strtoul(argv[1], NULL, 0);
    Window bottom_arg = (Window)strtoul(argv[2], NULL, 0);

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 2;
    }

    Window root = RootWindow(dpy, DefaultScreen(dpy));

    Window top_xid = get_toplevel_root_child(dpy, root, top_arg);
    Window bottom_xid = get_toplevel_root_child(dpy, root, bottom_arg);

    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    int top_idx = -1;
    int bottom_idx = -1;

    if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            if (children[i] == top_xid) top_idx = (int)i;
            if (children[i] == bottom_xid) bottom_idx = (int)i;
        }
        if (children) XFree(children);
    }
    XCloseDisplay(dpy);

    printf("Check Stacking: top_xid 0x%lx (root child 0x%lx)=%d, bottom_xid 0x%lx (root child 0x%lx)=%d, total=%u\n",
           (unsigned long)top_arg, (unsigned long)top_xid, top_idx,
           (unsigned long)bottom_arg, (unsigned long)bottom_xid, bottom_idx, nchildren);

    if (top_idx == -1 || bottom_idx == -1) {
        fprintf(stderr, "Error: One or both windows not found in XQueryTree!\n");
        return 3;
    }

    if (top_idx > bottom_idx) {
        printf("PASS: 0x%lx is strictly above 0x%lx in stacking order (%d > %d).\n",
               (unsigned long)top_arg, (unsigned long)bottom_arg, top_idx, bottom_idx);
        return 0;
    } else {
        printf("FAIL: 0x%lx is below 0x%lx in stacking order (%d <= %d)!\n",
               (unsigned long)top_arg, (unsigned long)bottom_arg, top_idx, bottom_idx);
        return 4;
    }
}
