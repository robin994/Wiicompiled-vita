#!/usr/bin/env python3
"""Bind resident configuration and cache call sites to the linked ARM binary."""
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
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    path = args.elf or args.build_dir / 'wiicompiled-vita-game'
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_ARM'
        symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
        names = {symbol.name: symbol for symbol in symbols}
        loads = [(segment['p_vaddr'], segment.data()) for segment in elf.iter_segments()
                 if segment['p_type'] == 'PT_LOAD']
    markers={m for _,data in loads for m in re.findall(rb'psarc-resident-assets-async-[a-z0-9-]+',data)}
    assert len(markers)==1, ('expected one resident-build marker',markers)
    marker=markers.pop()
    assert any(marker in data for _, data in loads), 'resident boot marker is absent'
    assert any(b'[resident-config]' in data for _, data in loads)
    assert any(b'[resident-assets]' in data for _, data in loads)
    cache = (args.build_dir / 'CMakeCache.txt').read_text()
    for option in ['WIC_RESIDENT_GEOMETRY', 'WIC_RESIDENT_TEXTURES', 'WIC_PRECOMPILED_ASSETS',
                   'AURORA_VITA_ASYNC_GX']:
        assert option + ':BOOL=ON' in cache, option + ' must be ON'
    flags = (args.build_dir / 'CMakeFiles/wiicompiled_backend.dir/flags.make').read_text()
    for option in ['WIC_RESIDENT_GEOMETRY', 'WIC_RESIDENT_TEXTURES']:
        assert re.search(r'-D' + option + r'=1(?:\s|$)', flags), option + ' was not compiled ON'

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

    def function(*tokens):
        found = [symbol for symbol in symbols if symbol['st_size'] and
                 symbol['st_info']['type'] == 'STT_FUNC' and symbol.name.startswith('_ZN') and
                 'veneer' not in symbol.name and
                 all(token in symbol.name for token in tokens)]
        assert len(found) == 1, (tokens, [s.name for s in found])
        return found[0]

    texture = function('TextureCache', 'get_or_upload')
    remember = function('TextureSnapshots', 'remember')
    hash_function = function('TextureSnapshots', 'content_hash')
    geometry = function('StaticGeometryCache', '3getE')
    native_geometry = function('load_native_geometry')
    backend = function('wiicompiled7backend10initialize')
    assert remember['st_value'] & ~1 in targets(texture), 'snapshot admission is not linked'
    assert names['memcmp']['st_value'] & ~1 in targets(texture), 'exact texture comparison is not linked'
    assert names['XXH32']['st_value'] & ~1 in targets(hash_function), 'content hash is not linked'
    assert native_geometry['st_value'] & ~1 in targets(geometry), 'resident geometry skips native records'
    for token in ['create_vertex_buffer', 'create_index_buffer']:
        assert function('gfx8Renderer', token)['st_value'] & ~1 in targets(geometry), token
    assert backend['st_value'] & ~1 in targets(names['main']), 'main skips resident backend initialization'

    result = {
        'passed': True, 'elf_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'boot_marker': marker.decode(), 'backend_compiled_resident_geometry': True,
        'backend_compiled_resident_textures': True, 'texture_calls_snapshot_admission': True,
        'texture_calls_exact_byte_comparison': True, 'snapshot_hash_calls_xxhash': True,
        'resident_geometry_calls_native_loader_and_buffer_creation': True,
        'main_calls_backend_initializer': True,
        'scope': 'Static ARM call sites and compiled configuration. GPU residency utilization, '
                 'cache hits, images, stability and FPS require device validation.'
    }
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
