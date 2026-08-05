#!/usr/bin/env python3
"""Derive a deterministic, generic scene dependency closure from game source."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter, defaultdict, deque
from pathlib import Path

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


def _rules(root: Path, rules_path: Path) -> list[dict]:
    if not rules_path.exists(): return []
    payload = json.loads(rules_path.read_text(encoding="utf-8"))
    if set(payload) != {"schema", "rules"} or payload["schema"] != "sm64-saturn-behavior-spawn-rules-v1":
        raise ClosureError("invalid behavior spawn rules schema")
    for rule in payload["rules"]:
        if set(rule) != {"behavior", "source", "reason", "children"} or not rule["reason"]:
            raise ClosureError("manual spawn rule must name behavior, exact source, and reason")
        if not (root / rule["source"]).is_file():
            raise ClosureError(f"manual spawn rule source missing: {rule['source']}")
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
    if any(token in behavior or token in model for token in ("Smoke", "Sparkle", "Explosion", "Shadow", "Puff", "Particle", "Bubble", "MODEL_SMOKE", "MODEL_SPARKLES")):
        return "effect"
    return "child"


def collect_scene_closure(root: Path, level: str, area: int, rules_path: Path) -> dict:
    root = root.resolve()
    script_path = f"levels/{level}/script.c"
    macro_path = f"levels/{level}/areas/{area}/macro.inc.c"
    level_text, macro_text = _read(root, script_path), _read(root, macro_path)
    model_geos, model_ids_path = _model_geos(root)
    presets, presets_path = _macro_presets(root)
    behavior_path = "data/behavior_data.c"
    behavior_text = _read(root, behavior_path)
    blocks = _behavior_blocks(behavior_text)
    native_sources = _native_behavior_sources(root)
    roots = _object_roots(level_text, macro_text, presets)
    manual_rules = _rules(root, rules_path)
    static_edges: dict[str, list[tuple[str, str, int, str]]] = defaultdict(list)
    for behavior, block in blocks.items():
        for call in _extract_calls(block, "SPAWN_CHILD") + _extract_calls(block, "SPAWN_CHILD_WITH_PARAM") + _extract_calls(block, "SPAWN_OBJ"):
            args = _arguments(call)
            model = next((arg for arg in args if re.fullmatch(r"MODEL_[A-Z0-9_]+", arg)), None)
            child = next((arg for arg in args if re.fullmatch(r"bhv[A-Za-z0-9_]+", arg)), None)
            if model and child: static_edges[behavior].append((model, child, 1, behavior_path))
    for rule in manual_rules:
        for child in rule["children"]:
            static_edges[rule["behavior"]].append((child["model"], child["behavior"], int(child.get("maximum_instances", 1)), rule["source"]))
    occurrence = Counter()
    acts: dict[str, set[str]] = defaultdict(set)
    root_names: dict[str, list[str]] = defaultdict(list)
    declared_model: dict[str, str] = {}
    queue = deque()
    for model, behavior, act, count in roots:
        queue.append((model, behavior, act, count, "level", frozenset()))
    child_map: dict[str, set[str]] = defaultdict(set)
    typed_children: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    used_sources: dict[str, set[str]] = defaultdict(set)
    while queue:
        model, behavior, act, count, source_kind, ancestors = queue.popleft()
        if behavior not in blocks:
            raise ClosureError(f"undeclared behavior {behavior}")
        if model != "MODEL_NONE" and model not in model_geos:
            raise ClosureError(f"no geo root for {model}")
        if behavior not in declared_model:
            declared_model[behavior] = model
        elif declared_model[behavior] == "MODEL_NONE" and model != "MODEL_NONE":
            declared_model[behavior] = model
        occurrence[behavior] += count
        acts[behavior].add(act)
        root_names[behavior].append(source_kind)
        used_sources[behavior].add(behavior_path)
        for native in re.findall(r"CALL_NATIVE\s*\(\s*(bhv_[A-Za-z0-9_]+)", blocks[behavior]):
            used_sources[behavior].update(native_sources.get(native, set()))
        for child_model, child, factor, source in static_edges.get(behavior, []):
            if child not in blocks:
                raise ClosureError(f"undeclared behavior {child}")
            child_map[behavior].add(child)
            typed_children[behavior][_edge_kind(child_model, child)].add(child)
            used_sources[behavior].add(source)
            if child in ancestors or child == behavior:
                continue
            queue.append((child_model, child, act, count * factor, f"spawn:{behavior}", ancestors | {behavior}))
    records = []
    for behavior in sorted(occurrence):
        model = declared_model[behavior]
        geo_root = "none" if model == "MODEL_NONE" else model_geos[model]
        geo_source = _geo_source(root, geo_root)
        sources = {behavior_path, model_ids_path} | used_sources[behavior]
        if geo_source: sources.add(geo_source)
        block = blocks[behavior]
        animations = sorted(set(re.findall(r"LOAD_ANIMATIONS\s*\(\s*[^,]+,\s*([A-Za-z0-9_]+)", block)))
        source_items = [_source(root, path) for path in sorted(sources)]
        sounds = sorted(set(re.findall(r"\bSOUND_[A-Z0-9_]+\b", "\n".join(_read(root, path) for path in sources))))
        records.append({"stable_id": behavior, "level": level, "area": area, "act_mask": " | ".join(sorted(acts[behavior])), "object_roots": sorted(set(root_names[behavior])), "model": model, "geo_root": geo_root, "behavior_root": behavior, "spawned_children": sorted(child_map[behavior]), "rewards": sorted(typed_children[behavior]["reward"]), "projectiles": sorted(typed_children[behavior]["projectile"]), "effects": sorted(typed_children[behavior]["effect"]), "animation_table": animations, "material_feature_bits": _features(root, geo_source), "maximum_live_instances": occurrence[behavior], "music_sequence_ids": [], "sfx_banks": ["general"] if sounds else [], "sfx_ids": sounds, "sources": source_items})
    scene_sources = [script_path, macro_path, presets_path]
    if rules_path.is_file():
        try:
            scene_sources.append(_relative(root, rules_path))
        except ValueError:
            pass
    source_paths = set(scene_sources)
    source_paths.update(source["path"] for record in records for source in record["sources"])
    document = {"schema": SCHEMA, "source_root": str(root), "level": level, "area": area, "records": records, "scene_sources": sorted(source_paths - {source["path"] for record in records for source in record["sources"]}), "source_hashes": {path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in sorted(source_paths)}, "music_sequence_ids": sorted(set(re.findall(r"\bSEQ_[A-Z0-9_]+\b", level_text))), "sfx_banks": sorted({bank for record in records for bank in record["sfx_banks"]}), "sfx_ids": sorted({sfx for record in records for sfx in record["sfx_ids"]})}
    validate_scene_closure(document)
    return document


def write_closure(path: Path, document: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")


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
