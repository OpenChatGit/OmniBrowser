#!/usr/bin/env bash
# Configure and build Omni Browser for Linux (intended to run inside WSL).
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "Run this inside WSL." >&2
  echo "  wsl -d Ubuntu -- bash -lc 'cd /mnt/c/Users/Nicol/Documents/Github/browser && bash scripts/build_linux.sh'" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -f "$HOME/.cargo/env" ]]; then
  # shellcheck disable=SC1091
  source "$HOME/.cargo/env"
fi

for tool in cmake ninja cargo rustc python3 curl tar; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing $tool. Run: bash scripts/setup_wsl.sh" >&2
    exit 1
  fi
done

CEF_DIR="$ROOT/third_party/cef-linux64"
if [[ ! -f "$CEF_DIR/cmake/FindCEF.cmake" ]]; then
  echo "Downloading CEF linux64..."
  bash "$ROOT/scripts/download_cef.sh"
fi

BUILD_DIR="${OMNI_BUILD_DIR:-build-linux}"
cmake -S "$ROOT" -B "$ROOT/$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
  -DUSE_SANDBOX=OFF

cmake --build "$ROOT/$BUILD_DIR" --target OmniBrowser

OUT="$ROOT/$BUILD_DIR/native/Release"
if [[ ! -x "$OUT/OmniBrowser" ]]; then
  OUT="$ROOT/$BUILD_DIR/native"
fi

echo
echo "Build OK. Start from the output directory (libcef.so + pak + ui must sit next to the binary):"
echo "  cd \"$OUT\""
echo "  ./OmniBrowser"
