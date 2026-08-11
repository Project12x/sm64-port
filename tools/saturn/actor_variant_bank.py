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

import hashlib
import re
import struct
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Mapping, Sequence

from actor_family_bundle import SourceRecord, _validate_s64b, source_identity
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
)
from saturn_mesh_ir import compile_mesh_ir


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
        self.candidates = tuple(sorted(self.expected))
        self._bytes: dict[str, bytes] = {}
        self._text: dict[str, str] = {}
        self._definitions: dict[tuple[str, str, str], tuple[str, ...]] = {}
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
            re.escape(symbol) + r"\s*\[\]\s*=")
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

    def source_records(self) -> tuple[SourceRecord, ...]:
        return tuple(SourceRecord(path, self.digest(path)) for path in sorted(self.used))


def _arguments(args: str) -> list[str]:
    return [field.strip() for field in args.split(",")]


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
    definition = (index.declared_block("Gfx", name, preferred) if declared
                  else index.resolve_block("Gfx", name, preferred))
    prior = list_paths.get(name)
    if prior is not None:
        if prior != definition.path:
            raise ActorSourceSelectionError(f"ambiguous reached display list: {name}")
        return
    lists[name] = _macro_tokens(
        definition.body, "gs", f"Gfx {name} in {definition.path}")
    terminators = [position for position, (macro, _args) in enumerate(lists[name])
                   if macro == "gsSPEndDisplayList"]
    if terminators != [len(lists[name]) - 1] or lists[name][-1][1].strip():
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


@dataclass(frozen=True)
class _Context:
    joint: int | None
    matrix: tuple[float, ...]
    switch: int | None = None
    billboard: int | None = None


_REPRESENTABLE_LAYERS = {
    "LAYER_OPAQUE": "opaque",
    "LAYER_ALPHA": "alpha",
    "LAYER_TRANSPARENT": "translucent",
}


