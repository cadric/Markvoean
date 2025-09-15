#!/bin/bash
# [0.2.0] - 2025-09-15 - build.sh
# Added: Quick build script for GTKText.

set -e

echo "🔧 Building GTKText..."
meson compile -C builddir

echo "✅ Build complete! Run with: ./run.sh"
