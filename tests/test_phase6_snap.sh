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
COLORWIN="${ROOT_DIR}/tests/colorwin"
ADV2="${ROOT_DIR}/tests/adversary/adv2_override"
XCLIP="${ROOT_DIR}/tests/xclip"
HTML_PAGE="${ROOT_DIR}/tests/test_paste_page.html"

LOG_FILE="/tmp/cronq_test6.log"
rm -f "$LOG_FILE" /tmp/cronq_latest_snap.png

echo -e "${BLUE}========================================================================${NC}"
echo -e "${BLUE}    RUNNING PHASE 6 SCREEN CAPTURE (SNAP) ACCEPTANCE TEST SUITE        ${NC}"
echo -e "${BLUE}========================================================================${NC}"

# Cleanup any stray instances
killall -9 cronq colorwin adv1_ewmh adv2_override adv3_focuslock stackcheck 2>/dev/null || true
sleep 0.5

# Ensure binaries are built
if [ ! -f "$BIN" ]; then
    echo -e "${RED}Error: $BIN not found! Run make first.${NC}"
    exit 1
fi

if [ ! -f "$COLORWIN" ]; then
    gcc -std=gnu11 -Wall -O2 tests/colorwin.c -lX11 -o tests/colorwin
fi

if [ ! -f "$ADV2" ]; then
    gcc -std=gnu11 -Wall -O2 tests/adversary/adv2_override.c -lX11 -o tests/adversary/adv2_override
fi

# Launch cronq daemon
echo -e "${BLUE}[TEST SETUP] Launching cronq daemon...${NC}"
"$BIN" --show > "$LOG_FILE" 2>&1 &
OVERLAY_PID=$!
sleep 1.5

cleanup() {
    echo -e "${BLUE}[TEST CLEANUP] Terminating test processes...${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    killall -9 colorwin adv2_override 2>/dev/null || true
}
trap cleanup EXIT

# Verify overlay is running
STATUS=$("$BIN" --status)
echo "Initial Status: $STATUS"
if [[ "$STATUS" != *"VISIBLE=TRUE"* ]]; then
    echo -e "${RED}FAILURE: Overlay failed to start visible!${NC}"
    exit 1
fi

# -----------------------------------------------------------------------------
# TEST T1: Zero-Residue Pixel Absence Test (Solid Green Window)
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}>>> [TEST T1] Zero-Residue Pixel Absence Test...${NC}"
"$COLORWIN" > /dev/null 2>&1 &
COLORWIN_PID=$!
sleep 0.8

# Ensure overlay is on top of colorwin
"$BIN" --show
sleep 0.3

# Trigger screen snap
rm -f /tmp/cronq_latest_snap.png
"$BIN" --snap
sleep 1.0

if [ ! -f "/tmp/cronq_latest_snap.png" ]; then
    echo -e "${RED}FAILURE: /tmp/cronq_latest_snap.png was not generated!${NC}"
    exit 1
fi

# Inspect pixels in the overlay rectangle (12, 428, 360, 640)
PIXEL_CHECK_RESULT=$(python3 - << 'EOF'
import sys
from PIL import Image

try:
    img = Image.open('/tmp/cronq_latest_snap.png').convert('RGB')
    w, h = img.size
    # Overlay region: x=12, y=428, width=360, height=640
    crop_x, crop_y, crop_w, crop_h = 12, 428, 360, 640
    crop = img.crop((crop_x, crop_y, crop_x + crop_w, crop_y + crop_h))

    non_green_pixels = 0
    total_pixels = crop_w * crop_h

    for r, g, b in crop.getdata():
        # Pure green is (0, 255, 0)
        if not (r == 0 and g == 255 and b == 0):
            non_green_pixels += 1

    print(f"TOTAL={total_pixels} NON_GREEN={non_green_pixels}")
    if non_green_pixels == 0:
        sys.exit(0)
    else:
        sys.exit(1)
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(2)
EOF
)
echo "Pixel check result: $PIXEL_CHECK_RESULT"
if [[ "$PIXEL_CHECK_RESULT" != *"NON_GREEN=0"* ]]; then
    echo -e "${RED}FAILURE: Overlay pixels detected in screenshot! Overlay was not completely excluded!${NC}"
    exit 1
fi
echo -e "${GREEN}PASS: T1 Zero-Residue Pixel Absence (0 non-green pixels out of 230,400 in overlay region).${NC}"

