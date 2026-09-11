#!/usr/bin/env python3
"""Reproduce the supplied 2026-09-11 log audit. Python standard library only.

Keeps the source intact; excludes NUL/replacement/interleaved metric records.
Race selection is a workload proxy, not proof of a particular course/camera.
All times in metrics.json remain in microseconds. Percentiles use nearest rank.
"""
import argparse
import hashlib
import json
import math
import re
import statistics
from pathlib import Path

START = '[BOOT] WiiCompiled Vita runtime start'
KINDS = ('producer_frame', 'render_present', 'perf_summary', 'direct_prep')
NEEDED = {
    'producer_frame': ('producer_frame', 'interval_us', 'queue_wait_us', 'packet_copy_us',
                       'draws', 'vertices', 'efb_cmds', 'efb_cap_fail', 'efb_destroy', 'worker'),
    'render_present': ('serial', 'interval_us', 'swap_us'),
    'perf_summary': ('serial', 'draws', 'vertices', 'render_us', 'efb_us', 'physical', 'merged'),
    'direct_prep': ('serial', 'prep_us', 'vertex_us', 'texture_us', 'textures'),
}


def stats(values):
    a = sorted(values)
    if not a:
        return {'n': 0}
    return dict(n=len(a), min=a[0], mean=statistics.mean(a), median=statistics.median(a),
                p95=a[math.ceil(.95 * len(a)) - 1], p99=a[math.ceil(.99 * len(a)) - 1], max=a[-1])


