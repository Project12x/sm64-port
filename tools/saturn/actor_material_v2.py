#!/usr/bin/env python3
"""Strict BOB Fast3D material capture lowering for pointer-free S64B-v2."""

from __future__ import annotations

import hashlib
import json
import os
import struct
from dataclasses import dataclass
from typing import Mapping, Sequence

from actor_bank_format import (
    ActorAlphaModeV2,
    ActorMaterialRecipeV2,
    ActorTargetLayerV2,
    ActorTileFormatV2,
)
from actor_bank_v2 import (
    ActorBankResourcesV2,
    RenderBindingV2,
    TargetMaterialV2,
    TextureTileV2,
)
from actor_family_bundle import SourceRecord
from bake_castle_uv import pack_clut16, quantize_clut16, sample_triangle


# (family_ordinal, model_id) keys admitted to the direct-textured material
# path.  family_ordinal is the 1-based position of the family in the sorted
# closure-derived family report, so this table is only valid for one closure
# shape: adding or removing a closure family renumbers it and every key here
# must be re-derived together with the capture digests below.
BOB_DIRECT_TEXTURED_KEYS = (
    (4, 0x00CD), (5, 0x00D7), (6, 0x00DB), (7, 0x00BC), (8, 0x008F),
    (9, 0x0076), (13, 0x00A3),
    (18, 0x00A4), (19, 0x00C9), (21, 0x00A8), (24, 0x007F),
    (28, 0x0084), (29, 0x0080), (32, 0x0096), (39, 0x00A5),
    (40, 0x0095), (45, 0x0074), (46, 0x00A6),
    # MODEL_STAR (star_geo). The level's goal object: until it is admitted
    # here, bhvStar / bhvSpawnedStar / bhvStarSpawnCoordinates all fall out
    # of the generic actor pipeline and render NOTHING, because
    # saturn_source_observe_object_begin() refuses models the identity
    # registry does not resolve. Its reported "unsupported rigid-group
    # source: textured" was never a capability limit -- "textured" is a
    # supported reason for keys on this list. Its real blocker was
    # G_TEXTURE_GEN, now accepted-and-ignored (see actor_variant_bank.py).
    (30, 0x007A),
    (26, 0x00D4),  # MODEL_1UP
    (33, 0x0078),  # MODEL_HEART
    (36, 0x0065),  # MODEL_METALLIC_BALL
    (14, 0x006A),  # MODEL_KOOPA_FLAG
    # Verified 2026-09-01 by building and running EACH addition, not just baking
    # it. Four actors below present frames at 6.3043 FPS, up from 6.1922.
    #
    # NOT admitted, and each for a measured reason rather than a guess:
    #   MODEL_WOODEN_SIGNPOST (3, 0x007C)  digest 7cd35e7015ce9ee3...
    #   MODEL_KOOPA_SHELL     (27, 0x00BE) digest 245e159d7e1ce8f0...
    #   MODEL_GOOMBA          (22, 0x00C0) digest a723df97dca32c81...
    # All three bake cleanly, pass every capacity check the bake has, reach the
    # identity registry -- and then the build presents ZERO frames across 3,600
    # vblanks with no fault from either CPU. Goomba was retested in isolation
    # without Bob-omb Buddy and still black-screened, so this is intrinsic, not
    # cumulative load.
    #
    # The pattern points at an unenforced RUNTIME capacity, the same class of
    # defect as the workspace constant fixed alongside this: admitted sets
    # summing to 273-282 texture commands per instance present frames, while 303
    # (signpost) and 403 (goomba) do not. The bake cannot see it, so it must be
    # found on the runtime side before these three are retried.
    #
    #   MODEL_BOBOMB_BUDDY (43, 0x00C3) digest 3dfb671731d24471... PRESENTS
    #   frames but costs 14% -- 6.3043 -> 5.4206 FPS, 9.52 -> 11.07 VB/frame.
    #   Held back as a deliberate framerate choice, not a defect. Re-add the key
    #   and its digest to accept that cost.
    # Admitted 2026-09-01 to finish BOB's object and enemy roster. Every one of
    # these reported "unsupported rigid-group source: textured", which means
    # only "not admitted" -- see the note on that above. Each was then verified
    # individually, one key per bake so the reported error was unambiguously
    # its own, and all nine reached the capture-signature stage with no
    # capability gap. MODEL_GOOMBA additionally needed gsSPNumLights, now
    # handled in actor_variant_bank.py; that macro is the one most likely to
    # gate lit character models generally, which is why it was worth fixing as
    # a capability rather than per actor -- Whomp's Fortress is full of them.
    # MODEL_KOOPA_WITH_SHELL (11, 0x0068) is deliberately NOT admitted. It
    # bakes cleanly -- its capture digest is
    # fb2efe80bff18df85ba0f089da49d83e5ccb7a56af2d65091d9a9ba2c4824abf -- but it
    # costs 15,296 B of texture+CLUT against a 52,672 B pool (29% of the whole
    # actor allowance for one model) and its 7,147 B of scratch is, by itself,
    # more than twice the workspace every other actor needs combined. Admitting
    # it alone pushed actor residency to 55,648 B, over the pool, and forced the
    # workspace capacity from 3,072 B to 7,168 B of HWRAM. The other eight fit
    # with 12,320 B of VDP1 to spare and +256 B of workspace. Revisit only with
    # a smaller texture budget for it, or if Koopa's visibility is worth that
    # much of the actor allowance.
)

