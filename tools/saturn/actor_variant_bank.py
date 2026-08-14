#!/usr/bin/env python3
"""Compile one closure-selected generic actor variant into an S64B bank.

Reference reuse is intentionally in-tree: source identity comes directly from
``actor_family_bundle``; the GeoLayout/display-list structural walk comes from
``dl_rigid_groups``; C block, Vtx, matrix, and Fast3D integer parsing are
close-ported from ``extract_mario_actor``; primitives come from
``saturn_mesh_ir``; and bytes come only from ``compile_actor_bank``'s shared
encoder. Unknown source constructs fail closed instead of being omitted.
"""

from __future__ import annotations

import ast
import hashlib
import re
import struct
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Mapping, Sequence

from actor_bank_format import validate_actor_bank
from actor_bank_v2 import pack_actor_bank_v2, source_identity_v2
from actor_family_bundle import SourceRecord, source_identity
from actor_material_v2 import (
    ActorMaterialV2Error,
    BOB_DIRECT_TEXTURED_KEYS,
    MaterialSignatureV2,
    compile_materials_v2,
    partial_transfer_divergence_v2,
)
from actor_source import (
    AnimationRecord,
    parse_animation_table_text,
    parse_generic_animation_file_text,
)
from compile_actor_bank import Geometry, Joint, Vertex, pack_actor_bank
from dl_rigid_groups import (
    walk_geo_layout,
)
from extract_mario_actor import (
    identity_matrix,
    matrix_apply,
    matrix_mul,
    scale_matrix,
)
from saturn_mesh_ir import compile_mesh_ir
from vdp1_texture import decode_png_rgb1555


class ActorVariantError(ValueError):
    """Base class for deterministic generic actor compilation failures."""


class ActorSourceSelectionError(ActorVariantError):
    """The closure did not select one exact source/model/root."""


class ActorSourceDriftError(ActorVariantError):
    """A closure-attested source no longer has its recorded hash."""


class UnsupportedActorSourceError(ActorVariantError):
    """A source construct is known to be outside this compiler boundary."""


class MalformedActorSourceError(ActorVariantError):
    """A selected source construct is malformed."""


class ActorJointOwnershipError(ActorVariantError):
    """Geometry or pose channels cannot be assigned to one exact joint."""


class ActorAnimationBindingError(ActorVariantError):
    """A closure-selected animation table/definition is invalid."""


@dataclass(frozen=True)
class CompiledActorVariant:
    family_ordinal: int
    model_id: int
    source_sha256: str
    payload_sha256: str
    lane_bytes: int
    maximum_scratch: int
    sources: tuple[SourceRecord, ...]
    payload: bytes
    report: dict[str, object]


@dataclass(frozen=True)
class _Definition:
    path: str
    body: str


@dataclass(frozen=True)
class _ModelBinding:
    model: str
    root: str
    kind: str
    layer: str | None


_C_INTEGER = re.compile(r"-?(?:0[xX][0-9A-Fa-f]+|\d+)")

_FAST3D_SCALARS = {
    "G_IM_FMT_RGBA": 0,
    "G_IM_FMT_IA": 3,
    "G_IM_SIZ_16b": 2,
    "G_IM_SIZ_16b_BYTES": 2,
    "G_TX_LOADTILE": 7,
    "G_TX_RENDERTILE": 0,
    "G_TX_WRAP": 0,
    "G_TX_NOMIRROR": 0,
    "G_TX_CLAMP": 2,
    "G_TX_NOMASK": 0,
    "G_TX_NOLOD": 0,
    "G_TEXTURE_IMAGE_FRAC": 2,
    "G_ON": 1,
    "G_OFF": 0,
}


def _fast3d_scalar(value: str, label: str) -> int:
    """Evaluate one bounded integer-only Fast3D macro expression."""
    try:
        root = ast.parse(value.strip(), mode="eval")
    except SyntaxError as error:
        raise MalformedActorSourceError(f"computed {label} state") from error

    def visit(node: ast.AST) -> int:
        if isinstance(node, ast.Expression):
            return visit(node.body)
        if isinstance(node, ast.Constant) and isinstance(node.value, int) and \
                not isinstance(node.value, bool):
            return node.value
        if isinstance(node, ast.Name) and node.id in _FAST3D_SCALARS:
            return _FAST3D_SCALARS[node.id]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            operand = visit(node.operand)
            return operand if isinstance(node.op, ast.UAdd) else -operand
        operations = {
            ast.Add: lambda a, b: a + b,
            ast.Sub: lambda a, b: a - b,
            ast.Mult: lambda a, b: a * b,
            ast.LShift: lambda a, b: a << b,
            ast.RShift: lambda a, b: a >> b,
            ast.BitOr: lambda a, b: a | b,
        }
        if isinstance(node, ast.BinOp) and type(node.op) in operations:
            left, right = visit(node.left), visit(node.right)
            if isinstance(node.op, (ast.LShift, ast.RShift)):
                if not 0 <= left <= 0xFFFFFFFF:
                    raise MalformedActorSourceError(
                        f"{label} shift operand exceeds uint32")
                if not 0 <= right <= 31:
                    raise MalformedActorSourceError(
                        f"{label} shift count is outside 0..31")
            return operations[type(node.op)](left, right)
        raise ValueError

    try:
        result = visit(root)
    except MalformedActorSourceError:
        raise
    except (ValueError, OverflowError) as error:
        raise MalformedActorSourceError(f"computed {label} state") from error
    if not -(1 << 31) <= result <= 0xFFFFFFFF:
        raise MalformedActorSourceError(f"{label} state exceeds uint32")
    return result


def _geometry_modes(value: str, label: str) -> tuple[str, ...]:
    fields = tuple(item.strip() for item in value.split("|"))
    allowed = {"G_LIGHTING", "G_SHADING_SMOOTH", "G_CULL_BACK"}
    if not fields or any(item not in allowed for item in fields) or len(set(fields)) != len(fields):
        raise UnsupportedActorSourceError(f"unapproved geometry mode state in {label}")
    return fields


def _tile_axis_mode(value: str, label: str) -> tuple[bool, bool]:
    fields = tuple(item.strip() for item in value.split("|"))
    allowed = {"G_TX_WRAP", "G_TX_NOMIRROR", "G_TX_CLAMP", "G_TX_MIRROR"}
    if not fields or any(item not in allowed for item in fields):
        raise UnsupportedActorSourceError(f"unapproved tile wrap/clamp state in {label}")
    clamp = "G_TX_CLAMP" in fields
    mirror = "G_TX_MIRROR" in fields
    if clamp and "G_TX_WRAP" in fields or mirror and "G_TX_NOMIRROR" in fields:
        raise UnsupportedActorSourceError(f"ambiguous tile wrap/clamp state in {label}")
    return clamp, mirror


def _strip_comments(source: str, label: str) -> str:
    output: list[str] = []
    cursor = 0
    while cursor < len(source):
        if source.startswith("//", cursor):
            end = source.find("\n", cursor + 2)
            if end < 0:
                break
            output.append("\n")
            cursor = end + 1
        elif source.startswith("/*", cursor):
            end = source.find("*/", cursor + 2)
            if end < 0:
                raise MalformedActorSourceError(f"unterminated block comment in {label}")
            output.append("".join("\n" if char == "\n" else " "
                                  for char in source[cursor:end + 2]))
            cursor = end + 2
        else:
            output.append(source[cursor])
            cursor += 1
    return "".join(output)


def _definition_bodies(source: str, kind: str, symbol: str,
                       label: str) -> tuple[str, ...]:
    clean = _strip_comments(source, label)
    declaration = re.compile(
        r"(?:static\s+)?const\s+" + re.escape(kind) + r"\s+" +
        re.escape(symbol) + r"\s*\[\]\s*=\s*\{")
    bodies: list[str] = []
    for match in declaration.finditer(clean):
        depth = 1
        cursor = match.end()
        while cursor < len(clean) and depth:
            if clean[cursor] == "{":
                depth += 1
            elif clean[cursor] == "}":
                depth -= 1
            cursor += 1
        if depth:
            raise MalformedActorSourceError(
                f"unterminated {kind} definition {symbol} in {label}")
        end = cursor - 1
        terminator = re.match(r"\s*;", clean[cursor:])
        if terminator is None:
            raise MalformedActorSourceError(
                f"malformed {kind} terminator {symbol} in {label}")
        bodies.append(clean[match.end():end])
    return tuple(bodies)


def _macro_tokens(body: str, prefix: str, label: str) -> list[tuple[str, str]]:
    tokens: list[tuple[str, str]] = []
    cursor = 0
    while True:
        whitespace = re.match(r"\s*", body[cursor:])
        cursor += whitespace.end()
        if cursor == len(body):
            break
        name = re.match(r"[A-Za-z_]\w*", body[cursor:])
        if name is None or not name.group(0).startswith(prefix):
            raise MalformedActorSourceError(f"unexplained token in {label}")
        macro = name.group(0)
        cursor += name.end()
        whitespace = re.match(r"\s*", body[cursor:])
        cursor += whitespace.end()
        if cursor == len(body) or body[cursor] != "(":
            raise MalformedActorSourceError(f"malformed command {macro} in {label}")
        argument_start = cursor + 1
        depth = 1
        cursor += 1
        while cursor < len(body) and depth:
            if body[cursor] in "\"'":
                raise MalformedActorSourceError(
                    f"unsupported literal in command {macro} in {label}")
            if body[cursor] == "(":
                depth += 1
            elif body[cursor] == ")":
                depth -= 1
            cursor += 1
        if depth:
            raise MalformedActorSourceError(f"unterminated command {macro} in {label}")
        arguments = body[argument_start:cursor - 1]
        whitespace = re.match(r"\s*", body[cursor:])
        cursor += whitespace.end()
        if cursor == len(body) or body[cursor] != ",":
            raise MalformedActorSourceError(f"missing command comma after {macro} in {label}")
        cursor += 1
        tokens.append((macro, arguments))
    if not tokens:
        raise MalformedActorSourceError(f"empty command initializer in {label}")
    return tokens


def _integer_literal(value: str, label: str) -> int:
    value = value.strip()
    if _C_INTEGER.fullmatch(value) is None:
        raise MalformedActorSourceError(f"{label} must be an integer literal")
    negative = value.startswith("-")
    digits = value.lstrip("-")
    base = 16 if digits.lower().startswith("0x") else (
        8 if len(digits) > 1 and digits.startswith("0") else 10)
    try:
        result = int(digits, base)
    except ValueError as error:
        raise MalformedActorSourceError(f"{label} is not a valid C integer") from error
    return -result if negative else result


def _integer_fields(args: str, count: int, label: str) -> list[int]:
    fields = _arguments(args)
    if len(fields) != count:
        raise MalformedActorSourceError(f"malformed {label}")
    return [_integer_literal(field, label) for field in fields]


_VTX_ROW = re.compile(
    r"\{\s*\{\s*\{\s*(" + _C_INTEGER.pattern + r")\s*,\s*(" +
    _C_INTEGER.pattern + r")\s*,\s*(" + _C_INTEGER.pattern +
    r")\s*\}\s*,\s*(" + _C_INTEGER.pattern +
    r")\s*,\s*\{\s*(" + _C_INTEGER.pattern + r")\s*,\s*(" +
    _C_INTEGER.pattern + r")\s*\}\s*,\s*\{\s*(" +
    _C_INTEGER.pattern + r")\s*,\s*(" + _C_INTEGER.pattern +
    r")\s*,\s*(" + _C_INTEGER.pattern + r")\s*,\s*(" +
    _C_INTEGER.pattern + r")\s*\}\s*\}\s*\}")


