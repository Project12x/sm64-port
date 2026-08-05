#!/usr/bin/env python3
"""Compiler and mutation tests for the compact Saturn actor bank."""

from __future__ import annotations

import copy
import hashlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from compile_actor_bank import (  # noqa: E402
    _source_digest,
    compile_mario_actor_bank,
    decode_animation_channels,
    validate_actor_bank_document,
    verify_legacy_pose_differential,
)


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tools/saturn/manifests/actors/mario.json"


class ActorBankTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.compiled = compile_mario_actor_bank(ROOT, MANIFEST)

    def test_complete_bank_is_compact_content_addressed_and_deterministic(self) -> None:
        document, payload = self.compiled
        validate_actor_bank_document(document, payload)
        self.assertEqual(document["animation_count"], 209)
        self.assertEqual(document["source_file_count"], 193)
        self.assertEqual([item["animation_id"] for item in document["animations"]], list(range(209)))
        self.assertEqual(document["payload_sha256"], hashlib.sha256(payload).hexdigest())
        self.assertEqual(payload[:4], b"S64B")
        self.assertNotIn("frame_vertices", document)
        self.assertFalse(document["switch_variant_geometry_complete"])
        self.assertEqual(document["switch_variant_runtime_owner"], "Task 10")
        self.assertEqual(document["s64p_dependency"]["kind"], "ANIMATION_DEPENDENCIES")
        self.assertEqual(document["s64p_dependency"]["sha256"], document["payload_sha256"])
        self.assertLess(document["payload_size"], document["full_prebake_vertex_light_bytes"])
        self.assertEqual(len(document["joints"]), 20)
        second_document, second_payload = compile_mario_actor_bank(ROOT, MANIFEST)
        self.assertEqual(payload, second_payload)
        self.assertEqual(document, second_document)

    def test_compact_channels_match_all_legacy_idle_and_walking_frames(self) -> None:
        document, payload = self.compiled
        proof = verify_legacy_pose_differential(ROOT, document, payload)
        self.assertEqual(proof["idle_frames"], 30)
        self.assertEqual(proof["walking_frames"], 77)
        self.assertEqual(proof["posed_vertex_mismatches"], 0)
        self.assertEqual(proof["light_input_mismatches"], 0)
        self.assertTrue(proof["exact"])

    def test_channel_decoder_uses_source_clamping_semantics(self) -> None:
        document, payload = self.compiled
        first = decode_animation_channels(document, payload, 0, 9999)
        record = document["animations"][0]
        self.assertEqual(len(first), record["channel_count"])
        self.assertEqual(first[0], record["last_channel_samples"][0])
        self.assertEqual(
            document["animations"][1]["indices_offset"],
            document["animations"][2]["indices_offset"],
        )

    def test_length_prefixed_stream_rejects_cross_stream_bleed(self) -> None:
        document, payload = self.compiled
        corrupt = bytearray(payload)
        offset = document["animations"][0]["indices_offset"]
        corrupt[offset - 4:offset] = b"\x00\x00\x00\x00"
        mutated = copy.deepcopy(document)
        mutated["payload_sha256"] = hashlib.sha256(corrupt).hexdigest()
        with self.assertRaisesRegex(ValueError, "index stream word count"):
            validate_actor_bank_document(mutated, bytes(corrupt))

    def test_missing_hash_duplicate_id_and_bad_joint_ordinal_fail_closed(self) -> None:
        document, payload = self.compiled
        missing_hash = copy.deepcopy(document)
        del missing_hash["sources"][0]["sha256"]
        with self.assertRaisesRegex(ValueError, "missing source hash"):
            validate_actor_bank_document(missing_hash, payload)

        duplicate = copy.deepcopy(document)
        duplicate["animations"][1]["animation_id"] = 0
        with self.assertRaisesRegex(ValueError, "duplicate animation ID"):
            validate_actor_bank_document(duplicate, payload)

        bad_joint = copy.deepcopy(document)
        bad_joint["vertices"][0]["joint_ordinal"] = bad_joint["joint_count"]
        with self.assertRaisesRegex(ValueError, "joint ordinal"):
            validate_actor_bank_document(bad_joint, payload)

    def test_missing_id_corrupt_span_and_nondeterministic_order_fail_closed(self) -> None:
        document, payload = self.compiled
        missing = copy.deepcopy(document)
        del missing["animations"][17]
        with self.assertRaisesRegex(ValueError, "complete animation ID set"):
            validate_actor_bank_document(missing, payload)

        corrupt = copy.deepcopy(document)
        corrupt["animations"][0]["values_offset"] = len(payload) - 1
        with self.assertRaisesRegex(ValueError, "animation stream span"):
            validate_actor_bank_document(corrupt, payload)

        reordered = copy.deepcopy(document)
        reordered["sources"] = list(reversed(reordered["sources"]))
        with self.assertRaisesRegex(ValueError, "canonical source order"):
            validate_actor_bank_document(reordered, payload)

    def test_provenance_binds_exact_sources_membership_and_header_digest(self) -> None:
        document, payload = self.compiled

        duplicate_path = copy.deepcopy(document)
        duplicate_path["sources"][1]["path"] = duplicate_path["sources"][0]["path"]
        duplicate_path["sources"].sort(key=lambda item: item["path"])
        with self.assertRaisesRegex(ValueError, "unique source path"):
            validate_actor_bank_document(duplicate_path, payload)

        bad_hash = copy.deepcopy(document)
        bad_hash["sources"][0]["sha256"] = "z" * 64
        with self.assertRaisesRegex(ValueError, "hex source hash"):
            validate_actor_bank_document(bad_hash, payload)

        bad_membership = copy.deepcopy(document)
        bad_membership["animations"][0]["source_path"] = "assets/anims/not-present.inc.c"
        with self.assertRaisesRegex(ValueError, "animation source membership"):
            validate_actor_bank_document(bad_membership, payload)

        bad_count = copy.deepcopy(document)
        bad_count["source_file_count"] = 192
        with self.assertRaisesRegex(ValueError, "193 animation source files"):
            validate_actor_bank_document(bad_count, payload)

        bad_inventory_pin = copy.deepcopy(document)
        bad_inventory_pin["animation_source_inventory"]["paths_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "repository-pinned animation source metadata"):
            validate_actor_bank_document(bad_inventory_pin, payload)

        bad_document_digest = copy.deepcopy(document)
        bad_document_digest["source_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "source-set digest"):
            validate_actor_bank_document(bad_document_digest, payload)

        bad_header = bytearray(payload)
        bad_header[26] ^= 1
        bad_header_document = copy.deepcopy(document)
        bad_header_document["payload_sha256"] = hashlib.sha256(bad_header).hexdigest()
        with self.assertRaisesRegex(ValueError, "header source digest"):
            validate_actor_bank_document(bad_header_document, bytes(bad_header))

        renamed = copy.deepcopy(document)
        old_path = renamed["animations"][0]["source_path"]
        new_path = "assets/anims/anim_FF.inc.c"
        renamed["animations"][0]["source_path"] = new_path
        next(source for source in renamed["sources"] if source["path"] == old_path)["path"] = new_path
        renamed["sources"].sort(key=lambda item: item["path"])
        renamed_digest = _source_digest(renamed["sources"])
        renamed["source_sha256"] = renamed_digest.hex()
        renamed_payload = bytearray(payload)
        renamed_payload[26:58] = renamed_digest
        renamed["payload_sha256"] = hashlib.sha256(renamed_payload).hexdigest()
        with self.assertRaisesRegex(ValueError, "animation source filename"):
            validate_actor_bank_document(renamed, bytes(renamed_payload))

    def test_resealed_valid_repartition_of_animation_filenames_is_rejected(self) -> None:
        document, payload = self.compiled
        repartitioned = copy.deepcopy(document)
        old_pair = "assets/anims/anim_01_02.inc.c"
        old_single = "assets/anims/anim_03.inc.c"
        new_single = "assets/anims/anim_01.inc.c"
        new_pair = "assets/anims/anim_02_03.inc.c"
        source_by_path = {item["path"]: item for item in repartitioned["sources"]}
        pair_hash = source_by_path[old_pair]["sha256"]
        single_hash = source_by_path[old_single]["sha256"]
        source_by_path[old_pair]["path"] = new_single
        source_by_path[old_single]["path"] = new_pair
        for animation in repartitioned["animations"]:
            if animation["animation_id"] == 1:
                animation["source_path"] = new_single
                animation["source_sha256"] = pair_hash
            elif animation["animation_id"] in (2, 3):
                animation["source_path"] = new_pair
                animation["source_sha256"] = single_hash
        repartitioned["sources"].sort(key=lambda item: item["path"])
        source_digest = _source_digest(repartitioned["sources"])
        repartitioned["source_sha256"] = source_digest.hex()
        repartitioned_payload = bytearray(payload)
        repartitioned_payload[26:58] = source_digest
        repartitioned["payload_sha256"] = hashlib.sha256(repartitioned_payload).hexdigest()

        with self.assertRaisesRegex(ValueError, "repository-pinned animation source set"):
            validate_actor_bank_document(repartitioned, bytes(repartitioned_payload))


if __name__ == "__main__":
    unittest.main()
