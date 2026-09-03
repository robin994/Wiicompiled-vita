#!/usr/bin/env python3
"""Resolve gx_begin_hot caller LRs against the generated guest symbol map."""

import argparse
import ast
import bisect
import re
import sys
from pathlib import Path


BOOT_MARKER = "[BOOT] WiiCompiled Vita runtime start"
HOT_RE = re.compile(
    r"gx_begin_hot frame=(\d+) rank=(\d+) lr=([0-9a-fA-F]+) count=(\d+) total=(\d+)"
)


def read_guest_symbols(path: Path):
    source = path.read_text(encoding="utf-8")

    address_block = source.split("const uint32_t kGuestMapSymbolAddresses[] = {", 1)[1].split("};", 1)[0]
    name_block = source.split("const char* const kGuestMapSymbolNames[] = {", 1)[1].split("};", 1)[0]
    addresses = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)u", address_block)]
    names = [ast.literal_eval(value) for value in re.findall(r'"(?:\\.|[^"\\])*"', name_block)]
    if len(addresses) != len(names):
        raise ValueError(f"symbol map length mismatch: {len(addresses)} addresses, {len(names)} names")
    return addresses, names


def resolve(address, addresses, names):
    index = bisect.bisect_right(addresses, address) - 1
    if index < 0 or address - addresses[index] >= 0x10000:
        return "?"
    offset = address - addresses[index]
    return names[index] if offset == 0 else f"{names[index]}+0x{offset:X}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("runtime_log", type=Path, help="runtime.log, or - for stdin")
    parser.add_argument(
        "--symbols",
        type=Path,
        default=Path("generated/guest_symbol_table.cpp"),
        help="generated guest symbol table (default: generated/guest_symbol_table.cpp)",
    )
    args = parser.parse_args()

    text = sys.stdin.read() if str(args.runtime_log) == "-" else args.runtime_log.read_text(encoding="utf-8", errors="replace")
    last_boot = text.rfind(BOOT_MARKER)
    if last_boot >= 0:
        text = text[last_boot:]
    addresses, names = read_guest_symbols(args.symbols)

    for line in text.splitlines():
        match = HOT_RE.search(line)
        if not match:
            continue
        address = int(match.group(3), 16)
        symbol = resolve(address, addresses, names)
        print(f"{line} symbol={symbol}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, IndexError, ValueError) as error:
        print(f"decode_gx_begin_hot.py: {error}", file=sys.stderr)
        sys.exit(2)
