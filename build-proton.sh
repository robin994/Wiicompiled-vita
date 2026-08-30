#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

# Builds the Windows executable exactly like build.sh (llvm-mingw), then runs
# it under Proton - for testing the Windows codepath on Linux without a
# Windows machine, alongside (not instead of) build-linux-native.sh's native
# ELF build. Needs a Proton install already on the system (Steam, or a
# standalone GE-Proton) - see find_proton() below.

# --package: zip a movable copy into dist/, with a run.sh Proton launcher
# alongside the Windows binaries. Needs a Proton install already on the
# target machine - Proton itself and Wine's shared libs aren't bundled.
PACKAGE="${PACKAGE:-0}"

# --appimage: build a self-contained .AppImage into dist/, using linuxdeploy
# (downloaded into .toolchain/ the first time). Bundles the Windows binaries
# and game data, but still needs a Proton install on the host - an AppImage
# can't sensibly bundle Proton itself (gigabytes, tied to Steam's updates).
APPIMAGE="${APPIMAGE:-0}"

# --retro: also build Retro Rewind. See build.sh for RETRO_ROOT.
RETRO="${RETRO:-0}"
RETRO_SKIP_WFC="${RETRO_SKIP_WFC:-0}"

# --install: copy each build into its own folder under $INSTALL_DIR (default
# ~/.local/share/WiiCompiled/Install) - BaseProton/, and RetroRewindProton/
# if --retro is set. Shares its disc data and RetroRewind6 copy with
# build-linux-native.sh's --install under the same $INSTALL_DIR. Also adds
# menu launchers, named "(Proton)" to tell them apart from the native ones.
# Use --install-dir=PATH to change the location.
INSTALL="${INSTALL:-0}"
INSTALL_DIR="${INSTALL_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/WiiCompiled/Install}"

# --no-run: skip launching under Proton after a plain dev build. Only
# applies to the dev build (no --install/--package/--appimage) - that one
# launches immediately by default, matching a normal build + double-click.
RUN="${RUN:-1}"

# --run-retro: launch RetroRewind.exe instead of WiiCompiled.exe for the dev
# build's immediate run. Implies --retro.
RUN_RETRO="${RUN_RETRO:-0}"

# --proton-dir=PATH (or PROTON_DIR): use a specific Proton install (a
# directory containing a `proton` script) - a Steam Proton under
# steamapps/common, or a GE-Proton under compatibilitytools.d. Auto-detected
# from the usual Steam locations when unset.
PROTON_DIR="${PROTON_DIR:-}"

# --prefix=PATH (or PROTON_PREFIX): the Wine prefix Proton runs in
# (STEAM_COMPAT_DATA_PATH) for the dev build's immediate run. Defaults to a
# dedicated dev prefix under .toolchain/, separate from any real Steam
# game's prefix. Installed/packaged builds get their own prefix instead, so
# this only affects the dev build here.
PROTON_PREFIX="${PROTON_PREFIX:-}"

# -i / --interactive prompts for the options below instead of taking flags.
# Also the default with no arguments on an interactive terminal.
INTERACTIVE="${INTERACTIVE:-0}"
for arg in "$@"; do
    case "$arg" in
        --retro) RETRO=1 ;;
        --retro-skip-wfc) RETRO=1; RETRO_SKIP_WFC=1 ;;
        --package) PACKAGE=1 ;;
        --appimage) APPIMAGE=1 ;;
        --install) INSTALL=1 ;;
        --install-dir=*) INSTALL=1; INSTALL_DIR="${arg#*=}" ;;
        --no-run) RUN=0 ;;
        --run-retro) RETRO=1; RUN_RETRO=1 ;;
        --proton-dir=*) PROTON_DIR="${arg#*=}" ;;
        --prefix=*) PROTON_PREFIX="${arg#*=}" ;;
        -i|--interactive) INTERACTIVE=1 ;;
    esac
done

# $1 question, $2 default (y|n). Returns 0 for yes.
prompt_yes_no() {
    local q="$1" def="${2:-n}" ans hint
    case "$def" in [Yy]*) hint="[Y/n]" ;; *) hint="[y/N]" ;; esac
    printf '%s %s ' "$q" "$hint" >&2
    read -r ans || ans=""
    case "${ans:-$def}" in [Yy]*) return 0 ;; *) return 1 ;; esac
}

