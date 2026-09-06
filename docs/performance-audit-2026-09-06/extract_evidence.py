#!/usr/bin/env python3
"""Extract this audit's append-log sessions; never merge serials across boots."""
import csv
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
LOG = ROOT / "build/vita/runtime.log"
raw = LOG.read_bytes()
lines = raw.decode("utf-8", errors="replace").splitlines()
starts = [i for i, line in enumerate(lines) if "[BOOT] WiiCompiled Vita runtime start" in line]
assert starts, "No boot marker; refuse to guess the session"

def fields(line):
    return dict(re.findall(r"([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)", line))

sessions = []
for n, start in enumerate(starts):
    end = starts[n + 1] if n + 1 < len(starts) else len(lines)
    init = next((i for i in range(start, end) if "init_marker=vglInitExtended phase=begin" in lines[i]), None)
    sessions.append({"boot": n + 1, "start_line": start + 1, "end_line": end,
                     "init_line": init + 1 if init is not None else None,
                     "flags": fields(lines[init]) if init is not None else {}})

start = starts[-1]
event_names = ["producer_frame", "perf_summary", "resource_summary", "gx_cpu_perf", "vi_perf",
               "m12_1_tex_fail", "m12_5_efb_budget", "raw_decode_fail", "movie: open",
               "movie: prepare", "thp: decode_enter", "thp: decode_error", "thp: native decode"]
events = []
for i in range(start, len(lines)):
    line = lines[i]
    for name in event_names:
        if name in line:
            # Concurrent log writes can repeat a prefix; retain the last complete instance.
            tail = line[line.rfind(name):]
            events.append({"line": i + 1, "kind": name, "fields": fields(tail), "text": line})
            break
    else:
        if "render_us=" in line and "perf_summary" not in line:
            events.append({"line": i + 1, "kind": "orphan_render_fragment",
                           "fields": fields(line), "text": line,
                           "note": "No serial in source line: temporal association only, not a joined frame."})

producers = [e for e in events if e["kind"] == "producer_frame"]
tail = [e for e in producers if 1655 <= int(e["fields"]["producer_frame"]) <= 1691]
tail_summary = {"serial_range": [1655, 1691], "samples": len(tail)}
for name, calc in {
    "interval_us": lambda f: int(f["interval_us"]),
    "wait_gx_us": lambda f: int(f["wait_gx"].split("/")[1]),
    "residual_interval_minus_all_waits_us": lambda f: int(f["interval_us"]) - int(f["prior_wait_us"]),
    "packet_copy_us": lambda f: int(f["packet_copy_us"]),
    "queue_wait_us": lambda f: int(f["queue_wait_us"]),
}.items():
    values = [calc(e["fields"]) for e in tail]
    tail_summary[name] = {"min": min(values), "median": statistics.median(values),
                          "max": max(values), "mean": statistics.mean(values)}
tail_summary["effective_producer_hz"] = 1_000_000 / tail_summary["interval_us"]["mean"]

with (OUT / "producer-frames.csv").open("w", newline="") as dest:
    cols = ["line", "serial", "interval_us", "wait_gx_us", "residual_interval_minus_all_waits_us",
            "queue_wait_us", "packet_copy_us", "draws", "vertices", "efb_cmds",
            "efb_calls", "efb_recorded", "efb_destroy", "efb_cap_fail", "raw_cap", "dropped"]
    writer = csv.DictWriter(dest, fieldnames=cols)
    writer.writeheader()
    for e in producers:
        f = e["fields"]
        row = {key: f.get(key, "") for key in cols}
        row.update(line=e["line"], serial=f["producer_frame"],
                   wait_gx_us=f["wait_gx"].split("/")[1],
                   residual_interval_minus_all_waits_us=int(f["interval_us"]) - int(f["prior_wait_us"]))
        writer.writerow(row)

evidence_file = ROOT / "build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.evidence.json"
build_evidence = json.loads(evidence_file.read_text())
source_checks = []
for path, expected in build_evidence["source_sha256"].items():
    source = ROOT / path
    actual = hashlib.sha256(source.read_bytes()).hexdigest() if source.is_file() else None
    source_checks.append({"path": path, "expected": expected, "actual": actual, "match": actual == expected})

artifacts = []
for path in ["build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.vpk",
             "build/vita/mkwii_runtime/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.elf",
             "build/vita/wiicompiled-vita-mkw-firstboot-astra-full-content-3d.manifest.txt"]:
    data = (ROOT / path).read_bytes()
    artifacts.append({"path": path, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})

evidence = {"log": {"path": str(LOG), "bytes": len(raw), "lines": len(lines),
                     "sha256": hashlib.sha256(raw).hexdigest(), "last_line": lines[-1]},
            "sessions": sessions, "latest_events": events, "heavy_tail": tail_summary,
            "artifacts_on_disk": artifacts, "build_source_checks": source_checks,
            "artifact_attribution": "Latest flags and packet size match full-content-3d; the log has no embedded build hash.",
            "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "git_status_at_extraction": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
            "limitations": ["Sparse/threshold-selected samples, not an unbiased frame-time distribution.",
                            "gx_cpu_perf.frame is a different sequence from producer_frame/perf_summary.serial.",
                            "Producer interval and renderer time overlap; never add them as independent CPU/GPU costs.",
                            "One orphan render fragment has no serial; do not silently label it serial 600."]}
(OUT / "evidence.json").write_text(json.dumps(evidence, indent=2, ensure_ascii=False) + "\n")
(OUT / "latest-session.log").write_text("\n".join(lines[start:]) + "\n")
print(json.dumps({"sessions": sessions, "heavy_tail": tail_summary,
                  "source_mismatches": [x["path"] for x in source_checks if not x["match"]],
                  "artifacts": artifacts}, indent=2))