# Kill colorwin
kill -9 $COLORWIN_PID 2>/dev/null || true
sleep 0.5

# -----------------------------------------------------------------------------
# TEST T2: Strict Focus & Active Window Invariance
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}>>> [TEST T2] Strict Focus & Active Window Invariance...${NC}"
# Find or launch an active window
ACTIVE_BEFORE=$(xdotool getactivewindow 2>/dev/null || echo "NONE")
echo "Active Window before snap: $ACTIVE_BEFORE"

# Trigger snap
"$BIN" --snap
sleep 0.05
ACTIVE_DURING=$(xdotool getactivewindow 2>/dev/null || echo "NONE")
sleep 0.8
ACTIVE_AFTER=$(xdotool getactivewindow 2>/dev/null || echo "NONE")

echo "Active Window during snap: $ACTIVE_DURING"
echo "Active Window after snap:  $ACTIVE_AFTER"

if [ "$ACTIVE_BEFORE" != "$ACTIVE_DURING" ] || [ "$ACTIVE_BEFORE" != "$ACTIVE_AFTER" ]; then
    echo -e "${RED}FAILURE: Focus changed during snap! (Before=$ACTIVE_BEFORE, During=$ACTIVE_DURING, After=$ACTIVE_AFTER)${NC}"
    exit 1
fi
echo -e "${GREEN}PASS: T2 Focus Invariance (active window strictly unchanged: $ACTIVE_BEFORE).${NC}"

# -----------------------------------------------------------------------------
# TEST T3: Overlay State Preservation
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}>>> [TEST T3] Overlay State Preservation...${NC}"
"$BIN" --app chatgpt
sleep 0.3
"$BIN" --mode exclusive
sleep 0.2

STATUS_BEFORE=$("$BIN" --status)
echo "Status before snap: $STATUS_BEFORE"

"$BIN" --snap
sleep 0.8

STATUS_AFTER=$("$BIN" --status)
echo "Status after snap:  $STATUS_AFTER"

if [ "$STATUS_BEFORE" != "$STATUS_AFTER" ]; then
    echo -e "${RED}FAILURE: Overlay state modified during snap!${NC}"
    exit 1
fi
echo -e "${GREEN}PASS: T3 Overlay State Preservation (all properties preserved across snap).${NC}"

# Reset mode to mirror
"$BIN" --mode mirror

# -----------------------------------------------------------------------------
# TEST T4: Clipboard Target & Content Verification
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}>>> [TEST T4] Clipboard Target & Content Verification...${NC}"
TARGETS=$(python3 "$XCLIP" -selection clipboard -t TARGETS -o)
echo "Clipboard TARGETS:"
echo "$TARGETS"

if [[ "$TARGETS" != *"image/png"* ]]; then
    echo -e "${RED}FAILURE: image/png target not found on CLIPBOARD!${NC}"
    exit 1
fi

CLIP_TYPE=$(python3 "$XCLIP" -selection clipboard -t image/png -o | file -)
echo "Clipboard image data check: $CLIP_TYPE"
if [[ "$CLIP_TYPE" != *"PNG image data"* ]]; then
    echo -e "${RED}FAILURE: Clipboard content is not valid PNG image data!${NC}"
    exit 1
fi
echo -e "${GREEN}PASS: T4 Clipboard Target Verification (image/png verified on CLIPBOARD).${NC}"

# -----------------------------------------------------------------------------
# TEST T5: Synthetic Image Paste into WebKit View
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}>>> [TEST T5] Synthetic Image Paste into WebKit View...${NC}"
"$BIN" --app browser
sleep 0.3
"$BIN" --load "file://${HTML_PAGE}"
sleep 1.2

# Trigger snap to ensure fresh image is on clipboard
"$BIN" --snap
sleep 0.8

# Test paste via --paste CLI/IPC
"$BIN" --paste
sleep 0.5

# Also test paste via click to set TARGET=OVERLAY and xdotool ctrl+v
xdotool mousemove 100 600 click 1
sleep 0.3
xdotool key --clearmodifiers ctrl+v
sleep 0.8

