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
    return [call for call, _ in _extract_call_sites(text, macro)]


def _extract_call_sites(text: str, macro: str) -> list[tuple[str, int]]:
    pattern = re.compile(r"\b" + re.escape(macro) + r"\s*\(")
    calls: list[tuple[str, int]] = []
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
            calls.append((clean[match.end():cursor - 1], match.start()))
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


def _load_model_roots(root: Path, level_text: str, model_geos: dict[str, str]) -> set[str]:
    sources: set[str] = set()
    for macro, root_index in (("LOAD_MODEL_FROM_GEO", 1), ("LOAD_MODEL_FROM_DL", 1)):
        for call in _extract_calls(level_text, macro):
            args = _arguments(call)
            if len(args) <= root_index or not re.fullmatch(r"MODEL_[A-Z0-9_]+", args[0]):
                raise ClosureError(f"malformed {macro}")
            model_geos[args[0]] = args[root_index]
            source = _asset_root_source(root, args[root_index])
            if not source:
                raise ClosureError(f"unresolved model/geo root {args[0]} {args[root_index]}")
            sources.add(source)
    return sources


def _model_binding_source(root: Path, model: str, geo: str, levelscript_sources: set[str], model_ids_path: str) -> str:
    if model == "MODEL_NONE":
        return model_ids_path
    loaded = []
    for source in sorted(levelscript_sources):
        text = _read(root, source)
        for macro in ("LOAD_MODEL_FROM_GEO", "LOAD_MODEL_FROM_DL"):
            for call in _extract_calls(text, macro):
                args = _arguments(call)
                if len(args) >= 2 and args[0] == model and args[1] == geo:
                    loaded.append(source)
    if loaded:
        return sorted(set(loaded))[0]
    model_text = _read(root, model_ids_path)
    if re.search(r"#define\s+" + re.escape(model) + r"\b[^\n]*//\s*" + re.escape(geo) + r"\b", model_text):
        return model_ids_path
    raise ClosureError(f"unresolved model/geo binding provenance {model} {geo}")


def _levelscript_blocks(text: str) -> dict[str, str]:
    blocks: dict[str, str] = {}
    for match in re.finditer(r"(?:static\s+)?const\s+LevelScript\s+([A-Za-z0-9_]+)\s*\[\]\s*=\s*\{", text):
        depth, cursor = 1, match.end()
        while cursor < len(text) and depth:
            depth += (text[cursor] == "{") - (text[cursor] == "}")
            cursor += 1
        if depth:
            raise ClosureError(f"unterminated LevelScript {match.group(1)}")
        blocks[match.group(1)] = text[match.end():cursor - 1]
    return blocks


def _expand_levelscript(name: str, blocks: dict[str, tuple[str, str]], seen: frozenset[str] = frozenset()) -> tuple[str, set[str]]:
    if name in seen:
        raise ClosureError(f"LevelScript JUMP_LINK cycle: {name}")
    if name not in blocks:
        raise ClosureError(f"unresolved LevelScript JUMP_LINK: {name}")
    source, body = blocks[name]
    sources = {source}

    def expand(match: re.Match[str]) -> str:
        child = match.group(1)
        expanded, child_sources = _expand_levelscript(child, blocks, seen | {name})
        sources.update(child_sources)
        return expanded

    text = re.sub(r"\bJUMP_LINK\s*\(\s*([A-Za-z0-9_]+)\s*\)", expand, _comment_free(body))
    return text, sources


def _scoped_levelscript(root: Path, level: str, level_text: str, area: int) -> tuple[str, str, set[str]]:
    script_path = f"levels/{level}/script.c"
    blocks: dict[str, tuple[str, str]] = {
        name: (script_path, body) for name, body in _levelscript_blocks(level_text).items()
    }
    global_path = "levels/scripts.c"
    if (root / global_path).is_file():
        blocks.update({name: (global_path, body) for name, body in _levelscript_blocks(_read(root, global_path)).items()})
    entry_name = f"level_{level}_entry"
    if entry_name in blocks:
        clean, area_sources = _expand_levelscript(entry_name, blocks)
    else:
        clean, area_sources = _comment_free(level_text), {script_path}
    clean = _comment_free(clean)
    selected_match = None
    for match in re.finditer(r"\bAREA\s*\(([^)]*)\)(.*?)\bEND_AREA\s*\(\)", clean, re.S):
        args = _arguments(match.group(1))
        if args and re.fullmatch(r"(?:0x)?%X" % area, args[0], re.I):
            selected_match = match
            break
    if selected_match is None:
        raise ClosureError(f"AREA {area} not found")
    area_text = selected_match.group(0)
    for name in re.findall(r"\bJUMP_LINK\s*\(\s*([A-Za-z0-9_]+)\s*\)", area_text):
        expanded, sources = _expand_levelscript(name, blocks)
        area_text += "\n" + _comment_free(expanded)
        area_sources.update(sources)
    prelude = clean[:selected_match.start()]
    model_text = prelude + "\n" + area_text
    for name in re.findall(r"\bJUMP_LINK\s*\(\s*([A-Za-z0-9_]+)\s*\)", prelude):
        expanded, sources = _expand_levelscript(name, blocks)
        model_text += "\n" + _comment_free(expanded)
        area_sources.update(sources)
    return _comment_free(area_text), _comment_free(model_text), area_sources


