#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_vita_render_budget import parse_budget, describe


class BudgetTests(unittest.TestCase):
    def test_joins_worker_and_gx_after_completed_frame(self):
        lines = (
            "[race-progress] frame=25 stage=0\n"
            "[wic-render-budget] frame=32 worker_frame_us=1000000 native_draws=310 sampled=1 "
            "draw_us=250000 display_queue_us=35 native_finish=2\n"
            "[wic-draw-health] completed_frame=32 submitted=480 requested=495 merged=170 "
            "efb_copies=12 efb_flush_us=450000 efb_capture_us=14000 "
            "white_fallback_draws=0\n"
            "[race-progress] frame=60 stage=2\n"
            "[wic-render-budget] frame=64 worker_frame_us=500000 native_draws=140 sampled=1\n"
        )
        rows = parse_budget(lines)
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["stage"], 0)
        self.assertEqual(rows[1]["stage"], 2)
        self.assertEqual(rows[0]["merged"], 170)
        self.assertIn("efb_flush_ms=450.0", describe(rows[0]))
        self.assertIn("gpu_finishes=2", describe(rows[0]))
        self.assertEqual(rows[0]["native_draws"], 310)

    def test_absent_or_incomplete_frames_not_misattributed(self):
        self.assertEqual(parse_budget("[wic-draw-health] completed_frame=32 merged=100\n"), [])
        rows = parse_budget("[wic-render-budget] frame=32 worker_frame_us=1000\n")
        self.assertEqual(len(rows), 1)
        self.assertIsNone(rows[0]["stage"])
        self.assertIn("merged=?", describe(rows[0]))


if __name__ == "__main__":
    unittest.main()
