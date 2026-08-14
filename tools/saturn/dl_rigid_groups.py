"""Assign each display-list triangle a stable ordinal and a rigid group.

A rigid group is the modelview matrix-stack identity in effect when a
triangle is emitted. Two triangles in the same group are transformed by the
same joint, so they move together rigidly under every animation: a merge
proven safe at rest stays safe in all poses. Triangles in different groups
can be pulled apart arbitrarily and are never merged.

Ordinals are assigned in the order the walk reaches the commands, so they are
a stable identity across frames and poses. That is what lets the safety
analysis be an offline bytecode problem.

**The joint hierarchy is in the geo layout, not the display list.** SM64's
actor and level display lists contain zero ``gsSPMatrix`` commands; every
joint is a ``GEO_ANIMATED_PART`` (or other transform node) in the actor's
``GeoLayout``. So the walk is two-level:

1. :func:`walk_geo_layout` walks the geo-layout tree. Transform-introducing
   nodes each start a new rigid group; ``GEO_OPEN_NODE`` pushes and
   ``GEO_CLOSE_NODE`` pops, exactly like ``gCurGraphNodeIndex`` in
   ``src/engine/geo_layout.c``.
2. Where a node binds a display list, :func:`walk_display_lists` walks that
   list for triangle ordinals; every triangle in it inherits the geo node's
   rigid group.

``GEO_SWITCH_CASE`` is handled by *separation* rather than by poisoning: each
direct child is one case, gets its own rigid group, and so can merge freely
within itself while never merging with a sibling case. ``GEO_ASM`` and any
unmodelled geo node still poison their subtree.

The display-list level keeps its own ``gsSPMatrix``/``gsSPPopMatrix``
handling. It costs nothing and stays correct if any list ever does use it.

Provenance / reference-code-first: the geo-layout tokenizer and block
extractor are reused directly from ``tools/saturn/extract_mario_actor.py``
(``blocks`` / ``geo_tokens``), which already parses this exact structure for
the Mario actor export. Reuse mode: dependency (in-repo). The node/stack
semantics were checked against ``src/engine/geo_layout.c``
(``geo_layout_cmd_open_node`` / ``_close_node`` / ``_branch`` / ``_return``).
No external library parses SM64 geo layouts; see the task report.
"""
from __future__ import annotations

import re
from collections import Counter
from dataclasses import dataclass, field
from typing import NamedTuple

# Macros the display-list walker understands. Anything else marks its group
# unsafe rather than being skipped -- an unmodelled construct is an unknown
# envelope, and this compiler never treats unknown as safe.
#
# Everything below either emits geometry, changes the modelview matrix, or is
# pure render state (texture image/tile/sync, combine mode, env colour, alpha
# compare, geometry mode, lights). Render state cannot move a vertex, so it is
# rigid-group neutral.
_KNOWN = {
    "gsSPVertex", "gsSP1Triangle", "gsSP2Triangles", "gsSPDisplayList",
    "gsSPBranchList",
    "gsSPMatrix", "gsSPPopMatrix", "gsSPEndDisplayList",
    "gsSPLight", "gsSPSetGeometryMode", "gsSPClearGeometryMode",
    "gsSPTexture", "gsDPPipeSync", "gsDPSetCombineMode",
    # Texture-load state used heavily by actors/mario/model.inc.c.
    "gsDPSetTextureImage", "gsDPLoadSync", "gsDPLoadBlock", "gsDPSetTile",
    "gsDPTileSync", "gsDPSetTileSize", "gsDPLoadTextureBlock",
    "gsDPSetEnvColor", "gsDPSetAlphaCompare",
}

