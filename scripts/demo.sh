#!/usr/bin/env bash
# ============================================================================
# Operating System-Based Train Ticket Reservation System - Live Demo Script
# Station Hub: Durgapur (DGR) | Eastern Railway (ER)
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

echo "======================================================================="
echo " STEP 1: Running Automated System Test Suite"
echo "======================================================================="
./run_tests

echo ""
echo "======================================================================="
echo " STEP 2: Running Multi-User OS Concurrency & Race-Condition Benchmark"
echo "======================================================================="
./train_system --simulate

echo ""
echo "======================================================================="
echo " STEP 3: Automated CLI Reservation & PNR Lifecycle Demonstration"
echo "======================================================================="
# 1. Login as admin
# 2. Book a ticket from Durgapur (DGR) to New Delhi (NDLS) in 3A
# 3. View operational metrics
# 4. Exit
printf "10\nadmin\nAdmin@123\n4\n12301\nDGR\nNDLS\n2026-12-15\n3A\n1\n2\nSiddhartha Ghosh\n64\nM\n1\nAnanya Ghosh\n32\nF\n3\n9\n0\n" | ./train_system

echo ""
echo "======================================================================="
echo " STEP 4: Inspecting Write-Ahead Log (WAL) & OS File Records"
echo "======================================================================="
if [ -f "data/transactions.wal" ]; then
    echo "--- Last 5 entries of data/transactions.wal ---"
    tail -n 5 data/transactions.wal
fi

echo ""
if [ -f "data/audit.log" ]; then
    echo "--- Last 5 entries of data/audit.log ---"
    tail -n 5 data/audit.log
fi

echo ""
echo "======================================================================="
echo " Train Ticket Reservation System Demo completed successfully!"
echo "======================================================================="
