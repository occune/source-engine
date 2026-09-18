#!/bin/bash
# Install the full nillerusr/source-engine build into the Source SDK Base 2013 MP
# install, replacing Valve's binaries with the fork's so the whole stack is
# internally consistent (required because the fork's .so files have different
# ABI/symbols than Valve's — e.g. libtogl.so expects g_Telemetry from libtier0.so).
#
# Creates .orig backups of every file it replaces (only the first time).
# Re-running is idempotent: it will not overwrite existing .orig files.
#
# Usage:  bash install-fork-into-sdk.sh
#         bash install-fork-into-sdk.sh --revert   (restore all .orig files)

set -euo pipefail

SDK="/home/zoothorns/.local/share/Steam/steamapps/common/Source SDK Base 2013 Multiplayer"
BUILD="/tmp/opencode/source-engine/build"
DST="$SDK/bin/linux64"

if [[ "${1:-}" == "--revert" ]]; then
    echo "Reverting all .orig files in $DST and $SDK/hl2/bin/linux64/ ..."
    for f in "$DST"/*.orig "$SDK/hl2/bin/linux64"/*.orig "$SDK"/hl2_linux64.orig; do
        [[ -f "$f" ]] || continue
        orig="${f%.orig}"
        cp -p "$f" "$orig"
        echo "  restored $orig"
    done
    echo "Done. (.orig backups left in place; delete them to free space.)"
    exit 0
fi

if [[ ! -d "$SDK" ]]; then
    echo "SDK not found at $SDK" >&2; exit 1
fi
if [[ ! -f "$BUILD/engine/libengine.so" ]]; then
    echo "Fork build not found at $BUILD/engine/libengine.so" >&2; exit 1
fi

echo "Installing fork binaries into $DST (64-bit)"

# Files in bin/linux64/ that use the lib prefix in the SDK layout.
# Everything else uses the bare name (no lib prefix).
# Format: "build/relative/path.so:destination_name.so"
declare -a FILES=(
    # Core libs (SDK uses lib prefix for these)
    "tier0/libtier0.so:libtier0.so"
    "vstdlib/libvstdlib.so:libvstdlib.so"
    "togl/libtogl.so:libtogl.so"
    "stub_steam/libsteam_api.so:libsteam_api.so"
    # Modules (SDK uses bare names)
    "engine/libengine.so:engine.so"
    "launcher/liblauncher.so:launcher.so"
    "datacache/libdatacache.so:datacache.so"
    "filesystem/libfilesystem_stdio.so:filesystem_stdio.so"
    "inputsystem/libinputsystem.so:inputsystem.so"
    "materialsystem/libmaterialsystem.so:materialsystem.so"
    "materialsystem/shaderapidx9/libshaderapidx9.so:shaderapidx9.so"
    "materialsystem/stdshaders/libstdshader_dx9.so:stdshader_dx9.so"
    "scenefilecache/libscenefilecache.so:scenefilecache.so"
    "soundemittersystem/libsoundemittersystem.so:soundemittersystem.so"
    "studiorender/libstudiorender.so:studiorender.so"
    "vgui2/src/libvgui2.so:vgui2.so"
    "vguimatsurface/libvguimatsurface.so:vguimatsurface.so"
    "video/libvideo_services.so:video_services.so"
    "vphysics/libvphysics.so:vphysics.so"
    "gameui/libGameUI.so:GameUI.so"
    "serverbrowser/libServerBrowser.so:ServerBrowser.so"
    "engine/voice_codecs/minimp3/libvaudio_minimp3.so:vaudio_minimp3.so"
    "utils/vtex/libvtex_dll.so:vtex_dll.so"
)

installed=0
missing=0
for pair in "${FILES[@]}"; do
    src="${pair%%:*}"
    dst="${pair##*:}"
    srcpath="$BUILD/$src"
    dstpath="$DST/$dst"
    if [[ ! -f "$srcpath" ]]; then
        echo "  SKIP (not built): $src"
        missing=$((missing+1))
        continue
    fi
    # Back up the original (only if no backup exists yet and target exists)
    if [[ ! -f "$dstpath.orig" && -f "$dstpath" ]]; then
        cp -p "$dstpath" "$dstpath.orig"
    fi
    cp -p "$srcpath" "$dstpath"
    installed=$((installed+1))
done
echo "  installed $installed files, skipped $missing"

# Launcher executable
if [[ -f "$BUILD/launcher_main/hl2_launcher" ]]; then
    if [[ ! -f "$SDK/hl2_linux64.orig" ]]; then
        cp -p "$SDK/hl2_linux64" "$SDK/hl2_linux64.orig"
    fi
    cp -p "$BUILD/launcher_main/hl2_launcher" "$SDK/hl2_linux64"
    echo "  installed hl2_launcher -> hl2_linux64"
fi

# client.so / server.so for hl2/
mkdir -p "$SDK/hl2/bin/linux64"
for pair in "game/client/libclient.so:client.so" "game/server/libserver.so:server.so"; do
    src="${pair%%:*}"
    dst="${pair##*:}"
    srcpath="$BUILD/$src"
    dstpath="$SDK/hl2/bin/linux64/$dst"
    if [[ ! -f "$srcpath" ]]; then continue; fi
    if [[ ! -f "$dstpath.orig" && -f "$dstpath" ]]; then
        cp -p "$dstpath" "$dstpath.orig"
    fi
    cp -p "$srcpath" "$dstpath"
    echo "  installed $src -> hl2/bin/linux64/$dst"
done

echo
echo "Done. To launch (from inside the SDK dir, Valve's hl2.sh has a CWD bug):"
echo "  cd \"$SDK\" && ./hl2.sh -game ipv6test -window -novid +map background01 +sv_lan 1 +ip 127.0.0.1 +hostport 27015"
echo
echo "Rollback: bash $0 --revert"