class _GeoCompiler:
    _LAYERS = _REPRESENTABLE_LAYERS
    _UNSUPPORTED = {
        "GEO_ASM", "GEO_HELD_OBJECT", "GEO_RENDER_RANGE", "GEO_SHADOW",
        "GEO_TRANSLATE_ROTATE", "GEO_ROTATION_NODE", "GEO_SCALE",
    }

    def __init__(self, layouts: Mapping[str, list[tuple[str, str]]], entry: str) -> None:
        self.layouts = layouts
        self.entry = entry
        base = _Context(None, identity_matrix())
        self.scope = base
        self.node = base
        self.stack: list[_Context] = []
        self.node_ordinal = 0
        self.joints: list[Joint] = []
        self.parts: list[dict[str, object]] = []
        self.switches: list[dict[str, object]] = []
        self.billboards: list[dict[str, object]] = []

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
            if macro == "GEO_ANIMATED_PART":
                fields = _arguments(args)
                if len(fields) != 5:
                    raise MalformedActorSourceError("malformed GEO_ANIMATED_PART")
                parent = self.scope.joint if self.scope.joint is not None else -1
                joint = Joint(len(self.joints), parent,
                              tuple(self._integer(fields[axis], "joint translation")
                                    for axis in range(1, 4)), ordinal)
                if joint.joint_ordinal and parent < 0:
                    raise ActorJointOwnershipError("multiple root animation joints")
                self.joints.append(joint)
                context = _Context(joint.joint_ordinal, identity_matrix(),
                                   self.scope.switch, self.scope.billboard)
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
                                        self.scope.billboard))
                continue
            if macro == "GEO_BILLBOARD":
                if _arguments(args) not in ([], [""]):
                    raise UnsupportedActorSourceError("unsupported GEO_BILLBOARD parameters")
                billboard = len(self.billboards)
                self.billboards.append({"display_lists": []})
                self._register(_Context(self.scope.joint, self.scope.matrix,
                                        self.scope.switch, billboard))
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
    _UNREPRESENTABLE = {
        "gsDPSetTextureImage", "gsDPLoadTextureBlock", "gsSPTexture",
        "gsDPSetCombineMode", "gsSPSetGeometryMode", "gsSPClearGeometryMode",
        "gsDPSetEnvColor", "gsDPSetAlphaCompare", "gsDPLoadSync",
        "gsDPLoadBlock", "gsDPSetTile", "gsDPTileSync", "gsDPSetTileSize",
    }

    def __init__(self, index: _SourceIndex,
                 lists: Mapping[str, list[tuple[str, str]]],
                 list_paths: Mapping[str, str]) -> None:
        self.index = index
        self.lists = lists
        self.list_paths = list_paths
        self.cache: list[tuple[tuple[int, ...], str, str, int] | None] = [None] * 32
        self.light: str | None = None
        self.light_rgb: tuple[int, int, int] | None = None
        self.materials: list[dict[str, object]] = []
        self.material_ids: dict[tuple[object, ...], int] = {}
        self.triangles: list[dict[str, object]] = []

    def _material(self, part: dict[str, object]) -> int:
        if self.light is None or self.light_rgb is None:
            raise ActorSourceSelectionError(
                f"display list {part['display_list']} emits geometry without a diffuse light")
        key = (self.light_rgb, self.light, part["layer"])
        if key not in self.material_ids:
            material_id = len(self.materials)
            self.material_ids[key] = material_id
            self.materials.append({
                "material_id": material_id, "rgb": list(self.light_rgb),
                "light": self.light, "texture": None,
                "combine_mode": None, "cull_back": True,
                "env_color": None, "alpha_compare": None, "layer": part["layer"],
            })
        return self.material_ids[key]

    def walk(self, name: str, part: dict[str, object], stack: tuple[str, ...] = ()) -> None:
        if name in stack:
            raise ActorSourceSelectionError(f"recursive display list: {' -> '.join(stack + (name,))}")
        body = self.lists.get(name)
        path = self.list_paths.get(name)
        if body is None or path is None:
            raise ActorSourceSelectionError(f"missing reached display list: {name}")
        local_ordinal = 0
        for macro, args in body:
            if macro in self._UNREPRESENTABLE:
                raise UnsupportedActorSourceError(
                    f"S64B v1 cannot represent Fast3D state {macro} in {name}")
            if macro == "gsSPDisplayList":
                child = re.fullmatch(r"\s*([A-Za-z_]\w*)\s*", args)
                if child is None:
                    raise MalformedActorSourceError(f"malformed gsSPDisplayList in {name}")
                self.walk(child.group(1), part, stack + (name,))
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
                if (selected.group(2) != "l" or
                        _integer_literal(selected.group(3),
                                         f"gsSPLight index in {name}") != 1):
                    raise UnsupportedActorSourceError(
                        f"S64B v1 cannot represent ambient/alternate light state in {name}")
                self.light = selected.group(1)
                self.light_rgb = self.index.light_rgb(self.light, path)
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
                material = self._material(part)
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
                        "texture": None,
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
                      model_binding: _ModelBinding
                      ) -> tuple[tuple[Joint, ...], tuple[Vertex, ...], Geometry,
                                 dict[str, object]]:
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
    geo = _GeoCompiler(layouts, entry)
    geo.walk(entry)
    joints, source_parts = geo.finish()
    try:
        structural_sites = walk_geo_layout(layouts, lists, entry)
    except ValueError as error:
        raise ActorSourceSelectionError(str(error)) from error
    unsupported = sorted({reason for site in structural_sites for reason in site.reasons})
    if unsupported:
        raise UnsupportedActorSourceError(f"unsupported rigid-group source: {unsupported[0]}")

    fast = _Fast3DCompiler(index, lists, list_paths)
    for part in source_parts:
        fast.walk(str(part["display_list"]), part)
    actual_sites = [(str(item["display_list"]), int(item["list_ordinal"]))
                    for item in fast.triangles]
    expected_sites = [(site.display_list, site.list_ordinal) for site in structural_sites]
    if actual_sites != expected_sites:
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
    primitive_documents = [{"material": primitive.material,
                            "indices": list(primitive.vertices)} for primitive in primitives]
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
    geometry = Geometry(tuple(parts), tuple(fast.materials), tuple(meshlets),
                        tuple(primitive_documents))
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
        "parts": parts, "materials": fast.materials,
        "primitives": primitive_documents, "meshlets": meshlets,
        "mesh_ir_report": pairing,
        "switches": geo.switches, "billboards": geo.billboards,
        "layers": [{"part_ordinal": int(item["branch_ordinal"]),
                    "layer": str(item["layer"]), "opacity": str(item["opacity"])}
                   for item in source_parts if item["layer"] != "LAYER_OPAQUE"],
    }
    return joints, tuple(vertices), geometry, metadata


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
        r"^[ \t]*#define[ \t]+" + re.escape(symbol) + r"[ \t]+([^\r\n]+)$",
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
        record_matches: list[tuple[str, str, str, str, str | None]] = []
        for item in model_variants:
            model = item["model"]
            geo_root = item["geo_root"]
            binding = models.get(model)
            if not isinstance(binding, dict):
                raise ActorSourceSelectionError(
                    f"closure has no model-variant provenance: {model}")
            if binding.get("geo_symbol") != geo_root:
                raise ActorSourceSelectionError(
                    f"closure model-variant provenance mismatch: {model}")
            for label in ("source", "binding_source", "geo_source"):
                if not isinstance(binding.get(label), str):
                    raise ActorSourceSelectionError(
                        f"closure model {label} is incomplete: {model}")
            value = _source_model_id(index, binding["source"], model)
            if value == requested_model_id:
                selected_binding = _source_model_binding(
                    index, binding["source"], binding["binding_source"],
                    model, geo_root)
                record_matches.append(
                    (model, geo_root, _normal_path(binding["geo_source"]),
                     selected_binding.kind, selected_binding.layer))
        if not record_matches:
            raise ActorSourceSelectionError(
                f"model ID {requested_model_id} has no closure variant")
        if len(record_matches) != 1:
            raise ActorSourceSelectionError(
                f"ambiguous model ID {requested_model_id} in closure variants")
        selected.add(record_matches[0])
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

    joints, vertices, geometry, geometry_report = _compile_geometry(
        index, geo_path, geo_root, model_binding)
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
        payload, packed = pack_actor_bank(
            family_id=family_ordinal, model_id=model_id, max_instances=maximum_live,
            source_digest=digest, joints=joints, animations=animations,
            vertices=vertices, geometry=geometry)
    except (ValueError, TypeError, OverflowError, struct.error) as error:
        raise MalformedActorSourceError(f"S64B packing rejected variant: {error}") from error
    try:
        bank = _validate_s64b(payload)
    except ValueError as error:
        raise MalformedActorSourceError(f"packed S64B failed validation: {error}") from error
    if (bank.family_ordinal != family_ordinal or bank.model_id != model_id or
            bank.source_sha256 != digest or bank.maximum_scratch != packed["max_scratch"]):
        raise MalformedActorSourceError("packed S64B validation identity mismatch")
    source_documents = [{"path": item.path, "sha256": item.sha256.hex()} for item in sources]
    report: dict[str, object] = {
        "schema": "sm64-saturn-actor-bank-v1", "version": 1, "magic": "S64B",
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
    return CompiledActorVariant(
        family_ordinal, model_id, digest.hex(), str(packed["payload_sha256"]),
        int(packed["lane_bytes"]), int(packed["max_scratch"]), sources, payload, report)
