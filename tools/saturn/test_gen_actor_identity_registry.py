import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "tools/saturn/gen_actor_identity_registry.py"

SUPPORTED_KEY = (
    '{"animation_table":[],"geo_root":"shared_geo",'
    '"geo_source":"actors/shared/geo.inc.c","model":"MODEL_SHARED",'
    '"model_variants":[{"geo_root":"shared_geo","model":"MODEL_SHARED"}]}'
)
UNSUPPORTED_KEY = (
    '{"animation_table":[],"geo_root":"unsupported_geo",'
    '"geo_source":"actors/unsupported/geo.inc.c",'
    '"model":"MODEL_UNSUPPORTED","model_variants":'
    '[{"geo_root":"unsupported_geo","model":"MODEL_UNSUPPORTED"}]}'
)
PAYLOAD = bytes(range(64))
PAYLOAD_SHA256 = hashlib.sha256(PAYLOAD).hexdigest()


def closure_record(behavior: str, model: str, geo_root: str, geo_source: str) -> dict:
    return {
        "animation_table": [],
        "behavior_root": behavior,
        "geo_root": geo_root,
        "model": model,
        "model_variants": [{"geo_root": geo_root, "model": model}],
        "root_provenance": {
            "behavior": {"source": "data/behavior_data.c", "symbol": behavior},
            "models": {
                model: {
                    "binding_source": "levels/scripts.c",
                    "geo_source": geo_source,
                    "geo_symbol": geo_root,
                    "source": "include/model_ids.h",
                }
            },
        },
        "stable_id": behavior,
    }


class GeneratorFixture:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.payload = root / "families.s64f"
        self.report = root / "actor-families.json"
        self.closure = root / "closure.json"
        self.model_ids = root / "model_ids.h"
        self.payload.write_bytes(PAYLOAD)
        self.model_ids.write_text(
            "#define MODEL_NONE 0x00\n"
            "#define MODEL_SHARED 0x21\n"
            "#define MODEL_UNSUPPORTED 0x22\n",
            encoding="utf-8",
        )
        records = [
            closure_record(
                "bhvSharedFirst", "MODEL_SHARED", "shared_geo",
                "actors/shared/geo.inc.c",
            ),
            closure_record(
                "bhvSharedSecond", "MODEL_SHARED", "shared_geo",
                "actors/shared/geo.inc.c",
            ),
            closure_record(
                "bhvUnsupported", "MODEL_UNSUPPORTED", "unsupported_geo",
                "actors/unsupported/geo.inc.c",
            ),
        ]
        self.closure.write_text(
            json.dumps({
                "schema": "sm64-saturn-scene-closure-v1",
                "level": "bob",
                "area": 1,
                "records": records,
            }),
            encoding="utf-8",
        )
        self.report.write_text(
            json.dumps({
                "schema": "sm64-saturn-actor-family-bank-v2",
                "version": 2,
                "scene": {"level": "bob", "area": 1},
                "family_count": 2,
                "closure_record_count": 3,
                "payload": self.payload.as_posix(),
                "payload_size": len(PAYLOAD),
                "payload_sha256": PAYLOAD_SHA256,
                "families": [
                    {
                        "family_id": 0x12345678,
                        "family_key": SUPPORTED_KEY,
                        "stable_id": "bhvSharedFirst",
                        "supported": True,
                    },
                    {
                        "family_id": 0x9ABCDEF0,
                        "family_key": UNSUPPORTED_KEY,
                        "stable_id": "bhvUnsupported",
                        "supported": False,
                    },
                ],
            }),
            encoding="utf-8",
        )

    def run(self, output: Path, *, scene_generation: int = 7) -> subprocess.CompletedProcess:
        return subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--family-report", str(self.report),
                "--closure", str(self.closure),
                "--model-ids", str(self.model_ids),
                "--scene-generation", str(scene_generation),
                "--output", str(output),
            ],
            capture_output=True,
            text=True,
        )