def _vertex_rows(body: str, label: str) -> list[tuple[int, ...]]:
    rows: list[tuple[int, ...]] = []
    cursor = 0
    while True:
        whitespace = re.match(r"\s*", body[cursor:])
        cursor += whitespace.end()
        if cursor == len(body):
            break
        match = _VTX_ROW.match(body, cursor)
        if match is None:
            raise MalformedActorSourceError(f"malformed Vtx row in {label}")
        values = tuple(_integer_literal(value, f"Vtx field in {label}")
                       for value in match.groups())
        if any(value < -0x8000 or value > 0x7FFF for value in values[0:3]):
            raise MalformedActorSourceError(f"Vtx coordinate exceeds int16 in {label}")
        if not 0 <= values[3] <= 0xFFFF:
            raise MalformedActorSourceError(f"Vtx flag exceeds uint16 in {label}")
        if any(value < -0x8000 or value > 0x7FFF for value in values[4:6]):
            raise MalformedActorSourceError(f"Vtx texture coordinate exceeds int16 in {label}")
        if any(value < -0x80 or value > 0xFF for value in values[6:10]):
            raise MalformedActorSourceError(f"Vtx color/normal exceeds byte range in {label}")
        rows.append(values)
        cursor = match.end()
        whitespace = re.match(r"\s*", body[cursor:])
        cursor += whitespace.end()
        if cursor == len(body) or body[cursor] != ",":
            raise MalformedActorSourceError(f"missing Vtx row comma in {label}")
        cursor += 1
    if not rows:
        raise MalformedActorSourceError(f"empty Vtx initializer in {label}")
    return rows


def _normal_path(value: object) -> str:
    if not isinstance(value, str) or not value or value.startswith("/") or \
            (len(value) >= 2 and value[1] == ":") or "\\" in value or "\x00" in value:
        raise ActorSourceSelectionError("source path is not normalized root-relative UTF-8")
    if any(part in ("", ".", "..") for part in value.split("/")):
        raise ActorSourceSelectionError("source path contains an invalid segment")
    try:
        value.encode("utf-8")
    except UnicodeEncodeError as error:
        raise ActorSourceSelectionError("source path is not normalized root-relative UTF-8") from error
    return value


class _SourceIndex:
    def __init__(self, root: Path, records: Sequence[dict[str, object]]) -> None:
        self.root = root.resolve()
        self.expected: dict[str, bytes] = {}
        for record in records:
            sources = record.get("sources")
            if not isinstance(sources, list):
                raise ActorSourceSelectionError("closure record has no source attestations")
            for item in sources:
                if not isinstance(item, dict):
                    raise ActorSourceSelectionError("closure source record is malformed")
                path = _normal_path(item.get("path"))
                digest = item.get("sha256")
                if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
                    raise ActorSourceSelectionError(f"closure source hash is malformed: {path}")
                raw = bytes.fromhex(digest)
                prior = self.expected.get(path)
                if prior is not None and prior != raw:
                    raise ActorSourceSelectionError(f"closure source hash conflict: {path}")
                self.expected[path] = raw
        # Source discovery is intentionally bounded by the closure. Looking
        # through the host checkout here would allow an unattested same-name
        # definition to change selection without entering source_identity().
        self.candidates = tuple(sorted(path for path in self.expected
                                       if path.endswith((".c", ".h"))))
        self._bytes: dict[str, bytes] = {}
        self._text: dict[str, str] = {}
        self._definitions: dict[tuple[str, str, str], tuple[str, ...]] = {}
        self._textures: dict[tuple[str, str], dict[str, object]] = {}
        self.used: set[str] = set()

    def _load(self, path: str) -> bytes:
        path = _normal_path(path)
        if path not in self._bytes:
            source = self.root / path
            if not source.is_file():
                raise ActorSourceSelectionError(f"selected source is missing: {path}")
            try:
                self._bytes[path] = source.read_bytes()
            except OSError as error:
                raise ActorSourceSelectionError(f"selected source is unreadable: {path}") from error
        return self._bytes[path]

    def text(self, path: str, *, use: bool = False) -> str:
        path = _normal_path(path)
        if path not in self._text:
            try:
                self._text[path] = self._load(path).decode("utf-8")
            except UnicodeDecodeError as error:
                raise MalformedActorSourceError(f"selected source is not UTF-8: {path}") from error
        if use:
            self.mark_used(path)
        return self._text[path]

    def digest(self, path: str) -> bytes:
        return hashlib.sha256(self._load(path)).digest()

    def mark_used(self, path: str) -> None:
        path = _normal_path(path)
        if path not in self.expected:
            raise ActorSourceSelectionError(f"reached source is not closure-attested: {path}")
        actual = self.digest(path)
        expected = self.expected[path]
        if actual != expected:
            raise ActorSourceDriftError(f"closure source hash drift: {path}")
        self.used.add(path)

    def require_attested(self, path: str, label: str) -> None:
        path = _normal_path(path)
        if path not in self.expected:
            raise ActorSourceSelectionError(f"{label} is not closure-attested: {path}")
        self.mark_used(path)

    def definition_bodies(self, path: str, kind: str,
                          symbol: str) -> tuple[str, ...]:
        key = (path, kind, symbol)
        if key not in self._definitions:
            self._definitions[key] = _definition_bodies(
                self.text(path), kind, symbol, path)
        return self._definitions[key]

    def declared_block(self, kind: str, symbol: str, path: str) -> _Definition:
        path = _normal_path(path)
        self.require_attested(path, f"declared {kind} source")
        bodies = self.definition_bodies(path, kind, symbol)
        if len(bodies) != 1:
            detail = "missing" if not bodies else "duplicate"
            raise ActorSourceSelectionError(
                f"{detail} declared {kind} {symbol}: {path}")
        return _Definition(path, bodies[0])

    def resolve_block(self, kind: str, symbol: str,
                      preferred: str | None = None) -> _Definition:
        if not re.fullmatch(r"[A-Za-z_]\w*", symbol):
            raise ActorSourceSelectionError(f"invalid {kind} symbol: {symbol}")
        if preferred is not None:
            preferred_bodies = self.definition_bodies(preferred, kind, symbol)
            if len(preferred_bodies) > 1:
                raise ActorSourceSelectionError(
                    f"ambiguous {kind} source {symbol}: {preferred}")
            if preferred_bodies:
                self.mark_used(preferred)
                return _Definition(preferred, preferred_bodies[0])
        matches: list[tuple[str, str]] = []
        for path in self.candidates:
            bodies = self.definition_bodies(path, kind, symbol)
            if len(bodies) > 1:
                raise ActorSourceSelectionError(
                    f"ambiguous {kind} source {symbol}: {path}")
            if bodies:
                matches.append((path, bodies[0]))
        if not matches:
            raise ActorSourceSelectionError(f"missing {kind} source: {symbol}")
        if len(matches) != 1:
            raise ActorSourceSelectionError(
                f"ambiguous {kind} source {symbol}: {', '.join(item[0] for item in matches)}")
        self.mark_used(matches[0][0])
        return _Definition(matches[0][0], matches[0][1])

    def resolve_animation(self, symbol: str) -> str:
        pattern = re.compile(
            r"(?:static\s+)?const\s+struct\s+Animation\s+" +
            re.escape(symbol) + r"\s*(?:\[\])?\s*=")
        matches = [path for path in self.candidates if pattern.search(self.text(path))]
        if not matches:
            raise ActorAnimationBindingError(f"missing selected Animation: {symbol}")
        if len(matches) != 1:
            raise ActorAnimationBindingError(
                f"ambiguous selected Animation {symbol}: {', '.join(matches)}")
        self.mark_used(matches[0])
        return matches[0]

    def light_rgb(self, symbol: str, preferred: str) -> tuple[int, int, int]:
        pattern = re.compile(
            r"(?:static\s+)?(?:const\s+)?Lights1\s+" + re.escape(symbol) +
            r"\s*=\s*gdSPDefLights1\s*\((.*?)\)\s*;", re.DOTALL)
        preferred_matches = pattern.findall(_strip_comments(self.text(preferred), preferred))
        if len(preferred_matches) > 1:
            raise ActorSourceSelectionError(f"ambiguous Lights1 source: {symbol}")
        matches = [(preferred, preferred_matches[0])] if preferred_matches else []
        if not matches:
            matches = [(path, body) for path in self.candidates
                       for body in pattern.findall(_strip_comments(self.text(path), path))]
        if len(matches) != 1:
            detail = "missing" if not matches else "ambiguous"
            raise ActorSourceSelectionError(f"{detail} Lights1 source: {symbol}")
        path, body = matches[0]
        self.mark_used(path)
        values = _integer_fields(body, 9, f"gdSPDefLights1 {symbol}")
        if len(values) != 9 or any(value < -128 or value > 255 for value in values):
            raise MalformedActorSourceError(f"unsupported gdSPDefLights1 shape: {symbol}")
        return tuple(max(0, min(31, value >> 3)) for value in values[3:6])

    def texture_source(self, symbol: str, preferred: str) -> dict[str, object]:
        """Resolve a texture declaration and its exact checked-in PNG source."""
        key = (symbol, preferred)
        if key in self._textures:
            return self._textures[key]
        if not re.fullmatch(r"[A-Za-z_]\w*", symbol):
            raise ActorSourceSelectionError(f"invalid texture image symbol: {symbol}")
        pattern = re.compile(
            r"(?:ALIGNED8\s+)?(?:static\s+)?const\s+(?:Texture|u8|u16)\s+" +
            re.escape(symbol) +
            r"\s*\[\]\s*=\s*\{\s*#include\s+\"([^\"]+)\"\s*\}\s*;",
            re.DOTALL)
        preferred_matches = pattern.findall(_strip_comments(self.text(preferred), preferred))
        if len(preferred_matches) > 1:
            raise ActorSourceSelectionError(f"ambiguous texture image source: {symbol}")
        matches = [(preferred, preferred_matches[0])] if preferred_matches else []
        if not matches:
            matches = [(path, include) for path in self.candidates
                       for include in pattern.findall(_strip_comments(self.text(path), path))]
        if len(matches) != 1:
            detail = "missing" if not matches else "ambiguous"
            raise ActorSourceSelectionError(
                f"{detail} texture image source: {symbol}")
        declaration_path, include_path = matches[0]
        self.mark_used(declaration_path)
        relative = _normal_path(include_path)
        if not relative.endswith((".rgba16.inc.c", ".ia16.inc.c")):
            raise UnsupportedActorSourceError(
                f"unapproved texture source include: {relative}")
        png_path = _normal_path(relative[:-6] + ".png")
        self.require_attested(png_path, "texture PNG source")
        try:
            payload = self._load(png_path)
            width, height, pixels = decode_png_rgb1555(payload, png_path)
        except (OSError, ValueError) as error:
            raise UnsupportedActorSourceError(
                f"texture source rejected: {png_path}: {error}") from error
        if hashlib.sha256(payload).digest() != self.expected[png_path]:
            raise ActorSourceDriftError(f"closure source hash drift: {png_path}")
        result: dict[str, object] = {
            "symbol": symbol,
            "path": png_path,
            "sha256": hashlib.sha256(payload).digest(),
            "width": width,
            "height": height,
            "pixels": pixels,
            "source_bytes": len(payload),
        }
        self._textures[key] = result
        return result

    def source_records(self) -> tuple[SourceRecord, ...]:
        return tuple(SourceRecord(path, self.digest(path)) for path in sorted(self.used))


