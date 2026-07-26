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

Adjacency comes from an **attribute-exact weld**: two source ``Vtx`` rows are
one vertex only when the whole row matches, normal/colour included. SM64
re-uploads the same physical vertex into several ``gsSPVertex`` batches, so
without a weld two triangles either side of a batch boundary never look
adjacent; welding on position alone would be unsafe, because SM64 stores
different normals at one position on hard edges. See
:func:`resolve_display_list_vertices`.

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

    ``corners`` is the merged quad's boundary cycle in VDP1 winding order, or
    ``None`` when unpaired. Each code names a corner *relative to this entry*:
    0-2 are this triangle's own corners in source order, 3-5 are the partner's
    (``code - 3``). Keeping it here means the emit stage gets a real fourth
    corner in the right order without recomputing the boundary walk per frame,
    and the encoding is independent of vertex numbering, so it survives
    whatever vertex representation the runtime happens to hold.
    """

    display_list: str
    list_ordinal: int
    partner_list_ordinal: int | None
    corners: tuple[int, int, int, int] | None = None


def _ints(text: str) -> list[int]:
    return [int(value, 0) for value in _INT_TOKEN.findall(text)]


def resolve_display_list_vertices(
    lists: Mapping[str, Sequence[tuple[str, str]]],
    vertex_rows: Mapping[str, Sequence[tuple[int, ...]]],
    *,
    weld: bool = True,
) -> tuple[list[Vertex], dict[Key, tuple[int, int, int]]]:
    """Resolve every triangle command to its three *source* vertices.

    Returns a flat vertex table and a ``(display_list, list_ordinal)`` map into
    it. ``vertex_rows`` must hold **complete** ``Vtx`` rows -- use
    :func:`extract_mario_actor.vertex_rows`, not ``vertex_groups()``, whose
    5-field rows drop exactly the fields the weld depends on. Truncated rows
    raise rather than silently welding on too little.

    **Vertex identity is the whole source row**, so two rows weld into one
    vertex only when they are byte-identical: same position, flag, uv and
    normal/colour. That is shading-neutral by construction -- the two rows are
    interchangeable in every field the renderer reads. Welding on position
    alone would not be: SM64 stores different normals at the same position on
    hard edges, and merging those changes Gouraud output.

    The weld matters because SM64 re-uploads the same physical vertex into
    several ``gsSPVertex`` batches. Without it, triangles either side of a
    batch boundary never share a vertex and so never look adjacent, which
    blocks the merge for purely bookkeeping reasons. ``mario_right_leg_shared_dl``
    is the clean example: 20 triangles split across two 16-row batches.

    Each display-list body is walked independently and starts with an empty
    cache. That matches :func:`dl_rigid_groups.walk_display_lists`, which gives
    every body its own ``list_ordinal`` counter, and it is conservative: a
    triangle reading a slot this body never loaded is simply left unresolved
    (and therefore never merged) rather than guessed at.
    """
    from extract_mario_actor import VTX_ROW_FIELDS  # local: keeps this dep-free

    for symbol, rows in vertex_rows.items():
        for index, row in enumerate(rows):
            if len(row) != VTX_ROW_FIELDS:
                raise ValueError(
                    f"{symbol}[{index}] is not a complete Vtx row: expected "
                    f"{VTX_ROW_FIELDS} fields, got {len(row)}. Use "
                    f"extract_mario_actor.vertex_rows(), not vertex_groups()."
                )

    table: list[Vertex] = []
    identifiers: dict[object, int] = {}
    triangle_vertices: dict[Key, tuple[int, int, int]] = {}

    def identify(source: tuple[str, int]) -> int | None:
        rows = vertex_rows.get(source[0])
        if rows is None or not 0 <= source[1] < len(rows):
            return None
        row = tuple(int(field) for field in rows[source[1]])
        # Welding keys identity on the row itself, so every upload of a
        # byte-identical row collapses to one vertex. Without it, identity is
        # the upload site and no two batches ever share a vertex.
        key: object = row if weld else source
        known = identifiers.get(key)
        if known is not None:
            return known
        identifiers[key] = len(table)
        table.append((row[0], row[1], row[2]))
        return identifiers[key]

    for name, body in lists.items():
        cache: list[tuple[str, int] | None] = [None] * _CACHE_SLOTS
        list_ordinal = 0
        for macro, args in body:
            if macro == "gsSPVertex":
                symbol = _SYMBOL.match(args)
                values = _ints(args[symbol.end():]) if symbol is not None else []
                rows = vertex_rows.get(symbol.group(1)) if symbol is not None else None
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


def _corner_code(
    vertex: int,
    own: tuple[int, int, int],
    mate: tuple[int, int, int],
    key: Key,
) -> int:
    """Name one boundary-cycle vertex as a corner of this pair.

    0-2 are ``own``'s corners in source order, 3-5 are ``mate``'s. The two
    triangles share an edge, so two of the four cycle vertices appear in both;
    resolving against ``own`` first keeps the encoding deterministic.
    """
    if vertex in own:
        return own.index(vertex)
    if vertex in mate:
        return 3 + mate.index(vertex)
    raise ValueError(f"quad corner {vertex} belongs to neither triangle of {key}")


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

    # The boundary cycle quad_pairing already walked, re-expressed relative to
    # each triangle so the emit stage never has to recompute it. Both entries
    # of a pair describe the same cycle in the same order, from their own base.
    corners: dict[int, tuple[int, int, int, int]] = {}
    for face, other in partner.items():
        own = triangle_vertices[face_keys[face]]
        mate = triangle_vertices[face_keys[other]]
        corners[face] = tuple(  # type: ignore[assignment]
            _corner_code(vertex, own, mate, face_keys[face])
            for vertex in matched[face].vertices
        )

    entries: list[QuadMapEntry] = []
    for key in sorted(grouped):
        index = face_index.get(key)
        other = partner.get(index) if index is not None else None
        entries.append(QuadMapEntry(
            display_list=key[0],
            list_ordinal=key[1],
            partner_list_ordinal=face_keys[other][1] if other is not None else None,
            corners=corners.get(index) if index is not None else None,
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
    from extract_mario_actor import vertex_rows

    layouts = parse_geo_layouts(geo_source)
    lists = parse_display_lists(model_source)
    sites = walk_geo_layout(layouts, lists, layout)
    vertices, triangle_vertices = resolve_display_list_vertices(
        lists, vertex_rows(model_source),
    )
    return build_quad_map(sites, triangle_vertices, vertices, **options)  # type: ignore[arg-type]


# -- C rendering -------------------------------------------------------------
#
# Entry encoding, one uint32_t per triangle command of a display list, indexed
# by that command's list_ordinal:
#
#     bit  0        paired flag -- 1 means "merge", and *only* this means it
#     bits 1..15    partner list_ordinal
#     bits 16..31   the four corner codes, 4 bits each, cycle order
#
# The flag is bit 0 rather than a magic partner value on purpose. Ordinal 0 is
# a legal partner, so an encoding that stored a bare partner index would make
# "unpaired" and "merge with the first triangle of this list" the same word.
# With the flag at bit 0, the all-zero word is the sentinel: a zeroed page, an
# unwritten slot, a truncated table and an ordinal past the recorded length all
# read as "do not merge", which is exactly today's behaviour. There is
# deliberately no third state in which the runtime infers mergeability.
#: The only "do not merge" encoding, and the value of any absent entry.
QUAD_MAP_NONE = 0x00000000
_ENTRY_PAIRED_MASK = 0x1
_ENTRY_PARTNER_SHIFT = 1
_ENTRY_PARTNER_MASK = 0x7FFF
_ENTRY_CORNER_BASE = 16
_ENTRY_CORNER_STRIDE = 4
_ENTRY_CORNER_MASK = 0xF
#: Corner codes are 0-5 (see :class:`QuadMapEntry`), so 4 bits is ample.
_CORNER_CODE_LIMIT = 6

#: Generated header the generated source includes. Both land in the build's
#: generated directory, next to mario_anim_data.c.
QUAD_MAP_HEADER_NAME = "saturn_quad_map.h"
_GENERATED_BY = "tools/saturn/quad_map.py"
_C_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")


def _encode_entry(entry: QuadMapEntry) -> int:
    """Pack one triangle command's decision into its uint32_t word."""
    if entry.partner_list_ordinal is None:
        if entry.corners is not None:
            raise ValueError(
                f"{(entry.display_list, entry.list_ordinal)} is unpaired but "
                f"carries a corner cycle"
            )
        return QUAD_MAP_NONE
    if entry.corners is None:
        raise ValueError(
            f"{(entry.display_list, entry.list_ordinal)} is paired but has no "
            f"corner cycle"
        )
    if len(entry.corners) != 4:
        raise ValueError(
            f"{(entry.display_list, entry.list_ordinal)}: a quad has four "
            f"corners, got {len(entry.corners)}"
        )
    if not 0 <= entry.partner_list_ordinal <= _ENTRY_PARTNER_MASK:
        raise ValueError(
            f"{(entry.display_list, entry.list_ordinal)}: partner "
            f"{entry.partner_list_ordinal} does not fit the encoding"
        )
    word = _ENTRY_PAIRED_MASK | (entry.partner_list_ordinal << _ENTRY_PARTNER_SHIFT)
    for index, code in enumerate(entry.corners):
        if not 0 <= code < _CORNER_CODE_LIMIT:
            raise ValueError(
                f"{(entry.display_list, entry.list_ordinal)}: corner code "
                f"{code} is outside 0-{_CORNER_CODE_LIMIT - 1}"
            )
        word |= code << (_ENTRY_CORNER_BASE + _ENTRY_CORNER_STRIDE * index)
    return word


