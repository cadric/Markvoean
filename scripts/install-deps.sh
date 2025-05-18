#!/bin/bash
# Script to install dependencies for GTKText

echo "Detecting Linux distribution..."

# Check for package managers
if command -v apt-get &> /dev/null; then
    echo "Debian/Ubuntu detected"
    echo "Installing dependencies..."
    sudo apt-get update
    sudo apt-get install -y build-essential pkg-config libgtk-4-dev libadwaita-1-dev libcmark-dev
elif command -v dnf &> /dev/null; then
    echo "Fedora detected"
    echo "Installing dependencies..."
    sudo dnf install -y gcc make pkgconfig gtk4-devel libadwaita-devel libcmark-devel
elif command -v pacman &> /dev/null; then
    echo "Arch Linux detected"
    echo "Installing dependencies..."
    sudo pacman -S --needed base-devel gtk4 libadwaita cmark
else
    echo "Unsupported distribution. Please install these packages manually:"
    echo "- GTK4 development package"
    echo "- libadwaita development package"
    echo "- cmark library development package"
    echo "- build tools (gcc, make, pkg-config)"
    exit 1
fi

echo "Dependencies installed successfully!"
exit 0
