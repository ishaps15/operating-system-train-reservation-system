#!/usr/bin/env bash
# ============================================================================
# Operating System-Based Train Ticket Reservation System - Build Script
# Campus / Station Node: Durgapur (DGR), West Bengal
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

if [ -f "./toolchain/usr/bin/make" ]; then
    MAKE_BIN="./toolchain/usr/bin/make"
elif command -v make >/dev/null 2>&1; then
    MAKE_BIN="make"
else
    echo "Error: make compiler utility not found." >&2
    exit 1
fi

echo "======================================================================="
echo " Building Eastern Railway PRS (C & SQL Operating System Suite)..."
echo "======================================================================="
$MAKE_BIN all

echo ""
echo "======================================================================="
echo " Executing System Test Suite..."
echo "======================================================================="
./run_tests
