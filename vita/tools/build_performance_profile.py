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
    MKW_VITA_TEXTURE_SAFE_RETRY=0,
)
PROFILES = {
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
    for folder in ("vita", "aurora-main/platforms/vita/gfx", "runtime/src/hle/gx"):
        sources.update(path for path in (ROOT/folder).rglob("*") if path.suffix in (".cpp", ".h", ".hpp", ".py"))
    evidence = dict(config=config, hardware_validated=False,
        artifacts={str(path.relative_to(ROOT)): dict(sha256=sha(path), bytes=path.stat().st_size) for path in files},
        source_sha256={str(path.relative_to(ROOT)): sha(path) for path in sorted(sources)},
        git_status=subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True))
    output = ROOT / "build/vita" / (target + ".evidence.json")
    output.write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Package and source evidence: {output}")

if __name__ == "__main__":
    main()
