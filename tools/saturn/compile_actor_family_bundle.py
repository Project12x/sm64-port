#!/usr/bin/env python3
"""Compile and atomically publish the exact bounded BOB S64F-v3 dependency.

Reference reuse is intentionally in-tree. Family grouping follows
``compile_actor_bank._family_record``/the attested family report, model lookup
uses ``gen_actor_identity_registry``, S64B bytes come solely from
``actor_variant_bank``, S64F bytes solely from ``actor_family_bundle``, and the
no-clobber report-last publication pattern follows the scene-package tooling.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path
from typing import Mapping

from actor_bank_format import validate_actor_bank
from actor_family_bundle import (
    FAMILY_FLAG_GEOMETRY,
    FAMILY_FLAG_SUPPORTED,
    BundleDocument,
    FamilyDocument,
    VariantKey,
    embedded_bank_payloads,
    pack_bundle,
    validate_bundle,
)
from actor_material_v2 import BOB_DIRECT_TEXTURED_KEYS
from actor_variant_bank import (
    ActorVariantError,
    compile_actor_variant,
)
from gen_actor_identity_registry import closure_family_key, parse_model_ids
from hermetic_manifest import normalize_repo_path
from inventory_actor_family_bundles import AUTHORITATIVE_LIMITS, prove_resource_inventory
from compile_actor_bank import compile_actor_family_banks
from target_profile import PACKAGE_CLASSES, resolve_target_profile


PROFILE = "tools/saturn/profiles/sourceboot-bob-demo-v1.json"
BUNDLE_NAME = "bob-area1-actors-v3.s64f"
DEPENDENCY_NAME = "bob-area1-actors-v3-dependency.json"
HEADER_NAME = "actor_bundle_capacity.h"
REPORT_NAME = "actor-family-bundle.json"


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _load_object(path: Path, label: str) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid {label}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def _family_records(closure: Mapping[str, object]) -> dict[str, list[dict[str, object]]]:
    records = closure.get("records")
    if not isinstance(records, list) or not records:
        raise ValueError("scene closure has no records")
    grouped: dict[str, list[dict[str, object]]] = {}
    for raw in records:
        if not isinstance(raw, dict):
            raise ValueError("scene closure record is not an object")
        grouped.setdefault(closure_family_key(raw), []).append(raw)
    return grouped


def _package_class_bytes(root: Path) -> tuple[tuple[str, ...], dict[str, int]]:
    profile = _load_object(root / PROFILE, "target profile")
    configuration = profile.get("release_config")
    if not isinstance(configuration, dict):
        raise ValueError("target profile release_config is invalid")
    with tempfile.TemporaryDirectory(prefix="actor-bundle-profile-") as temporary:
        resolved = resolve_target_profile(
            root, root / PROFILE, configuration, Path(temporary), mode="development")
        package_rows = resolved.package_set_document.get("packages")
        if not isinstance(package_rows, list):
            raise ValueError("target profile package set is invalid")
        ownership = [str(row.get("package_class")) for row in package_rows
                     if isinstance(row, dict)]
        if tuple(sorted(ownership, key=PACKAGE_CLASSES.index)) != PACKAGE_CLASSES or \
                len(ownership) != len(PACKAGE_CLASSES):
            raise ValueError("target profile must own exactly one package per package class")
        result: dict[str, int] = {}
        for kind in PACKAGE_CLASSES:
            aggregate = _load_object(resolved.package_class_manifests[kind],
                                     f"{kind} package-class manifest")
            packages = aggregate.get("packages")
            if not isinstance(packages, list) or len(packages) != 1:
                raise ValueError("target profile must own exactly one package per package class")
            inputs = packages[0].get("inputs") if isinstance(packages[0], dict) else None
            if not isinstance(inputs, list) or not inputs:
                raise ValueError("target profile package inputs are invalid")
            result[kind] = sum((root / str(item["path"])).stat().st_size
                               for item in inputs if isinstance(item, dict))
        return PACKAGE_CLASSES, result


def _repo_input(root: Path, path: Path, label: str) -> Path:
    try:
        return root / normalize_repo_path(root, path)
    except ValueError as error:
        raise ValueError(f"{label} must be a repository path") from error


def _reconcile_family_report(
    root: Path, closure_path: Path, supplied_path: Path, package_generation: int,
) -> dict[str, object]:
    supplied = _load_object(supplied_path, "family report")
    with tempfile.TemporaryDirectory(prefix="actor-family-reconcile-") as temporary:
        authoritative = compile_actor_family_banks(
            root, closure_path, Path(temporary), package_generation)
    supplied_semantics = dict(supplied)
    authoritative_semantics = dict(authoritative)
    supplied_semantics.pop("payload", None)
    authoritative_semantics.pop("payload", None)
    if supplied_semantics != authoritative_semantics:
        raise ValueError("family report does not match closure-derived semantics")
    return authoritative_semantics


def _scene_package_bytes(root: Path) -> int:
    report = _load_object(
        root / "build/saturn/packages/bob/1/provisional/scene-package-report.json",
        "scene package report")
    size = report.get("package_size")
    if type(size) is not int or size <= 0:
        raise ValueError("scene package report has invalid package_size")
    return size


def _capacity_header(report: Mapping[str, object]) -> bytes:
    inventory = report["resource_inventory"]
    shares = inventory["actor_shares"]
    lines = (
        "#ifndef SM64_SATURN_ACTOR_BUNDLE_CAPACITY_H",
        "#define SM64_SATURN_ACTOR_BUNDLE_CAPACITY_H",
        f"#define SM64_SATURN_ACTOR_BUNDLE_FAMILY_COUNT {report['family_count']}U",
        f"#define SM64_SATURN_ACTOR_BUNDLE_VARIANT_COUNT {report['supported_variant_count']}U",
        f"#define SM64_SATURN_ACTOR_BUNDLE_WORKSPACE_BYTES {inventory['workspace']['capacity_bytes']}U",
        f"#define SM64_SATURN_ACTOR_OUTPUT_SHARE {shares['output_records']}U",
        f"#define SM64_SATURN_ACTOR_COMMAND_SHARE {shares['texture_commands']}U",
        f"#define SM64_SATURN_ACTOR_GOURAUD_SHARE {shares['gouraud_tables']}U",
        f"#define SM64_SATURN_ACTOR_GUARANTEED_ANY_MIX {inventory['guaranteed_any_mix_count']}U",
        "#endif",
        "",
    )
    return "\n".join(lines).encode("ascii")


def _publish(output_dir: Path, files: Mapping[str, bytes], order: tuple[str, ...]) -> None:
    output_dir = Path(output_dir)
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    targets = tuple(output_dir / name for name in order)
    if any(path.exists() for path in targets):
        raise ValueError("publication target exists")
    stage = Path(tempfile.mkdtemp(prefix=".actor-bundle-stage-", dir=output_dir.parent))
    published: list[Path] = []
    try:
        for name in order:
            (stage / name).write_bytes(files[name])
        output_dir.mkdir(parents=True, exist_ok=True)
        for name, target in zip(order, targets):
            try:
                os.link(stage / name, target)
            except FileExistsError as error:
                raise ValueError("publication target exists") from error
            published.append(target)
    except Exception:
        for target in reversed(published):
            target.unlink(missing_ok=True)
        raise
    finally:
        shutil.rmtree(stage)


def _require_fresh_publication(output_dir: Path) -> None:
    targets = tuple(Path(output_dir) / name for name in (
        BUNDLE_NAME, DEPENDENCY_NAME, HEADER_NAME, REPORT_NAME))
    if any(path.exists() for path in targets):
        raise ValueError("publication target exists")


def validate_publication(
    output_dir: Path, *, root: Path | None = None, closure_path: Path | None = None,
    family_report_path: Path | None = None, model_ids_path: Path | None = None,
    package_generation: int | None = None,
) -> dict[str, object]:
    """Validate all four published sidecars and current resource semantics."""
    output_dir = Path(output_dir)
    expected_order = (BUNDLE_NAME, DEPENDENCY_NAME, HEADER_NAME, REPORT_NAME)
    paths = {name: output_dir / name for name in expected_order}
    if any(not path.is_file() for path in paths.values()):
        raise ValueError("actor family bundle publication is incomplete")
    report_bytes = paths[REPORT_NAME].read_bytes()
    report = _load_object(paths[REPORT_NAME], "actor family bundle report")
    if report_bytes != _json_bytes(report) or \
            tuple(report.get("publication_order", ())) != expected_order or \
            report.get("outputs") != {
                "bundle": BUNDLE_NAME, "dependency": DEPENDENCY_NAME,
                "header": HEADER_NAME, "report": REPORT_NAME,
            }:
        raise ValueError("actor family bundle report is noncanonical")
    payload = paths[BUNDLE_NAME].read_bytes()
    bundle = validate_bundle(payload)
    dependency = _load_object(paths[DEPENDENCY_NAME], "actor dependency")
    if paths[DEPENDENCY_NAME].read_bytes() != _json_bytes(dependency) or \
            dependency != report.get("actor_dependency") or \
            dependency.get("byte_count") != len(payload) or \
            dependency.get("sha256") != hashlib.sha256(payload).hexdigest() or \
            dependency.get("generation") != bundle.package_generation:
        raise ValueError("actor dependency sidecar mismatch")
    if paths[HEADER_NAME].read_bytes() != _capacity_header(report):
        raise ValueError("actor capacity header sidecar mismatch")
    rows = report.get("banks")
    totals = report.get("bundle_totals")
    inventory = report.get("resource_inventory")
    if not isinstance(rows, list) or not isinstance(totals, dict) or \
            not isinstance(inventory, dict):
        raise ValueError("actor family bundle report inventory is invalid")
    bank_payloads = embedded_bank_payloads(bundle)
    bank_views = tuple(validate_actor_bank(raw) for raw in bank_payloads)
    measured_totals = {
        "payload_bytes": len(payload),
        "embedded_s64b_bytes": sum(len(raw) for raw in bank_payloads),
        "texture_bytes": sum(view.texture_payload_size for view in bank_views),
        "clut_bytes": sum(view.clut_payload_size for view in bank_views),
        "workspace_lane_stride": bundle.workspace_lane_stride,
        "workspace_bytes": bundle.maximum_scratch,
    }
    if report.get("family_count") != bundle.family_count or \
            report.get("supported_variant_count") != bundle.variant_count or \
            totals != measured_totals:
        raise ValueError("actor family bundle report totals mismatch")
    costs = tuple((int(row["family_ordinal"]), int(row["model_id"]),
                   int(row["maximum_live_instances"]),
                   int(row["draw_records_per_instance"]),
                   int(row["texture_commands_per_instance"]),
                   int(row["gouraud_tables_per_instance"])) for row in rows)
    package_bytes = inventory.get("package_class_source_bytes")
    cart = inventory.get("cart")
    if not isinstance(package_bytes, dict) or not isinstance(cart, dict):
        raise ValueError("actor family bundle package inventory is invalid")
    if set(package_bytes) != set(AUTHORITATIVE_LIMITS.package_classes):
        raise ValueError("actor family bundle package inventory classes mismatch")
    ordered_package_bytes = {
        kind: int(package_bytes[kind]) for kind in AUTHORITATIVE_LIMITS.package_classes}
    recomputed = prove_resource_inventory(
        costs, texture_bytes=int(totals["texture_bytes"]),
        clut_bytes=int(totals["clut_bytes"]), bundle_bytes=len(payload),
        workspace_bytes=int(totals["workspace_bytes"]),
        package_class_bytes=ordered_package_bytes,
        scene_package_bytes=int(cart["scene_package_bytes"]),
    )
    if recomputed != inventory:
        raise ValueError("actor family bundle report resource inventory is stale")
    current_inputs = (root, closure_path, family_report_path, model_ids_path,
                      package_generation)
    if any(item is not None for item in current_inputs):
        if any(item is None for item in current_inputs):
            raise ValueError("current publication validation inputs are incomplete")
        with tempfile.TemporaryDirectory(prefix="actor-bundle-current-") as temporary:
            current = Path(temporary) / "publication"
            compile_scene_bundle(
                Path(root), Path(closure_path), Path(family_report_path),
                Path(model_ids_path), int(package_generation), current)
            for name in expected_order:
                if paths[name].read_bytes() != (current / name).read_bytes():
                    raise ValueError(
                        "actor family bundle publication is stale for current inputs")
    return report


def compile_scene_bundle(
    root: Path, closure_path: Path, family_report_path: Path,
    model_ids_path: Path, package_generation: int, output_dir: Path,
) -> dict[str, object]:
    """Build the real BOB bundle and publish all artifacts report-last."""
    root = Path(root).resolve()
    _require_fresh_publication(Path(output_dir))
    if type(package_generation) is not int or not 0 < package_generation <= 0xFFFFFFFF:
        raise ValueError("package generation must be a nonzero uint32")
    closure_path = _repo_input(root, Path(closure_path), "scene closure")
    family_report_path = _repo_input(root, Path(family_report_path), "family report")
    model_ids_path = _repo_input(root, Path(model_ids_path), "model IDs")
    closure = _load_object(closure_path, "scene closure")
    family_report = _reconcile_family_report(
        root, closure_path, family_report_path, package_generation)
    if closure.get("schema") != "sm64-saturn-scene-closure-v1" or \
            family_report.get("schema") != "sm64-saturn-actor-family-bank-v2":
        raise ValueError("closure/family report schema mismatch")
    families = family_report.get("families")
    if not isinstance(families, list) or len(families) != 47:
        raise ValueError("real BOB family report must contain exactly 47 families")
    grouped = _family_records(closure)
    model_ids = parse_model_ids(model_ids_path.read_text(encoding="utf-8"))
    supported = set(BOB_DIRECT_TEXTURED_KEYS)
    banks: dict[VariantKey, bytes] = {}
    bank_rows: list[dict[str, object]] = []
    family_documents: list[FamilyDocument] = []
    unsupported_rows: list[dict[str, object]] = []
    model_none_count = sum(
        1 for family in families if isinstance(family, dict)
        for variant in family.get("model_variants", ())
        if isinstance(variant, dict) and variant.get("model") == "MODEL_NONE")

    for ordinal, raw_family in enumerate(families, 1):
        if not isinstance(raw_family, dict):
            raise ValueError("family report row is invalid")
        family_key = str(raw_family.get("family_key", ""))
        records = grouped.get(family_key)
        if not records:
            raise ValueError("family report row is absent from closure")
        model_name = str(raw_family.get("model", "MODEL_NONE"))
        model_id = model_ids.get(model_name, 0) if model_name != "MODEL_NONE" else 0
        key = (ordinal, model_id)
        selected = key in supported
        unsupported_reason: str | None = None
        compiled = None
        if selected:
            compiled = compile_actor_variant(root, ordinal, model_id, records)
            bank = validate_actor_bank(compiled.payload)
            if bank.version != 2:
                raise ValueError("supported real BOB bank is not S64B v2")
            banks[VariantKey(*key)] = compiled.payload
            resources = compiled.report.get("resources")
            if not isinstance(resources, dict):
                raise ValueError("supported bank lacks v2 resource credits")
            bank_rows.append({
                "family_ordinal": ordinal, "model_id": model_id,
                "model": model_name, "stable_id": raw_family["stable_id"],
                "version": bank.version, "payload_sha256": compiled.payload_sha256,
                "source_sha256": compiled.source_sha256,
                "payload_bytes": len(compiled.payload),
                "maximum_live_instances": int(raw_family["maximum_live_instances"]),
                "lane_bytes": compiled.lane_bytes,
                "maximum_scratch": compiled.maximum_scratch,
                **{name: int(resources[name]) for name in (
                    "texture_resident_bytes", "clut_resident_bytes",
                    "draw_records_per_instance", "texture_commands_per_instance",
                    "gouraud_tables_per_instance")},
            })
        if bool(raw_family.get("supported")):
            variants = raw_family.get("model_variants")
            if not isinstance(variants, list):
                raise ValueError("family model variants are invalid")
            for variant in variants:
                if not isinstance(variant, dict):
                    raise ValueError("family model variant is invalid")
                candidate_name = str(variant.get("model", "MODEL_NONE"))
                if candidate_name == "MODEL_NONE":
                    continue
                candidate_id = model_ids.get(candidate_name, 0)
                if (ordinal, candidate_id) in supported:
                    continue
                try:
                    compile_actor_variant(root, ordinal, candidate_id, records)
                except ActorVariantError as error:
                    reason = str(error)
                else:
                    reason = "outside exact measured BOB material subset"
                unsupported_rows.append({
                    "family_ordinal": ordinal, "model_id": candidate_id,
                    "model": candidate_name, "stable_id": raw_family["stable_id"],
                    "reason": reason,
                })
                if unsupported_reason is None:
                    unsupported_reason = reason
        if not selected and unsupported_reason is None:
            reasons = raw_family.get("unsupported")
            unsupported_reason = ",".join(str(item) for item in reasons) \
                if isinstance(reasons, list) and reasons else "non-drawable/capability family"

        metadata = {name: raw_family.get(name) for name in (
            "family_key", "model", "geo_root", "geo_source", "model_variants",
            "capabilities", "runtime_capabilities", "required_capabilities")}
        family_documents.append(FamilyDocument(
            stable_family_hash=int(raw_family["family_id"]),
            capability_mask=int(raw_family["capability_mask"]),
            runtime_capability_mask=int(raw_family["runtime_capability_mask"]),
            maximum_live_instances=int(raw_family["maximum_live_instances"]),
            source_actor_count=int(raw_family["actor_count"]),
            flags=(FAMILY_FLAG_SUPPORTED | FAMILY_FLAG_GEOMETRY) if selected else 0,
            name=str(raw_family["stable_id"]).encode(),
            source=_json_bytes(raw_family["sources"]),
            unsupported=b"" if selected else str(unsupported_reason).encode(),
            metadata_blob=_json_bytes(metadata),
            model_ids=(model_id,) if selected else (),
        ))

    if set(banks) != {VariantKey(*item) for item in supported}:
        raise ValueError("supported BOB key inventory mismatch")
    payload = pack_bundle(BundleDocument(package_generation, family_documents), banks)
    bundle = validate_bundle(payload)
    parsed_once = tuple(validate_actor_bank(raw) for raw in embedded_bank_payloads(bundle))
    if len(parsed_once) != len(supported):
        raise ValueError("supported BOB bank count mismatch")
    texture_bytes = sum(row["texture_resident_bytes"] for row in bank_rows)
    clut_bytes = sum(row["clut_resident_bytes"] for row in bank_rows)
    embedded_bytes = sum(row["payload_bytes"] for row in bank_rows)
    classes, package_bytes = _package_class_bytes(root)
    costs = tuple((row["family_ordinal"], row["model_id"],
                   row["maximum_live_instances"], row["draw_records_per_instance"],
                   row["texture_commands_per_instance"],
                   row["gouraud_tables_per_instance"]) for row in bank_rows)
    inventory = prove_resource_inventory(
        costs, texture_bytes=texture_bytes, clut_bytes=clut_bytes,
        bundle_bytes=len(payload), workspace_bytes=bundle.maximum_scratch,
        package_class_bytes=package_bytes,
        scene_package_bytes=_scene_package_bytes(root),
    )
    dependency = {
        "kind": "ACTOR_DEPENDENCIES", "stable_id": "bob-area1-actors-v3",
        "generation": package_generation, "destination_class": "CART",
        "lifetime": "SCENE", "alignment": 4, "max_scratch": 0,
        "byte_count": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
    }
    report: dict[str, object] = {
        "schema": "sm64-saturn-actor-family-bundle-build-v1",
        "scene": {"level": closure.get("level"), "area": closure.get("area")},
        "package_generation": package_generation,
        "family_count": len(family_documents),
        "supported_variant_count": len(bank_rows),
        "unsupported_drawable_count": len(unsupported_rows),
        "model_none_count": model_none_count,
        "package_classes": list(classes),
        "bundle_totals": {
            "payload_bytes": len(payload), "embedded_s64b_bytes": embedded_bytes,
            "texture_bytes": texture_bytes, "clut_bytes": clut_bytes,
            "workspace_lane_stride": bundle.workspace_lane_stride,
            "workspace_bytes": bundle.maximum_scratch,
        },
        "banks": bank_rows,
        "unsupported_drawable": unsupported_rows,
        "resource_inventory": inventory,
        "actor_dependency": dependency,
        "provenance": {
            "reuse_mode": "same-project close-port/orchestration",
            "repository": "sm64-saturn-port",
            "license": "GPL-2.0-only",
            "files": [
                {"path": "tools/saturn/actor_family_bundle.py", "commit": "68ceec9c66070d48d05d6442e11c76e2b9b6b903"},
                {"path": "tools/saturn/actor_variant_bank.py", "commit": "86de51cc80914d1854a77859a73d34d17df01a98"},
                {"path": "tools/saturn/compile_actor_bank.py", "commit": "83cfc1add2d77c8f919ea353c4692c6a31d0256f"},
                {"path": "tools/saturn/compile_scene_package.py", "commit": "a3e228425bf9963c4d8534c76142f61920ae75ae"},
                {"path": "tools/saturn/target_profile.py", "commit": "b345dd1537c9fd0f68de6edf80d50f5a271f0c32"},
            ],
        },
        "outputs": {"bundle": BUNDLE_NAME, "dependency": DEPENDENCY_NAME,
                    "header": HEADER_NAME, "report": REPORT_NAME},
        "publication_order": [BUNDLE_NAME, DEPENDENCY_NAME, HEADER_NAME, REPORT_NAME],
    }
    order = tuple(report["publication_order"])
    files = {BUNDLE_NAME: payload, DEPENDENCY_NAME: _json_bytes(dependency),
             HEADER_NAME: _capacity_header(report), REPORT_NAME: _json_bytes(report)}
    _publish(Path(output_dir), files, order)
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--closure", required=True, type=Path)
    parser.add_argument("--family-report", required=True, type=Path)
    parser.add_argument("--model-ids", required=True, type=Path)
    parser.add_argument("--package-generation", required=True, type=int)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--verify-publication", action="store_true")
    args = parser.parse_args()
    if args.verify_publication:
        validate_publication(
            args.output_dir, root=args.root, closure_path=args.closure,
            family_report_path=args.family_report, model_ids_path=args.model_ids,
            package_generation=args.package_generation)
        return 0
    compile_scene_bundle(args.root, args.closure, args.family_report,
                         args.model_ids, args.package_generation, args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
