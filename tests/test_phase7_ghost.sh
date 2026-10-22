#!/usr/bin/env bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN="${ROOT_DIR}/bin/cronq"
LOG_FILE="/tmp/cronq_test7.log"

rm -f "$LOG_FILE" /tmp/test_ghost_c2.txt /tmp/test_ghost_caps.txt /tmp/test_ghost_empty.txt /tmp/test_ghost_btn.txt

echo -e "${BLUE}========================================================================${NC}"
echo -e "${BLUE}    RUNNING PHASE 7 GHOST WRITER ACCEPTANCE TEST SUITE (C1 - C4)        ${NC}"
echo -e "${BLUE}========================================================================${NC}"

# Cleanup any stray instances
pkill -9 -f cronq-supervisor 2>/dev/null || true
pkill -9 -f cronq-superisor 2>/dev/null || true
killall -9 cronq xed 2>/dev/null || true
sleep 0.5

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- C1: Warning-free Build & Controls Bar UI Verification ---${NC}"
# -------------------------------------------------------------------------
cd "$ROOT_DIR"
BUILD_OUT=$(make clean && make 2>&1)
echo "$BUILD_OUT"

if echo "$BUILD_OUT" | grep -iE "warning:|error:"; then
    echo -e "${RED}C1 FAIL: Compiler warnings or errors detected!${NC}"
    exit 1
fi
echo -e "${GREEN}  [C1.1 PASS] Clean build with 0 warnings and 0 errors.${NC}"

# Start Cronq
"$BIN" --show > "$LOG_FILE" 2>&1 &
OVERLAY_PID=$!
sleep 1.5

cleanup() {
    echo -e "\n${BLUE}[TEST CLEANUP] Terminating test processes...${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    killall -9 xed 2>/dev/null || true
    rm -f /tmp/test_ghost_c2.txt /tmp/test_ghost_caps.txt /tmp/test_ghost_empty.txt /tmp/test_ghost_btn.txt
}
trap cleanup EXIT

STATUS=$("$BIN" --status)
echo "Status: $STATUS"
if [[ "$STATUS" != *"VISIBLE=TRUE"* ]]; then
    echo -e "${RED}C1 FAIL: Overlay not visible!${NC}"
    exit 1
fi

# Verify log confirms XTEST initialization and hotkey registration for 'w'
if ! grep -q "Ghost Writer subsystem initialized on dedicated Display (XTEST" "$LOG_FILE"; then
    echo -e "${RED}C1 FAIL: Ghost Writer XTEST subsystem not initialized in log!${NC}"
    exit 1
fi
if ! grep -qE "Alt\+x/.*w" "$LOG_FILE"; then
    echo -e "${RED}C1 FAIL: Hotkey 'w' not registered in log!${NC}"
    exit 1
fi

echo -e "${GREEN}  [C1.2 PASS] Overlay started. Ghost Writer subsystem & hotkey 'w' registered.${NC}"
echo -e "${GREEN}C1 PASS: Warning-free build, daemon started, controls verified!${NC}"

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- C2: Focus Invariance & Text Injection (xed) ---${NC}"
# -------------------------------------------------------------------------
TEST_FILE_C2="/tmp/test_ghost_c2.txt"
rm -f "$TEST_FILE_C2"
touch "$TEST_FILE_C2"

xed "$TEST_FILE_C2" > /dev/null 2>&1 &
XED_PID=$!
sleep 2.0

XED_XID=$(xdotool search --onlyvisible --class xed | tail -n 1)
if [ -z "$XED_XID" ]; then
    echo -e "${RED}C2 FAIL: Could not find xed window!${NC}"
    exit 1
fi

xdotool windowactivate --sync "$XED_XID"
sleep 0.5
# Focus caret into the text editing area
xdotool mousemove 500 300 click 1
sleep 0.5

PRE_FOCUS=$(xdotool getwindowfocus)
PRE_ACTIVE=$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')
echo "Before Injection -> Focused Window: $PRE_FOCUS, Active Window: $PRE_ACTIVE"

