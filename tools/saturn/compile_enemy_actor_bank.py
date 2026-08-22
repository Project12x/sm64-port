#!/usr/bin/env python3
"""Emit and budget-check the first non-Mario actor bank.

`r2-rigid-part-evaluator.md` section 8.1 named a Bob-omb or Goomba `.s64b`
bake as blocker 1 of 3 and recorded that ``mario.s64b`` is the only bank in
the tree.  That is true of the standalone bank *files* and false of the
shipped package: ``bob-area1-actors-v3.s64f`` already carries a validated
S64B v2 bank for ``MODEL_BLACK_BOBOMB``.

This tool does not author a new bake.  It recompiles the same variant through
the same generic producer (``actor_variant_bank.compile_actor_variant``, the
one ``compile_actor_family_bundle.py`` calls), writes it out as a standalone
``.s64b`` for host verification, and then does the two things the published
bundle report cannot do on its own:

1. enforces the declared Saturn budget in
   ``tools/saturn/manifests/actors/<actor>.json`` -- part count against the
   shared 20-matrix joint workspace, rigid pose bytes against the 640 B cold
   class, primitives / texture / CLUT residency against the declared ceilings;
2. proves the emitted payload is **byte-identical** to the bank already
   published inside the scene package, so the standalone file is a view of
   what ships rather than a second, divergent bake.

The standalone file is deliberately **not** linked into the ELF: the bank is
already resident in CART inside the actor bundle, so the first enemy bank
costs zero additional cart bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from actor_bank_format import validate_actor_bank  # noqa: E402
from actor_variant_bank import compile_actor_variant  # noqa: E402
from gen_actor_identity_registry import closure_family_key  # noqa: E402


class EnemyBankError(ValueError):
    """The bake does not satisfy its declared Saturn budget."""


def _load(path: Path, label: str) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise EnemyBankError(f"invalid {label}: {error}") from error
    if not isinstance(value, dict):
        raise EnemyBankError(f"{label} must be an object")
    return value


def _records(closure: dict, family_report: dict, family_ordinal: int) -> tuple[list, dict]:
    families = family_report.get("families")
    if not isinstance(families, list) or not 0 < family_ordinal <= len(families):
        raise EnemyBankError("family ordinal is outside the attested family report")
    family = families[family_ordinal - 1]
    grouped: dict[str, list] = {}
    for record in closure.get("records", ()):
        grouped.setdefault(closure_family_key(record), []).append(record)
    records = grouped.get(str(family.get("family_key", "")))
    if not records:
        raise EnemyBankError("family report row is absent from the scene closure")
    return records, family


def _require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def compile_enemy_bank(root: Path, manifest_path: Path,
                       closure_path: Path, family_report_path: Path) -> dict:
    manifest = _load(manifest_path, "actor manifest")
    closure = _load(closure_path, "scene closure")
    family_report = _load(family_report_path, "actor family report")
    if manifest.get("schema") != "sm64-saturn-actor-source-v1":
        raise EnemyBankError("actor manifest schema mismatch")

    family_ordinal = int(manifest["family_ordinal"])
    model_id = int(manifest["model_id"])
    records, family = _records(closure, family_report, family_ordinal)
    if str(family.get("model")) != str(manifest["model"]):
        raise EnemyBankError("family report model does not match the manifest")

    compiled = compile_actor_variant(root, family_ordinal, model_id, records)
    report = compiled.report
    bank = validate_actor_bank(compiled.payload)
    if bank.version != 2:
        raise EnemyBankError("enemy bank is not S64B v2")

    joints = report["joints"]
    animations = report["animations"]
    resources = report["resources"]
    joint_count = int(report["joint_count"])

    header_bytes = 16
    part_bytes = 24
    pose_bytes = header_bytes + joint_count * part_bytes

    budget = manifest["budget"]
    failures: list[str] = []

    _require(joint_count <= int(budget["max_joints"]),
             f"joint count {joint_count} exceeds the {budget['max_joints']}-part "
             f"shared joint workspace", failures)
    _require(pose_bytes <= int(budget["max_rigid_pose_bytes"]),
             f"rigid pose {pose_bytes} B exceeds the "
             f"{budget['max_rigid_pose_bytes']} B cold class", failures)
    _require(int(report["primitive_count"]) <= int(budget["max_primitives"]),
             f"primitive count {report['primitive_count']} exceeds the declared "
             f"budget {budget['max_primitives']}", failures)
    _require(int(resources["texture_resident_bytes"]) <=
             int(budget["max_texture_resident_bytes"]),
             "texture residency exceeds the declared budget", failures)
    _require(int(resources["clut_resident_bytes"]) <=
             int(budget["max_clut_resident_bytes"]),
             "CLUT residency exceeds the declared budget", failures)
    _require(len(compiled.payload) <= int(budget["max_payload_bytes"]),
             "payload exceeds the declared budget", failures)
    _require(len(animations) <= int(budget["max_animations"]),
             "animation count exceeds the declared budget", failures)

    # One VDP1 command per source primitive.  T2.19a is the cautionary tale:
    # Mario's textured primitives emit a Gouraud polygon *plus* a distorted
    # sprite.  This bake must not inherit that structure by accident.
    commands = (int(resources["texture_commands_per_instance"]) +
                int(resources["gouraud_tables_per_instance"]))
    _require(int(resources["draw_records_per_instance"]) ==
             int(report["primitive_count"]),
             "draw records per instance is not one per source primitive", failures)
    _require(commands == int(report["primitive_count"]),
             f"textured ({resources['texture_commands_per_instance']}) plus "
             f"Gouraud ({resources['gouraud_tables_per_instance']}) commands "
             f"is {commands}, not one per primitive "
             f"({report['primitive_count']})", failures)

    # Parents strictly precede children: the rigid evaluator composes in
    # ordinal order and rejects a forward parent reference.
    roots = 0
    for joint in joints:
        ordinal = int(joint["joint_ordinal"])
        parent = int(joint["parent_ordinal"])
        if parent < 0:
            roots += 1
        else:
            _require(parent < ordinal,
                     f"joint {ordinal} has a forward parent reference {parent}",
                     failures)
    _require(roots == 1, f"hierarchy has {roots} roots, expected exactly 1", failures)

    # Every baked animation must carry a channel for the root translation and
    # for all three axes of every joint, or a frame cannot decode.
    for animation in animations:
        expected = 3 + joint_count * 3
        _require(int(animation["channel_count"]) == expected,
                 f"animation {animation['animation_id']} has "
                 f"{animation['channel_count']} channels, expected {expected}",
                 failures)
        _require(int(animation["joint_count"]) == joint_count,
                 f"animation {animation['animation_id']} joint count disagrees "
                 f"with the geo hierarchy", failures)
        _require(int(animation["frame_count"]) > 0,
                 f"animation {animation['animation_id']} has no frames", failures)

    keep = list(manifest["animation_selection"]["keep"])
    _require(sorted(int(a["animation_id"]) for a in animations) == sorted(keep),
             "baked animation IDs do not match the manifest keep list", failures)

    expected = manifest.get("expected", {})
    published_sha = str(expected.get("payload_sha256", ""))
    payload_sha = hashlib.sha256(compiled.payload).hexdigest()
    _require(not published_sha or payload_sha == published_sha,
             f"emitted payload {payload_sha} is not the bank published in the "
             f"scene package ({published_sha})", failures)

    if failures:
        raise EnemyBankError("; ".join(failures))

    return {
        "schema": "sm64-saturn-enemy-actor-bank-v1",
        "model": manifest["model"],
        "family_ordinal": family_ordinal,
        "model_id": model_id,
        "bank_version": bank.version,
        "payload_bytes": len(compiled.payload),
        "payload_sha256": payload_sha,
        "source_sha256": compiled.source_sha256,
        "joint_count": joint_count,
        "animation_count": len(animations),
        "animation_frame_counts": [int(a["frame_count"]) for a in animations],
        "animation_channel_counts": [int(a["channel_count"]) for a in animations],
        "vertex_count": int(report["vertex_count"]),
        "primitive_count": int(report["primitive_count"]),
        "meshlet_count": int(report["meshlet_count"]),
        "resources": {name: int(resources[name]) for name in sorted(resources)},
        "lane_bytes": compiled.lane_bytes,
        "maximum_scratch": compiled.maximum_scratch,
        "rigid_pose_bytes": pose_bytes,
        "rigid_pose_class_bytes": int(budget["max_rigid_pose_bytes"]),
        "joints": [
            {"joint_ordinal": int(j["joint_ordinal"]),
             "parent_ordinal": int(j["parent_ordinal"]),
             "translation": [int(v) for v in j["translation"]]}
            for j in joints
        ],
        "parts": [
            {"joint_ordinal": int(p["joint_ordinal"]),
             "display_list": str(p["display_list"])}
            for p in report["parts"]
        ],
        "linked_into_elf": False,
        "cart_bytes_added": 0,
    }, compiled.payload


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--closure", required=True)
    parser.add_argument("--family-report", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()

    root = Path(args.root).resolve()
    try:
        report, payload = compile_enemy_bank(
            root, Path(args.manifest), Path(args.closure),
            Path(args.family_report))
    except EnemyBankError as error:
        print(f"enemy actor bank: FAIL {error}", file=sys.stderr)
        raise SystemExit(1)

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)
    Path(args.report).parent.mkdir(parents=True, exist_ok=True)
    Path(args.report).write_text(
        json.dumps(report, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(f"enemy actor bank: PASS {report['model']} "
          f"{report['joint_count']} joints, {report['animation_count']} animations, "
          f"{report['payload_bytes']} B, rigid pose {report['rigid_pose_bytes']} B "
          f"of the {report['rigid_pose_class_bytes']} B class, "
          f"{report['cart_bytes_added']} cart bytes added")


if __name__ == "__main__":
    main()