def quad_map_tables(
    entries: Iterable[QuadMapEntry],
) -> list[tuple[str, list[int]]]:
    """Group entries into one dense, ordinal-indexed word array per list.

    Returned in display-list symbol order, which makes generation
    deterministic. Two reductions are applied, both safe because an absent
    entry means "do not merge":

    * a display list with no pair at all is dropped entirely;
    * trailing unpaired ordinals are truncated, so ``entry_count`` is the last
      paired ordinal plus one rather than the list's triangle count.

    Validation is strict rather than best-effort: a one-way pair, a partner in
    another display list, a duplicate key or an out-of-range corner code all
    raise. Every one of those would silently draw the wrong polygon at
    runtime, which is this project's worst failure mode.
    """
    by_list: dict[str, dict[int, QuadMapEntry]] = {}
    for entry in entries:
        if _C_IDENTIFIER.match(entry.display_list) is None:
            raise ValueError(
                f"{entry.display_list!r} is not a C identifier and cannot name "
                f"a display list symbol"
            )
        ordinals = by_list.setdefault(entry.display_list, {})
        if entry.list_ordinal in ordinals:
            raise ValueError(
                f"duplicate entry for {(entry.display_list, entry.list_ordinal)}"
            )
        if not 0 <= entry.list_ordinal <= _ENTRY_PARTNER_MASK:
            raise ValueError(
                f"{(entry.display_list, entry.list_ordinal)}: ordinal does not "
                f"fit the encoding"
            )
        ordinals[entry.list_ordinal] = entry

    tables: list[tuple[str, list[int]]] = []
    for display_list in sorted(by_list):
        ordinals = by_list[display_list]
        for entry in ordinals.values():
            partner = entry.partner_list_ordinal
            if partner is None:
                continue
            if partner == entry.list_ordinal:
                raise ValueError(
                    f"{(display_list, entry.list_ordinal)} is paired with "
                    f"itself"
                )
            mate = ordinals.get(partner)
            if mate is None or mate.partner_list_ordinal != entry.list_ordinal:
                raise ValueError(
                    f"{(display_list, entry.list_ordinal)} -> {partner} is not "
                    f"reciprocated within {display_list}"
                )
        words = [
            _encode_entry(ordinals[ordinal]) if ordinal in ordinals
            else QUAD_MAP_NONE
            for ordinal in range(max(ordinals) + 1)
        ]
        while words and words[-1] == QUAD_MAP_NONE:
            words.pop()
        if words:
            tables.append((display_list, words))
    return tables


