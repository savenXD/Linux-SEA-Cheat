#!/usr/bin/env bash
set -e

DURATION=30
while [[ $# -gt 0 ]]; do
    case "$1" in
        --seconds)
            DURATION="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

echo "[ADV4] Starting Fullscreen Video/Graphics adversary (glxgears -fullscreen) for ${DURATION}s..."

# Launch glxgears in fullscreen
glxgears -fullscreen >/dev/null 2>&1 &
GEARS_PID=$!

cleanup() {
    if kill -0 "$GEARS_PID" 2>/dev/null; then
        kill "$GEARS_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

sleep "$DURATION"
echo "[ADV4] Video adversary finished cleanly."
