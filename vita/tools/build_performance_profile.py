#!/usr/bin/env python3
"""Reproducible A/B packages, retaining the full NEON -Os object staging."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASE = dict(
    MKW_TRANSLATED_BUILD_DIR="build/vita/mkwii_translated_neon_os",
    MKW_TRANSLATED_OPT="-Os -fno-asynchronous-unwind-tables -mfpu=neon -mfloat-abi=hard",
    MKW_VITA_LYT_DIRECT=0, MKW_VITA_LYT_FAITHFUL=1, MKW_VITA_DISABLE_MOVIES=1,
    MKW_VITA_NATIVE_THP=0, MKW_VITA_PERF_LOG=0, MKW_VITA_COMPACT_VERTEX=1,
    MKW_VITA_FRAME_BATCHER=1, MKW_VITA_DIRECT_STREAM_WRITE=1,
    MKW_VITA_COMPACT_FRAME_STATE=1, MKW_VITA_FRAME_QUEUE_DEPTH=2,
    MKW_VITA_DL_TEMPLATE_CACHE=1, MKW_VITA_GX_STATE_GENERATIONS=1,
    MKW_VITA_RAW_LAYOUT_CACHE=1, MKW_VITA_RAW_MESH_CACHE=1,
    MKW_VITA_EFB_GPU_BLIT=0, MKW_VITA_EFB_TRANSFER_READBACK=0,
    MKW_VITA_EFB_READBACK_FLIP_Y=1, MKW_VITA_PERF_SKIP_EFB=0,
    MKW_VITA_PERF_SKIP_BILLBOARDS=0, MKW_VITA_PERF_SKIP_LIGHTTEXTURE=0,
    MKW_VITA_PERF_FORCE_3D_SOLID=0, MKW_VITA_PERF_INJECT_CLIP_TRIANGLE=0,
    MKW_VITA_PERF_INJECT_WII_TRIANGLE=0, MKW_VITA_EFB_RESIDENT_COPY=1,
    MKW_VITA_EFB_NATIVE_RES_COPY=0,
    MKW_VITA_EFB_COMMAND_CAPACITY=128, MKW_VITA_STREAM_SAFE_REUSE=0,
    MKW_VITA_UI_QUAD_RUNS=0, MKW_VITA_TEXTURE_SHARED_HEADROOM=0,
    MKW_VITA_TEXTURE_SAFE_RETRY=0, MKW_VITA_WAIT_TIMING_SERVICE=0,
    MKW_VITA_INCREMENTAL_CACHE_EVICTION=0,
    MKW_VITA_FIBER_IRQ_STATE=0, MKW_VITA_WAIT_SERVICE_PROFILE=0,
    MKW_VITA_AUDIO_WAIT_PROFILE=0, MKW_VITA_AUDIO_AI_PROFILE=0,
    MKW_VITA_NATIVE_AUDIOOUT=0, MKW_VITA_AUDIO_PACING=0, MKW_VITA_DIRECT_BATCHER=0,
    MKW_VITA_DIRECT_EFB=0, MKW_VITA_DIRECT_STATE_CACHE=0,
    MKW_VITA_DIRECT_TEV_SPECIALIZE=0, MKW_VITA_DIRECT_TEV_TWO_TEXTURE=0,
    MKW_VITA_DIRECT_3D_TEXTURED_COMPAT=0, MKW_VITA_DIRECT_3D_TEXTURE_RESCUE=0,
    MKW_VITA_DIRECT_FFP_PREWARM=0, MKW_VITA_FFP_CACHE_RUNTIME_SAVE=1,
    MKW_VITA_DIRECT_GX_DEPTH_RANGE=0,
    MKW_VITA_INTERNAL_RENDER_WIDTH=960, MKW_VITA_INTERNAL_RENDER_HEIGHT=544,
    MKW_VITA_DIRECT_PREP_WORKER=0,
    MKW_VITA_DIRECT_VERTEX_PREP=0, MKW_VITA_DIRECT_TEXTURE_PREP=0,
    MKW_VITA_DIRECT_TEXTURE_PRIORITY_PREP=0,
    MKW_VITA_DIRECT_STATE_PREP=0, MKW_VITA_DIRECT_TEXTURE_CACHE_ANTITHRASH=0,
    MKW_VITA_GUEST_IO_PROFILE=0,
    MKW_VITA_FULLSCREEN_PRESENT=0, MKW_VITA_DVD_HOST_BUFFERING=0,
    MKW_VITA_DVD_ASYNC_HOST=0, MKW_VITA_RFL_SHAPE_DL_BURST=0,
    MKW_VITA_BRSAR_PREFETCH=0, MKW_VITA_BRSAR_PREFETCH_COOPERATIVE=0,
    MKW_VITA_BRSAR_PREFETCH_PHYCONT=0,
    MKW_VITA_VGL_CDRAM_RESERVE_MB=0, MKW_VITA_VGL_PHYCONT_RESERVE_MB=0,
    MKW_VITA_VGL_CIRCULAR_POOL_MB=0, MKW_VITA_GUEST_WRITE_HIERARCHY=0,
    MKW_VITA_YAZ0_FAST_DIRECT=0, MKW_VITA_THP_ASYNC_WORKER=0,
    MKW_VITA_DIRECT_EFB_BATCH_SYNC=0, MKW_VITA_DIRECT_EFB_PREFLIGHT=0,
    MKW_VITA_RENDER_DECOUPLE=0, MKW_VITA_RENDER_TARGET_HZ=60,
    MKW_VITA_DIRECT_WORKER_TIMING=0,
    MKW_VITA_DIRECT_PRESENT_30HZ=0, MKW_VITA_DIRECT_DECOUPLED_PRESENT=0,
    MKW_VITA_GUEST_CPU_PROFILE=0, MKW_VITA_GUEST_PC_SAMPLER=0,
    MKW_VITA_GUEST_PC_SAMPLE_US=2000, MKW_VITA_BUFFERED_LOGGING=0,
    MKW_VITA_LOG_FLUSH_INTERVAL_US=250000, MKW_VITA_AUDIO_WAIT_MIN_INTERVAL_US=0,
    MKW_VITA_AUDIO_BACKLOG_CLAMP_BLOCKS=0, MKW_VITA_AX_NEON=0,
    MKW_VITA_THP_UNESCAPED_FIX=0, MKW_VITA_AUDIO_WAIT_BLOCK_BUDGET=4,
    MKW_VITA_SOL_CRITICAL_PROFILE=0, MKW_VITA_SOL_DETAIL_BURST=0,
    MKW_VITA_BUILD_MARKER=0, MKW_VITA_TEXTURE_RAW_CPU_CACHE=0,
    MKW_VITA_TEXTURE_CACHE_BUDGET_MB=12, MKW_VITA_PREP_TEXTURE_LIMIT=8,
    MKW_VITA_PREP_TEXTURE_BUDGET_MB=4, MKW_VITA_EFB_EXTENDED_COALESCE=0,
    MKW_VITA_EFB_DEFER_TRANSFER_FINISH=0, MKW_VITA_RESCUE_KEEP_VERTEX_COLOR=0,
    MKW_VITA_RESCUE_KEEP_DEPTH=0, MKW_VITA_RESCUE_KEEP_CULL=0,
    MKW_VITA_RESCUE_KEEP_BLEND_ALPHA=0, MKW_VITA_RESCUE_KEEP_TEV=0,
    MKW_VITA_DIRECT_LINEAR_BATCH_PREP=0, MKW_VITA_GUEST_ACTIVE_CPU_PROFILE=0,
    MKW_VITA_VERTEX_REUSE_CACHE=0, MKW_VITA_PACKET_MOVE_STATE=0,
    MKW_VITA_TIMELINE_PROFILE=0, MKW_VITA_LOADING_AUDIO_PROFILE=0,
    MKW_VITA_AURORA_RENDERER=1,
)
PROFILES = {
    "full-content-3d": dict(MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
                            MKW_VITA_TEXTURE_SHARED_HEADROOM=1, MKW_VITA_UI_QUAD_RUNS=1,
                            MKW_VITA_DISABLE_MOVIES=0, MKW_VITA_NATIVE_THP=1,
                            MKW_VITA_EFB_NATIVE_RES_COPY=1, MKW_VITA_TEXTURE_SAFE_RETRY=1,
                            MKW_VITA_PERF_LOG=0, MKW_VITA_CLIP_W=1),
    "full-content-p5_2-timing-service": dict(
        MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
        MKW_VITA_TEXTURE_SHARED_HEADROOM=1, MKW_VITA_UI_QUAD_RUNS=1,
        MKW_VITA_DISABLE_MOVIES=0, MKW_VITA_NATIVE_THP=1,
        MKW_VITA_EFB_NATIVE_RES_COPY=1, MKW_VITA_TEXTURE_SAFE_RETRY=1,
        MKW_VITA_PERF_LOG=0, MKW_VITA_CLIP_W=1, MKW_VITA_WAIT_TIMING_SERVICE=1),
    "full-content-p5_3-cache-eviction": dict(
        MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
        MKW_VITA_TEXTURE_SHARED_HEADROOM=1, MKW_VITA_UI_QUAD_RUNS=1,
        MKW_VITA_DISABLE_MOVIES=0, MKW_VITA_NATIVE_THP=1,
        MKW_VITA_EFB_NATIVE_RES_COPY=1, MKW_VITA_TEXTURE_SAFE_RETRY=1,
        MKW_VITA_PERF_LOG=0, MKW_VITA_CLIP_W=1, MKW_VITA_WAIT_TIMING_SERVICE=1,
        MKW_VITA_INCREMENTAL_CACHE_EVICTION=1),
    "full-content-p5_4-fiber-irq": dict(
        MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
        MKW_VITA_TEXTURE_SHARED_HEADROOM=1, MKW_VITA_UI_QUAD_RUNS=1,
        MKW_VITA_DISABLE_MOVIES=0, MKW_VITA_NATIVE_THP=1,
        MKW_VITA_EFB_NATIVE_RES_COPY=1, MKW_VITA_TEXTURE_SAFE_RETRY=1,
        MKW_VITA_PERF_LOG=0, MKW_VITA_CLIP_W=1, MKW_VITA_WAIT_TIMING_SERVICE=1,
        MKW_VITA_INCREMENTAL_CACHE_EVICTION=1, MKW_VITA_FIBER_IRQ_STATE=1),
    "p5-resident": {},
    "p6-resources": dict(MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
                         MKW_VITA_TEXTURE_SHARED_HEADROOM=1),
    "p7-ui": dict(MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
                  MKW_VITA_TEXTURE_SHARED_HEADROOM=1, MKW_VITA_UI_QUAD_RUNS=1),
    "full-features": dict(MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
                          MKW_VITA_TEXTURE_SHARED_HEADROOM=1, MKW_VITA_UI_QUAD_RUNS=1,
                          MKW_VITA_DISABLE_MOVIES=0, MKW_VITA_NATIVE_THP=1),
    "full-features-p5_1": dict(MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
                               MKW_VITA_TEXTURE_SHARED_HEADROOM=1, MKW_VITA_UI_QUAD_RUNS=1,
                               MKW_VITA_DISABLE_MOVIES=0, MKW_VITA_NATIVE_THP=1,
                               MKW_VITA_PERF_LOG=1,
                               MKW_VITA_EFB_NATIVE_RES_COPY=1,
                               MKW_VITA_TEXTURE_SAFE_RETRY=1),
    "full-features-p5_1-measure": dict(MKW_VITA_STREAM_SAFE_REUSE=1, MKW_VITA_EFB_COMMAND_CAPACITY=512,
                                       MKW_VITA_TEXTURE_SHARED_HEADROOM=1, MKW_VITA_UI_QUAD_RUNS=1,
                                       MKW_VITA_DISABLE_MOVIES=0, MKW_VITA_NATIVE_THP=1,
                                       MKW_VITA_PERF_LOG=0,
                                       MKW_VITA_EFB_NATIVE_RES_COPY=1,
                                       MKW_VITA_TEXTURE_SAFE_RETRY=1),
}

# Identical to P5.4 except bounded service attribution, with no callback changes.
PROFILES["full-content-p5_5-wait-service-profile"] = (
    PROFILES["full-content-p5_4-fiber-irq"] | dict(MKW_VITA_WAIT_SERVICE_PROFILE=1))

PROFILES["full-content-p5_6-audio-wait-profile"] = (
    PROFILES["full-content-p5_5-wait-service-profile"] | dict(MKW_VITA_AUDIO_WAIT_PROFILE=1))

PROFILES["full-content-p5_7-audio-ai-profile"] = (
    PROFILES["full-content-p5_6-audio-wait-profile"] | dict(MKW_VITA_AUDIO_AI_PROFILE=1))

PROFILES["full-content-p5_8-native-audioout"] = (
    PROFILES["full-content-p5_7-audio-ai-profile"] | dict(MKW_VITA_NATIVE_AUDIOOUT=1))

PROFILES["full-content-p5_9-audio-pacing"] = (
    PROFILES["full-content-p5_8-native-audioout"] | dict(MKW_VITA_AUDIO_PACING=1))

# P6.0 architectural bring-up: same full-content/P5.9 correctness and timing
# stack, but the GX HLE frame packet is consumed directly by vitaGL.  With
# MKW_VITA_AURORA_RENDERER=0 the Makefile does not compile/link
# vita/aurora_packet_renderer.cpp nor aurora-main/platforms/vita/gfx/*.cpp.
PROFILES["full-content-p6_0-direct-vitagl"] = (
    PROFILES["full-content-p5_9-audio-pacing"] | dict(MKW_VITA_AURORA_RENDERER=0))

PROFILES["full-content-p6_1-direct-batcher"] = (
    PROFILES["full-content-p6_0-direct-vitagl"] | dict(MKW_VITA_DIRECT_BATCHER=1))

PROFILES["full-content-p6_1b-direct-capacity"] = (
    PROFILES["full-content-p6_1-direct-batcher"] | {})

PROFILES["full-content-p6_2-direct-efb"] = (
    PROFILES["full-content-p6_1b-direct-capacity"] | dict(MKW_VITA_DIRECT_EFB=1))

PROFILES["full-content-p6_3-direct-state-cache"] = (
    PROFILES["full-content-p6_2-direct-efb"] | dict(MKW_VITA_DIRECT_STATE_CACHE=1))

PROFILES["full-content-p6_4-direct-tev-specialize"] = (
    PROFILES["full-content-p6_3-direct-state-cache"] | dict(MKW_VITA_DIRECT_TEV_SPECIALIZE=1))

PROFILES["full-content-p6_4a-direct-worker-timing"] = (
    PROFILES["full-content-p6_4-direct-tev-specialize"] |
    dict(MKW_VITA_DIRECT_WORKER_TIMING=1, MKW_VITA_PERF_SUMMARY_INTERVAL=60))

PROFILES["full-content-p6_5-thp-unescaped"] = (
    PROFILES["full-content-p6_4a-direct-worker-timing"] |
    dict(MKW_VITA_THP_UNESCAPED_FIX=1))

PROFILES["full-content-p6_6-audio-wait-budget"] = (
    PROFILES["full-content-p6_5-thp-unescaped"] |
    dict(MKW_VITA_AUDIO_WAIT_BLOCK_BUDGET=1))

PROFILES["full-content-p6_7-direct-tev-two-texture"] = (
    PROFILES["full-content-p6_6-audio-wait-budget"] |
    dict(MKW_VITA_DIRECT_TEV_TWO_TEXTURE=1))

PROFILES["full-content-p6_8-mt-thp-prep"] = (
    PROFILES["full-content-p6_7-direct-tev-two-texture"] |
    dict(MKW_VITA_DIRECT_PREP_WORKER=1))

PROFILES["full-content-p6_9-mt-vertex-prep"] = (
    PROFILES["full-content-p6_8-mt-thp-prep"] |
    dict(MKW_VITA_DIRECT_VERTEX_PREP=1))

PROFILES["full-content-p6_10-mt-texture-prep"] = (
    PROFILES["full-content-p6_9-mt-vertex-prep"] |
    dict(MKW_VITA_DIRECT_TEXTURE_PREP=1))

PROFILES["full-content-p6_11-mt-state-prep"] = (
    PROFILES["full-content-p6_10-mt-texture-prep"] |
    dict(MKW_VITA_DIRECT_STATE_PREP=1))

PROFILES["full-content-p6_12-texture-antithrash"] = (
    PROFILES["full-content-p6_11-mt-state-prep"] |
    dict(MKW_VITA_DIRECT_TEXTURE_CACHE_ANTITHRASH=1))

PROFILES["full-content-p6_13-guest-io-profile"] = (
    PROFILES["full-content-p6_12-texture-antithrash"] |
    dict(MKW_VITA_GUEST_IO_PROFILE=1))

# P6.14 mainline hardware candidate: full-screen display mapping, buffered host
# DVD I/O and batched direct-EFB synchronization, without changing EFB resolution.
PROFILES["full-content-p6_14-30fps-graphics-fullscreen"] = (
    PROFILES["full-content-p6_13-guest-io-profile"] |
    dict(MKW_VITA_FULLSCREEN_PRESENT=1,
         MKW_VITA_DVD_HOST_BUFFERING=1,
         MKW_VITA_DIRECT_EFB_BATCH_SYNC=1))

# P6.15 mainline: keep the Wii timeline independent and use vitaGL/VBlank
# interval 2 for a true 30 Hz presenter on USER_1.
PROFILES["full-content-p6_15-decoupled-30hz"] = (
    PROFILES["full-content-p6_14-30fps-graphics-fullscreen"] |
    dict(MKW_VITA_RENDER_DECOUPLE=1,
         MKW_VITA_RENDER_TARGET_HZ=30))

# Retain the earlier software-paced profiles for direct A/B reproduction.
PROFILES["full-content-p6_14-present-30hz"] = (
    PROFILES["full-content-p6_13-guest-io-profile"] |
    dict(MKW_VITA_DIRECT_PRESENT_30HZ=1))
PROFILES["full-content-p6_15-decoupled-present"] = (
    PROFILES["full-content-p6_14-present-30hz"] |
    dict(MKW_VITA_DIRECT_DECOUPLED_PRESENT=1))

# P6.16 layers producer truth/optimization work on the newer VBlank-decoupled
# mainline P6.15 stack, avoiding a second software 30 Hz pacer.
PROFILES["full-content-p6_16-producer-all"] = (
    PROFILES["full-content-p6_15-decoupled-30hz"] |
    dict(MKW_VITA_GUEST_CPU_PROFILE=1,
         MKW_VITA_GUEST_PC_SAMPLER=1,
         MKW_VITA_GUEST_PC_SAMPLE_US=2000,
         MKW_VITA_BUFFERED_LOGGING=1,
         MKW_VITA_LOG_FLUSH_INTERVAL_US=250000,
         MKW_VITA_AUDIO_WAIT_MIN_INTERVAL_US=6000,
         MKW_VITA_AUDIO_BACKLOG_CLAMP_BLOCKS=3,
         MKW_VITA_AX_NEON=1))

# P6.17 is the first hardware-profiled producer build: it keeps the full-screen
# P6.14/P6.15 mainline and compiles the guest shards observed hot in the P6.16
# hardware log, including the long task callbacks, with -O2.
PROFILES["full-content-p6_17-producer-hot-o2"] = (
    PROFILES["full-content-p6_16-producer-all"] |
    dict(MKW_TRANSLATED_HOT_SHARDS="build_shards/base_common/shard_725a43c460cb362e6c90773f.cpp build_shards/base_common/shard_34d48ae74a14049acdf6da40.cpp build_shards/base_common/shard_524ee36baae61ba4cf80125b.cpp build_shards/base_common/shard_0038fe7fd1fb97c71b0063d2.cpp build_shards/base_common/shard_dac23cfe7b0b40a5fa626d24.cpp build_shards/base_common/shard_7e4e85bfe552d83be87bec14.cpp build_shards/base_common/shard_5e1324bf49db899095100c5d.cpp build_shards/base_common/shard_25095ab60ee81e785006b467.cpp build_shards/base_common/shard_abfadd8a2b5c92f9a88d68ab.cpp build_shards/base_common/shard_ab968ad22595bad323bed60a.cpp build_shards/base_common/shard_2dbf13c5086ec6891f714b6b.cpp build_shards/base_common/shard_3eb59e491a36d7242d565a06.cpp build_shards/base_common/shard_1ad6c774f8193dc8e5318e40.cpp build_shards/base_common/shard_f03227ec9fbe553ae94880e2.cpp build_shards/base_common/shard_7d45b3eea519ff690d1f6529.cpp build_shards/base_common/shard_9d589652ea7bcaf3bc33c546.cpp build_shards/base_common/shard_60e06428f12fde230bb8717d.cpp",
         MKW_TRANSLATED_HOT_OPT="-O2"))

# P6.18 isolates the native Yaz0/SZS host-output fast path while retaining the
# exact P6.17 fullscreen, decoupling and hardware-profiled hot-shard settings.
PROFILES["full-content-p6_18-yaz0-host-decode"] = (
    PROFILES["full-content-p6_17-producer-hot-o2"] | {})

# P6.19 resolves both Yaz0 input and output to host RAM when safe and emits
# per-decode timings so archive stalls can be attributed precisely on hardware.
PROFILES["full-content-p6_19-yaz0-host-io-profile"] = (
    PROFILES["full-content-p6_18-yaz0-host-decode"] | {})

# P6.20 keeps the P6.19 decoder/profile but clamps direct Yaz0 source mapping
# to the actual contiguous MEM1/MEM2 bytes remaining from src, preventing
# near-bank-end archives from falling back to per-byte guest reads.
PROFILES["full-content-p6_20-yaz0-bank-clamp"] = (
    PROFILES["full-content-p6_19-yaz0-host-io-profile"] | {})

# P6.21 bulk-copies direct-RAM Yaz0 literal/back-reference runs and emits
# detailed GX caller/gap diagnostics only for producer frames slower than 500 ms.
PROFILES["full-content-p6_21-yaz0-bulk-slowframe"] = (
    PROFILES["full-content-p6_20-yaz0-bank-clamp"] | {})

# P6.22 turns the Wii asynchronous DVD APIs into real host asynchronous I/O.
# Reads use sceIoPread on USER_2; guest state/callback/GX invalidation commit on USER_0.
PROFILES["full-content-p6_22-native-async-dvd"] = (
    PROFILES["full-content-p6_21-yaz0-bulk-slowframe"] |
    dict(MKW_VITA_DVD_ASYNC_HOST=1))

# P6.23 attacks the two remaining measured producer stalls: RFL shape display-list
# construction (4.3 s single GXBegin gap) and synchronous BRSAR storage reads.
PROFILES["full-content-p6_23-rfl-burst-brsar-prefetch"] = (
    PROFILES["full-content-p6_22-native-async-dvd"] |
    dict(MKW_VITA_RFL_SHAPE_DL_BURST=1, MKW_VITA_BRSAR_PREFETCH=1))

# P6.24 reserves bounded CDRAM/PHYCONT headroom outside vitaGL and places the
# BRSAR prefetch cache in cached physically-contiguous main RAM.
PROFILES["full-content-p6_24-memory-headroom"] = (
    PROFILES["full-content-p6_23-rfl-burst-brsar-prefetch"] |
    dict(MKW_VITA_BRSAR_PREFETCH_PHYCONT=1,
         MKW_VITA_VGL_CDRAM_RESERVE_MB=8,
         MKW_VITA_VGL_PHYCONT_RESERVE_MB=12))

# P6.25 keeps the P6.24 memory policy but shrinks vitaGL's 32 MiB circular
# vertex/scratch pool to 8 MiB. Hardware A/B validates headroom and stability.
PROFILES["full-content-p6_25-circular-pool-8mb"] = (
    PROFILES["full-content-p6_24-memory-headroom"] |
    dict(MKW_VITA_VGL_CIRCULAR_POOL_MB=8))

# P6.26 keeps the validated P6.25 memory policy and accelerates guest-write
# validation for large GX vertex arrays with a 1 MiB summary generation level.
# Native THP stage timing is compiled in to quantify the remaining pre-draw cost.
PROFILES["full-content-p6_26-guestgen-thp-profile"] = (
    PROFILES["full-content-p6_25-circular-pool-8mb"] |
    dict(MKW_VITA_GUEST_WRITE_HIERARCHY=1))

# P6.27 turns native THP decode into a cooperative asynchronous guest wait.
# TurboJPEG runs on a dedicated USER_2 job lane while USER_0 is free to run
# other Wii fibers; completion publication/wakeup stays on USER_0.
PROFILES["full-content-p6_27-thp-async-worker"] = (
    PROFILES["full-content-p6_26-guestgen-thp-profile"] |
    dict(MKW_VITA_THP_ASYNC_WORKER=1))

# P6.28 avoids USER_1 glFinish stalls for direct-EFB copies rejected by CPU preflight.
PROFILES["full-content-p6_28-efb-preflight"] = (
    PROFILES["full-content-p6_27-thp-async-worker"] |
    dict(MKW_VITA_DIRECT_EFB_PREFLIGHT=1))

# P6.29 removes the per-frame EGG::Display::endFrame host wait and lets an
# EFB frame queue behind its predecessor while preserving serial execution.
PROFILES["full-content-p6_29-display-pipeline"] = (
    PROFILES["full-content-p6_28-efb-preflight"] |
    dict(MKW_VITA_DISPLAY_END_FRAME_DECOUPLE=1,
         MKW_VITA_EFB_DEFERRED_BARRIER=1))

# P6.30 replaces the dominant ARC/string resource-lookup loops with faithful native HLE.
PROFILES["full-content-p6_30-arc-native"] = (
    PROFILES["full-content-p6_29-display-pipeline"] | {})

# P6.31 keeps native stricmp, but ARC path conversion uses the exact translated
# RMCP01 routine on cache misses and replays only previously verified results.
PROFILES["full-content-p6_31-arc-oracle-cache"] = (
    PROFILES["full-content-p6_30-arc-native"] | {})

# P6.32 ports the hardware-proven solid/depth visibility probe to the active
# direct-vitaGL renderer while preserving the complete P6.31 performance stack.
PROFILES["full-content-p6_32-direct-3d-visible"] = (
    PROFILES["full-content-p6_31-arc-oracle-cache"] |
    dict(MKW_VITA_PERF_FORCE_3D_SOLID=1,
         MKW_VITA_PERF_SOLID_KEEP_DEPTH=0))

# P6.33 restores real textures after the P6.32 geometry-visibility proof. Exact
# TEV subsets remain faithful; unsupported perspective chains use an opaque
# texture-REPLACE compatibility path plus the GX-to-GL perspective depth map.
PROFILES["full-content-p6_33-direct-3d-textured"] = (
    PROFILES["full-content-p6_32-direct-3d-visible"] |
    dict(MKW_VITA_PERF_FORCE_3D_SOLID=0,
         MKW_VITA_DIRECT_3D_TEXTURED_COMPAT=1,
         MKW_VITA_DIRECT_GX_DEPTH_RANGE=1))

# P6.34 fixes the P6.33 GX->OpenGL perspective depth conversion. P6.33 mapped
# GX [0,1] as if it were [-1,0], pushing most 3D vertices beyond +W. Keep the
# real-texture compatibility path, but use the corrected [0,1] -> [-1,1] map.
PROFILES["full-content-p6_34-direct-3d-depthfix"] = (
    PROFILES["full-content-p6_33-direct-3d-textured"] |
    dict(MKW_TRANSLATED_HOT_SHARDS="build_shards/base_common/shard_725a43c460cb362e6c90773f.cpp build_shards/base_common/shard_34d48ae74a14049acdf6da40.cpp build_shards/base_common/shard_dac23cfe7b0b40a5fa626d24.cpp build_shards/base_common/shard_7e4e85bfe552d83be87bec14.cpp build_shards/base_common/shard_5e1324bf49db899095100c5d.cpp build_shards/base_common/shard_25095ab60ee81e785006b467.cpp build_shards/base_common/shard_abfadd8a2b5c92f9a88d68ab.cpp build_shards/base_common/shard_ab968ad22595bad323bed60a.cpp build_shards/base_common/shard_3eb59e491a36d7242d565a06.cpp build_shards/base_common/shard_1ad6c774f8193dc8e5318e40.cpp build_shards/base_common/shard_f03227ec9fbe553ae94880e2.cpp build_shards/base_common/shard_7d45b3eea519ff690d1f6529.cpp build_shards/base_common/shard_9d589652ea7bcaf3bc33c546.cpp build_shards/base_common/shard_60e06428f12fde230bb8717d.cpp"))

# P6.35 starts from the only hardware-proven 3D state (P6.32) and adds just
# TEX0 sampling. Perspective draws keep white vertex colour, neutral Z and
# depth/cull/blend/alpha disabled; TEX0 uses GX_REPLACE and a failed resolve
# deliberately falls back to the visible white silhouette.
PROFILES["full-content-p6_35-direct-3d-texture-rescue"] = (
    PROFILES["full-content-p6_34-direct-3d-depthfix"] |
    dict(MKW_VITA_PERF_FORCE_3D_SOLID=0,
         MKW_VITA_DIRECT_3D_TEXTURED_COMPAT=0,
         MKW_VITA_DIRECT_3D_TEXTURE_RESCUE=1,
         MKW_VITA_DIRECT_GX_DEPTH_RANGE=0))

# P6.36 keeps the hardware-visible P6.35 renderer unchanged while moving
# fixed-function shader realization and hot texture decode work out of race frames.
# Only signatures observed by prior runs are persisted/prewarmed; the first run
# seeds the guaranteed P6.35 TEX0/REPLACE state.
PROFILES["full-content-p6_36-direct-ffp-prewarm"] = (
    PROFILES["full-content-p6_35-direct-3d-texture-rescue"] |
    dict(MKW_VITA_DIRECT_FFP_PREWARM=1,
         MKW_VITA_DIRECT_TEXTURE_PRIORITY_PREP=1))

# P6.37 keeps P6.36 behavior but halves both Vita surface axes for debugging.
# Guest Wii viewport/EFB coordinates remain unchanged; only raster resolution drops.
PROFILES["full-content-p6_37-direct-272p-debug"] = (
    PROFILES["full-content-p6_36-direct-ffp-prewarm"] |
    dict(MKW_VITA_INTERNAL_RENDER_WIDTH=480,
         MKW_VITA_INTERNAL_RENDER_HEIGHT=272))

# P6.38 is the performance-first native-resolution build after the 272p A/B
# proved raster fill-rate is not the limiting factor. Remove profiling/prewarm
# overhead, prioritize foreground storage, tighten Yaz0, and compile the latest
# measured guest hotspots at O3 without changing PPC floating-point semantics.
PROFILES["full-content-p6_38-producer-io-yaz0"] = (
    PROFILES["full-content-p6_36-direct-ffp-prewarm"] |
    dict(MKW_VITA_INTERNAL_RENDER_WIDTH=960,
         MKW_VITA_INTERNAL_RENDER_HEIGHT=544,
         MKW_VITA_DIRECT_FFP_PREWARM=0,
         MKW_VITA_FFP_CACHE_RUNTIME_SAVE=0,
         MKW_VITA_BRSAR_PREFETCH_COOPERATIVE=1,
         MKW_VITA_YAZ0_FAST_DIRECT=1,
         MKW_VITA_GUEST_CPU_PROFILE=0,
         MKW_VITA_GUEST_PC_SAMPLER=0,
         MKW_VITA_GUEST_IO_PROFILE=0,
         MKW_VITA_WAIT_SERVICE_PROFILE=0,
         MKW_VITA_AUDIO_WAIT_PROFILE=0,
         MKW_VITA_AUDIO_AI_PROFILE=0,
         MKW_VITA_PERF_RING=0,
         MKW_VITA_PERF_SUMMARY_INTERVAL=120,
         MKW_TRANSLATED_HOT_SHARDS="build_shards/base_common/shard_725a43c460cb362e6c90773f.cpp build_shards/base_common/shard_34d48ae74a14049acdf6da40.cpp build_shards/base_common/shard_dac23cfe7b0b40a5fa626d24.cpp build_shards/base_common/shard_7e4e85bfe552d83be87bec14.cpp build_shards/base_common/shard_5e1324bf49db899095100c5d.cpp build_shards/base_common/shard_25095ab60ee81e785006b467.cpp build_shards/base_common/shard_abfadd8a2b5c92f9a88d68ab.cpp build_shards/base_common/shard_ab968ad22595bad323bed60a.cpp build_shards/base_common/shard_3eb59e491a36d7242d565a06.cpp build_shards/base_common/shard_1ad6c774f8193dc8e5318e40.cpp build_shards/base_common/shard_f03227ec9fbe553ae94880e2.cpp build_shards/base_common/shard_7d45b3eea519ff690d1f6529.cpp build_shards/base_common/shard_9d589652ea7bcaf3bc33c546.cpp build_shards/base_common/shard_60e06428f12fde230bb8717d.cpp build_shards/base_common/shard_230d3b08501d1c84788a7489.cpp build_shards/base_common/shard_cfaa9f5a3062596415aba33d.cpp",
         MKW_TRANSLATED_HOT_OPT="-O3"))

# Astra/Sol 5.6 high plan. Each profile below changes one causal family from
# P6.38. Hardware acceptance remains separate; the integrated profile exists
# only to prove that all implemented paths compile/link together.
PROFILES["full-content-p6_39-f01-critical-off"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_SOL_CRITICAL_PROFILE=0, MKW_VITA_SOL_DETAIL_BURST=0))
PROFILES["full-content-p6_39-f01-critical-on"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_SOL_CRITICAL_PROFILE=1, MKW_VITA_SOL_DETAIL_BURST=1,
         MKW_VITA_GUEST_CPU_PROFILE=1, MKW_VITA_GUEST_PC_SAMPLER=1,
         MKW_VITA_GUEST_ACTIVE_CPU_PROFILE=1))
PROFILES["full-content-p6_40-f02-texture-identity"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_TEXTURE_RAW_CPU_CACHE=1, MKW_VITA_PREP_TEXTURE_LIMIT=16,
         MKW_VITA_PREP_TEXTURE_BUDGET_MB=8))
PROFILES["full-content-p6_40-f02-texture-16m"] = (
    PROFILES["full-content-p6_40-f02-texture-identity"] |
    dict(MKW_VITA_TEXTURE_CACHE_BUDGET_MB=16))
PROFILES["full-content-p6_40-f02-texture-20m"] = (
    PROFILES["full-content-p6_40-f02-texture-identity"] |
    dict(MKW_VITA_TEXTURE_CACHE_BUDGET_MB=20))
PROFILES["full-content-p6_41-f03-efb-lifetime"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_EFB_COMMAND_CAPACITY=1024, MKW_VITA_EFB_EXTENDED_COALESCE=1,
         MKW_VITA_EFB_DEFER_TRANSFER_FINISH=1))
PROFILES["full-content-p6_42-f04-rescue-color"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_RESCUE_KEEP_VERTEX_COLOR=1))
PROFILES["full-content-p6_42-f04-rescue-depth"] = (
    PROFILES["full-content-p6_42-f04-rescue-color"] |
    dict(MKW_VITA_RESCUE_KEEP_DEPTH=1, MKW_VITA_DIRECT_GX_DEPTH_RANGE=1))
PROFILES["full-content-p6_42-f04-rescue-cull"] = (
    PROFILES["full-content-p6_42-f04-rescue-depth"] |
    dict(MKW_VITA_RESCUE_KEEP_CULL=1))
PROFILES["full-content-p6_42-f04-rescue-blend-alpha"] = (
    PROFILES["full-content-p6_42-f04-rescue-cull"] |
    dict(MKW_VITA_RESCUE_KEEP_BLEND_ALPHA=1))
PROFILES["full-content-p6_42-f04-rescue-tev"] = (
    PROFILES["full-content-p6_42-f04-rescue-blend-alpha"] |
    dict(MKW_VITA_RESCUE_KEEP_TEV=1))
PROFILES["full-content-p6_42-f04-native-material"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_DIRECT_3D_TEXTURE_RESCUE=0, MKW_VITA_DIRECT_3D_TEXTURED_COMPAT=0,
         MKW_VITA_DIRECT_GX_DEPTH_RANGE=1))
PROFILES["full-content-p6_43-f05-linear-batching"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_DIRECT_LINEAR_BATCH_PREP=1))
PROFILES["full-content-p6_44-f06-guest-attribution"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_GUEST_CPU_PROFILE=1, MKW_VITA_GUEST_PC_SAMPLER=1,
         MKW_VITA_GUEST_ACTIVE_CPU_PROFILE=1))
PROFILES["full-content-p6_45-f07-prep-ownership"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_VERTEX_REUSE_CACHE=1, MKW_VITA_PACKET_MOVE_STATE=1))
PROFILES["full-content-p6_46-f08-timeline"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_TIMELINE_PROFILE=1))
PROFILES["full-content-p6_47-f09-loading-audio"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_LOADING_AUDIO_PROFILE=1, MKW_VITA_GUEST_IO_PROFILE=1,
         MKW_VITA_AUDIO_WAIT_PROFILE=1, MKW_VITA_AUDIO_AI_PROFILE=1))
PROFILES["full-content-p6_48-sol-plan-integrated"] = (
    PROFILES["full-content-p6_38-producer-io-yaz0"] |
    dict(MKW_VITA_SOL_CRITICAL_PROFILE=1, MKW_VITA_TEXTURE_RAW_CPU_CACHE=1,
         MKW_VITA_PREP_TEXTURE_LIMIT=16, MKW_VITA_PREP_TEXTURE_BUDGET_MB=8,
         MKW_VITA_EFB_COMMAND_CAPACITY=1024, MKW_VITA_EFB_EXTENDED_COALESCE=1,
         MKW_VITA_EFB_DEFER_TRANSFER_FINISH=1, MKW_VITA_DIRECT_LINEAR_BATCH_PREP=1,
         MKW_VITA_GUEST_ACTIVE_CPU_PROFILE=1, MKW_VITA_VERTEX_REUSE_CACHE=1,
         MKW_VITA_PACKET_MOVE_STATE=1, MKW_VITA_TIMELINE_PROFILE=1,
         MKW_VITA_LOADING_AUDIO_PROFILE=1, MKW_VITA_GUEST_IO_PROFILE=1,
         MKW_VITA_AUDIO_WAIT_PROFILE=1, MKW_VITA_AUDIO_AI_PROFILE=1))


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=PROFILES)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--hot-shard", action="append", default=[], help="source path relative to generated/")
    parser.add_argument("--hot-opt", choices=["O2", "O3"], default="O2")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    config = BASE | PROFILES[args.profile]
    suffix = args.profile
    if args.hot_shard:
        for shard in args.hot_shard:
            path = (ROOT / "generated" / shard).resolve()
            if not path.is_relative_to(ROOT / "generated/build_shards") or not path.is_file() or path.suffix != ".cpp":
                parser.error(f"Invalid generated shard: {shard}")
        config["MKW_TRANSLATED_HOT_SHARDS"] = " ".join(args.hot_shard)
        config["MKW_TRANSLATED_HOT_OPT"] = "-" + args.hot_opt
        suffix += "-hot-" + args.hot_opt + "-" + hashlib.sha256("\n".join(args.hot_shard).encode()).hexdigest()[:8]
    target = "wiicompiled-vita-mkw-firstboot-astra-" + suffix
    marker_payload = json.dumps({"profile": suffix, "config": config}, sort_keys=True, separators=(",", ":"))
    config["MKW_VITA_BUILD_MARKER"] = int(hashlib.sha256(marker_payload.encode()).hexdigest()[:8], 16)
    config["MKW_FIRSTBOOT_TARGET"] = target
    config["MKW_VITA_BUILD_VARIANT"] = "astra-" + suffix
    command = ["make", "-f", "Makefile.vita", f"-j{args.jobs}"]
    if args.dry_run:
        command += ["-n"]
    command += [f"{key}={value}" for key, value in config.items()]
    command += ["mkw-first-boot-package", "verify-mkw-firstboot-vpk"]
    env = os.environ.copy()
    env.setdefault("VITASDK", "/usr/local/vitasdk")
    env["PATH"] = env["VITASDK"] + "/bin:" + env.get("PATH", "")
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    if args.dry_run:
        return
    files = [ROOT / "build/vita" / (target + ext) for ext in (".vpk", ".manifest.txt")]
    files += [ROOT / "build/vita/mkwii_runtime" / (target + ".elf")]
    sources = {ROOT / "Makefile.vita", Path(__file__).resolve()}
    for folder in ("vita", "aurora-main/platforms/vita/gfx", "runtime/src", "runtime/include"):
        sources.update(path for path in (ROOT/folder).rglob("*") if path.suffix in (".cpp", ".h", ".hpp", ".inc", ".py"))
    renderer_audit = {}
    if config["MKW_VITA_AURORA_RENDERER"] == 0:
        symbols = subprocess.check_output(
            [env["VITASDK"] + "/bin/arm-vita-eabi-nm", "-C", "--defined-only", str(files[-1])],
            text=True)
        forbidden = [line for line in symbols.splitlines()
                     if "AuroraPacketRenderer" in line or "aurora::vita::gfx::" in line]
        if forbidden:
            raise RuntimeError("Direct VitaGL ELF contains Aurora renderer symbols: " + "\n".join(forbidden[:10]))
        renderer_audit = dict(backend="gx-direct-vitagl", aurora_renderer_symbols=0)
    evidence = dict(config=config, hardware_validated=False, renderer_audit=renderer_audit,
        artifacts={str(path.relative_to(ROOT)): dict(sha256=sha(path), bytes=path.stat().st_size) for path in files},
        source_sha256={str(path.relative_to(ROOT)): sha(path) for path in sorted(sources)},
        git_status=subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True))
    output = ROOT / "build/vita" / (target + ".evidence.json")
    output.write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Package and source evidence: {output}")

if __name__ == "__main__":
    main()
