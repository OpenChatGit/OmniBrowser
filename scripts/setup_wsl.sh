#!/usr/bin/env bash
# Installs Omni Browser Linux build dependencies inside WSL (Ubuntu/Debian).
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "Run this script inside WSL, not from Windows PowerShell." >&2
  echo "  wsl -d Ubuntu -- bash -lc 'cd /mnt/c/Users/Nicol/Documents/Github/browser && bash scripts/setup_wsl.sh'" >&2
  exit 1
fi

sudo apt-get update
sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config python3 \
  libgtk-3-dev libnss3-dev libxss-dev libasound2-dev \
  libx11-dev libx11-xcb-dev libxcomposite-dev libxdamage-dev \
  libxrandr-dev libxkbcommon-dev libgbm-dev libpango1.0-dev \
  libatk1.0-dev libatk-bridge2.0-dev libcups2-dev libdrm-dev \
  libxshmfence-dev libxi-dev libxtst-dev libxcursor-dev \
  libxinerama-dev libdbus-1-dev \
  curl tar unzip zip git zenity

if ! command -v rustc >/dev/null 2>&1 || ! command -v cargo >/dev/null 2>&1; then
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
  # shellcheck disable=SC1091
  source "$HOME/.cargo/env"
fi

echo
echo "Tools:"
cmake --version | head -n 1
rustc --version
cargo --version
echo
echo "WSL setup complete. Next: bash scripts/build_linux.sh"
