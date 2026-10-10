#!/usr/bin/env python3
"""Verify diagnostic wrapping keeps the translated Layout dispatch contract."""
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
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exact-float-fma', action='store_true')
    parser.add_argument('--native-thp', action='store_true')
    parser.add_argument('--hardware-avc', action='store_true')
    args = parser.parse_args()
    with args.elf.open('rb') as stream:
        elf = ELFFile(stream)
        assert elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_ARM'
        all_symbols = list(elf.get_section_by_name('.symtab').iter_symbols())
        symbols = {s.name: s for s in all_symbols if s['st_info']['bind'] in ('STB_GLOBAL', 'STB_WEAK')}
        loads = [(s['p_vaddr'], s.data()) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']

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
    functions = {s['st_value'] & ~1: s for s in all_symbols
                 if s['st_info']['type'] == 'STT_FUNC' and s['st_size']}
    code_cache = {}

    def code_for(start):
        if start not in code_cache:
            size = functions[start]['st_size']
            code_cache[start] = subprocess.check_output([str(objdump), '-d', '--start-address=' + hex(start),
                '--stop-address=' + hex(start + size), str(args.elf)], text=True)
        return code_cache[start]

    def targets_for(start):
        return [destination(int(address, 16)) for address in
            re.findall(r'\b(?:blx?|b(?:\.w)?)\s+([0-9a-f]+)\s+<', code_for(start))]

    calls = []
    pairs = ['_Z18PpcFmaddPairInline18__simd64_float32_tS_S_',
             '_Z18PpcFmsubPairInline18__simd64_float32_tS_S_']
    wrapped_functions = [('__wrap_fma', 'fma'), ('__wrap_fmaf', 'fmaf'),
                              ('__wrap_func_8007A170', 'func_8007A170'),
                              ('__wrap_func_8007AB90', 'func_8007AB90'),
                              ('__wrap_func_80199B98', 'func_80199B98'),
                              ('__wrap_func_80199B58', 'func_80199B58'),
                              ('__wrap_func_80082AF0', 'func_80082AF0')] + [('__wrap_' + p, p) for p in pairs]
    if args.native_thp or args.hardware_avc:
        wrapped_functions.append(('__wrap_func_801B3BAC', 'func_801B3BAC'))
    for wrapped, original in wrapped_functions:
        for name in [wrapped, original]:
            assert name in symbols and symbols[name]['st_shndx'] != 'SHN_UNDEF', name
        start = symbols[wrapped]['st_value'] & ~1
        assert start != symbols[original]['st_value'] & ~1
        targets = targets_for(start)
        if wrapped == '__wrap_fmaf' and args.exact_float_fma:
            # -Os may outline the exact fast path and its libm fallback.
            for target in list(targets):
                if target in functions and 'FloatFma' in functions[target].name:
                    targets += targets_for(target)
        real = symbols[original]['st_value'] & ~1
        assert real in targets, f'{wrapped} does not call the original implementation'
        assert start not in targets, f'{wrapped} recurses into itself'
        calls.append({'wrapper': wrapped, 'wrapper_address': hex(start), 'original': original,
                      'original_address': hex(real), 'original_call_verified': True})

    registration, dispatch = [], []
    guests = [0x8007a170, 0x8007ab90, 0x80199b98,0x80199b58,0x80082af0]
    if args.native_thp or args.hardware_avc:
        guests.append(0x801b3bac)
    wrapped_guests = {address: symbols['__wrap_func_' + format(address, '08X')]['st_value'] for address in guests}
    for symbol in all_symbols:
        if symbol['st_info']['type'] != 'STT_OBJECT':
            continue
        if 'kRecords' in symbol.name:
            data = read(symbol['st_value'], symbol['st_size'])
            # ARM32 BulkTranslatedFunctionRecord: stride 40, entry +8, kind +12.
            assert len(data) % 40 == 0
            for offset in range(0, len(data), 40):
                guest = struct.unpack_from('<I', data, offset)[0]
                if guest in wrapped_guests:
                    entry = struct.unpack_from('<I', data, offset + 8)[0]
                    assert entry == wrapped_guests[guest] and data[offset + 12] == 0, 'Registration identity/kind mismatch'
                    registration.append({'guest': hex(guest), 'entry': hex(entry), 'kind': 'BaseTranslated'})
        elif 'kEntries' in symbol.name:
            data = read(symbol['st_value'], symbol['st_size'])
            # ARM32 RawDispatchRecord: stride 16, entry +4.
            if len(data) % 16:
                continue
            for offset in range(0, len(data), 16):
                guest = struct.unpack_from('<I', data, offset)[0]
                if guest in wrapped_guests:
                    entry = struct.unpack_from('<I', data, offset + 4)[0]
                    assert entry == wrapped_guests[guest], 'Immutable dispatch does not select the registration entry'
                    dispatch.append({'guest': hex(guest), 'entry': hex(entry)})
    for guest in wrapped_guests:
        assert sum(r['guest'] == hex(guest) for r in registration) == 1
        assert sum(r['guest'] == hex(guest) for r in dispatch) == 1
    thp = None
    if args.native_thp or args.hardware_avc:
        caller = symbols['func_80552BA4']['st_value'] & ~1
        assert symbols['__wrap_func_801B3BAC']['st_value'] & ~1 in targets_for(caller), 'THP caller bypasses native wrapper'
        if args.native_thp:
            for name in ('jpeg_read_raw_data', 'jpeg_mem_src'):
                assert name in symbols and symbols[name]['st_shndx'] != 'SHN_UNDEF', name
        assert 'GxNotifyGuestRamDmaWrite' in symbols, 'Video texture publication missing'
        thp = {'direct_guest_caller_uses_wrapper': True, 'native_jpeg_raw_yuv_linked': args.native_thp,
               'translated_fallback_linked': True, 'device_performance_proven': False}
    avc=None
    if args.hardware_avc:
        for name in ('sceVideodecInitLibrary','sceAvcdecCreateDecoder','sceAvcdecDecode','sceAvcdecDeleteDecoder'):
            assert name in symbols and symbols[name]['st_shndx']!='SHN_UNDEF',name
        avc={'native_decoder_imports_linked':True,'direct_guest_caller_uses_wrapper':True,
             'hardware_output_validated':False,'device_performance_proven':False}
    fast_math = None
    if args.exact_float_fma:
        candidate_codes = [code_for(symbols['__wrap_fmaf']['st_value'] & ~1)]
        candidate_codes += [code_for(address) for address, symbol in functions.items() if 'FloatFma' in symbol.name]
        fast_code = '\n'.join(candidate_codes)
        assert re.search(r'\b(?:vmul|vmla)\.f64\b', fast_code), 'Missing exact double product'
        assert 'vcvt.f64.f32' in fast_code and 'vcvt.f32.f64' in fast_code, 'Missing float conversion'
        assert re.search(r'\bvmrs\b.*fpscr', fast_code), 'Missing rounding-mode check'
        fast_math = {'double_product_and_float_conversions': True, 'rounding_mode_read': True,
                     'original_libm_fallback_reachable': True, 'scope': 'ARM static lowering, not device correctness'}
    result = {'elf_sha256': hashlib.sha256(args.elf.read_bytes()).hexdigest(), 'passed': True,
              'wrappers': calls, 'layout_registration': registration, 'layout_dispatch': dispatch,
              'exact_float_fma': fast_math,
              'native_thp': thp,
              'hardware_avc': avc,
              'validation_scope': 'static ARM linkage and dispatch identity; no device behavior proof'}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('CPU wrappers call real libm/Layout; registry and immutable dispatch agree')


if __name__ == '__main__':
    main()
