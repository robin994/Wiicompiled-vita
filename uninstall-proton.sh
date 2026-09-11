#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

# Undoes `build-proton.sh --install`. The BaseProton/ folders hold the Wine
# prefix run.sh created, so this can free several GB.

usage() {
    echo "usage: ./uninstall-proton.sh [--install-dir=PATH] [-y|--yes] [--dry-run]"
    echo "  removes what 'build-proton.sh --install' created; pass the same"
    echo "  --install-dir= if you used one at install time"
}

INSTALL_DIR="${INSTALL_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/WiiCompiled/Install}"
ASSUME_YES=0
DRY_RUN=0
for arg in "$@"; do
    case "$arg" in
        --install-dir=*) INSTALL_DIR="${arg#*=}" ;;
        -y|--yes) ASSUME_YES=1 ;;
        --dry-run) DRY_RUN=1 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unrecognized argument: $arg" >&2; usage >&2; exit 1 ;;
    esac
done

apps_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
icons_dir="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"

own_targets=(
    "$INSTALL_DIR/BaseProton"
    "$INSTALL_DIR/RetroRewindProton"
    "$apps_dir/wiicompiled-proton.desktop"
    "$apps_dir/wiicompiled-retrorewind-proton.desktop"
)
# Shared with build-linux-native.sh --install; only ours to delete if no native install is left.
shared_targets=(
    "$INSTALL_DIR/DATA"
    "$INSTALL_DIR/RetroRewind6"
    "$icons_dir/wiicompiled.png"
)

native_installed=0
if [ -e "$INSTALL_DIR/Base" ] || [ -e "$INSTALL_DIR/RetroRewind" ]; then
    native_installed=1
fi

targets=("${own_targets[@]}")
[ "$native_installed" = 0 ] && targets+=("${shared_targets[@]}")

present=()
for t in "${targets[@]}"; do
    if [ -e "$t" ]; then present+=("$t"); fi
done

if [ "${#present[@]}" -eq 0 ]; then
    echo "nothing to remove (looked under $INSTALL_DIR and $apps_dir)"
    [ "$native_installed" = 1 ] && echo "a build-linux-native.sh --install is still here; use ./uninstall-linux-native.sh for that"
    exit 0
fi

echo "will remove:"
printf '  %s\n' "${present[@]}"
[ "$native_installed" = 1 ] && echo "keeping DATA/RetroRewind6/icon - still used by a native install under $INSTALL_DIR"

if [ "$DRY_RUN" = 1 ]; then
    echo "(dry run - nothing removed)"
    exit 0
fi

if [ "$ASSUME_YES" != 1 ] && [ -t 0 ]; then
    printf 'proceed? [y/N] '
    read -r ans || ans=""
    case "$ans" in [Yy]*) ;; *) echo "aborted."; exit 0 ;; esac
fi

for t in "${present[@]}"; do
    rm -rf "$t"
    echo "removed $t"
done

if [ "$native_installed" = 0 ]; then
    rmdir "$INSTALL_DIR" 2>/dev/null && echo "removed $INSTALL_DIR" || true
    rmdir "$(dirname "$INSTALL_DIR")" 2>/dev/null && echo "removed $(dirname "$INSTALL_DIR")" || true
fi

command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$apps_dir" >/dev/null 2>&1 || true

echo "done."
