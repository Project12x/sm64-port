#!/usr/bin/env python3
"""Behavioral tests for the bounded, symbol-derived SCC1 target capture."""
from __future__ import annotations

import hashlib
import json
import os
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import capture_camera_idle as capture
from test_camera_idle_contract import build_scc1


ROUTE_FIELDS = (
    "magic", "version", "atan2_variant", "replay_ticks", "global_timer",
    "mario_action", "mario_pos_x_bits", "mario_pos_y_bits",
    "mario_pos_z_bits", "mario_face_angle_x", "mario_face_angle_y",
    "mario_face_angle_z", "camera_pos_x_bits", "camera_pos_y_bits",
    "camera_pos_z_bits", "camera_mode", "triangles_transformed",
    "triangles_emitted", "triangles_vdp1_emitted", "reject_near_far",
    "reject_backface", "reject_degenerate", "reject_vertex_range",
    "reject_command_capacity", "reject_vdp1_arena_capacity",
    "reject_w_nonpositive", "reject_z_near", "reject_z_far",
    "reject_offscreen", "reject_span",
    "reject_w_nonpositive_overflow_suspect", "fault_flags", "frame_serial",
    "sim_frt_ticks_accum", "render_frt_ticks_accum",
    "render_frt_ticks_last", "master_wait_ticks", "slave_busy_ticks",
    "slave_jobs_completed", "slave_timeouts", "camera_ticks_last",
    "camera_ticks_accum", "camera_invocations", "camera_ticks_max",
)


def route_bytes() -> bytes:
    values = {name: 0 for name in ROUTE_FIELDS}
    values.update(
        magic=0x53425234, version=5, atan2_variant=2, replay_ticks=2000,
        frame_serial=91, sim_frt_ticks_accum=101,
        render_frt_ticks_accum=102, render_frt_ticks_last=103,
        master_wait_ticks=104, slave_busy_ticks=105,
        slave_jobs_completed=106, slave_timeouts=107,
    )
    return b"".join(int(values[name]).to_bytes(4, "big") for name in ROUTE_FIELDS)


class FakeClient:
    def __init__(self, memory: dict[int, bytes]) -> None:
        self.memory = memory
        self.calls: list[tuple[str, dict[str, int]]] = []
        self.notifications = [{"method": "instance.ready"}]
        self.stderr = ""
        self.aborted = False
        self.closed = False

    def call(self, method: str, params: dict[str, int] | None = None) -> dict[str, object]:
        params = params or {}
        self.calls.append((method, params))
        if method == "exec.run_for" or method == "input.pulse":
            return {}
        if method == "mem.peek":
            address, count = params["address"], params["count"]
            for start, raw in self.memory.items():
                if start <= address and address + count <= start + len(raw):
                    offset = address - start
                    return {"data": list(raw[offset:offset + count])}
            return {"data": []}
        raise AssertionError(method)

    def shutdown(self) -> None:
        self.closed = True

    def abort(self) -> None:
        self.aborted = True


