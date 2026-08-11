#!/usr/bin/env python3
"""Strict, deterministic readers for inherited SM64 actor animation sources.

The source files remain the authority.  These helpers deliberately preserve
the historical ``mario_anims_converter.py`` ordering and spelling while also
providing a validated representation for Saturn package compilers.
"""

from __future__ import annotations

import hashlib
import os
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


SUPPORTED_GEO_NODES = frozenset({
    "GEO_ANIMATED_PART", "GEO_ASM", "GEO_BRANCH", "GEO_CLOSE_NODE",
    "GEO_BILLBOARD",
    "GEO_DISPLAY_LIST", "GEO_END", "GEO_HELD_OBJECT", "GEO_NODE_START",
    "GEO_OPEN_NODE", "GEO_RENDER_RANGE", "GEO_RETURN", "GEO_ROTATION_NODE",
    "GEO_SCALE", "GEO_SHADOW", "GEO_SWITCH_CASE", "GEO_TRANSLATE_ROTATE",
})

# Generic actor-family capability bits.  These are deliberately renderer
# neutral: they describe facts observed in source geo/closure data, not a
# family-specific implementation choice.  Keep the bit positions stable once
# a family bank has been emitted because they are part of the S64F ABI.
ACTOR_CAP_ANIMATED = 1 << 0
ACTOR_CAP_SWITCH = 1 << 1
ACTOR_CAP_BILLBOARD = 1 << 2
ACTOR_CAP_ALPHA = 1 << 3
ACTOR_CAP_TRANSLUCENT = 1 << 4
ACTOR_CAP_SHADOW = 1 << 5
ACTOR_CAP_PARENTED = 1 << 6
ACTOR_CAP_HELD = 1 << 7
ACTOR_CAP_MODEL_MUTATION = 1 << 8
ACTOR_CAP_SURFACE = 1 << 9
ACTOR_CAP_LOD = 1 << 10
ACTOR_CAP_PARTICLE = 1 << 11
ACTOR_CAP_EFFECT = 1 << 12
ACTOR_CAP_RIGID = 1 << 13
ACTOR_CAP_OPAQUE = 1 << 14
ACTOR_CAP_STATIC_TRANSFORM = 1 << 15
ACTOR_CAP_PLATFORM = 1 << 16
ACTOR_CAP_COLLECTIBLE = 1 << 17

ACTOR_CAPABILITY_NAMES = (
    "ANIMATED", "SWITCH", "BILLBOARD", "ALPHA", "TRANSLUCENT", "SHADOW",
    "PARENTED", "HELD", "MODEL_MUTATION", "SURFACE", "LOD", "PARTICLE",
    "EFFECT", "RIGID", "OPAQUE", "STATIC_TRANSFORM", "PLATFORM",
    "COLLECTIBLE",
)
ACTOR_CAPABILITY_BITS = {
    name: 1 << index for index, name in enumerate(ACTOR_CAPABILITY_NAMES)
}

ACTOR_RUNTIME_CAP_TRANSFORM = 1 << 0
ACTOR_RUNTIME_CAP_SCALE = 1 << 1
ACTOR_RUNTIME_CAP_MATERIAL = 1 << 2
ACTOR_RUNTIME_CAP_SURFACE = 1 << 3
ACTOR_RUNTIME_CAP_LIFECYCLE = 1 << 4
ACTOR_RUNTIME_CAPABILITY_NAMES = (
    "TRANSFORM", "SCALE", "MATERIAL", "SURFACE", "LIFECYCLE",
)
ACTOR_RUNTIME_CAPABILITY_BITS = {
    name: 1 << index for index, name in enumerate(ACTOR_RUNTIME_CAPABILITY_NAMES)
}


@dataclass(frozen=True)
class ActorCapabilityReport:
    """Source-owned capability facts for one closure actor record."""

    mask: int
    names: tuple[str, ...]
    unsupported: tuple[str, ...]
    geo_nodes: tuple[str, ...]
    runtime_mask: int
    runtime_names: tuple[str, ...]