# Geo-layout nodes that introduce a transform: each one starts a new rigid
# group, and everything in its subtree moves with it.
_GEO_TRANSFORM_NODES = {
    "GEO_ANIMATED_PART",
    "GEO_ROTATION_NODE", "GEO_ROTATION_NODE_WITH_DL",
    "GEO_TRANSLATE_ROTATE", "GEO_TRANSLATE_ROTATE_WITH_DL",
    "GEO_TRANSLATE", "GEO_TRANSLATE_WITH_DL",
    "GEO_TRANSLATE_NODE", "GEO_TRANSLATE_NODE_WITH_DL",
    "GEO_ROTATE", "GEO_ROTATE_WITH_DL",
    "GEO_ROTATE_Y", "GEO_ROTATE_Y_WITH_DL",
    "GEO_SCALE", "GEO_SCALE_WITH_DL",
    "GEO_BILLBOARD", "GEO_BILLBOARD_WITH_PARAMS",
    "GEO_BILLBOARD_WITH_PARAMS_AND_DL",
}

# Geo-layout nodes that neither transform nor gate visibility.
_GEO_NEUTRAL_NODES = {
    "GEO_NODE_START", "GEO_DISPLAY_LIST",
    # Shadows are emitted by the source scene's existing shadow/effect path;
    # omitting that separate primitive does not alter child actor geometry.
    "GEO_SHADOW",
}

# GEO_SWITCH_CASE selects exactly one of its direct children. Its cases are
# not poisoned: each direct child instead becomes its own rigid group.
#
# Why that is sound, checked against the real renderer:
#   * geo_process_switch() (src/game/rendering_graph_node.c:386) walks to the
#     selectedCase-th child and processes only that node, and
#     geo_process_node_and_siblings() sets `iterateChildren = (parent->type !=
#     GRAPH_NODE_TYPE_SWITCH_CASE)` (line 1288), so the chosen child's
#     siblings are *not* rendered.
#   * Therefore every triangle inside one case is drawn whenever that case is
#     drawn, and all of them hang below the same parent transform -- rigid.
#   * Two different cases are never merged because they hold different group
#     ids, which is a stronger guarantee than marking them unsafe: it also
#     survives a case being nested inside another case.
_GEO_SWITCH_NODES = {"GEO_SWITCH_CASE"}

# Nodes whose subtree is not a rest-pose guarantee.
#   GEO_ASM: an arbitrary runtime callback. It can hide its subtree or return
#     extra geometry. (Where an ASM node only rewrites a *sibling* transform
#     node's angles -- SM64's usual pattern -- that is rigid-safe by
#     construction: the whole subtree still shares one matrix. Poisoning the
#     subtree covers the cases that are not.) The switch-case argument above
#     does not extend to it: an ASM callback is not a fixed set of variants.
_GEO_POISON_NODES = {
    "GEO_ASM": "geo_asm",
}

# Nodes that carry a display-list argument in their last field.
_GEO_NODES_WITH_DISPLAY_LIST = {"GEO_ANIMATED_PART", "GEO_DISPLAY_LIST"} | {
    name for name in _GEO_TRANSFORM_NODES
    if name.endswith("_WITH_DL") or name.endswith("_AND_DL")
}

REASON_UNKNOWN_MACRO = "unknown_macro"
REASON_UNBALANCED_POP = "unbalanced_pop_matrix"
REASON_GEO_ASM = _GEO_POISON_NODES["GEO_ASM"]
REASON_UNKNOWN_GEO_NODE = "unknown_geo_node"
REASON_TEXTURED = "textured"

_IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
_SYMBOL = re.compile(r"\s*&?(\w+)")


class _Frame(NamedTuple):
    """State of one geo node, and (when pushed) of one sibling scope."""

    group: int
    poisoned: bool
    reasons: frozenset[str]
    #: True on a GEO_SWITCH_CASE node, and therefore on the scope its children
    #: live in: every node registered directly in that scope is a separate
    #: case and gets its own rigid group.
    switch_scope: bool = False


@dataclass(frozen=True)
class TriangleSite:
    ordinal: int
    rigid_group: int
    indices: tuple[int, int, int]
    unsafe: bool
    reasons: frozenset[str] = field(default_factory=frozenset)
    display_list: str = ""
    #: Index of this triangle command inside `display_list` alone. Unlike
    #: `ordinal`, this survives GEO_SWITCH_CASE selection, LOD selection and
    #: SM64's per-layer master-list bucketing, because the runtime always
    #: executes the same Gfx array from its start. See the Task 1 report.
    list_ordinal: int = 0