def _arguments(args: str) -> list[str]:
    fields: list[str] = []
    depth = 0
    start = 0
    for index, char in enumerate(args):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth < 0:
                raise MalformedActorSourceError("unbalanced command arguments")
        elif char == "," and depth == 0:
            fields.append(args[start:index].strip())
            start = index + 1
    if depth:
        raise MalformedActorSourceError("unbalanced command arguments")
    fields.append(args[start:].strip())
    return fields


def _display_list_arg(macro: str, args: str) -> tuple[str, str] | None:
    if macro not in ("GEO_ANIMATED_PART", "GEO_DISPLAY_LIST"):
        return None
    fields = _arguments(args)
    if macro == "GEO_DISPLAY_LIST" and len(fields) != 2:
        raise MalformedActorSourceError("malformed GEO_DISPLAY_LIST")
    if macro == "GEO_ANIMATED_PART" and len(fields) != 5:
        raise MalformedActorSourceError("malformed GEO_ANIMATED_PART")
    symbol = fields[-1]
    if symbol == "NULL":
        return None
    if not re.fullmatch(r"[A-Za-z_]\w*", symbol):
        raise MalformedActorSourceError(f"malformed display-list binding: {symbol}")
    return fields[0], symbol


def _branch_arg(macro: str, args: str) -> str | None:
    if macro not in ("GEO_BRANCH", "GEO_BRANCH_AND_LINK"):
        return None
    fields = _arguments(args)
    if macro == "GEO_BRANCH_AND_LINK" and len(fields) == 1:
        target = fields[0]
    elif macro == "GEO_BRANCH" and len(fields) == 2:
        if _integer_literal(fields[0], "GEO_BRANCH type") not in (0, 1):
            raise MalformedActorSourceError("GEO_BRANCH type must be zero or one")
        target = fields[1]
    else:
        raise MalformedActorSourceError(f"malformed {macro}")
    if re.fullmatch(r"[A-Za-z_]\w*", target) is None:
        raise MalformedActorSourceError(f"malformed {macro} target")
    return target


def _collect_layouts(index: _SourceIndex, entry: str, path: str,
                     layouts: dict[str, list[tuple[str, str]]],
                     layout_paths: dict[str, str], stack: tuple[str, ...] = (),
                     *, declared: bool = False) -> None:
    if entry in stack:
        raise ActorSourceSelectionError(f"recursive GeoLayout: {' -> '.join(stack + (entry,))}")
    definition = (index.declared_block("GeoLayout", entry, path) if declared
                  else index.resolve_block("GeoLayout", entry, path))
    prior = layout_paths.get(entry)
    if prior is not None and prior != definition.path:
        raise ActorSourceSelectionError(f"ambiguous reached GeoLayout: {entry}")
    layouts[entry] = _macro_tokens(
        definition.body, "GEO_", f"GeoLayout {entry} in {definition.path}")
    terminators = [position for position, (macro, _args) in enumerate(layouts[entry])
                   if macro in ("GEO_END", "GEO_RETURN")]
    if terminators != [len(layouts[entry]) - 1]:
        raise MalformedActorSourceError(
            f"GeoLayout {entry} requires one final GEO_END/GEO_RETURN")
    layout_paths[entry] = definition.path
    for macro, args in layouts[entry]:
        target = _branch_arg(macro, args)
        if target is not None and target not in layouts:
            _collect_layouts(index, target, definition.path, layouts, layout_paths,
                             stack + (entry,))


def _collect_lists(index: _SourceIndex, name: str, preferred: str,
                   lists: dict[str, list[tuple[str, str]]],
                   list_paths: dict[str, str], stack: tuple[str, ...] = (),
                   *, declared: bool = False) -> None:
    if name in stack:
        raise ActorSourceSelectionError(f"recursive display list: {' -> '.join(stack + (name,))}")
    if len(stack) >= 256:
        raise ActorSourceSelectionError(
            f"display-list traversal depth exceeds 256 at {name}")
    definition = (index.declared_block("Gfx", name, preferred) if declared
                  else index.resolve_block("Gfx", name, preferred))
    prior = list_paths.get(name)
    if prior is not None:
        if prior != definition.path:
            raise ActorSourceSelectionError(f"ambiguous reached display list: {name}")
        return
    lists[name] = _macro_tokens(
        definition.body, "gs", f"Gfx {name} in {definition.path}")
    ends = [position for position, (macro, _args) in enumerate(lists[name])
            if macro == "gsSPEndDisplayList"]
    branches = [position for position, (macro, _args) in enumerate(lists[name])
                if macro == "gsSPBranchList"]
    tail: str | None = None
    if branches:
        if branches != [len(lists[name]) - 1] or ends:
            raise MalformedActorSourceError(
                f"gsSPBranchList must be the sole final terminator in {name}")
        fields = _arguments(lists[name][-1][1])
        if len(fields) != 1 or re.fullmatch(r"[A-Za-z_]\w*", fields[0]) is None:
            raise MalformedActorSourceError(f"malformed gsSPBranchList in {name}")
        tail = fields[0]
    elif ends != [len(lists[name]) - 1] or lists[name][-1][1].strip():
        raise MalformedActorSourceError(
            f"display list {name} requires one final gsSPEndDisplayList()")
    list_paths[name] = definition.path
    for macro, args in lists[name]:
        if macro == "gsSPDisplayList":
            child = re.fullmatch(r"\s*([A-Za-z_]\w*)\s*", args)
            if child is None:
                raise MalformedActorSourceError(f"malformed gsSPDisplayList in {name}")
            _collect_lists(index, child.group(1), definition.path, lists, list_paths,
                           stack + (name,))
    if tail is not None:
        _collect_lists(index, tail, definition.path, lists, list_paths,
                       stack + (name,))


@dataclass(frozen=True)
class _Context:
    joint: int | None
    matrix: tuple[float, ...]
    switch: int | None = None
    billboard: int | None = None
    scale_q16: int = 65536


_REPRESENTABLE_LAYERS = {
    "LAYER_OPAQUE": "opaque",
    "LAYER_ALPHA": "alpha",
    "LAYER_TRANSPARENT": "translucent",
}


