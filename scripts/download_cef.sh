#!/usr/bin/env bash
# Downloads and extracts the pinned CEF linux64 binary into third_party/cef-linux64.
set -euo pipefail

VERSION="${1:-144.0.32+g5ce7d26+chromium-144.0.7559.258}"
PLATFORM="${2:-linux64}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THIRD_PARTY="$ROOT/third_party"
if [[ "$PLATFORM" == "linux64" || "$PLATFORM" == "linuxarm64" ]]; then
  CEF_DIR="$THIRD_PARTY/cef-$PLATFORM"
else
  CEF_DIR="$THIRD_PARTY/cef"
fi

FILE_NAME="cef_binary_${VERSION}_${PLATFORM}.tar.bz2"
ENCODED="$(python3 -c "import urllib.parse,sys; print(urllib.parse.quote(sys.argv[1], safe=''))" "$FILE_NAME")"
URL="https://cef-builds.spotifycdn.com/${ENCODED}"
ARCHIVE="$THIRD_PARTY/$FILE_NAME"

mkdir -p "$THIRD_PARTY"

if [[ ! -f "$ARCHIVE" ]]; then
  echo "Downloading $URL"
  curl -L --retry 3 --retry-delay 2 -o "$ARCHIVE" "$URL"
else
  echo "Using existing archive $ARCHIVE"
fi

EXTRACT="$THIRD_PARTY/cef_extract"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

echo "Extracting..."
tar -xjf "$ARCHIVE" -C "$EXTRACT"
INNER="$(find "$EXTRACT" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
if [[ -z "$INNER" ]]; then
  echo "Extraction failed: no directory found" >&2
  exit 1
fi

rm -rf "$CEF_DIR"
mv "$INNER" "$CEF_DIR"
rm -rf "$EXTRACT"

echo "CEF ready at $CEF_DIR"
