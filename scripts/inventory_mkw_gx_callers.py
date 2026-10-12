#!/usr/bin/env python3
"""Inventory static translated -> GX/HLE edges without touching game data or a device.

This is a migration map, not a profile or proof of runtime reachability. Native
callers, indirect calls, inline FIFO stores and assembly are outside its scope.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re


MACROS = re.compile(
    r"\b(?P<macro>PPC_NATIVE_OVERRIDE(?:_VOID)?|REGISTER_NATIVE_FUNCTION(?:_AS)?)"
    r"\s*\(\s*(?:0x)?(?P<address>[0-9a-fA-F]{8})[uU]?\s*,\s*"
    r"(?P<symbol>[A-Za-z_]\w*)")
COMMENTS_AND_LITERALS = re.compile(
    r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    r'|//[^\n]*|/\*.*?\*/', re.DOTALL)


def strip_non_code(text):
    # Preserve line numbers and mask strings too: a macro in a diagnostic is
    # not a registration. Preprocessor branches are deliberately not evaluated.
    return COMMENTS_AND_LITERALS.sub(
        lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), text)


def registrations(text, source):
    clean = strip_non_code(text)
    return [dict(address=int(m['address'], 16), symbol=m['symbol'],
                 macro=m['macro'], source=source,
                 line=clean.count('\n', 0, m.start()) + 1)
            for m in MACROS.finditer(clean)]


def family_hint(name):
    low = name.lower()
    for family, words in (
        ('models_g3d', ('g3d', 'resmdl', 'resshp', 'scnmdl', 'drawresmdl')),
        ('effects', ('nw4r::ef', 'nw4r__ef', 'drawstrategy', 'particle')),
        ('ui_text', ('lyt', 'drawquad', 'font', 'textwriter', 'ut::')),
        ('video', ('thp', 'movie')),
        ('efb_display', ('copytex', 'copydisp', 'peek', 'poke', 'xfb')),
        ('gx_sdk', ('gx::', 'gx__', '__gx', 'gxset', 'gxload')),
    ):
        if any(word in low for word in words):
            return family
    return 'unclassified'


def inventory(metadata, entries, symbols=None):
    if metadata.get('format') != 'mkw-base-translation-output' or metadata.get('formatVersion') != 2:
        raise ValueError('expected mkw-base-translation-output version 2')
    functions = metadata.get('functions')
    if not isinstance(functions, list):
        raise ValueError('missing functions list')
    symbols = symbols or {}
    by_target = {}
    for entry in entries:
        by_target.setdefault(entry['address'], []).append(entry)
    seen, callers = set(), []
    for function in functions:
        address = function['entryPoint']
        if not isinstance(address, int) or not 0 <= address <= 0xffffffff or address in seen:
            raise ValueError('invalid or duplicate function address')
        seen.add(address)
        build = function['build']
        dependencies = build.get('directCallDependencies')
        if not isinstance(dependencies, list) or any(
                not isinstance(t, int) or not 0 <= t <= 0xffffffff for t in dependencies):
            raise ValueError(f'missing or invalid direct dependencies at {address:08x}')
        targets = sorted(set(dependencies) & by_target.keys())
        if not targets:
            continue
        label = symbols.get(address, build['symbol'])
        callers.append(dict(address=f'0x{address:08x}', label=label,
                            family_hint=family_hint(label), symbol=build['symbol'],
                            generated_source=function['relativePath'],
                            gx_targets=[f'0x{t:08x}' for t in targets]))
    callers.sort(key=lambda c: c['address'])
    fan_in = Counter(t for caller in callers for t in caller['gx_targets'])
    targets = []
    for address, sources in sorted(by_target.items()):
        key = f'0x{address:08x}'
        targets.append(dict(address=key, label=symbols.get(address, sources[0]['symbol']),
                            translated_callers=fan_in[key],
                            declarations=[{k: v for k, v in s.items() if k != 'address'} for s in sources]))
    return dict(
        format='wic-gx-caller-inventory', version=1,
        scope='static translated direct-call dependencies to declared GX HLE entries',
        limitations=['not dynamic call counts or a performance ranking',
                     'indirect calls, native callers and inline FIFO writes are not counted',
                     'declarations include all preprocessor branches; they do not establish linked winners',
                     'family labels are symbol hints requiring source/disassembly confirmation',
                     'metadata identity does not establish installed SELF identity'],
        quality=metadata.get('quality'),
        summary=dict(translated_functions=len(functions), gx_declared_targets=len(by_target),
                     gx_targets_with_translated_callers=len(fan_in),
                     translated_gx_callers=len(callers), static_edges=sum(fan_in.values()),
                     family_hints=dict(sorted(Counter(c['family_hint'] for c in callers).items()))),
        targets=targets, callers=callers)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    metadata_path = args.metadata or root / 'generated/base_translation_output.json'
    sources = sorted((root / 'runtime/src/hle/gx').glob('*.cpp'))
    if not sources:
        parser.error('no GX HLE sources found')
    try:
        entries = [e for source in sources for e in registrations(
            source.read_text(), source.relative_to(root).as_posix())]
        if not entries:
            raise ValueError('no GX registrations found')
        symbol_path = root / 'projects/mkwii/MAP.txt'
        symbols = {}
        if symbol_path.exists():
            for line in symbol_path.read_text().splitlines():
                parts = line.split(maxsplit=1)
                if len(parts) == 2 and re.fullmatch('[0-9a-fA-F]{8}', parts[0]):
                    symbols[int(parts[0], 16)] = parts[1]
        result = inventory(json.loads(metadata_path.read_text()), entries, symbols)
        result['input_sha256'] = dict(
            translation_metadata=digest(metadata_path),
            gx_sources={p.relative_to(root).as_posix(): digest(p) for p in sources})
        if symbol_path.exists():
            result['input_sha256']['symbol_map'] = digest(symbol_path)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(json.dumps(result['summary'], sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
