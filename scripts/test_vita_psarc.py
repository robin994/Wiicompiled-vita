#!/usr/bin/env python3
from __future__ import annotations

import hashlib
from pathlib import Path
import sys
import tempfile
import subprocess
import struct

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from vita_psarc import BLOCK_SIZE, create_psarc, extract_entry, parse_psarc, verify_psarc


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        disc = root / "disc"
        native = root / "native"
        (disc / "files/Race").mkdir(parents=True)
        (native / "cache").mkdir(parents=True)

        shared = b"shared-payload-" * 5000
        (disc / "files/Race/a.bin").write_bytes(shared)
        (disc / "files/Race/b.bin").write_bytes(shared)
        (disc / "files/full.bin").write_bytes(b"A" * BLOCK_SIZE)
        (disc / "files/partial.bin").write_bytes(b"B" * (BLOCK_SIZE + 37))
        (native / "cache/empty.bin").write_bytes(b"")

        archive = root / "data.psarc"
        report = create_psarc(archive, [disc, native], ignore_case=True, merge_duplicates=True)
        require(report["entries"] == 5, "unexpected entry count")
        require(report["unique_payloads"] == 4, "duplicate payload was not merged")
        require(report["deduplicated_bytes"] == len(shared), "dedup byte count mismatch")

        verified = verify_psarc(archive, full=True)
        require(verified["entries"] == 5, "verify entry count mismatch")

        parsed = parse_psarc(archive)
        require(parsed.flags == 1, "ignore-case flag missing")
        require(parsed.paths == tuple(sorted(parsed.paths, key=lambda p: (p.casefold(), p))),
                "manifest ordering is not deterministic")

        one = extract_entry(archive, "DISC/FILES/RACE/A.BIN")
        two = extract_entry(archive, "disc/files/race/b.bin")
        require(one == shared and two == shared, "case-insensitive extraction failed")

        e1 = parsed.entries[parsed.paths.index("disc/files/Race/a.bin") + 1]
        e2 = parsed.entries[parsed.paths.index("disc/files/Race/b.bin") + 1]
        require((e1.zindex, e1.offset) == (e2.zindex, e2.offset),
                "deduplicated entries do not share payload")

        expected_hash = hashlib.md5("DISC/FILES/RACE/A.BIN".encode("utf-8")).digest()
        require(e1.digest == expected_hash, "ignore-case path hash mismatch")

        require(extract_entry(archive, "disc/files/full.bin") == b"A" * BLOCK_SIZE,
                "full block extraction failed")
        require(extract_entry(archive, "disc/files/partial.bin") == b"B" * (BLOCK_SIZE + 37),
                "partial block extraction failed")
        require(extract_entry(archive, "native/cache/empty.bin") == b"",
                "empty file extraction failed")

        if len(sys.argv)>1:
            probe=sys.argv[1]
            # Only source trees belong below the reference root.
            reference=root/"reference"
            reference.mkdir()
            import shutil
            shutil.copytree(disc,reference/"disc")
            shutil.copytree(native,reference/"native")
            subprocess.run([probe,"verify-root",str(archive),str(reference)],check=True)
            second=root/"second.psarc"
            create_psarc(second,[disc,native],ignore_case=True,merge_duplicates=True)
            require(archive.read_bytes()==second.read_bytes(),"output is not deterministic")
            original=archive.read_bytes()
            for kind,data in [("truncated",original[:20]),
                              ("bad-count",original[:20]+struct.pack(">I",0xffffffff)+original[24:]),
                              ("bad-codec",original[:8]+b"lzma"+original[12:])]:
                corrupt=root/(kind+".psarc");corrupt.write_bytes(data)
                result=subprocess.run([probe,"verify-root",str(corrupt),str(reference)],
                                      stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                require(result.returncode!=0,"native reader accepted "+kind)
            changed=bytearray(original);changed[-1]^=0xff
            corrupt=root/"bad-bytes.psarc";corrupt.write_bytes(changed)
            require(subprocess.run([probe,"verify-root",str(corrupt),str(reference)],
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode!=0,
                    "payload/source difference went undetected")

        # Reusing the hardened Strikers packer must reject unsafe source trees.
        (disc/"link.bin").symlink_to(disc/"files/full.bin")
        rejected=False
        try:create_psarc(root/"unsafe.psarc",[disc])
        except ValueError:rejected=True
        require(rejected,"source symlink accepted")

    print("vita_psarc_tests: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
