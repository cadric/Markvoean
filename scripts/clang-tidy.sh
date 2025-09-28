#!/usr/bin/env bash
# Run clang-tidy over project sources using compile_commands.json from Meson.
# Usage: ./scripts/clang-tidy.sh [filters]

set -euo pipefail

BUILD_DIR=${BUILD_DIR:-builddir}
if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
  echo "ℹ️  Generating compile_commands.json…"
  meson setup "$BUILD_DIR" --reconfigure >/dev/null
fi

if ! command -v clang-tidy >/dev/null 2>&1; then
  echo "⚠️  clang-tidy not found; skipping."
  exit 0
fi

mapfile -t FILES < <(rg --files -g '!$BUILD_DIR/**' -n --iglob 'src/**/*.c')
if [ ${#FILES[@]} -eq 0 ]; then
  echo "No source files found"
  exit 0
fi

echo "🔎 Running clang-tidy on ${#FILES[@]} files…"
clang-tidy -p "$BUILD_DIR" "$@" "${FILES[@]}"

