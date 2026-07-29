#!/usr/bin/env python3
"""Regression coverage for the SMC1 route-capture fixture contract."""

from __future__ import annotations

import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from compare_route_reports import PROBE_FIELDS


ROOT = Path(__file__).resolve().parent
VERIFY = ROOT / "verify_math_route_capture.py"


def encode_math(math: dict[str, object]) -> list[int]:
    corpus = math["corpus"]
    assert isinstance(corpus, list)
    words = [
        math["magic"], math["version"], math["replay_ticks"],
        math["atan2s_calls"], math["atan2_lookup_calls"],
        math["atan2s_samples"], math["atan2_lookup_samples"],
    ]
    for function in ("atan2s", "atan2_lookup"):
        samples = [sample for sample in corpus if sample["function"] == function]
        samples.extend(
            {"y_bits": 0, "x_bits": 0, "result": 0}
            for _ in range(64 - len(samples))
        )
        for sample in samples:
            words.extend((sample["y_bits"], sample["x_bits"], sample["result"]))
    return list(struct.pack(f">{len(words)}I", *words))


def capture() -> dict[str, object]:
    corpus = [
        {"function": function, "y_bits": 0x3F800000 + index, "x_bits": 0x40000000 + index,
         "result": (4096 if function == "atan2s" else 61440) + index}
        for function in ("atan2s", "atan2_lookup")
        for index in range(64)
    ]
    math = {
        "magic": 0x534D4331,
        "version": 2,
        "replay_ticks": 2000,
        "atan2s_calls": 42,
        "atan2_lookup_calls": 42,
        "atan2s_samples": 64,
        "atan2_lookup_samples": 64,
        "corpus": corpus,
    }
    route = {
        "magic": 0x53425234,
        "version": 4,
        "atan2_variant": 2,
        "replay_ticks": 2000,
        "global_timer": 2001,
        "mario_action": 0x04000440,
        "mario_pos_x_bits": 0xC2F70000,
        "mario_pos_y_bits": 0,
        "mario_pos_z_bits": 0x43E42000,
        "mario_face_angle_x": 0,
        "mario_face_angle_y": 0x8000,
        "mario_face_angle_z": 0,
        "camera_pos_x_bits": 0x41200000,
        "camera_pos_y_bits": 0x41A00000,
        "camera_pos_z_bits": 0x41F00000,
        "camera_mode": 1,
        "triangles_transformed": 2311,
        "triangles_emitted": 913,
        "triangles_vdp1_emitted": 829,
        "reject_near_far": 100,
        "reject_backface": 200,
        "reject_degenerate": 300,
        "reject_vertex_range": 400,
        "reject_command_capacity": 0,
        "reject_vdp1_arena_capacity": 0,
        "reject_w_nonpositive": 500,
        "reject_z_near": 600,
        "reject_z_far": 700,
        "reject_offscreen": 800,
        "reject_span": 900,
        "reject_w_nonpositive_overflow_suspect": 1000,
        "fault_flags": 0,
        "frame_serial": 500,
        "sim_frt_ticks_accum": 10000,
        "render_frt_ticks_accum": 20000,
        "render_frt_ticks_last": 40,
        "master_wait_ticks": 50,
        "slave_busy_ticks": 60,
        "slave_jobs_completed": 500,
        "slave_timeouts": 0,
    }
    route_bytes = list(
        b"".join(route[field].to_bytes(4, "big") for field in PROBE_FIELDS)
    )
    return {
        "evidence_kind": "ymir-sourceboot-smc1-math-route",
        "schema_version": 2,
        "artifacts": {
            "game": {"sha256": "1" * 64, "size": 80},
            "image": {"sha256": "2" * 64, "size": 1000},
            "elf": {"sha256": "3" * 64, "size": 2000},
        },
        "route_window": {"data": route_bytes, "decoded": route},
        "math_window": {"data": encode_math(math), "decoded": math},
    }


class VerifyMathRouteCaptureTests(unittest.TestCase):
    def run_verifier(self, first: dict[str, object], second: dict[str, object]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run1, run2, fixture = root / "run1.json", root / "run2.json", root / "fixture.json"
            run1.write_text(json.dumps(first), encoding="utf-8")
            run2.write_text(json.dumps(second), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(VERIFY), str(run1), str(run2), "--fixture", str(fixture)],
                text=True,
                capture_output=True,
                check=False,
            )
            if completed.returncode == 0:
                completed.fixture = json.loads(fixture.read_text(encoding="utf-8"))  # type: ignore[attr-defined]
            return completed

    def test_accepts_complete_deterministic_corpus_at_2000_ticks(self) -> None:
        completed = self.run_verifier(capture(), capture())
        self.assertEqual(completed.returncode, 0, completed.stderr)
        fixture = completed.fixture  # type: ignore[attr-defined]
        self.assertEqual(fixture["capture_contract"]["checkpoint_tick"], 2000)
        self.assertEqual(fixture["samples"], capture()["math_window"]["decoded"]["corpus"])

    def test_rejects_missing_function_from_corpus(self) -> None:
        first = capture()
        first["math_window"]["decoded"]["corpus"] = [  # type: ignore[index]
            sample for sample in first["math_window"]["decoded"]["corpus"]  # type: ignore[index]
            if sample["function"] == "atan2s"
        ]
        first["math_window"]["decoded"]["atan2_lookup_samples"] = 0  # type: ignore[index]
        first["math_window"]["data"] = encode_math(first["math_window"]["decoded"])  # type: ignore[index]
        completed = self.run_verifier(first, first)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("atan2_lookup", completed.stderr)

    def test_rejects_route_decoded_view_that_does_not_match_raw_capture(self) -> None:
        first = capture()
        first["route_window"]["decoded"]["global_timer"] += 1  # type: ignore[index]
        completed = self.run_verifier(first, first)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("decoded route probe does not match raw bytes", completed.stderr)

    def test_rejects_capture_without_raw_smc1_bytes(self) -> None:
        first = capture()
        del first["math_window"]["data"]  # type: ignore[index]
        completed = self.run_verifier(first, first)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("raw SMC1", completed.stderr)

    def test_rejects_decoded_smc1_view_that_does_not_match_raw_capture(self) -> None:
        first = capture()
        first["math_window"]["decoded"]["atan2s_calls"] += 1  # type: ignore[index]
        completed = self.run_verifier(first, first)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("decoded SMC1 does not match raw bytes", completed.stderr)


if __name__ == "__main__":
    unittest.main()