BAKE_POLICY_ID = 0x53423601
POLICY_DOCUMENT: dict[str, object] = {
    "name": "bob-direct-material-v1",
    "tile_classes": [16, 32],
    "tile_selection": "16 when source-period span <= 2; 32 when <= 16",
    "primitive": "one VDP1 distorted sprite (A,B,C,C) per source triangle",
    "pairing": "forbidden",
    "texture_format": "CLUT16",
    "quantizer": "deterministic transparent-plus-15-color median cut",
    "transparency": "CLUT index zero; alpha threshold 128",
    "sampling": "BIOS-probed VDP1 repeated-C affine weights",
    "fidelity": "VDP1 textured Gouraud is additive, not N64 texture-times-shade",
    "recipe_table": 1,
}


class ActorMaterialV2Error(ValueError):
    """An exact BOB material/source state is unsupported or malformed."""


@dataclass(frozen=True)
class MaterialSignatureV2:
    texture_path: str | None
    texture_sha256: bytes | None
    combine_mode: tuple[str, ...]
    geometry_mode: tuple[str, ...]
    tile_state: tuple[int, ...]
    layer: str
    opacity: int


_CATEGORY_LABELS = {
    "texture_image": "texture image",
    "tile_size": "tile size",
    "tile_mask_shift": "tile mask/shift",
    "tile_wrap_clamp": "tile wrap/clamp",
    "combine_mode": "combine mode",
    "geometry_mode": "geometry mode",
    "layer": "material layer",
    "opacity": "material opacity",
    "display_list_call": "display-list call",
    "tail_transfer": "tail transfer",
    "uv": "texture coordinate",
    "texture_source": "texture source",
    "material_state_trace": "material state trace",
}

