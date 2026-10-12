#!/usr/bin/env python3
"""Host-only tests for the WiiCompiled read-only Vita session capture."""
import hashlib
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from capture_vita_current import (fetch_log_tail, latest_boot, stream_sha256,
                                 verify_frame_ppm, verify_session_identity)


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
    def identity_session(self, config=b"[test]\ntime_trial=true\n", self_hash="a" * 64):
        return (b"[boot] build=current\n[boot-identity] self_sha256=" + self_hash.encode() +
                b" config_sha256=" + hashlib.sha256(config).hexdigest().encode() +
                b" config_loaded=1 cpu_mhz=444 gpu_mhz=222\n")

    def test_boot_identity_matches_installed_artifact_and_loaded_config(self):
        config = b"[test]\ntime_trial=true\n"
        identity = verify_session_identity(self.identity_session(config), "a" * 64, config)
        self.assertEqual(identity["state"], "verified")
        self.assertTrue(identity["matches_installed_files"])

    def test_old_session_cannot_be_bound_to_new_installed_self(self):
        with self.assertRaisesRegex(ValueError, "prior session"):
            verify_session_identity(self.identity_session(), "b" * 64,
                                    b"[test]\ntime_trial=true\n")

    def test_config_changed_after_boot_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "different settings"):
            verify_session_identity(self.identity_session(), "a" * 64,
                                    b"[test]\ntime_trial=false\n")

    def test_legacy_evidence_needs_explicit_unverified_archive(self):
        session = b"[boot] build=legacy\n"
        with self.assertRaisesRegex(ValueError, "no artifact/config identity"):
            verify_session_identity(session, "a" * 64, b"config")
        self.assertEqual(verify_session_identity(session, "a" * 64, b"config",
                                                allow_legacy=True)["state"], "legacy_unverified")

    def test_legacy_override_does_not_ignore_conflicting_identity(self):
        with self.assertRaisesRegex(ValueError, "prior session"):
            verify_session_identity(self.identity_session(), "b" * 64, b"config",
                                    allow_legacy=True)

    def test_multiple_or_unavailable_identities_are_rejected(self):
        session = self.identity_session()
        with self.assertRaisesRegex(ValueError, "multiple"):
            verify_session_identity(session + session.splitlines()[1] + b"\n", "a" * 64, b"config")
        with self.assertRaisesRegex(ValueError, "unavailable"):
            verify_session_identity(session.replace(b"a" * 64, b"unavailable"), "a" * 64, b"config")
        with self.assertRaisesRegex(ValueError, "loaded configuration"):
            verify_session_identity(session.replace(b"config_loaded=1", b"config_loaded=0"),
                                    "a" * 64, b"config")

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

    def test_ppm_requires_success_from_latest_boot(self):
        valid = b"P6\n960 544\n255\n" + bytes(960*544*3)
        session = b"[boot] build=latest\n[frame-capture] frame=700 saved=1 path=frame-700.ppm\n"
        self.assertEqual(verify_frame_ppm(session,700,valid),valid)
        with self.assertRaisesRegex(ValueError, "stale"):
            verify_frame_ppm(b"[boot] build=new\n",700,valid)
        with self.assertRaisesRegex(ValueError, "invalid"):
            verify_frame_ppm(session,700,valid[:-1])

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
