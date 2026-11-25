#!/bin/sh
# ./install.sh            copy the built mod into the game folder (adds d3d9.dll only)
# ./install.sh remove     delete the mod and its log
set -eu
cd "$(dirname "$0")"
GAME="${GAME_DIR:-/mnt/c/Program Files (x86)/Steam/steamapps/common/Alan Wake}"
MARK="alanwake collectibles mod"
[ -f "$GAME/AlanWake.exe" ] || { echo "AlanWake.exe not found in: $GAME" >&2; exit 1; }
if [ -e "$GAME/d3d9.dll" ] && ! grep -qa "$MARK" "$GAME/d3d9.dll"; then
    echo "Another d3d9.dll is already there; leaving it alone." >&2
    echo "To use both, rename it to d3d9_next.dll, then run this again." >&2
    exit 1
fi
if [ "${1:-}" = remove ]; then
    rm -f "$GAME/d3d9.dll" "$GAME/alanwake_collectibles.log" "$GAME/alanwake_collectibles.ini"
    rm -f "$GAME/alanwake_mod.log" # name used by version 0.1
    echo "removed"
    [ ! -e "$GAME/d3d9_next.dll" ] || echo "Rename d3d9_next.dll back to d3d9.dll to restore your other mod."
else
    cp build/d3d9.dll "$GAME/d3d9.dll"
    cmp build/d3d9.dll "$GAME/d3d9.dll"
    echo "installed in $GAME"
fi
