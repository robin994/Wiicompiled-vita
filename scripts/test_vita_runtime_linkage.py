#!/usr/bin/env python3
"""Reject incomplete Vita pthread linkage or a raw libco stack switch."""
import argparse
import json
import os
import re
import struct
import subprocess
from pathlib import Path

from elftools.elf.elffile import ELFFile


def thumb_constant(code, register):
    """Decode the MOVW/MOVT availability probe emitted by the Vita GCC runtime."""
    values = []
    for offset, opcode in ((0, 0xf240), (4, 0xf2c0)):
        first, second = struct.unpack_from('<HH', code, offset)
        assert first & 0xfbf0 == opcode, 'Unexpected GCC thread probe encoding'
        assert second >> 8 & 15 == register, 'Unexpected GCC thread probe register'
        values.append(((first & 15) << 12) | ((first >> 10 & 1) << 11) |
                      ((second >> 12 & 7) << 8) | (second & 255))
    return values[0] | (values[1] << 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.elf.open('rb') as stream:
        elf = ELFFile(stream)
        symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
        def code(name):
            symbol = symbols[name]
            section = elf.get_section(symbol['st_shndx'])
            offset = (symbol['st_value'] & ~1) - section['sh_addr']
            return section.data()[offset:offset + symbol['st_size']]

        pthread_symbols = {n: s for n, s in symbols.items()
                           if n.startswith('pthread_') and
                           s['st_info']['bind'] in {'STB_GLOBAL', 'STB_WEAK'}}
        unresolved = sorted(n for n, s in pthread_symbols.items()
                            if s['st_shndx'] == 'SHN_UNDEF' or not s['st_value'])
        assert not unresolved, 'Unresolved pthread primitives: ' + ', '.join(unresolved)
        for name in ['pthread_cancel', 'pthread_once', 'pthread_cond_wait',
                     'pthread_cond_destroy', 'pthread_cond_broadcast', 'pthread_cond_signal']:
            assert name in pthread_symbols and pthread_symbols[name]['st_info']['bind'] == 'STB_GLOBAL', \
                'Missing real pthread implementation: ' + name

        cancel = symbols['pthread_cancel']
        assert cancel['st_shndx'] != 'SHN_UNDEF' and cancel['st_value'], 'pthread_cancel is unresolved'
        assert cancel['st_info']['bind'] == 'STB_GLOBAL', 'pthread_cancel must use the actual pthread implementation'
        active_pointer = thumb_constant(code('__gthread_active_p'), 0)
        assert active_pointer == cancel['st_value'], 'GCC thread/TLS gate is disabled or does not resolve pthread_cancel'
        start = '_ZNSt6thread15_M_start_threadESt10unique_ptrINS_6_StateESt14default_deleteIS1_EEPFvvE'
        start_pointer = thumb_constant(code(start)[4:], 3)
        assert start_pointer == cancel['st_value'], 'std::thread still takes the immediate failure path'
        required = ['_sceFiberInitializeImpl', 'sceFiberRun', 'sceFiberSwitch',
                    'sceFiberReturnToThread', 'sceFiberFinalize', 'pthread_create',
                    '__emutls_get_address']
        for name in required:
            assert name in symbols and symbols[name]['st_shndx'] != 'SHN_UNDEF', 'Missing native runtime symbol: ' + name
        assert 'co_swap_function' not in symbols and 'co_switch' not in symbols, 'Raw libco backend remains linked'
        checks = [n for n in symbols if 'CheckRuntimeContracts' in n and 'veneer' not in n]
        assert checks, 'Runtime boot checks missing'

        loads = [(s['p_vaddr'], s.data()) for s in elf.iter_segments()
                 if s['p_type'] == 'PT_LOAD']
        def read(address, size):
            for start, data in loads:
                if start <= address and address + size <= start + len(data):
                    return data[address - start:address - start + size]
            return b''

        def destination(address):
            # Decode the literal of an ARM/Thumb interworking veneer, as in
            # the native stdio/wait audit. Repeated linker names are ambiguous.
            start = address & ~1
            data = read(start, 20)
            if data[:4] == bytes.fromhex('7847fde7'):
                start += 4
                data = data[4:]
            if len(data) >= 16 and struct.unpack_from('<3I', data) == (
                    0xe59fc004, 0xe08fc00c, 0xe12fff1c):
                literal = struct.unpack_from('<I', data, 12)[0]
                return ((start + 12 + literal) & 0xffffffff) & ~1
            return address & ~1

        guard = symbols['__cxa_guard_acquire']
        start = guard['st_value'] & ~1
        objdump = Path(os.environ.get('VITASDK', '/usr/local/vitasdk')) / 'bin/arm-vita-eabi-objdump'
        disassembly = subprocess.check_output([
            str(objdump), '-d', '-C', '--start-address=' + hex(start),
            '--stop-address=' + hex(start + guard['st_size']), str(args.elf)], text=True)
        branches = re.findall(r'\bblx?\s+([0-9a-f]+)\s+<', disassembly)
        targets = {destination(int(value, 16)) for value in branches}
        for name in ['pthread_once', 'pthread_mutex_lock', 'pthread_cond_wait']:
            assert symbols[name]['st_value'] & ~1 in targets, \
                '__cxa_guard_acquire does not call the real ' + name

        report = {'pthread_cancel': hex(cancel['st_value']),
                  'pthread_implementations': {n: hex(s['st_value']) for n, s in sorted(pthread_symbols.items())},
                  'unresolved_pthread_primitives': unresolved,
                  'cpp_guard_calls_once_mutex_and_cond_wait': True,
                  'cpp_guard_disassembly': disassembly,
                  'gcc_active_probe_pointer': hex(active_pointer),
                  'std_thread_probe_pointer': hex(start_pointer),
                  'native_fiber_symbols': {n: hex(symbols[n]['st_value']) for n in required},
                  'raw_libco_switch_absent': True, 'runtime_boot_checks_present': True,
                  'scope': 'linked ARM instruction and symbol audit; firmware behavior requires the Vita boot checks'}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: GCC TLS and std::thread availability probes resolve the real pthread_cancel.')
    print('PASS: all referenced pthread primitives resolve; C++ static guards call real once/mutex/condition implementations.')
    print('PASS: native SceFiber and boot checks linked; raw libco context switch absent.')


if __name__ == '__main__':
    main()