run_interactive() {
    echo "WiiCompiled Windows-build-under-Proton setup" >&2
    echo "(pass flags to skip: --retro --install --package --appimage --no-run)" >&2
    echo >&2

    if prompt_yes_no "Build Retro Rewind as well?" "$([ "$RETRO" = 1 ] && echo y || echo n)"; then
        RETRO=1
    else
        RETRO=0
        RUN_RETRO=0
    fi
    echo >&2

    local default_choice=1
    [ "$INSTALL" = 1 ] && default_choice=2
    [ "$PACKAGE" = 1 ] && default_choice=3
    [ "$APPIMAGE" = 1 ] && default_choice=4
    {
        echo "Output:"
        echo "  1) dev      - build in ./$BUILD_DIR and run it under Proton there"
        echo "  2) install  - tidy folders under $INSTALL_DIR, plus a menu entry / icon (Proton)"
        echo "  3) package  - a movable .zip in ./dist with a Proton run.sh (disc data bundled)"
        echo "  4) appimage - a self-contained .AppImage in ./dist (still needs Proton on the host)"
        printf 'Choose [1-4] (%s): ' "$default_choice"
    } >&2
    local choice
    read -r choice || choice=""
    PACKAGE=0; APPIMAGE=0; INSTALL=0
    case "${choice:-$default_choice}" in
        1) : ;;
        2) INSTALL=1 ;;
        3) PACKAGE=1 ;;
        4) APPIMAGE=1 ;;
        *) echo "unrecognized choice '$choice' - using dev" >&2 ;;
    esac
    echo >&2

    if [ "$INSTALL" = 0 ] && [ "$PACKAGE" = 0 ] && [ "$APPIMAGE" = 0 ]; then
        if prompt_yes_no "Run it under Proton after building?" "$([ "$RUN" = 1 ] && echo y || echo n)"; then
            RUN=1
            if [ "$RETRO" = 1 ]; then
                if prompt_yes_no "Run the Retro Rewind build (instead of the base game)?" "$([ "$RUN_RETRO" = 1 ] && echo y || echo n)"; then
                    RUN_RETRO=1
                else
                    RUN_RETRO=0
                fi
            fi
        else
            RUN=0
        fi
        echo >&2
    fi

    local summary="dev build in $BUILD_DIR"
    [ "$INSTALL" = 1 ] && summary="install to $INSTALL_DIR (with menu entry)"
    [ "$PACKAGE" = 1 ] && summary="package ./dist/WiiCompiled-proton.zip"
    [ "$APPIMAGE" = 1 ] && summary="AppImage(s) in ./dist"
    [ "$RETRO" = 1 ] && summary="$summary + Retro Rewind"
    if [ "$INSTALL" = 0 ] && [ "$PACKAGE" = 0 ] && [ "$APPIMAGE" = 0 ] && [ "$RUN" = 1 ]; then
        summary="$summary, then run $([ "$RUN_RETRO" = 1 ] && echo RetroRewind.exe || echo WiiCompiled.exe) under Proton"
    fi
    echo "==> $summary" >&2
    prompt_yes_no "Proceed?" y || { echo "aborted." >&2; exit 0; }
    echo >&2
}

BUILD_DIR="${BUILD_DIR:-native-build}"

if [ "$INTERACTIVE" = 1 ] || { [ "$#" -eq 0 ] && [ -t 0 ] && [ -t 1 ]; }; then
    run_interactive
fi
RETRO_ROOT="${RETRO_ROOT:-$(pwd)/PulsarPacks/completed/RetroRewind/RetroRewind6}"
export BUILD_DIR

# Rough check so a build fails fast instead of running out of space partway
# through. Override with REQUIRED_FREE_GB / REQUIRED_INSTALL_FREE_GB, or set
# SKIP_DISK_CHECK=1 to skip.
SKIP_DISK_CHECK="${SKIP_DISK_CHECK:-0}"
if [ "$SKIP_DISK_CHECK" != "1" ]; then
    build_required_gb=12
    { [ "$RETRO" = "1" ] || [ "$PACKAGE" = "1" ] || [ "$APPIMAGE" = "1" ]; } && build_required_gb=20
    install_required_gb=5
    [ "$RETRO" = "1" ] && install_required_gb=8

    # $1 path, $2 required GB.
    check_free_space() {
        local path="$1" need_gb="$2" avail_gb
        while [ ! -d "$path" ]; do path="$(dirname "$path")"; done
        avail_gb=$(($(df -Pk "$path" | awk 'NR==2 {print $4}') / 1024 / 1024))
        if [ "$avail_gb" -lt "$need_gb" ]; then
            echo "error: only ${avail_gb}GB free at $path (need ~${need_gb}GB); set REQUIRED_FREE_GB/REQUIRED_INSTALL_FREE_GB or SKIP_DISK_CHECK=1" >&2
            exit 1
        fi
    }

    check_free_space "$(pwd)" "${REQUIRED_FREE_GB:-$build_required_gb}"
    [ "$INSTALL" = "1" ] && check_free_space "$INSTALL_DIR" "${REQUIRED_INSTALL_FREE_GB:-$install_required_gb}"
fi

