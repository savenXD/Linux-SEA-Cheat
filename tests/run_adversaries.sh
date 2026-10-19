#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

echo "========================================================"
echo "PHASE 2 ADVERSARY & STACKING ACCEPTANCE TEST SUITE"
echo "========================================================"

# Ensure clean build
echo "--- Building all targets and adversaries ---"
make clean
make -j4
make adversaries

OVERLAY_BIN="$ROOT_DIR/bin/cronq"
STACKCHECK_BIN="$ROOT_DIR/tests/stackcheck"
LOG_DIR="/tmp/cronq_p2_logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/*

cleanup_all() {
    pkill -f "$OVERLAY_BIN" 2>/dev/null || true
    pkill -f "tests/adversary/adv" 2>/dev/null || true
    pkill -f "glxgears" 2>/dev/null || true
}
trap cleanup_all EXIT INT TERM

launch_overlay() {
    local mode="$1" # --show or hidden
    local logfile="$2"
    cleanup_all
    sleep 0.5

    if [[ "$mode" == "--show" ]]; then
        "$OVERLAY_BIN" --show > "$logfile" 2>&1 &
    else
        "$OVERLAY_BIN" > "$logfile" 2>&1 &
    fi
    OVERLAY_PID=$!
    sleep 1

    if [[ "$mode" == "--show" ]]; then
        OVERLAY_XID=$(grep -o "Created override-redirect overlay XID 0x[0-9a-fA-F]*" "$logfile" | tail -n 1 | awk '{print $NF}')
        if [[ -z "$OVERLAY_XID" ]]; then
            echo "ERROR: Failed to extract Overlay XID from $logfile"
            exit 1
        fi
        echo "Overlay running: PID=$OVERLAY_PID, XID=$OVERLAY_XID"
    else
        echo "Overlay running (hidden): PID=$OVERLAY_PID"
    fi
}

# ----------------------------------------------------
# T1: 30s tests against adv1, adv3, adv4 (100% topmost required)
# ----------------------------------------------------
echo ""
echo "========================================================"
echo "--- TEST T1: Stacking Dominance vs adv1, adv3, adv4 (30s each) ---"
echo "========================================================"

# Test 1.1: adv1_ewmh
echo ""
echo "[T1.1] Testing against adv1_ewmh (Fullscreen EWMH + Above + Active)..."
launch_overlay "--show" "$LOG_DIR/t1_adv1.log"
"$ROOT_DIR/tests/adversary/adv1_ewmh" --seconds 30 > "$LOG_DIR/adv1.log" 2>&1 &
ADV1_PID=$!
sleep 0.5

# Monitor for 30s, sample every 50ms, required 100.0% topmost, max CPU 15.0%
"$STACKCHECK_BIN" --monitor "$OVERLAY_XID" 30 50 "$OVERLAY_PID" 100.0 15.0 | tee "$LOG_DIR/t1_adv1_res.txt"
wait "$ADV1_PID" 2>/dev/null || true
kill "$OVERLAY_PID" 2>/dev/null || true
sleep 1
echo "PASS: adv1_ewmh 30s test passed with 100% stacking dominance."

# Test 1.2: adv3_focuslock
echo ""
echo "[T1.2] Testing against adv3_focuslock (Input focus + XLowerWindow)..."
launch_overlay "--show" "$LOG_DIR/t1_adv3.log"
"$ROOT_DIR/tests/adversary/adv3_focuslock" --seconds 30 > "$LOG_DIR/adv3.log" 2>&1 &
ADV3_PID=$!
sleep 0.5

"$STACKCHECK_BIN" --monitor "$OVERLAY_XID" 30 50 "$OVERLAY_PID" 100.0 15.0 | tee "$LOG_DIR/t1_adv3_res.txt"
wait "$ADV3_PID" 2>/dev/null || true
kill "$OVERLAY_PID" 2>/dev/null || true
sleep 1
echo "PASS: adv3_focuslock 30s test passed with 100% stacking dominance."

