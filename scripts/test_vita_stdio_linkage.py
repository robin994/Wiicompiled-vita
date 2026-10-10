#!/usr/bin/env python3
"""Check shipped ARM native-stack boundaries, including linker veneers."""
import argparse
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
    parser.add_argument('--with-waits', action='store_true',
                        help='Also check VI, GX retry, GKI and panic wait boundaries')
    parser.add_argument('--with-input', action='store_true',
                        help='Also check Vita controller reads use the native dispatcher')
    args = parser.parse_args()
    with args.elf.open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {s.name: (s['st_value'] & ~1, s['st_size'])
                   for s in elf.get_section_by_name('.symtab').iter_symbols()
                   if s['st_info']['type'] == 'STT_FUNC'}
        loads = [(s['p_vaddr'], s.data()) for s in elf.iter_segments()
                 if s['p_type'] == 'PT_LOAD']

    def read(address, size):
        for start, data in loads:
            if start <= address and address + size <= start + len(data):
                return data[address - start:address - start + size]
        return b''

    def destination(address):
        # GNU ARM linkers insert PC-relative veneers for distant Thumb calls.
        # Resolve the literal rather than trusting potentially repeated names.
        code = read(address & ~1, 20)
        start = address & ~1
        if code[:4] == bytes.fromhex('7847fde7'):  # Thumb bx pc; b .
            start += 4
            code = code[4:]
        if len(code) >= 16 and struct.unpack_from('<3I', code) == (
                0xe59fc004, 0xe08fc00c, 0xe12fff1c):
            literal = struct.unpack_from('<I', code, 12)[0]
            return ((start + 12 + literal) & 0xffffffff) & ~1
        return address & ~1

    native = next(name for name in symbols
                  if 'HostContext16RunOnNativeStack' in name and 'veneer' not in name)
    required = {
        '__swrite': ['__wrap__lseek_r', '__wrap__write_r'],
        '__wrap__lseek_r': [native],
        '__wrap__write_r': [native],
        'OS__Report_801a25d0': [native],
    }
    if args.with_waits:
        def function(fragment):
            found = [name for name in symbols if fragment in name
                     and 'veneer' not in name and '.cold' not in name]
            assert len(found) == 1, f'Ambiguous or missing function {fragment}: {found}'
            return found[0]

        until = function('_ZN8HostWait10SleepUntil')
        duration = function('_ZN8HostWait8SleepFor')
        required.update({
            function('SleepPreciselyUntil'): [until],
            function('BeginNextAuroraFrameWithRetry'): [duration],
            'GKI_delay_HLE_801301b4': [duration],
            'OS__Panic_801A2660_Cpu': [duration],
            until: [native],
            duration: [until],
        })
    if args.with_input:
        read_pad = [name for name in symbols if 'ReadPadEPv' in name and 'veneer' not in name]
        assert len(read_pad) == 1, f'Ambiguous or missing controller callback: {read_pad}'
        required.update({'PADRead': [native], read_pad[0]: ['sceCtrlPeekBufferPositive']})
    objdump = Path(os.environ.get('VITASDK', '/usr/local/vitasdk')) / 'bin/arm-vita-eabi-objdump'
    evidence = []
    for caller, callees in required.items():
        start, size = symbols[caller]
        assert size, 'Missing function extent: ' + caller
        text = subprocess.check_output([
            str(objdump), '-d', '-C', '--start-address=' + hex(start),
            '--stop-address=' + hex(start + size), str(args.elf)], text=True)
        branches = re.findall(r'\b(?:blx?|b(?:\.w|\.n)?)\s+([0-9a-f]+)\s+<', text)
        targets = {destination(int(value, 16)) for value in branches}
        for callee in callees:
            assert symbols[callee][0] in targets, f'{caller} bypasses {callee}'
        evidence.append(text)
    args.output.write_text('\n'.join(evidence))
    print('PASS: stdio append seek/write and OSReport reach the native dispatcher; ARM/Thumb veneers resolved.')
    if args.with_waits:
        print('PASS: VI, GX retry, GKI and panic waits reach HostWait and the native dispatcher.')
    if args.with_input:
        print('PASS: PADRead reaches the native dispatcher and its callback reads SceCtrl input.')


if __name__ == '__main__':
    main()