class TestActorIdentityRegistryGenerator(unittest.TestCase):
    def test_output_is_byte_stable_across_two_runs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = GeneratorFixture(Path(directory))
            first = Path(directory) / "first.h"
            second = Path(directory) / "second.h"
            one = fixture.run(first)
            two = fixture.run(second)
            self.assertEqual(one.returncode, 0, one.stderr)
            self.assertEqual(two.returncode, 0, two.stderr)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_supported_behavior_geo_pairs_resolve_and_shared_geo_stays_disambiguated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = GeneratorFixture(Path(directory))
            output = Path(directory) / "registry.h"
            result = fixture.run(output)
            self.assertEqual(result.returncode, 0, result.stderr)
            header = output.read_text(encoding="utf-8")
            self.assertIn("SATURN_ACTOR_IDENTITY_REGISTRY_COUNT 2U", header)
            self.assertIn("bhvSharedFirst", header)
            self.assertIn("bhvSharedSecond", header)
            self.assertEqual(header.count(".model_id = 0x0021U"), 2)
            self.assertIn(
                "saturn_actor_identity_registry_lookup(uint16_t model_id,",
                header,
            )
            self.assertIn("const BehaviorScript *behavior", header)
            self.assertIn(".family_id = 1U", header)
            self.assertIn(".scene_package_generation = 7U", header)

    def test_unsupported_family_is_absent_and_no_zero_identity_row_is_emitted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = GeneratorFixture(Path(directory))
            output = Path(directory) / "registry.h"
            result = fixture.run(output)
            self.assertEqual(result.returncode, 0, result.stderr)
            header = output.read_text(encoding="utf-8")
            self.assertNotIn("bhvUnsupported", header)
            self.assertNotIn("MODEL_UNSUPPORTED", header)
            self.assertNotIn(".model_id = 0x0022U", header)
            self.assertNotIn(".family_id = 0U", header)
            self.assertNotIn(".actor_bank_id = 0x00000000U", header)

    def test_bank_hash_words_are_the_real_payload_sha256_big_endian_words(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = GeneratorFixture(Path(directory))
            output = Path(directory) / "registry.h"
            result = fixture.run(output)
            self.assertEqual(result.returncode, 0, result.stderr)
            header = output.read_text(encoding="utf-8")
            expected = [
                int.from_bytes(bytes.fromhex(PAYLOAD_SHA256)[offset:offset + 4], "big")
                for offset in range(0, 32, 4)
            ]
            for word in expected:
                self.assertIn(f"0x{word:08X}U", header)
            self.assertIn(f".actor_bank_id = 0x{expected[0]:08X}U", header)

    def test_stale_report_payload_and_zero_scene_generation_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = GeneratorFixture(Path(directory))
            output = Path(directory) / "registry.h"
            fixture.payload.write_bytes(PAYLOAD + b"stale")
            stale = fixture.run(output)
            self.assertNotEqual(stale.returncode, 0)
            self.assertIn("payload", stale.stderr.lower())
            fixture.payload.write_bytes(PAYLOAD)
            zero = fixture.run(output, scene_generation=0)
            self.assertNotEqual(zero.returncode, 0)
            self.assertIn("scene generation", zero.stderr.lower())


class TestCurrentAuthoritativeInputs(unittest.TestCase):
    def test_current_bob_closure_maps_every_supported_drawable_variant(self) -> None:
        report = ROOT / "build/saturn/packages/bob/1/actors/actor-families.json"
        closure = ROOT / "build/saturn/packages/bob/1/closure.json"
        model_ids = ROOT / "include/model_ids.h"
        self.assertTrue(report.is_file(), "run compile-actor-banks first")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "registry.h"
            result = subprocess.run(
                [
                    sys.executable,
                    str(GENERATOR),
                    "--family-report", str(report),
                    "--closure", str(closure),
                    "--model-ids", str(model_ids),
                    "--scene-generation", "1",
                    "--output", str(output),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            header = output.read_text(encoding="utf-8")
            # Hand-counted from current Task 11 inputs: 72 supported records,
            # 54 supported drawable (model, behavior) variants after the
            # MODEL_NONE controller families are excluded.
            self.assertIn("SATURN_ACTOR_IDENTITY_REGISTRY_COUNT 54U", header)
            self.assertNotIn(".family_id = 0U", header)
            self.assertNotIn(".actor_bank_id = 0x00000000U", header)


if __name__ == "__main__":
    unittest.main()