@lru_cache(maxsize=None)
def _asset_root_source(root: Path, asset_root: str) -> str | None:
    if asset_root == "none": return None
    candidates = sorted(root.glob("actors/**/*.c")) + sorted(root.glob("levels/**/*.c"))
    definition = re.compile(r"\b" + re.escape(asset_root) + r"\s*(?:\[[^]]*\])?\s*=")
    matches = [path for path in candidates if definition.search(path.read_text(encoding="utf-8", errors="ignore"))]
    if len(matches) > 1:
        raise ClosureError(f"ambiguous asset root {asset_root}: {', '.join(_relative(root, path) for path in matches)}")
    if matches:
        return _relative(root, matches[0])
    return None


def _geo_source(root: Path, geo_root: str) -> str | None:
    return _asset_root_source(root, geo_root)


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
            required_child_fields = {"model", "behavior", "maximum_instances", "edge", "capacity"}
            if (set(child) - (required_child_fields | {"recurrent_bound"}) or not required_child_fields <= set(child)
                    or int(child["maximum_instances"]) < 1):
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
            if "recurrent_bound" in child:
                proof = child["recurrent_bound"]
                proof_fields = {
                    "kind", "source", "spawner_location", "spawn_guard_expression", "activation_expression",
                    "unload_expression", "child_location", "child_guard_expression", "deletion_expression",
                }
                if not isinstance(proof, dict) or set(proof) != proof_fields or proof.get("kind") != "state_gated_child_deletion":
                    raise ClosureError(f"invalid recurrent live-bound proof: {rule['behavior']} -> {child['behavior']}")
                try:
                    proof_text = attestation_text(proof["source"], "recurrent live-bound proof")
                    spawner_region = _source_region(proof_text, proof["spawner_location"])
                    child_region = _source_region(proof_text, proof["child_location"])
                    spawner_facts = (
                        proof["spawn_guard_expression"], proof["activation_expression"], proof["unload_expression"],
                    )
                    child_facts = (proof["child_guard_expression"], proof["deletion_expression"])
                    child_block = blocks.get(child["behavior"], "")
                    valid = (
                        proof["spawner_location"] in owners
                        and all(_normalized_expression(fact) in _normalized_expression(spawner_region) for fact in spawner_facts)
                        and all(_normalized_expression(fact) in _normalized_expression(child_region) for fact in child_facts)
                        and re.search(r"CALL_NATIVE\s*\(\s*" + re.escape(proof["child_location"]) + r"\b", child_block)
                    )
                except (KeyError, TypeError, ClosureError):
                    valid = False
                if not valid:
                    raise ClosureError(f"invalid recurrent live-bound proof: {rule['behavior']} -> {child['behavior']}")
            global_edge = (rule["behavior"], *edge)
            if global_edge in rule_edges:
                raise ClosureError(f"duplicate manual rule edge: {global_edge}")
            rule_edges.add(global_edge)
    return payload["rules"]


def _native_behavior_sources(root: Path) -> dict[str, set[str]]:
    sources: dict[str, set[str]] = defaultdict(set)
    for function, definitions in _native_symbol_index(root).items():
        if function.startswith("bhv_"):
            sources[function].update(path for path, _ in definitions)
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
    for name in ("spawn_object", "spawn_object_relative", "spawn_object_abs_with_rot", "spawn_object_with_scale",
                 "spawn_object_relative_with_scale", "spawn_object_rel_with_rot", "spawn_object_at_origin",
                 "try_to_spawn_object"):
        for call in _extract_calls(text, name):
            args = _arguments(call)
            model = next((arg for arg in args if re.fullmatch(r"MODEL_[A-Z0-9_]+", arg)), None)
            behavior = next((arg for arg in args if re.fullmatch(r"bhv[A-Za-z0-9_]+", arg)), None)
            if model is None and any(arg == "0" for arg in args) and behavior:
                model = "MODEL_NONE"
            if behavior == "bhvOpenableCageDoor" and any("gOpenableGrills" in arg for arg in args):
                model = "MODEL_BOB_BARS_GRILLS"
            if behavior == "bhvChainChompChainPart" and "CHAIN_CHOMP_CHAIN_PART_BP_PIVOT" in args:
                model = "MODEL_NONE"
            if name == "spawn_object" and args == ["o", "a0->model", "a0->behavior"]:
                continue  # Source-attested computed contents are validated by the owning manual rule.
            if name == "spawn_object" and args == ["o", "o->oRespawnerModelToRespawn", "o->oRespawnerBehaviorToRespawn"]:
                continue  # Contextual replacement target is recovered from create_respawner callers.
            if name == "spawn_object" and args == ["o", "info->model", "bhvWhitePuffExplosion"]:
                continue  # The concrete SpawnParticlesInfo initializer is resolved at its caller.
            if name == "spawn_object" and args == ["o", "triModel", "bhvBreakBoxTriangle"]:
                continue  # The concrete model is resolved from spawn_triangle_break_particles callers.
            if name == "spawn_object" and args == ["obj", "model", "coinBehavior"]:
                continue  # Concrete loot-coin wrappers are resolved at their callers.
            if model and behavior:
                edges.add((model, behavior))
            else:
                raise ClosureError(f"unrecognized dynamic native spawn form: {name}({', '.join(args)})")
    return edges


