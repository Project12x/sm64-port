#!/usr/bin/env python3
"""Derive a deterministic, generic scene dependency closure from game source."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter, defaultdict, deque
from functools import lru_cache
from pathlib import Path, PurePosixPath

from scene_package_schema import SCHEMA, validate_scene_closure


class ClosureError(ValueError):
    pass


def _read(root: Path, relative: str) -> str:
    path = root / relative
    if not path.is_file():
        raise ClosureError(f"missing required source {relative}")
    return path.read_text(encoding="utf-8")


def _comment_free(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def _relative(root: Path, path: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def _source(root: Path, relative: str) -> dict[str, str]:
    return {"path": relative, "sha256": hashlib.sha256((root / relative).read_bytes()).hexdigest()}


def _model_geos(root: Path) -> tuple[dict[str, str], str]:
    relative = "include/model_ids.h"
    text = _read(root, relative)
    result: dict[str, str] = {}
    for model, geo in re.findall(r"#define\s+(MODEL_[A-Z0-9_]+)\s+[^\n/]+//\s*([A-Za-z0-9_]+)", text):
        result[model] = geo
    return result, relative


def _behavior_blocks(text: str) -> dict[str, str]:
    starts = list(re.finditer(r"const\s+BehaviorScript\s+(bhv[A-Za-z0-9_]+)\s*\[\]\s*=\s*\{", text))
    blocks: dict[str, str] = {}
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else len(text)
        blocks[match.group(1)] = text[match.start():end]
    return blocks


def _macro_presets(root: Path) -> tuple[dict[str, tuple[str, str]], str]:
    relative = "include/macro_presets.h"
    text = _read(root, relative)
    names_path = root / "include/macro_preset_names.h"
    names: list[str] = []
    if names_path.exists():
        names = re.findall(r"\b(macro_[a-zA-Z0-9_]+)\s*,", names_path.read_text(encoding="utf-8"))
    entries = re.findall(r"\{\s*(bhv[A-Za-z0-9_]+)\s*,\s*(MODEL_[A-Z0-9_]+)\s*,[^}]*\}\s*,?\s*(?://\s*(macro_[a-zA-Z0-9_]+))?", text)
    result: dict[str, tuple[str, str]] = {}
    for index, (behavior, model, comment_name) in enumerate(entries):
        name = comment_name or (names[index] if index < len(names) else "")
        if name:
            result[name] = (model, behavior)
    return result, relative


def _extract_calls(text: str, macro: str) -> list[str]:
    pattern = re.compile(r"\b" + re.escape(macro) + r"\s*\(")
    calls: list[str] = []
    clean = _comment_free(text)
    for match in pattern.finditer(clean):
        depth, cursor = 1, match.end()
        while cursor < len(clean) and depth:
            if clean[cursor] == "(":
                depth += 1
            elif clean[cursor] == ")":
                depth -= 1
            cursor += 1
        if depth == 0:
            calls.append(clean[match.end():cursor - 1])
    return calls


def _arguments(call: str) -> list[str]:
    parts, start, depth = [], 0, 0
    for index, char in enumerate(call):
        if char == "(": depth += 1
        elif char == ")": depth -= 1
        elif char == "," and depth == 0:
            parts.append(call[start:index].strip()); start = index + 1
    parts.append(call[start:].strip())
    return parts


def _object_roots(level_text: str, macro_text: str, presets: dict[str, tuple[str, str]]) -> list[tuple[str, str, str, int]]:
    roots: list[tuple[str, str, str, int]] = []
    for command in ("OBJECT", "OBJECT_WITH_ACTS"):
        for call in _extract_calls(level_text, command):
            args = _arguments(call)
            if len(args) < 2: continue
            model = next((arg for arg in args if re.fullmatch(r"MODEL_[A-Z0-9_]+", arg)), None)
            behavior = next((arg for arg in reversed(args) if re.fullmatch(r"bhv[A-Za-z0-9_]+", arg)), None)
            if model and behavior:
                act = next((arg for arg in args if "ACT_" in arg or arg == "ALL_ACTS"), "ALL_ACTS")
                roots.append((model, behavior, act, 1))
    for command in ("MACRO_OBJECT", "MACRO_OBJECT_WITH_BEH_PARAM"):
        for call in _extract_calls(macro_text, command):
            args = _arguments(call)
            if args and args[0] in presets:
                model, behavior = presets[args[0]]
                roots.append((model, behavior, "ALL_ACTS", 1))
    return roots


def _load_model_from_geo(root: Path, level_text: str, model_geos: dict[str, str]) -> set[str]:
    sources: set[str] = set()
    for call in _extract_calls(level_text, "LOAD_MODEL_FROM_GEO"):
        args = _arguments(call)
        if len(args) != 2 or not re.fullmatch(r"MODEL_[A-Z0-9_]+", args[0]):
            raise ClosureError("malformed LOAD_MODEL_FROM_GEO")
        model_geos[args[0]] = args[1]
        source = _geo_source(root, args[1])
        if not source:
            raise ClosureError(f"missing geo source for LOAD_MODEL_FROM_GEO {args[0]} {args[1]}")
        sources.add(source)
    return sources


def _area_levelscript(level_text: str, area: int) -> str:
    clean = _comment_free(level_text)
    for match in re.finditer(r"\bAREA\s*\(([^)]*)\)(.*?)\bEND_AREA\s*\(\)", clean, re.S):
        args = _arguments(match.group(1))
        if args and re.fullmatch(r"(?:0x)?%X" % area, args[0], re.I):
            selected = match.group(0)
            for name in re.findall(r"JUMP_LINK\s*\(\s*(script_func_[A-Za-z0-9_]+)\s*\)", selected):
                match = re.search(r"static\s+const\s+LevelScript\s+" + re.escape(name) + r"\s*\[\]\s*=\s*\{", level_text)
                if match:
                    depth, cursor = 1, match.end()
                    while cursor < len(level_text) and depth:
                        depth += (level_text[cursor] == "{") - (level_text[cursor] == "}"); cursor += 1
                    selected += level_text[match.start():cursor]
            return selected
    raise ClosureError(f"AREA {area} not found")


def _geo_source(root: Path, geo_root: str) -> str | None:
    if geo_root == "none": return None
    for path in sorted(root.glob("actors/**/geo.inc.c")) + sorted(root.glob("levels/**/geo*.c")):
        if re.search(r"\b" + re.escape(geo_root) + r"\b", path.read_text(encoding="utf-8", errors="ignore")):
            return _relative(root, path)
    return None


def _features(root: Path, geo_source: str | None) -> list[str]:
    if not geo_source: return []
    text = _read(root, geo_source)
    feature_map = {"GEO_ANIMATED_PART": "animated", "GEO_BILLBOARD": "billboard", "GEO_SHADOW": "shadow", "LAYER_ALPHA": "alpha", "LAYER_TRANSPARENT": "transparent"}
    return sorted(value for token, value in feature_map.items() if token in text)


def _normalized_expression(text: str) -> str:
    return re.sub(r"\s+", " ", _comment_free(text)).strip().rstrip(";")


def _source_region(text: str, symbol: str) -> str:
    if symbol == "__file__":
        return text
    body = _function_body(text, symbol)
    if body:
        return body
    match = re.search(r"\b" + re.escape(symbol) + r"\b[^=;]*=\s*\{", text)
    if not match:
        return ""
    start = text.find("{", match.start())
    depth, cursor = 1, start + 1
    while cursor < len(text) and depth:
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    return text[match.start():cursor]


def _attested_capacity(capacity: dict, source_text: str) -> int:
    if set(capacity) - {"source", "location", "expression", "kind", "argument"} or not {"location", "expression", "kind"} <= set(capacity):
        raise ClosureError("manual rule capacity must name exact expression and location")
    region = _source_region(source_text, capacity["location"])
    expression = _normalized_expression(capacity["expression"])
    normalized_region = _normalized_expression(region)
    if not region or expression not in normalized_region:
        raise ClosureError(f"capacity expression not found at {capacity['location']}: {capacity['expression']}")
    kind = capacity["kind"]
    if kind == "single":
        return 1
    if kind == "occurrences":
        return normalized_region.count(expression)
    if kind in {"exclusive_loop", "inclusive_loop", "single_plus_inclusive_loop"}:
        match = re.search(r"for\s*\([^=;]+?=\s*(\d+)\s*;[^;]*?(<|<=)\s*(\d+)\s*;", expression)
        if not match:
            raise ClosureError(f"capacity expression is not a supported loop: {capacity['expression']}")
        start, operator, end = int(match.group(1)), match.group(2), int(match.group(3))
        expected_operator = "<" if kind == "exclusive_loop" else "<="
        if operator != expected_operator or end < start:
            raise ClosureError(f"capacity expression is not a supported {kind}: {capacity['expression']}")
        return end - start + (operator == "<=") + (kind == "single_plus_inclusive_loop")
    if kind == "literal":
        literals = re.findall(r"(?<![A-Za-z0-9_])([1-9][0-9]*)(?![A-Za-z0-9_])", expression)
        if len(literals) != 1:
            raise ClosureError(f"capacity expression must contain one positive literal: {capacity['expression']}")
        return int(literals[0])
    if kind == "count_argument":
        calls = _extract_calls(expression + ")" if expression.count("(") > expression.count(")") else expression, expression.split("(", 1)[0].split()[-1])
        if not calls:
            match = re.search(r"[A-Za-z_][A-Za-z0-9_]*\s*\((.*)\)", expression)
            calls = [match.group(1)] if match else []
        argument = capacity.get("argument")
        if len(calls) != 1 or not isinstance(argument, int):
            raise ClosureError("count_argument capacity requires one call and an argument index")
        args = _arguments(calls[0])
        if argument >= len(args) or not re.fullmatch(r"[1-9][0-9]*", args[argument]):
            raise ClosureError("count_argument capacity must cite a positive literal argument")
        return int(args[argument])
    raise ClosureError(f"unsupported capacity kind: {kind}")


def _rules(root: Path, rules_path: Path) -> list[dict]:
    if not rules_path.exists(): return []
    try:
        rules_path.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise ClosureError(f"rule file outside repository: {rules_path}") from error
    payload = json.loads(rules_path.read_text(encoding="utf-8"))
    if set(payload) != {"schema", "rules"} or payload["schema"] != "sm64-saturn-behavior-spawn-rules-v2":
        raise ClosureError("invalid behavior spawn rules schema")
    blocks = _behavior_blocks(_read(root, "data/behavior_data.c"))
    rule_behaviors: set[str] = set()
    rule_edges: set[tuple[str, str, str]] = set()
    def attestation_text(relative: str, label: str) -> str:
        candidate = PurePosixPath(relative)
        if candidate.is_absolute() or ".." in candidate.parts or candidate.as_posix() != relative:
            raise ClosureError(f"manual rule {label} source must be repository-relative: {relative}")
        path = (root / relative).resolve()
        if not path.is_file():
            raise ClosureError(f"manual rule {label} source missing: {relative}")
        return path.read_text(encoding="utf-8")
    for rule in payload["rules"]:
        if set(rule) != {"behavior", "owner", "source", "reason", "children"} or not rule["reason"]:
            raise ClosureError("manual spawn rule must name behavior, owner, exact source, and reason")
        if rule["behavior"] in rule_behaviors:
            raise ClosureError(f"duplicate manual rule behavior: {rule['behavior']}")
        rule_behaviors.add(rule["behavior"])
        source_path = PurePosixPath(rule["source"])
        if source_path.is_absolute() or ".." in source_path.parts or source_path.as_posix() != rule["source"]:
            raise ClosureError(f"manual spawn rule source must be repository-relative: {rule['source']}")
        try:
            source = (root / rule["source"]).resolve()
            source.relative_to(root.resolve())
        except ValueError as error:
            raise ClosureError(f"manual spawn rule source outside repository: {rule['source']}") from error
        if not source.is_file():
            raise ClosureError(f"manual spawn rule source missing: {rule['source']}")
        source_text = source.read_text(encoding="utf-8")
        block = blocks.get(rule["behavior"], "")
        owners = rule["owner"] if isinstance(rule["owner"], list) else [rule["owner"]]
        if not owners or len(set(owners)) != len(owners) or not all(isinstance(owner, str) for owner in owners):
            raise ClosureError(f"manual rule behavior {rule['behavior']} must name unique owners")
        for owner in owners:
            if not re.search(r"CALL_NATIVE\s*\(\s*" + re.escape(owner) + r"\b", block):
                raise ClosureError(f"manual rule behavior {rule['behavior']} does not call owner {owner}")
            if not _function_body(source_text, owner):
                raise ClosureError(f"manual rule source does not define owner {owner}")
        def routed_symbols(target_source: str) -> set[str]:
            return set().union(*(_routed_source_symbols(root, rule["source"], owner, target_source) for owner in owners))
        children = rule["children"]
        seen: set[tuple[str, str]] = set()
        for child in children:
            if set(child) != {"model", "behavior", "maximum_instances", "edge", "capacity"} or int(child["maximum_instances"]) < 1:
                raise ClosureError("manual spawn rule child must name model, behavior, positive count, edge, and capacity")
            edge = (child["model"], child["behavior"])
            if edge in seen:
                raise ClosureError(f"duplicate manual rule edge: {rule['behavior']} {edge[0]} {edge[1]}")
            seen.add(edge)
            edge_attestation = child["edge"]
            allowed_edge_fields = {"source", "location", "expression", "model_location", "model_expression", "model_symbol"}
            if set(edge_attestation) - allowed_edge_fields or not {"location", "expression"} <= set(edge_attestation):
                raise ClosureError("manual rule edge must name exact expression and location")
            edge_source = edge_attestation.get("source", rule["source"])
            edge_text = attestation_text(edge_source, "edge")
            routed_edge_symbols = routed_symbols(edge_source)
            if edge_attestation["location"] not in routed_edge_symbols:
                raise ClosureError(f"manual rule edge location is not reachable from owner {owners}: {edge_attestation['location']}")
            region = _source_region(edge_text, edge_attestation["location"])
            expression = _normalized_expression(edge_attestation["expression"])
            model_expression = _normalized_expression(edge_attestation.get("model_expression", edge_attestation["expression"]))
            model_location = edge_attestation.get("model_location", edge_attestation["location"])
            if model_location not in routed_edge_symbols:
                raise ClosureError(f"manual rule model location is not reachable from owner {owners}: {model_location}")
            model_region = _source_region(edge_text, model_location)
            model_attested = bool(re.search(r"\b" + re.escape(child["model"]) + r"\b", model_expression))
            if not model_attested and child["model"] == "MODEL_NONE" and edge_attestation.get("model_symbol"):
                symbol = edge_attestation["model_symbol"]
                model_attested = bool(re.search(r"\b" + re.escape(symbol) + r"\b", model_expression)
                                      and re.search(r"#define\s+" + re.escape(symbol) + r"\s+0\b", _read(root, "include/object_constants.h")))
            if (not region or expression not in _normalized_expression(region)
                    or not model_region or model_expression not in _normalized_expression(model_region)
                    or not model_attested
                    or not re.search(r"\b" + re.escape(child["behavior"]) + r"\b", expression)):
                raise ClosureError(f"manual rule edge expression does not attest {edge[0]} {edge[1]}")
            capacity_source = child["capacity"].get("source", rule["source"])
            capacity_text = attestation_text(capacity_source, "capacity")
            if child["capacity"]["location"] not in routed_symbols(capacity_source):
                raise ClosureError(f"manual rule capacity location is not reachable from owner {owners}: {child['capacity']['location']}")
            attested_count = _attested_capacity(child["capacity"], capacity_text)
            if attested_count != int(child["maximum_instances"]):
                raise ClosureError(f"capacity expression attests {attested_count}, not {child['maximum_instances']}: {edge[0]} {edge[1]}")
            global_edge = (rule["behavior"], *edge)
            if global_edge in rule_edges:
                raise ClosureError(f"duplicate manual rule edge: {global_edge}")
            rule_edges.add(global_edge)
    return payload["rules"]


def _native_behavior_sources(root: Path) -> dict[str, set[str]]:
    sources: dict[str, set[str]] = defaultdict(set)
    signature = re.compile(r"^\s*(?:static\s+)?(?:void|s32|s16|s8|u32|u16|f32|struct\s+\w+\s*\*)\s+(bhv_[A-Za-z0-9_]+)\s*\(", re.M)
    for path in sorted((root / "src/game/behaviors").glob("*.inc.c")):
        relative = _relative(root, path)
        for function in signature.findall(path.read_text(encoding="utf-8", errors="ignore")):
            sources[function].add(relative)
    return sources


def _edge_kind(model: str, behavior: str) -> str:
    if behavior in {"bhvStar", "bhvYellowCoin", "bhvRedCoin", "bhv1Up", "bhvHidden1up"} or any(token in model for token in ("COIN", "MODEL_STAR", "MODEL_1UP", "CAP")):
        return "reward"
    if "Bomb" in behavior or "BowlingBall" in behavior or "Projectile" in behavior:
        return "projectile"
    if any(token in behavior or token in model for token in ("Smoke", "Sparkle", "Explosion", "Shadow", "Puff", "Particle", "Bubble", "Wave", "Droplet", "Flame", "MODEL_SMOKE", "MODEL_SPARKLES")):
        return "effect"
    return "child"


def _acts(expression: str) -> frozenset[str]:
    tokens = set(re.findall(r"ACT_[1-6]", expression))
    return frozenset(tokens or {f"ACT_{index}" for index in range(1, 7)})


def _function_body(text: str, name: str) -> str:
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^)]*\)\s*\{", text)
    if not match:
        return ""
    depth, cursor = 1, match.end()
    while cursor < len(text) and depth:
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    return text[match.start():cursor]


def _native_spawn_edges(text: str) -> set[tuple[str, str]]:
    edges: set[tuple[str, str]] = set()
    for name in ("spawn_object", "spawn_object_relative", "spawn_object_abs_with_rot", "spawn_object_with_scale", "try_to_spawn_object"):
        for call in _extract_calls(text, name):
            args = _arguments(call)
            model = next((arg for arg in args if re.fullmatch(r"MODEL_[A-Z0-9_]+", arg)), None)
            behavior = next((arg for arg in args if re.fullmatch(r"bhv[A-Za-z0-9_]+", arg)), None)
            if not model and behavior == "bhvOpenableCageDoor" and any("gOpenableGrills" in arg for arg in args):
                model = "MODEL_BOB_BARS_GRILLS"
            if not model and behavior == "bhvChainChompChainPart" and "CHAIN_CHOMP_CHAIN_PART_BP_PIVOT" in args:
                model = "MODEL_NONE"
            if name == "spawn_object" and args == ["o", "a0->model", "a0->behavior"]:
                continue  # Source-attested computed contents are validated by the owning manual rule.
            if model and behavior:
                edges.add((model, behavior))
            else:
                raise ClosureError(f"unrecognized dynamic native spawn form: {name}({', '.join(args)})")
    return edges


def _source_regions(text: str) -> dict[str, str]:
    symbols = set(re.findall(r"^\s*(?:static\s+)?(?:[A-Za-z_]\w*\s+)+(?:\*\s*)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{", text, re.M))
    symbols.update(re.findall(r"^\s*(?:static\s+)?(?:const\s+)?(?:struct\s+\w+\s+|[A-Za-z_]\w*(?:\s+|\s*\*\s*))+([A-Za-z_]\w*)\s*(?:\[[^]]*\])?\s*=\s*\{", text, re.M))
    symbols.update(re.findall(r"^\s*(?:static\s+)?[A-Za-z_]\w*\s*\(\s*\*\s*([A-Za-z_]\w*)\s*\[[^]]*\]\s*\)\s*\([^)]*\)\s*=\s*\{", text, re.M))
    symbols.difference_update({"if", "for", "while", "switch"})
    return {symbol: region for symbol in symbols if (region := _source_region(text, symbol))}


@lru_cache(maxsize=None)
def _file_source_regions(path: str) -> dict[str, str]:
    return _source_regions(Path(path).read_text(encoding="utf-8", errors="ignore"))


def _reachable_native_regions(root: Path, source: str, entry: str) -> list[tuple[str, str]]:
    """Bounded source-defined function/data walk; no unresolved external calls."""
    regions = _file_source_regions(str((root / source).resolve()))
    pending, seen, reachable = [entry], set(), []
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        if len(seen) >= 64:
            raise ClosureError(f"native helper traversal limit exceeded: {entry}")
        seen.add(name)
        region = regions.get(name, "")
        if not region:
            continue
        reachable.append((name, region))
        pending.extend(symbol for symbol in set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\b", region)) if symbol in regions and symbol not in seen)
    return reachable


def _reachable_native_bodies(root: Path, source: str, entry: str) -> list[str]:
    return [region for _, region in _reachable_native_regions(root, source, entry)]


def _routed_source_symbols(root: Path, owner_source: str, owner: str, target_source: str) -> set[str]:
    owner_regions = _reachable_native_regions(root, owner_source, owner)
    target_symbols = _file_source_regions(str((root / target_source).resolve()))
    reached = {symbol for symbol, _ in owner_regions if target_source == owner_source}
    frontier_calls = {
        name
        for _, region in owner_regions
        for name in re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", region)
    }
    def walk(regions: dict[str, str], entries: set[str]) -> tuple[set[str], set[str]]:
        local_reached: set[str] = set()
        calls: set[str] = set()
        pending = [entry for entry in entries if entry in regions]
        while pending:
            symbol = pending.pop()
            if symbol in local_reached:
                continue
            if len(local_reached) >= 128:
                raise ClosureError(f"native helper traversal limit exceeded: {owner}")
            local_reached.add(symbol)
            region = regions[symbol]
            names = set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\b", region))
            pending.extend(name for name in names if name in regions and name not in local_reached)
            calls.update(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", region))
        return local_reached, calls
    target_reached, target_calls = walk(target_symbols, frontier_calls)
    reached.update(target_reached)
    frontier_calls.update(target_calls)
    # Generic object helpers can dispatch back into behavior-owned handlers.
    for bridge in sorted((root / "src/game").glob("*.c")):
        relative = _relative(root, bridge)
        if relative in {owner_source, target_source}:
            continue
        bridge_regions = _file_source_regions(str(bridge.resolve()))
        bridge_reached, bridge_calls = walk(bridge_regions, frontier_calls)
        if not bridge_reached:
            continue
        more_target, _ = walk(target_symbols, bridge_calls)
        reached.update(more_target)
    return reached


def _sound_declarations(root: Path) -> tuple[dict[str, list[str]], str]:
    relative = "include/sounds.h"
    text = _read(root, relative)
    declarations: dict[str, list[str]] = defaultdict(list)
    for match in re.finditer(r"^\s*#define\s+(SOUND_[A-Z0-9_]+)\b([^\n]*(?:\\\r?\n[^\n]*)*)", text, re.M):
        banks = re.findall(r"\bSOUND_ARG_LOAD\s*\(\s*(SOUND_BANK_[A-Z0-9_]+)\b", match.group(2))
        declarations[match.group(1)].extend(banks)
    return declarations, relative


def _behavior_sounds(root: Path, block: str, native_sources: dict[str, set[str]]) -> tuple[list[str], list[str], set[str]]:
    reached_text = [block]
    reached_sources: set[str] = set()
    for native in re.findall(r"CALL_NATIVE\s*\(\s*(bhv_[A-Za-z0-9_]+)", block):
        for source in native_sources.get(native, set()):
            reached_sources.add(source)
            reached_text.extend(region for _, region in _reachable_native_regions(root, source, native))
    sounds = sorted(set(re.findall(r"\bSOUND_[A-Z0-9_]+\b", "\n".join(reached_text))))
    if not sounds:
        return [], [], reached_sources
    declarations, declarations_path = _sound_declarations(root)
    banks: set[str] = set()
    for sound in sounds:
        declared = declarations.get(sound, [])
        if not declared:
            raise ClosureError(f"missing SOUND_ARG_LOAD declaration for {sound}")
        if len(declared) != 1:
            raise ClosureError(f"ambiguous SOUND_ARG_LOAD declaration for {sound}")
        banks.add(declared[0].removeprefix("SOUND_BANK_").lower())
    if sounds:
        reached_sources.add(declarations_path)
    return sounds, sorted(banks), reached_sources


def find_unruled_native_spawn_sites(root: Path, document: dict, rules_path: Path | None = None) -> list[str]:
    root = root.resolve()
    rules = _rules(root, rules_path or root / "tools/saturn/behavior_spawn_rules.json")
    declared = {(rule["behavior"], rule["source"], child["model"], child["behavior"])
                for rule in rules for child in rule["children"]}
    blocks = _behavior_blocks(_read(root, "data/behavior_data.c"))
    native_sources = _native_behavior_sources(root)
    sites: list[str] = []
    for record in document["records"]:
        for native in re.findall(r"CALL_NATIVE\s*\(\s*(bhv_[A-Za-z0-9_]+)", blocks[record["stable_id"]]):
            for source in native_sources.get(native, set()):
                for body in _reachable_native_bodies(root, source, native):
                  for model, child in _native_spawn_edges(body):
                    if (record["stable_id"], source, model, child) not in declared:
                        sites.append(f"{record['stable_id']}:{source}:{model}:{child}")
    return sorted(set(sites))


def collect_scene_closure(root: Path, level: str, area: int, rules_path: Path) -> dict:
    root = root.resolve()
    script_path = f"levels/{level}/script.c"
    macro_path = f"levels/{level}/areas/{area}/macro.inc.c"
    level_text, macro_text = _read(root, script_path), _read(root, macro_path)
    model_geos, model_ids_path = _model_geos(root)
    loaded_geo_sources = _load_model_from_geo(root, level_text, model_geos)
    presets, presets_path = _macro_presets(root)
    behavior_path = "data/behavior_data.c"
    behavior_text = _read(root, behavior_path)
    blocks = _behavior_blocks(behavior_text)
    native_sources = _native_behavior_sources(root)
    roots = _object_roots(_area_levelscript(level_text, area), macro_text, presets)
    manual_rules = _rules(root, rules_path)
    static_edges: dict[str, list[tuple[str, str, int, frozenset[str]]]] = defaultdict(list)
    for behavior, block in blocks.items():
        for call in _extract_calls(block, "SPAWN_CHILD") + _extract_calls(block, "SPAWN_CHILD_WITH_PARAM") + _extract_calls(block, "SPAWN_OBJ"):
            args = _arguments(call)
            model = next((arg for arg in args if re.fullmatch(r"MODEL_[A-Z0-9_]+", arg)), None)
            child = next((arg for arg in args if re.fullmatch(r"bhv[A-Za-z0-9_]+", arg)), None)
            if model and child: static_edges[behavior].append((model, child, 1, frozenset({behavior_path})))
    for rule in manual_rules:
        for child in rule["children"]:
            evidence_sources = {rule["source"], child["edge"].get("source", rule["source"]), child["capacity"].get("source", rule["source"])}
            if child["edge"].get("model_symbol"):
                evidence_sources.add("include/object_constants.h")
            static_edges[rule["behavior"]].append((child["model"], child["behavior"], int(child.get("maximum_instances", 1)), frozenset(evidence_sources)))
    occurrence: dict[str, Counter[str]] = defaultdict(Counter)
    acts: dict[str, set[str]] = defaultdict(set)
    root_names: dict[str, list[str]] = defaultdict(list)
    declared_model: dict[str, str] = {}
    queue = deque()
    for model, behavior, act, count in roots:
        queue.append((model, behavior, _acts(act), count, "level", frozenset()))
    child_map: dict[str, set[str]] = defaultdict(set)
    typed_children: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    used_sources: dict[str, set[str]] = defaultdict(set)
    while queue:
        model, behavior, active_acts, count, source_kind, ancestors = queue.popleft()
        if behavior not in blocks:
            raise ClosureError(f"undeclared behavior {behavior}")
        if model != "MODEL_NONE" and model not in model_geos:
            raise ClosureError(f"no geo root for {model}")
        if behavior not in declared_model:
            declared_model[behavior] = model
        elif declared_model[behavior] == "MODEL_NONE" and model != "MODEL_NONE":
            declared_model[behavior] = model
        for act in active_acts:
            occurrence[behavior][act] += count
        acts[behavior].update(active_acts)
        root_names[behavior].append(source_kind)
        used_sources[behavior].add(behavior_path)
        for native in re.findall(r"CALL_NATIVE\s*\(\s*(bhv_[A-Za-z0-9_]+)", blocks[behavior]):
            used_sources[behavior].update(native_sources.get(native, set()))
        for child_model, child, factor, edge_sources in static_edges.get(behavior, []):
            if child not in blocks:
                raise ClosureError(f"undeclared behavior {child}")
            child_map[behavior].add(child)
            typed_children[behavior][_edge_kind(child_model, child)].add(child)
            used_sources[behavior].update(edge_sources)
            if child in ancestors or child == behavior:
                raise ClosureError(f"behavior spawn cycle: {behavior} -> {child}")
            queue.append((child_model, child, active_acts, count * factor, f"spawn:{behavior}", ancestors | {behavior}))
    records = []
    for behavior in sorted(occurrence):
        model = declared_model[behavior]
        geo_root = "none" if model == "MODEL_NONE" else model_geos[model]
        geo_source = _geo_source(root, geo_root)
        sounds, banks, sound_sources = _behavior_sounds(root, blocks[behavior], native_sources)
        sources = {behavior_path, model_ids_path} | used_sources[behavior] | sound_sources
        if geo_source: sources.add(geo_source)
        block = blocks[behavior]
        animations = sorted(set(re.findall(r"LOAD_ANIMATIONS\s*\(\s*[^,]+,\s*([A-Za-z0-9_]+)", block)))
        source_items = [_source(root, path) for path in sorted(sources)]
        records.append({"stable_id": behavior, "level": level, "area": area, "act_mask": " | ".join(sorted(acts[behavior])), "object_roots": sorted(set(root_names[behavior])), "model": model, "geo_root": geo_root, "behavior_root": behavior, "spawned_children": sorted(child_map[behavior]), "rewards": sorted(typed_children[behavior]["reward"]), "projectiles": sorted(typed_children[behavior]["projectile"]), "effects": sorted(typed_children[behavior]["effect"]), "animation_table": animations, "material_feature_bits": _features(root, geo_source), "maximum_live_instances": max(occurrence[behavior].values()), "music_sequence_ids": [], "sfx_banks": banks, "sfx_ids": sounds, "sources": source_items})
    scene_sources = [script_path, macro_path, presets_path, *loaded_geo_sources]
    if rules_path.is_file():
        try:
            scene_sources.append(_relative(root, rules_path))
        except ValueError:
            pass
    source_paths = set(scene_sources)
    source_paths.update(source["path"] for record in records for source in record["sources"])
    document = {"schema": SCHEMA, "source_root": str(root), "level": level, "area": area, "records": records, "scene_sources": sorted(source_paths - {source["path"] for record in records for source in record["sources"]}), "source_hashes": {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sorted(source_paths)}, "music_sequence_ids": sorted(set(re.findall(r"\bSEQ_[A-Z0-9_]+\b", level_text))), "sfx_banks": sorted({bank for record in records for bank in record["sfx_banks"]}), "sfx_ids": sorted({sfx for record in records for sfx in record["sfx_ids"]})}
    unruled = find_unruled_native_spawn_sites(root, document, rules_path)
    if unruled:
        raise ClosureError("unruled reachable native spawn sites: " + ", ".join(unruled))
    validate_scene_closure(document)
    return document


def write_closure(path: Path, document: dict) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    canonical = dict(document)
    canonical["source_root"] = "."
    payload = (json.dumps(canonical, indent=2, sort_keys=True) + "\n").encode("utf-8")
    path.write_bytes(payload)
    return hashlib.sha256(payload).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--level", required=True)
    parser.add_argument("--area", required=True, type=int)
    parser.add_argument("--rules", type=Path, default=Path(__file__).with_name("behavior_spawn_rules.json"))
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    write_closure(args.output, collect_scene_closure(args.root, args.level, args.area, args.rules))


if __name__ == "__main__":
    main()
