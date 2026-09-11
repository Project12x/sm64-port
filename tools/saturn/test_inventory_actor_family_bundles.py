#!/usr/bin/env python3
"""Task 5 resource arithmetic and fail-closed inventory tests."""

from __future__ import annotations

import sys
import unittest
from dataclasses import replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from inventory_actor_family_bundles import (  # noqa: E402
    AUTHORITATIVE_LIMITS,
    ResourceLimits,
    prove_resource_inventory,
)


SUPPORTED_COSTS = (
    (4, 0x00CD, 240, 18, 18, 0),
    (6, 0x00DB, 240, 20, 20, 0),
    (7, 0x00BC, 240, 68, 12, 56),
    (8, 0x008F, 240, 18, 18, 0),
    (13, 0x00A3, 240, 16, 16, 0),
    (18, 0x00A4, 720, 2, 2, 0),
    (19, 0x00C9, 6, 2, 2, 2),
    (21, 0x00A8, 240, 2, 2, 0),
    (24, 0x007F, 480, 46, 16, 46),
    (28, 0x0084, 240, 2, 2, 0),
    (29, 0x0080, 242, 30, 8, 30),
    (32, 0x0096, 960, 14, 14, 0),
    (39, 0x00A5, 720, 12, 12, 0),
    (40, 0x0095, 480, 24, 24, 0),
    (46, 0x00A6, 240, 12, 12, 0),
)


class InventoryActorFamilyBundlesTest(unittest.TestCase):
    def test_exact_profile_shares_diagnostic_and_guaranteed_floor(self) -> None:
        report = prove_resource_inventory(
            SUPPORTED_COSTS, texture_bytes=21120, clut_bytes=3744,
            bundle_bytes=50000, workspace_bytes=2915,
            package_class_bytes={name: 1 for name in AUTHORITATIVE_LIMITS.package_classes},
        )
        self.assertEqual(report["source_ceiling_diagnostic"], {
            "acceptance": False,
            "name": "unconstrained_source_ceiling_envelope",
            "live_instances": 5528,
            "output_records": 101832,
            "texture_commands": 68668,
            "gouraud_tables": 42792,
        })
        shares = report["actor_shares"]
        self.assertEqual(shares["output_records"], 2718)
        self.assertEqual(shares["texture_commands"], 1351)
        self.assertEqual(shares["gouraud_tables"], 892)
        self.assertEqual(report["witness_maxima"], {
            "output_records": {"cost": 68, "family_ordinal": 7, "model_id": 0x00BC},
            "texture_commands": {"cost": 24, "family_ordinal": 40, "model_id": 0x0095},
            "gouraud_tables": {"cost": 56, "family_ordinal": 7, "model_id": 0x00BC},
        })
        self.assertEqual(report["service_floors"], {
            "live_instances": 64,
            "output_records": 39,
            "texture_commands": 56,
            "gouraud_tables": 15,
        })
        self.assertEqual(report["guaranteed_any_mix_count"], 15)
        self.assertTrue(all(all(value > 0 for value in row["margins"].values())
                            for row in report["individual_bank_margins"]))
        # 2026-09-10: the three limits-derived figures moved when
        # shared_post_command_gouraud_bytes was corrected from 446,432 to
        # 458,720 -- it means "VDP1 bytes after the command table AND the
        # Gouraud tables" and had double-subtracted the 12,288 B Gouraud
        # allocation. The derived remainder is now 458,720 - 393,760 = 64,960,
        # which matches the real partition this model had always described in
        # prose but not in its numbers. The actor/workspace figures below are
        # this test's own synthetic inputs and are deliberately unchanged.
        self.assertEqual(report["vdp1_residency"], {
            "shared_post_command_gouraud_bytes": 458720,
            "existing_reservations": {
                "terrain_texture_bytes": 333696,
                "mario_texture_bytes": 25600,
                "terrain_clut_bytes": 34464,
            },
            "current_binders_own_remaining": False,
            "existing_yaul_remaining_bytes": 64960,
            "actor_texture_bytes": 21120,
            "actor_clut_bytes": 3744,
            "future_repartition_bytes": 24864,
            "future_repartition_margin_bytes": 40096,
        })
        # Pre-existing failure, not related to the VDP1 correction above:
        # BOB_MAXIMUM_SCRATCH_BYTES went 3,267 -> 5,891 for King Bob-omb's
        # two-lane pose in an earlier, still-uncommitted change, which rounds
        # workspace_capacity_bytes to 6,144. This expectation was left on the
        # old 3,072 and has been red in the working tree since. used_bytes is
        # this test's own synthetic input and is unchanged.
        self.assertEqual(report["workspace"], {
            "used_bytes": 2915,
            "capacity_bytes": 6144,
            "margin_bytes": 3229,
        })

    def test_zero_cost_floor_is_explicit_and_all_one_unit_overflows_are_named(self) -> None:
        zero = prove_resource_inventory(
            ((1, 1, 1, 1, 0, 0),), texture_bytes=1, clut_bytes=1,
            bundle_bytes=1, workspace_bytes=1,
            package_class_bytes={name: 1 for name in AUTHORITATIVE_LIMITS.package_classes},
        )
        self.assertEqual(zero["service_floors"]["texture_commands"], 64)
        self.assertEqual(zero["service_floors"]["gouraud_tables"], 64)

        baseline = dict(
            costs=SUPPORTED_COSTS, texture_bytes=21120, clut_bytes=3744,
            bundle_bytes=50000, workspace_bytes=2915,
            package_class_bytes={name: 1 for name in AUTHORITATIVE_LIMITS.package_classes},
        )
        injections = {
            "texture budget exceeded": replace(AUTHORITATIVE_LIMITS, actor_texture_bytes=21119),
            "CLUT budget exceeded": replace(AUTHORITATIVE_LIMITS, actor_clut_bytes=3743),
            "cart budget exceeded": replace(AUTHORITATIVE_LIMITS, cart_bytes=50009),
            "workspace budget exceeded": replace(
                AUTHORITATIVE_LIMITS, workspace_capacity_bytes=2914),
            "output credit exceeded": replace(AUTHORITATIVE_LIMITS, output_share=45),
            "command credit exceeded": replace(AUTHORITATIVE_LIMITS, command_share=23),
            "Gouraud credit exceeded": replace(AUTHORITATIVE_LIMITS, gouraud_share=45),
        }
        for reason, limits in injections.items():
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                prove_resource_inventory(limits=limits, **baseline)

    def test_profile_owns_exactly_ten_package_classes(self) -> None:
        self.assertEqual(AUTHORITATIVE_LIMITS.package_classes, (
            "route", "input", "camera", "cart", "level", "shared-data",
            "actor", "animation", "audio", "texture",
        ))
        self.assertIsInstance(AUTHORITATIVE_LIMITS, ResourceLimits)


if __name__ == "__main__":
    unittest.main()
