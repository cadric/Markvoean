#!/usr/bin/env bash
# Run test suite under a virtual X display (Xvfb) when no display is available.
# Usage: ./scripts/test-headless.sh [meson-args...]

set -euo pipefail

BUILD_DIR=${BUILD_DIR:-builddir}

if command -v xvfb-run >/dev/null 2>&1; then
  echo "🧪 Running tests under Xvfb (headless)…"
  xvfb-run -s "-screen 0 1024x768x24" meson test -C "$BUILD_DIR" --print-errorlogs "$@"
else
  echo "⚠️  xvfb-run not found; running tests directly. GUI tests may fail without a display."
  meson test -C "$BUILD_DIR" --print-errorlogs "$@"
fi