class _GeoCompiler:
    _LAYERS = _REPRESENTABLE_LAYERS
    _UNSUPPORTED = {
        "GEO_ASM", "GEO_HELD_OBJECT", "GEO_RENDER_RANGE",
        "GEO_TRANSLATE_ROTATE", "GEO_ROTATION_NODE",
    }

    def __init__(self, layouts: Mapping[str, list[tuple[str, str]]], entry: str,
                 allow_saturn_reductions: bool = False) -> None:
        self.layouts = layouts
        self.entry = entry
        self.allow_saturn_reductions = allow_saturn_reductions
        base = _Context(None, identity_matrix())
        self.scope = base
        self.node = base
        self.stack: list[_Context] = []
        self.node_ordinal = 0
        self.joints: list[Joint] = []
        self.parts: list[dict[str, object]] = []
        self.switches: list[dict[str, object]] = []
        self.switch_joints: dict[int, tuple[int, int, tuple[int, int, int]]] = {}
        self.billboards: list[dict[str, object]] = []
        self.reductions: list[dict[str, object]] = []

    def _integer(self, value: str, label: str) -> int:
        try:
            result = int(value, 0)
        except ValueError as error:
            raise MalformedActorSourceError(f"{label} must be an integer") from error
        if not -32768 <= result <= 32767:
            raise MalformedActorSourceError(f"{label} exceeds int16")
        return result

    def _register(self, context: _Context) -> None:
        self.node = context

    def _scale_coordinate(self, value: int, scale_q16: int) -> int:
        product = value * scale_q16
        scaled = ((product + 32768) // 65536 if product >= 0
                  else -((-product + 32768) // 65536))
        if not -32768 <= scaled <= 32767:
            raise MalformedActorSourceError("scaled joint translation exceeds int16")
        return scaled

    def _bind(self, layer: str, name: str, context: _Context) -> None:
        if layer not in self._LAYERS:
            raise UnsupportedActorSourceError(f"unsupported material layer: {layer}")
        part = {
            "branch_ordinal": len(self.parts),
            "joint_ordinal": context.joint,
            "display_list": name,
            "layer": layer,
            "opacity": self._LAYERS[layer],
            "matrix": context.matrix,
        }
        self.parts.append(part)
        switch = context.switch if context.switch is not None else self.scope.switch
        billboard = context.billboard if context.billboard is not None else self.scope.billboard
        if switch is not None:
            values = self.switches[switch]["variant_display_lists"]
            values.append(name)
        if billboard is not None:
            values = self.billboards[billboard]["display_lists"]
            values.append(name)

    def walk(self, name: str, call_stack: tuple[str, ...] = ()) -> None:
        if name in call_stack:
            raise ActorSourceSelectionError(f"recursive GeoLayout: {' -> '.join(call_stack + (name,))}")
        tokens = self.layouts.get(name)
        if tokens is None:
            raise ActorSourceSelectionError(f"missing reached GeoLayout: {name}")
        for macro, args in tokens:
            ordinal = self.node_ordinal
            self.node_ordinal += 1
            if macro in ("GEO_OPEN_NODE", "GEO_CLOSE_NODE", "GEO_RETURN",
                         "GEO_END", "GEO_NODE_START") and args.strip():
                raise MalformedActorSourceError(f"malformed {macro}")
            if macro == "GEO_OPEN_NODE":
                self.stack.append(self.scope)
                self.scope = self.node
                continue
            if macro == "GEO_CLOSE_NODE":
                if not self.stack:
                    raise MalformedActorSourceError(f"unbalanced GEO_CLOSE_NODE in {name}")
                self.node, self.scope = self.scope, self.stack.pop()
                continue
            if macro in ("GEO_RETURN", "GEO_END"):
                return
            target = _branch_arg(macro, args)
            if target is not None:
                self.walk(target, call_stack + (name,))
                if macro == "GEO_BRANCH" and _arguments(args)[0] == "0":
                    return
                continue
            if macro == "GEO_NODE_START":
                self._register(self.scope)
                continue
            if macro == "GEO_SHADOW":
                if not self.allow_saturn_reductions:
                    raise UnsupportedActorSourceError(
                        "unsupported GeoLayout node: GEO_SHADOW")
                fields = _arguments(args)
                if (len(fields) != 3 or
                        re.fullmatch(r"SHADOW_[A-Za-z0-9_]+", fields[0]) is None):
                    raise MalformedActorSourceError("malformed GEO_SHADOW")
                try:
                    solidity = int(fields[1], 0)
                    size = int(fields[2], 0)
                except ValueError as error:
                    raise MalformedActorSourceError(
                        "GEO_SHADOW values must be integers") from error
                if not 0 <= solidity <= 255 or not 0 < size <= 32767:
                    raise MalformedActorSourceError("GEO_SHADOW values are out of range")
                self.reductions.append({
                    "node": "GEO_SHADOW", "mode": "source-shadow-path",
                    "type": fields[0], "solidity": solidity, "size": size,
                })
                self._register(self.scope)
                continue
            if macro == "GEO_SCALE":
                if not self.allow_saturn_reductions:
                    raise UnsupportedActorSourceError(
                        "unsupported GeoLayout node: GEO_SCALE")
                fields = _arguments(args)
                if len(fields) != 2:
                    raise MalformedActorSourceError("malformed GEO_SCALE")
                try:
                    parameter = int(fields[0], 0)
                    local_scale = int(fields[1], 0)
                except ValueError as error:
                    raise MalformedActorSourceError(
                        "GEO_SCALE values must be integers") from error
                if parameter != 0 or not 0 < local_scale <= 65536:
                    raise UnsupportedActorSourceError(
                        "unsupported GEO_SCALE parameters")
                combined = (self.scope.scale_q16 * local_scale + 32768) // 65536
                if not 0 < combined <= 65536:
                    raise UnsupportedActorSourceError(
                        "unsupported cumulative GEO_SCALE")
                context = _Context(
                    self.scope.joint,
                    matrix_mul(scale_matrix(local_scale / 65536.0),
                               self.scope.matrix),
                    self.scope.switch, self.scope.billboard, combined)
                self.reductions.append({
                    "node": "GEO_SCALE", "mode": "static-bake",
                    "scale_q16": local_scale,
                })
                self._register(context)
                continue
            if macro == "GEO_ANIMATED_PART":
                fields = _arguments(args)
                if len(fields) != 5:
                    raise MalformedActorSourceError("malformed GEO_ANIMATED_PART")
                parent = self.scope.joint if self.scope.joint is not None else -1
                translation = tuple(self._scale_coordinate(
                    self._integer(fields[axis], "joint translation"),
                    self.scope.scale_q16) for axis in range(1, 4))
                switch = self.scope.switch
                prior_switch_joint = (None if switch is None else
                                      self.switch_joints.get(switch))
                if prior_switch_joint is None:
                    joint = Joint(len(self.joints), parent, translation, ordinal)
                    if joint.joint_ordinal and parent < 0:
                        raise ActorJointOwnershipError("multiple root animation joints")
                    self.joints.append(joint)
                    if switch is not None:
                        self.switch_joints[switch] = (
                            joint.joint_ordinal, parent, translation)
                else:
                    joint_ordinal, prior_parent, prior_translation = prior_switch_joint
                    if parent != prior_parent or translation != prior_translation:
                        raise UnsupportedActorSourceError(
                            "switch animation alternatives require one shared joint")
                    joint = self.joints[joint_ordinal]
                context = _Context(
                    joint.joint_ordinal, self.scope.matrix,
                    self.scope.switch, self.scope.billboard,
                    self.scope.scale_q16)
                self._register(context)
                binding = _display_list_arg(macro, args)
                if binding is not None:
                    self._bind(*binding, context)
                continue
            if macro == "GEO_DISPLAY_LIST":
                self._register(self.scope)
                binding = _display_list_arg(macro, args)
                if binding is None:
                    raise MalformedActorSourceError("GEO_DISPLAY_LIST cannot bind NULL")
                self._bind(*binding, self.scope)
                continue
            if macro == "GEO_SWITCH_CASE":
                fields = _arguments(args)
                if len(fields) != 2 or not re.fullmatch(r"[A-Za-z_]\w*", fields[1]):
                    raise MalformedActorSourceError("malformed GEO_SWITCH_CASE")
                count = self._integer(fields[0], "switch case count")
                if count <= 0:
                    raise MalformedActorSourceError("switch case count must be positive")
                switch = len(self.switches)
                self.switches.append({"case_count": count, "callback": fields[1],
                                      "variant_display_lists": []})
                self._register(_Context(self.scope.joint, self.scope.matrix, switch,
                                        self.scope.billboard,
                                        self.scope.scale_q16))
                continue
            if macro == "GEO_BILLBOARD":
                if _arguments(args) not in ([], [""]):
                    raise UnsupportedActorSourceError("unsupported GEO_BILLBOARD parameters")
                billboard = len(self.billboards)
                self.billboards.append({"display_lists": []})
                self._register(_Context(self.scope.joint, self.scope.matrix,
                                        self.scope.switch, billboard,
                                        self.scope.scale_q16))
                continue
            if macro in self._UNSUPPORTED:
                raise UnsupportedActorSourceError(f"unsupported GeoLayout node: {macro}")
            raise UnsupportedActorSourceError(f"unknown GeoLayout node: {macro}")

    def finish(self) -> tuple[tuple[Joint, ...], list[dict[str, object]]]:
        if self.stack:
            raise MalformedActorSourceError("GeoLayout has unclosed nodes")
        if not self.parts:
            raise ActorSourceSelectionError("selected GeoLayout emits no display lists")
        if not self.joints:
            self.joints.append(Joint(0, -1, (0, 0, 0), 0))
        for part in self.parts:
            if part["joint_ordinal"] is None:
                if len(self.joints) != 1:
                    raise ActorJointOwnershipError("display list has no exact joint owner")
                part["joint_ordinal"] = 0
        for item in self.switches:
            if len(item["variant_display_lists"]) != item["case_count"]:
                raise ActorSourceSelectionError("switch variants do not match declared case count")
        return tuple(self.joints), self.parts


class _Fast3DCompiler:
    _MATERIAL_COMMANDS = {
        "gsDPSetTextureImage", "gsDPLoadTextureBlock", "gsSPTexture",
        "gsDPSetCombineMode", "gsSPSetGeometryMode", "gsSPClearGeometryMode",
        "gsDPSetEnvColor", "gsDPSetAlphaCompare", "gsDPLoadSync",
        "gsDPLoadBlock", "gsDPSetTile", "gsDPTileSync", "gsDPSetTileSize",
    }

    def __init__(self, index: _SourceIndex,
                 lists: Mapping[str, list[tuple[str, str]]],
                 list_paths: Mapping[str, str], family_ordinal: int,
                 model_id: int) -> None:
        self.index = index
        self.lists = lists
        self.list_paths = list_paths
        self.family_ordinal = family_ordinal
        self.model_id = model_id
        self.allow_v2 = (family_ordinal, model_id) in BOB_DIRECT_TEXTURED_KEYS
        self.cache: list[tuple[tuple[int, ...], str, str, int] | None] = [None] * 32
        self.light: str | None = None
        self.light_rgb: tuple[int, int, int] | None = None
        self.ambient_light: str | None = None
        self.materials: list[dict[str, object]] = []
        self.material_ids: dict[tuple[object, ...], int] = {}
        self.triangles: list[dict[str, object]] = []
        self.transfers: list[tuple[str, str, str, str]] = []
        self.material_trace: list[tuple[object, ...]] = []
        self.combine_mode: tuple[str, ...] = ()
        self.geometry_mode = {"G_LIGHTING", "G_SHADING_SMOOTH", "G_CULL_BACK"}
        self.env_color: tuple[int, int, int, int] | None = None
        self.alpha_compare: str | None = None
        self.texture_enabled = False
        self.texture_scale_s = 0
        self.texture_scale_t = 0
        self.texture_level = 0
        self.texture_tile = 0
        self.texture_image: dict[str, object] | None = None
        self.tiles: dict[int, dict[str, object]] = {}
        self.load_state: tuple[int, ...] | None = None
        self.load_complete = False

    def _state_snapshot(self) -> tuple[object, ...]:
        tiles = tuple((tile_id, tuple(sorted(tile.items())))
                      for tile_id, tile in sorted(self.tiles.items()))
        image = None if self.texture_image is None else tuple(
            sorted(self.texture_image.items()))
        return (
            self.light, self.light_rgb, self.ambient_light, self.combine_mode,
            tuple(sorted(self.geometry_mode)), self.env_color,
            self.alpha_compare, int(self.texture_enabled), self.texture_scale_s,
            self.texture_scale_t, self.texture_level, self.texture_tile, image,
            tiles, self.load_state, int(self.load_complete),
        )

    def _material_command_snapshot(
            self, macro: str, args: str, name: str) -> tuple[object, ...]:
        """Return the evaluated command-local state used by exact admission."""
        fields = _arguments(args)
        if macro in ("gsDPLoadSync", "gsDPTileSync"):
            return ()
        if macro == "gsDPSetTextureImage":
            return tuple(sorted(self.texture_image.items()))  # type: ignore[union-attr]
        if macro == "gsDPLoadTextureBlock":
            return (
                tuple(sorted(self.texture_image.items())),  # type: ignore[union-attr]
                tuple(sorted(self.tiles[0].items())), self.load_state,
            )
        if macro == "gsSPTexture":
            return (
                self.texture_scale_s, self.texture_scale_t, self.texture_level,
                self.texture_tile, int(self.texture_enabled),
            )
        if macro == "gsDPSetCombineMode":
            return self.combine_mode
        if macro in ("gsSPSetGeometryMode", "gsSPClearGeometryMode"):
            return tuple(sorted(_geometry_modes(fields[0], name)))
        if macro == "gsDPSetEnvColor":
            return self.env_color  # type: ignore[return-value]
        if macro == "gsDPSetAlphaCompare":
            return (self.alpha_compare,)
        if macro == "gsDPLoadBlock":
            return self.load_state  # type: ignore[return-value]
        if macro == "gsDPSetTile":
            tile_id = _fast3d_scalar(fields[4], f"tile state in {name}")
            return (tile_id, tuple(sorted(self.tiles[tile_id].items())))
        if macro == "gsDPSetTileSize":
            tile_id = _fast3d_scalar(fields[0], f"tile size in {name}")
            return (tile_id, tuple(sorted(self.tiles[tile_id].items())))
        raise UnsupportedActorSourceError(
            f"unknown Fast3D material trace state: {macro}")

    def _trace_material_state(
            self, path: str, name: str, macro: str,
            command_state: tuple[object, ...]) -> None:
        self.material_trace.append(
            (path, name, macro, command_state, self._state_snapshot()))

    def _texture_signature(self, part: dict[str, object], path: str,
                           name: str) -> tuple[MaterialSignatureV2,
                                               dict[str, object] | None,
                                               dict[str, object] | None]:
        if not self.texture_enabled:
            signature = MaterialSignatureV2(
                None, None, self.combine_mode, tuple(sorted(self.geometry_mode)),
                (), str(part["layer"]), 0 if part["opacity"] == "opaque" else 1)
            return signature, None, None
        image = self.texture_image
        tile = self.tiles.get(self.texture_tile)
        if image is None:
            raise UnsupportedActorSourceError(f"partial texture image state in {name}")
        if tile is None or "lrs" not in tile or "lrt" not in tile:
            raise UnsupportedActorSourceError(f"partial tile size state in {name}")
        if not self.load_complete or self.load_state is None:
            raise UnsupportedActorSourceError(f"partial texture load state in {name}")
        if not self.combine_mode:
            raise UnsupportedActorSourceError(f"partial combine mode state in {name}")
        if (int(image["fmt"]) != int(tile["fmt"]) or
                int(image["size"]) != int(tile["size"])):
            raise UnsupportedActorSourceError(f"ambiguous texture image/tile state in {name}")
        try:
            source = self.index.texture_source(str(image["symbol"]), path)
        except ActorSourceSelectionError as error:
            raise UnsupportedActorSourceError(
                f"unapproved texture image state/source in {name}: {error}") from error
        uls, ult = int(tile["uls"]), int(tile["ult"])
        lrs, lrt = int(tile["lrs"]), int(tile["lrt"])
        if lrs < uls or lrt < ult or any(value & 3 for value in (uls, ult, lrs, lrt)):
            raise UnsupportedActorSourceError(f"unapproved tile size state in {name}")
        width = (lrs - uls) // 4 + 1
        height = (lrt - ult) // 4 + 1
        tile_state = (
            int(image["fmt"]), int(image["size"]), int(image["width"]),
            self.texture_tile, uls, ult, lrs, lrt, width, height,
            int(tile["mask_s"]), int(tile["mask_t"]),
            int(tile["shift_s"]), int(tile["shift_t"]),
            self.texture_scale_s, self.texture_scale_t,
            int(bool(tile["clamp_s"])), int(bool(tile["clamp_t"])),
            int(bool(tile["mirror_s"])), int(bool(tile["mirror_t"])),
            int(tile["line"]), int(tile["tmem"]), int(tile["palette"]),
            *self.load_state,
        )
        signature = MaterialSignatureV2(
            str(source["path"]), source["sha256"], self.combine_mode,
            tuple(sorted(self.geometry_mode)), tile_state, str(part["layer"]),
            0 if part["opacity"] == "opaque" else 1)
        sample_state = {
            "width": width, "height": height,
            "uls": uls, "ult": ult, "lrs": lrs, "lrt": lrt,
            "mask_s": int(tile["mask_s"]), "mask_t": int(tile["mask_t"]),
            "shift_s": int(tile["shift_s"]), "shift_t": int(tile["shift_t"]),
            "clamp_s": bool(tile["clamp_s"]), "clamp_t": bool(tile["clamp_t"]),
            "mirror_s": bool(tile["mirror_s"]), "mirror_t": bool(tile["mirror_t"]),
            "sp_scale_s": self.texture_scale_s,
            "sp_scale_t": self.texture_scale_t,
        }
        texture = (int(source["width"]), int(source["height"]),
                   source["pixels"], source["sha256"].hex(),
                   int(source["source_bytes"]))
        return signature, sample_state, texture

    def _material(self, part: dict[str, object], path: str,
                  name: str) -> tuple[int, MaterialSignatureV2,
                                      dict[str, object] | None,
                                      dict[str, object] | None]:
        signature, tile, texture = self._texture_signature(part, path, name)
        lit = (signature.texture_path is None or
               signature.combine_mode ==
               ("G_CC_MODULATERGB", "G_CC_MODULATERGB")) and \
            "G_LIGHTING" in self.geometry_mode
        if lit and (self.light is None or self.light_rgb is None):
            raise ActorSourceSelectionError(
                f"display list {part['display_list']} emits geometry without a diffuse light")
        rgb = self.light_rgb if lit else (31, 31, 31)
        light = self.light if lit else None
        key = (rgb, light, signature, self.env_color, self.alpha_compare)
        if key not in self.material_ids:
            material_id = len(self.materials)
            self.material_ids[key] = material_id
            self.materials.append({
                "material_id": material_id, "rgb": list(rgb),
                "light": light, "texture": signature.texture_path,
                "texture_symbol": None if self.texture_image is None else self.texture_image["symbol"],
                "combine_mode": list(signature.combine_mode) or None,
                "cull_back": "G_CULL_BACK" in self.geometry_mode,
                "env_color": None if self.env_color is None else list(self.env_color),
                "alpha_compare": self.alpha_compare, "layer": part["layer"],
                "signature": signature,
            })
        return self.material_ids[key], signature, tile, texture

    def _set_texture_image(self, fields: list[str], name: str) -> None:
        if len(fields) != 4 or re.fullmatch(r"[A-Za-z_]\w*", fields[3]) is None:
            raise MalformedActorSourceError(f"malformed texture image state in {name}")
        fmt = _fast3d_scalar(fields[0], f"texture image format in {name}")
        size = _fast3d_scalar(fields[1], f"texture image size in {name}")
        width = _fast3d_scalar(fields[2], f"texture image width in {name}")
        if fmt not in (0, 3) or size != 2 or width <= 0:
            raise UnsupportedActorSourceError(f"unapproved texture image state in {name}")
        self.texture_image = {"fmt": fmt, "size": size, "width": width,
                              "symbol": fields[3]}
        self.load_state = None
        self.load_complete = False

    def _set_tile(self, fields: list[str], name: str) -> None:
        if len(fields) != 12:
            raise MalformedActorSourceError(f"malformed tile state in {name}")
        values = [_fast3d_scalar(fields[index], f"tile state in {name}")
                  for index in (0, 1, 2, 3, 4, 5, 7, 8, 10, 11)]
        fmt, size, line, tmem, tile_id, palette, mask_t, shift_t, mask_s, shift_s = values
        if fmt not in (0, 3) or size != 2 or tile_id not in (0, 7):
            raise UnsupportedActorSourceError(f"unapproved tile image state in {name}")
        clamp_t, mirror_t = _tile_axis_mode(fields[6], name)
        clamp_s, mirror_s = _tile_axis_mode(fields[9], name)
        self.tiles[tile_id] = {
            "fmt": fmt, "size": size, "line": line, "tmem": tmem,
            "palette": palette, "mask_t": mask_t, "shift_t": shift_t,
            "mask_s": mask_s, "shift_s": shift_s,
            "clamp_t": clamp_t, "mirror_t": mirror_t,
            "clamp_s": clamp_s, "mirror_s": mirror_s,
        }

    def _material_command(self, macro: str, args: str, name: str) -> None:
        fields = _arguments(args)
        if macro in ("gsDPLoadSync", "gsDPTileSync"):
            if args.strip():
                raise MalformedActorSourceError(f"malformed {macro} in {name}")
        elif macro == "gsDPSetTextureImage":
            self._set_texture_image(fields, name)
        elif macro == "gsDPSetCombineMode":
            if len(fields) != 2 or any(re.fullmatch(r"G_CC_[A-Z0-9_]+", item) is None
                                       for item in fields):
                raise MalformedActorSourceError(f"malformed combine mode state in {name}")
            self.combine_mode = tuple(fields)
        elif macro in ("gsSPSetGeometryMode", "gsSPClearGeometryMode"):
            if len(fields) != 1:
                raise MalformedActorSourceError(f"malformed geometry mode state in {name}")
            modes = _geometry_modes(fields[0], name)
            if macro == "gsSPSetGeometryMode":
                self.geometry_mode.update(modes)
            else:
                self.geometry_mode.difference_update(modes)
        elif macro == "gsDPSetEnvColor":
            values = [_fast3d_scalar(item, f"environment color in {name}") for item in fields]
            if len(values) != 4 or any(value < 0 or value > 255 for value in values):
                raise MalformedActorSourceError(f"malformed environment color state in {name}")
            self.env_color = tuple(values)
        elif macro == "gsDPSetAlphaCompare":
            if len(fields) != 1 or re.fullmatch(r"G_AC_[A-Z0-9_]+", fields[0]) is None:
                raise MalformedActorSourceError(f"malformed alpha compare state in {name}")
            self.alpha_compare = fields[0]
        elif macro == "gsSPTexture":
            if len(fields) != 5:
                raise MalformedActorSourceError(f"malformed texture enable state in {name}")
            values = [_fast3d_scalar(item, f"texture enable in {name}") for item in fields]
            if values[2] != 0 or values[3] != 0 or values[4] not in (0, 1):
                raise UnsupportedActorSourceError(f"unapproved texture enable state in {name}")
            self.texture_scale_s, self.texture_scale_t = values[0], values[1]
            self.texture_level, self.texture_tile = values[2], values[3]
            self.texture_enabled = bool(values[4])
        elif macro == "gsDPSetTile":
            self._set_tile(fields, name)
        elif macro == "gsDPSetTileSize":
            if len(fields) != 5:
                raise MalformedActorSourceError(f"malformed tile size state in {name}")
            values = [_fast3d_scalar(item, f"tile size in {name}") for item in fields]
            tile_id = values[0]
            if tile_id not in self.tiles:
                raise UnsupportedActorSourceError(f"partial tile size state in {name}")
            self.tiles[tile_id].update({"uls": values[1], "ult": values[2],
                                        "lrs": values[3], "lrt": values[4]})
        elif macro == "gsDPLoadBlock":
            if len(fields) != 5 or self.texture_image is None:
                raise UnsupportedActorSourceError(f"partial texture load state in {name}")
            tile_id = _fast3d_scalar(fields[0], f"texture load tile in {name}")
            if tile_id != 7 or tile_id not in self.tiles:
                raise UnsupportedActorSourceError(f"unapproved texture load tile state in {name}")
            block = tuple(_fast3d_scalar(field, f"texture load block in {name}")
                          for field in fields[1:4])
            dxt = re.fullmatch(
                r"CALC_DXT\(([^,]+),\s*G_IM_SIZ_16b_BYTES\)", fields[4])
            if dxt is None:
                raise UnsupportedActorSourceError(f"computed texture load state in {name}")
            dxt_width = _fast3d_scalar(dxt.group(1), f"texture load DXT width in {name}")
            load_tile = self.tiles[tile_id]
            self.load_state = (
                0, tile_id, *block, dxt_width,
                int(load_tile["fmt"]), int(load_tile["size"]),
                int(load_tile["line"]), int(load_tile["tmem"]),
                int(load_tile["palette"]), int(load_tile["mask_s"]),
                int(load_tile["mask_t"]), int(load_tile["shift_s"]),
                int(load_tile["shift_t"]), int(bool(load_tile["clamp_s"])),
                int(bool(load_tile["clamp_t"])), int(bool(load_tile["mirror_s"])),
                int(bool(load_tile["mirror_t"])),
            )
            self.load_complete = True
        elif macro == "gsDPLoadTextureBlock":
            if len(fields) != 12 or re.fullmatch(r"[A-Za-z_]\w*", fields[0]) is None:
                raise MalformedActorSourceError(f"malformed texture block state in {name}")
            fmt = _fast3d_scalar(fields[1], f"texture block format in {name}")
            size = _fast3d_scalar(fields[2], f"texture block size in {name}")
            width = _fast3d_scalar(fields[3], f"texture block width in {name}")
            height = _fast3d_scalar(fields[4], f"texture block height in {name}")
            if fmt not in (0, 3) or size != 2 or width <= 0 or height <= 0:
                raise UnsupportedActorSourceError(f"unapproved texture image state in {name}")
            clamp_s, mirror_s = _tile_axis_mode(fields[6], name)
            clamp_t, mirror_t = _tile_axis_mode(fields[7], name)
            mask_s = _fast3d_scalar(fields[8], f"texture block mask in {name}")
            mask_t = _fast3d_scalar(fields[9], f"texture block mask in {name}")
            shift_s = _fast3d_scalar(fields[10], f"texture block shift in {name}")
            shift_t = _fast3d_scalar(fields[11], f"texture block shift in {name}")
            self.texture_image = {"fmt": fmt, "size": size, "width": 1,
                                  "symbol": fields[0]}
            self.tiles[0] = {
                "fmt": fmt, "size": size, "line": width // 4, "tmem": 0,
                "palette": _fast3d_scalar(fields[5], f"texture block palette in {name}"),
                "mask_s": mask_s, "mask_t": mask_t,
                "shift_s": shift_s, "shift_t": shift_t,
                "clamp_s": clamp_s, "clamp_t": clamp_t,
                "mirror_s": mirror_s, "mirror_t": mirror_t,
                "uls": 0, "ult": 0, "lrs": (width - 1) * 4,
                "lrt": (height - 1) * 4,
            }
            self.texture_tile = 0
            self.load_state = (
                1, 7, 0, 0, width * height - 1, width,
                fmt, size, 0, 0, 0, 0, 0, 0, 0,
                1, 1, 0, 0,
            )
            self.load_complete = True
        else:
            raise UnsupportedActorSourceError(f"unknown Fast3D material/list state: {macro}")

    def walk(self, name: str, part: dict[str, object], stack: tuple[str, ...] = ()) -> None:
        if name in stack:
            raise ActorSourceSelectionError(f"recursive display list: {' -> '.join(stack + (name,))}")
        if len(stack) >= 256:
            raise ActorSourceSelectionError(
                f"display-list traversal depth exceeds 256 at {name}")
        body = self.lists.get(name)
        path = self.list_paths.get(name)
        if body is None or path is None:
            raise ActorSourceSelectionError(f"missing reached display list: {name}")
        local_ordinal = 0
        for macro, args in body:
            if macro in self._MATERIAL_COMMANDS:
                if not self.allow_v2:
                    raise UnsupportedActorSourceError(
                        f"S64B v1 cannot represent Fast3D state {macro} in {name}")
                self._material_command(macro, args, name)
                self._trace_material_state(
                    path, name, macro,
                    self._material_command_snapshot(macro, args, name))
            elif macro == "gsSPDisplayList":
                child = re.fullmatch(r"\s*([A-Za-z_]\w*)\s*", args)
                if child is None:
                    raise MalformedActorSourceError(f"malformed gsSPDisplayList in {name}")
                self.transfers.append((path, name, macro, child.group(1)))
                self.walk(child.group(1), part, stack + (name,))
            elif macro == "gsSPBranchList":
                child = re.fullmatch(r"\s*([A-Za-z_]\w*)\s*", args)
                if child is None:
                    raise MalformedActorSourceError(f"malformed gsSPBranchList in {name}")
                self.transfers.append((path, name, macro, child.group(1)))
                self.walk(child.group(1), part, stack + (name,))
                return
            elif macro == "gsSPVertex":
                fields = _arguments(args)
                if len(fields) != 3 or re.fullmatch(r"[A-Za-z_]\w*", fields[0]) is None:
                    raise MalformedActorSourceError(f"malformed gsSPVertex in {name}")
                group = fields[0]
                definition = self.index.resolve_block("Vtx", group, path)
                rows = _vertex_rows(definition.body, f"Vtx {group} in {definition.path}")
                count = _integer_literal(fields[1], f"gsSPVertex count in {name}")
                destination = _integer_literal(fields[2], f"gsSPVertex destination in {name}")
                if count <= 0 or count > len(rows) or destination < 0 or destination + count > 32:
                    raise MalformedActorSourceError(f"invalid Fast3D vertex-cache load in {name}")
                self.cache[destination:destination + count] = [
                    (rows[index], definition.path, group, index)
                    for index in range(count)
                ]
            elif macro == "gsSPLight":
                selected = re.fullmatch(
                    r"\s*&([A-Za-z_]\w*)\.(a|l)\s*,\s*(" +
                    _C_INTEGER.pattern + r")\s*", args)
                if selected is None:
                    raise MalformedActorSourceError(f"malformed gsSPLight in {name}")
                kind = selected.group(2)
                light_index = _integer_literal(selected.group(3),
                                               f"gsSPLight index in {name}")
                if kind == "a" and light_index == 2 and self.allow_v2:
                    self.ambient_light = selected.group(1)
                    self._trace_material_state(
                        path, name, macro,
                        (selected.group(1), kind, light_index))
                    continue
                if kind != "l" or light_index != 1:
                    raise UnsupportedActorSourceError(
                        f"S64B v1 cannot represent ambient/alternate light state in {name}")
                self.light = selected.group(1)
                self.light_rgb = self.index.light_rgb(self.light, path)
                if self.allow_v2:
                    self._trace_material_state(
                        path, name, macro,
                        (selected.group(1), kind, light_index))
            elif macro in ("gsSP1Triangle", "gsSP2Triangles"):
                expected_count = 8 if macro == "gsSP2Triangles" else 4
                values = _integer_fields(args, expected_count, f"{macro} in {name}")
                flags = (values[3], values[7]) if macro == "gsSP2Triangles" else (values[3],)
                if any(flag != 0 for flag in flags):
                    raise UnsupportedActorSourceError(
                        f"unsupported nonzero triangle flag in {name}")
                triples = ((values[0:3], values[4:7]) if macro == "gsSP2Triangles"
                           else (values[0:3],))
                if any(len(triple) != 3 for triple in triples):
                    raise MalformedActorSourceError(f"malformed {macro} in {name}")
                material, signature, tile, texture = self._material(part, path, name)
                for triple in triples:
                    if any(vertex < 0 or vertex >= 32 or self.cache[vertex] is None
                           for vertex in triple):
                        raise MalformedActorSourceError(f"unresolved Fast3D vertex in {name}")
                    rows = [self.cache[vertex] for vertex in triple]
                    local = [list(matrix_apply(part["matrix"], tuple(row[0][0:3])))
                             for row in rows]
                    self.triangles.append({
                        "display_list": name, "list_ordinal": local_ordinal,
                        "material": material, "joint_ordinal": part["joint_ordinal"],
                        "branch_ordinal": part["branch_ordinal"], "local_positions": local,
                        "opacity": 0 if part["opacity"] == "opaque" else 1,
                        "texture": texture,
                        "tile": tile,
                        "uv": [list(row[0][4:6]) for row in rows],
                        "signature": signature,
                    })
                    local_ordinal += 1
            elif macro in ("gsSPMatrix", "gsSPPopMatrix"):
                raise UnsupportedActorSourceError(
                    f"display-list matrix state has no closure joint owner: {name}")
            elif macro == "gsDPPipeSync":
                if args.strip():
                    raise MalformedActorSourceError(f"malformed gsDPPipeSync in {name}")
            elif macro == "gsSPEndDisplayList":
                if args.strip():
                    raise MalformedActorSourceError(f"malformed gsSPEndDisplayList in {name}")
            else:
                raise UnsupportedActorSourceError(f"unknown Fast3D material/list state: {macro}")
            if macro == "gsSPEndDisplayList":
                break


def _neutral_positions(vertices: Sequence[Vertex], joints: Sequence[Joint]) -> list[list[int]]:
    translations: list[tuple[int, int, int]] = []
    for joint in joints:
        parent = (0, 0, 0) if joint.parent_ordinal < 0 else translations[joint.parent_ordinal]
        translations.append(tuple(parent[axis] + joint.translation[axis] for axis in range(3)))
    return [[vertex.local[axis] + translations[vertex.joint_ordinal][axis]
             for axis in range(3)] for vertex in vertices]


def _meshlets(primitives: Sequence[dict[str, object]], positions: Sequence[list[int]],
              opacity: Sequence[int]) -> list[dict[str, object]]:
    output: list[dict[str, object]] = []
    current: list[int] = []
    current_key: tuple[int, int] | None = None

    def flush() -> None:
        nonlocal current
        if not current:
            return
        unique: list[int] = []
        seen: set[int] = set()
        for primitive_index in current:
            for vertex in primitives[primitive_index]["indices"]:
                if vertex not in seen:
                    seen.add(vertex)
                    unique.append(vertex)
        tiers: list[tuple[list[int], list[int]]] = []
        for tier in range(3):
            selected = current if tier < 2 else [index for index in current if index % 8 == 1]
            tier_vertices: list[int] = []
            tier_seen: set[int] = set()
            for primitive_index in selected:
                for vertex in primitives[primitive_index]["indices"]:
                    if vertex not in tier_seen:
                        tier_seen.add(vertex)
                        tier_vertices.append(vertex)
            tiers.append((selected, tier_vertices))
        output.append({
            "material": current_key[0], "opacity": current_key[1],
            "source_ordinal": current[0],
            "bounds": {
                "min": [min(positions[vertex][axis] for vertex in unique) for axis in range(3)],
                "max": [max(positions[vertex][axis] for vertex in unique) for axis in range(3)],
            },
            "tiers": tiers,
        })
        current = []

    for index, primitive in enumerate(primitives):
        key = (int(primitive["material"]), opacity[index])
        if current and (key != current_key or len(current) == 32):
            flush()
        if not current:
            current_key = key
        current.append(index)
    flush()
    if not output:
        raise ActorSourceSelectionError("selected actor emitted no meshlets")
    return output


def _compile_geometry(index: _SourceIndex, geo_path: str, geo_root: str,
                      model_binding: _ModelBinding, family_ordinal: int,
                      model_id: int
                      ) -> tuple[tuple[Joint, ...], tuple[Vertex, ...], Geometry,
                                 dict[str, object], dict[str, object]]:
    layouts: dict[str, list[tuple[str, str]]] = {}
    layout_paths: dict[str, str] = {}
    lists: dict[str, list[tuple[str, str]]] = {}
    list_paths: dict[str, str] = {}
    entry = geo_root
    if model_binding.kind == "geo":
        _collect_layouts(index, geo_root, geo_path, layouts, layout_paths, declared=True)
        for layout_name, tokens in layouts.items():
            preferred = layout_paths[layout_name]
            for macro, args in tokens:
                binding = _display_list_arg(macro, args)
                if binding is not None:
                    _collect_lists(index, binding[1], preferred, lists, list_paths)
    elif model_binding.kind == "display_list" and model_binding.layer is not None:
        entry = "__actor_direct_display_list_root"
        layouts[entry] = [
            ("GEO_DISPLAY_LIST", f"{model_binding.layer}, {geo_root}"),
            ("GEO_END", ""),
        ]
        layout_paths[entry] = geo_path
        _collect_lists(index, geo_root, geo_path, lists, list_paths, declared=True)
    else:
        raise ActorSourceSelectionError("selected model binding kind is incomplete")
    geo = _GeoCompiler(
        layouts, entry,
        allow_saturn_reductions=(family_ordinal, model_id) == (7, 0x00BC))
    geo.walk(entry)
    joints, source_parts = geo.finish()
    try:
        structural_sites = walk_geo_layout(layouts, lists, entry)
    except ValueError as error:
        raise ActorSourceSelectionError(str(error)) from error
    allow_v2 = (family_ordinal, model_id) in BOB_DIRECT_TEXTURED_KEYS
    unsupported = sorted({reason for site in structural_sites for reason in site.reasons
                          if not (allow_v2 and reason == "textured")})
    if unsupported:
        raise UnsupportedActorSourceError(f"unsupported rigid-group source: {unsupported[0]}")

    fast = _Fast3DCompiler(index, lists, list_paths, family_ordinal, model_id)
    for part in source_parts:
        try:
            fast.walk(str(part["display_list"]), part)
        except UnsupportedActorSourceError as error:
            divergence = partial_transfer_divergence_v2(
                family_ordinal, model_id, fast.transfers)
            if divergence is not None:
                raise UnsupportedActorSourceError(
                    f"unapproved BOB {divergence} state/source") from error
            raise
    actual_sites = [(str(item["display_list"]), int(item["list_ordinal"]))
                    for item in fast.triangles]
    expected_sites = [(site.display_list, site.list_ordinal) for site in structural_sites]
    if actual_sites != expected_sites and not allow_v2:
        raise MalformedActorSourceError("Fast3D extraction disagrees with rigid-group walk")

    vertices: list[Vertex] = []
    vertex_ids: dict[tuple[object, ...], int] = {}
    ir_triangles: list[dict[str, object]] = []
    primitive_opacity_by_source: list[int] = []
    for source_ordinal, triangle in enumerate(fast.triangles):
        indices: list[int] = []
        for local in triangle["local_positions"]:
            key = (triangle["joint_ordinal"], triangle["branch_ordinal"], tuple(local))
            if key not in vertex_ids:
                vertex_ids[key] = len(vertices)
                vertices.append(Vertex(tuple(local), int(triangle["joint_ordinal"]),
                                       int(triangle["branch_ordinal"])))
            indices.append(vertex_ids[key])
        ir_triangles.append({"source": source_ordinal,
                             "material": int(triangle["material"]), "indices": indices})
        primitive_opacity_by_source.append(int(triangle["opacity"]))
    if not vertices or not ir_triangles or not fast.materials:
        raise ActorSourceSelectionError("selected actor has incomplete geometry")
    positions = _neutral_positions(vertices, joints)
    mesh_ir = {
        "schema": "sm64-saturn-mesh-ir", "version": 1,
        "name": f"actor_variant_{geo_root}", "positions": positions,
        "materials": [{"id": item["material_id"], "rgb555": item["rgb"]}
                      for item in fast.materials],
        "triangles": ir_triangles,
        "pairing_forbidden_triangles": [index for index, item in enumerate(fast.triangles)
                                        if item["texture"] is not None],
        "vertex_attributes": {}, "validation_poses": [],
        "source": {"geo": geo_path, "display_lists": sorted(set(list_paths.values()))},
    }
    try:
        compiled, primitives, pairing = compile_mesh_ir(mesh_ir)
    except (ValueError, TypeError, OverflowError) as error:
        raise MalformedActorSourceError(f"generic Mesh IR rejected geometry: {error}") from error
    primitive_documents = [{
        "material": primitive.material,
        "indices": list(primitive.vertices),
        "source_triangles": list(compiled["primitives"][index]["source_triangles"]),
    } for index, primitive in enumerate(primitives)]
    primitive_opacity: list[int] = []
    for item in compiled["primitives"]:
        source_triangles = item["source_triangles"]
        values = {primitive_opacity_by_source[int(source)] for source in source_triangles}
        if len(values) != 1:
            raise MalformedActorSourceError("paired primitives cross material opacity")
        primitive_opacity.append(values.pop())
    meshlets = _meshlets(primitive_documents, positions, primitive_opacity)
    parts = [{"branch_ordinal": int(item["branch_ordinal"]),
              "joint_ordinal": int(item["joint_ordinal"]),
              "display_list": str(item["display_list"])} for item in source_parts]
    serialized_primitives = tuple({"material": item["material"],
                                   "indices": item["indices"]}
                                  for item in primitive_documents)
    geometry = Geometry(tuple(parts), tuple(fast.materials), tuple(meshlets),
                        serialized_primitives)
    reported_materials: list[dict[str, object]] = []
    for item in fast.materials:
        document = {key: value for key, value in item.items() if key != "signature"}
        if not allow_v2:
            document.pop("texture_symbol", None)
        else:
            signature = item["signature"]
            document["signature"] = {
                "texture_path": signature.texture_path,
                "texture_sha256": (None if signature.texture_sha256 is None
                                    else signature.texture_sha256.hex()),
                "combine_mode": list(signature.combine_mode),
                "geometry_mode": list(signature.geometry_mode),
                "tile_state": list(signature.tile_state),
                "layer": signature.layer,
                "opacity": signature.opacity,
            }
        reported_materials.append(document)
    metadata = {
        "positions": positions,
        "joints": [{"joint_ordinal": item.joint_ordinal,
                    "parent_ordinal": item.parent_ordinal,
                    "translation": list(item.translation),
                    "node_ordinal": item.node_ordinal,
                    "branch_ordinal": item.branch_ordinal} for item in joints],
        "vertices": [{"local": list(item.local),
                      "joint_ordinal": item.joint_ordinal,
                      "branch_ordinal": item.branch_ordinal} for item in vertices],
        "parts": parts, "materials": reported_materials,
        "primitives": list(serialized_primitives), "meshlets": meshlets,
        "mesh_ir_report": pairing,
        "switches": geo.switches, "billboards": geo.billboards,
        "saturn_reductions": geo.reductions,
        "layers": [{"part_ordinal": int(item["branch_ordinal"]),
                    "layer": str(item["layer"]), "opacity": str(item["opacity"])}
                   for item in source_parts if item["layer"] != "LAYER_OPAQUE"],
    }
    capture = {
        "family_ordinal": family_ordinal,
        "model_id": model_id,
        "materials": tuple(fast.materials),
        "triangles": tuple(fast.triangles),
        "transfers": tuple(fast.transfers),
        "material_trace": tuple(fast.material_trace),
        "final_material_state": fast._state_snapshot(),
        "actual_sites": tuple(actual_sites),
        "expected_sites": tuple(expected_sites),
        "primitives": tuple(primitive_documents),
    }
    return joints, tuple(vertices), geometry, metadata, capture


def _animation_records(index: _SourceIndex, records: Sequence[dict[str, object]],
                       joint_count: int, geo_path: str
                       ) -> tuple[tuple[AnimationRecord, ...], list[dict[str, object]]]:
    tables: dict[str, str] = {}
    for record in records:
        selected = record.get("animation_table", [])
        provenance = record.get("root_provenance")
        animation = provenance.get("animation") if isinstance(provenance, dict) else None
        if not isinstance(selected, list) or not isinstance(animation, dict):
            raise ActorAnimationBindingError("malformed closure animation binding")
        for table in selected:
            if not isinstance(table, str) or table not in animation:
                raise ActorAnimationBindingError(f"missing closure animation binding: {table}")
            path = _normal_path(animation[table])
            prior = tables.get(table)
            if prior is not None and prior != path:
                raise ActorAnimationBindingError(f"conflicting closure animation binding: {table}")
            tables[table] = path
    if not tables:
        digest = index.digest(geo_path).hex()
        channels = (joint_count + 1) * 3
        neutral = AnimationRecord(0, "neutral", "neutral", geo_path, digest,
                                  0, 1, 0, 0, 1, joint_count,
                                  tuple(value for _ in range(channels) for value in (1, 0)),
                                  (0,))
        return (neutral,), []

    symbol_ids: dict[str, int] = {}
    bindings: list[dict[str, object]] = []
    for table in sorted(tables):
        path = tables[table]
        index.require_attested(path, "animation table source")
        try:
            symbols = parse_animation_table_text(path, index.text(path), table)
        except ValueError as error:
            raise ActorAnimationBindingError(str(error)) from error
        ids: list[int] = []
        for symbol in symbols:
            if symbol not in symbol_ids:
                symbol_ids[symbol] = len(symbol_ids)
            ids.append(symbol_ids[symbol])
        bindings.append({"table": table, "source": path, "symbols": list(symbols),
                         "animation_ids": ids})
    by_path: dict[str, dict[str, int]] = {}
    for symbol, animation_id in symbol_ids.items():
        path = index.resolve_animation(symbol)
        by_path.setdefault(path, {})[symbol] = animation_id
    parsed: list[AnimationRecord] = []
    for path in sorted(by_path):
        try:
            source_records = parse_generic_animation_file_text(
                path, index.text(path), by_path[path])
        except ValueError as error:
            raise ActorAnimationBindingError(str(error)) from error
        raw_digest = index.digest(path).hex()
        parsed.extend(replace(item, source_sha256=raw_digest) for item in source_records)
    parsed.sort(key=lambda item: item.animation_id)
    if [item.animation_id for item in parsed] != list(range(len(parsed))):
        raise ActorAnimationBindingError("selected animation IDs are not contiguous")
    if any(item.joint_count != joint_count for item in parsed):
        raise ActorJointOwnershipError("selected animation joint count does not match GeoLayout")
    return tuple(parsed), bindings


def _source_model_id(index: _SourceIndex, path: str, symbol: str) -> int:
    path = _normal_path(path)
    index.require_attested(path, "model ID source")
    clean = _strip_comments(index.text(path), path)
    pattern = re.compile(
        r"^[ \t]*#define[ \t]+" + re.escape(symbol) +
        r"[ \t]+([^\r\n]+)\r?$",
        re.MULTILINE,
    )
    values = pattern.findall(clean)
    if len(values) != 1:
        detail = "missing" if not values else "duplicate"
        raise ActorSourceSelectionError(f"{detail} model ID definition: {symbol}")
    try:
        value = _integer_literal(values[0], f"model ID {symbol}")
    except MalformedActorSourceError as error:
        raise ActorSourceSelectionError(str(error)) from error
    if not 0 <= value <= 0xFFFF:
        raise ActorSourceSelectionError(f"model ID definition exceeds uint16: {symbol}")
    return value


def _levelscript_model_bindings(index: _SourceIndex, path: str) -> list[_ModelBinding]:
    """Return every exact LOAD_MODEL_FROM_GEO/DL binding in one attested source."""
    path = _normal_path(path)
    index.require_attested(path, "model binding source")
    clean = _strip_comments(index.text(path), path)
    pattern = re.compile(r"\b(LOAD_MODEL_FROM_(?:GEO|DL))\b")
    bindings: list[_ModelBinding] = []
    for match in pattern.finditer(clean):
        cursor = match.end()
        whitespace = re.match(r"\s*", clean[cursor:])
        cursor += whitespace.end()
        if cursor == len(clean) or clean[cursor] != "(":
            raise ActorSourceSelectionError(
                f"malformed model binding command in {path}")
        start = cursor + 1
        depth = 1
        cursor += 1
        while cursor < len(clean) and depth:
            if clean[cursor] in "\"'":
                raise ActorSourceSelectionError(
                    f"unsupported literal in model binding command in {path}")
            if clean[cursor] == "(":
                depth += 1
            elif clean[cursor] == ")":
                depth -= 1
            cursor += 1
        if depth:
            raise ActorSourceSelectionError(
                f"unterminated model binding command in {path}")
        fields = _arguments(clean[start:cursor - 1])
        macro = match.group(1)
        expected = 2 if macro == "LOAD_MODEL_FROM_GEO" else 3
        if (len(fields) != expected or
                re.fullmatch(r"MODEL_[A-Z0-9_]+", fields[0]) is None or
                re.fullmatch(r"[A-Za-z_]\w*", fields[1]) is None or
                (expected == 3 and re.fullmatch(r"LAYER_[A-Z0-9_]+", fields[2]) is None)):
            raise ActorSourceSelectionError(
                f"malformed model binding command in {path}")
        layer = fields[2] if expected == 3 else None
        boundary = cursor
        while boundary < len(clean) and clean[boundary] in " \t":
            boundary += 1
        if (boundary < len(clean) and
                clean[boundary] not in ",;\r\n"):
            raise ActorSourceSelectionError(
                f"unexplained token after model binding command in {path}")
        bindings.append(_ModelBinding(
            fields[0], fields[1],
            "geo" if macro == "LOAD_MODEL_FROM_GEO" else "display_list",
            layer,
        ))
    return bindings


def _source_model_binding(index: _SourceIndex, model_source: str,
                          binding_source: str, model: str,
                          geo_root: str) -> _ModelBinding:
    """Require source bytes, not provenance metadata, to bind model to root."""
    model_source = _normal_path(model_source)
    binding_source = _normal_path(binding_source)
    if binding_source == model_source:
        index.require_attested(binding_source, "model binding source")
        pattern = re.compile(
            r"^[ \t]*#define[ \t]+" + re.escape(model) +
            r"[ \t]+" + _C_INTEGER.pattern +
            r"[ \t]*//[ \t]*([A-Za-z_]\w*)[ \t]*$",
            re.MULTILINE,
        )
        matches = [_ModelBinding(model, root, "geo", None)
                   for root in pattern.findall(index.text(binding_source))]
    else:
        matches = [binding for binding in
                   _levelscript_model_bindings(index, binding_source)
                   if binding.model == model]
    if len(matches) != 1:
        detail = "missing" if not matches else "duplicate/conflicting"
        raise ActorSourceSelectionError(
            f"{detail} model binding for {model} in {binding_source}")
    if matches[0].root != geo_root:
        raise ActorSourceSelectionError(
            f"model binding for {model} selects {matches[0].root}, not {geo_root}")
    if (matches[0].kind == "display_list" and
            matches[0].layer not in _REPRESENTABLE_LAYERS):
        raise UnsupportedActorSourceError(
            f"unsupported selected direct-DL material layer in "
            f"{binding_source}: {matches[0].layer}")
    return matches[0]


def _record_selection(index: _SourceIndex, records: Sequence[dict[str, object]],
                      requested_model_id: int) -> tuple[str, str, str,
                                                         list[dict[str, object]],
                                                         _ModelBinding]:
    selected: set[tuple[str, str, str, str, str | None]] = set()
    variants: list[dict[str, object]] = []
    for record in records:
        primary = record.get("model")
        provenance = record.get("root_provenance")
        models = provenance.get("models") if isinstance(provenance, dict) else None
        if not isinstance(primary, str) or not isinstance(models, dict):
            raise ActorSourceSelectionError("closure model provenance is malformed")
        model_variants = record.get("model_variants", [])
        if (not isinstance(model_variants, list) or
                not model_variants or
                any(not isinstance(item, dict) or
                    not isinstance(item.get("model"), str) or
                    not isinstance(item.get("geo_root"), str)
                    for item in model_variants)):
            raise ActorSourceSelectionError("closure model variants are malformed")
        variant_models = [item["model"] for item in model_variants]
        if len(set(variant_models)) != len(variant_models) or primary not in variant_models:
            raise ActorSourceSelectionError("closure model variants do not contain one primary")
        resolved_variants: list[tuple[str, str, dict[str, object], int]] = []
        for item in model_variants:
            model = item["model"]
            geo_root = item["geo_root"]
            binding = models.get(model)
            if not isinstance(binding, dict):
                raise ActorSourceSelectionError(
                    f"closure has no model-variant provenance: {model}")
            if not isinstance(binding.get("source"), str):
                raise ActorSourceSelectionError(
                    f"closure model source is incomplete: {model}")
            value = _source_model_id(index, binding["source"], model)
            resolved_variants.append((model, geo_root, binding, value))

        numeric_matches = [item for item in resolved_variants
                           if item[3] == requested_model_id]
        for model, geo_root, binding, _value in resolved_variants:
            if model == "MODEL_NONE":
                if (geo_root != "none" or binding.get("geo_symbol") != "none" or
                        binding.get("geo_source") is not None):
                    raise ActorSourceSelectionError(
                        "closure non-drawable provenance mismatch: MODEL_NONE")
                if binding.get("binding_source") != binding["source"]:
                    raise ActorSourceSelectionError(
                        "closure non-drawable binding provenance mismatch: MODEL_NONE")
                continue
            if binding.get("geo_symbol") != geo_root:
                raise ActorSourceSelectionError(
                    f"closure model-variant provenance mismatch: {model}")
            for label in ("source", "binding_source", "geo_source"):
                if not isinstance(binding.get(label), str):
                    raise ActorSourceSelectionError(
                        f"closure model {label} is incomplete: {model}")
        if not numeric_matches:
            raise ActorSourceSelectionError(
                f"model ID {requested_model_id} has no closure variant")
        if len(numeric_matches) != 1:
            raise ActorSourceSelectionError(
                f"ambiguous model ID {requested_model_id} in closure variants")
        model, geo_root, binding, _value = numeric_matches[0]
        if model == "MODEL_NONE":
            raise ActorSourceSelectionError(
                "selected model is non-drawable: MODEL_NONE")
        selected_binding = _source_model_binding(
            index, binding["source"], binding["binding_source"],
            model, geo_root)
        selected.add(
            (model, geo_root, _normal_path(binding["geo_source"]),
             selected_binding.kind, selected_binding.layer))
        variants.extend(model_variants)
    if len(selected) != 1:
        raise ActorSourceSelectionError(
            f"model ID {requested_model_id} selects conflicting model/GeoLayout provenance")
    model, symbol, path, binding_kind, binding_layer = selected.pop()
    typed = sorted({(item["model"], item["geo_root"]) for item in variants})
    return (model, symbol, path,
            [{"model": item[0], "geo_root": item[1]} for item in typed],
            _ModelBinding(model, symbol, binding_kind, binding_layer))


def compile_actor_variant(
    root: Path,
    family_ordinal: int,
    model_id: int,
    records: Sequence[dict[str, object]],
) -> CompiledActorVariant:
    """Return one fully validated generic S64B without writing output."""
    if (isinstance(family_ordinal, bool) or not isinstance(family_ordinal, int) or
            not 0 < family_ordinal <= 0xFFFF):
        raise ActorSourceSelectionError("family ordinal must be a nonzero uint16")
    if (isinstance(model_id, bool) or not isinstance(model_id, int) or
            not 0 < model_id <= 0xFFFF):
        raise ActorSourceSelectionError("model ID must be a nonzero uint16")
    try:
        records = tuple(records)
    except TypeError as error:
        raise ActorSourceSelectionError("variant records must be a sequence") from error
    if not records or any(not isinstance(item, dict) for item in records):
        raise ActorSourceSelectionError("variant requires closure records")
    index = _SourceIndex(Path(root), records)
    model, geo_root, geo_path, model_variants, model_binding = _record_selection(
        index, records, model_id)
    for record in records:
        provenance = record["root_provenance"]
        behavior = provenance.get("behavior")
        binding = provenance["models"][model]
        if not isinstance(behavior, dict) or not isinstance(behavior.get("source"), str):
            raise ActorSourceSelectionError("closure behavior binding is incomplete")
        index.require_attested(behavior["source"], "behavior binding source")
        for label in ("source", "binding_source", "geo_source"):
            if not isinstance(binding.get(label), str):
                raise ActorSourceSelectionError(f"closure model {label} is incomplete")
            index.require_attested(binding[label], f"model {label}")

    joints, vertices, geometry, geometry_report, material_capture = _compile_geometry(
        index, geo_path, geo_root, model_binding, family_ordinal, model_id)
    animations, animation_bindings = _animation_records(
        index, records, len(joints), geo_path)
    sources = index.source_records()
    try:
        digest = source_identity(family_ordinal, model_id, sources)
    except (ValueError, TypeError, OverflowError) as error:
        raise ActorSourceSelectionError(f"invalid variant source identity: {error}") from error
    if int.from_bytes(digest[:4], "big") == 0:
        raise ActorSourceSelectionError("variant source identity has a zero bank ID")
    live_counts = [record.get("maximum_live_instances") for record in records]
    if any(isinstance(value, bool) or not isinstance(value, int) or
           not 0 < value <= 0xFFFF for value in live_counts):
        raise ActorSourceSelectionError(
            "variant maximum live instances must be nonzero uint16 values")
    maximum_live = sum(live_counts)
    if not 0 < maximum_live <= 0xFFFF:
        raise ActorSourceSelectionError("variant maximum live instances exceeds uint16")
    try:
        core_payload, core_packed = pack_actor_bank(
            family_id=family_ordinal, model_id=model_id, max_instances=maximum_live,
            source_digest=digest, joints=joints, animations=animations,
            vertices=vertices, geometry=geometry)
    except (ValueError, TypeError, OverflowError, struct.error) as error:
        raise MalformedActorSourceError(f"S64B packing rejected variant: {error}") from error
    payload = core_payload
    packed = core_packed
    material_report: dict[str, object] | None = None
    if (family_ordinal, model_id) in BOB_DIRECT_TEXTURED_KEYS:
        try:
            resources, material_sources, material_report = compile_materials_v2(
                index, material_capture, material_capture["primitives"], sources)
            combined_sources = tuple(sorted(sources + material_sources,
                                            key=lambda item: item.path))
            digest = source_identity_v2(
                family_ordinal, model_id, combined_sources,
                resources.bake_policy_id, material_report["policy"])
            payload, v2_packed = pack_actor_bank_v2(core_payload, digest, resources)
            packed = {**core_packed, **v2_packed,
                      "lane_bytes": core_packed["lane_bytes"],
                      "max_scratch": core_packed["max_scratch"],
                      "compact_channel_bytes": core_packed["compact_channel_bytes"],
                      "animations": core_packed["animations"]}
            sources = combined_sources
        except ActorMaterialV2Error as error:
            raise UnsupportedActorSourceError(str(error)) from error
        except (ValueError, TypeError, OverflowError, struct.error) as error:
            raise MalformedActorSourceError(
                f"S64B v2 material packing rejected variant: {error}") from error
    try:
        bank = validate_actor_bank(payload)
    except ValueError as error:
        raise MalformedActorSourceError(f"packed S64B failed validation: {error}") from error
    if (bank.family_ordinal != family_ordinal or bank.model_id != model_id or
            bank.source_sha256 != digest or bank.maximum_scratch != packed["max_scratch"]):
        raise MalformedActorSourceError("packed S64B validation identity mismatch")
    source_documents = [{"path": item.path, "sha256": item.sha256.hex()} for item in sources]
    version = bank.version
    report: dict[str, object] = {
        "schema": f"sm64-saturn-actor-bank-v{version}", "version": version, "magic": "S64B",
        "family_id": family_ordinal, "family_ordinal": family_ordinal,
        "model_id": model_id, "joint_count": len(joints),
        "animation_count": len(animations), "meshlet_count": len(geometry.meshlets),
        "primitive_count": len(geometry.primitives), "vertex_count": len(vertices),
        "max_instances": maximum_live, "feature_mask": 31,
        "source_sha256": digest.hex(), "payload_sha256": packed["payload_sha256"],
        "payload_size": packed["payload_size"], "lane_bytes": packed["lane_bytes"],
        "max_scratch": packed["max_scratch"], "compact_channel_bytes": packed["compact_channel_bytes"],
        "sources": source_documents, "animations": packed["animations"],
        "vertices": geometry_report["vertices"], "joints": geometry_report["joints"],
        "parts": geometry_report["parts"], "materials": geometry_report["materials"],
        "meshlets": geometry_report["meshlets"], "primitives": geometry_report["primitives"],
        "geometry": {key: geometry_report[key] for key in (
            "positions", "joints", "vertices", "parts", "materials", "primitives",
            "meshlets", "mesh_ir_report")},
        "selection": {
            "model": model, "geo_root": geo_root, "geo_source": geo_path,
            "model_variants": model_variants,
            "model_binding": {"kind": model_binding.kind,
                              "layer": model_binding.layer},
            "animation_bindings": animation_bindings,
            "switches": geometry_report["switches"],
            "billboards": geometry_report["billboards"],
            "layers": geometry_report["layers"],
        },
        "format": packed["format"],
    }
    if material_report is not None:
        report["material_policy"] = material_report
        report["resources"] = {
            key: packed[key] for key in (
                "material_count", "tile_count", "clut_count",
                "texture_resident_bytes", "clut_resident_bytes",
                "draw_records_per_instance", "texture_commands_per_instance",
                "gouraud_tables_per_instance")
        }
    return CompiledActorVariant(
        family_ordinal, model_id, digest.hex(), str(packed["payload_sha256"]),
        int(packed["lane_bytes"]), int(packed["max_scratch"]), sources, payload, report)
