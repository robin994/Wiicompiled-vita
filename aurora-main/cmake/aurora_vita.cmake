# Aurora Vita backend target. The default target is intentionally Dawn-free and
# can be built with AURORA_ENABLE_GX=OFF. Optional upstream-GX mode is a desktop
# integration/syntax gate while Aurora's upstream GX target still owns Dawn.
option(AURORA_VITA_WITH_UPSTREAM_GX "Compile the Vita bridge against Aurora's real GX structs (requires AURORA_ENABLE_GX)" OFF)

set(AURORA_VITA_BACKEND_SOURCES
    ${PROJECT_SOURCE_DIR}/platforms/vita/aurora_vita_backend.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gx/aurora_vita_draw_sink.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/integration/vita_feature_coverage.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/integration/vita_frame_trace.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/integration/vita_fifo_packet_queue.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/integration/vita_gx_capture.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/integration/vita_gx_replay.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/integration/wiicompiled_aurora_adapter.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/integration/vita_gx_backend.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_telemetry.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_memory_budget.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_gl_util.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_texture_decode.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_vertex_decode.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_vertex_pipeline.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_streaming_arena.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_draw_adapter.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_shader_gen.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_pipeline_key.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_buffer_pool.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_texture_cache.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_pipeline_cache.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_efb.cpp
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx/vita_renderer.cpp
)

if (AURORA_VITA_WITH_UPSTREAM_GX)
    if (NOT AURORA_ENABLE_GX)
        message(FATAL_ERROR "AURORA_VITA_WITH_UPSTREAM_GX requires AURORA_ENABLE_GX=ON until the upstream GX/Dawn split is complete")
    endif ()
    list(APPEND AURORA_VITA_BACKEND_SOURCES
        ${PROJECT_SOURCE_DIR}/platforms/vita/gx/aurora_gx_bridge.cpp
    )
endif ()

add_library(aurora_vita_backend STATIC ${AURORA_VITA_BACKEND_SOURCES})
add_library(aurora::vita_backend ALIAS aurora_vita_backend)
set_target_properties(aurora_vita_backend PROPERTIES FOLDER "aurora")
target_compile_features(aurora_vita_backend PUBLIC cxx_std_20)
target_include_directories(aurora_vita_backend PUBLIC
    ${PROJECT_SOURCE_DIR}/platforms/vita
    ${PROJECT_SOURCE_DIR}/platforms/vita/gfx
    ${PROJECT_SOURCE_DIR}/platforms/vita/integration
)
target_compile_definitions(aurora_vita_backend PUBLIC AURORA_PLATFORM_VITA=1 AURORA_GFX_VITA=1)

if (AURORA_VITA_WITH_UPSTREAM_GX)
    target_compile_definitions(aurora_vita_backend PRIVATE AURORA_VITA_UPSTREAM=1)
    # This is intentionally a desktop/integration dependency today. Removing
    # this link is the completion criterion for the Dawn-free upstream GX split.
    target_link_libraries(aurora_vita_backend PRIVATE aurora::gx)
endif ()

# When cross-compiling with VitaSDK the toolchain normally exposes these by name.
if (CMAKE_SYSTEM_NAME STREQUAL "Vita" OR DEFINED VITASDK OR CMAKE_CXX_COMPILER MATCHES "arm-vita-eabi")
    if (AURORA_VITA_WITH_UPSTREAM_GX)
        message(FATAL_ERROR "Do not enable AURORA_VITA_WITH_UPSTREAM_GX on Vita yet: upstream aurora::gx still links Dawn")
    endif ()
    target_compile_definitions(aurora_vita_backend PUBLIC __vita__=1)
    target_compile_options(aurora_vita_backend PRIVATE
        -O3 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti
        -mtune=cortex-a9 -mfpu=neon -ffast-math -fsigned-char
    )
    target_link_options(aurora_vita_backend PRIVATE -Wl,--gc-sections -Wl,-q)
    target_link_libraries(aurora_vita_backend PUBLIC
        vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub
        SceGxm_stub SceDisplay_stub SceCtrl_stub SceAppMgr_stub
        SceKernelDmacMgr_stub SceSysmodule_stub SceLibKernel_stub
        mathneon pthread m
    )
endif()