# Same tokenizer as extract_mario_actor.ints(): real triangle macros write the
# flag argument in hex (`gsSP2Triangles( 0,  1,  2, 0x0, ...)`), and dropping it
# would shift every following index by one.
_INT_TOKEN = re.compile(r"(?<![A-Za-z_])(?:0x[0-9A-Fa-f]+|-?\d+)")


def _ints(args: str) -> list[int]:
    return [int(value, 0) for value in _INT_TOKEN.findall(args)]


def _bound_display_list(macro: str, args: str) -> str | None:
    """Return the display list a geo node binds, if any."""
    if macro not in _GEO_NODES_WITH_DISPLAY_LIST:
        return None
    fields = [value.strip() for value in args.split(",")]
    last = fields[-1] if fields else ""
    if last and last != "NULL" and _IDENTIFIER.fullmatch(last):
        return last
    return None


class _Walker:
    """Shared ordinal/group allocator for the geo and display-list levels."""

    def __init__(
        self,
        layouts: dict[str, list[tuple[str, str]]],
        lists: dict[str, list[tuple[str, str]]],
    ) -> None:
        self.layouts = layouts
        self.lists = lists
        self.sites: list[TriangleSite] = []
        self._ordinal = 0
        self._next_group = 0
        # Fast3D texture state persists across sibling and nested display
        # lists exactly as extract_mario_actor.flatten_parts models it.
        self._texture: str | None = None
        # Geo-layout node stack (mirrors gCurGraphNodeList/gCurGraphNodeIndex).
        self._scope = _Frame(0, False, frozenset())
        self._node = self._scope
        self._node_stack: list[_Frame] = []

    # -- allocation ------------------------------------------------------
    def _new_group(self) -> int:
        self._next_group += 1
        return self._next_group

    # -- display-list level ----------------------------------------------
    def walk_list(
        self,
        name: str,
        group: int,
        poisoned: bool,
        reasons: frozenset[str],
        stack: tuple[str, ...] = (),
    ) -> None:
        if name in stack:
            raise ValueError(
                f"recursive display list: {' -> '.join(stack + (name,))}"
            )
        if len(stack) >= 256:
            raise ValueError(f"display-list traversal depth exceeds 256 at {name}")
        body = self.lists.get(name)
        if body is None:
            raise ValueError(f"missing display list {name}")
        current = group
        current_reasons = set(reasons)
        matrix_stack: list[int] = []
        local_ordinal = 0
        for position, (macro, args) in enumerate(body):
            if macro not in _KNOWN:
                # Unmodelled command: everything after it in this list is
                # suspect. Poison rather than guess, and break the group so a
                # later triangle can never share an earlier triangle's group
                # across a command that may have moved the matrix.
                poisoned = True
                current_reasons.add(REASON_UNKNOWN_MACRO)
                current = self._new_group()
                continue
            if macro == "gsSPMatrix":
                # The matrix changed, so the transform changed: always a new
                # group. But only G_MTX_PUSH grows the hardware stack, and a
                # walker stack out of step with the hardware's would let a
                # later gsSPPopMatrix restore a group that is not loaded.
                if "G_MTX_PUSH" in args:
                    matrix_stack.append(current)
                current = self._new_group()
            elif macro == "gsSPPopMatrix":
                if matrix_stack:
                    current = matrix_stack.pop()
                else:
                    # Popping past the start of this list restores a matrix
                    # this walk never saw. Unknown envelope: poison.
                    poisoned = True
                    current_reasons.add(REASON_UNBALANCED_POP)
                    current = self._new_group()
            elif macro == "gsDPSetTextureImage":
                # gsDPSetTextureImage(fmt, siz, width, timg): timg is last.
                symbol = re.search(r"(\w+)\s*$", args.strip())
                if symbol is not None:
                    self._texture = symbol.group(1)
            elif macro == "gsDPLoadTextureBlock":
                # gsDPLoadTextureBlock(timg, ...): timg is first. Missing this
                # would leave metal-Mario's textured triangles looking safe.
                symbol = _SYMBOL.match(args)
                if symbol is not None:
                    self._texture = symbol.group(1)
            elif macro == "gsSPTexture" and "G_OFF" in args:
                self._texture = None
            elif macro == "gsSPDisplayList":
                child = _SYMBOL.match(args)
                if child is None:
                    raise ValueError(f"unreadable gsSPDisplayList in {name}: {args}")
                self.walk_list(
                    child.group(1), current, poisoned,
                    frozenset(current_reasons), stack + (name,),
                )
            elif macro == "gsSPBranchList":
                child = _IDENTIFIER.fullmatch(args.strip())
                if child is None:
                    raise ValueError(f"unreadable gsSPBranchList in {name}: {args}")
                if position != len(body) - 1:
                    raise ValueError(f"gsSPBranchList must be final in {name}")
                self.walk_list(
                    child.group(0), current, poisoned,
                    frozenset(current_reasons), stack + (name,),
                )
                return
            elif macro in ("gsSP1Triangle", "gsSP2Triangles"):
                values = _ints(args)
                triples = (
                    (values[0:3], values[4:7])
                    if macro == "gsSP2Triangles"
                    else (values[0:3],)
                )
                if any(len(triple) != 3 for triple in triples):
                    raise ValueError(f"unreadable {macro} in {name}: {args}")
                site_reasons = set(current_reasons)
                unsafe = poisoned
                if self._texture is not None:
                    # Textured triangles are excluded for a different and
                    # legitimate reason: the Saturn mesh IR forces them to
                    # VDP1 triangle fallbacks (docs/saturn/SATURN_MESH_IR.md,
                    # "Current limits"). Not an unmodelled construct.
                    unsafe = True
                    site_reasons.add(REASON_TEXTURED)
                frozen = frozenset(site_reasons)
                for triple in triples:
                    self.sites.append(TriangleSite(
                        ordinal=self._ordinal,
                        rigid_group=current,
                        indices=(triple[0], triple[1], triple[2]),
                        unsafe=unsafe,
                        reasons=frozen,
                        display_list=name,
                        list_ordinal=local_ordinal,
                    ))
                    self._ordinal += 1
                    local_ordinal += 1

    # -- geo-layout level ------------------------------------------------
    def _register(
        self,
        macro: str,
        args: str,
        *,
        transform: bool = False,
        reason: str | None = None,
        switch: bool = False,
    ) -> None:
        """Register one geo node and walk any display list it binds."""
        # A transform node always opens a group. So does every direct child of
        # a GEO_SWITCH_CASE: each one is a separate case, and keeping the cases
        # in different groups is what stops two variants merging.
        group = (
            self._new_group()
            if transform or self._scope.switch_scope
            else self._scope.group
        )
        self._node = _Frame(
            group,
            self._scope.poisoned or reason is not None,
            self._scope.reasons | ({reason} if reason is not None else set()),
            switch_scope=switch,
        )
        name = _bound_display_list(macro, args)
        if name is not None:
            self.walk_list(name, self._node.group, self._node.poisoned,
                           self._node.reasons)

    def walk_layout(self, name: str, stack: tuple[str, ...] = ()) -> None:
        if name in stack:
            raise ValueError(
                f"recursive geo layout: {' -> '.join(stack + (name,))}"
            )
        tokens = self.layouts.get(name)
        if tokens is None:
            raise ValueError(f"missing geo layout {name}")
        for macro, args in tokens:
            if macro == "GEO_OPEN_NODE":
                # Descend into the node registered immediately before this.
                self._node_stack.append(self._scope)
                self._scope = self._node
            elif macro == "GEO_CLOSE_NODE":
                if not self._node_stack:
                    raise ValueError(f"unbalanced GEO_CLOSE_NODE in {name}")
                # geo_layout_cmd_close_node only decrements the index, so the
                # current node after a close is the node we descended into.
                inner, self._scope = self._scope, self._node_stack.pop()
                self._node = inner
            elif macro in ("GEO_RETURN", "GEO_END"):
                return
            elif macro in ("GEO_BRANCH", "GEO_BRANCH_AND_LINK"):
                fields = [value.strip() for value in args.split(",")]
                if macro == "GEO_BRANCH_AND_LINK":
                    target, link = fields[0], True
                elif len(fields) == 2:
                    target, link = fields[1], fields[0] == "1"
                else:
                    raise ValueError(f"unexpected GEO_BRANCH in {name}: {args}")
                # geo_layout_cmd_branch does not save gCurGraphNodeIndex, so
                # the branched layout builds into the caller's node scope.
                self.walk_layout(target, stack + (name,))
                if not link:
                    return  # a type-0 branch is a jump: no return address.
            elif macro in _GEO_TRANSFORM_NODES:
                self._register(macro, args, transform=True)
            elif macro in _GEO_SWITCH_NODES:
                self._register(macro, args, switch=True)
            elif macro in _GEO_NEUTRAL_NODES:
                self._register(macro, args)
            else:
                self._register(macro, args, reason=_GEO_POISON_NODES.get(
                    macro, REASON_UNKNOWN_GEO_NODE))


