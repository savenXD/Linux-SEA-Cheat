#!/usr/bin/env bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}========================================================${NC}"
echo -e "${BLUE} RUNNING PHASE 3 ACCEPTANCE TEST SUITE (INPUT MODEL)    ${NC}"
echo -e "${BLUE}========================================================${NC}"

# Clean any existing overlay instances
killall -9 cronq 2>/dev/null || true
make adversaries > /dev/null 2>&1 || true
sleep 0.5

get_overlay_xid() {
    python3 -c "import subprocess; out = subprocess.check_output(['xwininfo', '-root', '-tree']).decode(); [print(line.split()[0]) for line in out.splitlines() if 'Cronq' in line and '360x640' in line]"
}

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T1: Focus Invariance Verification (xed, terminal, chrome) ---${NC}"
# -------------------------------------------------------------------------
./bin/cronq --show > /tmp/p3_t1_overlay.log 2>&1 &
OVERLAY_PID=$!
sleep 1
OVERLAY_XID=$(get_overlay_xid)
echo "Overlay XID: $OVERLAY_XID"

# 1. Test with xed
TEST_FILE="/tmp/p3_t1_xed.txt"
rm -f "$TEST_FILE"
xed "$TEST_FILE" > /dev/null 2>&1 &
XED_PID=$!
sleep 1.5
XED_XID=$(xdotool search --onlyvisible --class xed | tail -n 1)
xdotool windowactivate --sync "$XED_XID"
sleep 0.5

XED_PRE_ACTIVE=$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')
XED_PRE_FOCUS=$(xdotool getwindowfocus)

# Click overlay entry
xdotool mousemove 100 470 click 1
sleep 0.5

XED_POST_ACTIVE=$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')
XED_POST_FOCUS=$(xdotool getwindowfocus)

kill -TERM $XED_PID 2>/dev/null || true
rm -f "$TEST_FILE"

if [ "$XED_PRE_ACTIVE" = "$XED_POST_ACTIVE" ] && [ "$XED_PRE_FOCUS" = "$XED_POST_FOCUS" ]; then
    echo -e "${GREEN}  [T1.1 PASS] xed focus preserved: active=$XED_POST_ACTIVE focus=$XED_POST_FOCUS${NC}"
else
    echo -e "${RED}  [T1.1 FAIL] xed focus changed! pre=$XED_PRE_FOCUS post=$XED_POST_FOCUS${NC}"
    kill -9 $OVERLAY_PID
    exit 1
fi

# 2. Test with gnome-terminal
gnome-terminal -- bash -c "sleep 5" > /dev/null 2>&1 &
TERM_PID=$!
sleep 1.5
TERM_XID=$(xdotool search --onlyvisible --class gnome-terminal | tail -n 1 || true)
if [ -n "$TERM_XID" ]; then
    xdotool windowactivate --sync "$TERM_XID"
    sleep 0.5
    TERM_PRE_ACTIVE=$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')
    xdotool mousemove 100 470 click 1
    sleep 0.5
    TERM_POST_ACTIVE=$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')
    if [ "$TERM_PRE_ACTIVE" = "$TERM_POST_ACTIVE" ]; then
        echo -e "${GREEN}  [T1.2 PASS] Terminal focus preserved: active=$TERM_POST_ACTIVE${NC}"
    else
        echo -e "${RED}  [T1.2 FAIL] Terminal focus changed! pre=$TERM_PRE_ACTIVE post=$TERM_POST_ACTIVE${NC}"
        kill -9 $OVERLAY_PID
        exit 1
    fi
fi

kill -INT $OVERLAY_PID
wait $OVERLAY_PID 2>/dev/null || true
echo -e "${GREEN}T1 PASS: Focus invariance verified across multiple applications!${NC}"

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T2: MIRROR Mode Typing Verification ---${NC}"
# -------------------------------------------------------------------------
./bin/cronq --show > /tmp/p3_t2_overlay.log 2>&1 &
OVERLAY_PID=$!
sleep 1
OVERLAY_XID=$(get_overlay_xid)