# Filled from the closure-attested all-34-key RED inventory.  Each digest is
# SHA-256 over canonical category JSON, so a source/state change cannot gain
# admission merely by remaining syntactically valid.
_EXPECTED_CAPTURE_HASHES: dict[tuple[int, int], str] = {
    (4, 0x00CD): "5effdad14956faaf30891aa37c1a749334b2eec6c86561a3521bc789456636de",
    # The three BOB coin variants.  Same actors/coin/geo.inc.c GEO_SWITCH_CASE
    # over four flat display lists; the per-frame state is one integer, so no
    # pose slot is involved.  Admitted 2026-08-22 with the generic Saturn
    # reduction policy (their GEO_SHADOW node is answered by the recreated
    # Saturn shadow sprite, not by a scene-graph shadow node).
    (5, 0x00D7): "698ac125a783301e67e24a4d455da613b28926fa4c527dda072b41a952ed67b8",
    (6, 0x00DB): "dec1090d31a6109f0103cc88ee950d35f6e186e726981a3a9a1829c3d414510f",
    (7, 0x00BC): "623a659fb8bf030d8fa7ceb371c333809ebfef8f0e339261b13fdba679a22413",
    (8, 0x008F): "ab424ba6e19a80f0d0fcf86efbfb063ad750674a35f71c2ed54c47006eb9232c",
    (9, 0x0076): "809f4b989a7151d34c42fadce6ab5349ba1a821692ff0d7ffd95c419a618fc9c",
    (13, 0x00A3): "d8f2d5208b0e3df4c256e74138fb6c9160b5de9bea512001990d6938d66492d2",
    (18, 0x00A4): "c3561e3b23d73e251b09d617a0c3d4b21593b2c663f0094d6937a32ee0afaa77",
    (19, 0x00C9): "f9153e587b41473b28ea6ba145926d1a0b17d8eb4bf28ee7b280aea44313b6ee",
    (21, 0x00A8): "81a7ec0e7739cf2e513f9ae969b6d8cfc967eecf116be75769dab21a572466ee",
    (24, 0x007F): "40f44e5dcf0f38019b3e36b06350886a863373cd4b33677e73a5a9778cb02e27",
    (28, 0x0084): "2912fe9e1635306effadb3b86776dda5471685d92c2d4e7415312642daf13a79",
    (29, 0x0080): "046ecadc6ba3b29eafa7b7130bd750e27f7147a4e8b037ed872288c37c33a969",
    (32, 0x0096): "4db45f30e715f7ffe8f18a8830337b8e60aa36f9709f09ed7025ca8e2afea7b4",
    (39, 0x00A5): "9aebb9ed6f67e1e315570ca109e572ed0fba24ab7de250c627ada93f57bf59c9",
    (40, 0x0095): "92357dc51fcc0b157596b9df97455eb30a949ff860111c4b569e7c8d2974628f",
    (45, 0x0074): "896d859e2628e0c93e76934a7bfb0d9eff737ef4ebb14e81ffc1e348bb40870d",
    (46, 0x00A6): "a9a763a4ca33fac8c6dea8dbe4fd6f5dda773d462762b50d9d3e86ada407a45d",
    # MODEL_STAR, admitted 2026-09-01 for end-to-end BOB. Measured, not
    # invented: the bake reports its own capture digest when a key is
    # admitted without one, and this is that value. Approving it fixes the
    # star's exact material/source state, so any later drift in the star
    # source or its Fast3D state fails the build instead of baking
    # something different under the same approval.
    (30, 0x007A): "fe50d1763a1e0151cc8b0eda3a89e42d1b65206dfbefaa9173d32c2f34c1ad62",
    (22, 0x00C0): "a723df97dca32c81bc87446e37f72c403ecdd1d7bcde756b767721450acb88ff",
    (26, 0x00D4): "3fb6b73bac8caec8e2e7ab0a19e4fa2f2ba3e159e5f680e7d85a7ce5f7188d8c",
    (33, 0x0078): "b01f764c7075a589505999d9cda674ca1234b86400865cdcd8d332d1d334afb9",
    (36, 0x0065): "d42b4f2f96f2e8505016f3ed9cf8a8e2ff9fc3ebb62491bf3f6c331335f3dc0a",
    (14, 0x006A): "dbfd13ba87785275c9c30e8a629dc542884d483156c3f78bdcbecb2821e46e50",
    (43, 0x00C3): "3dfb671731d24471bf8621ce7ce1f7da01c5ff9a9fbbd12d8cb49ce4ede3dc02",
    # The nine actors admitted 2026-09-01. Every digest below was MEASURED by
    # the bake -- it reports its own capture digest when a key is admitted
    # without one -- and each was taken from a run with only that key admitted.
    # Approving them fixes each actor's exact material/source state, so later
    # drift fails the build instead of baking something different under the
    # same approval.
    (3, 0x007C): "7cd35e7015ce9ee308db31d62b21a529bd48e517831b8df7887eb15e448a3f4f",
    (14, 0x006A): "dbfd13ba87785275c9c30e8a629dc542884d483156c3f78bdcbecb2821e46e50",
    (26, 0x00D4): "3fb6b73bac8caec8e2e7ab0a19e4fa2f2ba3e159e5f680e7d85a7ce5f7188d8c",
    (27, 0x00BE): "245e159d7e1ce8f0e8662bde54e548e28689761826c99853e76d4da6d6f78964",
    (33, 0x0078): "b01f764c7075a589505999d9cda674ca1234b86400865cdcd8d332d1d334afb9",
    (36, 0x0065): "d42b4f2f96f2e8505016f3ed9cf8a8e2ff9fc3ebb62491bf3f6c331335f3dc0a",
    (43, 0x00C3): "3dfb671731d24471bf8621ce7ce1f7da01c5ff9a9fbbd12d8cb49ce4ede3dc02",
}
_EXPECTED_CATEGORY_HASHES: dict[tuple[int, int], dict[str, str]] = {
    (7, 0x00BC): {
        "combine_mode": "91323b27a3bae7230a6ebfbad36db44319f80a9fbcf5a136e591cc2be5189e48",
        "display_list_call": "37c60ae0d2920da6e65ec266d3f9a37ed7235d6da7a060ae1678844a1008099a",
        "geometry_mode": "e5202db3112d967bfe7489f382639d582f8272b817bb2ea1625ccd5cd6bb89e5",
        "layer": "c125737f993167abb80abf21f59a0b336cf2afc9181ad2dfc1231bbb56f29ef3",
        "material_state_trace": "d79a6717a1235e34a3e1738f730ee2e17d0a64a921e9bbaa3ca0eea1a46c5a08",
        "opacity": "6a53b11fe888e6e99e84e7bdaba3db0701e029d087b06cd3320625d59b673a22",
        "tail_transfer": "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945",
        "texture_image": "974ddc1c442dceabc0df582c71f2d2516b631214420c162181cade2f089636f2",
        "texture_source": "4a51f3c48520fdc44419a73d21080b3dcafc91211fe59bca830ce0900d6c1924",
        "tile_mask_shift": "74b46919ff887ad3194621d0aa4aaa259a13f8dc0cff44c032197e63f520f7cd",
        "tile_size": "7eb30411c167d45a0b125345f3ee8be4bf5a6ac2662a27af7c0dfbaf7cd3aff5",
        "tile_wrap_clamp": "d67aecf4e19f7c4b89c556eae580a610fb116264287d684e97aff2935c6923b5",
        "uv": "b783185ee2bdc0f66500cb756d43a3e779052b88b3d8570368f8fa6fb1717df8",
    },
    (29, 0x0080): {
        "combine_mode": "27f22ea024ac7ece386ac093628f365cb62d94d5ffad611b5845ba0af1154f7a",
        "display_list_call": "30ecff95429d777b351cafd63ad01ffccacf07e9033e2a86b4181151e00a3f1d",
        "geometry_mode": "65063ce7742a1224c3fe1542519f108846792896e493e2393272480e94803be7",
        "layer": "97a1c7cd7c0df612585f193f3cc248d39f08ab18514e354ab9ebfdd1bb0a8642",
        "opacity": "0588b2094e86d8b301fe97f37ba427c4128cc71bc3f75b1f082f3616daf7fd65",
        "tail_transfer": "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945",
        "texture_image": "165a18fb4d413b6957b1dd8dc418546dc4c21cfb4c066b59717b9c9f1d2f7c67",
        "texture_source": "aa79bfce90169a270d6f0232ded0edaacec6cf3b102b7305abdfbf888ffbb0b9",
        "tile_mask_shift": "71ea61d65dcc9dd71860ee2017d0954ca669c987d1a5d37615a1d374eb9518a6",
        "tile_size": "af01b95edbc457eee3ebdb345a3f0236d510b1603fef0c6a2c818a61969123e4",
        "tile_wrap_clamp": "a21c4c7947338bbcd906975de7ad9afd43b3eae46371e014d3f8409fd8b108c6",
        "uv": "db165321cafa47ef328a051ea4ea31b95f2d2f6ded173d4dd90c51e09ec8176c",
        "material_state_trace": "14b02cbf5119caf0baec7b21985a8162c731a9eea0fb56ab227bd8bcffb915d2",
    },
}

