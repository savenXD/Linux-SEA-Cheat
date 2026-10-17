#!/usr/bin/env bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}========================================================================${NC}"
echo -e "${BLUE}    RUNNING COMPLETE CONSOLIDATED ACCEPTANCE TEST SUITE (PHASES 1-6)    ${NC}"
echo -e "${BLUE}========================================================================${NC}"

# Kill any stray processes
killall -9 phoneoverlay colorwin adv1_ewmh adv2_override adv3_focuslock stackcheck 2>/dev/null || true
sleep 0.5

echo -e "\n${BLUE}>>> [SUITE 1/6] Running Phase 1 Acceptance Tests...${NC}"
bash tests/acceptance_phase1.sh
echo -e "${GREEN}>>> [SUITE 1/6] Phase 1 Suite PASSED (100%)${NC}"

echo -e "\n${BLUE}>>> [SUITE 2/6] Running Phase 2 Adversary Stacking Tests...${NC}"
bash tests/run_adversaries.sh
echo -e "${GREEN}>>> [SUITE 2/6] Phase 2 Suite PASSED (100%)${NC}"

echo -e "\n${BLUE}>>> [SUITE 3/6] Running Phase 3 Input Model & Invariance Tests...${NC}"
bash tests/run_phase3_tests.sh
echo -e "${GREEN}>>> [SUITE 3/6] Phase 3 Suite PASSED (100%)${NC}"

echo -e "\n${BLUE}>>> [SUITE 4/6] Running Phase 4 Browser, UI, IPC & Supervisor Tests...${NC}"
bash tests/test_phase4_browser.sh
echo -e "${GREEN}>>> [SUITE 4/6] Phase 4 Suite PASSED (100%)${NC}"

echo -e "\n${BLUE}>>> [SUITE 5/6] Running Phase 5 ChatGPT, Dual Apps & Clipboard Shield Tests...${NC}"
bash tests/test_phase5_apps.sh
echo -e "${GREEN}>>> [SUITE 5/6] Phase 5 Suite PASSED (100%)${NC}"

echo -e "\n${BLUE}>>> [SUITE 6/7] Running Phase 6 Screen Capture (Snap) Acceptance Tests...${NC}"
bash tests/test_phase6_snap.sh
echo -e "${GREEN}>>> [SUITE 6/7] Phase 6 Suite PASSED (100%)${NC}"

echo -e "\n${BLUE}>>> [SUITE 7/7] Running Phase 7 Ghost Writer Acceptance Tests...${NC}"
bash tests/test_phase7_ghost.sh
echo -e "${GREEN}>>> [SUITE 7/7] Phase 7 Suite PASSED (100%)${NC}"

echo -e "\n${GREEN}========================================================================${NC}"
echo -e "${GREEN}    CONGRATULATIONS: ALL 7 TEST SUITES PASSED WITH 100% SUCCESS!        ${NC}"
echo -e "${GREEN}========================================================================${NC}"
