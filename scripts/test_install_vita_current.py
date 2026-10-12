#!/usr/bin/env python3
"""Host-only fake-FTP tests for deploying the current WiiCompiled SELF."""
import ftplib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from install_vita_current import (activate, configure, digest, verify_build,
                                   write_toml_value, main)
from install_vita_current import file_digest


class FakeFTP:
    def __init__(self, files=None, fail_rename_target=None):
        self.files = dict(files or {})
        self.fail_rename_target = fail_rename_target
        self.failed = False

    def storbinary(self, command, source, **_):
        op, path = command.split(" ", 1)
        if op != "STOR":
            raise AssertionError(command)
        self.files[path] = source.read()

    def retrbinary(self, command, callback, **_):
        op, path = command.split(" ", 1)
        if op != "RETR":
            raise AssertionError(command)
        callback(self.files[path])

    def rename(self, old, new):
        if (new == self.fail_rename_target and not self.failed):
            self.failed = True
            raise ftplib.error_perm("550 simulated interrupted activation")
        if old not in self.files:
            raise ftplib.error_perm("550 no such file")
        if new in self.files:
            raise ftplib.error_perm("550 already exists")
        self.files[new] = self.files.pop(old)


class InstallCurrentTests(unittest.TestCase):
    ORIGINAL = (b"# Keep my NAND and ISO settings\n"
                b"[paths]\ndvd_psarc = \"ux0:data/wiicompiled-vita/mariokart.psarc\"\n"
                b"[test]\ntime_trial = true # old run\n"
                b"[diagnostics]\nenabled = true\n"
                b"[audio]\nlegacy_stop_dma_on_queue_full = true\n"
                b"[vita]\nshader_binary_preload = true\n")

    def test_menu_a_b_changes_only_audio_between_runs(self):
        baseline = configure(self.ORIGINAL, menu=True, quiet=True,
                             legacy_audio=False)
        self.assertIn(b"time_trial = false # old run", baseline)
        self.assertIn(b"enabled = false", baseline)
        self.assertIn(b"legacy_stop_dma_on_queue_full = false", baseline)
        assert baseline != self.ORIGINAL
        legacy = configure(baseline, menu=False, quiet=False,
                           legacy_audio=True, audio_only=True)
        self.assertEqual(legacy.replace(b"legacy_stop_dma_on_queue_full = true",
                                        b"legacy_stop_dma_on_queue_full = false"),
                         baseline)
        self.assertEqual(configure(legacy, menu=True, quiet=True,
                                   legacy_audio=False, audio_only=True),
                         baseline)

    def test_default_sections_and_crlf_are_preserved(self):
        raw = b"[paths]\r\ndvd_psarc = \"file.psarc\"\r\n"
        result = configure(raw, menu=True, quiet=True, legacy_audio=False)
        self.assertIn(b"[test]\r\ntime_trial = false\r\n", result)
        self.assertIn(b"[audio]\r\nlegacy_stop_dma_on_queue_full = false\r\n", result)
        self.assertIn(b'dvd_psarc = "file.psarc"', result)
        self.assertNotIn(b"\n\n", result)
        self.assertEqual(configure(result, menu=True, quiet=True,
                                   legacy_audio=False), result)

    def test_batching_a_b_does_not_modify_game_or_audio(self):
        baseline = configure(self.ORIGINAL, menu=False, quiet=True,
                             legacy_audio=False, draw_batching=False)
        batch = configure(baseline, menu=True, quiet=False, legacy_audio=True,
                          batching_only=True, draw_batching=True)
        self.assertEqual(
            batch.replace(b"draw_batching = true", b"draw_batching = false"),
            baseline)
        self.assertIn(b"time_trial = true # old run", batch)
        self.assertIn(b"legacy_stop_dma_on_queue_full = false", batch)
        self.assertEqual(configure(batch, menu=True, quiet=False,
                                   legacy_audio=True, batching_only=True,
                                   draw_batching=False), baseline)
        with self.assertRaisesRegex(ValueError, "requires"):
            configure(baseline, menu=True, quiet=True, legacy_audio=False,
                      batching_only=True)

    def test_budget_probe_only_keeps_game_audio_and_batch_profile(self):
        baseline = configure(self.ORIGINAL, menu=False, quiet=True,
                             legacy_audio=False, draw_batching=True)
        on = configure(baseline, menu=True, quiet=False, legacy_audio=True,
                       probe_only=True, render_budget_probe=True)
        self.assertIn(b"render_budget_probe = true", on)
        self.assertIn(b"draw_batching = true", on)
        self.assertIn(b"time_trial = true # old run", on)
        self.assertIn(b"legacy_stop_dma_on_queue_full = false", on)
        off = configure(on, menu=True, quiet=False, legacy_audio=True,
                        probe_only=True, render_budget_probe=False)
        self.assertEqual(configure(off, menu=True, quiet=False, legacy_audio=True,
                                   probe_only=True, render_budget_probe=True), on)
        with self.assertRaisesRegex(ValueError, "requires"):
            configure(on, menu=True, quiet=False, legacy_audio=True, probe_only=True)

    def test_visual_shader_ab_changes_exactly_one_flag(self):
        baseline = configure(self.ORIGINAL, menu=False, quiet=True,
                             legacy_audio=False, draw_batching=True,
                             render_budget_probe=True,
                             gpu_lighting=True, native_materials=True)
        cpu = configure(baseline, menu=True, quiet=False, legacy_audio=True,
                        visual_only=True, gpu_lighting=False)
        self.assertEqual(cpu.replace(b"gpu_lighting = false", b"gpu_lighting = true"),
                         baseline)
        self.assertIn(b"native_materials = true", cpu)
        self.assertIn(b"draw_batching = true", cpu)
        self.assertIn(b"render_budget_probe = true", cpu)
        reference = configure(cpu, menu=True, quiet=False, legacy_audio=True,
                              visual_only=True, native_materials=False)
        self.assertIn(b"native_materials = false", reference)
        self.assertIn(b"gpu_lighting = false", reference)
        restored = configure(reference, menu=True, quiet=False, legacy_audio=True,
                             visual_only=True, gpu_lighting=True)
        self.assertIn(b"gpu_lighting = true", restored)
        with self.assertRaisesRegex(ValueError, "exactly one"):
            configure(baseline, menu=True, quiet=True, legacy_audio=False,
                      visual_only=True, gpu_lighting=True, native_materials=False)
        with self.assertRaisesRegex(ValueError, "exactly one"):
            configure(baseline, menu=True, quiet=True, legacy_audio=False,
                      visual_only=True)

    def test_capture_frame_integer_and_existing_config_preserved(self):
        data = configure(self.ORIGINAL, menu=False, quiet=False,
                         legacy_audio=False, draw_batching=True,
                         capture_frame=690)
        self.assertIn(b"capture_frame = 690", data)
        self.assertIn(b"draw_batching = true", data)
        self.assertIn(b"shader_binary_preload = true", data)
        self.assertIn(b'dvd_psarc = "ux0:data/wiicompiled-vita/mariokart.psarc"', data)
        self.assertEqual(configure(data, menu=False, quiet=False, legacy_audio=False,
                                   draw_batching=True, capture_frame=690), data)
        with self.assertRaisesRegex(ValueError, "Invalid type"):
            write_toml_value(b'[diagnostics]\ncapture_frame = "700"\n',
                             "diagnostics", "capture_frame", 700)

    def test_precomputed_a_b_a_preserves_all_other_settings(self):
        baseline = configure(self.ORIGINAL, menu=False, quiet=True,
                             legacy_audio=False, draw_batching=True,
                             render_budget_probe=True, precomputed_geometry=False)
        candidate = configure(baseline, menu=True, quiet=False, legacy_audio=True,
                              precomputed_only=True, precomputed_geometry=True)
        self.assertEqual(candidate.replace(b"precomputed_geometry = true",
                                            b"precomputed_geometry = false"), baseline)
        self.assertEqual(configure(candidate, menu=True, quiet=False, legacy_audio=True,
                                   precomputed_only=True, precomputed_geometry=False), baseline)
        self.assertEqual(configure(candidate, menu=True, quiet=False, legacy_audio=True,
                                   precomputed_only=True, precomputed_geometry=True), candidate)
        with self.assertRaisesRegex(ValueError, "requires"):
            configure(baseline, menu=True, quiet=True, legacy_audio=False,
                      precomputed_only=True)

    def test_native_recipe_a_b_a_does_not_modify_previous_options(self):
        control = configure(self.ORIGINAL, menu=False, quiet=True,
                            legacy_audio=False, draw_batching=True,
                            precomputed_geometry=True, prepared_display_lists=False)
        enabled = configure(control, menu=True, quiet=False, legacy_audio=True,
                            recipe_only=True, prepared_display_lists=True)
        self.assertEqual(enabled.replace(b"prepared_display_lists = true",
                                         b"prepared_display_lists = false"), control)
        self.assertIn(b"time_trial = true # old run", enabled)
        self.assertIn(b"precomputed_geometry = true", enabled)
        self.assertEqual(configure(enabled, menu=True, quiet=False,
                                   legacy_audio=True, recipe_only=True,
                                   prepared_display_lists=False), control)
        with self.assertRaisesRegex(ValueError, "requires"):
            configure(control, menu=True, quiet=True, legacy_audio=False,
                      recipe_only=True)

    def test_native_recipe_cli_rejects_mixed_controls_early(self):
        with mock.patch("install_vita_current.verify_build") as verify:
            for extra in ([], ["--time-trial"], ["--render-batching", "on"],
                          ["--precomputed-geometry", "on"],
                          ["--efb-only"], ["--native-materials", "reference"]):
                with self.subTest(extra=extra), mock.patch("sys.stderr", io.StringIO()):
                    args = ["--recipe-only"]
                    if extra:
                        args += ["--prepared-display-lists", "on"] + extra
                    with self.assertRaises(SystemExit) as error:
                        main(args)
                    self.assertEqual(error.exception.code, 2)
            verify.assert_not_called()

    def test_native_lyt_a_b_a_preserves_all_other_settings(self):
        control=configure(self.ORIGINAL,menu=False,quiet=True,legacy_audio=False,
                          prepared_display_lists=True,native_lyt_packets=False)
        enabled=configure(control,menu=True,quiet=False,legacy_audio=True,
                          lyt_only=True,native_lyt_packets=True)
        self.assertEqual(enabled.replace(b"native_lyt_packets = true",
                                         b"native_lyt_packets = false"),control)
        self.assertIn(b"time_trial = true # old run",enabled)
        self.assertIn(b"prepared_display_lists = true",enabled)
        self.assertEqual(configure(enabled,menu=True,quiet=False,legacy_audio=True,
                                   lyt_only=True,native_lyt_packets=False),control)
        with self.assertRaisesRegex(ValueError,"requires"):
            configure(control,menu=True,quiet=True,legacy_audio=False,lyt_only=True)

    def test_native_lyt_rejects_other_partial_cli_modes(self):
        with mock.patch("install_vita_current.verify_build") as verify:
            for args in (["--lyt-only"],
                         ["--lyt-only","--native-lyt-packets","on","--time-trial"],
                         ["--lyt-only","--native-lyt-packets","on","--recipe-only"],
                         ["--lyt-only","--native-lyt-packets","on","--precomputed-geometry","on"],
                         ["--native-lyt-packets","on","--batch-only","--render-batching","on"]):
                with self.subTest(args=args),mock.patch("sys.stderr",io.StringIO()):
                    with self.assertRaises(SystemExit) as err:main(args)
                    self.assertEqual(err.exception.code,2)
            verify.assert_not_called()

    def test_efb_copy_and_retirement_ab_without_replacing_self(self):
        baseline=configure(self.ORIGINAL,menu=False,quiet=True,legacy_audio=False,
                           draw_batching=True,precomputed_geometry=True,
                           async_efb_copies=False,deferred_efb_targets=False)
        async_copy=configure(baseline,menu=True,quiet=False,legacy_audio=True,
                             efb_only=True,async_efb_copies=True)
        self.assertEqual(async_copy.replace(b"async_efb_copies = true",
                                             b"async_efb_copies = false"),baseline)
        retired=configure(async_copy,menu=True,quiet=False,legacy_audio=True,
                          efb_only=True,deferred_efb_targets=True)
        self.assertIn(b"precomputed_geometry = true",retired)
        self.assertIn(b"time_trial = true # old run",retired)
        self.assertIn(b"deferred_efb_targets = true",retired)
        no_retire=configure(retired,menu=True,quiet=False,legacy_audio=True,
                            efb_only=True,deferred_efb_targets=False)
        self.assertEqual(no_retire,async_copy)
        restored=configure(no_retire,menu=True,quiet=False,legacy_audio=True,
                           efb_only=True,async_efb_copies=False)
        self.assertEqual(restored,baseline)
        for controls in ({},dict(async_efb_copies=True,deferred_efb_targets=True)):
            with self.subTest(controls=controls),self.assertRaisesRegex(ValueError,"exactly one"):
                configure(baseline,menu=False,quiet=True,legacy_audio=False,
                          efb_only=True,**controls)

    def test_efb_only_invalid_combinations_fail_before_build_or_ftp(self):
        with mock.patch("install_vita_current.verify_build") as verify:
            for extra in (["--deferred-efb-targets","on"],
                          ["--time-trial"],["--precomputed-geometry","on"],
                          ["--batch-only"],["--render-batching","on"]):
                with self.subTest(extra=extra),mock.patch("sys.stderr",io.StringIO()):
                    with self.assertRaises(SystemExit) as error:
                        main(["--efb-only","--async-efb-copies","on"]+extra)
                    self.assertEqual(error.exception.code,2)
            verify.assert_not_called()

    def test_precomputed_cli_rejects_mixed_controls_before_build_or_ftp(self):
        with mock.patch("install_vita_current.verify_build") as verify:
            for extra in ([], ["--batch-only"], ["--time-trial"],
                          ["--budget-probe", "off"], ["--gpu-lighting", "cpu"]):
                args = ["--precomputed-only"]
                if extra:
                    args += ["--precomputed-geometry", "on"] + extra
                with self.subTest(extra=extra), mock.patch("sys.stderr", io.StringIO()):
                    with self.assertRaises(SystemExit) as error:
                        main(args)
                    self.assertEqual(error.exception.code, 2)
            verify.assert_not_called()

    def test_precomputed_cli_requires_matching_installed_self(self):
        payloads = {"eboot.bin": b"current candidate"}
        delivery = {"head": "a" * 40, "aurora_pin": "b" * 40}
        with tempfile.TemporaryDirectory() as tmp, \
                mock.patch("install_vita_current.verify_build", return_value=(payloads, delivery)), \
                mock.patch("install_vita_current.ftplib.FTP"), \
                mock.patch("install_vita_current.read_remote", return_value=b"old executable"), \
                mock.patch("builtins.print"):
            with self.assertRaisesRegex(RuntimeError, "current SELF"):
                main(["--build-dir", tmp, "--precomputed-only",
                      "--precomputed-geometry", "on"])

    def test_malformed_setting_does_not_replace_other_options(self):
        raw = b"[audio]\nlegacy_stop_dma_on_queue_full = \"false\"\n"
        with self.assertRaisesRegex(ValueError, "Invalid type"):
            write_toml_value(raw, "audio", "legacy_stop_dma_on_queue_full", True)

    def test_installer_keeps_backup_and_preserves_unrelated_files(self):
        original = {"self": b"previous self", "config": b"[test]\ntime_trial = true\n"}
        data = {**original, "psarc": b"unchanged PSARC", "save": b"player save"}
        ftp = FakeFTP(data)
        changes = [("self", b"next self"), ("config", b"next config")]
        backups = activate(ftp, changes, original, "20261010")
        self.assertEqual(ftp.files["self"], b"next self")
        self.assertEqual(ftp.files["config"], b"next config")
        self.assertEqual(ftp.files[backups["self"]], b"previous self")
        self.assertEqual(ftp.files[backups["config"]], original["config"])
        self.assertEqual(ftp.files["psarc"], b"unchanged PSARC")
        self.assertEqual(ftp.files["save"], b"player save")

    def test_installer_rolls_back_two_files_if_activation_fails(self):
        original = {"self": b"old self", "config": b"old config"}
        ftp = FakeFTP(original, fail_rename_target="config")
        with self.assertRaises(ftplib.error_perm):
            activate(ftp, [("self", b"new self"), ("config", b"new config")],
                     original, "20261010")
        self.assertEqual(ftp.files["self"], original["self"])
        self.assertEqual(ftp.files["config"], original["config"])

    def test_changed_files_during_stage_prevent_activation(self):
        original = {"self": b"old self"}
        ftp = FakeFTP(original)
        before = ftp.files.copy()

        def modified_stor(command, source, **kwargs):
            FakeFTP.storbinary(ftp, command, source, **kwargs)
            ftp.files["self"] = b"external change"

        ftp.storbinary = modified_stor
        with self.assertRaisesRegex(RuntimeError, "Source changed"):
            activate(ftp, [("self", b"new self")], original, "20261010")
        self.assertEqual(ftp.files["self"], b"external change")
        self.assertNotIn("self.backup-20261010", ftp.files)

    def test_current_local_artifacts_must_have_fresh_provenance(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(FileNotFoundError):
                verify_build(Path(folder))
        self.assertEqual(digest(b"a"), digest(b"a"))

    def test_package_provenance_and_async_gate(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self_file = folder / "game-eboot.bin"
            self_file.write_bytes(b"new executable")
            exe = folder / "wiicompiled-vita-game"
            exe.write_bytes(b"test ELF")
            package = folder / "wiicompiled-vita-game.vpk"
            with zipfile.ZipFile(package, "w") as archive:
                archive.writestr("eboot.bin", self_file.read_bytes())
                archive.writestr("sce_sys/param.sfo", b"synthetic SFO")
                archive.writestr("wii_bootstrap/settings.dat", b"fake data")
            files = [{"path": str(p.resolve()), "sha256": file_digest(p)}
                     for p in (exe, self_file, package)]
            delivery = {
                "head": "b" * 40, "aurora_pin": "a" * 40,
                "source_and_generated_sha256": "current-source-digest",
                "source_files_hashed": 3,
                "cmake_options": {
                    "WIC_BUILD_GAME:BOOL": "ON",
                    "AURORA_VITA_RENDERER:STRING": "GXM",
                    "AURORA_VITA_ASYNC_GX:BOOL": "ON",
                    "WIC_NATIVE_DIRECT_SUBMIT:BOOL": "OFF",
                    "WIC_NATIVE_DIRECT_STREAM:BOOL": "OFF",
                },
                "artifacts": [{"product": "wiicompiled-vita-game",
                               "packaged_eboot_matches": True, "files": files}],
            }
            def git_id(cmd, **_):
                return ("a" if any(str(p).endswith("aurora-main") for p in cmd)
                        else "b") * 40 + "\n"
            def save():
                (folder / "DELIVERY.json").write_text(json.dumps(delivery))
            with mock.patch("install_vita_current.subprocess.check_output",
                            side_effect=git_id), mock.patch("record_vita_delivery.source_digest",
                            return_value=("current-source-digest",3)) as source, mock.patch("vita_sfo.verify_package_sfo",
                                                            return_value={"ATTRIBUTE2": 12}):
                save()
                contents, _ = verify_build(folder)
                self.assertEqual(contents["eboot.bin"], b"new executable")
                source.return_value=("stale-source-digest",3)
                with self.assertRaisesRegex(ValueError, "rebuild Vita"):
                    verify_build(folder)
                source.return_value=("current-source-digest",3)
                delivery["source_files_hashed"]=4
                save()
                with self.assertRaisesRegex(ValueError, "rebuild Vita"):
                    verify_build(folder)
                delivery["source_files_hashed"]=3
                save()
                delivery["cmake_options"]["AURORA_VITA_ASYNC_GX:BOOL"] = "OFF"
                save()
                with self.assertRaisesRegex(ValueError, "async options"):
                    verify_build(folder)
                delivery["cmake_options"]["AURORA_VITA_ASYNC_GX:BOOL"] = "ON"
                delivery["cmake_options"]["WIC_NATIVE_DIRECT_STREAM:BOOL"] = "ON"
                save()
                with self.assertRaisesRegex(ValueError, "P0 baseline"):
                    verify_build(folder)
                delivery["cmake_options"]["WIC_NATIVE_DIRECT_STREAM:BOOL"] = "OFF"
                save()
                exe.write_bytes(b"tampered ELF")
                with self.assertRaisesRegex(ValueError, "Stale/incorrect"):
                    verify_build(folder)


if __name__ == "__main__":
    unittest.main()
