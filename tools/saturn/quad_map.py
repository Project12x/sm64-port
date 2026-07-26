"""Compile a display-list quad map: which triangles VDP1 may draw as one quad.

VDP1's native primitive is a four-corner quadrilateral, so a triangle and a
quad cost exactly one command each. Two coplanar triangles sharing an edge can
therefore occupy *one* command instead of two -- but only where that is
provably safe for every pose the actor can reach.

Eligibility is the intersection of two independent gates:

1. **Structural** (:mod:`dl_rigid_groups`). Both triangles must carry the same
   rigid group -- the same joint transform moves both -- and neither may be
   flagged unsafe. This gate is evaluated first and is purely structural, so no
   amount of favourable rest-pose geometry can merge across a joint boundary.
   A merge inside one rigid group is pose-independent by construction: the
   joint applies one affine map to both triangles, which preserves coplanarity,
   winding and convexity, so a pair proven safe at rest stays safe in every
   animation frame. That is why this module passes no deformation poses to
   :mod:`quad_pairing` -- the rigid-group argument is strictly stronger than
   sampling a finite set of poses.
2. **Geometric** (:mod:`quad_pairing`, used unmodified). Shared-edge topology,
   consistent winding, normal alignment and projected convexity.

**The map is keyed on ``(display_list, list_ordinal)``, never on the global
walk ordinal.** The runtime cannot reproduce a global ordinal: a
``GEO_SWITCH_CASE`` makes the static walk visit every case where the runtime
executes only the selected one, and SM64 buckets display lists into eight
layers of the master list, so runtime execution order is not geo-tree order.
The index of a triangle *within its own display list* survives all of that,
because the runtime always executes the same ``Gfx`` array from its start.

One display list is often bound by several geo nodes (Mario's face lists are
each bound eight times, once per eye variant). The map has a single entry per
key, so a merge decision must hold for **every** instantiation. Two keys are
therefore compared on their whole *rigid-group signature* -- the tuple of
groups they hold across all instantiations, in walk order -- and a key is
ineligible if it is unsafe in any single instantiation.

How the structural gate is enforced: eligible keys are partitioned into buckets
of ``(display_list, rigid_group_signature)`` and :func:`quad_pairing.candidates`
is invoked once per bucket, so a cross-bucket pair is never even a candidate.
The bucket id is additionally written into the face material channel, which
``candidates()`` already refuses to merge across, and every selected pair is
re-checked against its bucket before an entry is emitted. Partitioning rather
than relying on the material alone also raises the achievable rate: an edge
incident on three faces is skipped by ``candidates()``, and partitioning keeps
faces from other buckets from creating those three-way edges.

Provenance / reference-code-first: the pairing core is
``tools/saturn/quad_pairing.py`` (unmodified), which already wraps NetworkX's
exact blossom matching. The Fast3D vertex-cache semantics reproduced in
:func:`resolve_display_list_vertices` are ported from
``tools/saturn/extract_mario_actor.py`` (``flatten``, lines 245-317) -- the
persistent 32-slot cache with destination-offset loads, whose absence produced
an exploded actor in an earlier prototype. Reuse mode: close-port (in-repo).
See the task report for the external prior-art survey.
"""
from __future__ import annotations

import re
from collections import Counter
from dataclasses import dataclass
from typing import Iterable, Mapping, Sequence

from dl_rigid_groups import TriangleSite
from quad_pairing import QuadCandidate, Vertex, candidates, maximum_weight_matching

#: A triangle whose cache slots were never loaded in its own display list, so
#: its source vertices cannot be identified. Never merged rather than guessed.
REASON_UNRESOLVED_VERTICES = "unresolved_vertices"
#: The same key resolved to different vertex-cache slots in two instantiations.
REASON_UNSTABLE_CACHE_INDICES = "unstable_cache_indices"
#: Two keys of one display list were reached a different number of times, so
#: their rigid-group signatures cannot be compared position by position.
REASON_UNSTABLE_INSTANTIATIONS = "unstable_instantiation_count"
#: Marked unsafe by the walker without a specific reason attached.
REASON_UNSAFE_UNSPECIFIED = "unsafe"

Key = tuple[str, int]

