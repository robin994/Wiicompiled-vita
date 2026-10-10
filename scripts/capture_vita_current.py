#!/usr/bin/env python3
"""Capture a WiiCompiled Vita test session after closing the game and opening FTP.

Read-only on the device; associates the latest log session with the installed
SELF, Config.toml and PSARC metadata. Outputs under ignored build/ directory.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import ftplib
import hashlib
import io
import json
from pathlib import Path
import re

from install_vita_current import APP_ROOT, CONFIG_PATH, PSARC_PATH, ROOT
from install_vita_current import digest, read_remote, remote_metadata


def latest_boot(log: bytes) -> bytes:
    marker = b"[boot] build="
    offset = log.rfind(marker)
    if offset < 0:
        raise ValueError("No complete [boot] build marker in this log tail; increase --tail-mb")
    # The prefix contains the earlier session(s), or the remainder of a prior
    # incomplete log line if FTP REST started in the middle of a line.
    if offset and log[offset - 1] not in (10, 13):
        raise ValueError("Latest boot marker is not line-aligned; increase --tail-mb")
    return log[offset:]

def verify_frame_ppm(session: bytes, frame: int, screenshot: bytes) -> bytes:
    expected = f"[frame-capture] frame={frame} saved=1".encode()
    if expected not in session:
        raise ValueError("Current boot does not confirm the requested frame capture;"
                         " refusing a stale screenshot from a previous run")
    header = b"P6\n960 544\n255\n"
    if not screenshot.startswith(header) or len(screenshot) != len(header) + 960*544*3:
        raise ValueError("Captured PPM has invalid dimensions or byte length")
    return screenshot


def stream_sha256(ftp, path: str) -> tuple[str, int]:
    value = hashlib.sha256()
    size = 0

    def consume(chunk: bytes) -> None:
        nonlocal size
        value.update(chunk)
        size += len(chunk)

    ftp.retrbinary("RETR " + path, consume, blocksize=1024 * 1024)
    return value.hexdigest(), size


def fetch_log_tail(ftp, *, tail_mb: int) -> tuple[bytes, int, int]:
    path = "ux0:/data/wiicompiled-vita/runtime.log"
    ftp.voidcmd("TYPE I")
    file_size = ftp.size(path)
    offset = max(0, file_size - tail_mb * 1024 * 1024)
    output = io.BytesIO()
    ftp.retrbinary("RETR " + path, output.write, blocksize=1024 * 1024,
                   rest=offset if offset else None)
    return output.getvalue(), file_size, offset


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.1.79")
    parser.add_argument("--port", type=int, default=1337)
    parser.add_argument("--label", required=True, help="menu-continue, menu-legacy, time-trial")
    parser.add_argument("--tail-mb", type=int, default=8)
    parser.add_argument("--frame-ppm", type=int,
                        help="Fetch a verified screenshot from the latest boot only")
    parser.add_argument("--output-root", type=Path,
                        default=ROOT / "build/vita-psarc/device-runs")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[a-zA-Z0-9_-]{1,48}", args.label):
        parser.error("--label must contain only letters, digits, underscores or hyphens")
    if not 1 <= args.tail_mb <= 32:
        parser.error("--tail-mb must be between 1 and 32")
    if args.frame_ppm is not None and not 1 <= args.frame_ppm <= 100000:
        parser.error("--frame-ppm must be between 1 and 100000")

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = args.output_root / (args.label + "-" + stamp)
    output.mkdir(parents=True, exist_ok=False)
    with ftplib.FTP() as ftp:
        ftp.connect(args.host, args.port, timeout=30)
        ftp.login()
        ftp.set_pasv(True)
        config = read_remote(ftp, CONFIG_PATH)
        self_hash, self_size = stream_sha256(ftp, APP_ROOT + "/eboot.bin")
        psarc = remote_metadata(ftp, PSARC_PATH)
        remote_log = remote_metadata(ftp, "ux0:/data/wiicompiled-vita/runtime.log")
        tail, log_size, offset = fetch_log_tail(ftp, tail_mb=args.tail_mb)
        session = latest_boot(tail)
        screenshot = None
        if args.frame_ppm is not None:
            expected = f"[frame-capture] frame={args.frame_ppm} saved=1".encode()
            if expected not in session:
                raise ValueError("Current boot does not confirm the requested frame capture;"
                                 " refusing a stale screenshot from a previous run")
            screenshot = verify_frame_ppm(session,args.frame_ppm,read_remote(
                ftp,f"ux0:/data/wiicompiled-vita/aurora/diagnostics/frame-{args.frame_ppm}.ppm"))
    (output / "Config.toml").write_bytes(config)
    (output / "runtime-session.log").write_bytes(session)
    if screenshot is not None:
        (output / f"frame-{args.frame_ppm}.ppm").write_bytes(screenshot)
    record = {
        "label": args.label, "created_utc": datetime.now(timezone.utc).isoformat(),
        "device": f"{args.host}:{args.port}", "title_id": "WICMKW001",
        "installed_self_sha256": self_hash, "installed_self_bytes": self_size,
        "config_sha256": digest(config), "psarc": psarc,
        "remote_runtime_log": remote_log, "remote_log_bytes": log_size,
        "rest_offset": offset, "captured_latest_session_bytes": len(session),
        "boot_line": session.split(b"\n", 1)[0].decode("utf-8", errors="replace"),
        "capture_only": True,
    }
    if screenshot is not None:
        record["screenshot"] = {"frame": args.frame_ppm,
                                "bytes": len(screenshot),
                                "sha256": digest(screenshot)}
    (output / "CAPTURE.json").write_text(json.dumps(record, indent=2) + "\n",
                                         encoding="utf-8")
    print("Read-only capture:", output)
    print("SELF:", self_hash, "bytes:", self_size, "PSARC:", psarc)
    print(record["boot_line"])
    print("Profile windows:", session.count(b"[runtime-profile]"),
          "cadence windows:", session.count(b"[runtime-cadence]"),
          "GX stalls:", session.count(b"[gx-stall]"),
          "guest stalls:", session.count(b"[guest-stall]"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