# Check for dotnet (the translator targets net8.0)
if ! command -v dotnet >/dev/null 2>&1; then
    echo "error: dotnet not found on PATH; install the .NET 8 SDK (e.g. the 'dotnet-sdk-8.0'" >&2
    echo "package on Debian/Ubuntu/Fedora, 'dotnet-sdk' on Arch, or https://dotnet.microsoft.com/download/dotnet/8.0)" >&2
    exit 1
fi
if ! dotnet --list-sdks 2>/dev/null | grep -qE '^([8-9]|[1-9][0-9])\.'; then
    echo "error: no .NET 8+ SDK found (the translator targets net8.0); installed SDKs:" >&2
    dotnet --list-sdks 2>&1 | sed 's/^/  /' >&2
    echo "install the .NET 8 SDK (e.g. the 'dotnet-sdk-8.0' package on Debian/Ubuntu/Fedora," >&2
    echo "'dotnet-sdk' on Arch, or https://dotnet.microsoft.com/download/dotnet/8.0)" >&2
    exit 1
fi

build_args=()
[ "$RETRO" = "1" ] && build_args+=(--retro)
[ "$RETRO_SKIP_WFC" = "1" ] && build_args+=(--retro-skip-wfc)

echo "==> building the Windows executable (./build.sh ${build_args[*]:-})"
./build.sh "${build_args[@]}"

CONFIG_FILE="$BUILD_DIR/UserData/Config.toml"

# --package/--appimage bundle disc data, same as build-linux-native.sh.
# Extract it if build.sh's own run didn't already need to.
have_extracted_data() {
    [ -d "extracted/DATA/sys" ] && [ -d "extracted/DATA/files" ]
}

if { [ "$PACKAGE" = "1" ] || [ "$APPIMAGE" = "1" ]; } && ! have_extracted_data; then
    NODTOOL_VERSION="${NODTOOL_VERSION:-v2.0.0-alpha.10}"
    NODTOOL_DIR="${NODTOOL_DIR:-$(pwd)/.toolchain/nodtool}"

    ensure_nodtool() {
        if [ -n "${NODTOOL:-}" ] && [ -x "${NODTOOL:-}" ]; then return 0; fi
        if command -v nodtool >/dev/null 2>&1; then NODTOOL="$(command -v nodtool)"; return 0; fi
        local asset
        case "$(uname -m)" in
            x86_64|amd64)  asset="nodtool-linux-x86_64" ;;
            aarch64|arm64) asset="nodtool-linux-aarch64" ;;
            i686|i386|x86) asset="nodtool-linux-i686" ;;
            *) echo "error: no prebuilt nodtool for $(uname -m); install nodtool and set NODTOOL" >&2; return 1 ;;
        esac
        NODTOOL="$NODTOOL_DIR/$asset"
        if [ ! -x "$NODTOOL" ]; then
            echo "==> fetching nodtool $NODTOOL_VERSION into $NODTOOL_DIR (first disc extract only)" >&2
            mkdir -p "$NODTOOL_DIR"
            if ! curl -fL -o "$NODTOOL.tmp" \
                "https://github.com/encounter/nod/releases/download/$NODTOOL_VERSION/$asset"; then
                rm -f "$NODTOOL.tmp"; echo "error: could not download nodtool" >&2; return 1
            fi
            chmod +x "$NODTOOL.tmp"
            mv -f "$NODTOOL.tmp" "$NODTOOL"
        fi
    }

    if [ -z "${GAME_IMAGE:-}" ]; then
        shopt -s nullglob nocaseglob
        candidates=(*.wbfs *.iso *.gcm *.gcz *.ciso *.wia *.rvz)
        shopt -u nullglob nocaseglob
        if [ "${#candidates[@]}" -eq 1 ]; then
            GAME_IMAGE="${candidates[0]}"
        elif [ "${#candidates[@]}" -gt 1 ]; then
            echo "error: multiple disc images found at the repo root; set GAME_IMAGE=path/to/image" >&2
            printf '  - %s\n' "${candidates[@]}" >&2
            exit 1
        fi
    fi

    if [ -n "${GAME_IMAGE:-}" ] && ensure_nodtool; then
        echo "==> extracting $GAME_IMAGE (needed for --package/--appimage's bundled disc data)"
        rm -rf extracted
        "$NODTOOL" extract "$GAME_IMAGE" extracted/DATA -q
    fi

    if ! have_extracted_data; then
        echo "error: --package/--appimage need extracted/DATA, which isn't there yet." >&2
        echo "  place a clean PAL RMCP01 disc image (ISO/WBFS/GCZ/CISO/WIA/RVZ) at the repo root" >&2
        echo "  and re-run (nodtool is fetched automatically), or extract it yourself:" >&2
        echo "  nodtool extract your-game.iso ./extracted/DATA" >&2
        exit 1
    fi
fi

