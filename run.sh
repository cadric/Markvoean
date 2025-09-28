#!/bin/bash
# [0.2.1] - 2025-09-28 - run.sh
# Changed: Ensure builddir is configured for C23 prior to run.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/builddir"
EXECUTABLE="$BUILD_DIR/src/gtktext"
SCHEMA_DIR="$PROJECT_ROOT/data"

# Ensure builddir exists and is configured for C23
if [ ! -d "$BUILD_DIR" ]; then
  echo "🔧 Configuring builddir (C23)…"
  meson setup "$BUILD_DIR" -D c_std=c23
else
  echo "🔧 Reconfiguring builddir (C23)…"
  meson setup "$BUILD_DIR" --reconfigure -D c_std=c23
fi

# Build if missing
if [ ! -x "$EXECUTABLE" ]; then
  echo "🔨 Building…"
  meson compile -C "$BUILD_DIR"
fi

# Ensure schema is compiled
if [ ! -f "$SCHEMA_DIR/gschemas.compiled" ] || [ "$SCHEMA_DIR/org.gtk.gtktext.gschema.xml" -nt "$SCHEMA_DIR/gschemas.compiled" ]; then
    echo "📋 Compiling GSettings schema..."
    glib-compile-schemas "$SCHEMA_DIR"
fi

echo "🚀 Launching IFG with Wayland backend (policy compliant)..."
GDK_BACKEND=wayland GSETTINGS_SCHEMA_DIR="$SCHEMA_DIR" exec "$EXECUTABLE" "$@"
