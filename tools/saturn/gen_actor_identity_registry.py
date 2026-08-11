#!/usr/bin/env python3
"""Generate the source-observer actor identity registry from Task 11 outputs.

The runtime key is the exact behavior script plus the model ID resolved from
``sharedChild`` through ``gLoadedGraphNodes[]``.  That is the pointer-safe
runtime spelling of the closure's (geo layout, behavior script) identity:
model ID alone is insufficient because several BOB behaviors share geometry.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


MODEL_DEFINE_RE = re.compile(
    r"#define\s+(MODEL_[A-Z0-9_]+)\s+(0[xX][0-9A-Fa-f]+|\d+)"
)
C_IDENTIFIER_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


@dataclass(frozen=True)
class RegistryRow:
    model_id: int
    model_name: str
    geo_symbol: str
    behavior_symbol: str
    family_id: int
    actor_bank_id: int
    actor_bank_hash_words: tuple[int, ...]
    scene_package_generation: int


def canonical_json(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def parse_model_ids(text: str) -> dict[str, int]:
    result = {name: int(value, 0) for name, value in MODEL_DEFINE_RE.findall(text)}
    if not result:
        raise ValueError("model ID header contains no numeric MODEL_* definitions")
    return result


def sha256_words(digest_hex: str) -> tuple[int, ...]:
    try:
        digest = bytes.fromhex(digest_hex)
    except ValueError as error:
        raise ValueError("payload SHA-256 is not hexadecimal") from error
    if len(digest) != 32:
        raise ValueError("payload SHA-256 must contain exactly 32 bytes")
    return tuple(
        int.from_bytes(digest[offset:offset + 4], "big")
        for offset in range(0, 32, 4)
    )


def closure_family_key(record: dict) -> str:
    model = str(record.get("model", "MODEL_NONE"))
    provenance = record.get("root_provenance")
    if not isinstance(provenance, dict):
        raise ValueError(f"closure record {record.get('stable_id')!r} has no provenance")
    models = provenance.get("models")
    binding = models.get(model) if isinstance(models, dict) else None
    if not isinstance(binding, dict):
        raise ValueError(
            f"closure record {record.get('stable_id')!r} has no primary model provenance"
        )
    variants = record.get("model_variants")
    if not isinstance(variants, list) or not variants:
        raise ValueError(f"closure record {record.get('stable_id')!r} has no model variants")
    key = {
        "model": model,
        "geo_source": binding.get("geo_source"),
        "geo_root": str(record.get("geo_root", "none")),
        "animation_table": sorted(record.get("animation_table", [])),
        "model_variants": sorted(variants, key=canonical_json),
    }
    return canonical_json(key)


def _validated_inputs(report: dict, closure: dict, payload: bytes) -> None:
    if report.get("schema") != "sm64-saturn-actor-family-bank-v2":
        raise ValueError("family report schema is not sm64-saturn-actor-family-bank-v2")
    if closure.get("schema") != "sm64-saturn-scene-closure-v1":
        raise ValueError("closure schema is not sm64-saturn-scene-closure-v1")
    families = report.get("families")
    records = closure.get("records")
    if not isinstance(families, list) or not families:
        raise ValueError("family report contains no families")
    if not isinstance(records, list) or not records:
        raise ValueError("scene closure contains no records")
    if report.get("family_count") != len(families):
        raise ValueError("family report count is stale")
    if report.get("closure_record_count") != len(records):
        raise ValueError("family report closure count is stale")
    scene = report.get("scene")
    if not isinstance(scene, dict) or (
        scene.get("level") != closure.get("level") or
        scene.get("area") != closure.get("area")
    ):
        raise ValueError("family report scene does not match closure")
    if report.get("payload_size") != len(payload):
        raise ValueError("family bank payload size does not match report")
    actual_digest = hashlib.sha256(payload).hexdigest()
    if report.get("payload_sha256") != actual_digest:
        raise ValueError("family bank payload SHA-256 does not match report")
    ordering = [(int(item["family_id"]), str(item["family_key"])) for item in families]
    if ordering != sorted(ordering) or len(ordering) != len(set(ordering)):
        raise ValueError("family report order is not canonical and unique")


def build_registry_rows(
    report: dict,
    closure: dict,
    model_ids: dict[str, int],
    scene_package_generation: int,
) -> list[RegistryRow]:
    if not 0 < scene_package_generation <= 0xFFFFFFFF:
        raise ValueError("scene generation must be a nonzero uint32")
    hash_words = sha256_words(str(report["payload_sha256"]))
    actor_bank_id = hash_words[0]
    if actor_bank_id == 0:
        raise ValueError("family bank digest produces reserved zero actor bank ID")

    families_by_key: dict[str, tuple[int, dict]] = {}
    for ordinal, family in enumerate(report["families"], start=1):
        key = str(family["family_key"])
        if key in families_by_key:
            raise ValueError("family report contains duplicate family keys")
        if ordinal > 0xFFFF:
            raise ValueError("family record ordinal does not fit observer ABI")
        families_by_key[key] = (ordinal, family)

    rows: dict[tuple[int, str], RegistryRow] = {}
    for record in closure["records"]:
        key = closure_family_key(record)
        resolved = families_by_key.get(key)
        if resolved is None:
            raise ValueError(
                f"closure record {record.get('stable_id')!r} is absent from family report"
            )
        family_ordinal, family = resolved
        if not family.get("supported", False):
            continue
        behavior = str(record.get("behavior_root", ""))
        provenance = record.get("root_provenance", {})
        behavior_provenance = provenance.get("behavior", {})
        if (
            not C_IDENTIFIER_RE.fullmatch(behavior) or
            not isinstance(behavior_provenance, dict) or
            behavior_provenance.get("symbol") != behavior
        ):
            raise ValueError(f"closure behavior provenance is invalid for {behavior!r}")
        model_provenance = provenance.get("models", {})
        for variant in record["model_variants"]:
            model_name = str(variant.get("model", "MODEL_NONE"))
            geo_symbol = str(variant.get("geo_root", "none"))
            if model_name == "MODEL_NONE" or geo_symbol == "none":
                continue
            if model_name not in model_ids:
                raise ValueError(f"closure references unknown model {model_name!r}")
            model_id = model_ids[model_name]
            if not 0 < model_id <= 0xFFFF:
                raise ValueError(f"model ID for {model_name!r} is reserved or out of range")
            binding = model_provenance.get(model_name)
            if (
                not isinstance(binding, dict) or
                binding.get("geo_symbol") != geo_symbol or
                not C_IDENTIFIER_RE.fullmatch(geo_symbol)
            ):
                raise ValueError(
                    f"closure geo provenance is invalid for {behavior!r}/{model_name!r}"
                )
            row = RegistryRow(
                model_id=model_id,
                model_name=model_name,
                geo_symbol=geo_symbol,
                behavior_symbol=behavior,
                family_id=family_ordinal,
                actor_bank_id=actor_bank_id,
                actor_bank_hash_words=hash_words,
                scene_package_generation=scene_package_generation,
            )
            row_key = (model_id, behavior)
            prior = rows.get(row_key)
            if prior is not None and prior != row:
                raise ValueError(f"conflicting registry identity for {row_key!r}")
            rows[row_key] = row
    ordered = sorted(rows.values(), key=lambda row: (row.model_id, row.behavior_symbol))
    if not ordered:
        raise ValueError("actor identity registry has no supported drawable rows")
    return ordered


def build_header(rows: Iterable[RegistryRow]) -> str:
    ordered = list(rows)
    lines = [
        "/* Generated by tools/saturn/gen_actor_identity_registry.py. Do not edit. */",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        '#include "behavior_data.h"',
        "",
        "typedef struct saturn_actor_identity_registry_entry {",
        "    uint16_t model_id;",
        "    uint16_t family_id; /* 1-based S64F record ordinal; zero is absent. */",
        "    const BehaviorScript *behavior;",
        "    uint32_t actor_bank_id;",
        "    uint32_t actor_bank_hash_words[8];",
        "    uint32_t scene_package_generation;",
        "} saturn_actor_identity_registry_entry_t;",
        "",
        f"#define SATURN_ACTOR_IDENTITY_REGISTRY_COUNT {len(ordered)}U",
        "",
        "/* Sorted by numeric model ID, then behavior symbol. The model ID is",
        " * resolved from sharedChild through gLoadedGraphNodes[] at the seam;",
        " * behavior equality disambiguates geometry shared by multiple actors. */",
        "static const saturn_actor_identity_registry_entry_t",
        "saturn_actor_identity_registry_table[SATURN_ACTOR_IDENTITY_REGISTRY_COUNT] = {",
    ]
    for row in ordered:
        words = ", ".join(f"0x{word:08X}U" for word in row.actor_bank_hash_words)
        lines.extend([
            "    { .model_id = 0x%04XU, .family_id = %dU," % (
                row.model_id, row.family_id,
            ),
            "      .behavior = %s, .actor_bank_id = 0x%08XU," % (
                row.behavior_symbol, row.actor_bank_id,
            ),
            f"      .actor_bank_hash_words = {{ {words} }},",
            "      .scene_package_generation = %dU }, /* %s / %s */" % (
                row.scene_package_generation, row.geo_symbol, row.model_name,
            ),
        ])
    lines.extend([
        "};",
        "",
        "static inline const saturn_actor_identity_registry_entry_t *",
        "saturn_actor_identity_registry_lookup(uint16_t model_id,",
        "                                      const BehaviorScript *behavior)",
        "{",
        "    size_t lo = 0U;",
        "    size_t hi = SATURN_ACTOR_IDENTITY_REGISTRY_COUNT;",
        "    if (model_id == 0U || behavior == NULL) return NULL;",
        "    while (lo < hi) {",
        "        const size_t mid = lo + (hi - lo) / 2U;",
        "        if (saturn_actor_identity_registry_table[mid].model_id < model_id)",
        "            lo = mid + 1U;",
        "        else",
        "            hi = mid;",
        "    }",
        "    while (lo < SATURN_ACTOR_IDENTITY_REGISTRY_COUNT &&",
        "           saturn_actor_identity_registry_table[lo].model_id == model_id) {",
        "        if (saturn_actor_identity_registry_table[lo].behavior == behavior)",
        "            return &saturn_actor_identity_registry_table[lo];",
        "        lo++;",
        "    }",
        "    return NULL;",
        "}",
        "",
    ])
    return "\n".join(lines)


def generate(
    family_report_path: Path,
    closure_path: Path,
    model_ids_path: Path,
    scene_package_generation: int,
) -> str:
    report = json.loads(family_report_path.read_text(encoding="utf-8"))
    closure = json.loads(closure_path.read_text(encoding="utf-8"))
    payload_path = Path(str(report.get("payload", "")))
    if not payload_path.is_absolute():
        payload_path = family_report_path.parent / payload_path
    payload = payload_path.read_bytes()
    _validated_inputs(report, closure, payload)
    model_ids = parse_model_ids(model_ids_path.read_text(encoding="utf-8"))
    rows = build_registry_rows(report, closure, model_ids, scene_package_generation)
    return build_header(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--family-report", type=Path, required=True)
    parser.add_argument("--closure", type=Path, required=True)
    parser.add_argument("--model-ids", type=Path, required=True)
    parser.add_argument("--scene-generation", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    header = generate(
        args.family_report,
        args.closure,
        args.model_ids,
        args.scene_generation,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