# Writes a standalone run.sh that finds Proton and launches $exe_name under
# it. The heredoc is quoted (no interpolation), so $HOME/$PROTON_DIR stay
# literal here and only resolve later, when the script actually runs.
write_proton_launcher() {
    local dest_dir="$1" script_name="$2" exe_name="$3"
    cat > "$dest_dir/$script_name" <<'EOF_RUN'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

find_proton() {
    if [ -n "${PROTON_DIR:-}" ] && [ -x "$PROTON_DIR/proton" ]; then return 0; fi
    local search_dirs=(
        "$HOME/.steam/steam/steamapps/common"
        "$HOME/.steam/root/steamapps/common"
        "$HOME/.local/share/Steam/steamapps/common"
        "$HOME/.steam/steam/compatibilitytools.d"
        "$HOME/.steam/root/compatibilitytools.d"
        "$HOME/.local/share/Steam/compatibilitytools.d"
    )
    local candidates=() d p
    for d in "${search_dirs[@]}"; do
        [ -d "$d" ] || continue
        for p in "$d"/*/; do
            [ -x "${p}proton" ] && candidates+=("${p%/}")
        done
    done
    [ "${#candidates[@]}" -gt 0 ] || return 1
    PROTON_DIR="$(ls -td "${candidates[@]}" | head -n1)"
}

if ! find_proton; then
    echo "error: could not find a Proton install." >&2
    echo "  Install one through Steam (or a GE-Proton build into compatibilitytools.d)," >&2
    echo "  or set PROTON_DIR=/path/to/Proton (a directory with a 'proton' script)." >&2
    exit 1
fi

STEAM_ROOT=""
for d in "$HOME/.steam/steam" "$HOME/.steam/root" "$HOME/.local/share/Steam"; do
    [ -d "$d" ] && { STEAM_ROOT="$d"; break; }
done

PROTON_PREFIX="${PROTON_PREFIX:-$HERE/proton-prefix}"
mkdir -p "$PROTON_PREFIX"

echo "==> running @@EXE@@ under Proton ($PROTON_DIR)"
STEAM_COMPAT_DATA_PATH="$PROTON_PREFIX" \
STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_ROOT" \
"$PROTON_DIR/proton" run "$HERE/@@EXE@@" "$@"
EOF_RUN
    sed -i "s/@@EXE@@/$exe_name/g" "$dest_dir/$script_name"
    chmod +x "$dest_dir/$script_name"
}

if [ "$INSTALL" = "1" ]; then
    echo ""
    echo "==> installing tidy per-product Proton folders under $INSTALL_DIR"

    # Shares $INSTALL_DIR with build-linux-native.sh's --install, but copies
    # its own fresh copy so this still works standalone.
    dvd_abs=""
    if [ -d "extracted/DATA/sys" ] && [ -d "extracted/DATA/files" ]; then
        echo "    copying disc data -> $INSTALL_DIR/DATA"
        rm -rf "$INSTALL_DIR/DATA"
        mkdir -p "$INSTALL_DIR"
        cp -r "extracted/DATA" "$INSTALL_DIR/DATA"
        dvd_abs="$INSTALL_DIR/DATA"
    else
        echo "    note: extracted/DATA not found - leaving dvd_root commented in the installed config" >&2
    fi

    retro_abs=""
    if [ "$RETRO" = "1" ] && [ -d "$RETRO_ROOT" ]; then
        echo "    copying RetroRewind6 ($(du -sh "$RETRO_ROOT" 2>/dev/null | cut -f1)) -> $INSTALL_DIR/RetroRewind6"
        rm -rf "$INSTALL_DIR/RetroRewind6"
        mkdir -p "$INSTALL_DIR"
        cp -r "$RETRO_ROOT" "$INSTALL_DIR/RetroRewind6"
        retro_abs="$INSTALL_DIR/RetroRewind6"
    fi

    # $1 product folder, $2 binary name, $3 = 1 when this product is Retro Rewind
    install_product() {
        local name="$1" binary="$2" is_retro="$3"
        local dest="$INSTALL_DIR/$name"
        rm -rf "$dest"
        mkdir -p "$dest/UserData"
        cp -f "$BUILD_DIR"/*.dll "$BUILD_DIR/$binary" "$BUILD_DIR/dsp_coef.bin" \
              "$BUILD_DIR/initial_pipeline_cache.db" "$dest/"
        cp -r "$BUILD_DIR/wii_bootstrap" "$dest/wii_bootstrap"
        touch "$dest/portable.txt"
        sed "/^\[paths\]/,\$d" "$CONFIG_FILE" > "$dest/UserData/Config.toml"
        {
            echo "[paths]"
            if [ -n "$dvd_abs" ]; then
                echo "dvd_root = \"$dvd_abs\""
            else
                echo "# dvd_root = \"D:/MarioKartWii/DATA\""
            fi
            if [ "$is_retro" = "1" ] && [ -n "$retro_abs" ]; then
                echo "retro_rewind_root = \"$retro_abs\""
            fi
            echo "# nand_root = \"D:/WiiNand\""
        } >> "$dest/UserData/Config.toml"
        write_proton_launcher "$dest" run.sh "$binary"
        echo "    $dest/run.sh"
    }

    install_product BaseProton WiiCompiled.exe 0
    if [ "$RETRO" = "1" ]; then
        install_product RetroRewindProton RetroRewind.exe 1
    fi

    # Menu launcher(s) + icon. Named "(Proton)", with distinct .desktop
    # filenames, so these sit next to build-linux-native.sh's without
    # overwriting them.
    apps_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
    icons_dir="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"
    mkdir -p "$apps_dir" "$icons_dir"
    icon_name=""
    if [ -f runtime/assets/appimage/wiicompiled.png ]; then
        cp -f runtime/assets/appimage/wiicompiled.png "$icons_dir/wiicompiled.png"
        icon_name="wiicompiled"
    fi
    # $1 .desktop basename, $2 Name=, $3 executable path
    write_desktop() {
        local file="$apps_dir/$1.desktop"
        {
            echo "[Desktop Entry]"
            echo "Type=Application"
            echo "Name=$2"
            echo "GenericName=Mario Kart Wii PC Recompiled"
            echo "Comment=Statically recompiled Mario Kart Wii (Windows build via Proton)"
            echo "Exec=\"$3\""
            [ -n "$icon_name" ] && echo "Icon=$icon_name"
            echo "Categories=Game;"
            echo "Terminal=false"
        } > "$file"
        echo "    $file"
    }
    write_desktop wiicompiled-proton "WiiCompiled (Proton)" "$INSTALL_DIR/BaseProton/run.sh"
    if [ "$RETRO" = "1" ]; then
        write_desktop wiicompiled-retrorewind-proton "WiiCompiled Retro Rewind (Proton)" \
            "$INSTALL_DIR/RetroRewindProton/run.sh"
    fi
    command -v update-desktop-database >/dev/null 2>&1 && \
        update-desktop-database "$apps_dir" >/dev/null 2>&1 || true
fi

if [ "$PACKAGE" = "1" ] || [ "$APPIMAGE" = "1" ]; then
    # Stage a movable copy once. --package zips this directly; --appimage
    # copies it into an AppDir below.
    STAGE_DIR="dist/.stage-WiiCompiled-proton"
    echo ""
    echo "==> staging a movable Proton copy at $STAGE_DIR (this could take a while)"
    rm -rf "$STAGE_DIR"
    mkdir -p "$STAGE_DIR/UserData"
    cp -f "$BUILD_DIR"/*.exe "$BUILD_DIR"/*.dll "$BUILD_DIR/dsp_coef.bin" "$BUILD_DIR/initial_pipeline_cache.db" "$STAGE_DIR/"
    cp -r "$BUILD_DIR/wii_bootstrap" "$STAGE_DIR/wii_bootstrap"
    touch "$STAGE_DIR/portable.txt"
    cp -r "extracted/DATA" "$STAGE_DIR/DATA"

    stage_paths=('dvd_root = "../DATA"')
    if [ "$RETRO" = "1" ]; then
        echo "==> copying RetroRewind6 ($(du -sh "$RETRO_ROOT" | cut -f1))"
        cp -r "$RETRO_ROOT" "$STAGE_DIR/RetroRewind6"
        stage_paths+=('retro_rewind_root = "../RetroRewind6"')
    fi
    sed "/^\[paths\]/,\$d" "$CONFIG_FILE" > "$STAGE_DIR/UserData/Config.toml"
    { echo "[paths]"; printf '%s\n' "${stage_paths[@]}"; } >> "$STAGE_DIR/UserData/Config.toml"

    write_proton_launcher "$STAGE_DIR" run.sh WiiCompiled.exe
    if [ "$RETRO" = "1" ]; then
        write_proton_launcher "$STAGE_DIR" run-retrorewind.sh RetroRewind.exe
    fi

    if [ "$PACKAGE" = "1" ]; then
        echo "==> zipping $STAGE_DIR"
        rm -rf "dist/WiiCompiled-proton"
        cp -r "$STAGE_DIR" "dist/WiiCompiled-proton"
        rm -f "dist/WiiCompiled-proton.zip"
        (cd dist && zip -rq -1 "WiiCompiled-proton.zip" "WiiCompiled-proton")
        rm -rf "dist/WiiCompiled-proton"
        echo "==> packaged: dist/WiiCompiled-proton.zip ($(du -sh dist/WiiCompiled-proton.zip | cut -f1))"
    fi

    if [ "$APPIMAGE" = "1" ]; then
        echo ""
        echo "==> building AppImage(s)"

        # Just linuxdeploy's own tooling here - the Windows binaries aren't
        # ELF, so there are no shared libs of theirs to bundle. Downloaded
        # once into .toolchain/, same as the other build scripts' toolchains.
        LINUXDEPLOY_DIR="$(pwd)/.toolchain/linuxdeploy"
        LINUXDEPLOY="$LINUXDEPLOY_DIR/linuxdeploy-x86_64.AppImage"
        LINUXDEPLOY_PLUGIN="$LINUXDEPLOY_DIR/linuxdeploy-plugin-appimage-x86_64.AppImage"
        if [ ! -x "$LINUXDEPLOY" ] || [ ! -x "$LINUXDEPLOY_PLUGIN" ]; then
            echo "==> fetching linuxdeploy into $LINUXDEPLOY_DIR (first --appimage build only)"
            mkdir -p "$LINUXDEPLOY_DIR"
            curl -L -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
            curl -L -o "$LINUXDEPLOY_PLUGIN" "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-x86_64.AppImage"
            chmod +x "$LINUXDEPLOY" "$LINUXDEPLOY_PLUGIN"
        fi

        ICON_FILE="runtime/assets/appimage/wiicompiled.png"
        if [ ! -f "$ICON_FILE" ]; then
            echo "error: --appimage needs an icon at $ICON_FILE (any size) - place one and re-run" >&2
            exit 1
        fi

        export PATH="$LINUXDEPLOY_DIR:$PATH"
        export APPIMAGE_EXTRACT_AND_RUN=1
        mkdir -p dist

        # linuxdeploy needs an ELF executable to anchor the AppImage on; the
        # real game is a bundled Windows binary launched from AppRun, so
        # /bin/true stands in as a do-nothing placeholder. (Not `command -v
        # true` - that can resolve to the shell builtin, not a real file.)
        LAUNCHER_STUB=""
        for candidate in /usr/bin/true /bin/true; do
            if [ -x "$candidate" ]; then
                LAUNCHER_STUB="$candidate"
                break
            fi
        done
        if [ -z "$LAUNCHER_STUB" ]; then
            echo "error: --appimage needs a placeholder executable (looked for /usr/bin/true, /bin/true)" >&2
            exit 1
        fi

        # Each AppImage gets its own fresh AppDir. Sharing one AppDir between
        # builds made linuxdeploy's appimage plugin overwrite the first
        # build's output.
        #
        # $1=output name, $2=Windows binary to run, $3=desktop Name=.
        build_appimage() {
            local out_name="$1" binary="$2" human_name="$3"
            local appdir="dist/.stage-AppDir-$out_name"
            rm -rf "$appdir"
            mkdir -p "$appdir/usr/bin"
            cp -r "$STAGE_DIR/." "$appdir/usr/bin/"
            # Drop portable.txt: it would put UserData/ inside the read-only
            # squashfs mount.
            rm -f "$appdir/usr/bin/portable.txt" "$appdir/usr/bin/run.sh" "$appdir/usr/bin/run-retrorewind.sh"
            cp "$LAUNCHER_STUB" "$appdir/usr/bin/$out_name-stub"

            # Without portable.txt, the exe falls back to its normal Windows
            # config location - SHGetKnownFolderPath(FOLDERID_LocalAppData),
            # which under Wine/Proton resolves inside the Proton prefix, not
            # $XDG_DATA_HOME. Unlike the native AppImage (whose non-portable
            # default genuinely is $XDG_DATA_HOME, so it can just write
            # Config.toml there), this has to pre-seed Config.toml at
            # Proton's own prefix-local AppData path before every launch.
            # That relies on "steamuser" being the prefix's default Windows
            # username - a stable Proton/Steam Runtime convention (also
            # relied on by Lutris, ProtonUp, etc.), not a documented
            # guarantee. Proton itself is never bundled - this searches the
            # host for one, same as build-proton.sh's own run.sh, in POSIX sh.
            cat > "$appdir/AppRun" <<EOF_APPRUN
#!/bin/sh
set -eu
HERE="\$(CDPATH= cd -- "\$(dirname -- "\$0")" && pwd)"
DATA_HOME="\${XDG_DATA_HOME:-\$HOME/.local/share}/WiiCompiled-proton"
mkdir -p "\$DATA_HOME"

find_proton() {
    if [ -n "\${PROTON_DIR:-}" ] && [ -x "\$PROTON_DIR/proton" ]; then return 0; fi
    for d in "\$HOME/.steam/steam/steamapps/common" "\$HOME/.steam/root/steamapps/common" \\
             "\$HOME/.local/share/Steam/steamapps/common" "\$HOME/.steam/steam/compatibilitytools.d" \\
             "\$HOME/.steam/root/compatibilitytools.d" "\$HOME/.local/share/Steam/compatibilitytools.d"; do
        [ -d "\$d" ] || continue
        for p in "\$d"/*/; do
            if [ -x "\${p}proton" ]; then
                PROTON_DIR="\${p%/}"
                return 0
            fi
        done
    done
    return 1
}
if ! find_proton; then
    echo "error: could not find a Proton install. Install one through Steam (or a" >&2
    echo "GE-Proton build into compatibilitytools.d), or set PROTON_DIR=/path/to/Proton." >&2
    exit 1
fi
STEAM_ROOT=""
for d in "\$HOME/.steam/steam" "\$HOME/.steam/root" "\$HOME/.local/share/Steam"; do
    [ -d "\$d" ] && { STEAM_ROOT="\$d"; break; }
done
PROTON_PREFIX="\${PROTON_PREFIX:-\$DATA_HOME/proton-prefix}"
mkdir -p "\$PROTON_PREFIX"

WINE_APPDATA="\$PROTON_PREFIX/pfx/drive_c/users/steamuser/AppData/Local/WiiCompiled"
mkdir -p "\$WINE_APPDATA"
CONFIG="\$WINE_APPDATA/Config.toml"
if [ ! -f "\$CONFIG" ]; then
    cp "\$HERE/usr/bin/UserData/Config.toml" "\$CONFIG"
fi
sed -i '/^\[paths\]/,\$d' "\$CONFIG"
{
    echo "[paths]"
    echo "dvd_root = \"\$HERE/usr/bin/DATA\""
EOF_APPRUN
            if [ "$RETRO" = "1" ]; then
                cat >> "$appdir/AppRun" <<'EOF_APPRUN'
    echo "retro_rewind_root = \"$DATA_HOME/sdroot/RetroRewind6\""
EOF_APPRUN
            fi
            cat >> "$appdir/AppRun" <<EOF_APPRUN
} >> "\$CONFIG"
EOF_APPRUN
            if [ "$RETRO" = "1" ]; then
                # Same as build-linux-native.sh's AppRun: Retro Rewind
                # writes saves next to retro_rewind_root's parent, so
                # RetroRewind6/ must be a real directory, not a symlink -
                # symlink its contents instead.
                cat >> "$appdir/AppRun" <<'EOF_APPRUN'
mkdir -p "$DATA_HOME/sdroot/RetroRewind6"
for entry in "$HERE"/usr/bin/RetroRewind6/* "$HERE"/usr/bin/RetroRewind6/.[!.]*; do
    [ -e "$entry" ] || continue
    ln -sfn "$entry" "$DATA_HOME/sdroot/RetroRewind6/$(basename "$entry")"
done
EOF_APPRUN
            fi
            cat >> "$appdir/AppRun" <<EOF_APPRUN
STEAM_COMPAT_DATA_PATH="\$PROTON_PREFIX" STEAM_COMPAT_CLIENT_INSTALL_PATH="\$STEAM_ROOT" \\
    exec "\$PROTON_DIR/proton" run "\$HERE/usr/bin/$binary" "\$@"
EOF_APPRUN
            chmod +x "$appdir/AppRun"

            cat > "$appdir/$out_name.desktop" <<EOF_DESKTOP
[Desktop Entry]
Type=Application
Name=$human_name
GenericName=Mario Kart Wii PC Recompiled
Comment=Statically recompiled Mario Kart Wii (Windows build via Proton)
Exec=$out_name-stub
Icon=wiicompiled
Categories=Game;
Terminal=false
EOF_DESKTOP

            rm -f ./*.AppImage
            "$LINUXDEPLOY" --appdir "$appdir" --executable "$appdir/usr/bin/$out_name-stub" \
                --desktop-file "$appdir/$out_name.desktop" --icon-file "$ICON_FILE" --output appimage
            mv -f ./*.AppImage "dist/$out_name-proton-linux-x86_64.AppImage"
            rm -rf "$appdir"
            echo "==> packaged: dist/$out_name-proton-linux-x86_64.AppImage ($(du -sh "dist/$out_name-proton-linux-x86_64.AppImage" | cut -f1))"
        }

        build_appimage WiiCompiled WiiCompiled.exe "WiiCompiled (Proton)"
        if [ "$RETRO" = "1" ]; then
            build_appimage RetroRewind RetroRewind.exe "WiiCompiled Retro Rewind (Proton)"
        fi
    fi

    rm -rf "$STAGE_DIR"
fi

echo ""
echo "Build complete! Find it at $BUILD_DIR/WiiCompiled.exe"
if [ "$RETRO" = "1" ]; then
    echo "Retro Rewind build at $BUILD_DIR/RetroRewind.exe"
fi
echo ""
echo "This is the same Windows binary build.sh produces, run under Proton instead of"
echo "Wine directly or a real Windows machine."

if [ "$INSTALL" = "1" ]; then
    echo ""
    echo "Installed: $INSTALL_DIR/BaseProton/run.sh"
    if [ "$RETRO" = "1" ]; then
        echo "       and $INSTALL_DIR/RetroRewindProton/run.sh"
    fi
    echo "Menu launchers written to ${XDG_DATA_HOME:-$HOME/.local/share}/applications/, named"
    echo "\"(Proton)\" to tell them apart from build-linux-native.sh's native launchers."
fi

if [ "$APPIMAGE" = "1" ]; then
    echo ""
    echo "To play: chmod +x dist/WiiCompiled-proton-linux-x86_64.AppImage and run it, anywhere -"
    echo "as long as a Proton install exists somewhere Steam would put one on this machine."
    if [ "$RETRO" = "1" ]; then
        echo "Retro Rewind: dist/RetroRewind-proton-linux-x86_64.AppImage, same way."
    fi
elif [ "$PACKAGE" = "1" ]; then
    echo ""
    echo "To play: unzip dist/WiiCompiled-proton.zip anywhere and run ./run.sh inside it"
    echo "(./run-retrorewind.sh for Retro Rewind). Needs a Proton install on that machine -"
    echo "Proton itself and Wine's shared libraries are NOT bundled."
elif [ "$INSTALL" != "1" ] && [ "$RUN" != "1" ]; then
    echo ""
    echo "To play: run $BUILD_DIR/WiiCompiled.exe under Proton yourself, or re-run without"
    echo "--no-run to have this script do it. Re-run with --package for a movable copy,"
    echo "--install for tidy folders / a menu entry."
fi

if [ "$RUN" = "1" ] && [ "$INSTALL" != "1" ] && [ "$PACKAGE" != "1" ] && [ "$APPIMAGE" != "1" ]; then
    find_proton() {
        if [ -n "$PROTON_DIR" ] && [ -x "$PROTON_DIR/proton" ]; then return 0; fi
        local search_dirs=(
            "$HOME/.steam/steam/steamapps/common"
            "$HOME/.steam/root/steamapps/common"
            "$HOME/.local/share/Steam/steamapps/common"
            "$HOME/.steam/steam/compatibilitytools.d"
            "$HOME/.steam/root/compatibilitytools.d"
            "$HOME/.local/share/Steam/compatibilitytools.d"
        )
        local candidates=() d p
        for d in "${search_dirs[@]}"; do
            [ -d "$d" ] || continue
            for p in "$d"/*/; do
                [ -x "${p}proton" ] && candidates+=("${p%/}")
            done
        done
        [ "${#candidates[@]}" -gt 0 ] || return 1
        PROTON_DIR="$(ls -td "${candidates[@]}" | head -n1)"
    }

    if ! find_proton; then
        echo "" >&2
        echo "error: could not find a Proton install." >&2
        echo "  Looked under steamapps/common/ and compatibilitytools.d/ in the usual Steam locations." >&2
        echo "  Install a Proton version through Steam (or GE-Proton into compatibilitytools.d), or" >&2
        echo "  point at one directly: --proton-dir=/path/to/Proton (a directory with a 'proton' script)." >&2
        echo "  Re-run with --no-run to just build without launching it." >&2
        exit 1
    fi
    echo "==> using Proton at $PROTON_DIR"

    find_steam_root() {
        local d
        for d in "$HOME/.steam/steam" "$HOME/.steam/root" "$HOME/.local/share/Steam"; do
            [ -d "$d" ] && { echo "$d"; return 0; }
        done
        return 1
    }
    STEAM_ROOT="$(find_steam_root || true)"

    PROTON_PREFIX="${PROTON_PREFIX:-$(pwd)/.toolchain/proton-prefix}"
    mkdir -p "$PROTON_PREFIX"

    if [ "$RUN_RETRO" = "1" ]; then
        EXE_NAME="RetroRewind.exe"
    else
        EXE_NAME="WiiCompiled.exe"
    fi
    EXE_PATH="$BUILD_DIR/$EXE_NAME"
    if [ ! -f "$EXE_PATH" ]; then
        echo "error: $EXE_PATH not found after building" >&2
        exit 1
    fi

    echo ""
    echo "==> running $EXE_PATH under Proton"
    echo "    prefix: $PROTON_PREFIX"
    [ -n "$STEAM_ROOT" ] || echo "    warning: no Steam install found; STEAM_COMPAT_CLIENT_INSTALL_PATH left unset" >&2

    STEAM_COMPAT_DATA_PATH="$PROTON_PREFIX" \
    STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_ROOT" \
    "$PROTON_DIR/proton" run "$(realpath "$EXE_PATH")"
fi