def analyze_actor_capabilities(
    geo_source: str | None,
    *,
    material_feature_bits: tuple[str, ...] = (),
    animation_table: tuple[str, ...] = (),
    model_variants: tuple[dict[str, object], ...] = (),
    object_roots: tuple[str, ...] = (),
    effects: tuple[str, ...] = (),
    capability_hints: tuple[str, ...] = (),
) -> ActorCapabilityReport:
    """Derive generic capabilities without matching a family name.

    The closure collector owns the source/model bindings.  This helper merely
    classifies the bound geo vocabulary and closure facts, and records every
    unsupported node instead of silently flattening it.  A model-less actor is
    represented by an empty node set and therefore remains inspectable with a
    zero geometry capability mask.
    """
    text = geo_source or ""
    nodes = tuple(sorted(set(re.findall(r"\b(GEO_[A-Z0-9_]+)\s*\(", text))))
    unsupported = sorted(set(nodes) - SUPPORTED_GEO_NODES)
    mask = 0
    if animation_table or "GEO_ANIMATED_PART" in nodes:
        mask |= ACTOR_CAP_ANIMATED
    if "GEO_SWITCH_CASE" in nodes:
        mask |= ACTOR_CAP_SWITCH
    if "GEO_BILLBOARD" in nodes:
        mask |= ACTOR_CAP_BILLBOARD
    features = {item.lower() for item in material_feature_bits}
    hints = {item.upper() for item in capability_hints}
    if "alpha" in features:
        mask |= ACTOR_CAP_ALPHA
    if "transparent" in features or "translucent" in features:
        mask |= ACTOR_CAP_TRANSLUCENT
    if "GEO_SHADOW" in nodes or "shadow" in features:
        mask |= ACTOR_CAP_SHADOW
    if any(root.startswith("spawn:") for root in object_roots):
        mask |= ACTOR_CAP_PARENTED
    # GEO_HELD_OBJECT is the authoritative source indication.  Do not infer
    # held semantics from a behavior name or model name.
    if "GEO_HELD_OBJECT" in nodes:
        mask |= ACTOR_CAP_HELD
    if len(model_variants) > 1 or "GEO_SWITCH_CASE" in nodes:
        mask |= ACTOR_CAP_MODEL_MUTATION
    if "surface" in features or "GEO_SURFACE" in nodes:
        mask |= ACTOR_CAP_SURFACE
    if "GEO_RENDER_RANGE" in nodes:
        mask |= ACTOR_CAP_LOD
    if "particle" in features:
        mask |= ACTOR_CAP_PARTICLE
    if effects:
        mask |= ACTOR_CAP_EFFECT
    rigid = (bool(text) and not animation_table and
             "GEO_ANIMATED_PART" not in nodes and len(model_variants) <= 1)
    if rigid:
        mask |= ACTOR_CAP_RIGID | ACTOR_CAP_STATIC_TRANSFORM
    if text and not ({"alpha", "transparent", "translucent"} & features):
        mask |= ACTOR_CAP_OPAQUE
    if "PLATFORM" in hints:
        mask |= ACTOR_CAP_PLATFORM
    if "COLLECTIBLE" in hints:
        mask |= ACTOR_CAP_COLLECTIBLE
    runtime_mask = 0
    if text:
        runtime_mask |= (ACTOR_RUNTIME_CAP_TRANSFORM |
                         ACTOR_RUNTIME_CAP_SCALE |
                         ACTOR_RUNTIME_CAP_MATERIAL)
    # Runtime surface ownership is deliberately unavailable until the scene
    # closure carries a schema-validated typed evidence field.  The legacy
    # actor capability bit above remains source-vocabulary-compatible for
    # existing host tests; it is not sufficient to admit a Saturn family.
    if object_roots:
        runtime_mask |= ACTOR_RUNTIME_CAP_LIFECYCLE
    names = tuple(name for name in ACTOR_CAPABILITY_NAMES if mask & ACTOR_CAPABILITY_BITS[name])
    runtime_names = tuple(name for name in ACTOR_RUNTIME_CAPABILITY_NAMES
                          if runtime_mask & ACTOR_RUNTIME_CAPABILITY_BITS[name])
    return ActorCapabilityReport(mask, names, tuple(unsupported), nodes,
                                 runtime_mask, runtime_names)


@dataclass(frozen=True)
class AnimationRecord:
    animation_id: int
    enum_name: str
    symbol: str
    source_path: str
    source_sha256: str
    flags: int
    y_translation_divisor: int
    start_frame: int
    loop_start: int
    frame_count: int
    joint_count: int
    indices: tuple[int, ...]
    values: tuple[int, ...]


@dataclass(frozen=True)
class SourceFile:
    path: str
    sha256: str


