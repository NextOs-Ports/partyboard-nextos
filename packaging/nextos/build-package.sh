#!/bin/bash
# PartyBoard — NextOS package builder (Mali-450/GLES2).
# Assembles a clean staging tree from the build output + the SDL3 mali driver.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD=""
SDL3=""
STAGE=""
ROM=""
MENU_IMAGE=""

usage() {
  echo "Usage: $0 --build-dir DIR --sdl3 FILE --stage-dir DIR [--rom FILE] [--menu-image FILE]"
}

while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD="$2"; shift 2 ;;
    --sdl3) SDL3="$2"; shift 2 ;;
    --stage-dir) STAGE="$2"; shift 2 ;;
    --rom) ROM="$2"; shift 2 ;;
    --menu-image) MENU_IMAGE="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[ -n "$BUILD" ] && [ -n "$SDL3" ] && [ -n "$STAGE" ] || { usage >&2; exit 2; }
[ -f "$BUILD/partyboard" ] || { echo "Missing binary: $BUILD/partyboard" >&2; exit 1; }
[ -f "$BUILD/libdol.so" ] || { echo "Missing libdol.so" >&2; exit 1; }
[ -f "$SDL3" ] || { echo "Missing SDL3 (mali): $SDL3" >&2; exit 1; }
[ ! -e "$STAGE" ] || { echo "Stage path already exists: $STAGE" >&2; exit 1; }

mkdir -p "$STAGE/ports/partyboard" "$STAGE/ports_scripts/images"
PORT="$STAGE/ports/partyboard"
mkdir -p "$PORT/assets" "$PORT/runtime/home" "$PORT/runtime/config" "$PORT/runtime/cache"

# Binary + RELs + shared libs (RUNPATH=$ORIGIN finds them beside the binary).
cp "$BUILD/partyboard" "$PORT/partyboard"
# All GameCube REL modules + the shared DOL (every *.so the build produced).
for so in "$BUILD"/*.so; do cp "$so" "$PORT/"; done

# SDL3 (mali-fbdev driver) + bundled deps.
cp "$SDL3" "$PORT/libSDL3.so.0"
cp "$BUILD/_deps/png-build/libpng16.so.16" "$PORT/libpng16.so.16"
cp "$BUILD/_deps/zlib-build/libz.so.1" "$PORT/libz.so.1"

# Resources + launcher + controller db + metadata.
cp -a "$ROOT/res" "$PORT/res"
cp "$ROOT/packaging/nextos/PartyBoard.sh" "$PORT/PartyBoard.sh"
cp "$ROOT/packaging/nextos/PartyBoard.sh" "$STAGE/ports_scripts/PartyBoard.sh"
cp "$ROOT/packaging/nextos/gamecontrollerdb.txt" "$PORT/gamecontrollerdb.txt"
cp "$ROOT/packaging/nextos/port.json" "$PORT/port.json"
cp "$ROOT/packaging/nextos/gameinfo.xml" "$PORT/gameinfo.xml"

mkdir -p "$PORT/licenses"
cp "$ROOT/README.md" "$PORT/licenses/PARTYBOARD-README.md" 2>/dev/null || true
cp "$ROOT/extern/aurora/LICENSE" "$PORT/licenses/AURORA-LICENSE" 2>/dev/null || true

# Optional disc image (private; not in public/source distributions).
if [ -n "$ROM" ]; then
  cp "$ROM" "$PORT/assets/$(basename "$ROM")"
fi

# Menu art.
if [ -n "$MENU_IMAGE" ]; then
  cp "$MENU_IMAGE" "$STAGE/ports_scripts/images/PartyBoard.png"
elif [ -f "$PORT/res/logo.png" ]; then
  cp "$PORT/res/logo.png" "$STAGE/ports_scripts/images/PartyBoard.png"
fi

# Strip + drop build-specific RUNPATH entries (launcher sets LD_LIBRARY_PATH).
if command -v aarch64-linux-gnu-strip >/dev/null 2>&1; then
  aarch64-linux-gnu-strip --strip-unneeded "$PORT/partyboard" "$PORT/libdol.so" "$PORT/libSDL3.so.0" 2>/dev/null || true
fi
if command -v patchelf >/dev/null 2>&1; then
  patchelf --remove-rpath "$PORT/partyboard" 2>/dev/null || true
  patchelf --remove-rpath "$PORT/libdol.so" 2>/dev/null || true
fi

find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -type f -exec chmod 0644 {} +
chmod 0755 "$PORT/partyboard" "$PORT/PartyBoard.sh" "$STAGE/ports_scripts/PartyBoard.sh"

echo "Staged clean package at: $STAGE"
du -sh "$STAGE"
echo "RELs (m4xx/w0x): $(ls "$PORT"/m*.so "$PORT"/w*Dll.so 2>/dev/null | wc -l)"