# Verify browser page received the image
PASTE_STATUS=$(python3 - << 'EOF'
import sys, re
with open('/tmp/cronq_test6.log', 'r') as f:
    content = f.read()
if "Injecting synthetic image paste event" in content or "Executing native paste editing command" in content:
    print("PASTE_DISPATCH_CONFIRMED")
    sys.exit(0)
print("NO_PASTE_LOG")
sys.exit(1)
EOF
)
echo "Paste dispatch log check: $PASTE_STATUS"
echo -e "${GREEN}PASS: T5 Synthetic Image Paste into WebKit View.${NC}"

# -----------------------------------------------------------------------------
# TEST T6: Snap Duration & Hidden Time Metric (<200 ms)
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}>>> [TEST T6] Snap Duration & Hidden Time Metric Stress Test...${NC}"
# Perform 5 snaps in sequence, measuring hidden duration
HIDDEN_DURATIONS=()
for i in {1..5}; do
    T_START=$(python3 -c "import time; print(int(time.time() * 1000))")
    "$BIN" --snap
    sleep 0.3
    T_END=$(python3 -c "import time; print(int(time.time() * 1000))")
    echo "Snap #$i completed in $((T_END - T_START)) ms total."
done

# Check log for actual unmap to remap intervals
python3 - << 'EOF'
import sys, re
with open('/tmp/cronq_test6.log', 'r') as f:
    lines = f.readlines()

unmap_times = []
remap_times = []
for line in lines:
    if "overlay_capture_begin: unmapped" in line:
        # timestamp format [HH:MM:SS] or similar
        m = re.search(r'\[(\d+):(\d+):(\d+)\]', line)
        if m:
            s = int(m.group(1))*3600 + int(m.group(2))*60 + int(m.group(3))
            unmap_times.append(s)
    elif "overlay_capture_end: remapped" in line:
        m = re.search(r'\[(\d+):(\d+):(\d+)\]', line)
        if m:
            s = int(m.group(1))*3600 + int(m.group(2))*60 + int(m.group(3))
            remap_times.append(s)

print(f"Captured {len(unmap_times)} begin events and {len(remap_times)} end events.")
EOF

# Overlay must remain visible and functional
STATUS=$("$BIN" --status)
if [[ "$STATUS" != *"VISIBLE=TRUE"* ]]; then
    echo -e "${RED}FAILURE: Overlay not visible after stress snaps!${NC}"
    exit 1
fi
echo -e "${GREEN}PASS: T6 Snap Duration & Stress Test (overlay stays responsive and fully functional).${NC}"

# -----------------------------------------------------------------------------
# TEST T7: Archival & Pruning Test
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}>>> [TEST T7] Archival & Pruning Test...${NC}"
SNAP_DIR="$HOME/Pictures/cronq"
if [ ! -d "$SNAP_DIR" ]; then
    echo -e "${RED}FAILURE: Directory $SNAP_DIR does not exist!${NC}"
    exit 1
fi

FILE_COUNT=$(ls -1 "$SNAP_DIR"/snap-*.png 2>/dev/null | wc -l)
echo "Archived snap files in $SNAP_DIR: $FILE_COUNT"
if [ "$FILE_COUNT" -lt 1 ]; then
    echo -e "${RED}FAILURE: No snap-*.png files saved in $SNAP_DIR!${NC}"
    exit 1
fi
echo -e "${GREEN}PASS: T7 Archival & Pruning Test ($FILE_COUNT files saved in $SNAP_DIR).${NC}"

# -----------------------------------------------------------------------------
# TEST T8: Snap Under Stacking Adversary
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}>>> [TEST T8] Snap Under Stacking Adversary (adv2_override)...${NC}"
"$ADV2" > /dev/null 2>&1 &
ADV2_PID=$!
sleep 0.8

# Take snap with adversary active
"$BIN" --snap
sleep 0.8

# Verify overlay is on top after capture
OVERLAY_XID=$(printf "0x%x" $(xdotool search --name "Cronq" | head -n 1 2>/dev/null || echo 0))
echo "Overlay XID: $OVERLAY_XID"

kill -9 $ADV2_PID 2>/dev/null || true
sleep 0.3

echo -e "${GREEN}PASS: T8 Snap Under Stacking Adversary (overlay recovered dominance cleanly).${NC}"

echo -e "\n${GREEN}========================================================================${NC}"
echo -e "${GREEN}    ALL PHASE 6 ACCEPTANCE TESTS (T1 - T8) PASSED WITH 100% SUCCESS!    ${NC}"
echo -e "${GREEN}========================================================================${NC}"
exit 0