@dataclass(frozen=True)
class AnimationInventory:
    animation_ids: tuple[str, ...]
    source_files: tuple[SourceFile, ...]
    records: tuple[AnimationRecord, ...]


@dataclass(frozen=True)
class SkeletonJoint:
    joint_ordinal: int
    parent_ordinal: int
    translation: tuple[int, int, int]
    node_ordinal: int
    branch_ordinal: int = 0xFFFF


def _numbers(body: str, *, signed: bool) -> tuple[int, ...]:
    values = []
    for token in re.findall(r"(?<![A-Za-z0-9_])(?:0[xX][0-9A-Fa-f]+|-?\d+)", body):
        value = int(token, 0)
        if signed and value >= 0x8000:
            value -= 0x10000
        values.append(value)
    return tuple(values)


def parse_animation_id_header_text(source: str) -> tuple[str, ...]:
    match = re.search(r"enum\s+MarioAnimID\s*\{(.*?)\}", source, re.DOTALL)
    if match is None:
        raise ValueError("missing MarioAnimID enum")
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    body = re.sub(r"//.*", "", body)
    names: list[str] = []
    values: list[int] = []
    next_value = 0
    for raw in body.split(","):
        entry = raw.strip()
        if not entry:
            continue
        item = re.fullmatch(r"(MARIO_ANIM_[A-Z0-9_]+)(?:\s*=\s*(0[xX][0-9A-Fa-f]+|\d+))?", entry)
        if item is None:
            raise ValueError(f"invalid animation ID declaration: {entry}")
        name, explicit = item.groups()
        value = int(explicit, 0) if explicit is not None else next_value
        if name in names or value in values:
            raise ValueError("duplicate animation ID")
        names.append(name)
        values.append(value)
        next_value = value + 1
    if not names or values != list(range(len(values))):
        raise ValueError("animation IDs must be contiguous from zero")
    return tuple(names)


def parse_animation_file_text(filename: str, source: str,
                              animation_ids: tuple[str, ...] | None = None) -> tuple[AnimationRecord, ...]:
    arrays: dict[str, tuple[int, ...]] = {}
    for kind, name, body in re.findall(
        r"(?:static\s+)?const\s+(u16|s16)\s+(anim_[A-Fa-f0-9_]+_(?:indices|values))\[\]\s*=\s*\{(.*?)\};",
        source, re.DOTALL,
    ):
        arrays[name] = _numbers(body, signed=kind == "s16")
    records: list[AnimationRecord] = []
    digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
    for symbol, body in re.findall(
        r"(?:static\s+)?const\s+struct\s+Animation\s+(anim_[A-Fa-f0-9]+)\[\]\s*=\s*\{(.*?)\};",
        source, re.DOTALL,
    ):
        fields = [field.strip() for field in body.split(",") if field.strip()]
        if len(fields) != 9:
            raise ValueError(f"{filename}: incomplete Animation header for {symbol}")
        try:
            header = [int(fields[index], 0) for index in range(5)]
        except ValueError as error:
            raise ValueError(f"{filename}: invalid Animation scalar for {symbol}") from error
        values_name, indices_name = fields[6], fields[7]
        if values_name not in arrays or indices_name not in arrays:
            raise ValueError(f"{filename}: missing channel array for {symbol}")
        values, indices = arrays[values_name], arrays[indices_name]
        if len(indices) < 6 or len(indices) % 6:
            raise ValueError(f"{filename}: corrupt Animation index length for {symbol}")
        for channel in range(0, len(indices), 2):
            count, offset = indices[channel:channel + 2]
            if count <= 0 or offset < 0 or offset + count > len(values):
                raise ValueError(f"{filename}: corrupt channel span for {symbol}")
        animation_id = int(symbol.removeprefix("anim_"), 16)
        if animation_ids is not None and animation_id >= len(animation_ids):
            raise ValueError(f"{filename}: animation symbol is outside MarioAnimID")
        enum_name = animation_ids[animation_id] if animation_ids is not None else f"MARIO_ANIM_{animation_id:02X}"
        records.append(AnimationRecord(
            animation_id=animation_id,
            enum_name=enum_name,
            symbol=symbol,
            source_path=filename.replace("\\", "/"),
            source_sha256=digest,
            flags=header[0],
            y_translation_divisor=header[1],
            start_frame=header[2],
            loop_start=header[3],
            frame_count=header[4],
            joint_count=len(indices) // 6 - 1,
            indices=indices,
            values=values,
        ))
    if not records:
        raise ValueError(f"{filename}: no Animation records")
    return tuple(records)


