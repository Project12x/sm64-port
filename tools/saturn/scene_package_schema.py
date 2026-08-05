#!/usr/bin/env python3
"""Validation for version-one, source-derived scene-closure documents."""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

SCHEMA = "sm64-saturn-scene-closure-v1"
TOP_FIELDS = {"schema", "source_root", "level", "area", "records", "scene_sources", "source_hashes", "music_sequence_ids", "sfx_banks", "sfx_ids"}
RECORD_FIELDS = {"stable_id", "level", "area", "act_mask", "object_roots", "model", "geo_root", "model_variants", "behavior_root", "spawned_children", "children", "rewards", "projectiles", "effects", "animation_table", "material_feature_bits", "maximum_live_instances", "music_sequence_ids", "sfx_banks", "sfx_ids", "sources", "root_provenance"}


def _defines(text: str, symbol: str) -> bool:
    return bool(re.search(r"(?:^|\n)\s*(?:#define\s+" + re.escape(symbol) + r"\b|(?:const\s+)?(?:struct\s+\w+\s+|[A-Za-z_]\w*(?:\s+|\s*\*\s*))+" + re.escape(symbol) + r"\s*(?:\[[^]]*\])?\s*=)", text))


def validate_scene_closure(document: dict) -> None:
    missing = TOP_FIELDS - set(document)
    if missing:
        raise ValueError(f"missing closure field: {sorted(missing)[0]}")
    unknown = set(document) - TOP_FIELDS
    if unknown:
        raise ValueError(f"unknown field: {sorted(unknown)[0]}")
    if document.get("schema") != SCHEMA:
        raise ValueError("unsupported scene closure schema")
    if not isinstance(document.get("records"), list):
        raise ValueError("records must be a list")
    if not isinstance(document.get("level"), str) or not document["level"]:
        raise ValueError("level must be a non-empty string")
    if not isinstance(document.get("area"), int) or document["area"] < 1:
        raise ValueError("area must be a positive integer")
    stable_ids: set[str] = set()
    source_root = Path(document["source_root"])
    source_hashes = document.get("source_hashes", {})
    if not isinstance(source_hashes, dict):
        raise ValueError("source_hashes must be an object")
    for record in document["records"]:
        missing = RECORD_FIELDS - set(record)
        if missing:
            raise ValueError(f"missing record field: {sorted(missing)[0]}")
        unknown = set(record) - RECORD_FIELDS
        if unknown:
            raise ValueError(f"unknown record field: {sorted(unknown)[0]}")
        stable_id = record.get("stable_id")
        if not stable_id or stable_id in stable_ids:
            raise ValueError(f"duplicate stable_id: {stable_id}")
        stable_ids.add(stable_id)
        if record.get("level") != document["level"] or record.get("area") != document["area"]:
            raise ValueError(f"record level/area mismatch: {stable_id}")
        if not record.get("sources"):
            raise ValueError(f"record {stable_id} has no sources")
        if record.get("maximum_live_instances", -1) < 0:
            raise ValueError(f"invalid maximum_live_instances: {stable_id}")
        for source in record["sources"]:
            if not isinstance(source, dict) or set(source) != {"path", "sha256"}:
                raise ValueError(f"invalid source provenance: {stable_id}")
            path = source.get("path")
            digest = source.get("sha256")
            if not path or not digest:
                raise ValueError(f"missing source hash: {stable_id}")
            actual = hashlib.sha256((source_root / path).read_bytes()).hexdigest()
            if actual != digest or source_hashes.get(path) != digest:
                raise ValueError(f"stale source hash: {path}")
        provenance = record.get("root_provenance")
        if not isinstance(provenance, dict) or set(provenance) != {"behavior", "models", "animation"}:
            raise ValueError(f"invalid root provenance: {stable_id}")
        behavior_provenance = provenance["behavior"]
        if (not isinstance(behavior_provenance, dict) or set(behavior_provenance) != {"symbol", "source"}
                or behavior_provenance["symbol"] != record["behavior_root"]):
            raise ValueError(f"invalid root provenance: {stable_id}")
        variants = record.get("model_variants")
        if (not isinstance(variants, list) or not variants
                or any(not isinstance(variant, dict) or set(variant) != {"model", "geo_root"} for variant in variants)
                or variants != sorted(variants, key=lambda variant: (variant["model"], variant["geo_root"]))
                or len({variant["model"] for variant in variants}) != len(variants)):
            raise ValueError(f"invalid model variants: {stable_id}")
        concrete_primary = next((variant for variant in variants if variant["model"] != "MODEL_NONE"), variants[0])
        if record["model"] != concrete_primary["model"] or record["geo_root"] != concrete_primary["geo_root"]:
            raise ValueError(f"invalid model variants: {stable_id}")
        if (not isinstance(provenance["models"], dict)
                or set(provenance["models"]) != {variant["model"] for variant in variants}
                or not isinstance(provenance["animation"], dict)
                or set(provenance["animation"]) != set(record["animation_table"])):
            raise ValueError(f"invalid root provenance: {stable_id}")
        source_paths = {source["path"] for source in record["sources"]}
        expected_paths = {behavior_provenance["source"], *provenance["animation"].values()}
        for model, model_provenance in provenance["models"].items():
            if not isinstance(model_provenance, dict) or set(model_provenance) != {"source", "binding_source", "geo_symbol", "geo_source"}:
                raise ValueError(f"invalid root provenance: {stable_id}")
            expected_paths.add(model_provenance["source"])
            expected_paths.add(model_provenance["binding_source"])
            if model_provenance["geo_source"] is not None:
                expected_paths.add(model_provenance["geo_source"])
        if None in expected_paths or not expected_paths <= source_paths:
            raise ValueError(f"invalid root provenance: {stable_id}")
        behavior_text = (source_root / behavior_provenance["source"]).read_text(encoding="utf-8", errors="ignore")
        if not _defines(behavior_text, behavior_provenance["symbol"]):
            raise ValueError(f"invalid root provenance: {stable_id}")
        variants_by_model = {variant["model"]: variant for variant in variants}
        for model, model_provenance in provenance["models"].items():
            model_text = (source_root / model_provenance["source"]).read_text(encoding="utf-8", errors="ignore")
            binding_text = (source_root / model_provenance["binding_source"]).read_text(encoding="utf-8", errors="ignore")
            variant = variants_by_model[model]
            if not _defines(model_text, model) or model_provenance["geo_symbol"] != variant["geo_root"]:
                raise ValueError(f"invalid root provenance: {stable_id}")
            if model == "MODEL_NONE":
                if (model_provenance["binding_source"] != model_provenance["source"]
                        or model_provenance["geo_source"] is not None or variant["geo_root"] != "none"):
                    raise ValueError(f"invalid root provenance: {stable_id}")
            else:
                geo_source = model_provenance["geo_source"]
                binding = (
                    re.search(r"#define\s+" + re.escape(model) + r"\b[^\n]*//\s*" + re.escape(variant["geo_root"]) + r"\b", binding_text)
                    if model_provenance["binding_source"] == model_provenance["source"]
                    else re.search(r"\bLOAD_MODEL_FROM_(?:GEO|DL)\s*\(\s*" + re.escape(model) + r"\s*,\s*" + re.escape(variant["geo_root"]) + r"\b", binding_text)
                )
                if (not binding or geo_source is None
                        or not _defines((source_root / geo_source).read_text(encoding="utf-8", errors="ignore"), variant["geo_root"])):
                    raise ValueError(f"invalid root provenance: {stable_id}")
        for animation, path in provenance["animation"].items():
            if not _defines((source_root / path).read_text(encoding="utf-8", errors="ignore"), animation):
                raise ValueError(f"invalid root provenance: {stable_id}")
        for field in ("object_roots", "spawned_children", "children", "rewards", "projectiles", "effects", "animation_table", "material_feature_bits", "music_sequence_ids", "sfx_banks", "sfx_ids"):
            values = record.get(field)
            if not isinstance(values, list) or any(not isinstance(value, str) for value in values) or values != sorted(set(values)):
                raise ValueError(f"{field} must be a sorted unique string list: {stable_id}")
    all_paths = {source["path"] for record in document["records"] for source in record["sources"]}
    all_paths.update(document.get("scene_sources", []))
    if set(source_hashes) != all_paths:
        raise ValueError("source hash coverage is incomplete")
    for path in all_paths:
        actual = hashlib.sha256((source_root / path).read_bytes()).hexdigest()
        if source_hashes[path] != actual:
            raise ValueError(f"stale source hash: {path}")
    for record in document["records"]:
        for child in record["spawned_children"]:
            if child not in stable_ids:
                raise ValueError(f"unknown child reference: {record['stable_id']} -> {child}")
        typed = [set(record[field]) for field in ("children", "rewards", "projectiles", "effects")]
        if any(typed[a] & typed[b] for a in range(4) for b in range(a + 1, 4)):
            raise ValueError(f"typed child lists disagree: {record['stable_id']}")
        if set().union(*typed) != set(record["spawned_children"]):
            raise ValueError(f"typed child lists incomplete: {record['stable_id']}")
    for field in ("music_sequence_ids", "sfx_banks", "sfx_ids"):
        values = document.get(field)
        if not isinstance(values, list) or values != sorted(set(values)):
            raise ValueError(f"{field} must be a sorted unique list")
        union = sorted({value for record in document["records"] for value in record[field]})
        if values != union:
            raise ValueError(f"audio union mismatch: {field}")