_EXPECTED_TRANSFER_PREFIXES: dict[tuple[int, int], tuple[tuple[str, str, str, str], ...]] = {
    (29, 0x0080): (
        ("actors/cannon_base/model.inc.c", "cannon_base_seg8_dl_080057F8",
         "gsSPDisplayList", "cannon_base_seg8_dl_08005658"),
        ("actors/cannon_base/model.inc.c", "cannon_base_seg8_dl_080057F8",
         "gsSPDisplayList", "cannon_base_seg8_dl_080056D0"),
    ),
}


def partial_transfer_divergence_v2(
    family_ordinal: int,
    model_id: int,
    transfers: Sequence[tuple[str, str, str, str]],
) -> str | None:
    """Name an already-observed transfer mutation without accepting partial state."""
    expected = _EXPECTED_TRANSFER_PREFIXES.get((family_ordinal, model_id))
    if expected is None:
        return None
    actual = tuple(transfers)
    prefix = expected[:len(actual)]
    if actual == prefix:
        return None
    first = next((edge for index, edge in enumerate(actual)
                  if index >= len(expected) or edge != expected[index]), None)
    if first is not None and first[2] == "gsSPBranchList":
        return "tail transfer"
    return "display-list call"


def _canonical_hash(value: object) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=True).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _capture_category_hashes(capture: Mapping[str, object]) -> dict[str, str]:
    materials = tuple(capture["materials"])
    triangles = tuple(capture["triangles"])
    signatures = [item["signature"] for item in materials]
    if any(not isinstance(item, MaterialSignatureV2) for item in signatures):
        raise ActorMaterialV2Error("partial material signature")

    def signature_rows(field: str) -> list[object]:
        rows: list[object] = []
        for material in materials:
            signature = material["signature"]
            if signature.texture_path is None and field in (
                    "texture_image", "tile_size", "tile_mask_shift",
                    "tile_wrap_clamp", "texture_source"):
                continue
            state = tuple(signature.tile_state)
            if field == "texture_image":
                rows.append((material["material_id"], material["texture_symbol"],
                             signature.texture_path, state[0:4], state[23:]))
            elif field == "tile_size":
                rows.append((material["material_id"], state[4:10]))
            elif field == "tile_mask_shift":
                rows.append((material["material_id"], state[10:16]))
            elif field == "tile_wrap_clamp":
                rows.append((material["material_id"], state[16:20]))
            elif field == "combine_mode":
                rows.append((material["material_id"], signature.combine_mode,
                             material["env_color"], material["alpha_compare"]))
            elif field == "geometry_mode":
                rows.append((material["material_id"], signature.geometry_mode))
            elif field == "layer":
                rows.append((material["material_id"], signature.layer))
            elif field == "opacity":
                rows.append((material["material_id"], signature.opacity))
            elif field == "texture_source":
                rows.append((material["material_id"], signature.texture_path,
                             signature.texture_sha256.hex()))
        return rows

    values: dict[str, object] = {
        field: signature_rows(field)
        for field in ("texture_image", "tile_size", "tile_mask_shift",
                      "tile_wrap_clamp", "combine_mode", "geometry_mode",
                      "layer", "opacity", "texture_source")
    }
    values["display_list_call"] = [edge for edge in capture["transfers"]
                                   if edge[2] == "gsSPDisplayList"]
    values["tail_transfer"] = [edge for edge in capture["transfers"]
                               if edge[2] == "gsSPBranchList"]
    values["uv"] = [(item["display_list"], item["list_ordinal"], item["uv"])
                    for item in triangles if item["signature"].texture_path is not None]
    values["material_state_trace"] = {
        "trace": capture["material_trace"],
        "final_state": capture["final_material_state"],
    }
    return {name: _canonical_hash(value) for name, value in values.items()}