def parse_animation_table_text(filename: str, source: str,
                               table_symbol: str) -> tuple[str, ...]:
    """Parse one closure-selected Animation pointer table without guessing."""
    if not re.fullmatch(r"[A-Za-z_]\w*", table_symbol):
        raise ValueError(f"{filename}: invalid animation table symbol")
    match = re.search(
        r"(?:static\s+)?const\s+struct\s+Animation\s*\*\s*const\s+" +
        re.escape(table_symbol) + r"\s*\[\]\s*=\s*\{(.*?)\};",
        source, re.DOTALL,
    )
    if match is None:
        raise ValueError(f"{filename}: missing animation table {table_symbol}")
    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    body = re.sub(r"//.*", "", body)
    entries = [item.strip() for item in body.split(",") if item.strip()]
    if not entries or entries[-1] != "NULL":
        raise ValueError(f"{filename}: animation table requires one trailing NULL")
    symbols: list[str] = []
    for entry in entries[:-1]:
        item = re.fullmatch(r"&([A-Za-z_]\w*)", entry)
        if item is None:
            raise ValueError(f"{filename}: unsupported animation table entry {entry}")
        symbol = item.group(1)
        if symbol in symbols:
            raise ValueError(f"{filename}: duplicate animation table entry {symbol}")
        symbols.append(symbol)
    if not symbols:
        raise ValueError(f"{filename}: animation table contains no animations")
    return tuple(symbols)


def parse_generic_animation_file_text(
    filename: str,
    source: str,
    symbol_ids: Mapping[str, int],
) -> tuple[AnimationRecord, ...]:
    """Parse closure-selected compact Animation definitions by exact symbol."""
    if (not symbol_ids or any(not re.fullmatch(r"[A-Za-z_]\w*", symbol)
                              for symbol in symbol_ids)):
        raise ValueError(f"{filename}: invalid selected animation symbols")
    ids = tuple(symbol_ids.values())
    if (any(isinstance(value, bool) or not isinstance(value, int) or value < 0
            for value in ids) or len(set(ids)) != len(ids)):
        raise ValueError(f"{filename}: invalid selected animation IDs")
    arrays: dict[str, tuple[int, ...]] = {}
    for kind, name, body in re.findall(
        r"(?:static\s+)?const\s+(u16|s16)\s+([A-Za-z_]\w*)\[\]\s*=\s*\{(.*?)\};",
        source, re.DOTALL,
    ):
        arrays[name] = _numbers(body, signed=kind == "s16")
    records: list[AnimationRecord] = []
    digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
    found: set[str] = set()
    for symbol, body in re.findall(
        r"(?:static\s+)?const\s+struct\s+Animation\s+([A-Za-z_]\w*)\[\]\s*=\s*\{(.*?)\};",
        source, re.DOTALL,
    ):
        if symbol not in symbol_ids:
            continue
        fields = [field.strip() for field in body.split(",") if field.strip()]
        if len(fields) != 9:
            raise ValueError(f"{filename}: incomplete Animation header for {symbol}")
        try:
            header = [int(fields[index], 0) for index in range(5)]
        except ValueError as error:
            raise ValueError(f"{filename}: invalid Animation scalar for {symbol}") from error
        values_name, indices_name = fields[6], fields[7]
        if values_name not in arrays or indices_name not in arrays:
            raise ValueError(f"{filename}: missing channel array for {symbol}")
        values, indices = arrays[values_name], arrays[indices_name]
        if len(indices) < 12 or len(indices) % 6:
            raise ValueError(f"{filename}: corrupt Animation index length for {symbol}")
        for channel in range(0, len(indices), 2):
            count, offset = indices[channel:channel + 2]
            if count <= 0 or offset < 0 or offset + count > len(values):
                raise ValueError(f"{filename}: corrupt channel span for {symbol}")
        records.append(AnimationRecord(
            animation_id=symbol_ids[symbol], enum_name=symbol, symbol=symbol,
            source_path=filename.replace("\\", "/"), source_sha256=digest,
            flags=header[0], y_translation_divisor=header[1],
            start_frame=header[2], loop_start=header[3], frame_count=header[4],
            joint_count=len(indices) // 6 - 1, indices=indices, values=values,
        ))
        found.add(symbol)
    missing = sorted(set(symbol_ids) - found)
    if missing:
        raise ValueError(f"{filename}: missing selected Animation {missing[0]}")
    records.sort(key=lambda record: record.animation_id)
    return tuple(records)


