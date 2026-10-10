#!/usr/bin/env python3
"""Verify worker startup and the frame lifetime barrier in the linked ARM code."""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--boot-marker', help='Expected boot marker for a newer candidate')
    parser.add_argument('--synchronous', action='store_true', help='Require a compiled synchronous control instead')
    args = parser.parse_args()
    path = args.build_dir / 'wiicompiled-vita-game'
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_ARM'
        symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
        names = {symbol.name: symbol for symbol in symbols}
        loads = [(segment['p_vaddr'], segment.data()) for segment in elf.iter_segments()
                 if segment['p_type'] == 'PT_LOAD']

    def read(address, size):
        for start, data in loads:
            if start <= address and address + size <= start + len(data):
                return data[address - start:address - start + size]
        return b''

    def destination(address):
        start = address & ~1
        data = read(start, 20)
        if data[:4] == bytes.fromhex('7847fde7'):
            start += 4
            data = data[4:]
        if len(data) >= 16 and struct.unpack_from('<3I', data) == (0xe59fc004, 0xe08fc00c, 0xe12fff1c):
            return ((start + 12 + struct.unpack_from('<I', data, 12)[0]) & 0xffffffff) & ~1
        return address & ~1

    objdump = Path(os.environ.get('VITASDK', '/usr/local/vitasdk')) / 'bin/arm-vita-eabi-objdump'

    def targets(symbol):
        start = symbol['st_value'] & ~1
        code = subprocess.check_output([str(objdump), '-d', '--start-address=' + hex(start),
                                        '--stop-address=' + hex(start + symbol['st_size']), str(path)], text=True)
        return {destination(int(address, 16)) for address in
                re.findall(r'\b(?:blx?|b(?:\.w)?)\s+([0-9a-f]+)\s+<', code)}

    def function(name):
        symbol = names[name]
        assert symbol['st_shndx'] != 'SHN_UNDEF' and symbol['st_size'], name
        return symbol

    async_enabled=not args.synchronous
    startup = names.get('_ZN6aurora2gx4fifo12start_workerEv')
    barrier = function('_ZN6aurora2gx4fifo8run_syncEPFvPvES2_')
    begin = function('_ZN6aurora4vita11begin_frameEv')
    frame = function('_ZN6aurora4vita9end_frameEv')
    starts_worker=bool(startup and startup['st_size'] and
                       startup['st_value'] & ~1 in targets(function('GXInit')))
    assert starts_worker==async_enabled, 'GXInit worker startup differs from requested mode'
    if async_enabled:
        worker_calls = targets(startup)
        for name in ['sceKernelCreateThread', 'sceKernelStartThread']:
            assert names[name]['st_value'] & ~1 in worker_calls, 'GX worker does not call ' + name
    assert barrier['st_value'] & ~1 in targets(begin), 'begin_frame does not await its own worker result'
    assert barrier['st_value'] & ~1 in targets(frame), 'end_frame does not wait for worker-owned buffers'

    cache = (args.build_dir / 'CMakeCache.txt').read_text()
    flags = (args.build_dir / 'aurora/CMakeFiles/aurora_vita_gxm_backend.dir/flags.make').read_text()
    assert 'AURORA_VITA_ASYNC_GX:BOOL='+('ON' if async_enabled else 'OFF') in cache
    assert '-DAURORA_VITA_ASYNC_GX='+str(int(async_enabled))+' ' in flags
    if args.boot_marker:
        marker=args.boot_marker.encode()
    else:
        prefix=b'psarc-resident-assets-' if 'WIC_RESIDENT_TEXTURES:BOOL=ON' in cache else b'psarc-precompiled-assets-'
        if async_enabled:prefix+=b'async-'
        markers={m for _,data in loads for m in re.findall(re.escape(prefix)+rb'[a-z0-9-]+',data)}
        assert len(markers)==1, ('expected one installed-build marker',markers)
        marker=markers.pop()
    assert any(marker in data for _, data in loads), 'async boot marker is absent'
    assert any(b'[gx-worker] requested=%d running=%d' in data for _, data in loads)
    result = {
        'passed': True, 'elf_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'cmake_async_gx': 'ON' if async_enabled else 'OFF', 'aurora_backend_compiled_async': async_enabled,
        'gx_init_calls_start_worker': starts_worker, 'start_worker_calls_kernel_thread_create_and_start': True if async_enabled else None,
        'begin_frame_calls_run_sync': True, 'end_frame_calls_run_sync': True,
        'boot_marker': marker.decode(),
        'consumer_macro_note': 'The legacy ASYNC_GX=0 define in PPC/runtime units is unused. '
                               'The effective startup switch is private to the Aurora backend.',
        'scope': 'Static ARM startup and lifetime-barrier audit. Actual worker startup, CPU affinity, '
                 'device stability and FPS remain unverified until manual launch.'
    }
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
