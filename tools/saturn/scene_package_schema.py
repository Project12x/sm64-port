#!/usr/bin/env python3
"""Validation for version-one, source-derived scene-closure documents."""
from __future__ import annotations

import hashlib
from pathlib import Path

SCHEMA = "sm64-saturn-scene-closure-v1"
TOP_FIELDS = {"schema", "source_root", "level", "area", "records", "scene_sources", "source_hashes", "music_sequence_ids", "sfx_banks", "sfx_ids"}
RECORD_FIELDS = {"stable_id", "level", "area", "act_mask", "object_roots", "model", "geo_root", "behavior_root", "spawned_children", "rewards", "projectiles", "effects", "animation_table", "material_feature_bits", "maximum_live_instances", "music_sequence_ids", "sfx_banks", "sfx_ids", "sources"}


def validate_scene_closure(document: dict) -> None:
    unknown = set(document) - TOP_FIELDS
    if unknown:
        raise ValueError(f"unknown field: {sorted(unknown)[0]}")
    if document.get("schema") != SCHEMA:
        raise ValueError("unsupported scene closure schema")
    if not isinstance(document.get("records"), list):
        raise ValueError("records must be a list")
    stable_ids: set[str] = set()
    source_root = Path(document["source_root"])
    source_hashes = document.get("source_hashes", {})
    if not isinstance(source_hashes, dict):
        raise ValueError("source_hashes must be an object")
    for record in document["records"]:
        unknown = set(record) - RECORD_FIELDS
        if unknown:
            raise ValueError(f"unknown record field: {sorted(unknown)[0]}")
        stable_id = record.get("stable_id")
        if not stable_id or stable_id in stable_ids:
            raise ValueError(f"duplicate stable_id: {stable_id}")
        stable_ids.add(stable_id)
        if not record.get("sources"):
            raise ValueError(f"record {stable_id} has no sources")
        if record.get("maximum_live_instances", -1) < 0:
            raise ValueError(f"invalid maximum_live_instances: {stable_id}")
        for source in record["sources"]:
            path = source.get("path")
            digest = source.get("sha256")
            if not path or not digest:
                raise ValueError(f"missing source hash: {stable_id}")
            actual = hashlib.sha256((source_root / path).read_bytes()).hexdigest()
            if actual != digest or source_hashes.get(path) != digest:
                raise ValueError(f"stale source hash: {path}")
    all_paths = {source["path"] for record in document["records"] for source in record["sources"]}
    all_paths.update(document.get("scene_sources", []))
    if set(source_hashes) != all_paths:
        raise ValueError("source hash coverage is incomplete")
    for path in all_paths:
        actual = hashlib.sha256((source_root / path).read_bytes()).hexdigest()
        if source_hashes[path] != actual:
            raise ValueError(f"stale source hash: {path}")