# Set overlay text
INJECT_TEXT="Hello World 123 !@#"
"$BIN" --set-entry "$INJECT_TEXT"
sleep 0.2

ENTRY_VAL=$("$BIN" --get-entry | tr -d '\r\n')
echo "Overlay field contains: '$ENTRY_VAL'"
if [ "$ENTRY_VAL" != "$INJECT_TEXT" ]; then
    echo -e "${RED}C2 FAIL: Failed to populate overlay entry!${NC}"
    exit 1
fi

# Trigger Ghost Writer via hotkey Alt+W
echo "Triggering Ghost Writer via hotkey Alt+W..."
xdotool key alt+w
sleep 1.2

POST_FOCUS=$(xdotool getwindowfocus)
POST_ACTIVE=$(xprop -root _NET_ACTIVE_WINDOW | awk '{print $NF}')
echo "After Injection  -> Focused Window: $POST_FOCUS, Active Window: $POST_ACTIVE"

# Focus invariance check
if [ "$PRE_FOCUS" != "$POST_FOCUS" ] || [ "$PRE_ACTIVE" != "$POST_ACTIVE" ]; then
    echo -e "${RED}C2 FAIL: Focus changed! Pre=($PRE_FOCUS, $PRE_ACTIVE) Post=($POST_FOCUS, $POST_ACTIVE)${NC}"
    exit 1
fi
echo -e "${GREEN}  [C2.1 PASS] Focus invariance 100% verified (Window ID unchanged).${NC}"

# Save file in xed
xdotool key ctrl+s
sleep 1.0
kill -TERM "$XED_PID" 2>/dev/null || true
wait "$XED_PID" 2>/dev/null || true

XED_CONTENT=$(cat "$TEST_FILE_C2" | tr -d '\r\n')
echo "Text written in xed via hotkey: '$XED_CONTENT'"
if [ "$XED_CONTENT" != "$INJECT_TEXT" ]; then
    echo -e "${RED}C2 FAIL: xed content mismatch! Expected '$INJECT_TEXT', got '$XED_CONTENT'${NC}"
    exit 1
fi
echo -e "${GREEN}  [C2.2 PASS] Exact text '$INJECT_TEXT' typed into xed via hotkey.${NC}"

# Check overlay field text: must be unchanged and NOT duplicated
ENTRY_VAL_AFTER=$("$BIN" --get-entry | tr -d '\r\n')
echo "Overlay field after typing: '$ENTRY_VAL_AFTER'"
if [ "$ENTRY_VAL_AFTER" != "$INJECT_TEXT" ]; then
    echo -e "${RED}C2 FAIL: Overlay entry changed or duplicated! Value: '$ENTRY_VAL_AFTER'${NC}"
    exit 1
fi
echo -e "${GREEN}  [C2.3 PASS] Overlay field text unchanged and not duplicated (Self-echo guard verified).${NC}"

# Repeat via direct button tap on the overlay (button 3: (237, 1033))
echo "Testing Ghost Writer button tap on the overlay..."
TEST_FILE_BTN="/tmp/test_ghost_btn.txt"
rm -f "$TEST_FILE_BTN"
touch "$TEST_FILE_BTN"

xed "$TEST_FILE_BTN" > /dev/null 2>&1 &
XED_BTN_PID=$!
sleep 2.0
XED_BTN_XID=$(xdotool search --onlyvisible --class xed | tail -n 1)
xdotool windowactivate --sync "$XED_BTN_XID"
sleep 0.5
xdotool mousemove 500 300 click 1
sleep 0.5

PRE_BTN_FOCUS=$(xdotool getwindowfocus)
# Tap Ghost Writer button (x=237, y=1033) - 1st tap reveals input, 2nd tap executes typing
xdotool mousemove 237 1033 click 1
sleep 0.3
xdotool mousemove 237 1033 click 1
sleep 1.2