def render_quad_map_h(entries: Iterable[QuadMapEntry] = ()) -> str:
    """Render the generated header: types, sentinel, accessors and bound.

    Generated rather than hand-written so the encoding cannot drift between
    the compiler that writes the words and the runtime that reads them.

    ``entries`` is used only to derive SM64_SATURN_QUAD_MAP_MAX_ENTRIES, the
    longest row this table will actually emit. The runtime indexes a fixed
    ordinal-keyed side array by that bound, so deriving it here -- from the
    same quad_map_tables() trimming that produces the rows themselves --
    is what stops the runtime's array size from drifting away from the data
    it has to hold. With no entries the bound is 1, the smallest legal
    array size; the real build always passes the entries it emitted.
    """
    tables = quad_map_tables(entries)
    max_entries = max((len(words) for _, words in tables), default=1) or 1
    return f"""\
/* Generated by {_GENERATED_BY} -- do not edit. */
#ifndef SM64_SATURN_QUAD_MAP_H
#define SM64_SATURN_QUAD_MAP_H

#include <stdint.h>

#include "types.h"

/* One word per triangle command, indexed by that command's ordinal within
 * its own display list (gsSP2Triangles counts as two; triangles inside a
 * nested gsSPDisplayList belong to the child's own count, not the parent's).
 *
 *   bit  0        paired -- 1, and only 1, means "merge"
 *   bits 1..15    partner ordinal, in the same display list
 *   bits 16..31   four 4-bit corner codes in cycle order; 0-2 name this
 *                 triangle's own corners in source order, 3-5 the partner's
 *                 (code - 3)
 *
 * SM64_SATURN_QUAD_MAP_NONE is the all-zero word. That is deliberate: a
 * zeroed page, an unwritten slot, a display list absent from the table and an
 * ordinal past entry_count all read as "do not merge", which is exactly the
 * one-command-per-triangle behaviour this port already has. There is no third
 * state in which mergeability may be inferred. */
typedef uint32_t sm64_saturn_quad_map_entry_t;

#define SM64_SATURN_QUAD_MAP_NONE {QUAD_MAP_NONE:#010x}U
#define SM64_SATURN_QUAD_MAP_IS_PAIRED(entry) (((entry) & {_ENTRY_PAIRED_MASK:#x}U) != 0U)
#define SM64_SATURN_QUAD_MAP_PARTNER(entry) (((entry) >> {_ENTRY_PARTNER_SHIFT}U) & {_ENTRY_PARTNER_MASK:#x}U)
#define SM64_SATURN_QUAD_MAP_CORNER(entry, index) (((entry) >> ({_ENTRY_CORNER_BASE}U + {_ENTRY_CORNER_STRIDE}U * (index))) & {_ENTRY_CORNER_MASK:#x}U)
/* Corner codes run 0-5; anything else is a corrupt entry. */
#define SM64_SATURN_QUAD_MAP_CORNER_LIMIT {_CORNER_CODE_LIMIT}U

/* The longest row in this table. The runtime keys a fixed ordinal-indexed
 * side array off this so it can merge a pair whose two ordinals are not
 * adjacent; the array must be at least this long. Emitted here, from the
 * rows actually generated, so that bound cannot drift from the data --
 * saturn_fast3d_frontend.c static-asserts its own capacity against it. */
#define SM64_SATURN_QUAD_MAP_MAX_ENTRIES {max_entries}U

/* One row per display list that has at least one merged pair. `display_list`
 * is the link-time address of the real Gfx array, which is what the frontend
 * has in hand when it starts interpreting a list -- see the note in the
 * generated source. Lists with no pair are simply absent. */
typedef struct sm64_saturn_quad_map_list {{
    const Gfx *display_list;
    const sm64_saturn_quad_map_entry_t *entries;
    uint16_t entry_count;
    uint16_t reserved;
}} sm64_saturn_quad_map_list_t;

extern const sm64_saturn_quad_map_list_t sm64_saturn_quad_map_lists[];
extern const uint16_t sm64_saturn_quad_map_list_count;

#endif /* SM64_SATURN_QUAD_MAP_H */
"""


