if(NOT EXISTS "${PROJECT_SOURCE_DIR}/generated/base_translation_output.json")
    message(FATAL_ERROR "Run ./build-vita.sh prepare before WIC_BUILD_GAME=ON")
endif()
include(FetchContent)
include("${PROJECT_SOURCE_DIR}/runtime/cmake/MbedTLSPin.cmake")
FetchContent_Declare(wic_mbedtls
    URL "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-${MKW_MBEDTLS_VERSION}/mbedtls-${MKW_MBEDTLS_VERSION}.tar.bz2"
    URL_HASH SHA256=${MKW_MBEDTLS_SHA256})
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(wic_mbedtls)
foreach(t mbedtls mbedx509 mbedcrypto)
    target_compile_definitions(${t} PUBLIC MBEDTLS_USER_CONFIG_FILE="wic_mbedtls_config.h")
    target_include_directories(${t} PUBLIC "${PROJECT_SOURCE_DIR}/vita")
endforeach()
file(GLOB crypto_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/runtime/third_party/cryptopp/*.cpp")
list(FILTER crypto_sources EXCLUDE REGEX "/(adhoc|bench[123]|datatest|dlltest|fipsalgt|fipstest|pch|regtest[1234]|simple|test|validat[0-9]+)\\.cpp$")
list(FILTER crypto_sources EXCLUDE REGEX "(_avx|_ppc|_simd|_sse)\\.cpp$")
add_library(wic_crypto STATIC ${crypto_sources})
target_compile_definitions(wic_crypto PUBLIC CRYPTOPP_DISABLE_ASM=1 CRYPTOPP_NO_DLL=1)
target_include_directories(wic_crypto SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/runtime/third_party")
target_compile_options(wic_crypto PRIVATE -Os -w)

add_library(wic_pugixml STATIC "${PROJECT_SOURCE_DIR}/runtime/third_party/pugixml/pugixml.cpp")
target_include_directories(wic_pugixml PUBLIC "${PROJECT_SOURCE_DIR}/runtime/third_party/pugixml")
add_library(wic_libco STATIC "${PROJECT_SOURCE_DIR}/runtime/third_party/libco/libco.c")
target_include_directories(wic_libco PUBLIC "${PROJECT_SOURCE_DIR}/runtime/third_party/libco")

file(GLOB_RECURSE native_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/runtime/src/*.cpp")
list(FILTER native_sources EXCLUDE REGEX "/(main|guest_flat_memory|guest_flat_memory_macos|audio_backend|host_cpu_baseline|settings_overlay|controller_mapping_wizard|input_bindings|input_expr|wii_remote_input|discord_presence)\\.cpp$")
list(FILTER native_sources EXCLUDE REGEX "/(platform/host_platform|product/retro_rewind_product)\\.cpp$")
add_library(wiicompiled_runtime OBJECT ${native_sources}
    "${PROJECT_SOURCE_DIR}/vita/guest_memory.cpp"
    "${PROJECT_SOURCE_DIR}/vita/game_platform.cpp"
    "${PROJECT_SOURCE_DIR}/vita/game_input.cpp"
    "${PROJECT_SOURCE_DIR}/vita/game_audio.cpp"
    "${PROJECT_SOURCE_DIR}/vita/game_diagnostics.cpp"
    "${PROJECT_SOURCE_DIR}/vita/game_time_trial.cpp"
    "${PROJECT_SOURCE_DIR}/vita/psarc_backend.cpp"
    "${PROJECT_SOURCE_DIR}/vita/game_aurora.cpp")
target_link_libraries(wiicompiled_runtime PUBLIC wiicompiled_core aurora::vita_backend
    wic_crypto wic_pugixml wic_libco mbedtls mbedx509 mbedcrypto SceAudio_stub SceNet_stub)
if(WIC_PRECOMPILED_ASSETS)
    target_sources(wiicompiled_runtime PRIVATE "${PROJECT_SOURCE_DIR}/vita/native_assets.cpp"
        "${PROJECT_SOURCE_DIR}/vita/native_audio.cpp")
endif()
target_include_directories(wiicompiled_runtime PUBLIC
    "${PROJECT_SOURCE_DIR}/runtime/include" "${PROJECT_SOURCE_DIR}/runtime/src"
    "${PROJECT_SOURCE_DIR}/runtime/third_party/toml11" "${PROJECT_SOURCE_DIR}")
target_compile_definitions(wiicompiled_runtime PUBLIC MKW_TARGET_VITA=1 TARGET_PC=1
    WIC_FAST_PPC_MATH=$<BOOL:${WIC_FAST_PPC_MATH}>
    WIC_FAST_RAM_RANGES=$<BOOL:${WIC_FAST_RAM_RANGES}>)
target_compile_definitions(wiicompiled_runtime PUBLIC WIC_RESIDENT_GEOMETRY=$<BOOL:${WIC_RESIDENT_GEOMETRY}>)
target_compile_definitions(wiicompiled_runtime PRIVATE WIC_PRECOMPILED_ASSETS=$<BOOL:${WIC_PRECOMPILED_ASSETS}>)
target_compile_definitions(wiicompiled_runtime PRIVATE AURORA_VITA_ASYNC_GX=0)
target_compile_features(wiicompiled_runtime PUBLIC cxx_std_20)
target_compile_options(wiicompiled_runtime PRIVATE -Os -ffunction-sections -fdata-sections -fno-fast-math -ffp-contract=off)
file(GLOB translated_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/generated/build_shards/base_common/*.cpp"
    "${PROJECT_SOURCE_DIR}/generated/build_shards/base_registration/*.cpp"
    "${PROJECT_SOURCE_DIR}/generated/build_shards/base_dispatch/*.cpp")
enable_language(ASM)
add_executable(wiicompiled-vita-game "${PROJECT_SOURCE_DIR}/vita/game_main.cpp"
    "${PROJECT_SOURCE_DIR}/vita/game_stdio.cpp"
    "${PROJECT_SOURCE_DIR}/vita/runtime_checks.cpp"
    ${translated_sources} "${PROJECT_SOURCE_DIR}/generated/data_sections_init.cpp"
    "${PROJECT_SOURCE_DIR}/generated/data_sections_init_blobs.S"
    "${PROJECT_SOURCE_DIR}/generated/guest_symbol_table.cpp")
# These units never inspect this legacy renderer implementation macro.
target_compile_definitions(wiicompiled-vita-game PRIVATE AURORA_VITA_ASYNC_GX=0)
execute_process(COMMAND git -C "${PROJECT_SOURCE_DIR}/aurora-main" rev-parse HEAD
    OUTPUT_VARIABLE WIC_AURORA_REV OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND git -C "${PROJECT_SOURCE_DIR}/aurora-main" status --porcelain --untracked-files=normal
    OUTPUT_VARIABLE WIC_AURORA_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT WIC_AURORA_STATUS STREQUAL "")
    string(APPEND WIC_AURORA_REV "-dirty")
endif()
set_property(SOURCE "${PROJECT_SOURCE_DIR}/vita/game_main.cpp" APPEND PROPERTY
    COMPILE_DEFINITIONS WIC_AURORA_REV="${WIC_AURORA_REV}"
    WIC_PRECOMPILED_ASSETS=$<BOOL:${WIC_PRECOMPILED_ASSETS}>
    WIC_GX_ASYNC_BUILD=$<BOOL:${AURORA_VITA_ASYNC_GX}>
    WIC_RESIDENT_TEXTURES=$<BOOL:${WIC_RESIDENT_TEXTURES}>
    WIC_NATIVE_CALL_PROFILE=$<BOOL:${WIC_NATIVE_CALL_PROFILE}>
    WIC_NATIVE_THP=$<BOOL:${WIC_NATIVE_THP}>
    WIC_HARDWARE_AVC=$<BOOL:${WIC_HARDWARE_AVC}>
    WIC_DISTINCT_CPU_CORES=$<BOOL:${WIC_DISTINCT_CPU_CORES}>)
if(WIC_CPU_DIAGNOSTICS OR WIC_EXACT_FLOAT_FMA)
    target_sources(wiicompiled_runtime PRIVATE "${PROJECT_SOURCE_DIR}/vita/game_cpu_diagnostics.cpp")
    target_link_options(wiicompiled-vita-game PRIVATE -Wl,--wrap=fmaf)
endif()
if(WIC_EXACT_FLOAT_FMA)
    target_compile_definitions(wiicompiled_runtime PRIVATE WIC_EXACT_FLOAT_FMA=1)
endif()
if(WIC_CPU_DIAGNOSTICS)
    target_sources(wiicompiled_runtime PRIVATE "${PROJECT_SOURCE_DIR}/vita/game_heap_watch.cpp")
    target_link_options(wiicompiled-vita-game PRIVATE -Wl,--wrap=func_80199B58 -Wl,--wrap=func_80082AF0)
    target_compile_definitions(wiicompiled_runtime PRIVATE WIC_CPU_DIAGNOSTICS=1)
    target_compile_definitions(wiicompiled-vita-game PRIVATE WIC_CPU_DIAGNOSTICS=1)
    target_link_options(wiicompiled-vita-game PRIVATE
        -Wl,--wrap=fma -Wl,--wrap=func_8007A170
        -Wl,--wrap=func_8007AB90 -Wl,--wrap=func_80199B98
        -Wl,--wrap=_Z18PpcFmaddPairInline18__simd64_float32_tS_S_
        -Wl,--wrap=_Z18PpcFmsubPairInline18__simd64_float32_tS_S_)
endif()
if(WIC_PROFILE_GUEST)
    target_sources(wiicompiled-vita-game PRIVATE "${PROJECT_SOURCE_DIR}/vita/guest_profile.cpp")
    set_source_files_properties(${translated_sources} PROPERTIES COMPILE_OPTIONS -finstrument-functions)
    target_compile_definitions(wiicompiled_runtime PRIVATE WIC_PROFILE_GUEST=1)
endif()
if(WIC_NATIVE_THP OR WIC_HARDWARE_AVC)
    find_package(JPEG REQUIRED)
    target_sources(wiicompiled_runtime PRIVATE
        "${PROJECT_SOURCE_DIR}/vita/thp_native.cpp" "${PROJECT_SOURCE_DIR}/vita/game_thp.cpp")
    target_link_libraries(wiicompiled_runtime PUBLIC JPEG::JPEG)
    target_link_options(wiicompiled-vita-game PRIVATE -Wl,--wrap=func_801B3BAC)
    target_compile_definitions(wiicompiled_runtime PRIVATE WIC_NATIVE_THP=$<BOOL:${WIC_NATIVE_THP}>)
endif()
if(WIC_HARDWARE_AVC)
    target_sources(wiicompiled_runtime PRIVATE
        "${PROJECT_SOURCE_DIR}/vita/avc_video.cpp" "${PROJECT_SOURCE_DIR}/vita/avc_hardware.cpp")
    target_compile_definitions(wiicompiled_runtime PRIVATE WIC_HARDWARE_AVC=1)
    target_link_libraries(wiicompiled_runtime PUBLIC SceVideodec_stub SceSysmodule_stub)
endif()
target_link_libraries(wiicompiled-vita-game PRIVATE wiicompiled_runtime wiicompiled_backend
    ScePower_stub SceFiber_stub SceSysmodule_stub
    "-Wl,--whole-archive" pthread "-Wl,--no-whole-archive")
# libstdc++ references pthread primitives weakly, which does not extract their
# objects from a static archive. Include the complete pthread archive, including
# pthread_once used by pre-main C++ guards, and keep section garbage collection.
# Scope the linker flags to pthread: VitaSDK's Generic platform does not expose
# CMake's WHOLE_ARCHIVE feature. All other libraries keep ordinary extraction.
target_compile_options(wiicompiled-vita-game PRIVATE -Os -ffunction-sections -fdata-sections -fno-fast-math -ffp-contract=off)
target_link_options(wiicompiled-vita-game PRIVATE -Wl,--gc-sections -Wl,-q
    -Wl,--wrap=_lseek_r -Wl,--wrap=_write_r
    -Wl,-Map=${PROJECT_BINARY_DIR}/wiicompiled-vita-game.map)
vita_create_self(game-eboot.bin wiicompiled-vita-game UNSAFE)
set(WIC_VITA_VPK_FILES
    "${PROJECT_SOURCE_DIR}/runtime/assets/dsp/dsp_coef.bin" "dsp_coef.bin"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/misc.bin" "wii_bootstrap/shared2/wc24/misc.bin"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/nwc24dl.bin" "wii_bootstrap/shared2/wc24/nwc24dl.bin"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/nwc24fl.bin" "wii_bootstrap/shared2/wc24/nwc24fl.bin"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/nwc24fls.bin" "wii_bootstrap/shared2/wc24/nwc24fls.bin"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/nwc24msg.cbk" "wii_bootstrap/shared2/wc24/nwc24msg.cbk"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/nwc24msg.cfg" "wii_bootstrap/shared2/wc24/nwc24msg.cfg"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/mbox/Readme.txt" "wii_bootstrap/shared2/wc24/mbox/Readme.txt"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/mbox/wc24recv.ctl" "wii_bootstrap/shared2/wc24/mbox/wc24recv.ctl"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/mbox/wc24recv.mbx" "wii_bootstrap/shared2/wc24/mbox/wc24recv.mbx"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/mbox/wc24send.ctl" "wii_bootstrap/shared2/wc24/mbox/wc24send.ctl"
    "${PROJECT_SOURCE_DIR}/runtime/assets/wii/shared2/wc24/mbox/wc24send.mbx" "wii_bootstrap/shared2/wc24/mbox/wc24send.mbx")

set(VITA_MKSFOEX_FLAGS "${WIC_VITA_SFO_FLAGS} -d ATTRIBUTE2=12")
vita_create_vpk(wiicompiled-vita-game.vpk WICMKW001 game-eboot.bin
    VERSION 00.01
    NAME "WiiCompiled Mario Kart"
    FILE ${WIC_VITA_VPK_FILES})
