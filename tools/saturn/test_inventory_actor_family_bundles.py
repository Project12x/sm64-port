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
            SUPPORTED_COSTS, texture_bytes=16640, clut_bytes=2816,
            bundle_bytes=50000, workspace_bytes=1091,
            package_class_bytes={name: 1 for name in AUTHORITATIVE_LIMITS.package_classes},
        )
        self.assertEqual(report["source_ceiling_diagnostic"], {
            "acceptance": False,
            "name": "unconstrained_source_ceiling_envelope",
            "live_instances": 5288,
            "output_records": 85512,
            "texture_commands": 65788,
            "gouraud_tables": 29352,
        })
        shares = report["actor_shares"]
        self.assertEqual(shares["output_records"], 2718)
        self.assertEqual(shares["texture_commands"], 1351)
        self.assertEqual(shares["gouraud_tables"], 892)
        self.assertEqual(report["witness_maxima"], {
            "output_records": {"cost": 46, "family_ordinal": 24, "model_id": 0x007F},
            "texture_commands": {"cost": 24, "family_ordinal": 40, "model_id": 0x0095},
            "gouraud_tables": {"cost": 46, "family_ordinal": 24, "model_id": 0x007F},
        })
        self.assertEqual(report["service_floors"], {
            "live_instances": 64,
            "output_records": 59,
            "texture_commands": 56,
            "gouraud_tables": 19,
        })
        self.assertEqual(report["guaranteed_any_mix_count"], 19)
        self.assertTrue(all(all(value > 0 for value in row["margins"].values())
                            for row in report["individual_bank_margins"]))
        self.assertEqual(report["vdp1_residency"], {
            "shared_post_command_gouraud_bytes": 446432,
            "existing_reservations": {
                "terrain_texture_bytes": 333696,
                "mario_texture_bytes": 25600,
                "terrain_clut_bytes": 34464,
            },
            "current_binders_own_remaining": False,
            "existing_yaul_remaining_bytes": 52672,
            "actor_texture_bytes": 16640,
            "actor_clut_bytes": 2816,
            "future_repartition_bytes": 19456,
            "future_repartition_margin_bytes": 33216,
        })
        self.assertEqual(report["workspace"], {
            "used_bytes": 1091,
            "capacity_bytes": 1280,
            "margin_bytes": 189,
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
            costs=SUPPORTED_COSTS, texture_bytes=16640, clut_bytes=2816,
            bundle_bytes=50000, workspace_bytes=1091,
            package_class_bytes={name: 1 for name in AUTHORITATIVE_LIMITS.package_classes},
        )
        injections = {
            "texture budget exceeded": replace(AUTHORITATIVE_LIMITS, actor_texture_bytes=16639),
            "CLUT budget exceeded": replace(AUTHORITATIVE_LIMITS, actor_clut_bytes=2815),
            "cart budget exceeded": replace(AUTHORITATIVE_LIMITS, cart_bytes=50009),
            "workspace budget exceeded": replace(
                AUTHORITATIVE_LIMITS, workspace_capacity_bytes=1090),
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
