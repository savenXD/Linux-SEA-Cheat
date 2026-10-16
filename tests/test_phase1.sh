#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

echo "=== Building Test Helpers ==="
gcc -Wall -Wextra -std=c11 tests/check_stacking.c -lX11 -o bin/check_stacking
gcc -Wall -Wextra -std=c11 tests/competitor_window.c -lX11 -o bin/competitor_window

echo "=== Starting PhoneOverlay ==="
LOG_FILE="/tmp/phoneoverlay_phase1.log"
rm -f "$LOG_FILE"

bin/phoneoverlay > "$LOG_FILE" 2>&1 &
OVERLAY_PID=$!

cleanup() {
    if kill -0 "$OVERLAY_PID" 2>/dev/null; then
        kill -TERM "$OVERLAY_PID" 2>/dev/null || true
        wait "$OVERLAY_PID" 2>/dev/null || true
    fi
    if [ -n "$COMP_PID" ] && kill -0 "$COMP_PID" 2>/dev/null; then
        kill -9 "$COMP_PID" 2>/dev/null || true
        wait "$COMP_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Wait for overlay XID to appear
OVERLAY_XID=""
for i in {1..30}; do
    if grep -q "Created GTK_WINDOW_POPUP with XID" "$LOG_FILE"; then
        OVERLAY_XID=$(grep "Created GTK_WINDOW_POPUP with XID" "$LOG_FILE" | head -n 1 | awk '{print $6}')
        break
    fi
    sleep 0.1
done

if [ -z "$OVERLAY_XID" ]; then
    echo "FAIL: Overlay did not start or report XID in time. Log:"
    cat "$LOG_FILE"
    exit 1
fi

echo "Overlay XID: $OVERLAY_XID (PID: $OVERLAY_PID)"

echo "--------------------------------------------------------"
echo "TEST 1: Override-Redirect & Geometry Verification"
echo "--------------------------------------------------------"
XWININFO_OUT=$(xwininfo -id "$OVERLAY_XID")

echo "$XWININFO_OUT" | grep -E "Absolute upper-left|Width|Height|Override Redirect State"

if echo "$XWININFO_OUT" | grep -q "Override Redirect State: yes"; then
    echo "PASS: Window has Override Redirect State: yes"
else
    echo "FAIL: Window is not override-redirect!"
    exit 1
fi

if echo "$XWININFO_OUT" | grep -q "Width: 360" && echo "$XWININFO_OUT" | grep -q "Height: 640"; then
    echo "PASS: Window size is 360x640"
else
    echo "FAIL: Window size is not 360x640!"
    exit 1
fi

if echo "$XWININFO_OUT" | grep -q "Absolute upper-left X:  0" && echo "$XWININFO_OUT" | grep -q "Absolute upper-left Y:  440"; then
    echo "PASS: Window position is at (0, 440) - Bottom-Left of 1920x1080 display"
else
    echo "FAIL: Window position does not match primary bottom-left!"
    exit 1
fi

echo "--------------------------------------------------------"
echo "TEST 2: Zero-Focus-Stealing Verification on Interaction"
echo "--------------------------------------------------------"
ACTIVE_BEFORE=$(xdotool getactivewindow 2>&1 || echo "none")
echo "Active Window before click: $ACTIVE_BEFORE"

# Read exact button center coordinates from log
BTN_COORDS=$(grep "Test Button Center" "$LOG_FILE" | head -n 1 | sed 's/.*(\([0-9]*\),[ ]*\([0-9]*\))/\1 \2/')
BTN_X=$(echo "$BTN_COORDS" | awk '{print $1}')
BTN_Y=$(echo "$BTN_COORDS" | awk '{print $2}')
echo "Clicking test button at ($BTN_X, $BTN_Y)..."
xdotool mousemove "$BTN_X" "$BTN_Y" click 1
sleep 0.5

ACTIVE_AFTER=$(xdotool getactivewindow 2>&1 || echo "none")
echo "Active Window after click: $ACTIVE_AFTER"

if [ "$ACTIVE_BEFORE" = "$ACTIVE_AFTER" ]; then
    echo "PASS: Active window ID was NOT changed by clicking the overlay"
else
    echo "FAIL: Active window changed from $ACTIVE_BEFORE to $ACTIVE_AFTER!"
    exit 1
fi

if grep -q "Overlay test button clicked" "$LOG_FILE"; then
    echo "PASS: Click was received and processed by overlay widget"
else
    echo "FAIL: Overlay widget did not receive the click event!"
    exit 1
fi

echo "--------------------------------------------------------"
echo "TEST 3: Stacking Dominance Against Competitor Window"
echo "--------------------------------------------------------"
bin/competitor_window > /tmp/competitor.log 2>&1 &
COMP_PID=$!

COMP_XID=""
for i in {1..20}; do
    if grep -q "Competitor window created with XID" /tmp/competitor.log; then
        COMP_XID=$(grep "Competitor window created with XID" /tmp/competitor.log | awk '{print $6}')
        break
    fi
    sleep 0.1
done

echo "Competitor XID: $COMP_XID"
# Give event loop 0.3s to process ConfigureNotify and re-raise overlay
sleep 0.3

bin/check_stacking "$OVERLAY_XID" "$COMP_XID"
kill -9 "$COMP_PID" 2>/dev/null || true
wait "$COMP_PID" 2>/dev/null || true
unset COMP_PID

echo "--------------------------------------------------------"
echo "TEST 4: Clean Shutdown on SIGTERM"
echo "--------------------------------------------------------"
kill -TERM "$OVERLAY_PID"
wait "$OVERLAY_PID"
EXIT_CODE=$?
echo "Exit code: $EXIT_CODE"

if [ "$EXIT_CODE" -eq 0 ]; then
    echo "PASS: Clean exit code 0"
else
    echo "FAIL: Non-zero exit code $EXIT_CODE"
    exit 1
fi

if grep -q "Exited cleanly" "$LOG_FILE"; then
    echo "PASS: Clean teardown sequence completed"
else
    echo "FAIL: Teardown message not found in log"
    cat "$LOG_FILE"
    exit 1
fi

echo "========================================================"
echo "ALL PHASE 1 ACCEPTANCE TESTS PASSED!"
echo "========================================================"