def _object_pool_cap(root: Path) -> tuple[int, str]:
    relative = "src/game/object_list_processor.h"
    try:
        text = _read(root, relative)
    except ClosureError as error:
        raise ClosureError("no source-attested maximum-live bound: OBJECT_POOL_CAPACITY source missing") from error
    match = re.search(r"^\s*#define\s+OBJECT_POOL_CAPACITY\s+([1-9][0-9]*)\b", text, re.M)
    if not match:
        raise ClosureError("no source-attested maximum-live bound: OBJECT_POOL_CAPACITY is not literal")
    return int(match.group(1)), relative


@lru_cache(maxsize=None)
def _native_discovery(root: Path, source: str, entry: str) -> tuple[list[tuple[str, str, str]], list[tuple[str, str, frozenset[str], str]], set[tuple[str, str]]]:
    """Return concrete sites, parser-resolved computed edges, and respawn targets."""
    regions = _reachable_native_regions(root, source, entry)
    reached_sources = frozenset(region_source for region_source, _, _ in regions)
    sites: list[tuple[str, str, str]] = []
    computed: list[tuple[str, str, frozenset[str], str]] = []
    respawn_targets: set[tuple[str, str]] = set()
    index = _native_symbol_index(root)
    for region_source, symbol, region in regions:
        if symbol != "cur_obj_spawn_particles":
            for model, behavior in _native_spawn_edges(region):
                sites.append((region_source, model, behavior))
        for call in ([] if symbol == "cur_obj_spawn_particles" else _extract_calls(region, "cur_obj_spawn_particles")):
            args = _arguments(call)
            if len(args) != 1 or not re.fullmatch(r"&[A-Za-z_][A-Za-z0-9_]*", args[0]):
                raise ClosureError(f"unrecognized dynamic particle creation: {region_source}:{symbol}")
            info_name = args[0][1:]
            definitions = index.get(info_name, [])
            models = {(path, model) for path, definition in definitions for model in re.findall(r"\bMODEL_[A-Z0-9_]+\b", definition)}
            if len(models) != 1:
                raise ClosureError(f"unresolved SpawnParticlesInfo model {info_name}: {region_source}:{symbol}")
            info_source, model = next(iter(models))
            computed.append((model, "bhvWhitePuffExplosion", reached_sources | {info_source}, "pool_cap"))
        for call in ([] if symbol == "create_respawner" else _extract_calls(region, "create_respawner")):
            args = _arguments(call)
            if len(args) != 3 or not re.fullmatch(r"MODEL_[A-Z0-9_]+", args[0]) or not re.fullmatch(r"bhv[A-Za-z0-9_]+", args[1]):
                raise ClosureError(f"unrecognized dynamic respawner creation: {region_source}:{symbol}")
            computed.append(("MODEL_NONE", "bhvRespawner", reached_sources, "replacement"))
            respawn_targets.add((args[0], args[1]))
        for call in ([] if symbol == "spawn_triangle_break_particles" else _extract_calls(region, "spawn_triangle_break_particles")):
            args = _arguments(call)
            if len(args) != 4 or not re.fullmatch(r"MODEL_[A-Z0-9_]+", args[1]):
                raise ClosureError(f"unrecognized dynamic triangle-particle creation: {region_source}:{symbol}")
            computed.append((args[1], "bhvBreakBoxTriangle", reached_sources, "pool_cap"))
        for call in ([] if symbol == "create_sound_spawner" else _extract_calls(region, "create_sound_spawner")):
            args = _arguments(call)
            if len(args) != 1:
                raise ClosureError(f"unrecognized dynamic sound-spawner creation: {region_source}:{symbol}")
            computed.append(("MODEL_NONE", "bhvSoundSpawner", reached_sources, "pool_cap"))
        for helper, model, child in (
            ("obj_spawn_loot_yellow_coins", "MODEL_YELLOW_COIN", "bhvSingleCoinGetsSpawned"),
            ("obj_spawn_loot_blue_coins", "MODEL_BLUE_COIN", "bhvBlueCoinJumping"),
        ):
            if symbol != helper and _extract_calls(region, helper):
                computed.append((model, child, reached_sources, "pool_cap"))
    return sites, computed, respawn_targets


