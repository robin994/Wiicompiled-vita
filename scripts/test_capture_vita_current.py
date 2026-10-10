#!/usr/bin/env python3
"""Host-only tests for the WiiCompiled read-only Vita session capture."""
import hashlib
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from capture_vita_current import fetch_log_tail, latest_boot, stream_sha256


class MemoryFTP:
    def __init__(self, files):
        self.files = files
        self.commands = []

    def voidcmd(self, command):
        self.commands.append(command)

    def size(self, path):
        return len(self.files[path])

    def retrbinary(self, command, callback, *, rest=None, **_):
        self.commands.append(command)
        path = command.removeprefix("RETR ")
        chunk = self.files[path]
        callback(chunk[rest:] if rest is not None else chunk)


class CaptureTests(unittest.TestCase):
    def test_latest_session_excludes_prior_footage(self):
        data = (
            b"[boot] build=first\n[frame-health] frame=2\n"
            b"[boot] build=second\n[runtime-cadence] frame=70\n"
        )
        self.assertEqual(latest_boot(data), data[data.index(b"[boot] build=second"):])
        self.assertNotIn(b"build=first", latest_boot(data))

    def test_missing_boot_marker_is_not_misattributed(self):
        with self.assertRaisesRegex(ValueError, "increase --tail-mb"):
            latest_boot(b"[runtime-cadence] frame=100\n")

    def test_midline_marker_rejected(self):
        with self.assertRaisesRegex(ValueError, "line-aligned"):
            latest_boot(b"noise [boot] build=bad\n")

    def test_file_hash_stream_read_only(self):
        content = b"test SELF" * 100
        ftp = MemoryFTP({"self": content})
        sha, size = stream_sha256(ftp, "self")
        self.assertEqual(sha, hashlib.sha256(content).hexdigest())
        self.assertEqual(size, len(content))
        self.assertEqual(ftp.commands, ["RETR self"])

    def test_ftp_tail_uses_rest_instead_of_full_log(self):
        path = "ux0:/data/wiicompiled-vita/runtime.log"
        data = b"A" * (2 * 1024 * 1024) + b"\n[boot] build=latest\n"
        ftp = MemoryFTP({path: data})
        tail, total, offset = fetch_log_tail(ftp, tail_mb=1)
        self.assertEqual(total, len(data))
        self.assertEqual(offset, len(data) - 1024 * 1024)
        self.assertEqual(tail, data[offset:])
        self.assertEqual(latest_boot(tail), b"[boot] build=latest\n")
        self.assertIn("TYPE I", ftp.commands)


if __name__ == "__main__":
    unittest.main()
