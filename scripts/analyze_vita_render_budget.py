#!/usr/bin/env python3
"""Summarize WiiCompiled's opt-in sparse GXM/EFB budget probe.

The diagnostic GXM phases and EFB flush overlap. Do not add them together.
Samples (one per 32 frames) also incur instrumentation overhead and must not
be included in a quiet-FPS comparison.
"""
import argparse
from pathlib import Path
import re


def numbers(line: str) -> dict[str, int]:
    return {key: int(value) for key, value in re.findall(r"\b([a-z_]+)=([0-9]+)\b", line)}


def parse_budget(text: str) -> list[dict]:
    stages: list[tuple[int, int]] = []
    rows: dict[int, dict] = {}
    for line in text.splitlines():
        if "[race-progress]" in line:
            data = numbers(line)
            if "frame" in data and "stage" in data:
                stages.append((data["frame"], data["stage"]))
        if "[wic-render-budget]" in line:
            data = numbers(line)
            if "frame" in data:
                rows.setdefault(data["frame"], {}).update(data)
        if "[wic-draw-health]" in line:
            data = numbers(line)
            completed = data.pop("completed_frame", None)
            if completed is not None:
                rows.setdefault(completed, {}).update(data)
    stages.sort()
    output = []
    for frame, data in sorted(rows.items()):
        if "worker_frame_us" not in data:
            continue
        known = [stage for observed, stage in stages if observed <= frame]
        data["stage"] = known[-1] if known else None
        output.append(data)
    return output


def describe(sample: dict) -> str:
    def ms(key: str) -> str:
        return f"{sample.get(key, 0)/1000:.1f}"
    return (
        f"frame={sample['frame']} stage={sample.get('stage', '?')} "
        f"worker_ms={ms('worker_frame_us')} "
        f"draws={sample.get('native_draws', '?')} "
        f"requested={sample.get('requested', '?')} merged={sample.get('merged', '?')} "
        f"native_pipeline_ms={ms('pipeline_us')} "
        f"native_textures_ms={ms('texture_us')} native_draw_ms={ms('draw_us')} "
        f"efb_calls={sample.get('efb_copies', '?')} "
        f"efb_flush_ms={ms('efb_flush_us')} efb_capture_ms={ms('efb_capture_us')} "
        f"efb_clear_ms={ms('efb_clear_us')} "
        f"queue_ms={ms('display_queue_us')} "
        f"white_draws={sample.get('white_fallback_draws', '?')} "
        f"gpu_finishes={sample.get('native_finish', '?')} "
        f"finish_destroy={sample.get('finish_destroy', '?')} "
        f"finish_destroy_ms={ms('finish_destroy_us')} "
        f"finish_buffer={sample.get('finish_buffer', '?')} "
        f"finish_texture={sample.get('finish_texture', '?')} "
        f"finish_readback={sample.get('finish_readback', '?')} "
        f"finish_target={sample.get('finish_target', '?')}"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="runtime-session.log after a Time Trial")
    parser.add_argument("--stage", type=int, choices=(0, 1, 2), help="Only this race stage")
    parser.add_argument("--last", type=int, default=12, help="Number of latest sampled frames")
    args = parser.parse_args(argv)
    if args.last < 1:
        parser.error("--last must be positive")
    samples = parse_budget(args.log.read_text(encoding="utf-8", errors="replace"))
    if args.stage is not None:
        samples = [r for r in samples if r.get("stage") == args.stage]
    print(f"Budget samples: {len(samples)}; GXM and EFB phases may overlap.")
    print("Samples include probe overhead. Compare normal FPS with render_budget_probe OFF.")
    if (samples and all(row.get("worker_frame_us", 0) == 0 for row in samples) and
            all(row.get("efb_capture_us", 0) == 0 for row in samples)):
        print("WARNING: probe timings are all zero. The older Aurora clock "
              "was gated by diagnostics.enabled=false; update Aurora before "
              "attributing costs. Draw counts/merges are still valid.")
    for sample in samples[-args.last:]:
        print(describe(sample))
    if not samples:
        print("No matching samples. Enable [vita] render_budget_probe = true and "
              "capture a session containing at least 32 completed frames.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
