#!/bin/bash
# [0.2.0] - 2025-09-15 - build.sh
# Added: Quick build script for IFG.

set -e

echo "🔧 Building IFG..."
meson compile -C builddir

echo "✅ Build complete! Run with: ./run.sh"
