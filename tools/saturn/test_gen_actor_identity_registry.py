import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "tools/saturn/gen_actor_identity_registry.py"
sys.path.insert(0, str(ROOT / "tools/saturn"))
from compile_actor_bank import _pack_family_bank  # noqa: E402

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

def packed_family(supported: bool, family_id: int, family_key: str,
                  stable_id: str, geo_source: str) -> dict:
    return {
        "family_id": family_id,
        "family_key": family_key,
        "stable_id": stable_id,
        "supported": supported,
        "geo_source": geo_source,
        "capability_mask": 0x20,
        "runtime_capability_mask": 0x01,
        "maximum_live_instances": 4,
        "actor_count": 1,
        "sources": [],
        "unsupported": [] if supported else ["TEST_UNSUPPORTED"],
        "model": "MODEL_SHARED" if supported else "MODEL_UNSUPPORTED",
        "geo_root": "shared_geo" if supported else "unsupported_geo",
        "capabilities": [],
        "geo_nodes": [],
        "animation_table": [],
        "model_variants": [],
        "effects": [],
        "runtime_capabilities": [],
        "required_capabilities": [],
    }


PAYLOAD_FAMILIES = [
    packed_family(True, 0x12345678, SUPPORTED_KEY, "bhvSharedFirst",
                  "actors/shared/geo.inc.c"),
    packed_family(False, 0x9ABCDEF0, UNSUPPORTED_KEY, "bhvUnsupported",
                  "actors/unsupported/geo.inc.c"),
]
PAYLOAD = _pack_family_bank(PAYLOAD_FAMILIES)
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
                "scene_package_generation": 7,
                "family_count": 2,
                "closure_record_count": 3,
                "payload": self.payload.as_posix(),
                "payload_size": len(PAYLOAD),
                "payload_sha256": PAYLOAD_SHA256,
                "header_content_sha256": PAYLOAD[24:56].hex(),
                "families": PAYLOAD_FAMILIES,
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
    def test_s64f_identity_layout_digest_and_record_spans_fail_closed(self) -> None:
        """Break caught: arbitrary or internally malformed bytes are accepted
        merely because the outer report size/SHA were updated to match them.
        """
        with tempfile.TemporaryDirectory() as directory:
            fixture = GeneratorFixture(Path(directory))
            output = Path(directory) / "registry.h"

            for label, payload in (
                ("identity", bytes(range(64))),
                ("internal digest", PAYLOAD[:-1] + bytes([PAYLOAD[-1] ^ 1])),
            ):
                fixture.payload.write_bytes(payload)
                report = json.loads(fixture.report.read_text(encoding="utf-8"))
                report["payload_size"] = len(payload)
                report["payload_sha256"] = hashlib.sha256(payload).hexdigest()
                fixture.report.write_text(json.dumps(report), encoding="utf-8")
                result = fixture.run(output)
                self.assertNotEqual(result.returncode, 0, label)

            malformed = bytearray(PAYLOAD)
            # First record's name offset escapes the declared blob.
            struct.pack_into(">I", malformed, 56 + 5 * 4, 0xFFFFFFFF)
            malformed[24:56] = bytes(32)
            malformed[24:56] = hashlib.sha256(malformed).digest()
            fixture.payload.write_bytes(malformed)
            report = json.loads(fixture.report.read_text(encoding="utf-8"))
            report["payload_size"] = len(malformed)
            report["payload_sha256"] = hashlib.sha256(malformed).hexdigest()
            fixture.report.write_text(json.dumps(report), encoding="utf-8")
            result = fixture.run(output)
            self.assertNotEqual(result.returncode, 0, "record span")

    def test_payload_records_and_scene_generation_must_match_report(self) -> None:
        """Break caught: report metadata or caller generation drifts from the
        validated S64F/package generation but still emits an admitting row.
        """
        with tempfile.TemporaryDirectory() as directory:
            fixture = GeneratorFixture(Path(directory))
            output = Path(directory) / "registry.h"
            report = json.loads(fixture.report.read_text(encoding="utf-8"))
            report["families"][0]["capability_mask"] = 0x21
            fixture.report.write_text(json.dumps(report), encoding="utf-8")
            mismatched = fixture.run(output)
            self.assertNotEqual(mismatched.returncode, 0)
            self.assertIn("record", mismatched.stderr.lower())

            report["families"][0]["capability_mask"] = 0x20
            fixture.report.write_text(json.dumps(report), encoding="utf-8")
            stale = fixture.run(output, scene_generation=8)
            self.assertNotEqual(stale.returncode, 0)
            self.assertIn("generation", stale.stderr.lower())

    def test_generated_identity_drives_real_observer_admission_and_miss_rejection(self) -> None:
        """Break caught: generated lookup is not the identity source consumed
        by the real observer/capture admission path, or a miss gains fallback
        identity instead of remaining zero and rejected.
        """
        compiler = shutil.which("gcc") or shutil.which("cc")
        self.assertIsNotNone(compiler, "host C compiler is required")
        with tempfile.TemporaryDirectory() as directory:
            fixture = GeneratorFixture(Path(directory))
            header = Path(directory) / "actor_identity_registry.h"
            result = fixture.run(header)
            self.assertEqual(result.returncode, 0, result.stderr)
            (Path(directory) / "behavior_data.h").write_text(
                "#pragma once\n"
                "#include <stdint.h>\n"
                "typedef uintptr_t BehaviorScript;\n"
                "extern const BehaviorScript bhvSharedFirst[];\n"
                "extern const BehaviorScript bhvSharedSecond[];\n"
                "extern const BehaviorScript bhvUnsupported[];\n",
                encoding="utf-8",
            )
            source = Path(directory) / "registry_admission_test.c"
            source.write_text(
                r'''#include <assert.h>
#include <string.h>

#include "actor_identity_registry.h"
#include "saturn_actor_instance.h"

const BehaviorScript bhvSharedFirst[] = { 1U };
const BehaviorScript bhvSharedSecond[] = { 2U };
const BehaviorScript bhvUnsupported[] = { 3U };

static sm64_saturn_actor_source_observation_t base_source(uint16_t slot)
{
    sm64_saturn_actor_source_observation_t source;
    memset(&source, 0, sizeof(source));
    source.source_generation = 5U;
    source.pool_slot = slot;
    source.model_id = 0x21U;
    source.parent_index = SM64_SATURN_ACTOR_INSTANCE_NO_PARENT;
    source.parent_node_ordinal = SM64_SATURN_ACTOR_INSTANCE_NO_PARENT;
    source.scale_q16[0] = source.scale_q16[1] = source.scale_q16[2] = 65536;
    source.active = 1U;
    source.render_active = 1U;
    source.opacity = 191U;
    source.draw_distance_q16 = 300 * 65536;
    source.render_range_min_q16 = -64 * 65536;
    source.render_range_max_q16 = 512 * 65536;
    source.render_range_state = 1U;
    source.switch_count = 1U;
    source.switch_state[0] = 3U;
    return source;
}

int main(void)
{
    sm64_saturn_geo_state_observer_t observer;
    sm64_saturn_actor_source_observation_t source = base_source(7U);
    sm64_saturn_actor_instance_snapshot_t snapshot[2];
    sm64_saturn_actor_capture_telemetry_t stats;
    uint16_t count = 0U, word;

    assert(saturn_actor_identity_registry_apply(
        source.model_id, bhvSharedSecond, &source));
    assert(source.family_id != 0U && source.actor_bank_id != 0U);
    assert(source.scene_package_generation == 7U);
    for (word = 0U; word < 8U; word++)
        assert(source.actor_bank_hash_words[word] != 0U);

    sm64_saturn_geo_state_observer_init(&observer, 2U);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 5U);
    sm64_saturn_actor_instances_set_observer(&observer);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    sm64_saturn_geo_state_observer_end_frame(&observer);
    assert(sm64_saturn_actor_instances_capture(
        snapshot, 2U, 5U, &count, &stats));
    assert(count == 1U && stats.published_count == 1U);
    assert(snapshot[0].family_id == source.family_id);
    assert(snapshot[0].actor_bank_id == source.actor_bank_id);
    assert(snapshot[0].scene_package_generation == 7U);
    assert(snapshot[0].opacity == 191U);
    assert(snapshot[0].render_range_min_q16 == -64 * 65536);
    assert(snapshot[0].render_range_max_q16 == 512 * 65536);
    assert(snapshot[0].switch_count == 1U && snapshot[0].switch_state[0] == 3U);

    source = base_source(8U);
    assert(!saturn_actor_identity_registry_apply(
        source.model_id, bhvUnsupported, &source));
    assert(source.family_id == 0U && source.actor_bank_id == 0U &&
           source.scene_package_generation == 0U);
    for (word = 0U; word < 8U; word++)
        assert(source.actor_bank_hash_words[word] == 0U);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 5U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    sm64_saturn_geo_state_observer_end_frame(&observer);
    assert(sm64_saturn_actor_instances_capture(
        snapshot, 2U, 5U, &count, &stats));
    assert(count == 0U && stats.unknown_family_count == 1U);
    return 0;
}
''',
                encoding="utf-8",
            )
            executable = Path(directory) / (
                "registry-admission-test.exe" if os.name == "nt"
                else "registry-admission-test"
            )
            compiled = subprocess.run(
                [
                    str(compiler), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    f"-I{directory}",
                    f"-I{ROOT / 'src/port/saturn/gfx'}",
                    str(source),
                    str(ROOT / "src/port/saturn/gfx/saturn_actor_instance.c"),
                    str(ROOT / "src/port/saturn/gfx/saturn_geo_state_observer.c"),
                    "-o", str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            admitted = subprocess.run(
                [str(executable)], capture_output=True, text=True,
            )
            self.assertEqual(admitted.returncode, 0, admitted.stderr)

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