class CaptureCameraIdleTest(unittest.TestCase):
    def test_resolves_all_addresses_and_markers_from_the_sibling_elf(self) -> None:
        nm = """
00210000 B sourceboot_camera_idle_capture
06010200 B sourceboot_route_checkpoint
06010400 B g_sm64_saturn_source_cart_probe
00000001 A sm64_saturn_camera_variant_marker
00000001 A sm64_saturn_camera_route_marker
"""
        parent_path = os.environ.get("PATH")
        with mock.patch("capture_camera_idle.subprocess.run") as run:
            run.return_value = mock.Mock(returncode=0, stdout=nm, stderr="")
            symbols = capture.resolve_symbols(Path("role.elf"))
        self.assertEqual(
            symbols,
            {
                "sourceboot_camera_idle_capture": 0x00210000,
                "sourceboot_route_checkpoint": 0x06010200,
                "g_sm64_saturn_source_cart_probe": 0x06010400,
                "sm64_saturn_camera_variant_marker": 1,
                "sm64_saturn_camera_route_marker": 1,
            },
        )
        self.assertEqual(os.environ.get("PATH"), parent_path)
        child_path = run.call_args.kwargs["env"]["PATH"]
        self.assertTrue(child_path.startswith(r"C:\msys64\usr\bin" + os.pathsep))
        self.assertIn("role.elf", run.call_args.args[0])

    def test_exact_reader_uses_two_full_chunks_and_one_exact_tail(self) -> None:
        address = 0x00210000
        raw = b"\x11" * 65536 + b"\x22" * 65536 + b"\x33" * 63424
        client = FakeClient({address: raw})
        self.assertEqual(capture.read_exact(client, address, 194496), raw)
        peeks = [params for method, params in client.calls if method == "mem.peek"]
        self.assertEqual(
            peeks,
            [
                {"address": address, "count": 65536},
                {"address": address + 65536, "count": 65536},
                {"address": address + 131072, "count": 63424},
            ],
        )

    def test_exact_reader_rejects_short_missing_repeated_and_oversized_chunks(self) -> None:
        class BadClient:
            def __init__(self, data: object) -> None:
                self.data = data

            def call(self, method: str, params: dict[str, int]) -> dict[str, object]:
                return {"data": self.data}

        for bad in ([], [0] * 9, [0] * 11, [0] * 10 + [256], "not-bytes"):
            with self.subTest(bad=type(bad).__name__), self.assertRaises(ValueError):
                capture.read_exact(BadClient(bad), 0x200000, 10)

        repeated = FakeClient({0x200000: bytes(range(20))})
        with mock.patch.object(repeated, "call", side_effect=[
            {"data": list(range(10))}, {"data": list(range(10))}
        ]), self.assertRaises(ValueError):
            capture.read_exact(repeated, 0x200000, 20, chunk_size=10)

    def test_capture_preserves_raw_timing_and_cart_proof_from_one_pause(self) -> None:
        scc_address, route_address, cart_address = 0x210000, 0x6010200, 0x6010400
        scc = build_scc1(idle_start=0)
        route = route_bytes()
        cart = struct.pack(">7I", 0x53434152, 5, 1234, 1234, 7, 4 * 1024 * 1024, 0)
        client = FakeClient({scc_address: scc, route_address: route, cart_address: cart})
        symbols = {
            "sourceboot_camera_idle_capture": scc_address,
            "sourceboot_route_checkpoint": route_address,
            "g_sm64_saturn_source_cart_probe": cart_address,
            "sm64_saturn_camera_variant_marker": 1,
            "sm64_saturn_camera_route_marker": 1,
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            elf, cue, iso, source, manifest = (
                root / "role.elf", root / "role.cue", root / "role.iso",
                root / "SOURCE.DAT", root / "route.json",
            )
            elf.write_bytes(b"elf")
            cue.write_bytes(b"cue")
            iso.write_bytes(b"iso")
            source.write_bytes(b"x" * 1234)
            manifest.write_text(json.dumps({
                "route_version": "bob-default-camera-v1", "route_id": 2,
                "checkpoint_tick": 2000, "simulation_ticks": 2000,
                "samples": [{"ticks": 2000, "stick_x": 0, "stick_y": 0, "buttons": 0}],
            }), encoding="utf-8")
            with mock.patch("capture_camera_idle.resolve_symbols", return_value=symbols), \
                    mock.patch("capture_camera_idle.newest_sibling_elf", return_value=elf), \
                    mock.patch("capture_camera_idle.time.perf_counter", side_effect=[10.0, 20.0]):
                report, cart_report = capture.capture_target(
                    client=client, game=cue, route_manifest=manifest,
                    capture_role="camera-source-baseline", expected_idle_start_tick=0,
                    discovery=True, source_data=source, frames=600,
                )

        self.assertEqual(len(report["scc1_window"]["data"]), 194496)
        self.assertEqual(len(report["route_window"]["data"]), 176)
        self.assertEqual(report["route_window"]["decoded"]["replay_ticks"], 2000)
        self.assertEqual(report["scc1_window"]["decoded"]["header"][7:10],
                         [0x53425234, 5, 2000])
        self.assertEqual(report["absolute_markers"],
                         {"camera_variant": 1, "camera_route": 1})
        self.assertEqual(report["timing"]["frame_serial"], 91)
        self.assertEqual(report["timing"]["sim_frt_ticks_accum"], 101)
        self.assertEqual(report["emulation_timing"]["wall_clock_seconds"], 10.0)
        self.assertEqual(report["emulation_timing"]["emulated_vblank_fps"], 60.0)
        self.assertEqual(report["emulation_timing"]["emulation_speed_ratio"], 1.0)
        self.assertEqual(cart_report["raw_probe"]["data"], list(cart))
        self.assertEqual(cart_report["decoded"]["stage"], "READY")
        self.assertEqual(cart_report["decoded"]["status"], "OK")
        self.assertEqual(cart_report["source_data"]["sha256"],
                         hashlib.sha256(b"x" * 1234).hexdigest())
        self.assertTrue(client.closed)
        self.assertFalse(client.aborted)

    def test_role_mismatch_and_any_exception_abort_the_client(self) -> None:
        client = FakeClient({})
        with mock.patch("capture_camera_idle.resolve_symbols", side_effect=ValueError("bad role")):
            with self.assertRaises(ValueError):
                capture.capture_target(
                    client=client, game=Path("x.cue"), route_manifest=Path("x.json"),
                    capture_role="camera-source-baseline", expected_idle_start_tick=0,
                    discovery=True, source_data=None, frames=1,
                )
        self.assertTrue(client.aborted)


if __name__ == "__main__":
    unittest.main()
