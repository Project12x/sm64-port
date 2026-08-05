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


if __name__ == "__main__":
    unittest.main()
