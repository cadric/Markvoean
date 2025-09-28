#!/usr/bin/env bash
# Install build and runtime dependencies for the GTK4/libadwaita project.

set -euo pipefail

echo "🔍 Detecting Linux distribution..."

install_apt() {
  echo "🐧 Debian/Ubuntu detected"
  sudo apt-get update
  sudo apt-get install -y \
    build-essential pkg-config \
    libgtk-4-dev libadwaita-1-dev libcmark-dev libsoup-3.0-dev \
    ninja-build python3-pip
}

install_dnf() {
  echo "🦄 Fedora detected"
  sudo dnf install -y \
    gcc make pkgconfig \
    gtk4-devel libadwaita-devel libcmark-devel libsoup3-devel \
    ninja-build python3-pip
}

install_pacman() {
  echo "🟩 Arch Linux detected"
  sudo pacman -S --needed --noconfirm \
    base-devel pkgconf \
    gtk4 libadwaita cmark libsoup3 \
    ninja python-pip
}

if command -v apt-get >/dev/null 2>&1; then
  install_apt
elif command -v dnf >/dev/null 2>&1; then
  install_dnf
elif command -v pacman >/dev/null 2>&1; then
  install_pacman
else
  echo "❌ Unsupported distribution. Please install these packages manually:"
  echo "   - GTK4 + libadwaita + libcmark (+ libsoup-3.0) development packages"
  echo "   - Build tools (gcc, make/base-devel, pkg-config), ninja"
  echo "   - Python 3 + pip"
  exit 1
fi

echo "🔧 Ensuring Meson (>= 1.7.2) is available..."
if command -v meson >/dev/null 2>&1; then
  if meson --version | awk 'BEGIN{ok=0} {split($1,v,"."); if ((v[1]>1) || (v[1]==1 && v[2]>=7)) ok=1} END{exit ok?0:1}'; then
    echo "   Meson $(meson --version) is recent enough"
  else
    echo "   System Meson too old; installing/upgrading via pip (user)"
    python3 -m pip install --user --upgrade "meson>=1.7.2"
    echo "   Add ~/.local/bin to PATH if you haven't: export PATH=\"$HOME/.local/bin:$PATH\""
  fi
else
  echo "   Installing Meson via pip (user)"
  python3 -m pip install --user "meson>=1.7.2"
  echo "   Add ~/.local/bin to PATH if you haven't: export PATH=\"$HOME/.local/bin:$PATH\""
fi

echo "✅ Dependencies installed successfully!"
exit 0
