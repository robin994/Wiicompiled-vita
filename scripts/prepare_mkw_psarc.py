#!/usr/bin/env python3
"""Prepare Mario Kart Wii PAL data as a Vita-friendly PSARC.

The expensive Wii partition decrypt/H0 validation happens once on the host.
The resulting PSARC contains only files referenced by the disc filesystem plus
an index preserving their original decrypted-partition offsets.

The PSARC container itself is game-agnostic (scripts/vita_psarc.py); this file
is deliberately the Mario Kart profile layer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
import tempfile
import os

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from vita_psarc import create_psarc, parse_psarc, verify_psarc, iter_entry, collect_files

CHUNK = 4 * 1024 * 1024
EXPECTED_GAME_ID = "RMCP01"
EXPECTED_EXECUTABLES = {
    "sys/main.dol": "80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05",
    "rel/StaticR.rel": "16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb", buffering=0) as handle:
        while True:
            block = handle.read(CHUNK)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def resolve_disc_tool(explicit: Path | None) -> Path:
    if explicit is not None:
        path = explicit if explicit.is_absolute() else ROOT / explicit
        if path.is_file():
            return path
        raise FileNotFoundError(path)
    candidates = [
        ROOT / "build/host/wiicompiled-disc",
        ROOT / "build/base/wiicompiled-disc",
        ROOT / "build/wiicompiled-disc",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(
        "wiicompiled-disc not found; build a host tree first "
        "(cmake -S . -B build/host && cmake --build build/host --target wiicompiled-disc)"
    )


def inspect_disc(tool: Path, iso: Path) -> dict:
    result = subprocess.run(
        [str(tool), "inspect", str(iso)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    first = result.stdout.splitlines()[0] if result.stdout else ""
    fields: dict[str, str] = {}
    for token in first.split():
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    if fields.get("game_id") != EXPECTED_GAME_ID:
        raise ValueError(f"expected {EXPECTED_GAME_ID}, got {fields.get('game_id', '<unknown>')}")
    return {
        "game_id": fields["game_id"],
        "iso_bytes": int(fields["iso_bytes"]),
        "partition_bytes": int(fields["partition_bytes"]),
        "entries": int(fields["entries"]),
    }


def extract_disc(tool: Path, iso: Path, staging_disc: Path) -> None:
    subprocess.run([str(tool), "extract", str(iso), str(staging_disc)], check=True)


def verify_profile(staging_disc: Path) -> dict:
    verified: dict[str, dict] = {}
    for relative, expected in EXPECTED_EXECUTABLES.items():
        path = staging_disc / relative
        if not path.is_file():
            raise FileNotFoundError(path)
        actual = sha256_file(path)
        if actual != expected:
            raise ValueError(
                f"{relative} differs from translated {EXPECTED_GAME_ID} rev0: {actual}"
            )
        verified[relative] = {"sha256": actual, "bytes": path.stat().st_size}
    index = staging_disc / "index.bin"
    if not index.is_file() or index.stat().st_size < 32:
        raise ValueError("disc/index.bin was not generated")
    return verified


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", nargs="?", type=Path, default=ROOT / "mariokart.iso")
    parser.add_argument("--output", type=Path, default=ROOT / "build/vita-data/mariokart.psarc")
    parser.add_argument("--disc-tool", type=Path)
    parser.add_argument("--replace", action="store_true")
    parser.add_argument("--keep-staging", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    iso = (args.iso if args.iso.is_absolute() else ROOT / args.iso).resolve()
    output = (args.output if args.output.is_absolute() else ROOT / args.output)
    if output.is_symlink():
        raise ValueError("refusing a symlink output")
    output = output.resolve()
    if output == iso:
        raise ValueError("archive cannot replace the source ISO")
    if output.exists() and not args.replace:
        raise FileExistsError("output exists; use --replace: " + str(output))
    if not iso.is_file():
        raise FileNotFoundError(iso)
    tool = resolve_disc_tool(args.disc_tool).resolve()
    probe = tool.with_name("wiicompiled-psarc")
    if not probe.is_file():
        raise FileNotFoundError("native PSARC verification tool missing: " + str(probe))
    disc = inspect_disc(tool, iso)
    output.parent.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    # Only an invocation-owned directory is removed. Existing staging and a
    # previously working archive are never deleted before validation succeeds.
    workspace = Path(tempfile.mkdtemp(prefix="mkw-psarc-", dir=output.parent))
    try:
        input_root = workspace / "inputs"
        staging_disc = input_root / "disc"
        staging_disc.mkdir(parents=True)
        extract_disc(tool, iso, staging_disc)
        profile = verify_profile(staging_disc)
        inventory = []
        for item in collect_files([staging_disc]):
            inventory.append({"path": item.archive_path, "bytes": item.size,
                              "sha256": sha256_file(item.source)})
        extracted_bytes = sum(item["bytes"] for item in inventory)
        candidate = workspace / "candidate.psarc"
        create = create_psarc(candidate, [staging_disc], ignore_case=True, merge_duplicates=True)
        verified = verify_psarc(candidate, full=True)
        parsed = parse_psarc(candidate)
        required = {"disc/index.bin", "disc/sys/main.dol", "disc/sys/fst.bin",
                    "disc/sys/boot.bin", "disc/sys/bi2.bin", "disc/rel/StaticR.rel",
                    "disc/raw/header.bin"}
        missing = {path.casefold() for path in required} - {path.casefold() for path in parsed.paths}
        if missing:
            raise RuntimeError("PSARC missing runtime-critical paths: " + ", ".join(sorted(missing)))
        by_path = {entry["path"]: entry for entry in inventory}
        if set(parsed.paths) != set(by_path):
            raise RuntimeError("source/archive inventory mismatch")
        with candidate.open("rb", buffering=0) as handle:
            for index, path in enumerate(parsed.paths, 1):
                digest = hashlib.sha256()
                for chunk in iter_entry(handle, parsed, index):
                    digest.update(chunk)
                if digest.hexdigest() != by_path[path]["sha256"]:
                    raise RuntimeError("archive bytes differ from source: " + path)
        native = subprocess.run([str(probe), "verify-root", str(candidate), str(input_root)],
                                check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        native_verified = json.loads(native.stdout)
        # candidate.psarc is outside the input root: give the native verifier a
        # root containing only archive inputs, not the candidate itself.
        report = {
            "format_version": 2, "profile": "mkw-rmcp01-rev0", "blocks": "stored",
            "source": {"path": str(iso), "bytes": iso.stat().st_size,
                       "sha256": sha256_file(iso), **disc},
            "verified_executables": profile,
            "staging": {"logical_bytes": extracted_bytes,
                        "iso_overhead_removed_bytes": iso.stat().st_size - extracted_bytes},
            "psarc": {**create, "archive": str(output), "verify": verified,
                      "native_verify": native_verified, "sha256": sha256_file(candidate),
                      "saved_vs_iso_bytes": iso.stat().st_size - candidate.stat().st_size},
            "inventory": inventory, "seconds": time.perf_counter() - start,
        }
        report["psarc"]["verify"]["archive"] = str(output)
        staged_report = workspace / "report.json"
        staged_report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        os.replace(candidate, output)
        os.replace(staged_report, output.with_suffix(".json"))
        saved = report["psarc"]["saved_vs_iso_bytes"]
        print(f"PSARC: {output}\nbytes={output.stat().st_size} saved={saved} "
              f"reduction={100*saved/iso.stat().st_size:.2f}% files={len(inventory)}")
        print(f"SHA256={report['psarc']['sha256']} full source/native-reader verification PASS")
        if args.keep_staging:
            print(f"staging retained at {workspace}")
        return 0
    finally:
        if not args.keep_staging:
            shutil.rmtree(workspace)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"PSARC preparation failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
