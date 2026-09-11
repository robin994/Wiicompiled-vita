#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

# Undoes `build-linux-native.sh --install`.

usage() {
    echo "usage: ./uninstall-linux-native.sh [--install-dir=PATH] [-y|--yes] [--dry-run]"
    echo "  removes what 'build-linux-native.sh --install' created; pass the same"
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
    "$INSTALL_DIR/Base"
    "$INSTALL_DIR/RetroRewind"
    "$apps_dir/wiicompiled.desktop"
    "$apps_dir/wiicompiled-retrorewind.desktop"
)
# Shared with build-proton.sh --install; only ours to delete if no Proton install is left.
shared_targets=(
    "$INSTALL_DIR/DATA"
    "$INSTALL_DIR/RetroRewind6"
    "$icons_dir/wiicompiled.png"
)

proton_installed=0
if [ -e "$INSTALL_DIR/BaseProton" ] || [ -e "$INSTALL_DIR/RetroRewindProton" ]; then
    proton_installed=1
fi

targets=("${own_targets[@]}")
[ "$proton_installed" = 0 ] && targets+=("${shared_targets[@]}")

present=()
for t in "${targets[@]}"; do
    if [ -e "$t" ]; then present+=("$t"); fi
done

if [ "${#present[@]}" -eq 0 ]; then
    echo "nothing to remove (looked under $INSTALL_DIR and $apps_dir)"
    [ "$proton_installed" = 1 ] && echo "a build-proton.sh --install is still here; use ./uninstall-proton.sh for that"
    exit 0
fi

echo "will remove:"
printf '  %s\n' "${present[@]}"
[ "$proton_installed" = 1 ] && echo "keeping DATA/RetroRewind6/icon - still used by a Proton install under $INSTALL_DIR"

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

if [ "$proton_installed" = 0 ]; then
    rmdir "$INSTALL_DIR" 2>/dev/null && echo "removed $INSTALL_DIR" || true
    rmdir "$(dirname "$INSTALL_DIR")" 2>/dev/null && echo "removed $(dirname "$INSTALL_DIR")" || true
fi

command -v update-desktop-database >/dev/null 2>&1 && \
    update-desktop-database "$apps_dir" >/dev/null 2>&1 || true

echo "done."