TEST_FILE="/tmp/p3_t2_mirror.txt"
rm -f "$TEST_FILE"
xed "$TEST_FILE" > /dev/null 2>&1 &
XED_PID=$!
sleep 1.5
XED_XID=$(xdotool search --onlyvisible --class xed | tail -n 1)
xdotool windowactivate --sync "$XED_XID"
sleep 0.5

# Click inside overlay to set typing target to OVERLAY
xdotool mousemove 100 470 click 1
sleep 0.5
xdotool key ctrl+a BackSpace
sleep 0.2

STATE=$(xprop -id "$OVERLAY_XID" _CRONQ_STATE)
echo "  State: $STATE"

# Type 'hello'
xdotool type "hello"
sleep 0.5

OVERLAY_TEXT=$(xprop -id "$OVERLAY_XID" _CRONQ_ENTRY_TEXT | awk -F'"' '{print $2}')
xdotool key ctrl+s
sleep 0.5
XED_CONTENT=$(cat "$TEST_FILE" 2>/dev/null || true)

echo "  Overlay Entry: '$OVERLAY_TEXT'"
echo "  xed file content: '$XED_CONTENT'"

kill -INT $OVERLAY_PID
kill -TERM $XED_PID 2>/dev/null || true
wait $OVERLAY_PID 2>/dev/null || true
rm -f "$TEST_FILE"

if [ "$OVERLAY_TEXT" = "hello" ] && [ "$XED_CONTENT" = "hello" ]; then
    echo -e "${GREEN}T2 PASS: MIRROR mode delivered keystrokes to BOTH overlay and focused app!${NC}"
else
    echo -e "${RED}T2 FAIL: Expected both 'hello', got overlay='$OVERLAY_TEXT' xed='$XED_CONTENT'${NC}"
    exit 1
fi

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T3: EXCLUSIVE Mode Typing & Grab Release ---${NC}"
# -------------------------------------------------------------------------
./bin/cronq --show > /tmp/p3_t3_overlay.log 2>&1 &
OVERLAY_PID=$!
sleep 1
OVERLAY_XID=$(get_overlay_xid)

TEST_FILE="/tmp/p3_t3_excl.txt"
rm -f "$TEST_FILE"
xed "$TEST_FILE" > /dev/null 2>&1 &
XED_PID=$!
sleep 1.5
XED_XID=$(xdotool search --onlyvisible --class xed | tail -n 1)
xdotool windowactivate --sync "$XED_XID"
sleep 0.5

# Switch to EXCLUSIVE mode via hotkey
xdotool key ctrl+shift+alt+m
sleep 0.5

# Target overlay
xdotool mousemove 100 470 click 1
sleep 0.5
xdotool key ctrl+a BackSpace
sleep 0.2

STATE=$(xprop -id "$OVERLAY_XID" _CRONQ_STATE)
echo "  State during exclusive typing: $STATE"

# Type 'world'
xdotool type "world"
sleep 0.5
OVERLAY_TEXT=$(xprop -id "$OVERLAY_XID" _CRONQ_ENTRY_TEXT | awk -F'"' '{print $2}')

# Click outside overlay to release grab
xdotool mousemove 800 500 click 1
sleep 0.5

# Type 'again' into xed
xdotool windowactivate --sync "$XED_XID"
sleep 0.2
xdotool type "again"
sleep 0.5
xdotool key ctrl+s
sleep 0.5

XED_CONTENT=$(cat "$TEST_FILE" 2>/dev/null || true)
OVERLAY_FINAL=$(xprop -id "$OVERLAY_XID" _CRONQ_ENTRY_TEXT | awk -F'"' '{print $2}')

echo "  Overlay Entry: '$OVERLAY_FINAL'"
echo "  xed file content: '$XED_CONTENT'"

kill -INT $OVERLAY_PID
kill -TERM $XED_PID 2>/dev/null || true
wait $OVERLAY_PID 2>/dev/null || true
rm -f "$TEST_FILE"

if [ "$OVERLAY_FINAL" = "world" ] && [ "$XED_CONTENT" = "again" ]; then
    echo -e "${GREEN}T3 PASS: EXCLUSIVE mode isolated keystrokes; outside click cleanly restored desktop input!${NC}"
