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
# Measured maximum per-actor scratch across the admitted set. 2026-09-01: 2915
# -> 3267 with BOB's eight new objects and enemies; MODEL_GOOMBA is now the
# high-water mark. Capacity rounds this up to the 256 B granule, so the HWRAM
# cost of the whole roster expansion is 3,072 -> 3,328 B, one granule.
#
# 2026-09-03: this number was already correct on 2026-09-01 and was then
# wrongly discounted. 3,267 B against a runtime buffer of 3,072 B is exactly
# what black-screened MODEL_GOOMBA -- see the note in source_scene_bundle.h.
# The runtime constant is now 3,328 and compile_actor_family_bundle.py proves
# the relationship at bake time instead of leaving it to be rediscovered.
# King Bob-omb's measured two-lane requirement; the LWRAM phase union grows
# to the rounded capacity, retaining the existing sequential lifetime owners.
BOB_MAXIMUM_SCRATCH_BYTES = 5891
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
    # Actor VDP1 residency ceiling.  These are a *budget*, not a measurement:
    # they are checked against the bundle's actual residency, and they are
    # only affordable because they are drawn from `existing_vdp1_remaining_bytes`
    # (52,672 B of unallocated VDP1 that no current binder owns).
    #
    # 2026-08-22: raised from 18,048 / 2,976 to admit the three BOB coin
    # variants (red/blue/yellow), which cost 1,024 B of texture and 256 B of
    # CLUT each -- 3,072 B + 768 B in total.  After the raise the actor
    # residency is 24,864 B against the 52,672 B remaining, leaving 27,808 B
    # (`future_repartition_margin_bytes`) still unclaimed.
    #
    # 2026-09-01: raised from 21,120 / 3,744 to admit MODEL_STAR, which BOB
    # cannot be completed without -- until it was admitted, bhvStar and its two
    # siblings fell out of the generic actor pipeline and rendered nothing at
    # all.  Measured cost, not estimated: 384 B of texture and 96 B of CLUT,
    # 480 B in total, taken from the same unclaimed VDP1 margin.  Note the
    # previous values were exactly the then-current residency, so the star did
    # not overflow a budget with slack in it -- it overflowed one sized
    # precisely to what was already resident.  Actor residency is now 25,344 B
    # against the 52,672 B remaining, leaving 27,328 B unclaimed.
    #
    # 2026-09-01 (second raise): 21,504 / 3,840 -> 33,664 / 6,688 to admit eight
    # more BOB objects and enemies -- signpost, koopa flag, koopa shell, goomba,
    # 1up, heart, metallic ball, bob-omb buddy. Measured, not estimated. Actor
    # residency is now 40,352 B against the 52,672 B pool, leaving 12,320 B.
    #
    # MODEL_KOOPA_WITH_SHELL was left out deliberately: it alone wanted 15,296 B
    # (29% of the pool) and 7,147 B of scratch, which would have pushed
    # residency to 55,648 B -- over the pool -- and the workspace capacity from
    # 3,072 B to 7,168 B of HWRAM. See the note beside its key in
    # actor_material_v2.py.
    #
    # 2026-09-03: 22,912 / 4,192 -> 32,256 / 6,528 to admit MODEL_WOODEN_SIGNPOST,
    # MODEL_KOOPA_SHELL and MODEL_GOOMBA, the three that black-screened on
    # 2026-09-01. VDP1 was never what stopped them -- the real partition has
    # 64,960 B remaining, not the 52,672 B modelled here, so this pool has been
    # conservative throughout and the budget was never the binding constraint.
    # They were stopped by two hand-maintained RUNTIME capacities that nothing
    # compared against the bake: the 3,072 B workspace and the 2,560 B boot
    # upload stage. Both are now raised and both are bake-enforced.
    # King Bob-omb adds measured 3,840 texture + 864 CLUT bytes within the
    # same total VDP1 allowance; 9,184 bytes remain after admission.
    # Checkerboard elevator: +3,328 texture / 256 CLUT bytes, leaving
    # 5,600 bytes in the unchanged pool. No runtime arena grows.
    # 39424 -> 44032 (2026-09-10): MODEL_BOB_SEESAW_PLATFORM adds a 4,608 B
    # texture bank. As the note above says, this number is set EQUAL to current
    # residency, so it is not "full" -- every new actor overflows it by
    # construction and the raise is bookkeeping, not a hardware finding. The
    # real VDP1 arena is 64,960 B (see the note above), so 44,032 still leaves
    # ~20,900 B unclaimed and this pool remains the conservative model it has
    # always been rather than the binding constraint.
    # 44032 -> 45056 (2026-09-10): the chain chomp gate's +1,024 B.
    actor_texture_bytes: int = 45056
    # 7648 -> 8032 -> 8096 (2026-09-10): the seesaw platform's +384 B and the
    # chain chomp gate's +64 B of CLUT, both measured by the bake.
    actor_clut_bytes: int = 8096
    # 446432 -> 458720 (2026-09-10), which moves the derived actor pool below
    # from 52,672 to 64,960 B and is what finally admitted the chain chomp gate.
    #
    # This field is "VDP1 bytes after the command table AND the Gouraud tables",
    # and 446,432 double-subtracted the Gouraud allocation: the real partition
    # is 524,256 - 53,248 (cmdt) - 12,288 (gouraud) = 458,720, and 446,432 is
    # exactly 458,720 - 12,288. Two independent in-repo statements already said
    # so -- the 2026-09-03 note above ("the real partition has 64,960 B
    # remaining, not the 52,672 B modelled here") and the same arithmetic in the
    # project's VDP1 notes. Correcting it makes the derived remainder match
    # both: 458,720 - 393,760 = 64,960.
    #
    # Why this is safe to correct rather than merely conservative to leave: this
    # model is BAKE-TIME bookkeeping. The runtime does not allocate from it --
    # source_scene_bundle.c copies the REAL partitions->texture_size /
    # clut_size, and saturn_actor_material.c bounds-checks every mapping against
    # them. If this number were ever too generous the failure would be a loud
    # runtime rejection at bundle publication, not silent VRAM corruption.
    #
    # 446,432 also appears as a bare constant in bake_bob_tiles.py and
    # bake_bob_bsp_fragments.py with no derivation. Those govern TERRAIN
    # residency and are deliberately left alone here; if they share this
    # off-by-one-allocation they are separately conservative, which is the safe
    # direction, and changing them would re-bake terrain for no current need.
    shared_post_command_gouraud_bytes: int = 458720
    terrain_texture_bytes: int = 333696
    mario_texture_bytes: int = 25600
    terrain_clut_bytes: int = 34464
    # Derived: shared_post_command_gouraud_bytes - (terrain texture + mario
    # texture + terrain CLUT) = 458,720 - 393,760. The reservation equation in
    # prove_resource_inventory asserts exactly that, so these two move together
    # or the bake fails. 52,672 -> 64,960 (2026-09-10); see the note above.
    existing_vdp1_remaining_bytes: int = 64960
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
    # Name the measured value and the constant to raise. These budgets are set
    # equal to current residency, so admitting ANY actor trips them by
    # construction -- a bare "budget exceeded" makes a routine, expected step
    # look like a hardware limit and costs a round trip to measure. The upload
    # stage check in compile_actor_family_bundle.py already reports its bytes
    # this way; these two were the last that did not.
    if texture_bytes > limits.actor_texture_bytes:
        raise ValueError(
            f"texture budget exceeded: bundle needs {texture_bytes} B but "
            f"ResourceLimits.actor_texture_bytes is "
            f"{limits.actor_texture_bytes} B")
    if clut_bytes > limits.actor_clut_bytes:
        raise ValueError(
            f"CLUT budget exceeded: bundle needs {clut_bytes} B but "
            f"ResourceLimits.actor_clut_bytes is {limits.actor_clut_bytes} B")
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