def _verify_exact_capture(family_ordinal: int, model_id: int,
                          capture: Mapping[str, object]) -> dict[str, str]:
    key = (family_ordinal, model_id)
    actual = _capture_category_hashes(capture)
    capture_hash = _canonical_hash(actual)
    expected_capture = _EXPECTED_CAPTURE_HASHES.get(key)
    if expected_capture is None:
        raise ActorMaterialV2Error(
            "unapproved BOB material signature: "
            f"family {family_ordinal} model 0x{model_id:04x} "
            f"capture={capture_hash} measured={json.dumps(actual, sort_keys=True)}")
    if capture_hash != expected_capture:
        expected = _EXPECTED_CATEGORY_HASHES.get(key, {})
        changed = [_CATEGORY_LABELS[category] for category in _CATEGORY_LABELS
                   if category in expected and actual.get(category) != expected[category]]
        if changed:
            detail = "; ".join(f"{label} state/source" for label in changed)
            raise ActorMaterialV2Error(
                f"unapproved BOB {detail}: family {family_ordinal} "
                f"model 0x{model_id:04x}")
        raise ActorMaterialV2Error(
            "unapproved BOB material signature state/source: "
            f"family {family_ordinal} model 0x{model_id:04x}")
    return actual


def _target_layer(signature: MaterialSignatureV2) -> tuple[int, int]:
    if signature.layer == "LAYER_OPAQUE" and signature.opacity == 0:
        return ActorTargetLayerV2.OPAQUE, ActorAlphaModeV2.OPAQUE
    if signature.layer == "LAYER_ALPHA" and signature.opacity == 1:
        return ActorTargetLayerV2.CUTOUT, ActorAlphaModeV2.BINARY_ZERO_TRANSPARENT
    if signature.layer == "LAYER_TRANSPARENT" and signature.opacity == 1:
        return ActorTargetLayerV2.TRANSLUCENT, ActorAlphaModeV2.HALF_TRANSPARENT
    raise ActorMaterialV2Error(
        f"unapproved material layer/opacity: {signature.layer}/{signature.opacity}")