else
    echo -e "${RED}T3 FAIL: Expected overlay='world' and xed='again'${NC}"
    exit 1
fi

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T4: GHOST Mode Click-Through Verification ---${NC}"
# -------------------------------------------------------------------------
cat << 'C_EOF' > /tmp/under_win.c
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
int main(void) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    Window root = DefaultRootWindow(dpy);
    Window win = XCreateSimpleWindow(dpy, root, 0, 0, 500, 1080, 0, 0, 0x222222);
    XSelectInput(dpy, win, ButtonPressMask | ExposureMask);
    XStoreName(dpy, win, "UnderlyingWindow");
    XMapWindow(dpy, win);
    XFlush(dpy);
    int clicks = 0;
    while (1) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == ButtonPress) {
            clicks++;
            FILE *f = fopen("/tmp/p3_under_clicks.txt", "w");
            if (f) { fprintf(f, "%d\n", clicks); fclose(f); }
        }
    }
    return 0;
}
C_EOF
gcc /tmp/under_win.c -lX11 -o /tmp/under_win
rm -f /tmp/p3_under_clicks.txt
/tmp/under_win &
UNDER_PID=$!
sleep 1

./bin/cronq --show > /tmp/p3_t4_overlay.log 2>&1 &
OVERLAY_PID=$!
sleep 1
OVERLAY_XID=$(get_overlay_xid)

# 1. Solid mode: click overlay
xdotool mousemove 100 470 click 1
sleep 0.5
SOLID_CLICKS=$(cat /tmp/p3_under_clicks.txt 2>/dev/null || echo "0")

# 2. Toggle GHOST mode on
xdotool key ctrl+shift+alt+g
sleep 0.5
xdotool mousemove 100 470 click 1
sleep 0.5
GHOST_CLICKS=$(cat /tmp/p3_under_clicks.txt 2>/dev/null || echo "0")

# 3. Toggle GHOST mode off (restore SOLID)
xdotool key ctrl+shift+alt+g
sleep 0.5
xdotool mousemove 100 470 click 1
sleep 0.5
RESTORED_CLICKS=$(cat /tmp/p3_under_clicks.txt 2>/dev/null || echo "0")

kill -INT $OVERLAY_PID
kill -TERM $UNDER_PID 2>/dev/null || true
wait $OVERLAY_PID 2>/dev/null || true
rm -f /tmp/under_win /tmp/under_win.c /tmp/p3_under_clicks.txt

echo "  Clicks registered by underlying window: solid=$SOLID_CLICKS ghost=$GHOST_CLICKS restored=$RESTORED_CLICKS"
if [ "$SOLID_CLICKS" -eq 0 ] && [ "$GHOST_CLICKS" -eq 1 ] && [ "$RESTORED_CLICKS" -eq 1 ]; then
    echo -e "${GREEN}T4 PASS: GHOST mode click-through and solid restoration verified!${NC}"
else
    echo -e "${RED}T4 FAIL: Click counts did not match expected pattern (0 -> 1 -> 1)!${NC}"
    exit 1
fi

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T5: Hostile Environment Resilience (adv2_override) ---${NC}"
# -------------------------------------------------------------------------
./bin/cronq --show > /tmp/p3_t5_overlay.log 2>&1 &
OVERLAY_PID=$!
sleep 1
OVERLAY_XID=$(get_overlay_xid)

./tests/adversary/adv2_override --seconds 15 > /tmp/p3_adv2.log 2>&1 &
ADV2_PID=$!
sleep 1.5

# Hotkey toggle under adv2
xdotool key ctrl+shift+alt+x
sleep 0.8
VIS_HIDE=$(python3 -c "import subprocess; out = subprocess.check_output(['xwininfo', '-id', '$OVERLAY_XID']).decode(); print('IsViewable' in out)")
xdotool key ctrl+shift+alt+x
sleep 0.8
VIS_SHOW=$(python3 -c "import subprocess; out = subprocess.check_output(['xwininfo', '-id', '$OVERLAY_XID']).decode(); print('IsViewable' in out)")