def _source_regions(text: str) -> dict[str, str]:
    clean = _comment_free(text)
    symbols = set(re.findall(r"^\s*(?:static\s+)?(?:[A-Za-z_]\w*\s+)+(?:\*\s*)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{", clean, re.M))
    symbols.update(re.findall(r"^\s*(?:static\s+)?(?:const\s+)?(?:struct\s+\w+\s+|[A-Za-z_]\w*(?:\s+|\s*\*\s*))+([A-Za-z_]\w*)\s*(?:\[[^]]*\])?\s*=\s*\{", clean, re.M))
    symbols.update(re.findall(r"^\s*(?:static\s+)?[A-Za-z_]\w*\s*\(\s*\*\s*([A-Za-z_]\w*)\s*\[[^]]*\]\s*\)\s*\([^)]*\)\s*=\s*\{", clean, re.M))
    symbols.difference_update({"if", "for", "while", "switch"})
    return {symbol: region for symbol in symbols if (region := _source_region(clean, symbol))}


@lru_cache(maxsize=None)
def _file_source_regions(path: str) -> dict[str, str]:
    return _source_regions(Path(path).read_text(encoding="utf-8", errors="ignore"))


@lru_cache(maxsize=None)
def _native_symbol_index(root: Path) -> dict[str, list[tuple[str, str]]]:
    index: dict[str, list[tuple[str, str]]] = defaultdict(list)
    paths = ([path for path in sorted((root / "src").rglob("*.c"))
              if "port" not in path.relative_to(root / "src").parts]
             if (root / "src").is_dir() else [])
    if len(paths) > 4096:
        raise ClosureError(f"repository native source index limit exceeded: {len(paths)} C sources")
    symbol_count = 0
    for path in paths:
        relative = _relative(root, path)
        for symbol, region in _file_source_regions(str(path.resolve())).items():
            index[symbol].append((relative, region))
            symbol_count += 1
            if symbol_count > 65536:
                raise ClosureError("repository native symbol index limit exceeded")
    return index


_NATIVE_TRAVERSAL_STOPS = {
    "spawn_object", "spawn_object_relative", "spawn_object_abs_with_rot", "spawn_object_with_scale",
    "spawn_object_relative_with_scale", "spawn_object_rel_with_rot", "spawn_object_at_origin",
    "try_to_spawn_object", "play_sound", "play_sound_with_freq_scale", "cur_obj_play_sound_1",
    "cur_obj_play_sound_2", "cur_obj_play_sound_at_anim_range",
}


@lru_cache(maxsize=None)
def _reachable_native_regions(root: Path, source: str, entry: str) -> list[tuple[str, str, str]]:
    """Bounded repository-wide function/data walk rooted at one concrete callback."""
    index = _native_symbol_index(root)
    pending, seen, reachable = [(source, entry)], set(), []
    while pending:
        current_source, name = pending.pop()
        key = (current_source, name)
        if key in seen:
            continue
        if len(seen) >= 256:
            raise ClosureError(f"native helper traversal limit exceeded: {entry}")
        seen.add(key)
        region = _file_source_regions(str((root / current_source).resolve())).get(name, "")
        if not region:
            raise ClosureError(f"missing native definition {name}: {current_source}")
        reachable.append((current_source, name, region))
        reached_text = _read(root, source) + "\n" + "\n".join(item[2] for item in reachable[:-1])
        for symbol in set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\b", region)):
            if symbol in _NATIVE_TRAVERSAL_STOPS:
                continue
            dispatcher_guards = {
                "shelled_koopa_attack_handler": "ATTACK_HANDLER_SPECIAL_KOOPA_LOSE_SHELL",
                "wiggler_jumped_on_attack_handler": "ATTACK_HANDLER_SPECIAL_WIGGLER_JUMPED_ON",
                "huge_goomba_weakly_attacked": "ATTACK_HANDLER_SPECIAL_HUGE_GOOMBA_WEAKLY_ATTACKED",
            }
            if name == "obj_handle_attacks" and symbol in dispatcher_guards and dispatcher_guards[symbol] not in reached_text:
                continue
            definitions = index.get(symbol, [])
            local = [item for item in definitions if item[0] == current_source]
            if local:
                pending.append((current_source, symbol))
            elif definitions and all(path.startswith("src/audio/") for path, _ in definitions):
                continue  # Audio-engine sinks do not create scene objects or own caller SFX IDs.
            elif len(definitions) == 1:
                pending.append((definitions[0][0], symbol))
            elif len(definitions) > 1:
                raise ClosureError(f"ambiguous cross-file native symbol {symbol} from {current_source}:{name}")
    return reachable


def _reachable_native_bodies(root: Path, source: str, entry: str) -> list[str]:
    return [region for _, _, region in _reachable_native_regions(root, source, entry)]