def _target_material(signature: MaterialSignatureV2) -> TargetMaterialV2:
    if signature.texture_path is None:
        return TargetMaterialV2(
            ActorMaterialRecipeV2.FLAT_GOURAUD,
            ActorTargetLayerV2.OPAQUE,
            ActorAlphaModeV2.OPAQUE,
        )
    layer, alpha = _target_layer(signature)
    combiner = signature.combine_mode
    # Actors bake FLAT. Owner decision 2026-09-01: "character models don't have
    # to be lit -- we just do flat with no lighting."
    #
    # This is a throughput decision, not only an aesthetic one. CLUT16_GOURAUD
    # costs one gouraud table per drawn primitive per instance, and those come
    # out of a fixed per-frame share of 892. MODEL_GOOMBA lit wants **202
    # tables per instance**, which alone drops the guaranteed any-mix service
    # floor to 892/202 = 4 actors -- for every actor in the scene, not just
    # Goomba. At 15 (the pre-Goomba floor) BOB's populated scene is serviceable;
    # at 4 it is not, and the frame never completes.
    #
    # Flat costs zero tables, so the floor is governed by the draw-record and
    # texture-command shares instead. The visible cost is that lit models lose
    # their shading gradient and render at full texture brightness, which is
    # what Saturn-era ports of N64 content generally did anyway.
    #
    # Set SATURN_ACTOR_GOURAUD_LIGHTING=1 to restore the lit recipe; it is a
    # measurable per-frame budget, not a free switch.
    lit = ("G_LIGHTING" in signature.geometry_mode
           and os.environ.get("SATURN_ACTOR_GOURAUD_LIGHTING") == "1")
    if layer == ActorTargetLayerV2.TRANSLUCENT:
        recipe = ActorMaterialRecipeV2.CLUT16_HALF_TRANSPARENT
    elif combiner == ("G_CC_MODULATERGB", "G_CC_MODULATERGB") and lit:
        recipe = ActorMaterialRecipeV2.CLUT16_GOURAUD
    elif combiner == ("G_CC_MODULATERGB", "G_CC_MODULATERGB"):
        # The same combiner with lighting dropped: modulate against a flat
        # full-brightness shade, which CLUT16_REPLACE already expresses.
        recipe = ActorMaterialRecipeV2.CLUT16_REPLACE
    elif combiner in (
            ("G_CC_DECALRGBA", "G_CC_DECALRGBA"),
            ("G_CC_MODULATERGBA", "G_CC_MODULATERGBA"),
            ("G_CC_MODULATEIA", "G_CC_MODULATEIA"),
            ("G_CC_DECALFADEA", "G_CC_DECALFADEA"),
            # Same argument as G_CC_DECALFADE below: G_CC_DECALRGB is
            # G_CC_DECALRGBA without the alpha cycle, the alpha-carrying form is
            # already approved on this recipe, and CLUT16_REPLACE writes the
            # texel either way.
            ("G_CC_DECALRGB", "G_CC_DECALRGB"),
            # MODEL_STAR's combiner. G_CC_DECALFADE is G_CC_DECALFADEA without
            # the alpha cycle -- it decals the texture and fades RGB by the
            # environment alpha. The alpha-carrying variant directly above is
            # already approved onto CLUT16_REPLACE, and on VDP1 the two collapse
            # to the same thing: the recipe writes the texel, and there is no
            # per-pixel environment fade to honour either way. Mapping it
            # anywhere else would claim a distinction the target cannot make.
            ("G_CC_DECALFADE", "G_CC_DECALFADE")):
        recipe = ActorMaterialRecipeV2.CLUT16_REPLACE
    else:
        raise ActorMaterialV2Error(
            f"unapproved combine mode state: {','.join(combiner)}")
    return TargetMaterialV2(recipe, layer, alpha)


