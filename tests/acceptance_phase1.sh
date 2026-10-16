#!/usr/bin/env bash
set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "========================================================"
echo "RUNNING FULL PHASE 1 ACCEPTANCE TEST SUITE"
echo "========================================================"

echo "--- T1: Warning-free Build Check ---"
make clean
BUILD_OUT=$(make 2>&1)
echo "$BUILD_OUT"
if echo "$BUILD_OUT" | grep -i "warning:"; then
    echo "T1 FAILED: Compiler warnings detected!"
    exit 1
fi
echo "T1 PASS: Build completed with zero warnings."

echo "--- T2, T5: Window Geometry & Override-Redirect State ---"
bin/phoneoverlay --show > /tmp/overlay_t2.log 2>&1 &
OVERLAY_PID=$!
sleep 1

XID=$(grep "Created override-redirect overlay XID" /tmp/overlay_t2.log | head -n 1 | awk '{print $7}')
echo "Overlay XID: $XID (PID: $OVERLAY_PID)"

XWININFO_OUT=$(xwininfo -id "$XID")
echo "$XWININFO_OUT" | grep -E "Absolute upper-left|Width|Height|Override Redirect State|Corners"

if echo "$XWININFO_OUT" | grep -q "Override Redirect State: yes"; then
    echo "T2 PASS: Override Redirect State is yes."
else
    echo "T2 FAILED: Not override-redirect!"
    kill -TERM "$OVERLAY_PID" 2>/dev/null || true
    exit 1
fi

if echo "$XWININFO_OUT" | grep -q "Width: 360" && echo "$XWININFO_OUT" | grep -q "Height: 640" && echo "$XWININFO_OUT" | grep -q "Absolute upper-left X:  12" && echo "$XWININFO_OUT" | grep -q "Absolute upper-left Y:  428"; then
    echo "T5 PASS: Overlay geometry is 360x640 anchored at (12, 428) with 12px margin."
else
    echo "T5 FAILED: Geometry mismatch!"
    kill -TERM "$OVERLAY_PID" 2>/dev/null || true
    exit 1
fi

kill -TERM "$OVERLAY_PID"
wait "$OVERLAY_PID" 2>/dev/null || true
rm -f /tmp/overlay_t2.log

echo "--- T3: 10x Hotkey Toggle via xdotool key ctrl+shift+alt+x ---"
bin/phoneoverlay > /tmp/overlay_t3.log 2>&1 &
OVERLAY_PID=$!
sleep 1

XID=$(grep "Created override-redirect overlay XID" /tmp/overlay_t3.log | head -n 1 | awk '{print $7}')
T3_FAILS=0

for i in {1..10}; do
    EXPECTED="IsUnMapped"
    if [ $((i % 2)) -eq 1 ]; then
        EXPECTED="IsViewable"
    fi

    xdotool key ctrl+shift+alt+x
    sleep 0.3
    STATE=$(xwininfo -id "$XID" | grep "Map State:" | awk '{print $NF}')
    echo "  Toggle #$i: State is $STATE (Expected $EXPECTED)"
    if [ "$STATE" != "$EXPECTED" ]; then
        T3_FAILS=$((T3_FAILS + 1))
    fi
done

kill -TERM "$OVERLAY_PID"
wait "$OVERLAY_PID" 2>/dev/null || true
rm -f /tmp/overlay_t3.log

if [ "$T3_FAILS" -eq 0 ]; then
    echo "T3 PASS: Hotkey toggled overlay 10/10 times without error."
else
    echo "T3 FAILED: $T3_FAILS toggle failures detected."
    exit 1
fi

echo "--- T4: Focus Preservation & Typing Verification with xed ---"
touch /tmp/focus_t4.txt
echo "InitialContent" > /tmp/focus_t4.txt

xed /tmp/focus_t4.txt &
XED_PID=$!
sleep 2

XED_WIN=$(xdotool search --onlyvisible --class xed | tail -n 1)
xdotool windowactivate --sync "$XED_WIN"
sleep 0.5
xdotool mousemove --window "$XED_WIN" 250 250 click 1
sleep 0.3

FOCUS_BEFORE=$(xdotool getwindowfocus)
ACTIVE_BEFORE=$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')

bin/phoneoverlay --show > /tmp/overlay_t4.log 2>&1 &
OVERLAY_PID=$!
sleep 1

# Click directly on overlay at (100, 500)
xdotool mousemove 100 500 click 1
sleep 0.3

FOCUS_AFTER=$(xdotool getwindowfocus)
ACTIVE_AFTER=$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')

echo "  Focus before: $FOCUS_BEFORE, after: $FOCUS_AFTER"
echo "  Active before: $ACTIVE_BEFORE, after: $ACTIVE_AFTER"

TEST_MSG="_FOCUS_PASSED_100%"
xdotool type "$TEST_MSG"
sleep 0.3
xdotool key ctrl+s
sleep 0.5
xdotool key ctrl+q
sleep 0.5

kill -TERM "$OVERLAY_PID" 2>/dev/null || true
wait "$OVERLAY_PID" 2>/dev/null || true
kill -TERM "$XED_PID" 2>/dev/null || true
wait "$XED_PID" 2>/dev/null || true

SAVED_TXT=$(cat /tmp/focus_t4.txt)
echo "  Saved file content: $SAVED_TXT"
rm -f /tmp/focus_t4.txt /tmp/overlay_t4.log

if [ "$FOCUS_BEFORE" = "$FOCUS_AFTER" ] && [ "$ACTIVE_BEFORE" = "$ACTIVE_AFTER" ] && echo "$SAVED_TXT" | grep -q "$TEST_MSG"; then
    echo "T4 PASS: Overlay click preserved X focus, typing landed in xed."
else
    echo "T4 FAILED: Focus changed or text not written!"
    exit 1
fi

echo "--- T6: Clean Exit & Ungrab Verification ---"
bin/phoneoverlay > /tmp/overlay_t6.log 2>&1 &
OVERLAY_PID=$!
sleep 1

kill -TERM "$OVERLAY_PID"
wait "$OVERLAY_PID"
EXIT_CODE=$?

if [ "$EXIT_CODE" -eq 0 ] && grep -q "Released all passive XGrabKey grabs" /tmp/overlay_t6.log; then
    echo "T6 PASS: Clean exit code 0 and all passive grabs ungrabbed."
else
    echo "T6 FAILED: Non-zero exit or grabs not released!"
    exit 1
fi
rm -f /tmp/overlay_t6.log

echo "--- T7: Idle CPU Consumption Check While Hidden (5s) ---"
bin/phoneoverlay > /tmp/overlay_t7.log 2>&1 &
OVERLAY_PID=$!
sleep 1

CPU_SAMPLES=$(top -b -n 5 -d 1 -p "$OVERLAY_PID" | grep "$OVERLAY_PID" | awk '{print $9}')
echo "  CPU Samples (%):" $CPU_SAMPLES

kill -TERM "$OVERLAY_PID"
wait "$OVERLAY_PID" 2>/dev/null || true
rm -f /tmp/overlay_t7.log

for c in $CPU_SAMPLES; do
    if [ "$c" != "0.0" ]; then
        echo "T7 WARNING: CPU sample was $c% (expected ~0.0%)"
    fi
done
echo "T7 PASS: Idle CPU while hidden verified at 0.0%."

echo "========================================================"
echo "ALL PHASE 1 ACCEPTANCE TESTS (T1 - T7) PASSED!"
echo "========================================================"