def walk_display_lists(
    lists: dict[str, list[tuple[str, str]]],
    entry: str,
) -> list[TriangleSite]:
    """Walk `entry` and return one TriangleSite per emitted triangle."""
    walker = _Walker({}, lists)
    walker.walk_list(entry, 0, False, frozenset())
    return walker.sites


def walk_geo_layout(
    layouts: dict[str, list[tuple[str, str]]],
    lists: dict[str, list[tuple[str, str]]],
    entry: str,
) -> list[TriangleSite]:
    """Walk a geo layout and every display list it binds.

    `layouts` maps a GeoLayout symbol to its tokenized macro stream, `lists`
    maps a Gfx symbol to its tokenized macro stream. Every triangle inherits
    the rigid group of the geo node that bound its display list.
    """
    walker = _Walker(layouts, lists)
    walker.walk_layout(entry)
    return walker.sites


def rigid_group_stats(sites: list[TriangleSite]) -> dict[str, object]:
    """Summarise a walk: how much merging is even possible, and what blocks it."""
    reasons: Counter[str] = Counter()
    for site in sites:
        reasons.update(site.reasons)
    safe_per_group: Counter[int] = Counter(
        site.rigid_group for site in sites if not site.unsafe
    )
    unsafe = sum(1 for site in sites if site.unsafe)
    return {
        "triangle_sites": len(sites),
        "rigid_groups": len({site.rigid_group for site in sites}),
        "safe_sites": len(sites) - unsafe,
        "unsafe_sites": unsafe,
        "unsafe_fraction": (unsafe / len(sites)) if sites else 0.0,
        "unsafe_by_reason": dict(sorted(reasons.items())),
        "groups_with_multiple_safe_sites": sum(
            1 for count in safe_per_group.values() if count > 1
        ),
        "max_safe_sites_in_a_group": max(safe_per_group.values(), default=0),
    }


def parse_display_lists(source: str) -> dict[str, list[tuple[str, str]]]:
    """Tokenize every `const Gfx name[]` block in a source file."""
    from extract_mario_actor import blocks  # local: keeps this module dep-free

    return {
        name: re.findall(r"(gs\w+)\(([^;]*?)\)", body, re.DOTALL)
        for name, body in blocks(source, "Gfx").items()
    }


def parse_geo_layouts(source: str) -> dict[str, list[tuple[str, str]]]:
    """Tokenize every `const GeoLayout name[]` block in a source file."""
    from extract_mario_actor import blocks, geo_tokens

    return {
        name: geo_tokens(body)
        for name, body in blocks(source, "GeoLayout").items()
    }


def main() -> None:
    import argparse
    import json
    from pathlib import Path

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--geo", type=Path, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--layout", required=True,
                        help="entry GeoLayout symbol, e.g. mario_geo_body")
    args = parser.parse_args()
    layouts = parse_geo_layouts(args.geo.read_text(encoding="utf-8"))
    lists = parse_display_lists(args.model.read_text(encoding="utf-8"))
    sites = walk_geo_layout(layouts, lists, args.layout)
    report = {"layout": args.layout, **rigid_group_stats(sites)}
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