def _triangle_period_span(triangle: Mapping[str, object]) -> float:
    state = triangle["tile"]
    uv = triangle["uv"]
    span_s = max(point[0] for point in uv) - min(point[0] for point in uv)
    span_t = max(point[1] for point in uv) - min(point[1] for point in uv)
    tile_s = int(state["lrs"]) - int(state["uls"]) + 4
    tile_t = int(state["lrt"]) - int(state["ult"]) + 4
    if tile_s <= 0 or tile_t <= 0:
        raise ActorMaterialV2Error("invalid tile size state")
    return max(span_s / tile_s, span_t / tile_t) / 8.0


MODULATE_IA_COMBINER = ("G_CC_MODULATEIA", "G_CC_MODULATEIA")


def _folded_shade(triangle: Mapping[str, object]) -> tuple[int, int, int] | None:
    """The constant shade colour to fold into this tile's CLUT, or None.

    `G_CC_MODULATEIA` is texture-intensity times shade.  The source textures
    are IA16 -- greyscale plus alpha -- so the actor's only colour lives in
    the Vtx shade bytes.  Neither VDP1 recipe reproduces a multiply: REPLACE
    ignores shade outright, and textured Gouraud on VDP1 is *additive*, not
    texture-times-shade (POLICY_DOCUMENT["fidelity"]).

    Folding the shade into the CLUT at bake time gives the exact N64 product
    for free: the intensity ramp is premultiplied once, the runtime keeps its
    cheap CLUT16_REPLACE path, and no Gouraud table is spent.  This is only
    representable when the shade is constant across the source triangle, so a
    varying shade fails closed rather than silently picking one vertex.
    """
    if triangle["signature"].combine_mode != MODULATE_IA_COMBINER:
        return None
    shade = triangle.get("shade")
    if not shade:
        raise ActorMaterialV2Error(
            "G_CC_MODULATEIA source triangle carries no shade colour")
    first = tuple(int(value) for value in shade[0][0:3])
    if any(tuple(int(value) for value in row[0:3]) != first for row in shade):
        raise ActorMaterialV2Error(
            "G_CC_MODULATEIA shade varies across the source triangle; a "
            "constant CLUT cannot represent it")
    if any(not 0 <= value <= 0xFF for value in first):
        raise ActorMaterialV2Error("shade colour is outside byte range")
    # Shade ALPHA is deliberately not folded.  CLUT16 carries transparency as
    # index zero against a fixed threshold (POLICY_DOCUMENT["transparency"]),
    # so a fractional shade alpha has nowhere to live in the palette.  Leaving
    # it alone keeps alpha behaviour exactly as it was before this fold
    # existed rather than inventing a new approximation: smoke is
    # {0xff,0xff,0xff,0xb4}, whose RGB fold is the identity, so only its
    # unfoldable alpha differs and its bank stays byte-identical.
    return first


def _modulate_rgb1555(pixel: int, shade: tuple[int, int, int]) -> int:
    """Multiply one RGB1555 texel by a 0..255 shade, preserving the MSB."""
    if not pixel & 0x8000:
        return pixel
    out = pixel & 0x8000
    for index, offset in enumerate((0, 5, 10)):
        component = (pixel >> offset) & 0x1F
        scaled = (component * shade[index] + 127) // 255
        out |= (scaled if scaled <= 0x1F else 0x1F) << offset
    return out


def _bake_tile(triangle: Mapping[str, object]) -> TextureTileV2:
    span = _triangle_period_span(triangle)
    if span <= 2:
        tile_size = 16
    elif span <= 16:
        tile_size = 32
    else:
        raise ActorMaterialV2Error("unapproved texture coordinate span")
    texture = triangle["texture"]
    uv = tuple(tuple(int(value) for value in point) for point in triangle["uv"])
    tile_state = triangle["tile"]
    pixels = [sample_triangle(texture, uv, tile_state, x, y, tile_size, 1)
              for y in range(tile_size) for x in range(tile_size)]
    shade = _folded_shade(triangle)
    if shade is not None:
        pixels = [_modulate_rgb1555(pixel, shade) for pixel in pixels]
    palette, mapping = quantize_clut16(pixels)
    packed = bytes(pack_clut16([mapping[value] for value in pixels]))
    clut = struct.pack(">16H", *palette)
    return TextureTileV2(packed, tile_size, tile_size,
                         ActorTileFormatV2.CLUT16, clut)


MAX_SWITCH_ORDINAL = 0xFF
MAX_SWITCH_CASE = 0xFE


