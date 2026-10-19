#!/usr/bin/env bash
# ==============================================================================
# Cronq — Automated Uninstaller
# ==============================================================================
set -e

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${CYAN}${BOLD}"
echo "  ================================================================"
echo "    🗑️  Cronq — Desktop Uninstaller"
echo "  ================================================================"
echo -e "${NC}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${BOLD}[1/3] Terminating any running Cronq processes...${NC}"
pkill -9 -f cronq-supervisor 2>/dev/null || true
pkill -9 -f cronq 2>/dev/null || true
sleep 1
echo -e "${GREEN}✓ All processes stopped.${NC}"

echo -e "\n${BOLD}[2/3] Removing installed binaries and desktop integrations...${NC}"
make uninstall

echo -e "\n${BOLD}[3/3] Checking application data and cache...${NC}"
PURGE=0
if [ "$1" = "--purge" ]; then
    PURGE=1
elif [ -t 0 ]; then
    read -r -p "Do you also want to remove user data/cookies (~/.config/cronq & ~/.local/share/cronq)? [y/N]: " answer
    case "$answer" in
        [yY][eE][sS]|[yY]) PURGE=1 ;;
        *) PURGE=0 ;;
    esac
fi

if [ "$PURGE" -eq 1 ]; then
    rm -rf "$HOME/.config/cronq" "$HOME/.local/share/cronq"
    echo -e "${GREEN}✓ User data and cache purged.${NC}"
else
    echo -e "${CYAN}• User data preserved in ~/.config/cronq and ~/.local/share/cronq.${NC}"
fi

echo -e "\n${GREEN}${BOLD}================================================================"
echo "  ✓ Cronq has been completely uninstalled from your system."
echo -e "================================================================${NC}\n"
