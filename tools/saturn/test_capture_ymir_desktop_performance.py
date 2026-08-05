#!/usr/bin/env python3
"""Host-contract tests for automatic desktop-Ymir performance capture."""

from __future__ import annotations

import sys
import tempfile
import unittest
from datetime import UTC, datetime
from pathlib import Path
from unittest import mock


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

try:
    import capture_ymir_desktop_performance as capture
except ModuleNotFoundError as error:
    raise AssertionError("desktop Ymir performance capture helper is missing") from error


class DesktopYmirPerformanceCaptureTests(unittest.TestCase):
    def test_parse_title_extracts_native_vdp_counters(self) -> None:
        title = (
            "Ymir 0.4.0-dev - [T-99901G] SM64 BOB | Speed: 100% / 100% "
            "| VDP2: 60 fps | VDP1: 4 fps, 917 draws | GUI: 59 fps"
        )

        self.assertEqual(
            capture.parse_performance_title(title),
            {
                "speed_percent": 100,
                "target_speed_percent": 100,
                "vdp2_fps": 60,
                "vdp1_fps": 4,
                "vdp1_draws": 917,
                "gui_fps": 59,
            },
        )

    def test_parse_title_rejects_non_performance_and_no_disc_titles(self) -> None:
        self.assertIsNone(capture.parse_performance_title("Ymir 0.4.0-dev - No disc inserted"))
        self.assertIsNone(capture.parse_performance_title("Ymir 0.4.0-dev (paused)"))

    def test_summarize_uses_one_native_counter_sample_per_title_update(self) -> None:
        samples = [
            {"elapsed_seconds": 0.0, "vdp2_fps": 0, "vdp1_fps": 0, "vdp1_draws": 0},
            {"elapsed_seconds": 1.1, "vdp2_fps": 60, "vdp1_fps": 3, "vdp1_draws": 880},
            {"elapsed_seconds": 2.2, "vdp2_fps": 60, "vdp1_fps": 4, "vdp1_draws": 905},
            {"elapsed_seconds": 3.3, "vdp2_fps": 60, "vdp1_fps": 4, "vdp1_draws": 910},
        ]

        self.assertEqual(
            capture.summarize_samples(samples),
            {
                "running_snapshots": 3,
                "vdp2_fps_median": 60,
                "vdp1_fps_median": 4,
                "vdp1_fps_min": 3,
                "vdp1_fps_max": 4,
                "vdp1_draws_median": 905,
            },
        )

    def test_summarize_preserves_fractional_even_sample_median(self) -> None:
        summary = capture.summarize_samples(
            [
                {"vdp2_fps": 60, "vdp1_fps": 3, "vdp1_draws": 800},
                {"vdp2_fps": 60, "vdp1_fps": 4, "vdp1_draws": 900},
            ]
        )
        self.assertEqual(summary["vdp1_fps_median"], 3.5)
        self.assertEqual(summary["vdp1_draws_median"], 850.0)

    def test_summarize_rejects_capture_without_running_vdp2_sample(self) -> None:
        with self.assertRaisesRegex(ValueError, "no running Ymir performance samples"):
            capture.summarize_samples(
                [{"elapsed_seconds": 0.0, "vdp2_fps": 0, "vdp1_fps": 0, "vdp1_draws": 0}]
            )

    def test_collect_samples_reads_only_the_requested_process_window(self) -> None:
        titles = iter(
            [
                "Ymir 0.4.0-dev - BIOS",
                "Ymir 0.4.0-dev - [T-99901G] SM64 | Speed: 100% / 100% | VDP2: 60 fps | VDP1: 3 fps, 880 draws | GUI: 59 fps",
                "Ymir 0.4.0-dev - [T-99901G] SM64 | Speed: 100% / 100% | VDP2: 60 fps | VDP1: 4 fps, 905 draws | GUI: 59 fps",
            ]
        )
        slept: list[float] = []
        timestamps = iter(
            [
                datetime(2026, 8, 5, 0, 0, 0, tzinfo=UTC),
                datetime(2026, 8, 5, 0, 0, 1, tzinfo=UTC),
                datetime(2026, 8, 5, 0, 0, 2, tzinfo=UTC),
            ]
        )

        samples = capture.collect_performance_samples(
            4321,
            sample_count=3,
            interval_seconds=1.1,
            title_reader=lambda pid: next(titles) if pid == 4321 else "wrong process",
            sleeper=slept.append,
            clock=lambda: next(timestamps),
        )

        self.assertEqual([sample["vdp1_fps"] for sample in samples], [3, 4])
        self.assertEqual([sample["sample_index"] for sample in samples], [1, 2])
        self.assertEqual(
            [sample["observed_utc"] for sample in samples],
            ["2026-08-05T00:00:00+00:00", "2026-08-05T00:00:01+00:00"],
        )
        self.assertEqual(slept, [1.1, 1.1, 1.1])

    def test_select_process_title_requires_one_visible_window_for_exact_pid(self) -> None:
        windows = [(111, "other"), (4321, "Ymir counters"), (999, "debugger")]
        self.assertEqual(capture.select_process_title(4321, windows), "Ymir counters")
        with self.assertRaisesRegex(RuntimeError, "no visible window"):
            capture.select_process_title(7777, windows)
        with self.assertRaisesRegex(RuntimeError, "multiple visible windows"):
            capture.select_process_title(4321, windows + [(4321, "second")])

    def test_launch_capture_records_exact_process_and_leaves_gui_running(self) -> None:
        title = (
            "Ymir 0.4.0-dev - [T-99901G] SM64 | Speed: 100% / 100% "
            "| VDP2: 60 fps | VDP1: 4 fps, 905 draws | GUI: 59 fps"
        )
        process = mock.Mock(pid=4321)
        process.poll.return_value = None
        plan = {"command": ["ymir-sdl3.exe", "--disc", "game.cue"], "working_directory": "."}

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.json"
            with mock.patch.object(capture.subprocess, "Popen", return_value=process) as popen:
                report = capture.launch_and_capture(
                    plan,
                    warmup_seconds=0,
                    sample_count=2,
                    interval_seconds=1.1,
                    report_output=output,
                    title_reader=lambda pid: title if pid == 4321 else "",
                    sleeper=lambda _seconds: None,
                )

            self.assertEqual(popen.call_args.args[0], plan["command"])
            self.assertTrue(report["alive_after_capture"])
            self.assertEqual(report["pid"], 4321)
            self.assertEqual(report["summary"]["vdp1_fps_median"], 4)
            self.assertEqual(report["summary"]["running_snapshots"], 2)
            self.assertTrue(output.with_suffix(".stdout.log").is_file())
            self.assertTrue(output.with_suffix(".stderr.log").is_file())

    def test_report_wrapper_persists_structured_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.json"
            plan = {"command": ["ymir"], "cue": {"sha256": "cue"}, "iso": {"sha256": "iso"}}

            with self.assertRaisesRegex(RuntimeError, "window missing"):
                capture.capture_to_report(
                    plan,
                    output,
                    capture_runner=mock.Mock(side_effect=RuntimeError("window missing")),
                    warmup_seconds=0,
                    sample_count=2,
                    interval_seconds=1.1,
                )

            report = capture.json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "failed")
            self.assertEqual(report["failure"]["type"], "RuntimeError")
            self.assertEqual(report["failure"]["message"], "window missing")
            self.assertEqual(report["failure"]["stage"], "capture")
            self.assertIsNone(report["failure"]["pid"])
            self.assertEqual(report["plan"], plan)

    def test_report_wrapper_preserves_launcher_failure_stage_and_pid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "capture.json"
            failure = capture.DesktopCaptureError("window missing", stage="sampling", pid=4321)
            with self.assertRaises(capture.DesktopCaptureError):
                capture.capture_to_report(
                    {"command": ["ymir"]},
                    output,
                    capture_runner=mock.Mock(side_effect=failure),
                    warmup_seconds=0,
                    sample_count=2,
                    interval_seconds=1.1,
                )
            report = capture.json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["failure"]["stage"], "sampling")
            self.assertEqual(report["failure"]["pid"], 4321)


if __name__ == "__main__":
    unittest.main()
