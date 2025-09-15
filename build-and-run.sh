#!/bin/bash
# [0.2.0] - 2025-09-15 - build-and-run.sh
# Added: Convenience script to compile, test, and launch GTKText.

set -e  # Exit on any error

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/builddir"
EXECUTABLE="$BUILD_DIR/src/gtktext"
SCHEMA_DIR="$PROJECT_ROOT/data"

echo "🔧 Building GTKText..."
meson compile -C "$BUILD_DIR"

echo "🧪 Running tests..."
meson test -C "$BUILD_DIR"

echo "📋 Ensuring GSettings schema is compiled..."
if [ ! -f "$SCHEMA_DIR/gschemas.compiled" ] || [ "$SCHEMA_DIR/org.gtk.gtktext.gschema.xml" -nt "$SCHEMA_DIR/gschemas.compiled" ]; then
    echo "   Compiling GSettings schema..."
    glib-compile-schemas "$SCHEMA_DIR"
fi

echo "🚀 Launching GTKText..."
echo "   Executable: $EXECUTABLE"
echo "   Schema dir: $SCHEMA_DIR"

# Set environment and launch
GSETTINGS_SCHEMA_DIR="$SCHEMA_DIR" exec "$EXECUTABLE" "$@"
