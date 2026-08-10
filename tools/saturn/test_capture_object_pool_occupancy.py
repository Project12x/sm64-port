#!/usr/bin/env python3
"""Artifact-binding contracts for object-pool occupancy evidence."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import capture_object_pool_occupancy as capture
import gen_build_identity as identity
import test_release_manifest as release_fixtures


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
    def test_cli_requires_release_manifest_for_new_evidence(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            capture.main([])
        self.assertEqual(caught.exception.code, 2)

    def test_cli_report_records_verified_release_manifest_sha256(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = {name: root / name for name in (
                "ymir.exe", "ipl.bin", "game.cue", "game.elf", "release.json"
            )}
            for path in paths.values():
                path.write_bytes(b"fixture")
            output = root / "report.json"
            binding = {
                "verified": SimpleNamespace(document={"identity_version": 2}),
                "probe": {"expected_bytes": [1]},
                "sealed_identity": b"\x01",
                "identity_values": {"route_replay_mode": 1, "live_input_mode": 1},
                "pool_capacity": 208,
                "release_manifest_sha256": "d" * 64,
            }
            sample = {
                "magic_valid": True, "peak_allocated": 1, "alloc_failures": 0,
                "current_allocated": 1,
            }

            class Client:
                stderr = ""
                notifications: list[dict[str, object]] = []

                def call(self, *_args: object, **_kwargs: object) -> dict[str, object]:
                    return {}

                def shutdown(self) -> None:
                    pass

                def abort(self) -> None:
                    pass

            with (
                mock.patch.object(capture, "resolve_release_binding", return_value=binding),
                mock.patch.object(capture, "bind_capture_artifacts", return_value={}),
                mock.patch.object(capture, "build_elf_identity_probe", return_value={}),
                mock.patch.object(capture, "resolve_smoke_addresses", return_value={
                    capture.PROBE_SYMBOL: 0x06001000,
                }),
                mock.patch.object(capture, "YmirClient", return_value=Client()),
                mock.patch.object(capture, "run_bios_handoff"),
                mock.patch.object(capture, "prove_sealed_target_identity", return_value={}),
                mock.patch.object(capture, "read_smoke_sample", return_value=sample),
                mock.patch.object(capture, "smoke_acceptance", return_value={"pass": True}),
                mock.patch.object(capture, "artifact_identity", return_value={}),
                mock.patch.object(capture, "protocol_and_diagnostics", return_value={}),
            ):
                result = capture.main([
                    "--ymir", str(paths["ymir.exe"]), "--ipl", str(paths["ipl.bin"]),
                    "--game", str(paths["game.cue"]), "--elf", str(paths["game.elf"]),
                    "--release-manifest", str(paths["release.json"]),
                    "--post-bios-frames", "20000", "--sample-interval", "500",
                    "--output", str(output),
                ])
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(result, 0)
        self.assertEqual(report["release_manifest_sha256"], "d" * 64)

    def test_release_binding_uses_v2_config_and_keeps_explicit_v1_spec(self) -> None:
        resolver = getattr(capture, "resolve_release_binding", None)
        self.assertTrue(callable(resolver), "occupancy capture must resolve its sealed release")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = root / "game.cue"
            elf = root / "game.elf"
            game.write_bytes(b"cue")
            elf.write_bytes(b"elf")
            for version in (1, 2):
                with self.subTest(version=version):
                    document = {
                        "identity_version": version,
                        "identity_sha256": "a" * 64,
                        "effective_config_sha256": "b" * 64,
                        "effective_config": {
                            "object_pool_capacity": 208,
                            "route_replay_mode": 1,
                            "live_input_mode": 1,
                        },
                    }
                    verified = SimpleNamespace(
                        manifest_sha256="c" * 64,
                        document=document,
                        outputs={"cue": game.resolve(), "elf": elf.resolve()},
                    )
                    probe = {
                        "sha256": "a" * 64,
                        "expected_bytes": [1, 2, 3],
                        "identity": {
                            "version": version,
                            "effective_config_hash": "b" * 64,
                        },
                    }
                    identity_spec = root / "v1.json" if version == 1 else None
                    if identity_spec is not None:
                        identity_spec.write_text(
                            '{"object_pool_capacity":208}', encoding="utf-8"
                        )
                    with (
                        mock.patch.object(capture, "verify_release_manifest", return_value=verified),
                        mock.patch.object(capture, "build_elf_build_identity_probe", return_value=probe),
                        mock.patch.object(capture, "pool_capacity_from_sealed_artifact", return_value=208),
                        mock.patch.object(
                            capture, "build_identity",
                            return_value=SimpleNamespace(
                                raw=bytes(probe["expected_bytes"]),
                                values={"object_pool_capacity": 208},
                            ),
                        ),
                    ):
                        binding = resolver(root / "release.json", game, elf, identity_spec)
                    self.assertEqual(binding["pool_capacity"], 208)
                    self.assertEqual(binding["release_manifest_sha256"], "c" * 64)
            with (
                mock.patch.object(capture, "verify_release_manifest", return_value=SimpleNamespace(
                    manifest_sha256="c" * 64,
                    document={"identity_version": 1},
                    outputs={"cue": game.resolve(), "elf": elf.resolve()},
                )),
                self.assertRaisesRegex(ValueError, "identity spec.*v1"),
            ):
                resolver(root / "release.json", game, elf, None)

    def test_v1_identity_spec_must_equal_manifest_elf_symbol(self) -> None:
        resolver = capture.resolve_release_binding
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game, elf, spec = root / "game.cue", root / "game.elf", root / "v1.json"
            game.write_bytes(b"cue")
            elf.write_bytes(b"elf")
            spec.write_text("{}", encoding="utf-8")
            verified = SimpleNamespace(
                manifest_sha256="c" * 64,
                document={"identity_version": 1},
                outputs={"cue": game.resolve(), "elf": elf.resolve()},
            )
            probe = {"expected_bytes": [1, 2, 3]}
            with (
                mock.patch.object(capture, "verify_release_manifest", return_value=verified),
                mock.patch.object(capture, "build_elf_build_identity_probe", return_value=probe),
                mock.patch.object(capture, "validate_release_identity_probe"),
                mock.patch.object(capture, "build_identity", return_value=SimpleNamespace(raw=b"different")),
                mock.patch.object(capture, "pool_capacity_from_sealed_artifact", return_value=208),
                self.assertRaisesRegex(ValueError, "identity spec.*ELF identity symbol"),
            ):
                resolver(root / "release.json", game, elf, spec)

    def test_release_binding_exposes_only_snapshot_paths_to_capture_io(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = release_fixtures.ReleaseFixture(Path(directory))
            manifest = fixture.write()
            expected_elf = fixture.outputs["elf"].read_bytes()
            binding = capture.resolve_release_binding(
                manifest, fixture.outputs["cue"], fixture.outputs["elf"], None
            )
            for name, path in fixture.outputs.items():
                path.write_bytes(f"mutated {name}".encode("ascii"))
            self.assertNotEqual(binding["elf"], fixture.outputs["elf"].resolve())
            self.assertNotEqual(binding["cue"], fixture.outputs["cue"].resolve())
            self.assertEqual(binding["elf"].read_bytes(), expected_elf)

    def test_real_v1_release_writer_and_occupancy_compatibility(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = release_fixtures.ReleaseFixture(Path(directory))
            fixture.rebuild_identity(1)
            manifest = fixture.write()
            spec = fixture.root / "identity-v1-spec.json"
            spec.write_text(
                json.dumps(fixture.identity_spec, sort_keys=True), encoding="utf-8"
            )
            binding = capture.resolve_release_binding(
                manifest, fixture.outputs["cue"], fixture.outputs["elf"], spec
            )
            self.assertEqual(binding["verified"].document["identity_version"], 1)
            self.assertEqual(binding["pool_capacity"], 208)
            self.assertEqual(
                bytes(binding["probe"]["expected_bytes"]),
                identity.build_identity(fixture.identity_spec).raw,
            )

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