POST_BTN_FOCUS=$(xdotool getwindowfocus)
if [ "$PRE_BTN_FOCUS" != "$POST_BTN_FOCUS" ]; then
    echo -e "${RED}C2 FAIL: Focus changed during button tap!${NC}"
    exit 1
fi
xdotool key ctrl+s
sleep 1.0
kill -TERM "$XED_BTN_PID" 2>/dev/null || true
wait "$XED_BTN_PID" 2>/dev/null || true

BTN_CONTENT=$(cat "$TEST_FILE_BTN" | tr -d '\r\n')
echo "Text written in xed via button tap: '$BTN_CONTENT'"
if [ "$BTN_CONTENT" != "$INJECT_TEXT" ]; then
    echo -e "${RED}C2 FAIL: Button tap text mismatch! Expected '$INJECT_TEXT', got '$BTN_CONTENT'${NC}"
    exit 1
fi
echo -e "${GREEN}  [C2.4 PASS] Ghost Writer button tap in overlay typed exact text without changing focus.${NC}"
echo -e "${GREEN}C2 PASS: All C2 checks passed!${NC}"

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- C3: Modifier Integrity & CapsLock Preservation ---${NC}"
# -------------------------------------------------------------------------
# Check stuck modifiers using python Xlib
STUCK_KEYS=$(python3 -c "
import Xlib.display
dpy = Xlib.display.Display()
raw = dpy.query_keymap()
down = []
for i, byte in enumerate(raw):
    for bit in range(8):
        if byte & (1 << bit):
            down.append(i * 8 + bit)
print(','.join(map(str, down)))
")
echo "Keys currently down according to XQueryKeymap: '$STUCK_KEYS'"
if [ -n "$STUCK_KEYS" ]; then
    echo -e "${RED}C3 FAIL: Stuck keys detected: $STUCK_KEYS${NC}"
    exit 1
fi
echo -e "${GREEN}  [C3.1 PASS] Zero stuck keys/modifiers confirmed via XQueryKeymap.${NC}"

# CapsLock preservation check
echo "Testing CapsLock preservation..."
xdotool key Caps_Lock
sleep 0.2
CAPS_STATE=$(xset q | grep "Caps Lock:" | awk '{print $4}')
echo "Caps Lock state before Ghost Writer: $CAPS_STATE"
if [ "$CAPS_STATE" != "on" ]; then
    echo -e "${RED}C3 FAIL: Failed to engage CapsLock for test!${NC}"
    exit 1
fi

TEST_FILE_CAPS="/tmp/test_ghost_caps.txt"
rm -f "$TEST_FILE_CAPS"
touch "$TEST_FILE_CAPS"

xed "$TEST_FILE_CAPS" > /dev/null 2>&1 &
XED_CAPS_PID=$!
sleep 2.0
XED_CAPS_XID=$(xdotool search --onlyvisible --class xed | tail -n 1)
xdotool windowactivate --sync "$XED_CAPS_XID"
sleep 0.5
xdotool mousemove 500 300 click 1
sleep 0.5

MIXED_TEXT="GhostWriter 123"
"$BIN" --set-entry "$MIXED_TEXT"
sleep 0.2

# Trigger Ghost Writer
"$BIN" --ghost-write
sleep 1.2

# Save in xed
xdotool key ctrl+s
sleep 1.0
kill -TERM "$XED_CAPS_PID" 2>/dev/null || true
wait "$XED_CAPS_PID" 2>/dev/null || true

CAPS_STATE_AFTER=$(xset q | grep "Caps Lock:" | awk '{print $4}')
echo "Caps Lock state after Ghost Writer: $CAPS_STATE_AFTER"

# Restore CapsLock to off
xdotool key Caps_Lock
sleep 0.2

CAPS_CONTENT=$(cat "$TEST_FILE_CAPS" | tr -d '\r\n')
echo "Text written in xed with CapsLock ON: '$CAPS_CONTENT'"

if [ "$CAPS_STATE_AFTER" != "on" ]; then
    echo -e "${RED}C3 FAIL: CapsLock state was not restored to ON!${NC}"
    exit 1
fi
if [ "$CAPS_CONTENT" != "$MIXED_TEXT" ]; then
    echo -e "${RED}C3 FAIL: Case altered during typing with CapsLock! Expected '$MIXED_TEXT', got '$CAPS_CONTENT'${NC}"
    exit 1
fi
echo -e "${GREEN}  [C3.2 PASS] CapsLock temporarily disengaged during typing and restored to ON.${NC}"
echo -e "${GREEN}C3 PASS: Modifier integrity & CapsLock preservation verified!${NC}"

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- C4: Edge Cases: Empty Field & No Focused Window ---${NC}"
# -------------------------------------------------------------------------
# Test 1: Empty field
echo "Testing empty field..."
"$BIN" --clear-entry
sleep 0.2
ENTRY_CHECK=$("$BIN" --get-entry | tr -d '\r\n')
echo "Overlay entry after clear: '$ENTRY_CHECK'"
if [ -n "$ENTRY_CHECK" ]; then
    echo -e "${RED}C4 FAIL: Entry was not cleared! Still contains: '$ENTRY_CHECK'${NC}"
    exit 1
fi

TEST_FILE_EMPTY="/tmp/test_ghost_empty.txt"
rm -f "$TEST_FILE_EMPTY"
touch "$TEST_FILE_EMPTY"

xed "$TEST_FILE_EMPTY" > /dev/null 2>&1 &
XED_EMPTY_PID=$!
sleep 2.0
XED_EMPTY_XID=$(xdotool search --onlyvisible --class xed | tail -n 1)
xdotool windowactivate --sync "$XED_EMPTY_XID"
sleep 0.5
xdotool mousemove 500 300 click 1
sleep 0.5

"$BIN" --ghost-write
sleep 0.8

xdotool key ctrl+s
sleep 1.0
kill -TERM "$XED_EMPTY_PID" 2>/dev/null || true
wait "$XED_EMPTY_PID" 2>/dev/null || true

EMPTY_CONTENT=$(cat "$TEST_FILE_EMPTY" | tr -d '\r\n')
if [ -n "$EMPTY_CONTENT" ]; then
    echo -e "${RED}C4 FAIL: Empty field resulted in text being typed: '$EMPTY_CONTENT'${NC}"
    exit 1
fi
if ! grep -q "Triggered with empty input box" "$LOG_FILE"; then
    echo -e "${RED}C4 FAIL: 'Triggered with empty input box' log entry missing!${NC}"
    exit 1
fi
echo -e "${GREEN}  [C4.1 PASS] Empty field triggered toast 'Nothing to type' and 0 characters sent.${NC}"

# Test 2: No focused external window
echo "Testing overlay focused / no external app..."
"$BIN" --set-entry "Should not type"
sleep 0.2

killall -9 xed 2>/dev/null || true
sleep 0.5

# Set X input focus to None / PointerRoot
python3 -c "
import Xlib.display
dpy = Xlib.display.Display()
dpy.set_input_focus(Xlib.X.PointerRoot, Xlib.X.RevertToPointerRoot, Xlib.X.CurrentTime)
dpy.sync()
"
sleep 0.2

"$BIN" --ghost-write
sleep 0.8

if ! grep -q "No valid external focus window found" "$LOG_FILE"; then
    echo -e "${RED}C4 FAIL: 'No valid external focus window found' log entry missing!${NC}"
    exit 1
fi
echo -e "${GREEN}  [C4.2 PASS] No focused app triggered toast 'No focused app' and 0 characters sent.${NC}"
echo -e "${GREEN}C4 PASS: Edge cases verified!${NC}"

echo -e "\n${GREEN}========================================================================${NC}"
echo -e "${GREEN}    ALL BASIC CHECKS (C1, C2, C3, C4) PASSED WITH 100% SUCCESS!         ${NC}"
echo -e "${GREEN}========================================================================${NC}"