def _switch_selector(triangle: Mapping[str, object],
                     switch_case_by_part: Mapping[int, object]) -> int:
    """Pack the switch case that selects this primitive, or 0 for always.

    A GEO_SWITCH_CASE child is its own GEO_DISPLAY_LIST node and therefore its
    own part, so a primitive belongs to exactly one case. A coin declares
    GEO_SWITCH_CASE(8, geo_switch_anim_state) over four display lists listed
    twice each -- eight parts, eight cases -- which is why the part ordinal
    rather than the display-list name is the key.

    Encoding is (switch << 8) | (case + 1). The case is stored plus one so
    that a zero record means "unconditional", which keeps every bank without a
    switch byte-identical to its previous bake.
    """
    if not switch_case_by_part:
        return 0
    part = triangle.get("branch_ordinal")
    if part is None:
        return 0
    entry = switch_case_by_part.get(int(part))
    if entry is None:
        return 0
    switch, case = (int(value) for value in entry)
    if not 0 <= switch <= MAX_SWITCH_ORDINAL:
        raise ActorMaterialV2Error(
            f"switch ordinal {switch} exceeds the selector field")
    if not 0 <= case <= MAX_SWITCH_CASE:
        raise ActorMaterialV2Error(
            f"switch case {case} exceeds the selector field")
    return (switch << 8) | (case + 1)


def compile_materials_v2(
    index: object,
    display_lists: Mapping[str, object],
    primitives: Sequence[Mapping[str, object]],
    source_identity_inputs: Sequence[SourceRecord],
) -> tuple[ActorBankResourcesV2, tuple[SourceRecord, ...], dict[str, object]]:
    """Validate one exact measured BOB capture and bake target-ready resources."""
    family_ordinal = int(display_lists["family_ordinal"])
    model_id = int(display_lists["model_id"])
    if (family_ordinal, model_id) not in BOB_DIRECT_TEXTURED_KEYS:
        raise ActorMaterialV2Error(
            f"unapproved BOB material key: family {family_ordinal} model 0x{model_id:04x}")
    category_hashes = _verify_exact_capture(family_ordinal, model_id, display_lists)
    materials = tuple(display_lists["materials"])
    triangles = tuple(display_lists["triangles"])
    switch_cases = display_lists.get("switch_case_by_part") or {}
    targets = tuple(_target_material(item["signature"]) for item in materials)
    tiles: list[TextureTileV2] = []
    bindings: list[RenderBindingV2] = []
    textured_count = 0
    for primitive_index, primitive in enumerate(primitives):
        material_id = int(primitive["material"])
        if material_id < 0 or material_id >= len(materials):
            raise ActorMaterialV2Error(
                f"primitive {primitive_index} material is outside capture")
        signature = materials[material_id]["signature"]
        sources = tuple(int(item) for item in primitive["source_triangles"])
        if signature.texture_path is None:
            bindings.append(RenderBindingV2(material_id, 0xFFFF, 0))
            continue
        if len(sources) != 1 or sources[0] < 0 or sources[0] >= len(triangles):
            raise ActorMaterialV2Error(
                f"textured primitive {primitive_index} was paired or lost source ownership")
        triangle = triangles[sources[0]]
        if triangle["signature"] != signature:
            raise ActorMaterialV2Error(
                f"textured primitive {primitive_index} material signature mismatch")
        bindings.append(RenderBindingV2(
            material_id, len(tiles), _switch_selector(triangle, switch_cases)))
        tiles.append(_bake_tile(triangle))
        textured_count += 1
    identity_sources = {item.path: item.sha256 for item in source_identity_inputs}
    texture_sources = {
        item["signature"].texture_path: item["signature"].texture_sha256
        for item in materials if item["signature"].texture_path is not None
    }
    for path, digest in texture_sources.items():
        if identity_sources.get(path) != digest:
            raise ActorMaterialV2Error(
                f"texture source is not closure-attested with exact hash: {path}")
    resources = ActorBankResourcesV2(
        tuple(bindings), targets, tuple(tiles), BAKE_POLICY_ID)
    report: dict[str, object] = {
        "policy": POLICY_DOCUMENT,
        "bake_policy_id": BAKE_POLICY_ID,
        "category_sha256": category_hashes,
        "textured_triangle_count": textured_count,
        "textured_pair_count": 0,
        "source_triangle_count": len(triangles),
        "tile_input_count": len(tiles),
        "source_paths": sorted(texture_sources),
        "saturn_fidelity": POLICY_DOCUMENT["fidelity"],
    }
    return resources, (), report
