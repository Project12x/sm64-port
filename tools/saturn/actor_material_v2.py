#!/usr/bin/env python3
"""Strict BOB Fast3D material capture lowering for pointer-free S64B-v2."""

from __future__ import annotations

import hashlib
import json
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


BOB_DIRECT_TEXTURED_KEYS = (
    (4, 0x00CD), (6, 0x00DB), (8, 0x008F), (13, 0x00A3),
    (18, 0x00A4), (19, 0x00C9), (21, 0x00A8), (24, 0x007F),
    (28, 0x0084), (29, 0x0080), (32, 0x0096), (39, 0x00A5),
    (40, 0x0095), (46, 0x00A6),
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
    (6, 0x00DB): "dec1090d31a6109f0103cc88ee950d35f6e186e726981a3a9a1829c3d414510f",
    (8, 0x008F): "ab424ba6e19a80f0d0fcf86efbfb063ad750674a35f71c2ed54c47006eb9232c",
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
    (46, 0x00A6): "a9a763a4ca33fac8c6dea8dbe4fd6f5dda773d462762b50d9d3e86ada407a45d",
}
_EXPECTED_CATEGORY_HASHES: dict[tuple[int, int], dict[str, str]] = {
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
    lit = "G_LIGHTING" in signature.geometry_mode
    if layer == ActorTargetLayerV2.TRANSLUCENT:
        recipe = ActorMaterialRecipeV2.CLUT16_HALF_TRANSPARENT
    elif combiner == ("G_CC_MODULATERGB", "G_CC_MODULATERGB") and lit:
        recipe = ActorMaterialRecipeV2.CLUT16_GOURAUD
    elif combiner in (
            ("G_CC_DECALRGBA", "G_CC_DECALRGBA"),
            ("G_CC_MODULATERGBA", "G_CC_MODULATERGBA"),
            ("G_CC_MODULATEIA", "G_CC_MODULATEIA"),
            ("G_CC_DECALFADEA", "G_CC_DECALFADEA")):
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
    palette, mapping = quantize_clut16(pixels)
    packed = bytes(pack_clut16([mapping[value] for value in pixels]))
    clut = struct.pack(">16H", *palette)
    return TextureTileV2(packed, tile_size, tile_size,
                         ActorTileFormatV2.CLUT16, clut)


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
            bindings.append(RenderBindingV2(material_id, 0xFFFF))
            continue
        if len(sources) != 1 or sources[0] < 0 or sources[0] >= len(triangles):
            raise ActorMaterialV2Error(
                f"textured primitive {primitive_index} was paired or lost source ownership")
        triangle = triangles[sources[0]]
        if triangle["signature"] != signature:
            raise ActorMaterialV2Error(
                f"textured primitive {primitive_index} material signature mismatch")
        bindings.append(RenderBindingV2(material_id, len(tiles)))
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
