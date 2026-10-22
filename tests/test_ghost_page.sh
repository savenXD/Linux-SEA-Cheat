#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /tmp/cronq-ghost-test.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT
make -j4
# The test includes window.c and ghost.c to exercise the real page callbacks.
cc -std=gnu11 -Wall -Wextra -O0 -g \
  $(pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 x11 xi xext xrandr xtst) \
  tests/test_ghost_page.c src/input.o src/hotkey.o src/raise.o src/xutil.o src/config.o src/browser.o \
  $(pkg-config --libs gtk+-3.0 webkit2gtk-4.1 x11 xi xext xrandr xtst) -lm -o "$test_dir/test-ghost-page"
XDG_CONFIG_HOME="$test_dir/config" "$test_dir/test-ghost-page"