def render_quad_map_c(
    entries: Iterable[QuadMapEntry],
    *,
    provenance: Sequence[str] = (),
) -> str:
    """Render the quad map as a C translation unit.

    ``entries`` may span several actors; :class:`QuadMapEntry` already carries
    its display list, and display-list symbols are unique across the link, so
    no per-actor namespacing is needed.

    **How the runtime finds a row.** The frontend never sees a symbol name, it
    sees a ``Gfx *``. It does not need a name: the display lists are ordinary C
    symbols, so this file references them directly and each row stores the
    array's link-time address. ``geo_process_master_list`` emits
    ``gSPDisplayList(gfx++, node->displayList)``, so the ``w1`` word of the
    ``G_DL`` the frontend decodes *is* ``&mario_butt_dl[0]`` -- the same
    address this table holds. Identification is therefore a pointer compare
    against a table small enough to scan once per display list entered, and the
    per-triangle lookup that follows is a plain indexed load, no search. Lists
    the frontend enters that are not in the table (the pool-built master list
    itself, anything unanalysed) simply find no row and merge nothing.
    """
    tables = quad_map_tables(entries)
    lines = [f"/* Generated by {_GENERATED_BY} -- do not edit. */"]
    if provenance:
        lines.append("/*")
        for note in provenance:
            lines.append(f" * {note}")
        lines.append(" */")
    lines.append(f'#include "{QUAD_MAP_HEADER_NAME}"')
    lines.append("")
    lines.append("/* The real display-list symbols. Declaring them here rather")
    lines.append(" * than including an actor group header keeps this file")
    lines.append(" * independent of which bank a list happens to live in; a")
    lines.append(" * symbol that does not exist is a link error, not a silent")
    lines.append(" * mismatch. */")
    for display_list, _words in tables:
        lines.append(f"extern const Gfx {display_list}[];")
    lines.append("")

    for display_list, words in tables:
        paired = sum(1 for word in words if word & _ENTRY_PAIRED_MASK) // 2
        lines.append(
            f"/* {display_list}: {len(words)} mapped ordinals, "
            f"{paired} merged pair{'' if paired == 1 else 's'}. */"
        )
        lines.append(
            f"static const sm64_saturn_quad_map_entry_t "
            f"sm64_saturn_quad_map_entries_{display_list}[{len(words)}] = {{"
        )
        for start in range(0, len(words), 4):
            row = ", ".join(f"{word:#010x}U" for word in words[start:start + 4])
            lines.append(f"    {row},")
        lines.append("};")
        lines.append("")

    if tables:
        lines.append(
            f"const sm64_saturn_quad_map_list_t "
            f"sm64_saturn_quad_map_lists[{len(tables)}] = {{"
        )
        for display_list, words in tables:
            lines.append(
                f"    {{ {display_list}, "
                f"sm64_saturn_quad_map_entries_{display_list}, "
                f"{len(words)}U, 0U }},"
            )
        lines.append("};")
    else:
        # A zero-length array is not C. One inert row plus a count of zero
        # keeps the declaration valid and the table unreadable.
        lines.append("const sm64_saturn_quad_map_list_t "
                     "sm64_saturn_quad_map_lists[1] = {")
        lines.append("    { NULL, NULL, 0U, 0U },")
        lines.append("};")
    lines.append("")
    lines.append(
        f"const uint16_t sm64_saturn_quad_map_list_count = {len(tables)}U;"
    )
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    import argparse
    import json
    from pathlib import Path

    parser = argparse.ArgumentParser(description="Compile an actor's quad map.")
    parser.add_argument("--geo", type=Path)
    parser.add_argument("--model", type=Path)
    parser.add_argument("--layout",
                        help="entry GeoLayout symbol, e.g. mario_geo_body")
    parser.add_argument("--actor", nargs=3, action="append", default=[],
                        metavar=("GEO", "MODEL", "LAYOUT"),
                        help="compile one more model into the same table; "
                             "repeatable")
    parser.add_argument("--projection-policy", default="sampled",
                        choices=("sampled", "planar"))
    parser.add_argument("--minimum-normal-alignment", type=float, default=0.80)
    parser.add_argument("--dump-entries", type=Path,
                        help="write the full quad map as JSON")
    parser.add_argument("--emit-c", type=Path,
                        help="write the generated C table")
    parser.add_argument("--emit-h", type=Path,
                        help="write the generated C header")
    parser.add_argument("--report", type=Path,
                        help="write the per-model statistics as JSON")
    arguments = parser.parse_args()

    actors = list(arguments.actor)
    if arguments.geo is not None or arguments.model is not None \
            or arguments.layout is not None:
        if None in (arguments.geo, arguments.model, arguments.layout):
            parser.error("--geo, --model and --layout must be given together")
        actors.append((str(arguments.geo), str(arguments.model),
                       arguments.layout))
    if not actors:
        parser.error("give at least one --actor GEO MODEL LAYOUT")

    entries: list[QuadMapEntry] = []
    report: dict[str, object] = {}
    provenance: list[str] = []
    for geo, model, layout in actors:
        model_paths = [Path(part) for part in str(model).split(",")]
        model_source = "\n".join(
            path.read_text(encoding="utf-8") for path in model_paths)
        actor_entries, stats = compile_actor(
            Path(geo).read_text(encoding="utf-8"),
            model_source,
            layout,
            projection_policy=arguments.projection_policy,
            minimum_normal_alignment=arguments.minimum_normal_alignment,
        )
        entries.extend(actor_entries)
        report[layout] = stats
        provenance.append(
            f"{layout}: {geo} + {model} -- {stats['keys']} triangle commands, "
            f"{stats['quad_count']} merged pairs"
        )

    if arguments.dump_entries is not None:
        arguments.dump_entries.write_text(json.dumps([
            {
                "display_list": entry.display_list,
                "list_ordinal": entry.list_ordinal,
                "partner_list_ordinal": entry.partner_list_ordinal,
                "corners": list(entry.corners) if entry.corners else None,
            }
            for entry in entries
        ], indent=2), encoding="utf-8")
    if arguments.emit_h is not None:
        arguments.emit_h.parent.mkdir(parents=True, exist_ok=True)
        arguments.emit_h.write_text(
            render_quad_map_h(entries), encoding="utf-8")
    if arguments.emit_c is not None:
        arguments.emit_c.parent.mkdir(parents=True, exist_ok=True)
        arguments.emit_c.write_text(
            render_quad_map_c(entries, provenance=provenance), encoding="utf-8")
    if arguments.report is not None:
        arguments.report.parent.mkdir(parents=True, exist_ok=True)
        arguments.report.write_text(json.dumps(report, indent=2),
                                    encoding="utf-8")
    if arguments.emit_c is None and arguments.report is None:
        print(json.dumps(report, indent=2))
        return
    for layout, stats in report.items():
        print(
            f"quad_map: {layout}: {stats['keys']} triangle commands, "  # type: ignore[index]
            f"{stats['quad_count']} merged pairs, "  # type: ignore[index]
            f"{stats['render_primitive_count']} VDP1 commands"  # type: ignore[index]
        )


if __name__ == "__main__":
    main()
