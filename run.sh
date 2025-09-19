#!/bin/bash
# [0.2.0] - 2025-09-15 - run.sh
# Added: Quick run script for IFG.

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
EXECUTABLE="$PROJECT_ROOT/builddir/src/gtktext"
SCHEMA_DIR="$PROJECT_ROOT/data"

# Ensure schema is compiled
if [ ! -f "$SCHEMA_DIR/gschemas.compiled" ] || [ "$SCHEMA_DIR/org.gtk.gtktext.gschema.xml" -nt "$SCHEMA_DIR/gschemas.compiled" ]; then
    echo "📋 Compiling GSettings schema..."
    glib-compile-schemas "$SCHEMA_DIR"
fi

echo "🚀 Launching IFG with Wayland backend (policy compliant)..."
# Enforce Wayland-only policy as per CLAUDE.md guidelines
GDK_BACKEND=wayland GSETTINGS_SCHEMA_DIR="$SCHEMA_DIR" exec "$EXECUTABLE" "$@"