_INT_TOKEN = re.compile(r"(?<![A-Za-z_])(?:0x[0-9A-Fa-f]+|-?\d+)")
_SYMBOL = re.compile(r"\s*&?(\w+)")
#: Fast3D's vertex cache. Matches extract_mario_actor.flatten.
_CACHE_SLOTS = 32


@dataclass(frozen=True)
class QuadMapEntry:
    """One triangle command's merge decision.

    ``partner_list_ordinal`` is the other triangle of the quad, or ``None``
    when this triangle stays a standalone (degenerate-quad) command. Pairing
    is symmetric: if A names B, B names A, and both are in ``display_list``.
    """

    display_list: str
    list_ordinal: int
    partner_list_ordinal: int | None


def _ints(text: str) -> list[int]:
    return [int(value, 0) for value in _INT_TOKEN.findall(text)]


def resolve_display_list_vertices(
    lists: Mapping[str, Sequence[tuple[str, str]]],
    vertex_groups: Mapping[str, Sequence[tuple[int, ...]]],
) -> tuple[list[Vertex], dict[Key, tuple[int, int, int]]]:
    """Resolve every triangle command to its three *source* vertices.

    Returns a flat vertex table and a ``(display_list, list_ordinal)`` map into
    it. Vertex identity is the source ``Vtx`` row -- ``(group symbol, index)``
    -- not the cache slot and not the position. Two distinct rows that happen
    to share a position (a duplicated UV-seam vertex) stay distinct, so a seam
    is never mistaken for a shared edge.

    Each display-list body is walked independently and starts with an empty
    cache. That matches :func:`dl_rigid_groups.walk_display_lists`, which gives
    every body its own ``list_ordinal`` counter, and it is conservative: a
    triangle reading a slot this body never loaded is simply left unresolved
    (and therefore never merged) rather than guessed at.
    """
    table: list[Vertex] = []
    identifiers: dict[tuple[str, int], int] = {}
    triangle_vertices: dict[Key, tuple[int, int, int]] = {}

    def identify(source: tuple[str, int]) -> int | None:
        known = identifiers.get(source)
        if known is not None:
            return known
        rows = vertex_groups.get(source[0])
        if rows is None or not 0 <= source[1] < len(rows):
            return None
        row = rows[source[1]]
        if len(row) < 3:
            return None
        identifiers[source] = len(table)
        table.append((int(row[0]), int(row[1]), int(row[2])))
        return identifiers[source]

    for name, body in lists.items():
        cache: list[tuple[str, int] | None] = [None] * _CACHE_SLOTS
        list_ordinal = 0
        for macro, args in body:
            if macro == "gsSPVertex":
                symbol = _SYMBOL.match(args)
                values = _ints(args[symbol.end():]) if symbol is not None else []
                rows = vertex_groups.get(symbol.group(1)) if symbol is not None else None
                if symbol is None or rows is None or len(values) != 2:
                    # An unreadable load leaves the cache in an unknown state.
                    cache = [None] * _CACHE_SLOTS
                    continue
                count, destination = values
                if (count != len(rows) or destination < 0
                        or destination + count > _CACHE_SLOTS):
                    cache = [None] * _CACHE_SLOTS
                    continue
                for offset in range(count):
                    cache[destination + offset] = (symbol.group(1), offset)
            elif macro in ("gsSP1Triangle", "gsSP2Triangles"):
                values = _ints(args)
                triples = (
                    (values[0:3], values[4:7])
                    if macro == "gsSP2Triangles"
                    else (values[0:3],)
                )
                for triple in triples:
                    # list_ordinal advances for every triangle command, resolved
                    # or not, so it stays aligned with the rigid-group walk.
                    ordinal, list_ordinal = list_ordinal, list_ordinal + 1
                    if len(triple) != 3:
                        continue
                    sources = [
                        cache[index] if 0 <= index < _CACHE_SLOTS else None
                        for index in triple
                    ]
                    if any(source is None for source in sources):
                        continue
                    resolved = [identify(source) for source in sources]  # type: ignore[arg-type]
                    if any(index is None for index in resolved):
                        continue
                    triangle_vertices[(name, ordinal)] = (
                        resolved[0], resolved[1], resolved[2],
                    )  # type: ignore[assignment]
    return table, triangle_vertices