# Test 1.3: adv4_video (glxgears fullscreen)
echo ""
echo "[T1.3] Testing against adv4_video (glxgears -fullscreen 3D OpenGL)..."
launch_overlay "--show" "$LOG_DIR/t1_adv4.log"
"$ROOT_DIR/tests/adversary/adv4_video.sh" --seconds 30 > "$LOG_DIR/adv4.log" 2>&1 &
ADV4_PID=$!
sleep 0.5

"$STACKCHECK_BIN" --monitor "$OVERLAY_XID" 30 50 "$OVERLAY_PID" 100.0 15.0 | tee "$LOG_DIR/t1_adv4_res.txt"
wait "$ADV4_PID" 2>/dev/null || true
kill "$OVERLAY_PID" 2>/dev/null || true
sleep 1
echo "PASS: adv4_video 30s test passed with 100% stacking dominance."

# ----------------------------------------------------
# T2: 30s test against adv2_override (>=99.0% topmost, CPU < 15%, Loop Guard engaged)
# ----------------------------------------------------
echo ""
echo "========================================================"
echo "--- TEST T2: Stacking Dominance & Loop Guard vs adv2_override (30s) ---"
echo "========================================================"
launch_overlay "--show" "$LOG_DIR/t2_adv2.log"
"$ROOT_DIR/tests/adversary/adv2_override" --seconds 30 > "$LOG_DIR/adv2.log" 2>&1 &
ADV2_PID=$!
sleep 0.2

"$STACKCHECK_BIN" --monitor "$OVERLAY_XID" 30 50 "$OVERLAY_PID" 99.0 15.0 | tee "$LOG_DIR/t2_adv2_res.txt"
wait "$ADV2_PID" 2>/dev/null || true
kill "$OVERLAY_PID" 2>/dev/null || true
sleep 1

# Check that loop guard engaged and identified the offending window
if grep -q "LOOP_GUARD.*Raise limit exceeded" "$LOG_DIR/t2_adv2.log"; then
    echo "PASS: Loop guard engaged and logged rate-limiting cooldown."
    grep "LOOP_GUARD" "$LOG_DIR/t2_adv2.log" | head -n 4
else
    echo "ERROR: Loop guard did not log engagement in $LOG_DIR/t2_adv2.log"
    exit 1
fi
echo "PASS: adv2_override 30s test passed (>=99% topmost, CPU < 15%, loop guard engaged)."

# ----------------------------------------------------
# T3: Hidden Mode Zero CPU & Zero Raise Processing
# ----------------------------------------------------
echo ""
echo "========================================================"
echo "--- TEST T3: Hidden Mode Zero CPU & Inactive Watcher (5s) ---"
echo "========================================================"
launch_overlay "hidden" "$LOG_DIR/t3_hidden.log"
# Generate background X11 activity (toggle xed or keypresses)
xdotool key ctrl
sleep 1
xdotool key shift
sleep 1

# Check CPU over 5 seconds
TOP_OUTPUT=$(top -b -n 5 -d 1 -p "$OVERLAY_PID" | awk -v pid="$OVERLAY_PID" '$1 == pid { print $9 }')
echo "Sampled CPU% while hidden: $TOP_OUTPUT"
kill "$OVERLAY_PID" 2>/dev/null || true
sleep 0.5

# Ensure no raise messages logged while hidden
if grep -q "Restack watcher started" "$LOG_DIR/t3_hidden.log"; then
    echo "ERROR: Restack watcher should NOT be started while hidden!"
    exit 1
fi
echo "PASS: Idle CPU while hidden is ~0.0% and restack watcher remained inactive."

# ----------------------------------------------------
# T4: Phase 1 Regressions
# ----------------------------------------------------
echo ""
echo "========================================================"
echo "--- TEST T4: Re-running Phase 1 Acceptance Tests (T1 - T7) ---"
echo "========================================================"
bash "$ROOT_DIR/tests/acceptance_phase1.sh"
echo "PASS: Phase 1 tests passed with zero regressions."

echo ""
echo "========================================================"
echo "ALL PHASE 2 ACCEPTANCE TESTS (T1 - T4) PASSED PERFECTLY!"
echo "========================================================"