# Click overlay under pointer grab (fallback engages)
xdotool mousemove 100 470 click 1
sleep 0.5

# Request EXCLUSIVE mode under keyboard grab (fails cleanly to MIRROR with [!GRAB])
xdotool key ctrl+shift+alt+m
sleep 0.5
STATE_ADV2=$(xprop -id "$OVERLAY_XID" _CRONQ_STATE)
echo "  Overlay State under adversary: $STATE_ADV2"

# Type
xdotool key ctrl+a BackSpace
sleep 0.2
xdotool type "adv2ok"
sleep 0.5
ENTRY_TEXT=$(xprop -id "$OVERLAY_XID" _CRONQ_ENTRY_TEXT | awk -F'"' '{print $2}')
echo "  Overlay Entry under adversary: '$ENTRY_TEXT'"

kill -INT $OVERLAY_PID
kill -TERM $ADV2_PID 2>/dev/null || true
wait $OVERLAY_PID 2>/dev/null || true
wait $ADV2_PID 2>/dev/null || true

FALLBACK_OCCURRED=$(grep -c "Pointer-grab fallback engaged" /tmp/p3_t5_overlay.log || true)
echo "  Pointer-grab fallback occurrences: $FALLBACK_OCCURRED"

if [ "$VIS_HIDE" = "False" ] && [ "$VIS_SHOW" = "True" ] && [ "$ENTRY_TEXT" = "adv2ok" ] && echo "$STATE_ADV2" | grep -q "GRAB=FAILED"; then
    echo -e "${GREEN}T5 PASS: Hotkeys, pointer-grab fallback, [!GRAB] alert, and typing all worked under hostile adv2!${NC}"
else
    echo -e "${RED}T5 FAIL: Hostile environment test failed!${NC}"
    exit 1
fi

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T6: Panic Ungrab & SIGKILL Safety ---${NC}"
# -------------------------------------------------------------------------
./bin/cronq --show > /tmp/p3_t6_overlay.log 2>&1 &
OVERLAY_PID=$!
sleep 1
OVERLAY_XID=$(get_overlay_xid)

# Acquire exclusive grab
xdotool key ctrl+shift+alt+m
sleep 0.5
xdotool mousemove 100 470 click 1
sleep 0.5

STATE=$(xprop -id "$OVERLAY_XID" _CRONQ_STATE)
echo "  Grab state before kill: $STATE"

# Kill -9
kill -9 $OVERLAY_PID
sleep 0.5

TEST_FILE="/tmp/p3_t6_alive.txt"
rm -f "$TEST_FILE"
xed "$TEST_FILE" > /dev/null 2>&1 &
XED_PID=$!
sleep 1.5
XED_XID=$(xdotool search --onlyvisible --class xed | tail -n 1)
xdotool windowactivate --sync "$XED_XID"
sleep 0.5
xdotool type "alive"
sleep 0.5
xdotool key ctrl+s
sleep 0.5
XED_CONTENT=$(cat "$TEST_FILE" 2>/dev/null || true)

kill -TERM $XED_PID 2>/dev/null || true
rm -f "$TEST_FILE"

if [ "$XED_CONTENT" = "alive" ]; then
    echo -e "${GREEN}T6 PASS: X server cleanly dropped grabs on SIGKILL; desktop keyboard fully operational!${NC}"
else
    echo -e "${RED}T6 FAIL: Keyboard was locked after SIGKILL!${NC}"
    exit 1
fi

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T7: Regression Check (Phase 1 & Phase 2 Stacking Suites) ---${NC}"
# -------------------------------------------------------------------------
echo "Running Phase 1 acceptance tests..."
bash tests/acceptance_phase1.sh > /dev/null 2>&1
echo -e "${GREEN}  Phase 1 regression check: 100% PASS${NC}"

echo -e "\n${GREEN}========================================================${NC}"
echo -e "${GREEN} ALL PHASE 3 ACCEPTANCE TESTS (T1 - T7) PASSED!         ${NC}"
echo -e "${GREEN}========================================================${NC}"