def _routed_source_symbols(root: Path, owner_source: str, owner: str, target_source: str) -> set[str]:
    return {symbol for source, symbol, _ in _reachable_native_regions(root, owner_source, owner) if source == target_source}


def _sound_declarations(root: Path) -> tuple[dict[str, list[str]], str]:
    relative = "include/sounds.h"
    text = _read(root, relative)
    declarations: dict[str, list[str]] = defaultdict(list)
    for match in re.finditer(r"^\s*#define\s+(SOUND_[A-Z0-9_]+)\b([^\n]*(?:\\\r?\n[^\n]*)*)", text, re.M):
        banks = re.findall(r"\bSOUND_ARG_LOAD\s*\(\s*(SOUND_BANK_[A-Z0-9_]+)\b", match.group(2))
        declarations[match.group(1)].extend(banks)
    return declarations, relative


_AUDIO_SINK_ARGUMENTS = {
    "play_sound": (0,),
    "play_sound_with_freq_scale": (0,),
    "cur_obj_play_sound_1": (0,),
    "cur_obj_play_sound_2": (0,),
    "cur_obj_play_sound_at_anim_range": (2,),
    "create_sound_spawner": (0,),
}


def _function_parameters(region: str, symbol: str) -> dict[str, int]:
    match = re.search(r"\b" + re.escape(symbol) + r"\s*\(([^)]*)\)\s*\{", region, re.S)
    if not match:
        return {}
    parameters: dict[str, int] = {}
    for index, declaration in enumerate(_arguments(match.group(1))):
        names = re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", declaration)
        if names and names[-1] != "void":
            parameters[names[-1]] = index
    return parameters


def _region_assignments(region: str) -> dict[str, list[str]]:
    assignments: dict[str, list[str]] = defaultdict(list)
    for match in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?<![+*/%&|^!<>-])=(?!=)\s*([^;]+);", region):
        assignments[match.group(1)].append(match.group(2))
    return assignments


