#!/usr/bin/env python3
"""Execute the linked ARM coroutine switch and check its hard-float ABI.

Requires pyelftools and Unicorn. Takes the unstripped Vita ELF, so this tests
the instructions that will actually ship rather than a second implementation.
"""
import argparse
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn import arm_const as registers


def swap_code(path):
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        symbols = [symbol for symbol in elf.get_section_by_name('.symtab').iter_symbols()
                   if symbol.name == 'co_swap_function']
        assert len(symbols) == 1, 'Could not identify the linked libco ARM switch'
        symbol = symbols[0]
        section = elf.get_section(symbol['st_shndx'])
        offset = symbol['st_value'] - section['sh_addr']
        return section.data()[offset:offset + symbol['st_size']]


def check_roundtrip(code, thumb_entry=False):
    emulator = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for address, length in ((0x10000, 0x1000), (0x20000, 0x1000),
                            (0x30000, 0x4000), (0x40000, 0x4000), (0x50000, 0x1000)):
        emulator.mem_map(address, length)
    emulator.mem_write(0x10000, code)
    emulator.reg_write(registers.UC_ARM_REG_C1_C0_2, 0x00f00000)
    emulator.reg_write(registers.UC_ARM_REG_FPEXC, 0x40000000)
    general = [getattr(registers, 'UC_ARM_REG_R' + str(i)) for i in range(4, 12)]
    floating = [getattr(registers, 'UC_ARM_REG_D' + str(i)) for i in range(8, 16)]
    first = [0x12340000 + i for i in range(8)]
    second = [0x56780000 + i for i in range(8)]
    first_fp = [0x3ff0000000000000 + i for i in range(8)]
    second_fp = [0x4000000000000000 + i for i in range(8)]
    source_sp = 0x33f00
    target_sp = 0x43f00
    target_pc = 0x50010 | int(thumb_entry)
    emulator.mem_write(0x20100, struct.pack('<10I', *second, target_sp - 64, target_pc))
    emulator.mem_write(target_sp - 64, struct.pack('<8Q', *second_fp))
    for register, value in zip(general + floating, first + first_fp):
        emulator.reg_write(register, value)
    emulator.reg_write(registers.UC_ARM_REG_SP, source_sp)
    emulator.reg_write(registers.UC_ARM_REG_LR, 0x50000)
    emulator.reg_write(registers.UC_ARM_REG_R0, 0x20100)
    emulator.reg_write(registers.UC_ARM_REG_R1, 0x20000)
    emulator.emu_start(0x10000, target_pc & ~1, count=32)
    assert [emulator.reg_read(r) for r in general] == second, 'Integer registers were not restored'
    assert [emulator.reg_read(r) for r in floating] == second_fp, 'd8-d15 were not restored'
    assert emulator.reg_read(registers.UC_ARM_REG_SP) == target_sp, 'Target stack frame imbalance'
    assert bool(emulator.reg_read(registers.UC_ARM_REG_CPSR) & 0x20) == thumb_entry, 'ARM/Thumb entry lost'
    assert bytes(emulator.mem_read(source_sp - 64, 64)) == struct.pack('<8Q', *first_fp), 'd8-d15 were not saved'
    if thumb_entry:
        return
    # Simulate the resumed coroutine changing every callee-saved FP register.
    for register, value in zip(floating, [x + 1024 for x in second_fp]):
        emulator.reg_write(register, value)
    emulator.reg_write(registers.UC_ARM_REG_R0, 0x20000)
    emulator.reg_write(registers.UC_ARM_REG_R1, 0x20100)
    emulator.reg_write(registers.UC_ARM_REG_LR, 0x50020)
    emulator.emu_start(0x10000, 0x50000, count=32)
    assert [emulator.reg_read(r) for r in general] == first, 'Integer roundtrip failed'
    assert [emulator.reg_read(r) for r in floating] == first_fp, 'Floating-point roundtrip failed'
    assert emulator.reg_read(registers.UC_ARM_REG_SP) == source_sp, 'Source stack frame imbalance'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    code = swap_code(args.elf)
    check_roundtrip(code)
    check_roundtrip(code, thumb_entry=True)
    print('PASS: linked ARM switch preserves r4-r11, d8-d15, SP and ARM/Thumb continuation')


if __name__ == '__main__':
    main()
