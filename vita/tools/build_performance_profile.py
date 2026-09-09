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
    MKW_VITA_DIRECT_PREP_WORKER=0,
    MKW_VITA_DIRECT_VERTEX_PREP=0, MKW_VITA_DIRECT_TEXTURE_PREP=0,
    MKW_VITA_DIRECT_STATE_PREP=0, MKW_VITA_DIRECT_TEXTURE_CACHE_ANTITHRASH=0,
    MKW_VITA_GUEST_IO_PROFILE=0,
    MKW_VITA_FULLSCREEN_PRESENT=0, MKW_VITA_DVD_HOST_BUFFERING=0,
    MKW_VITA_DIRECT_EFB_BATCH_SYNC=0,
    MKW_VITA_RENDER_DECOUPLE=0, MKW_VITA_RENDER_TARGET_HZ=60,
    MKW_VITA_DIRECT_WORKER_TIMING=0,
    MKW_VITA_DIRECT_PRESENT_30HZ=0, MKW_VITA_DIRECT_DECOUPLED_PRESENT=0,
    MKW_VITA_GUEST_CPU_PROFILE=0, MKW_VITA_GUEST_PC_SAMPLER=0,
    MKW_VITA_GUEST_PC_SAMPLE_US=2000, MKW_VITA_BUFFERED_LOGGING=0,
    MKW_VITA_LOG_FLUSH_INTERVAL_US=250000, MKW_VITA_AUDIO_WAIT_MIN_INTERVAL_US=0,
    MKW_VITA_AUDIO_BACKLOG_CLAMP_BLOCKS=0, MKW_VITA_AX_NEON=0,
    MKW_VITA_THP_UNESCAPED_FIX=0, MKW_VITA_AUDIO_WAIT_BLOCK_BUDGET=4,
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
