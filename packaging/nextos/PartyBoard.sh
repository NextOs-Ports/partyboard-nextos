#!/bin/bash
# PartyBoard — NextOS / Mali-450 GLES2 launcher (native Mario Party 4 port)
# Mirrors the approved Dusklight recipe: SDL3 "mali" fbdev driver, ALSA audio env,
# runtime under the port dir, foreground launch (no setsid/nohup/watchdog).

PORTNAME="PartyBoard"

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

[ -f "$controlfolder/control.txt" ] && source "$controlfolder/control.txt"
[ -n "${CFW_NAME:-}" ] && [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
command -v get_controls >/dev/null 2>&1 && get_controls

if [ -n "${directory:-}" ]; then
  ROMSROOT="/$directory"
else
  ROMSROOT="/storage/roms"
fi

GAMEDIR="${PARTYBOARD_GAMEDIR:-$ROMSROOT/ports/partyboard}"
RUNTIME_DIR="$GAMEDIR/runtime"
ASSETS_DIR="$GAMEDIR/assets"
BIN="$GAMEDIR/partyboard"
SDL3_LIB="$GAMEDIR/libSDL3.so.0"
LOGFILE="$GAMEDIR/log.txt"

mkdir -p "$RUNTIME_DIR/home" "$RUNTIME_DIR/config" "$RUNTIME_DIR/cache" "$ASSETS_DIR"
cd "$GAMEDIR" || exit 1
ulimit -c 0

[ ! -s "$LOGFILE" ] || mv -f "$LOGFILE" "$GAMEDIR/log.prev.txt"
: >"$LOGFILE"
exec >>"$LOGFILE" 2>&1

cleanup() { command -v pm_finish >/dev/null 2>&1 && pm_finish; }
trap cleanup EXIT INT TERM

need() { [ -e "$1" ] || { echo "[missing] $1"; return 1; }; }
need "$BIN" || exit 1
need "$SDL3_LIB" || exit 1

# Locate the GameCube disc (GMPE01 Rev.0/Rev.1). Auto-pick the first image.
DVD_PATH="${PARTYBOARD_DISC:-}"
if [ -z "$DVD_PATH" ]; then
  for c in "$ASSETS_DIR"/*.rvz "$ASSETS_DIR"/*.RVZ "$ASSETS_DIR"/*.iso "$ASSETS_DIR"/*.ISO "$ASSETS_DIR"/*.gcm "$ASSETS_DIR"/*.GCM; do
    [ -f "$c" ] && { DVD_PATH="$c"; break; }
  done
fi
if [ -z "$DVD_PATH" ]; then
  echo "[missing] Place a supported Mario Party 4 (GMPE01) .rvz/.iso/.gcm in $ASSETS_DIR"
  exit 1
fi

# Seed config.json so PartyBoard boots straight into the game (skip disc-select UI).
export HOME="$RUNTIME_DIR/home"
export XDG_DATA_HOME="$RUNTIME_DIR"
export XDG_CONFIG_HOME="$RUNTIME_DIR/config"
export XDG_CACHE_HOME="$RUNTIME_DIR/cache"
# SDL_GetPrefPath("MarioPartyRD","Party Board") -> $XDG_DATA_HOME/MarioPartyRD/Party Board/
CFG="$RUNTIME_DIR/MarioPartyRD/Party Board/config.json"
mkdir -p "$(dirname "$CFG")"
cat >"$CFG" <<EOF
{
    "backend.isoPath": "$DVD_PATH",
    "backend.skipPreLaunchUI": true,
    "backend.isoVerification": 1,
    "video.enableFullscreen": true,
    "video.enableVsync": false
}
EOF

export LD_LIBRARY_PATH="$GAMEDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_PRELOAD="$SDL3_LIB${LD_PRELOAD:+:$LD_PRELOAD}"

# NextOS Mali-450 fbdev: the bundled SDL3 carries the mali video backend.
export SDL_VIDEODRIVER=mali
export SDL_AUDIODRIVER=alsa
export SDL_AUDIO_ALSA_DEFAULT_PLAYBACK_DEVICE="${SDL_AUDIO_ALSA_DEFAULT_PLAYBACK_DEVICE:-plughw:0,0}"
unset ALSA_CONFIG_PATH

# Gamepad mapping: prefer PortMaster's, then the bundled Twin USB (PS2 adapter,
# 0810:0001) default so Start/face buttons map before the user configures it.
if [ -n "${sdl_controllerconfig:-}" ]; then
  export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
elif [ -z "${SDL_GAMECONTROLLERCONFIG:-}" ]; then
  export SDL_GAMECONTROLLERCONFIG_FILE="$GAMEDIR/gamecontrollerdb.txt"
fi

echo "[partyboard] NextOS Mali-450 GLES2"
echo "[partyboard] disc=$(basename "$DVD_PATH")"
chmod +x "$BIN" 2>/dev/null || true
command -v pm_platform_helper >/dev/null 2>&1 && pm_platform_helper "$BIN"

"$BIN"
exit $?