def load_animation_inventory(root: Path) -> AnimationInventory:
    root = root.resolve()
    ids = parse_animation_id_header_text(
        (root / "include/mario_animation_ids.h").read_text(encoding="utf-8"))
    files = sorted((root / "assets/anims").glob("*.inc.c"), key=lambda path: path.name)
    source_files: list[SourceFile] = []
    records: list[AnimationRecord] = []
    for path in files:
        source = path.read_text(encoding="utf-8")
        relative = path.relative_to(root).as_posix()
        source_files.append(SourceFile(relative, hashlib.sha256(source.encode("utf-8")).hexdigest()))
        records.extend(parse_animation_file_text(relative, source, ids))
    records.sort(key=lambda record: record.animation_id)
    actual = [record.animation_id for record in records]
    if len(files) != 193:
        raise ValueError(f"Mario animation source inventory requires 193 files, found {len(files)}")
    if len(ids) != 209 or actual != list(range(209)):
        raise ValueError("Mario animation inventory does not cover the complete animation ID set")
    if len({record.symbol for record in records}) != len(records):
        raise ValueError("duplicate animation ID or symbol")
    return AnimationInventory(ids, tuple(source_files), tuple(records))


def validate_geo_node_vocabulary(source: str) -> None:
    nodes = set(re.findall(r"\b(GEO_[A-Z0-9_]+)\s*\(", source))
    unsupported = sorted(nodes - SUPPORTED_GEO_NODES)
    if unsupported:
        raise ValueError(f"unsupported geo node: {unsupported[0]}")


def parse_mario_skeleton(source: str) -> tuple[SkeletonJoint, ...]:
    """Extract the 20 source channel owners from ``mario_geo_body``.

    The two open-hand branches consume animation triplets just like the
    original GeoLayout evaluator; the face branch does not.
    """
    validate_geo_node_vocabulary(source)
    layouts = {
        name: body for name, body in re.findall(
            r"(?:static\s+)?const\s+GeoLayout\s+(\w+)\[\]\s*=\s*\{(.*?)\};",
            source, re.DOTALL)
    }
    body = layouts.get("mario_geo_body")
    if body is None:
        raise ValueError("missing mario_geo_body")
    tokens = [
        (match.group(1), match.group(2) or "")
        for match in re.finditer(
            r"(GEO_(?:OPEN_NODE|CLOSE_NODE)|GEO_\w+)\s*(?:\(([^)]*)\))?", body)
    ]
    branch_offsets = {
        "mario_geo_left_hand": (60, 0, 0),
        "mario_geo_right_hand": (60, 0, 0),
    }
    joints: list[SkeletonJoint] = []

    def add(parent: int, translation: tuple[int, int, int], node: int,
            branch: int = 0xFFFF) -> int:
        ordinal = len(joints)
        joints.append(SkeletonJoint(ordinal, parent, translation, node, branch))
        return ordinal

    def walk(index: int, parent: int) -> int:
        last = parent
        while index < len(tokens):
            node_ordinal = index
            macro, arguments = tokens[index]
            index += 1
            if macro == "GEO_CLOSE_NODE":
                return index
            if macro == "GEO_OPEN_NODE":
                index = walk(index, last)
                last = parent
                continue
            if macro == "GEO_RETURN":
                return index
            if macro == "GEO_ANIMATED_PART":
                fields = [field.strip() for field in arguments.split(",")]
                if len(fields) != 5:
                    raise ValueError("unsupported GEO_ANIMATED_PART shape")
                last = add(parent, tuple(int(fields[axis]) for axis in range(1, 4)),
                           node_ordinal)
                continue
            if macro == "GEO_BRANCH":
                fields = [field.strip() for field in arguments.split(",")]
                if len(fields) == 2 and fields[1] in branch_offsets:
                    last = add(parent, branch_offsets[fields[1]], node_ordinal,
                               node_ordinal)
        return index

    walk(0, -1)
    if len(joints) != 20:
        raise ValueError(f"Mario skeleton requires 20 animation joints, found {len(joints)}")
    return tuple(joints)


