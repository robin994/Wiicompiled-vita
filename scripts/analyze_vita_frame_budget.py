#!/usr/bin/env python3
"""Measure WiiCompiled Vita frame budgets from preserved runtime logs.

A runtime-profile or runtime-cadence window is measured wall time, not a GPU
timer. FIFO and native request counters are nested within that wall time and
must never be added to it. A quiet cadence does not report those sub-timers.
Use matching SELF/config/scene captures for controlled A/B comparisons.
"""
from __future__ import annotations

import argparse
from bisect import bisect_right
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import re
import sys

TARGET_FPS = 60.0
TARGET_US = 1_000_000.0 / TARGET_FPS
_FIELDS = re.compile(r"([a-zA-Z_][a-zA-Z_0-9]*)=([^\s]+)")


@dataclass(frozen=True)
class FrameWindow:
    end_frame: int
    frames: int
    drawn: int | None
    wall_us: int
    fifo_us: int | None
    native_us: int | None
    captures: int
    diagnostic: int
    stage: int | None = None
    paused: bool | None = None
    kind: str = "profile"

    @property
    def ms_per_frame(self) -> float:
        return self.wall_us / self.frames / 1000.0


def fields(line: str) -> dict[str, str]:
    return dict(_FIELDS.findall(line))


def _integer(values: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(values.get(key, str(default)), 0)
    except ValueError as exc:
        raise ValueError(f"invalid {key}={values.get(key)}") from exc


def parse_latest_session(path: Path) -> tuple[str, list[FrameWindow]]:
    """Use only the most recent boot; never combine runs from cumulative logs."""
    marker = "unknown"
    windows: list[FrameWindow] = []
    progress: list[tuple[int, int | None, bool | None]] = []
    with path.open(encoding="utf-8", errors="replace") as log:
        for raw in log:
            line = raw.strip()
            if line.startswith("[boot] build="):
                marker = fields(line).get("build", "unknown")
                windows.clear()
                progress.clear()
            elif line.startswith("[race-progress]"):
                values = fields(line)
                try:
                    frame = _integer(values, "frame")
                    raw_stage = _integer(values, "stage", 0xffffffff)
                    stage = None if raw_stage == 0xffffffff else raw_stage
                    pause_values = [
                        _integer(values, key, 0xffffffff)
                        for key in ("scene_paused", "section_paused", "controller_paused")
                    ]
                    paused = True if 1 in pause_values else (
                        False if all(value == 0 for value in pause_values) else None)
                    progress.append((frame, stage, paused))
                except ValueError:
                    pass
            elif line.startswith("[runtime-profile]") or line.startswith("[runtime-cadence]"):
                values = fields(line)
                is_profile = line.startswith("[runtime-profile]")
                try:
                    count = _integer(values, "frames")
                    wall_us = _integer(values, "wall_us")
                    drawn = _integer(values, "drawn_frames") if is_profile else None
                    fifo = _integer(values, "fifo_us") if is_profile else None
                    native = _integer(values, "native_request_us") if is_profile else None
                    captures = _integer(values, "captures") if is_profile else 0
                    diagnostic = _integer(values, "diagnostic") if is_profile else 0
                    end = _integer(values, "frame")
                except ValueError:
                    continue
                if not (count > 0 and wall_us > 0 and captures >= 0 and end >= count
                        and (drawn is None or 0 <= drawn <= count)
                        and (fifo is None or fifo >= 0)
                        and (native is None or native >= 0)):
                    continue
                windows.append(FrameWindow(end, count, drawn, wall_us, fifo,
                                           native, captures, diagnostic,
                                           kind="profile" if is_profile else "cadence"))
    # A progress marker may be logged after the profile window; use only the
    # latest progress that was already known at the window's end.
    progress.sort()
    times = [p[0] for p in progress]
    result = []
    for window in windows:
        index = bisect_right(times, window.end_frame) - 1
        stage = progress[index][1] if index >= 0 else None
        paused = progress[index][2] if index >= 0 else None
        result.append(FrameWindow(**{**asdict(window), "stage": stage, "paused": paused}))
    return marker, result


def weighted_percentile(windows: list[FrameWindow], percent: int) -> float:
    """Percentile of per-frame window rates, weighted by each window's frames."""
    assert 0 <= percent <= 100
    ordered = sorted(windows, key=lambda item: item.ms_per_frame)
    threshold = max(1, (sum(w.frames for w in ordered) * percent + 99) // 100)
    seen = 0
    for window in ordered:
        seen += window.frames
        if seen >= threshold:
            return window.ms_per_frame
    return ordered[-1].ms_per_frame


def analyze(path: Path, *, stage: int | None = None,
            min_frame: int = 0, include_captures: bool = False,
            unpaused_only: bool = False) -> dict:
    marker, all_windows = parse_latest_session(path)
    windows = [
        w for w in all_windows
        if w.end_frame >= min_frame
        and (include_captures or w.captures == 0)
        and (stage is None or w.stage == stage)
        and (not unpaused_only or w.paused is False)
    ]
    if not windows:
        raise ValueError(f"{path}: no matching runtime-profile windows")
    total_frames = sum(w.frames for w in windows)
    total_drawn = sum(w.drawn for w in windows if w.drawn is not None) if all(
        w.drawn is not None for w in windows) else None
    total_us = sum(w.wall_us for w in windows)
    attributed = all(w.kind == "profile" for w in windows)
    fifo_us = sum(w.fifo_us for w in windows if w.fifo_us is not None) if attributed else None
    native_us = sum(w.native_us for w in windows if w.native_us is not None) if attributed else None
    mean_ms = total_us / total_frames / 1000.0
    return {
        "source": str(path),
        "boot_marker": marker,
        "stage": stage,
        "window_count": len(windows),
        "frames": total_frames,
        "drawn_frames": total_drawn,
        "min_end_frame": min(w.end_frame for w in windows),
        "max_end_frame": max(w.end_frame for w in windows),
        "captures_included": sum(w.captures for w in windows),
        "diagnostic_windows": sum(bool(w.diagnostic) for w in windows),
        "cadence_windows": sum(w.kind == "cadence" for w in windows),
        "profile_windows": sum(w.kind == "profile" for w in windows),
        "paused_windows": sum(w.paused is True for w in windows),
        "unknown_pause_windows": sum(w.paused is None for w in windows),
        "wall_us": total_us,
        "fps": total_frames * 1_000_000.0 / total_us,
        "drawn_fps": total_drawn * 1_000_000.0 / total_us if total_drawn is not None else None,
        "mean_ms": mean_ms,
        "p50_ms": weighted_percentile(windows, 50),
        "p90_ms": weighted_percentile(windows, 90),
        "p95_ms": weighted_percentile(windows, 95),
        "fifo_ms_per_frame": fifo_us / total_frames / 1000.0 if fifo_us is not None else None,
        "fifo_wall_fraction": fifo_us / total_us if fifo_us is not None else None,
        "native_ms_per_frame": native_us / total_frames / 1000.0 if native_us is not None else None,
        "target_60_ms": TARGET_US / 1000.0,
        "slowdown_vs_60": mean_ms * 1000.0 / TARGET_US,
        "passes_60_wall_budget": mean_ms * 1000.0 <= TARGET_US,
        "passes_60_window_p95_budget": weighted_percentile(windows, 95) * 1000.0 <= TARGET_US,
        "performance_proven_in_game": False,
        "note": ("Wall, FIFO and native timings are overlapping, not additive. "
                 "Frame rate is not proof of correct gameplay, graphics or GPU load.")
    }


def print_result(result: dict) -> None:
    print(f"Source: {result['source']}")
    print(f"Boot: {result['boot_marker']} | stage={result['stage']} | "
          f"windows={result['window_count']} frames={result['frames']}")
    drawn = f"; drawn {result['drawn_fps']:.3f} FPS" if result["drawn_fps"] is not None else ""
    print(f"Present {result['fps']:.3f} FPS{drawn}; "
          f"mean {result['mean_ms']:.2f} ms/frame")
    print(f"Window-weighted frame time p50={result['p50_ms']:.2f}, "
          f"p90={result['p90_ms']:.2f}, p95={result['p95_ms']:.2f} ms")
    if result["fifo_ms_per_frame"] is not None:
        print(f"Nested FIFO elapsed {result['fifo_ms_per_frame']:.2f} ms/frame "
              f"({result['fifo_wall_fraction']:.1%} of total wall); "
              f"native requests {result['native_ms_per_frame']:.2f} ms/frame")
    else:
        print("FIFO, native and drawn-frame attribution: not measured in quiet cadence")
    print(f"60 FPS budget {result['target_60_ms']:.3f} ms/frame; "
          f"current mean {result['slowdown_vs_60']:.1f}x budget")
    if result["diagnostic_windows"]:
        print(f"WARNING: {result['diagnostic_windows']} diagnostic windows; "
              "not a performance-neutral benchmark.")
    if result["paused_windows"]:
        print(f"WARNING: {result['paused_windows']} windows are from a paused scene, "
              "not active gameplay.")
    if result["unknown_pause_windows"]:
        print(f"WARNING: {result['unknown_pause_windows']} windows lack a known pause state.")
    print("NOTE: no direct GPU timing or rendered-image correctness is inferred.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path, help="one or two local runtime logs")
    parser.add_argument("--stage", type=int, help="only profile windows with this race stage")
    parser.add_argument("--min-frame", type=int, default=0, help="exclude early loading windows")
    parser.add_argument("--include-captures", action="store_true",
                        help="allow frames with readback/capture overhead")
    parser.add_argument("--unpaused", action="store_true",
                        help="only count windows where no race pause flag is active")
    parser.add_argument("--json", type=Path, help="write summary without raw log data")
    args = parser.parse_args(argv)
    if len(args.logs) > 2:
        parser.error("provide one or two log files")
    try:
        reports = [analyze(p, stage=args.stage, min_frame=args.min_frame,
                           include_captures=args.include_captures,
                           unpaused_only=args.unpaused) for p in args.logs]
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    for index, report in enumerate(reports):
        if index:
            print()
        print_result(report)
    comparison = None
    if len(reports) == 2:
        baseline, candidate = reports
        comparison = {
            "baseline": baseline["source"],
            "candidate": candidate["source"],
            "speedup_wall": baseline["mean_ms"] / candidate["mean_ms"],
            "same_boot_marker": baseline["boot_marker"] == candidate["boot_marker"],
            "controlled_scene_verified": False,
        }
        print(f"\nObserved wall-time ratio: {comparison['speedup_wall']:.3f}x "
              "(candidate vs baseline)")
        print("WARNING: ensure identical SELF settings, scene, hardware clocks, "
              "diagnostics and warm cache before attributing a speedup.")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({"results": reports,
                                          "comparison": comparison}, indent=2) + "\n",
                             encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
