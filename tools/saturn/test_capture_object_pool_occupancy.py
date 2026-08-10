#!/usr/bin/env python3
"""Artifact-binding contracts for object-pool occupancy evidence."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import capture_object_pool_occupancy as capture
import gen_build_identity as identity


def _spec(root: Path, capacity: int) -> dict[str, object]:
    artifacts: dict[str, dict[str, str]] = {}
    for index, field in enumerate(identity.ARTIFACT_HASH_FIELDS):
        artifact = root / f"{field}.bin"
        artifact.write_bytes(f"artifact-{index}".encode("ascii"))
        artifacts[field] = {
            "path": str(artifact),
            "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(),
        }
    return {
        "features": {
            "complete_mario_animation": 1,
            "dynamic_actor_closure": 1,
            "semantic_audio": 0,
        },
        "renderer_pipeline": 4,
        "level_id": 9,
        "area_id": 1,
        "route_id": 0,
        "route_replay_mode": 0,
        "live_input_mode": 0,
        "camera_route": 0,
        "camera_variant": 3,
        "diagnostic_mode": 0,
        "bootstrap_ticks": 600,
        "cart_mbit": 32,
        "cart_stage_sectors": 16,
        "hot_promotion": 0,
        "near_clip": 0,
        "bsp_order": 1,
        "polygon_tier": 0,
        "fragment_mode": 0,
        "atan2_variant": 2,
        "demo_path": 0,
        "demo_view_radius": 6000,
        "slave_render": 1,
        "camera_idle_start_tick": 0,
        "camera_idle_discovery": 0,
        "camera_range_capture": 0,
        "bsp_fragment_flat": 0,
        "fast3d_q16_trace": 0,
        "experimental_skip_geo_walk": 0,
        "object_pool_capacity": capacity,
        "artifacts": artifacts,
    }


class ObjectPoolCaptureArtifactBindingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.spec = _spec(self.root, 208)
        self.spec_path = self.root / "saturn_build_identity_spec.json"
        self.spec_path.write_text(json.dumps(self.spec), encoding="utf-8")
        self.elf = self.root / "sourceboot.elf"
        self.elf.write_bytes(b"ELF-prefix" + identity.build_identity(self.spec).raw + b"ELF-suffix")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_reports_capacity_only_when_the_sealed_spec_is_embedded_in_the_elf(self) -> None:
        self.assertEqual(
            capture.pool_capacity_from_sealed_artifact(self.spec_path, self.elf),
            208,
        )

    def test_rejects_a_spec_that_does_not_match_the_capture_elf(self) -> None:
        self.elf.write_bytes(b"stale ELF without the identity tuple")
        with self.assertRaisesRegex(ValueError, "identity.*ELF"):
            capture.pool_capacity_from_sealed_artifact(self.spec_path, self.elf)


class ObjectPoolOccupancyTests(unittest.TestCase):
    def test_prove_sealed_target_identity_requires_elf_and_sealed_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            sealed = identity.build_identity(_spec(Path(directory), 208)).raw
            code_probe = {
                "address": 0x06001000,
                "size": 4,
                "expected_bytes": [1, 2, 3, 4],
                "expected_sha256": "unused-by-the-fake",
            }
            build_probe = {
                "address": 0x06002000,
                "size": len(sealed),
                "expected_bytes": list(sealed),
            }

            class Client:
                def __init__(self, code: bytes, loaded: bytes) -> None:
                    self.code = code
                    self.loaded = loaded

                def call(self, method: str, params: dict[str, int]) -> dict[str, list[int]]:
                    if method != "mem.peek":
                        raise AssertionError(method)
                    if params["address"] == 0x06001000:
                        return {"data": list(self.code)}
                    if params["address"] == 0x26002000:
                        return {"data": list(self.loaded)}
                    raise AssertionError(params)

            self.assertTrue(capture.prove_sealed_target_identity(
                Client(b"\x01\x02\x03\x04", sealed), code_probe, build_probe, sealed,
            )["match"])
            with self.assertRaisesRegex(ValueError, "running target"):
                capture.prove_sealed_target_identity(
                    Client(b"\x00\x02\x03\x04", sealed), code_probe, build_probe, sealed,
                )
            with self.assertRaisesRegex(ValueError, "sealed build identity"):
                capture.prove_sealed_target_identity(
                    Client(b"\x01\x02\x03\x04", sealed), code_probe, build_probe,
                    b"not-the-elf-identity",
                )

    def test_decode_signed_camera_yaw(self) -> None:
        self.assertEqual(capture.decode_s16_be([0x80, 0x00]), -32768)
        self.assertEqual(capture.decode_s16_be([0x7F, 0xFF]), 32767)

    def test_decode_cart_probe_requires_ready_complete_ok(self) -> None:
        words = [0x53434152, 5, 3565776, 3565776, 0x5A, 4 << 20, 0]
        raw = b"".join(x.to_bytes(4, "big") for x in words)
        self.assertTrue(capture.decode_cart_probe(raw)["ready_complete_ok"])
        words[3] -= 1
        raw = b"".join(x.to_bytes(4, "big") for x in words)
        self.assertFalse(capture.decode_cart_probe(raw)["ready_complete_ok"])

    def test_smoke_acceptance_requires_every_nonvisual_gate(self) -> None:
        samples = [
            {"label": "post-bios-9600", "magic_valid": True,
             "alloc_failures": 0, "area_yaw": 10, "exception_magic": 0,
             "cart": {"ready_complete_ok": True},
             "boot": {"vdp2_presentation_generation": 20}},
            {"label": "post-bios-9900", "magic_valid": True,
             "alloc_failures": 0, "area_yaw": 11, "exception_magic": 0,
             "cart": {"ready_complete_ok": True},
             "boot": {"vdp2_presentation_generation": 21}},
        ]
        expected = {
            "pool_alloc_failures_zero": True,
            "cart_ready_complete_ok": True,
            "exception_record_clear": True,
            "vdp_generations_climbing": True,
            "area_yaw_changes_9500_10000": True,
            "pass": True,
        }
        self.assertEqual(capture.smoke_acceptance(samples), expected)

        mutations = {
            "frozen yaw": lambda changed: changed[1].__setitem__("area_yaw", 10),
            "held VDP generation": lambda changed: changed[1]["boot"].__setitem__(
                "vdp2_presentation_generation", 20
            ),
            "exception record": lambda changed: changed[0].__setitem__(
                "exception_magic", 0x53484258
            ),
            "cart failure": lambda changed: changed[1]["cart"].__setitem__(
                "ready_complete_ok", False
            ),
            "allocation failure": lambda changed: changed[0].__setitem__(
                "alloc_failures", 1
            ),
        }
        for name, mutate in mutations.items():
            with self.subTest(name=name):
                changed = [
                    {
                        **sample,
                        "cart": dict(sample["cart"]),
                        "boot": dict(sample["boot"]),
                    }
                    for sample in samples
                ]
                mutate(changed)
                self.assertFalse(capture.smoke_acceptance(changed)["pass"])


if __name__ == "__main__":
    unittest.main()
