# Contributing to Operating System-Based Train Ticket Reservation System

Thank you for your interest in contributing! This codebase simulates an Indian Railways Passenger Reservation System (PRS) using low-level Operating System concepts in C and SQL.

---

## 🛠️ Development Workflow

### 1. Prerequisites
- GCC / Clang (supporting C99 or later)
- GNU Make
- `libmariadb-dev` / `libmariadb3` (or MySQL client headers)
- POSIX-compliant operating system (Linux, macOS, BSD, WSL)

### 2. Building the Project
```bash
# Build train_system binary and test suite
make all

# Clean build artifacts
make clean
```

### 3. Running Verification Tests
Any proposed code changes must pass the entire test suite and the concurrency load simulation:
```bash
# Execute unit & integration tests
make test
# or
./run_tests

# Run multi-user concurrency stress test
./train_system --simulate
```

---

## 📐 Code Style Guidelines
- **C Standard:** Pure C99/C11 standard without compiler-specific extensions where possible.
- **Headers:** Keep interfaces modular in `include/` with guards (`#ifndef HEADER_H`).
- **Memory & Resources:** Always pair resource acquisition (`open()`, `pthread_mutex_lock()`, `socket()`) with guaranteed release (`close()`, `pthread_mutex_unlock()`, signal flush).
- **Advisory Record Locking:** Maintain POSIX `fcntl` record locking semantics for multi-process safety.
- **Durability:** Critical state transitions must issue Write-Ahead Log (WAL) records followed by `fsync()`.
