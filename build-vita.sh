#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mode="${1:-game}"
if (($#)); then shift; fi
jobs="${WIC_JOBS:-8}"
export DOTNET_ROLL_FORWARD="${DOTNET_ROLL_FORWARD:-Major}"
host() {
    local options=()
    if [[ -d /opt/homebrew/opt/openssl@3 ]]; then
        options+=(-DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3)
    fi
    cmake -S . -B build/host -DWIC_AURORA_TESTS=OFF "${options[@]}"
    if (($#)); then
        cmake --build build/host --parallel "$jobs" --target "$@"
    else
        cmake --build build/host --parallel "$jobs"
    fi
    export WIC_DISC_TOOL="$PWD/build/host/wiicompiled-disc"
}
prepare() {
    # Generation must bootstrap without the generated game headers. Build only
    # the ISO reader needed by the translator, rather than every runtime test.
    host wiicompiled-disc
    "$WIC_DISC_TOOL" inspect mariokart.iso
    dotnet build translator/Translator.sln -c Release
    local cli=translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll
    local project=projects/mkwii/vita-iso.yml
    dotnet "$cli" generate-data-init --project "$project"
    dotnet "$cli" translate-recursive 0x800060A4 --project "$project" --threads "$jobs" \
        --output-metadata generated/base_translation_output.json
    dotnet "$cli" emit-build-shards --project "$project"
}
native() {
    local game="$1"
    shift
    local sdk="${VITASDK:-/usr/local/vitasdk}"
    local defaultFolder=build/vita
    if [[ "$game" == ON ]]; then defaultFolder=build/vita-game; fi
    local folder="${WIC_BUILD_DIR:-$defaultFolder}"
    cmake -S . -B "$folder" -DCMAKE_TOOLCHAIN_FILE="$sdk/share/vita.toolchain.cmake" \
        -DVITASDK="$sdk" -DCMAKE_BUILD_TYPE=Release -DWIC_BUILD_GAME="$game" \
        -DWIC_FAST_PPC_MATH=OFF -DWIC_RESIDENT_GEOMETRY=OFF \
        -DWIC_NATIVE_TEXTURES=OFF -DWIC_PROFILE_GUEST=OFF "$@"
    cmake --build "$folder" --parallel "$jobs"
    local product=wiicompiled-vita-iso-check
    if [[ "$game" == ON ]]; then product=wiicompiled-vita-game; fi
    python3 aurora-main/tools/check_vita_gxm_binary.py "$folder/$product" --map "$folder/$product.map"
    if [[ "$game" == ON ]]; then
        python3 scripts/test_vita_runtime_linkage.py "$folder/$product" --output "$folder/RUNTIME_LINKAGE.json"
        python3 scripts/test_vita_stdio_linkage.py "$folder/$product" --with-waits --with-input \
            --output "$folder/NATIVE_BOUNDARIES.txt"
        if rg -q '^WIC_CPU_DIAGNOSTICS:BOOL=ON$' "$folder/CMakeCache.txt"; then
            local cpuAuditOptions=()
            if rg -q '^WIC_NATIVE_THP:BOOL=ON$' "$folder/CMakeCache.txt"; then
                cpuAuditOptions+=(--native-thp)
            fi
            if rg -q '^WIC_HARDWARE_AVC:BOOL=ON$' "$folder/CMakeCache.txt"; then
                cpuAuditOptions+=(--hardware-avc)
            fi
            if rg -q '^WIC_EXACT_FLOAT_FMA:BOOL=ON$' "$folder/CMakeCache.txt"; then
                python3 scripts/test_vita_cpu_linkage.py "$folder/$product" --output "$folder/CPU_LINKAGE.json" --exact-float-fma "${cpuAuditOptions[@]}"
            else
                python3 scripts/test_vita_cpu_linkage.py "$folder/$product" --output "$folder/CPU_LINKAGE.json" "${cpuAuditOptions[@]}"
            fi
        fi
        if rg -q '^WIC_PRECOMPILED_ASSETS:BOOL=ON$' "$folder/CMakeCache.txt"; then
            python3 scripts/test_vita_native_assets_linkage.py "$folder/$product" \
                --output "$folder/NATIVE_ASSETS_LINKAGE.json"
        fi
        python3 scripts/test_vita_native_render_linkage.py "$folder" \
            --output "$folder/NATIVE_RENDER_LINKAGE.json"
        if rg -q '^AURORA_VITA_ASYNC_GX:BOOL=ON$' "$folder/CMakeCache.txt"; then
            python3 scripts/test_vita_async_gx_linkage.py "$folder" \
                --output "$folder/ASYNC_GX_LINKAGE.json"
        else
            python3 scripts/test_vita_async_gx_linkage.py "$folder" --synchronous \
                --output "$folder/SYNCHRONOUS_GX_LINKAGE.json"
        fi
        if rg -q '^WIC_RESIDENT_GEOMETRY:BOOL=ON$' "$folder/CMakeCache.txt" &&
           rg -q '^WIC_RESIDENT_TEXTURES:BOOL=ON$' "$folder/CMakeCache.txt" &&
           rg -q '^AURORA_VITA_ASYNC_GX:BOOL=ON$' "$folder/CMakeCache.txt"; then
            python3 scripts/test_vita_resident_assets_linkage.py "$folder" \
                --output "$folder/RESIDENT_ASSETS_LINKAGE.json"
        fi
    fi
    local manifestOptions=()
    if [[ "$game" == ON ]]; then manifestOptions+=(--iso mariokart.iso); fi
    python3 scripts/record_vita_delivery.py "$folder" "${manifestOptions[@]}"
}
case "$mode" in
    host) host ;;
    prepare) prepare ;;
    data)
        host
        PYTHONDONTWRITEBYTECODE=1 python3 scripts/prepare_mkw_psarc.py "$@"
        ;;
    check) native OFF "$@" ;;
    game)
        if [[ "${WIC_SKIP_PREPARE:-0}" != 1 ]]; then prepare; fi
        native ON "$@"
        ;;
    test)
        host
        ctest --test-dir build/host --output-on-failure
        dotnet test translator/Translator.sln -c Release
        cmake -S aurora-main -B build/aurora-host -DAURORA_ENABLE_VITA_BACKEND=ON \
            -DAURORA_VITA_BACKEND_ONLY=ON -DAURORA_VITA_WITH_GX_FRONTEND=ON \
            -DAURORA_VITA_RENDERER=VITAGL -DAURORA_VITA_BUILD_BACKEND_TESTS=ON
        cmake --build build/aurora-host --parallel "$jobs"
        ctest --test-dir build/aurora-host --output-on-failure
        ;;
    *) echo "Usage: ./build-vita.sh {host|prepare|data|check|game|test} [options]" >&2; exit 2 ;;
esac
