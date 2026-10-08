#!/usr/bin/env bash
# Runs clang-tidy over first-party sources using a configured build directory
# (default: build/clang, from `cmake --preset clang`). Fails on any finding.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
BUILD_DIR="${1:-build/clang}"
RUN_TIDY="${RUN_CLANG_TIDY:-run-clang-tidy-18}"
command -v "$RUN_TIDY" >/dev/null || RUN_TIDY=run-clang-tidy
LOG="$(mktemp)"
"$RUN_TIDY" -p "$BUILD_DIR" -j "$(nproc 2>/dev/null || echo 2)" -quiet \
    "$(pwd)/(core|dsp|testing|plugins|devices|engine|app|tools)/.*\.(cpp|mm)$" >"$LOG" 2>&1 || true
if grep -E "(warning|error):" "$LOG" | grep -v "warnings generated" >/dev/null; then
    grep -E "(warning|error):" "$LOG" | sort -u
    echo "clang-tidy reported findings."
    exit 1
fi
echo "clang-tidy passed."
