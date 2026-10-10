#!/usr/bin/env python3
"""Host-only regression tests for 60 FPS budget summaries."""
from pathlib import Path
import tempfile
import unittest

from analyze_vita_frame_budget import analyze, parse_latest_session, weighted_percentile


class FrameBudgetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "runtime.log"

    def write(self, value):
        self.path.write_text(value, encoding="utf-8")
        return self.path

    def test_latest_boot_and_no_capture_overhead(self):
        path = self.write(
            "[boot] build=old\n"
            "[runtime-profile] frame=60 frames=60 drawn_frames=60 wall_us=60000000 "
            "fifo_us=40000000 native_request_us=25000 captures=0 diagnostic=1\n"
            "[boot] build=new\n"
            "[race-progress] frame=100 stage=2 scene_paused=0 section_paused=0 controller_paused=0\n"
            "[runtime-profile] frame=120 frames=20 drawn_frames=20 wall_us=400000 "
            "fifo_us=200000 native_request_us=50000 captures=0 diagnostic=0\n"
            "[runtime-profile] frame=140 frames=20 drawn_frames=10 wall_us=200000 "
            "fifo_us=50000 native_request_us=30000 captures=0 diagnostic=0\n"
            "[runtime-profile] frame=160 frames=20 drawn_frames=20 wall_us=500000 "
            "fifo_us=400000 native_request_us=90000 captures=1 diagnostic=1\n"
        )
        report = analyze(path, stage=2)
        self.assertEqual(report["boot_marker"], "new")
        self.assertEqual(report["window_count"], 2)
        self.assertEqual(report["frames"], 40)
        self.assertEqual(report["drawn_frames"], 30)
        self.assertAlmostEqual(report["fps"], 40 / 0.6)
        self.assertAlmostEqual(report["mean_ms"], 15)
        self.assertAlmostEqual(report["p50_ms"], 10)
        self.assertAlmostEqual(report["p95_ms"], 20)
        self.assertAlmostEqual(report["fifo_ms_per_frame"], 6.25)
        self.assertAlmostEqual(report["fifo_wall_fraction"], 250000 / 600000)
        self.assertTrue(report["passes_60_wall_budget"])
        self.assertFalse(report["passes_60_window_p95_budget"])
        self.assertEqual(report["paused_windows"], 0)
        self.assertEqual(analyze(path, stage=2, unpaused_only=True)["frames"], 40)
        self.assertFalse(report["performance_proven_in_game"])
        with_capture = analyze(path, stage=2, include_captures=True)
        self.assertEqual(with_capture["frames"], 60)
        self.assertEqual(with_capture["captures_included"], 1)
        self.assertEqual(with_capture["diagnostic_windows"], 1)

    def test_filter_by_stage_and_min_frame(self):
        path = self.write(
            "[boot] build=new\n"
            "[race-progress] frame=30 stage=1 scene_paused=1 section_paused=0 controller_paused=0\n"
            "[runtime-profile] frame=35 frames=5 drawn_frames=5 wall_us=100000 captures=0\n"
            "[race-progress] frame=60 stage=2 scene_paused=0 section_paused=0 controller_paused=0\n"
            "[runtime-profile] frame=70 frames=10 drawn_frames=10 wall_us=200000 captures=0\n"
            "[runtime-profile] frame=80 frames=10 drawn_frames=10 wall_us=100000 captures=0\n"
        )
        report = analyze(path, stage=2, min_frame=75)
        self.assertEqual(report["frames"], 10)
        self.assertEqual(report["max_end_frame"], 80)
        with self.assertRaisesRegex(ValueError, "no matching"):
            analyze(path, stage=3)
        self.assertEqual(analyze(path, unpaused_only=True)["frames"], 20)
        self.assertEqual(analyze(path)["paused_windows"], 1)

    def test_quiet_cadence_does_not_invent_fifo_drawn_gpu_timings(self):
        path = self.write(
            "[boot] build=quiet\n"
            "[race-progress] frame=100 stage=2 scene_paused=0 section_paused=0 controller_paused=0\n"
            "[runtime-cadence] frame=120 frames=20 wall_us=20000000 present_hz=1.000 attribution=0\n"
            "[runtime-cadence] frame=140 frames=20 wall_us=10000000 present_hz=2.000 attribution=0\n"
        )
        report = analyze(path, stage=2, unpaused_only=True)
        self.assertEqual(report["frames"], 40)
        self.assertEqual(report["cadence_windows"], 2)
        self.assertEqual(report["profile_windows"], 0)
        self.assertEqual(report["diagnostic_windows"], 0)
        self.assertEqual(report["paused_windows"], 0)
        self.assertEqual(report["unknown_pause_windows"], 0)
        self.assertIsNone(report["drawn_frames"])
        self.assertIsNone(report["drawn_fps"])
        self.assertIsNone(report["fifo_ms_per_frame"])
        self.assertIsNone(report["fifo_wall_fraction"])
        self.assertIsNone(report["native_ms_per_frame"])
        self.assertAlmostEqual(report["fps"], 4 / 3)
        self.assertAlmostEqual(report["mean_ms"], 750)

    def test_mixed_quiet_and_profile_does_not_misattribute_subtimers(self):
        path = self.write(
            "[boot] build=mixed\n"
            "[runtime-cadence] frame=60 frames=60 wall_us=60000000 present_hz=1 attribution=0\n"
            "[runtime-profile] frame=70 frames=10 drawn_frames=10 wall_us=5000000 "
            "fifo_us=2000000 native_request_us=100000 captures=0 diagnostic=1\n"
        )
        result = analyze(path)
        self.assertEqual(result["profile_windows"], 1)
        self.assertEqual(result["cadence_windows"], 1)
        self.assertEqual(result["diagnostic_windows"], 1)
        self.assertIsNone(result["fifo_ms_per_frame"])
        self.assertIsNone(result["drawn_fps"])

    def test_bad_windows_ignored(self):
        path = self.write(
            "[boot] build=new\n"
            "[runtime-profile] frame=10 frames=0 wall_us=10000 captures=0\n"
            "[runtime-profile] frame=20 frames=10 wall_us=oops captures=0\n"
            "[runtime-profile] frame=10 frames=10 drawn_frames=10 wall_us=100000 captures=0\n"
        )
        marker, windows = parse_latest_session(path)
        self.assertEqual(marker, "new")
        self.assertEqual(len(windows), 1)
        self.assertAlmostEqual(weighted_percentile(windows, 90), 10)

    def test_empty_log_rejected(self):
        with self.assertRaisesRegex(ValueError, "no matching"):
            analyze(self.write("[boot] build=new\n"))


if __name__ == "__main__":
    unittest.main()
