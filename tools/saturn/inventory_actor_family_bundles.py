#!/usr/bin/env python3
"""Checked Task 5 scene inventory and Saturn resource-credit proof."""

from __future__ import annotations

import argparse
import json
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence


UINT32_MAX = (1 << 32) - 1
BOB_MAXIMUM_SCRATCH_BYTES = 1091
ACTOR_WORKSPACE_GRANULE_BYTES = 256
BOB_WORKSPACE_CAPACITY_BYTES = (
    BOB_MAXIMUM_SCRATCH_BYTES + ACTOR_WORKSPACE_GRANULE_BYTES - 1
) & ~(ACTOR_WORKSPACE_GRANULE_BYTES - 1)


@dataclass(frozen=True)
class ResourceLimits:
    observer_count: int = 64
    actor_arena_bytes: int = 65536
    output_share: int = 2718
    command_share: int = 1351
    gouraud_share: int = 892
    actor_texture_bytes: int = 16640
    actor_clut_bytes: int = 2816
    shared_post_command_gouraud_bytes: int = 446432
    terrain_texture_bytes: int = 333696
    mario_texture_bytes: int = 25600
    terrain_clut_bytes: int = 34464
    existing_vdp1_remaining_bytes: int = 52672
    # Generic-bundle design: the emitted scene maximum is rounded up to 256 B.
    workspace_capacity_bytes: int = BOB_WORKSPACE_CAPACITY_BYTES
    cart_bytes: int = 4 * 1024 * 1024
    package_classes: tuple[str, ...] = (
        "route", "input", "camera", "cart", "level", "shared-data",
        "actor", "animation", "audio", "texture",
    )


AUTHORITATIVE_LIMITS = ResourceLimits()


def _u32(value: int, label: str) -> int:
    if type(value) is not int or not 0 <= value <= UINT32_MAX:
        raise ValueError(f"{label} is outside uint32")
    return value


def _add(left: int, right: int, label: str) -> int:
    left, right = _u32(left, label), _u32(right, label)
    if right > UINT32_MAX - left:
        raise ValueError(f"{label} overflow")
    return left + right


def _mul(left: int, right: int, label: str) -> int:
    left, right = _u32(left, label), _u32(right, label)
    if left and right > UINT32_MAX // left:
        raise ValueError(f"{label} overflow")
    return left * right


def _floor(share: int, cost: int, observer_count: int) -> int:
    """A zero-cost resource cannot reduce the observer-limited floor."""
    return observer_count if cost == 0 else share // cost