def fields(line):
    return {k: (list(map(int, v.split('/'))) if '/' in v else int(v))
            for k, v in re.findall(r'\b([a-z_]+)=(\d+(?:/\d+)*)(?=\s|$)', line)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    here = Path(__file__).resolve().parent
    p.add_argument('--log', type=Path, default=here / 'evidence/runtime.original.log')
    p.add_argument('--out', type=Path, default=here / 'evidence')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    raw = args.log.read_bytes()
    lines = raw.decode('utf-8', errors='replace').splitlines()
    starts = [i for i, line in enumerate(lines) if START in line] + [len(lines)]
    if len(starts) < 2:
        raise SystemExit('No WiiCompiled boot marker found')
    boots, invalid, duplicates, conflicts = [], [], [], []
    for boot, (start, end) in enumerate(zip(starts, starts[1:]), 1):
        records = {kind: {} for kind in KINDS}
        config = next(({'line': i + 1, 'text': lines[i]} for i in range(start, end)
                       if 'init_marker=vglInitExtended phase=begin' in lines[i]), None)
        for i in range(start, end):
            line = lines[i]
            kind = next((k for k in KINDS if re.search(r'\[gx\] ' + k + r'[ =]', line)), None)
            if kind is None or 'worker_started' in line:
                continue
            d = fields(line)
            if ('\0' in line or '\ufffd' in line or line.count('[gx]') != 1
                    or not all(k in d for k in NEEDED[kind])):
                invalid.append({'boot': boot, 'line': i + 1, 'kind': kind})
                continue
            serial = d.get('producer_frame', d.get('serial'))
            if serial in records[kind]:
                old = records[kind][serial]
                if old['fields'] != d:
                    conflicts.append({'boot': boot, 'kind': kind, 'serial': serial,
                                      'lines': [old['line'], i + 1]})
                else:
                    duplicates.append({'boot': boot, 'kind': kind, 'serial': serial, 'line': i + 1})
                continue
            records[kind][serial] = {'line': i + 1, 'fields': d}
        race = [r for r in records['producer_frame'].values()
                if 5900 <= r['fields']['draws'] <= 6300 and r['fields']['efb_cmds'] == 13]
        workers = {}
        for row in race:
            worker = row['fields']['worker']
            if isinstance(worker, list) and len(worker) == 4 and worker[0] != 0:
                workers.setdefault(worker[0], {'serial': worker[0], 'render_us': worker[1],
                                              'swap_us': worker[2], 'observation_line': row['line']})
        summary = {key: stats(r['fields'][key] for r in race)
                   for key in ('interval_us', 'queue_wait_us', 'packet_copy_us', 'draws', 'vertices')}
        summary['worker_render_us'] = stats(r['render_us'] for r in workers.values())
        boots.append(dict(boot=boot, line_start=start + 1, line_end=end, config=config,
                          counts={k: len(v) for k, v in records.items()},
                          max_logged_producer_draws=max((r['fields']['draws'] for r in records['producer_frame'].values()), default=0),
                          race_summary=summary, race_producers=race,
                          race_workers=list(workers.values()), records=records))
    last = boots[-1]
    present = [r for serial, r in last['records']['render_present'].items() if 1248 <= serial <= 1298]
    present.sort(key=lambda r: r['fields']['serial'])
    serials = [r['fields']['serial'] for r in present]
    continuous = serials == list(range(1248, 1299))
    intervals = [r['fields']['interval_us'] for r in present]
    present_summary = dict(serial_first=1248, serial_last=1298,
                           complete_contiguous_window=continuous,
                           interval_us=stats(intervals), swap_us=stats(r['fields']['swap_us'] for r in present))
    if continuous:
        present_summary['duration_us'] = sum(intervals)
        present_summary['observed_present_fps'] = len(intervals) * 1e6 / sum(intervals)
    audio = []
    for i in range(last['line_start'] - 1, last['line_end']):
        if 'vita_audioout stats' in lines[i] and '\0' not in lines[i]:
            d = fields(lines[i])
            if all(k in d for k in ('real', 'silence', 'underrun')):
                audio.append({'line': i + 1, 'fields': d})
    last_audio = audio[-1] if audio else None
    if last_audio:
        d = last_audio['fields']
        last_audio['silence_fraction'] = d['silence'] / (d['silence'] + d['real'])
    pre_race_audio = next((r for r in reversed(audio) if r['line'] < 19809), None)
    audio_delta = None
    if pre_race_audio and last_audio:
        delta = {k: last_audio['fields'][k] - pre_race_audio['fields'][k]
                 for k in ('real', 'silence', 'underrun')}
        audio_delta = dict(start_line=pre_race_audio['line'], end_line=last_audio['line'], **delta,
                           silence_fraction=delta['silence'] / (delta['silence'] + delta['real']))
    result = dict(source_sha256=hashlib.sha256(raw).hexdigest(), bytes=len(raw), lines=len(lines),
                  nul_bytes=raw.count(b'\0'), nul_lines=sum('\0' in l for l in lines),
                  method='Workload proxy: 5900<=draws<=6300, efb_cmds=13. No interpolation of absent producers. '
                         'Worker serial deduplicated independently. P6.38 attribution by config, not embedded build ID. '
                         'P95/P99 nearest rank of logged samples only. Fixed present window is specific to supplied log.',
                  rejected_metric_records=invalid, duplicate_metric_records=duplicates,
                  conflicting_metric_records=conflicts, boots=boots,
                  latest_present_window=present_summary, latest_audio=last_audio,
                  late_audio_delta=audio_delta)
    (args.out / 'metrics.json').write_text(json.dumps(result, indent=2) + '\n')
    start = starts[-2]
    (args.out / 'latest-boot.numbered.txt').write_text(''.join(
        f'{i+1:05d} {lines[i].replace(chr(0), "<NUL>")}\n' for i in range(start, len(lines))))
    selected = [7368, 11264, 17967, 18002, 18181, 18216, 18217, 18536, 18651, 18653,
                19672, 19673, 19675, 19782, 19795, 19802, 19804, 19809, 19846, 19928]
    (args.out / 'key-records.txt').write_text(''.join(
        f'{i:05d} {lines[i-1]}\n' for i in selected if i <= len(lines)))
    print(json.dumps(dict(boots=len(boots), rejected=len(invalid), duplicates=len(duplicates),
                          conflicts=len(conflicts), latest_race=last['race_summary'],
                          presents=present_summary, audio=last_audio, late_audio_delta=audio_delta), indent=2))


if __name__ == '__main__':
    main()
