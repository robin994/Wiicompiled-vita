#!/usr/bin/env python3
"""Install the CURRENT WiiCompiled Vita build over VitaShell FTP.

Never launches the game, removes game data, writes a PSARC or deploys a sealed
historical candidate. Requires a fresh DELIVERY.json from ./build-vita.sh game.
The process must be closed and VitaShell FTP must be running before deployment.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import ftplib
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parent.parent
APP_ROOT = "ux0:/app/WICMKW001"
CONFIG_PATH = "ux0:/data/wiicompiled-vita/Config.toml"
PSARC_PATH = "ux0:/data/wiicompiled-vita/mariokart.psarc"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def verify_build(folder: Path, *, async_required: bool = True) -> tuple[dict[str, bytes], dict]:
    from record_vita_delivery import source_digest
    from vita_sfo import verify_package_sfo

    delivery_path = folder / "DELIVERY.json"
    delivery = json.loads(delivery_path.read_text(encoding="utf-8"))
    artifacts = next((item for item in delivery["artifacts"]
                      if item["product"] == "wiicompiled-vita-game"), None)
    if artifacts is None:
        raise ValueError("DELIVERY.json lacks wiicompiled-vita-game")
    cmake = delivery["cmake_options"]
    if cmake.get("WIC_BUILD_GAME:BOOL") != "ON":
        raise ValueError("DELIVERY.json does not describe a Vita game build")
    if cmake.get("AURORA_VITA_RENDERER:STRING") != "GXM":
        raise ValueError("Expected the native GXM renderer, not VitaGL")
    if async_required and cmake.get("AURORA_VITA_ASYNC_GX:BOOL") != "ON":
        raise ValueError("AURORA_VITA_ASYNC_GX is OFF; rebuild with the async options shown")
    if cmake.get("WIC_NATIVE_DIRECT_SUBMIT:BOOL", "OFF") != "OFF" or (
            cmake.get("WIC_NATIVE_DIRECT_STREAM:BOOL", "OFF") != "OFF"):
        raise ValueError("P0 baseline requires native direct submit/stream OFF")
    if delivery.get("aurora_pin") != subprocess.check_output(
            ["git", "-C", str(ROOT / "aurora-main"), "rev-parse", "HEAD"],
            text=True).strip():
        raise ValueError("DELIVERY.json was generated against a different Aurora commit")
    if delivery.get("head") != subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip():
        raise ValueError("DELIVERY.json was generated against a different WiiCompiled commit")
    # Both Git pins may match even though M04/M06 or generated translation files
    # changed since the SELF was built. Prevent no-op A/B tests on stale code.
    source_hash,source_count=source_digest(ROOT)
    if (not delivery.get("source_and_generated_sha256") or
            delivery["source_and_generated_sha256"]!=source_hash or
            delivery.get("source_files_hashed")!=source_count):
        raise ValueError("Local source/generated files differ from DELIVERY.json; rebuild Vita before installing")

    expected_files = {Path(item["path"]).resolve(): item["sha256"]
                      for item in artifacts["files"]}
    package = (folder / "wiicompiled-vita-game.vpk").resolve()
    self_file = (folder / "game-eboot.bin").resolve()
    exe = (folder / "wiicompiled-vita-game").resolve()
    for path in (package, self_file, exe):
        if expected_files.get(path) != file_digest(path):
            raise ValueError(f"Stale/incorrect delivery artifact: {path}")
    if delivery_path.stat().st_mtime + 1 < max(package.stat().st_mtime, self_file.stat().st_mtime):
        raise ValueError("DELIVERY.json predates this VPK/SELF: run ./build-vita.sh game")

    with zipfile.ZipFile(package) as z:
        members = [item for item in z.infolist() if not item.is_dir()]
        names = [item.filename for item in members]
        if len(names) != len(set(names)):
            raise ValueError("Duplicate VPK paths")
        for name in names:
            parts = PurePosixPath(name)
            if parts.is_absolute() or ".." in parts.parts or "\\" in name:
                raise ValueError(f"Unsafe VPK path: {name}")
        payloads = {item.filename: z.read(item) for item in members}
    if payloads.get("eboot.bin") is None or payloads.get("sce_sys/param.sfo") is None:
        raise ValueError("Invalid VPK layout")
    verify_package_sfo(payloads["sce_sys/param.sfo"], "WICMKW001")
    if digest(payloads["eboot.bin"]) != file_digest(self_file):
        raise ValueError("VPK eboot.bin does not match generated game-eboot.bin")
    if not artifacts.get("packaged_eboot_matches"):
        raise ValueError("DELIVERY.json did not verify the packaged executable")
    return payloads, delivery


def write_toml_value(data: bytes, section: str, key: str, value: bool | int) -> bytes:
    if type(value) not in (bool, int):
        raise ValueError("Only validated boolean/integer options are supported")
    text = data.decode("utf-8")
    before = tomllib.loads(text)
    section_values = before.get(section, {})
    if not isinstance(section_values, dict):
        raise ValueError(f"Unsupported TOML section: {section}")
    if key in section_values and type(section_values[key]) is not type(value):
        raise ValueError(f"Invalid type: [{section}].{key}")
    if key in section_values and section_values[key] == value:
        return data
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines(keepends=True)
    section_re = re.compile(r"\s*\[" + re.escape(section) + r"\]\s*(?:#.*)?$")
    section_index = next((i for i, line in enumerate(lines)
                          if section_re.fullmatch(line.strip())), None)
    literal = ("true" if value else "false") if type(value) is bool else str(value)
    if section_index is None:
        text = text.rstrip("\r\n") + newline + newline + (
            f"[{section}]{newline}{key} = {literal}{newline}")
    else:
        section_end = next((i for i in range(section_index + 1, len(lines))
                            if re.match(r"\s*\[", lines[i])), len(lines))
        assignment = re.compile(r"^(\s*" + re.escape(key) + r"\s*=\s*)"
                                r"(true|false|[0-9]+)(\s*(?:#.*)?)$")
        key_index = next((i for i in range(section_index + 1, section_end)
                          if re.match(r"\s*" + re.escape(key) + r"\s*=", lines[i])), None)
        if key_index is None:
            if section_end and not lines[section_end - 1].endswith(("\r", "\n")):
                lines[section_end - 1] += newline
            lines.insert(section_end, f"{key} = {literal}{newline}")
        else:
            old = lines[key_index].rstrip("\r\n")
            replaced, count = assignment.subn(lambda m: m[1] + literal + m[3], old)
            if count != 1:
                raise ValueError(f"Unsupported TOML formatting: [{section}].{key}")
            lines[key_index] = replaced + (newline if lines[key_index].endswith(("\r", "\n")) else "")
        text = "".join(lines)
    updated = text.encode("utf-8")
    actual = tomllib.loads(text)
    expected = {**before, section: {**section_values, key: value}}
    if actual != expected or type(actual[section][key]) is not type(value):
        raise ValueError("TOML update changed unrelated configuration")
    return updated


def configure(data: bytes, *, menu: bool, quiet: bool, legacy_audio: bool,
              audio_only: bool = False, batching_only: bool = False,
              draw_batching: bool | None = None,
              capture_frame: int | None = None,
              probe_only: bool = False,
              render_budget_probe: bool | None = None,
              visual_only: bool = False,
              gpu_lighting: bool | None = None,
              native_materials: bool | None = None,
              precomputed_only: bool = False,
              precomputed_geometry: bool | None = None,
              efb_only: bool = False,
              async_efb_copies: bool | None = None,
              deferred_efb_targets: bool | None = None,
              recipe_only: bool = False,
              prepared_display_lists: bool | None = None,
              lyt_only: bool = False,
              native_lyt_packets: bool | None = None) -> bytes:
    if lyt_only:
        if native_lyt_packets is None:
            raise ValueError("lyt_only requires an explicit native_lyt_packets value")
        return write_toml_value(data, "vita", "native_lyt_packets", native_lyt_packets)
    if recipe_only:
        if prepared_display_lists is None:
            raise ValueError("recipe_only requires an explicit prepared_display_lists value")
        return write_toml_value(data, "vita", "prepared_display_lists", prepared_display_lists)
    if efb_only:
        if (async_efb_copies is None) == (deferred_efb_targets is None):
            raise ValueError("efb_only requires exactly one EFB option")
        return write_toml_value(data, "vita",
                                "async_efb_copies" if async_efb_copies is not None
                                else "deferred_efb_targets",
                                async_efb_copies if async_efb_copies is not None
                                else deferred_efb_targets)
    if precomputed_only:
        if precomputed_geometry is None:
            raise ValueError("precomputed_only requires an explicit precomputed_geometry value")
        return write_toml_value(data, "vita", "precomputed_geometry", precomputed_geometry)
    if visual_only:
        if (gpu_lighting is None) == (native_materials is None):
            raise ValueError("visual_only requires exactly one graphics control")
        return write_toml_value(data, "vita",
                                "gpu_lighting" if gpu_lighting is not None else "native_materials",
                                gpu_lighting if gpu_lighting is not None else native_materials)
    if probe_only:
        if render_budget_probe is None:
            raise ValueError("probe_only requires an explicit render_budget_probe value")
        return write_toml_value(data, "vita", "render_budget_probe", render_budget_probe)
    if batching_only:
        if draw_batching is None:
            raise ValueError("batching_only requires an explicit draw_batching value")
        return write_toml_value(data, "vita", "draw_batching", draw_batching)
    if not audio_only:
        data = write_toml_value(data, "test", "time_trial", not menu)
        data = write_toml_value(data, "diagnostics", "enabled", not quiet)
        if capture_frame is not None:
            data = write_toml_value(data, "diagnostics", "capture_frame", capture_frame)
        if draw_batching is not None:
            data = write_toml_value(data, "vita", "draw_batching", draw_batching)
        if render_budget_probe is not None:
            data = write_toml_value(data, "vita", "render_budget_probe", render_budget_probe)
        if gpu_lighting is not None:
            data = write_toml_value(data, "vita", "gpu_lighting", gpu_lighting)
        if native_materials is not None:
            data = write_toml_value(data, "vita", "native_materials", native_materials)
        if precomputed_geometry is not None:
            data = write_toml_value(data, "vita", "precomputed_geometry", precomputed_geometry)
        if prepared_display_lists is not None:
            data = write_toml_value(data, "vita", "prepared_display_lists", prepared_display_lists)
        if native_lyt_packets is not None:
            data = write_toml_value(data, "vita", "native_lyt_packets", native_lyt_packets)
        if async_efb_copies is not None:
            data = write_toml_value(data, "vita", "async_efb_copies", async_efb_copies)
        if deferred_efb_targets is not None:
            data = write_toml_value(data, "vita", "deferred_efb_targets", deferred_efb_targets)
    return write_toml_value(data, "audio", "legacy_stop_dma_on_queue_full", legacy_audio)


def read_remote(ftp, path: str) -> bytes:
    buffer = io.BytesIO()
    ftp.retrbinary("RETR " + path, buffer.write, blocksize=1024 * 1024)
    return buffer.getvalue()


def remote_metadata(ftp, path: str) -> dict:
    ftp.voidcmd("TYPE I")
    return {"size": ftp.size(path), "mtime": ftp.sendcmd("MDTM " + path)}


def activate(ftp, changes: list[tuple[str, bytes]], originals: dict[str, bytes],
             stamp: str) -> dict[str, str]:
    """Stage/verify every file before altering any original, then roll back on failure."""
    staging: dict[str, str] = {}
    moved: list[tuple[str, str]] = []
    try:
        for path, data in changes:
            temporary = path + ".upload-" + stamp
            ftp.storbinary("STOR " + temporary, io.BytesIO(data), blocksize=1024 * 1024)
            if digest(read_remote(ftp, temporary)) != digest(data):
                raise RuntimeError("Staging hash mismatch: " + path)
            staging[path] = temporary
        for path, content in originals.items():
            if digest(read_remote(ftp, path)) != digest(content):
                raise RuntimeError("Source changed during transfer: " + path)
        for path, data in changes:
            backup = path + ".backup-" + stamp
            ftp.rename(path, backup)
            moved.append((path, backup))
            ftp.rename(staging[path], path)
            if digest(read_remote(ftp, path)) != digest(data):
                raise RuntimeError("Installed content differs: " + path)
        return dict(moved)
    except Exception as failed:
        errors = []
        for path, backup in reversed(moved):
            try:
                ftp.rename(path, path + ".failed-" + stamp)
            except ftplib.error_perm as error:
                if not str(error).startswith("550"):
                    errors.append(str(error))
            try:
                ftp.rename(backup, path)
            except Exception as error:
                errors.append(f"{backup}: {error}")
        if errors:
            raise RuntimeError("Automatic rollback failed; restore manually: "
                               + "; ".join(errors)) from failed
        raise
    finally:
        # Safe to leave failed temporary files on Vita for manual inspection.
        pass


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/vita-psarc")
    parser.add_argument("--host", default="192.168.1.79")
    parser.add_argument("--port", type=int, default=1337)
    parser.add_argument("--check-local", action="store_true",
                        help="Verify latest build provenance, VPK, SELF and SFO; no FTP")
    parser.add_argument("--dry-run", action="store_true",
                        help="Verify device state and planned changes without uploads")
    parser.add_argument("--audio-only", action="store_true",
                        help="Change only the queue-full audio policy; keep menu/diagnostics intact")
    parser.add_argument("--batch-only", action="store_true",
                        help="Change only [vita] draw_batching; SELF must already match")
    parser.add_argument("--probe-only", action="store_true",
                        help="Change only [vita] render_budget_probe; SELF must already match")
    parser.add_argument("--visual-only", action="store_true",
                        help="Change exactly one shader/lighting option; SELF must already match")
    parser.add_argument("--precomputed-only", action="store_true",
                        help="Change only [vita] precomputed_geometry; SELF must already match")
    parser.add_argument("--recipe-only", action="store_true",
                        help="Change only [vita] prepared_display_lists; SELF must already match")
    parser.add_argument("--lyt-only", action="store_true",
                        help="Change only [vita] native_lyt_packets; SELF must already match")
    parser.add_argument("--prepared-display-lists", choices=("keep", "on", "off"), default="keep",
                        help="A/B test immutable multi-draw native NW4r shape-list recipes")
    parser.add_argument("--native-lyt-packets", choices=("keep", "on", "off"), default="keep",
                        help="A/B test native NW4r UI quads on the async GX consumer")
    parser.add_argument("--efb-only", action="store_true",
                        help="Change exactly one EFB experiment; SELF must already match")
    parser.add_argument("--async-efb-copies", choices=("keep", "on", "off"), default="keep",
                        help="Avoid the redundant GXDrawDone before queued EFB texture copies")
    parser.add_argument("--deferred-efb-targets", choices=("keep", "on", "off"), default="keep",
                        help="Retire/reuse EFB render targets only after GPU scene completion")
    parser.add_argument("--precomputed-geometry", choices=("keep", "on", "off"), default="keep",
                        help="Compare immutable precomputed GXM geometry using the same SELF")
    parser.add_argument("--gpu-lighting", choices=("keep", "gpu", "cpu"), default="keep",
                        help="Compare native GXM lighting against conservative CPU fallback")
    parser.add_argument("--native-materials", choices=("keep", "native", "reference"), default="keep",
                        help="Compare native material shader generation against TEV reference")
    parser.add_argument("--budget-probe", choices=("keep", "on", "off"), default="keep",
                        help="Sparse native GX/GXM/EFB timing, independent of full diagnostics")
    parser.add_argument("--render-batching", choices=("keep", "on", "off"), default="keep",
                        help="Default keep; opt-in experimental GXM pending-draw batching")
    parser.add_argument("--capture-frame", type=int, default=None,
                        help="Single diagnostic screenshot at an explicit completed frame")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--menu", action="store_true", help="Manual 1 Player (default)")
    group.add_argument("--time-trial", action="store_true",
                       help="Automatic Mario/Luigi Circuit Time Trial")
    parser.add_argument("--audio-policy", choices=("continue", "legacy"), default="continue")
    parser.add_argument("--diagnostics", choices=("quiet", "enabled"), default="quiet")
    args = parser.parse_args(argv)
    if args.lyt_only and (
            args.native_lyt_packets == "keep" or args.recipe_only or args.audio_only or
            args.batch_only or args.probe_only or args.visual_only or args.precomputed_only or
            args.efb_only or args.menu or args.time_trial or args.render_batching != "keep" or
            args.budget_probe != "keep" or args.async_efb_copies != "keep" or
            args.deferred_efb_targets != "keep" or args.precomputed_geometry != "keep" or
            args.prepared_display_lists != "keep" or args.gpu_lighting != "keep" or
            args.native_materials != "keep" or args.capture_frame is not None or
            args.diagnostics != "quiet" or args.audio_policy != "continue"):
        parser.error("--lyt-only requires --native-lyt-packets on/off and no unrelated settings")
    if args.native_lyt_packets != "keep" and (
            args.audio_only or args.batch_only or args.probe_only or args.visual_only or
            args.precomputed_only or args.efb_only or args.recipe_only):
        parser.error("Other partial modes preserve native_lyt_packets")
    if args.recipe_only and (
            args.lyt_only or args.prepared_display_lists == "keep" or args.audio_only or args.batch_only or
            args.probe_only or args.visual_only or args.precomputed_only or args.efb_only or
            args.menu or args.time_trial or args.render_batching != "keep" or
            args.budget_probe != "keep" or args.async_efb_copies != "keep" or
            args.deferred_efb_targets != "keep" or args.precomputed_geometry != "keep" or
            args.gpu_lighting != "keep" or args.native_materials != "keep" or
            args.capture_frame is not None or args.diagnostics != "quiet" or
            args.audio_policy != "continue"):
        parser.error("--recipe-only requires --prepared-display-lists on/off and no unrelated settings")
    if args.prepared_display_lists != "keep" and (
            args.audio_only or args.batch_only or args.probe_only or
            args.visual_only or args.precomputed_only or args.efb_only):
        parser.error("Other partial modes preserve prepared_display_lists")
    changed_efb = (args.async_efb_copies != "keep") + (args.deferred_efb_targets != "keep")
    config_only = (args.audio_only or args.batch_only or args.probe_only or
                   args.visual_only or args.precomputed_only or args.efb_only or
                   args.recipe_only or args.lyt_only)
    if args.efb_only and (
            changed_efb != 1 or args.precomputed_geometry != "keep" or
            args.render_batching != "keep" or args.budget_probe != "keep" or
            args.gpu_lighting != "keep" or args.native_materials != "keep" or
            args.capture_frame is not None or args.menu or args.time_trial or
            args.diagnostics != "quiet" or args.audio_policy != "continue" or
            args.audio_only or args.batch_only or args.probe_only or
            args.visual_only or args.precomputed_only):
        parser.error("--efb-only requires exactly one --async-efb-copies/--deferred-efb-targets control")
    if changed_efb and config_only and not args.efb_only:
        parser.error("Other config-only modes preserve EFB experiment options")
    changed_visual = (args.gpu_lighting != "keep") + (args.native_materials != "keep")
    if args.precomputed_only and (
            args.precomputed_geometry == "keep" or args.audio_only or args.batch_only or
            args.probe_only or args.visual_only or args.efb_only or changed_visual or
            args.render_batching != "keep" or args.budget_probe != "keep" or
            args.capture_frame is not None or args.menu or args.time_trial or
            args.diagnostics != "quiet" or args.audio_policy != "continue"):
        parser.error("--precomputed-only requires --precomputed-geometry on/off and no other controls")
    if args.precomputed_geometry != "keep" and (
            args.audio_only or args.batch_only or args.probe_only or
            args.visual_only or args.efb_only):
        parser.error("Other config-only modes preserve precomputed_geometry")
    if args.visual_only and (changed_visual != 1 or args.batch_only or args.probe_only or
                             args.audio_only or args.render_batching != "keep" or
                             args.budget_probe != "keep"):
        parser.error("--visual-only requires exactly one --gpu-lighting/--native-materials control")
    if not args.visual_only and (args.batch_only or args.probe_only or args.audio_only) and changed_visual:
        parser.error("Config-only modes preserve all unrelated shader/lighting controls")
    if args.batch_only and (args.audio_only or args.render_batching == "keep"):
        parser.error("--batch-only requires --render-batching on/off, not --audio-only")
    if args.probe_only and (args.audio_only or args.batch_only or
                            args.budget_probe == "keep" or args.render_batching != "keep"):
        parser.error("--probe-only requires --budget-probe on/off and no other partial mode")
    if args.batch_only and args.budget_probe != "keep":
        parser.error("--batch-only preserves all other rendering options")
    if args.audio_only and args.render_batching != "keep":
        parser.error("--audio-only preserves rendering; omit --render-batching")
    if args.audio_only and args.budget_probe != "keep":
        parser.error("--audio-only preserves rendering; omit --budget-probe")
    if args.capture_frame is not None and (
            args.capture_frame < 1 or args.capture_frame > 100000 or
            args.diagnostics != "enabled" or args.batch_only or args.audio_only or
            args.probe_only or args.visual_only or args.efb_only):
        parser.error("--capture-frame requires --diagnostics enabled, frame 1..100000")

    build = args.build_dir.resolve()
    payloads, delivery = verify_build(build)
    new_self = payloads["eboot.bin"]
    new_sha = digest(new_self)
    print("Build verified: WiiCompiled", delivery["head"][:12],
          "Aurora", delivery["aurora_pin"][:12], flush=True)
    print("SELF:", new_sha, "bytes:", len(new_self), "profile: async GXM",
          flush=True)
    if args.check_local:
        return 0

    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = build / "device-installs" / stamp
    output.mkdir(parents=True, exist_ok=False)
    record: dict = {"utc": stamp, "host": args.host, "port": args.port,
                    "self_sha256": new_sha, "git_commit": delivery["head"],
                    "aurora_commit": delivery["aurora_pin"],
                    "mode": "preserved" if (
                        args.audio_only or args.batch_only or args.probe_only or
                        args.visual_only or args.precomputed_only or args.efb_only or
                        args.recipe_only or args.lyt_only) else ("time-trial" if args.time_trial else "menu"),
                    "diagnostics": args.diagnostics,
                    "audio_policy": args.audio_policy,
                    "audio_only": args.audio_only, "batch_only": args.batch_only,
                    "probe_only": args.probe_only, "budget_probe": args.budget_probe,
                    "visual_only": args.visual_only,
                    "precomputed_only": args.precomputed_only,
                    "precomputed_geometry": args.precomputed_geometry,
                    "recipe_only": args.recipe_only,
                    "prepared_display_lists": args.prepared_display_lists,
                    "lyt_only": args.lyt_only,"native_lyt_packets": args.native_lyt_packets,
                    "efb_only": args.efb_only,"async_efb_copies": args.async_efb_copies,
                    "deferred_efb_targets": args.deferred_efb_targets,
                    "gpu_lighting": args.gpu_lighting, "native_materials": args.native_materials,
                    "render_batching": args.render_batching,
                    "capture_frame": args.capture_frame, "dry_run": args.dry_run,
                    "launched": False}
    with ftplib.FTP() as ftp:
        ftp.connect(args.host, args.port, timeout=30)
        ftp.login()
        ftp.set_pasv(True)
        prior_self = read_remote(ftp, APP_ROOT + "/eboot.bin")
        if (args.batch_only or args.probe_only or args.visual_only or
                args.precomputed_only or args.efb_only or args.recipe_only or
                args.lyt_only) and prior_self != new_self:
            raise RuntimeError("Config-only mode requires the current SELF to be installed first")
        prior_config = read_remote(ftp, CONFIG_PATH)
        original_files = {APP_ROOT + "/eboot.bin": prior_self, CONFIG_PATH: prior_config}
        (output / "prior-eboot.bin").write_bytes(prior_self)
        (output / "Config.before.toml").write_bytes(prior_config)
        for name, content in payloads.items():
            if name == "eboot.bin":
                continue
            if read_remote(ftp, APP_ROOT + "/" + name) != content:
                raise RuntimeError("Installed resource differs from VPK: " + name
                                   + "; install full VPK using VitaShell first")
        psarc_before = remote_metadata(ftp, PSARC_PATH)
        next_config = configure(prior_config, menu=not args.time_trial,
                                quiet=args.diagnostics == "quiet",
                                legacy_audio=args.audio_policy == "legacy",
                                audio_only=args.audio_only,
                                batching_only=args.batch_only,
                                probe_only=args.probe_only,
                                visual_only=args.visual_only,
                                draw_batching=None if args.render_batching == "keep"
                                    else args.render_batching == "on",
                                render_budget_probe=None if args.budget_probe == "keep"
                                    else args.budget_probe == "on",
                                gpu_lighting=None if args.gpu_lighting == "keep"
                                    else args.gpu_lighting == "gpu",
                                native_materials=None if args.native_materials == "keep"
                                    else args.native_materials == "native",
                                precomputed_only=args.precomputed_only,
                                precomputed_geometry=None if args.precomputed_geometry == "keep"
                                    else args.precomputed_geometry == "on",
                                recipe_only=args.recipe_only,
                                prepared_display_lists=None if args.prepared_display_lists == "keep"
                                    else args.prepared_display_lists == "on",
                                lyt_only=args.lyt_only,
                                native_lyt_packets=None if args.native_lyt_packets == "keep"
                                    else args.native_lyt_packets == "on",
                                efb_only=args.efb_only,
                                async_efb_copies=None if args.async_efb_copies == "keep"
                                    else args.async_efb_copies == "on",
                                deferred_efb_targets=None if args.deferred_efb_targets == "keep"
                                    else args.deferred_efb_targets == "on",
                                capture_frame=args.capture_frame)
        (output / "Config.next.toml").write_bytes(next_config)
        changes: list[tuple[str, bytes]] = []
        if prior_self != new_self:
            changes.append((APP_ROOT + "/eboot.bin", new_self))
        if prior_config != next_config:
            changes.append((CONFIG_PATH, next_config))
        record.update({"installed_old_self_sha256": digest(prior_self),
                       "config_before_sha256": digest(prior_config),
                       "config_after_sha256": digest(next_config),
                       "psarc_before": psarc_before,
                       "files_to_update": [path for path, _ in changes],
                       "other_vpk_entries_unchanged": len(payloads) - 1})
        if args.dry_run:
            print("DRY RUN: would update:", record["files_to_update"], flush=True)
        else:
            print("Writing staged files (do not run WiiCompiled during install)...", flush=True)
            backups = activate(ftp, changes, original_files, stamp)
            record["remote_backups"] = backups
            if digest(read_remote(ftp, APP_ROOT + "/eboot.bin")) != new_sha:
                raise RuntimeError("Final installed SELF hash mismatch")
            if read_remote(ftp, CONFIG_PATH) != next_config:
                raise RuntimeError("Final configuration mismatch")
        if remote_metadata(ftp, PSARC_PATH) != psarc_before:
            raise RuntimeError("PSARC metadata unexpectedly changed")
        record["state"] = "dry_run_checked" if args.dry_run else "installed_verified"
        (output / "INSTALL.json").write_text(json.dumps(record, indent=2) + "\n",
                                             encoding="utf-8")
    print("SUCCESS:", record["state"], "SELF", new_sha, flush=True)
    print("Backup/receipt:", output, flush=True)
    print("Close WiiCompiled from LiveArea, then launch it from LiveArea manually.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