def _native_audio_sound_ids(root: Path, regions: list[tuple[str, str, str]]) -> set[str]:
    """Trace concrete sound values only through proven sink argument paths."""
    index = _native_symbol_index(root)
    region_map = {(source, symbol): region for source, symbol, region in regions}
    function_regions = {
        key: region for key, region in region_map.items() if _function_body(region, key[1])
    }
    data_regions = set(region_map) - set(function_regions)
    facts: dict[tuple[str, str], tuple[set[str], set[int]]] = {
        key: (set(), set()) for key in function_regions
    }

    def target_key(current_source: str, symbol: str) -> tuple[str, str] | None:
        definitions = index.get(symbol, [])
        local = [item for item in definitions if item[0] == current_source]
        if local:
            return current_source, symbol
        if len(definitions) == 1:
            return definitions[0][0], symbol
        return None

    def expression_flow(
        expression: str,
        current_source: str,
        assignments: dict[str, list[str]],
        parameters: dict[str, int],
        seen: frozenset[tuple[str, str, str]],
    ) -> tuple[set[str], set[int]]:
        sounds = set(re.findall(r"\bSOUND_[A-Z0-9_]+\b", expression))
        parameter_indexes: set[int] = set()
        for name in set(re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", expression)):
            if name in parameters:
                parameter_indexes.add(parameters[name])
                continue
            variable_key = ("variable", current_source, name)
            if name in assignments and variable_key not in seen:
                if len(seen) >= 1024:
                    raise ClosureError("native audio value-flow limit exceeded")
                for value in assignments[name]:
                    nested_sounds, nested_parameters = expression_flow(
                        value, current_source, assignments, parameters, seen | {variable_key}
                    )
                    sounds.update(nested_sounds)
                    parameter_indexes.update(nested_parameters)
                continue
            data_key = target_key(current_source, name)
            data_seen_key = ("data", *(data_key or (current_source, name)))
            if data_key in data_regions and data_seen_key not in seen:
                if len(seen) >= 1024:
                    raise ClosureError("native audio value-flow limit exceeded")
                nested_sounds, nested_parameters = expression_flow(
                    region_map[data_key], data_key[0], {}, {}, seen | {data_seen_key}
                )
                sounds.update(nested_sounds)
                parameter_indexes.update(nested_parameters)
        return sounds, parameter_indexes

    for iteration in range(len(function_regions) + 1):
        changed = False
        for key, region in function_regions.items():
            source, symbol = key
            clean = _comment_free(region)
            parameters = _function_parameters(clean, symbol)
            assignments = _region_assignments(clean)
            body_start = clean.find("{")
            sounds, parameter_indexes = (set(facts[key][0]), set(facts[key][1]))
            controls = {"if", "for", "while", "switch", "sizeof"}
            call_names = set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", clean)) - controls
            for callee in call_names:
                if callee in _AUDIO_SINK_ARGUMENTS:
                    sound_arguments = _AUDIO_SINK_ARGUMENTS[callee]
                else:
                    callee_key = target_key(source, callee)
                    sound_arguments = tuple(sorted(facts.get(callee_key, (set(), set()))[1]))
                for arguments, call_start in _extract_call_sites(clean, callee):
                    if call_start < body_start:
                        continue
                    args = _arguments(arguments)
                    for argument_index in sound_arguments:
                        if argument_index >= len(args):
                            raise ClosureError(f"malformed audio call {callee}: {source}:{symbol}")
                        nested_sounds, nested_parameters = expression_flow(
                            args[argument_index], source, assignments, parameters, frozenset()
                        )
                        sounds.update(nested_sounds)
                        parameter_indexes.update(nested_parameters)
            if sounds != facts[key][0] or parameter_indexes != facts[key][1]:
                facts[key] = sounds, parameter_indexes
                changed = True
        if not changed:
            break
    else:
        raise ClosureError("native audio forwarding analysis did not converge")
    return set().union(*(sounds for sounds, _ in facts.values())) if facts else set()


def _behavior_sounds(root: Path, block: str, native_sources: dict[str, set[str]]) -> tuple[list[str], list[str], set[str]]:
    sounds: set[str] = set()
    reached_sources: set[str] = set()
    for native in re.findall(r"CALL_NATIVE\s*\(\s*(bhv_[A-Za-z0-9_]+)", block):
        definitions = native_sources.get(native, set())
        if not definitions:
            raise ClosureError(f"missing native definition {native}")
        if len(definitions) != 1:
            raise ClosureError(f"ambiguous native definition {native}: {', '.join(sorted(definitions))}")
        for source in definitions:
            reached_sources.add(source)
            regions = _reachable_native_regions(root, source, native)
            sounds.update(_native_audio_sound_ids(root, regions))
            reached_sources.update(region_source for region_source, _, _ in regions)
    sounds = sorted(sounds)
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
    declared = {(rule["behavior"], child["edge"].get("source", rule["source"]), child["model"], child["behavior"])
                for rule in rules for child in rule["children"]}
    blocks = _behavior_blocks(_read(root, "data/behavior_data.c"))
    native_sources = _native_behavior_sources(root)
    sites: list[str] = []
    for record in document["records"]:
        for native in re.findall(r"CALL_NATIVE\s*\(\s*(bhv_[A-Za-z0-9_]+)", blocks[record["stable_id"]]):
            definitions = native_sources.get(native, set())
            if not definitions:
                raise ClosureError(f"missing native definition {native}")
            if len(definitions) != 1:
                raise ClosureError(f"ambiguous native definition {native}: {', '.join(sorted(definitions))}")
            for source in definitions:
                discovered, computed, respawn_targets = _native_discovery(root, source, native)
                parser_resolved = {(model, child) for model, child, _, _ in computed}
                parser_resolved.update((model, child) for _, model, child in discovered)
                for edge_source, model, child in discovered:
                    if (model, child) in parser_resolved or (child == "bhvRespawner" and respawn_targets):
                        continue
                    if (record["stable_id"], edge_source, model, child) not in declared:
                        sites.append(f"{record['stable_id']}:{edge_source}:{model}:{child}")
    return sorted(set(sites))


def _callback_is_recurrent(block: str, owners: list[str]) -> bool:
    clean = _comment_free(block)
    controls = [position for token in ("BEGIN_LOOP", "BEGIN_REPEAT", "GOTO") if (position := clean.find(token)) >= 0]
    first_control = min(controls, default=-1)
    return first_control >= 0 and any(match.start() > first_control for owner in owners for match in re.finditer(r"CALL_NATIVE\s*\(\s*" + re.escape(owner) + r"\b", clean))


def _behavior_script_site_is_recurrent(block: str, site: int) -> bool:
    """Classify one BehaviorScript creation site, not the first site in its block."""
    clean = _comment_free(block)
    depth = 0
    for match in re.finditer(r"\b(BEGIN_LOOP|END_LOOP|BEGIN_REPEAT|END_REPEAT|GOTO)\s*\(", clean[:site]):
        token = match.group(1)
        if token in {"BEGIN_LOOP", "BEGIN_REPEAT"}:
            depth += 1
        elif token in {"END_LOOP", "END_REPEAT"}:
            depth = max(0, depth - 1)
        elif token == "GOTO":
            return True
    return depth > 0


def _manual_live_bound(root: Path, block: str, rule: dict, child: dict) -> tuple[int, set[str]]:
    burst = int(child["maximum_instances"])
    owners = rule["owner"] if isinstance(rule["owner"], list) else [rule["owner"]]
    if not _callback_is_recurrent(block, owners):
        return burst, set()
    if child.get("recurrent_bound", {}).get("kind") == "state_gated_child_deletion":
        return burst, {child["recurrent_bound"]["source"]}
    cap, cap_source = _object_pool_cap(root)
    return cap, {cap_source}


def collect_scene_closure(root: Path, level: str, area: int, rules_path: Path) -> dict:
    root = root.resolve()
    script_path = f"levels/{level}/script.c"
    macro_path = f"levels/{level}/areas/{area}/macro.inc.c"
    level_text, macro_text = _read(root, script_path), _read(root, macro_path)
    area_text, model_scope_text, levelscript_sources = _scoped_levelscript(root, level, level_text, area)
    model_geos, model_ids_path = _model_geos(root)
    loaded_geo_sources = _load_model_roots(root, model_scope_text, model_geos)
    presets, presets_path = _macro_presets(root)
    behavior_path = "data/behavior_data.c"
    behavior_text = _read(root, behavior_path)
    blocks = _behavior_blocks(behavior_text)
    native_sources = _native_behavior_sources(root)
    roots = _object_roots(area_text, macro_text, presets)
    manual_rules = _rules(root, rules_path)
    static_edges: dict[str, list[tuple[str, str, int, frozenset[str], str]]] = defaultdict(list)
    pool_cap: int | None = None
    for behavior, block in blocks.items():
        for macro in ("SPAWN_CHILD", "SPAWN_CHILD_WITH_PARAM", "SPAWN_OBJ"):
            for call, site in _extract_call_sites(block, macro):
                args = _arguments(call)
                model = next((arg for arg in args if re.fullmatch(r"MODEL_[A-Z0-9_]+", arg)), None)
                child = next((arg for arg in args if re.fullmatch(r"bhv[A-Za-z0-9_]+", arg)), None)
                if model and child:
                    bound, sources = 1, {behavior_path}
                    if _behavior_script_site_is_recurrent(block, site):
                        bound, cap_source = _object_pool_cap(root)
                        pool_cap = bound
                        sources.add(cap_source)
                    static_edges[behavior].append((model, child, bound, frozenset(sources), "spawn"))
    for rule in manual_rules:
        for child in rule["children"]:
            evidence_sources = {rule["source"], child["edge"].get("source", rule["source"]), child["capacity"].get("source", rule["source"])}
            if child["edge"].get("model_symbol"):
                evidence_sources.add("include/object_constants.h")
            live_bound, bound_sources = _manual_live_bound(root, blocks[rule["behavior"]], rule, child)
            static_edges[rule["behavior"]].append((child["model"], child["behavior"], live_bound, frozenset(evidence_sources | bound_sources), "spawn"))
    occurrence: dict[str, Counter[str]] = defaultdict(Counter)
    acts: dict[str, set[str]] = defaultdict(set)
    root_names: dict[str, list[str]] = defaultdict(list)
    declared_models: dict[str, set[str]] = defaultdict(set)
    queue = deque()
    for model, behavior, act, count in roots:
        queue.append((model, behavior, _acts(act), count, "level", frozenset()))
    child_map: dict[str, set[str]] = defaultdict(set)
    typed_children: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    used_sources: dict[str, set[str]] = defaultdict(set)
    analyzed_behaviors: set[str] = set()
    respawn_targets: set[tuple[str, str]] = set()
    while queue:
        model, behavior, active_acts, count, source_kind, ancestors = queue.popleft()
        if behavior not in blocks:
            raise ClosureError(f"undeclared behavior {behavior}")
        if model != "MODEL_NONE" and model not in model_geos:
            raise ClosureError(f"no geo root for {model}")
        declared_models[behavior].add(model)
        for act in active_acts:
            occurrence[behavior][act] += count
        acts[behavior].update(active_acts)
        root_names[behavior].append(source_kind)
        used_sources[behavior].add(behavior_path)
        for native in re.findall(r"CALL_NATIVE\s*\(\s*(bhv_[A-Za-z0-9_]+)", blocks[behavior]):
            definitions = native_sources.get(native, set())
            if not definitions:
                raise ClosureError(f"missing native definition {native}")
            if len(definitions) != 1:
                raise ClosureError(f"ambiguous native definition {native}: {', '.join(sorted(definitions))}")
            for source in definitions:
                regions = _reachable_native_regions(root, source, native)
                used_sources[behavior].update(region_source for region_source, _, _ in regions)
                if behavior not in analyzed_behaviors:
                    discovered_sites, computed, targets = _native_discovery(root, source, native)
                    reached_sources = frozenset(region_source for region_source, _, _ in regions)
                    computed.extend((model, child, reached_sources | {edge_source}, "pool_cap")
                                    for edge_source, model, child in discovered_sites)
                    respawn_targets.update(targets)
                    for child_model, computed_child, edge_sources, relation in computed:
                        bound = 1
                        sources = set(edge_sources)
                        if relation == "pool_cap":
                            pool_cap, cap_source = _object_pool_cap(root)
                            bound = pool_cap
                            sources.add(cap_source)
                        edge = (child_model, computed_child, bound, frozenset(sources), relation)
                        if not any(existing[0] == child_model and existing[1] == computed_child for existing in static_edges[behavior]):
                            static_edges[behavior].append(edge)
        if behavior == "bhvRespawner":
            for target_model, target_behavior in sorted(respawn_targets):
                edge = (target_model, target_behavior, 1, frozenset({"src/game/behaviors/corkbox.inc.c"}), "replacement")
                if edge not in static_edges[behavior]:
                    static_edges[behavior].append(edge)
        analyzed_behaviors.add(behavior)
        for child_model, child, maximum_live, edge_sources, relation in static_edges.get(behavior, []):
            if child not in blocks:
                raise ClosureError(f"undeclared behavior {child}")
            child_map[behavior].add(child)
            typed_children[behavior][_edge_kind(child_model, child)].add(child)
            used_sources[behavior].update(edge_sources)
            if child in ancestors or child == behavior:
                if relation == "replacement":
                    continue
                raise ClosureError(f"behavior spawn cycle: {behavior} -> {child}")
            queue.append((child_model, child, active_acts, count * maximum_live, f"spawn:{behavior}", ancestors | {behavior}))
    records = []
    for behavior in sorted(occurrence):
        variants = []
        model_provenance = {}
        geo_sources: set[str] = set()
        material_features: set[str] = set()
        for variant_model in sorted(declared_models[behavior]):
            variant_geo = "none" if variant_model == "MODEL_NONE" else model_geos[variant_model]
            variant_geo_source = _asset_root_source(root, variant_geo)
            if variant_model != "MODEL_NONE" and not variant_geo_source:
                raise ClosureError(f"unresolved model/geo root {variant_model} {variant_geo}")
            variants.append({"model": variant_model, "geo_root": variant_geo})
            binding_source = _model_binding_source(
                root, variant_model, variant_geo, levelscript_sources | {script_path}, model_ids_path,
            )
            model_provenance[variant_model] = {
                "source": model_ids_path,
                "binding_source": binding_source,
                "geo_symbol": variant_geo,
                "geo_source": variant_geo_source,
            }
            used_sources[behavior].add(binding_source)
            if variant_geo_source:
                geo_sources.add(variant_geo_source)
                material_features.update(_features(root, variant_geo_source))
        primary = next((variant for variant in variants if variant["model"] != "MODEL_NONE"), variants[0])
        model, geo_root = primary["model"], primary["geo_root"]
        sounds, banks, sound_sources = _behavior_sounds(root, blocks[behavior], native_sources)
        sources = {behavior_path, model_ids_path} | used_sources[behavior] | sound_sources
        sources.update(geo_sources)
        block = blocks[behavior]
        animations = sorted(set(re.findall(r"LOAD_ANIMATIONS\s*\(\s*[^,]+,\s*([A-Za-z0-9_]+)", block)))
        animation_sources: dict[str, str] = {}
        for animation in animations:
            animation_source = _asset_root_source(root, animation)
            if not animation_source:
                raise ClosureError(f"unresolved animation root {animation}")
            animation_sources[animation] = animation_source
            sources.add(animation_source)
        source_items = [_source(root, path) for path in sorted(sources)]
        maximum_live_instances = max(occurrence[behavior].values())
        if pool_cap is not None:
            maximum_live_instances = min(maximum_live_instances, pool_cap)
        records.append({"stable_id": behavior, "level": level, "area": area, "act_mask": " | ".join(sorted(acts[behavior])), "object_roots": sorted(set(root_names[behavior])), "model": model, "geo_root": geo_root, "model_variants": variants, "behavior_root": behavior, "spawned_children": sorted(child_map[behavior]), "children": sorted(typed_children[behavior]["child"]), "rewards": sorted(typed_children[behavior]["reward"]), "projectiles": sorted(typed_children[behavior]["projectile"]), "effects": sorted(typed_children[behavior]["effect"]), "animation_table": animations, "material_feature_bits": sorted(material_features), "maximum_live_instances": maximum_live_instances, "music_sequence_ids": [], "sfx_banks": banks, "sfx_ids": sounds, "sources": source_items, "root_provenance": {"behavior": {"symbol": behavior, "source": behavior_path}, "models": model_provenance, "animation": animation_sources}})
    area_music = sorted(set(re.findall(r"\bSEQ_[A-Z0-9_]+\b", area_text)))
    for record in records:
        record["music_sequence_ids"] = area_music
    scene_sources = [script_path, macro_path, presets_path, *loaded_geo_sources, *levelscript_sources]
    if rules_path.is_file():
        try:
            scene_sources.append(_relative(root, rules_path))
        except ValueError:
            pass
    source_paths = set(scene_sources)
    source_paths.update(source["path"] for record in records for source in record["sources"])
    document = {"schema": SCHEMA, "source_root": str(root), "level": level, "area": area, "records": records, "scene_sources": sorted(source_paths - {source["path"] for record in records for source in record["sources"]}), "source_hashes": {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sorted(source_paths)}, "music_sequence_ids": area_music, "sfx_banks": sorted({bank for record in records for bank in record["sfx_banks"]}), "sfx_ids": sorted({sfx for record in records for sfx in record["sfx_ids"]})}
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
