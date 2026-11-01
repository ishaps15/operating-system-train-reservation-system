#!/usr/bin/env bash
# ============================================================================
# Operating System-Based Train Ticket Reservation System
# MySQL / MariaDB Database Provisioning Script
# Campus / Division: Durgapur (DGR) / Asansol (ASN), Eastern Railway
# ============================================================================

set -euo pipefail

DB_USER="${1:-root}"
DB_PASS="${2:-}"
DB_HOST="${3:-127.0.0.1}"
DB_PORT="${4:-3306}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

echo "======================================================================="
echo " Provisioning Railway Relational Database (MySQL / MariaDB)"
echo " Host: $DB_HOST:$DB_PORT | User: $DB_USER"
echo "======================================================================="

MYSQL_CMD="mysql -h $DB_HOST -P $DB_PORT -u $DB_USER"
if [ -n "$DB_PASS" ]; then
    MYSQL_CMD="$MYSQL_CMD -p$DB_PASS"
fi

echo "[1/2] Loading 3NF Relational Schema (sql/schema.sql)..."
$MYSQL_CMD < "$ROOT_DIR/sql/schema.sql"

echo "[2/2] Loading Eastern Railway Seed Dataset (sql/seed_data.sql)..."
$MYSQL_CMD < "$ROOT_DIR/sql/seed_data.sql"

echo "======================================================================="
echo " Database successfully provisioned and verified!"
echo " Run with live MySQL integration using:"
echo "   ./train_system --mysql --host $DB_HOST --port $DB_PORT --user $DB_USER"
echo "======================================================================="
