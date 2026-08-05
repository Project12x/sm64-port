#!/usr/bin/env python3
"""Source-contract tests for the compact Saturn Mario animation bank."""

from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from actor_source import (  # noqa: E402
    load_animation_inventory,
    parse_animation_file_text,
    parse_animation_id_header_text,
    parse_mario_skeleton,
    render_legacy_mario_anims,
    validate_geo_node_vocabulary,
)


ROOT = Path(__file__).resolve().parents[2]


class ActorSourceTest(unittest.TestCase):
    def test_complete_mario_inventory_has_stable_ids_and_hashes(self) -> None:
        inventory = load_animation_inventory(ROOT)
        self.assertEqual(len(inventory.animation_ids), 209)
        self.assertEqual(len(inventory.source_files), 193)
        self.assertEqual([record.animation_id for record in inventory.records], list(range(209)))
        self.assertEqual(inventory.records[0].symbol, "anim_00")
        self.assertEqual(inventory.records[-1].symbol, "anim_D0")
        self.assertTrue(all(len(source.sha256) == 64 for source in inventory.source_files))

    def test_missing_and_duplicate_animation_ids_fail_closed(self) -> None:
        valid = "enum MarioAnimID { MARIO_ANIM_ZERO, MARIO_ANIM_ONE };"
        self.assertEqual(parse_animation_id_header_text(valid), ("MARIO_ANIM_ZERO", "MARIO_ANIM_ONE"))
        with self.assertRaisesRegex(ValueError, "duplicate animation ID"):
            parse_animation_id_header_text(
                "enum MarioAnimID { MARIO_ANIM_ZERO, MARIO_ANIM_ZERO };"
            )
        with self.assertRaisesRegex(ValueError, "contiguous"):
            parse_animation_id_header_text(
                "enum MarioAnimID { MARIO_ANIM_ZERO = 0, MARIO_ANIM_TWO = 2 };"
            )

    def test_corrupt_animation_channel_span_fails_closed(self) -> None:
        source = """
static const struct Animation anim_00[] = {
    1,
    1,
    0,
    0,
    2,
    ANIMINDEX_NUMPARTS(anim_00_indices),
    anim_00_values,
    anim_00_indices,
    0,
};
static const u16 anim_00_indices[] = {
    0x0003, 0x0000, 0x0001, 0x0000, 0x0001, 0x0000,
};
static const s16 anim_00_values[] = {
    0x0000, 0x0001,
};
"""
        with self.assertRaisesRegex(ValueError, "channel span"):
            parse_animation_file_text("anim_00.inc.c", source)

    def test_combined_source_resolves_shared_stream_by_header_reference(self) -> None:
        source = (ROOT / "assets/anims/anim_01_02.inc.c").read_text(encoding="utf-8")
        records = parse_animation_file_text("assets/anims/anim_01_02.inc.c", source)
        self.assertEqual([record.animation_id for record in records], [1, 2])
        self.assertEqual(records[0].indices, records[1].indices)
        self.assertEqual(records[0].values, records[1].values)

    def test_unsupported_geo_node_fails_closed(self) -> None:
        geo = (ROOT / "actors/mario/geo.inc.c").read_text(encoding="utf-8")
        validate_geo_node_vocabulary(geo)
        with self.assertRaisesRegex(ValueError, "unsupported geo node"):
            validate_geo_node_vocabulary(geo.replace("GEO_END(),", "GEO_CAMERA(),\n    GEO_END(),", 1))

    def test_mario_skeleton_has_all_channel_owned_nodes(self) -> None:
        geo = (ROOT / "actors/mario/geo.inc.c").read_text(encoding="utf-8")
        joints = parse_mario_skeleton(geo)
        self.assertEqual(len(joints), 20)
        self.assertEqual([joint.joint_ordinal for joint in joints], list(range(20)))
        self.assertEqual(joints[0].parent_ordinal, -1)
        self.assertTrue(all(joint.parent_ordinal < joint.joint_ordinal for joint in joints))
        self.assertEqual(sum(joint.branch_ordinal != 0xFFFF for joint in joints), 2)

    def test_shared_parser_preserves_legacy_converter_bytes(self) -> None:
        output = render_legacy_mario_anims(ROOT / "assets/anims")
        self.assertEqual(len(output), 2_388_236)
        self.assertEqual(
            hashlib.sha256(output).hexdigest(),
            "08b1fc5b7cb03bceb0b64660656facdd6e353fadc6ed2758ff446d0d066c84b9",
        )
        legacy = subprocess.run(
            [sys.executable, str(ROOT / "tools/mario_anims_converter.py")],
            cwd=ROOT, capture_output=True, check=True,
        ).stdout
        self.assertEqual(output, legacy)

    def test_shared_parser_keeps_legacy_mesh_header_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            output = temporary / "saturn_mario_actor_mesh.h"
            subprocess.run([
                sys.executable, str(ROOT / "tools/saturn/extract_mario_actor.py"),
                "--model", str(ROOT / "actors/mario/model.inc.c"),
                "--geo", str(ROOT / "actors/mario/geo.inc.c"),
                "--animation", str(ROOT / "assets/anims/anim_C5.inc.c"),
                "--walking-animation", str(ROOT / "assets/anims/anim_48.inc.c"),
                "--animation-frame", "0", "--output", str(output),
                "--report", str(temporary / "report.json"),
                "--mesh-ir-output", str(temporary / "mesh.json"),
            ], cwd=ROOT, check=True)
            self.assertEqual(
                output.read_bytes(),
                (ROOT / "src/port/saturn/gfx/saturn_mario_actor_mesh.h").read_bytes(),
            )


if __name__ == "__main__":
    unittest.main()