def _instantiations(sites: Iterable[TriangleSite]) -> dict[Key, list[TriangleSite]]:
    """Group sites by key, preserving walk order.

    The n-th occurrence of every key in one display list belongs to the n-th
    binding of that list, because each binding walks the whole body in order
    from ``list_ordinal`` 0. That alignment is what makes signature comparison
    position-by-position meaningful, and it is checked below.
    """
    grouped: dict[Key, list[TriangleSite]] = {}
    for site in sites:
        grouped.setdefault((site.display_list, site.list_ordinal), []).append(site)
    return grouped


def build_quad_map(
    sites: Iterable[TriangleSite],
    triangle_vertices: Mapping[Key, tuple[int, int, int]],
    vertices: Sequence[Vertex],
    *,
    minimum_normal_alignment: float = 0.80,
    projection_policy: str = "sampled",
) -> tuple[list[QuadMapEntry], dict[str, object]]:
    """Decide, for every triangle command, whether it merges and with what.

    ``sites`` are :class:`dl_rigid_groups.TriangleSite` records from a geo-layout
    or display-list walk; ``triangle_vertices`` and ``vertices`` come from
    :func:`resolve_display_list_vertices`. Every distinct key gets exactly one
    entry, whether or not it is eligible or paired.
    """
    vertex_list = list(vertices)
    grouped = _instantiations(sites)

    counts_per_list: dict[str, set[int]] = {}
    for (display_list, _ordinal), instances in grouped.items():
        counts_per_list.setdefault(display_list, set()).add(len(instances))
    unstable_lists = {
        display_list for display_list, counts in counts_per_list.items()
        if len(counts) != 1
    }

    ineligible: Counter[str] = Counter()
    eligible: dict[Key, tuple[str, tuple[int, ...]]] = {}
    for key, instances in sorted(grouped.items()):
        display_list = key[0]
        reasons: set[str] = set()
        for site in instances:
            if site.unsafe:
                reasons |= set(site.reasons) or {REASON_UNSAFE_UNSPECIFIED}
        if display_list in unstable_lists:
            reasons.add(REASON_UNSTABLE_INSTANTIATIONS)
        if len({site.indices for site in instances}) != 1:
            reasons.add(REASON_UNSTABLE_CACHE_INDICES)
        if key not in triangle_vertices:
            reasons.add(REASON_UNRESOLVED_VERTICES)
        if reasons:
            ineligible.update(reasons)
            continue
        eligible[key] = (
            display_list,
            tuple(site.rigid_group for site in instances),
        )

    # Bucket = one display list and one rigid-group signature. Two keys may
    # only pair inside a bucket, so both the joint boundary and the display
    # list boundary are enforced by the partition itself.
    buckets: dict[tuple[str, tuple[int, ...]], list[Key]] = {}
    for key in sorted(eligible):
        buckets.setdefault(eligible[key], []).append(key)
    bucket_ids = {bucket: index for index, bucket in enumerate(sorted(buckets))}

    face_keys = sorted(eligible)
    face_index = {key: index for index, key in enumerate(face_keys)}

    options: list[QuadCandidate] = []
    geometric_rejections: Counter[str] = Counter()
    for bucket, keys in sorted(buckets.items()):
        faces = [(bucket_ids[bucket], *triangle_vertices[key]) for key in keys]
        found, rejected = candidates(
            vertex_list,
            faces,
            minimum_normal_alignment=minimum_normal_alignment,
            projection_policy=projection_policy,
        )
        geometric_rejections.update(rejected)
        for option in found:
            options.append(QuadCandidate(
                face_index[keys[option.first]],
                face_index[keys[option.second]],
                option.vertices,
                option.alignment,
                option.shared_length_squared,
            ))

    matched = maximum_weight_matching(options) if options else {}

    partner: dict[int, int] = {}
    for face, option in matched.items():
        partner[face] = option.second if face == option.first else option.first
    for face, other in partner.items():
        # Defensive: a non-reciprocal or cross-bucket pair is a corrupted-
        # geometry bug at runtime, this project's worst failure mode.
        if partner.get(other) != face:
            raise ValueError(f"asymmetric pairing: {face_keys[face]} -> {face_keys[other]}")
        if eligible[face_keys[face]] != eligible[face_keys[other]]:
            raise ValueError(
                f"pair spans buckets: {face_keys[face]} -> {face_keys[other]}"
            )

    entries: list[QuadMapEntry] = []
    for key in sorted(grouped):
        index = face_index.get(key)
        other = partner.get(index) if index is not None else None
        entries.append(QuadMapEntry(
            display_list=key[0],
            list_ordinal=key[1],
            partner_list_ordinal=face_keys[other][1] if other is not None else None,
        ))

    paired = len(partner)
    candidate_faces = {index for option in options for index in (option.first, option.second)}
    per_display_list: dict[str, dict[str, int]] = {}
    for key in sorted(grouped):
        row = per_display_list.setdefault(
            key[0], {"keys": 0, "eligible_keys": 0, "quad_count": 0},
        )
        row["keys"] += 1
        if key in eligible:
            row["eligible_keys"] += 1
            index = face_index[key]
            if index in partner and index < partner[index]:
                row["quad_count"] += 1

    import networkx  # local: quad_pairing already hard-requires it

    stats: dict[str, object] = {
        "triangle_sites": sum(len(instances) for instances in grouped.values()),
        "keys": len(grouped),
        "eligible_keys": len(eligible),
        "ineligible_keys": len(grouped) - len(eligible),
        "ineligible_by_reason": dict(sorted(ineligible.items())),
        "buckets": len(buckets),
        "candidate_count": len(options),
        "quad_count": paired // 2,
        "paired_keys": paired,
        "unpaired_keys": len(grouped) - paired,
        "commands_saved": paired // 2,
        "render_primitive_count": len(grouped) - (paired // 2),
        "geometric_rejection_reasons": dict(sorted(geometric_rejections.items())),
        "unpaired_by_reason": {
            "structurally_ineligible": len(grouped) - len(eligible),
            "no_geometric_candidate": len(eligible) - len(candidate_faces),
            "matcher_left_unmatched": len(candidate_faces) - paired,
        },
        "matcher": "networkx.max_weight_matching",
        "matcher_version": networkx.__version__,
        "minimum_normal_alignment": minimum_normal_alignment,
        "projection_policy": projection_policy,
        "per_display_list": per_display_list,
    }
    return entries, stats


def compile_actor(
    geo_source: str,
    model_source: str,
    layout: str,
    **options: object,
) -> tuple[list[QuadMapEntry], dict[str, object]]:
    """Walk one actor end to end and return its quad map and stats."""
    from dl_rigid_groups import (  # local: keeps the import graph shallow
        parse_display_lists, parse_geo_layouts, walk_geo_layout,
    )
    from extract_mario_actor import vertex_groups

    layouts = parse_geo_layouts(geo_source)
    lists = parse_display_lists(model_source)
    sites = walk_geo_layout(layouts, lists, layout)
    vertices, triangle_vertices = resolve_display_list_vertices(
        lists, vertex_groups(model_source),
    )
    return build_quad_map(sites, triangle_vertices, vertices, **options)  # type: ignore[arg-type]


def main() -> None:
    import argparse
    import json
    from pathlib import Path

    parser = argparse.ArgumentParser(description="Compile an actor's quad map.")
    parser.add_argument("--geo", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--layout", required=True,
                        help="entry GeoLayout symbol, e.g. mario_geo_body")
    parser.add_argument("--projection-policy", default="sampled",
                        choices=("sampled", "planar"))
    parser.add_argument("--minimum-normal-alignment", type=float, default=0.80)
    parser.add_argument("--dump-entries", type=Path,
                        help="write the full quad map as JSON")
    arguments = parser.parse_args()
    entries, stats = compile_actor(
        arguments.geo.read_text(encoding="utf-8"),
        arguments.model.read_text(encoding="utf-8"),
        arguments.layout,
        projection_policy=arguments.projection_policy,
        minimum_normal_alignment=arguments.minimum_normal_alignment,
    )
    if arguments.dump_entries is not None:
        arguments.dump_entries.write_text(json.dumps([
            {
                "display_list": entry.display_list,
                "list_ordinal": entry.list_ordinal,
                "partner_list_ordinal": entry.partner_list_ordinal,
            }
            for entry in entries
        ], indent=2), encoding="utf-8")
    print(json.dumps({"layout": arguments.layout, **stats}, indent=2))


if __name__ == "__main__":
    main()