def _legacy_lines(animation_root: Path) -> list[str]:
    """Port the historical converter literally so its bytes remain stable."""
    items: list[tuple[str, str, object]] = []
    lengths: dict[str, int] = {}
    order: dict[str, int] = {}
    header_count = 0
    for path in sorted(animation_root.glob("*.inc.c"), key=lambda item: item.name):
        logical: list[str] = []
        for line in path.read_text(encoding="utf-8").splitlines():
            line = re.sub(r"/\*.*?\*/", "", line).split("//", 1)[0].strip()
            if line:
                logical.append(line)
        index = 0
        while index < len(logical):
            line = logical[index]
            for prefix in ("static ", "const "):
                if line.startswith(prefix):
                    line = line[len(prefix):]
            logical[index] = line
            is_struct = line.startswith("struct Animation ") and line.endswith("[] = {")
            is_indices = line.startswith("u16 ") and line.endswith("[] = {")
            is_values = line.startswith("s16 ") and line.endswith("[] = {")
            if is_struct:
                name = line[len("struct Animation "):-6]
                start = index + 1
                if start + 9 >= len(logical) or logical[start + 9] != "};":
                    raise SyntaxError(f"{path.name}: malformed Animation header")
                v1, v2, v3, v4, v5 = (int(logical[start + n].rstrip(","), 0) for n in range(5))
                values = logical[start + 6].rstrip(",")
                indices = logical[start + 7].rstrip(",")
                items.append(("header", name, (v1, v2, v3, v4, v5, values, indices)))
                order[name] = len(items)
                header_count += 1
                index = start + 10
                continue
            if is_indices or is_values:
                name = line[len("s16 "):-6]
                index += 1
                values: list[str] = []
                while index < len(logical) and logical[index] != "};":
                    value_line = logical[index].rstrip(",")
                    if value_line:
                        values.extend(value_line.split(","))
                    index += 1
                if index >= len(logical):
                    raise SyntaxError(f"{path.name}: unterminated array")
                items.append(("array", name, (is_indices, values)))
                lengths[name] = len(values)
                order[name] = len(items)
                index += 1
                continue
            raise SyntaxError(f"{path.name}: unsupported source syntax: {line}")

    structdef = ["u32 numEntries;", "const struct Animation *addrPlaceholder;",
                 f"struct OffsetSizePair entries[{header_count}];"]
    structobj = [f"{header_count},", "NULL,", "{"]
    for item_type, name, obj in items:
        if item_type == "header":
            _v1, _v2, _v3, _v4, _v5, values, indices = obj
            if order[indices] < order[name] or order[values] < order[indices]:
                raise SyntaxError(f"invalid legacy array order for {name}")
            offset = f"offsetof(struct MarioAnimsObj, {name})"
            end = f"offsetof(struct MarioAnimsObj, {values}) + sizeof(gMarioAnims.{values})"
            structobj.append("{" + offset + ", " + end + " - " + offset + "},")
    structobj.append("},")
    for item_type, name, obj in items:
        if item_type == "header":
            v1, v2, v3, v4, v5, values, indices = obj
            indices_len = lengths[indices] // 6 - 1
            offset = f"offsetof(struct MarioAnimsObj, {name})"
            end = f"offsetof(struct MarioAnimsObj, {values}) + sizeof(gMarioAnims.{values})"
            structdef.append(f"struct Animation {name};")
            structobj.append("{" + ", ".join([
                str(v1), str(v2), str(v3), str(v4), str(v5), str(indices_len),
                f"(const s16 *)(offsetof(struct MarioAnimsObj, {values}) - {offset})",
                f"(const u16 *)(offsetof(struct MarioAnimsObj, {indices}) - {offset})",
                end + " - " + offset,
            ]) + "},")
        else:
            is_indices, values = obj
            value_type = "u16" if is_indices else "s16"
            structdef.append(f"{value_type} {name}[{len(values)}];")
            structobj.append("{" + ",".join(values) + "},")
    output = ["#include \"game/memory.h\"", "#include <stddef.h>", "", "const struct MarioAnimsObj {"]
    output.extend(structdef)
    output.append("} gMarioAnims = {")
    output.extend(structobj)
    output.append("};")
    return output


def render_legacy_mario_anims(animation_root: Path) -> bytes:
    # ``print`` in the historical converter used the platform text newline.
    # Preserve that byte contract so Windows-built animation blobs do not
    # silently churn while the parser is shared with the Saturn compiler.
    return (os.linesep.join(_legacy_lines(animation_root)) + os.linesep).encode("utf-8")