def prove_resource_inventory(
    costs: Sequence[tuple[int, int, int, int, int, int]], *,
    texture_bytes: int, clut_bytes: int, bundle_bytes: int,
    workspace_bytes: int, package_class_bytes: Mapping[str, int],
    scene_package_bytes: int | None = None,
    limits: ResourceLimits = AUTHORITATIVE_LIMITS,
) -> dict[str, object]:
    """Prove residency and conservative per-frame service without allocation."""
    if tuple(package_class_bytes) != limits.package_classes:
        raise ValueError("package inventory must contain the exact ten canonical classes")
    for label, value in (
        ("texture bytes", texture_bytes), ("CLUT bytes", clut_bytes),
        ("bundle bytes", bundle_bytes), ("workspace bytes", workspace_bytes),
    ):
        _u32(value, label)
    if texture_bytes > limits.actor_texture_bytes:
        raise ValueError("texture budget exceeded")
    if clut_bytes > limits.actor_clut_bytes:
        raise ValueError("CLUT budget exceeded")
    if workspace_bytes > limits.workspace_capacity_bytes:
        raise ValueError("workspace budget exceeded")

    rows = []
    source_live = source_output = source_commands = source_gouraud = 0
    maxima = {"output_records": (0, 0, 0),
              "texture_commands": (0, 0, 0),
              "gouraud_tables": (0, 0, 0)}
    for raw in costs:
        if len(raw) != 6:
            raise ValueError("bank cost row must contain six integers")
        family, model, live, draws, commands, gouraud = (
            _u32(value, "bank cost") for value in raw)
        if not family or not model or not live:
            raise ValueError("bank identity/live ceiling must be nonzero")
        if draws >= limits.output_share:
            raise ValueError("output credit exceeded")
        if commands >= limits.command_share:
            raise ValueError("command credit exceeded")
        if gouraud >= limits.gouraud_share:
            raise ValueError("Gouraud credit exceeded")
        margins = {
            "output_records": limits.output_share - draws,
            "texture_commands": limits.command_share - commands,
            "gouraud_tables": limits.gouraud_share - gouraud,
        }
        rows.append({"family_ordinal": family, "model_id": model,
                     "maximum_live_instances": live,
                     "costs": {"output_records": draws,
                               "texture_commands": commands,
                               "gouraud_tables": gouraud},
                     "margins": margins})
        source_live = _add(source_live, live, "source live envelope")
        source_output = _add(source_output, _mul(live, draws, "source output envelope"),
                             "source output envelope")
        source_commands = _add(source_commands, _mul(live, commands, "source command envelope"),
                               "source command envelope")
        source_gouraud = _add(source_gouraud, _mul(live, gouraud, "source Gouraud envelope"),
                              "source Gouraud envelope")
        for label, cost in (("output_records", draws),
                            ("texture_commands", commands),
                            ("gouraud_tables", gouraud)):
            if cost > maxima[label][0]:
                maxima[label] = (cost, family, model)
    if not rows:
        raise ValueError("supported bank inventory is empty")

    reserved_vdp1 = _add(
        _add(limits.terrain_texture_bytes, limits.mario_texture_bytes,
             "existing VDP1 reservations"),
        limits.terrain_clut_bytes, "existing VDP1 reservations")
    if reserved_vdp1 > limits.shared_post_command_gouraud_bytes or \
            limits.shared_post_command_gouraud_bytes - reserved_vdp1 != \
            limits.existing_vdp1_remaining_bytes:
        raise ValueError("VDP1 profile reservation equation mismatch")
    existing_residency = _add(texture_bytes, clut_bytes, "actor residency")
    if existing_residency > limits.existing_vdp1_remaining_bytes:
        raise ValueError("combined texture/CLUT budget exceeded")
    package_total = 0
    for kind in limits.package_classes:
        package_total = _add(package_total, _u32(package_class_bytes[kind],
                                                f"{kind} package bytes"),
                             "package byte total")
    package_image_bytes = package_total if scene_package_bytes is None else _u32(
        scene_package_bytes, "scene package bytes")
    cart_total = _add(bundle_bytes, package_image_bytes, "cart byte total")
    if cart_total > limits.cart_bytes:
        raise ValueError("cart budget exceeded")

    witness = {label: {"cost": value[0], "family_ordinal": value[1],
                       "model_id": value[2]} for label, value in maxima.items()}
    floors = {
        "live_instances": limits.observer_count,
        "output_records": _floor(limits.output_share, maxima["output_records"][0],
                                 limits.observer_count),
        "texture_commands": _floor(limits.command_share, maxima["texture_commands"][0],
                                   limits.observer_count),
        "gouraud_tables": _floor(limits.gouraud_share, maxima["gouraud_tables"][0],
                                 limits.observer_count),
    }
    guaranteed = min(floors.values())
    if guaranteed <= 0:
        raise ValueError("guaranteed service floor is nonpositive")
    return {
        "fixed_limits": {
            "observer_count": limits.observer_count,
            "actor_arena_bytes": limits.actor_arena_bytes,
            "actor_output_records": limits.output_share,
            "cart_bytes": limits.cart_bytes,
            "workspace_bytes": limits.workspace_capacity_bytes,
        },
        "actor_shares": {"output_records": limits.output_share,
                         "texture_commands": limits.command_share,
                         "gouraud_tables": limits.gouraud_share},
        "share_derivation": {
            "commands": "2048 - setup(2) - END(1) - maximum Mario(694)",
            "gouraud": "1536 - maximum Mario(644)",
            "priority": "Mario plus generic actors essential; optional terrain receives remainder",
        },
        "source_ceiling_diagnostic": {
            "acceptance": False, "name": "unconstrained_source_ceiling_envelope",
            "live_instances": source_live, "output_records": source_output,
            "texture_commands": source_commands, "gouraud_tables": source_gouraud,
        },
        "witness_maxima": witness,
        "service_floors": floors,
        "guaranteed_any_mix_count": guaranteed,
        "individual_bank_margins": rows,
        "vdp1_residency": {
            "shared_post_command_gouraud_bytes":
                limits.shared_post_command_gouraud_bytes,
            "existing_reservations": {
                "terrain_texture_bytes": limits.terrain_texture_bytes,
                "mario_texture_bytes": limits.mario_texture_bytes,
                "terrain_clut_bytes": limits.terrain_clut_bytes,
            },
            "current_binders_own_remaining": False,
            "existing_yaul_remaining_bytes": limits.existing_vdp1_remaining_bytes,
            "actor_texture_bytes": texture_bytes,
            "actor_clut_bytes": clut_bytes,
            "future_repartition_bytes": existing_residency,
            "future_repartition_margin_bytes": limits.existing_vdp1_remaining_bytes - existing_residency,
        },
        "cart": {"bundle_bytes": bundle_bytes,
                 "scene_package_bytes": package_image_bytes,
                 "total_bytes": cart_total,
                 "margin_bytes": limits.cart_bytes - cart_total},
        "package_class_source_bytes": dict(package_class_bytes),
        "workspace": {"used_bytes": workspace_bytes,
                      "capacity_bytes": limits.workspace_capacity_bytes,
                      "margin_bytes": limits.workspace_capacity_bytes - workspace_bytes},
        "package_classes": list(limits.package_classes),
    }


def inventory_bundles(root: Path, package_generation: int) -> dict[str, object]:
    """Compile the canonical real BOB scene privately and return its inventory."""
    from compile_actor_family_bundle import compile_scene_bundle
    root = Path(root).resolve()
    with tempfile.TemporaryDirectory(prefix="actor-bundle-inventory-") as temporary:
        return compile_scene_bundle(
            root, root / "build/saturn/packages/bob/1/closure.json",
            root / "build/saturn/packages/bob/1/actors/actor-families.json",
            root / "include/model_ids.h", package_generation, Path(temporary))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--package-generation", type=int, default=1)
    args = parser.parse_args()
    print(json.dumps(inventory_bundles(args.root, args.package_generation),
                     sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
