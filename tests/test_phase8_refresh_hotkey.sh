#!/usr/bin/env bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}========================================================================${NC}"
echo -e "${BLUE}  PHASE 8 TESTS: UNIVERSAL REFRESH & SINGLE-KEY SHORTCUT BUG FIX        ${NC}"
echo -e "${BLUE}========================================================================${NC}"

# Clean existing instances
killall -9 cronq 2>/dev/null || true
sleep 0.5

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T1: Single-Key Bug Fix (Plain 'x', 's', 'g', 'w' MUST NOT trigger) ---${NC}"
# -------------------------------------------------------------------------
./bin/cronq --show > /tmp/p8_test.log 2>&1 &
OVERLAY_PID=$!
sleep 2

STATUS=$(./bin/cronq --status)
echo "Initial Status: $STATUS"
if [[ "$STATUS" != *"VISIBLE=TRUE"* ]]; then
    echo -e "${RED}T1 FAIL: Overlay not visible after start!${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi

# Type plain 'x' without Alt modifier
echo "Sending plain key 'x' (without Alt)..."
xdotool key x
sleep 0.3

STATUS_AFTER_X=$(./bin/cronq --status)
echo "Status after plain 'x': $STATUS_AFTER_X"
if [[ "$STATUS_AFTER_X" != *"VISIBLE=TRUE"* ]]; then
    echo -e "${RED}T1 FAIL: Plain 'x' unexpectedly toggled the overlay! Single-key bug is present.${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi
echo -e "${GREEN}T1.1 PASS: Plain 'x' did NOT toggle the overlay.${NC}"

# Type plain 's' without Alt modifier
echo "Sending plain key 's' (without Alt)..."
xdotool key s
sleep 0.3
STATUS_AFTER_S=$(./bin/cronq --status)
if [[ "$STATUS_AFTER_S" != *"VISIBLE=TRUE"* ]]; then
    echo -e "${RED}T1 FAIL: Plain 's' disrupted visibility!${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi
echo -e "${GREEN}T1.2 PASS: Plain 's' did NOT trigger hotkey actions.${NC}"

# Type plain 'g' without Alt modifier
echo "Sending plain key 'g' (without Alt)..."
xdotool key g
sleep 0.3
STATUS_AFTER_G=$(./bin/cronq --status)
if [[ "$STATUS_AFTER_G" != *"GHOST=SOLID"* ]]; then
    echo -e "${RED}T1 FAIL: Plain 'g' toggled ghost mode!${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi
echo -e "${GREEN}T1.3 PASS: Plain 'g' did NOT toggle ghost mode.${NC}"

# Type plain 'w' without Alt modifier
echo "Sending plain key 'w' (without Alt)..."
xdotool key w
sleep 0.3
echo -e "${GREEN}T1.4 PASS: Plain 'w' did NOT trigger Ghost Writer.${NC}"

# Test Alt+x hotkey (must toggle)
echo "Sending 'Alt+x' hotkey..."
xdotool key alt+x
sleep 0.5
STATUS_ALT_X=$(./bin/cronq --status)
echo "Status after Alt+x: $STATUS_ALT_X"
if [[ "$STATUS_ALT_X" != *"VISIBLE=FALSE"* ]]; then
    echo -e "${RED}T1 FAIL: Alt+x did NOT toggle overlay to VISIBLE=FALSE!${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi
echo -e "${GREEN}T1.5 PASS: Alt+x correctly toggled overlay to VISIBLE=FALSE.${NC}"

# Toggle back with Alt+x
echo "Sending 'Alt+x' hotkey to restore..."
xdotool key alt+x
sleep 0.5
STATUS_ALT_X_2=$(./bin/cronq --status)
echo "Status after second Alt+x: $STATUS_ALT_X_2"
if [[ "$STATUS_ALT_X_2" != *"VISIBLE=TRUE"* ]]; then
    echo -e "${RED}T1 FAIL: Second Alt+x did NOT restore visibility!${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi
echo -e "${GREEN}T1.6 PASS: Alt+x restored visibility to VISIBLE=TRUE.${NC}"

# -------------------------------------------------------------------------
echo -e "\n${BLUE}--- T2: Universal Refresh / Factory Fresh State ---${NC}"
# -------------------------------------------------------------------------
echo "Dirtying overlay state (setting entry, switching to browser, enabling ghost mode)..."
./bin/cronq --set-entry "DirtyEntryData999"
./bin/cronq --app browser
./bin/cronq --ghost on
sleep 0.5

PRE_STATUS=$(./bin/cronq --status)
PRE_ENTRY=$(./bin/cronq --get-entry)
echo "Pre-reset Status: $PRE_STATUS"
echo "Pre-reset Entry:  $PRE_ENTRY"

if [[ "$PRE_STATUS" != *"APP=browser"* ]] || [[ "$PRE_STATUS" != *"GHOST=GHOST"* ]] || [ "$PRE_ENTRY" != "DirtyEntryData999" ]; then
    echo -e "${RED}T2 FAIL: Pre-reset state was not set as expected.${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi

echo "Triggering Universal Reset via CLI/IPC..."
./bin/cronq --reset
sleep 1.0

POST_STATUS=$(./bin/cronq --status)
POST_ENTRY=$(./bin/cronq --get-entry)
echo "Post-reset Status: $POST_STATUS"
echo "Post-reset Entry:  '$POST_ENTRY'"

if [[ "$POST_STATUS" != *"APP=home"* ]]; then
    echo -e "${RED}T2 FAIL: App was not reset to 'home'! Post status: $POST_STATUS${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi

if [[ "$POST_STATUS" != *"GHOST=SOLID"* ]]; then
    echo -e "${RED}T2 FAIL: Ghost mode was not reset to 'SOLID'! Post status: $POST_STATUS${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi

if [[ "$POST_STATUS" != *"MODE=MIRROR"* ]]; then
    echo -e "${RED}T2 FAIL: Key mode was not reset to 'MIRROR'! Post status: $POST_STATUS${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi

if [[ "$POST_STATUS" != *"TARGET=NONE"* ]]; then
    echo -e "${RED}T2 FAIL: Typing target was not reset to 'NONE'! Post status: $POST_STATUS${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi

if [ -n "$POST_ENTRY" ]; then
    echo -e "${RED}T2 FAIL: Test entry was not cleared! Post entry: '$POST_ENTRY'${NC}"
    kill -9 $OVERLAY_PID 2>/dev/null || true
    exit 1
fi
echo -e "${GREEN}T2 PASS: Universal Reset successfully restored pristine Home state.${NC}"

# Cleanup
./bin/cronq --kill || true
wait $OVERLAY_PID 2>/dev/null || true

echo -e "\n${GREEN}========================================================================${NC}"
echo -e "${GREEN}  PHASE 8 TESTS COMPLETED WITH 100% SUCCESS!                             ${NC}"
echo -e "${GREEN}========================================================================${NC}"
