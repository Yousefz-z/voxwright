#!/usr/bin/env bash
# Fails if any C, C++, or Objective-C++ source is not clang-format clean.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
CLANG_FORMAT="${CLANG_FORMAT:-clang-format-18}"
command -v "$CLANG_FORMAT" >/dev/null || CLANG_FORMAT=clang-format
git ls-files --cached --others --exclude-standard -- '*.c' '*.cpp' '*.hpp' '*.h' '*.mm' \
    | grep -v '^ports/' \
    | xargs -r "$CLANG_FORMAT" --dry-run --Werror
echo "Format check passed."
