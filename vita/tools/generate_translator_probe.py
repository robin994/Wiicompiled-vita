#!/usr/bin/env python3
from pathlib import Path
import struct
import sys


def write_u32_be(buf: bytearray, offset: int, value: int) -> None:
    struct.pack_into(">I", buf, offset, value & 0xFFFFFFFF)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: generate_translator_probe.py <output-dir>", file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    root.mkdir(parents=True, exist_ok=True)

    entry = 0x80001000
    target = 0x80001100
    words = [
        0x48000101,  # bl 0x80001100
        0x4E800020,  # blr
    ]
    # Keep the callee above the translator's leaf-inlining threshold (56 guest
    # instructions) so the probe exercises the emitted state-free call ABI.
    while entry + len(words) * 4 < target:
        words.append(0x60000000)  # nop padding outside either function
    words.extend([0x38630001] * 60)  # addi r3,r3,1
    words.append(0x4E800020)          # blr
    text = b"".join(struct.pack(">I", word) for word in words)

    image = bytearray(0x100 + len(text))
    write_u32_be(image, 0x00, 0x100)       # text section 0 file offset
    write_u32_be(image, 0x48, entry)       # text section 0 address
    write_u32_be(image, 0x90, len(text))   # text section 0 size
    write_u32_be(image, 0xE0, entry)       # entry point
    image[0x100:] = text
    (root / "main.dol").write_bytes(image)

    (root / "recomp.yml").write_text(
        "\n".join([
            "schema_version: 1",
            "project:",
            "  id: vita-arm32-probe",
            "  display_name: Vita ARM32 Translator Probe",
            "memory:",
            "  sda_base: 0x80002000",
            "  sda2_base: 0x80003000",
            "inputs:",
            "  dol:",
            "    path: main.dol",
            "translation:",
            "  entry_points: [0x80001000]",
            "  allow_unsupported_instructions: false",
            "output:",
            "  root: generated",
            "",
        ]),
        encoding="utf-8",
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
