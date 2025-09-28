#!/usr/bin/env bash
# Run include-what-you-use (iwyu) if available, using compile_commands.json.

set -euo pipefail

BUILD_DIR=${BUILD_DIR:-builddir}
if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
  echo "ℹ️  Generating compile_commands.json…"
  meson setup "$BUILD_DIR" --reconfigure >/dev/null
fi

if ! command -v iwyu >/dev/null 2>&1; then
  echo "⚠️  iwyu not found; skipping."
  exit 0
fi

mapfile -t FILES < <(rg --files -g '!$BUILD_DIR/**' -n --iglob 'src/**/*.c')
if [ ${#FILES[@]} -eq 0 ]; then
  echo "No source files found"
  exit 0
fi

echo "🔎 Running iwyu on ${#FILES[@]} files…"
iwyu -p "$BUILD_DIR" "${FILES[@]}"

