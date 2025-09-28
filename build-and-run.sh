#!/bin/bash
# [0.2.0] - 2025-09-15 - build-and-run.sh
# Added: Convenience script to compile, test, and launch IFG.

set -euo pipefail  # Strict mode

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/builddir"
EXECUTABLE="$BUILD_DIR/src/gtktext"
SCHEMA_DIR="$PROJECT_ROOT/data"

echo "🔧 Configuring (C23) and building IFG..."
if [ ! -d "$BUILD_DIR" ]; then
  meson setup "$BUILD_DIR" -D c_std=c23
else
  meson setup "$BUILD_DIR" --reconfigure -D c_std=c23
fi
meson compile -C "$BUILD_DIR"

echo "🧪 Running tests..."
# If no display is available, fall back to headless-safe tests or Xvfb wrapper
if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ]; then
  if command -v xvfb-run >/dev/null 2>&1; then
    echo "   No display found; running tests under Xvfb"
    xvfb-run -s "-screen 0 1024x768x24" meson test -C "$BUILD_DIR" --print-errorlogs || true
  else
    echo "   No display found and no Xvfb; running data-only suite"
    meson test -C "$BUILD_DIR" --suite gtktext:data --print-errorlogs || true
  fi
else
  meson test -C "$BUILD_DIR" --print-errorlogs || true
fi

echo "📋 Ensuring GSettings schema is compiled..."
if [ ! -f "$SCHEMA_DIR/gschemas.compiled" ] || [ "$SCHEMA_DIR/org.gtk.gtktext.gschema.xml" -nt "$SCHEMA_DIR/gschemas.compiled" ]; then
    echo "   Compiling GSettings schema..."
    glib-compile-schemas "$SCHEMA_DIR"
fi

echo "🚀 Launching IFG with Wayland backend (policy compliant)..."
echo "   Executable: $EXECUTABLE"
echo "   Schema dir: $SCHEMA_DIR"
echo "   Backend: Wayland (enforced)"

# Set environment and launch with Wayland enforcement
GDK_BACKEND=wayland GSETTINGS_SCHEMA_DIR="$SCHEMA_DIR" exec "$EXECUTABLE" "$@"
