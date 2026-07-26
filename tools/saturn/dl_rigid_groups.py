"""Assign each display-list triangle a stable ordinal and a rigid group.

A rigid group is the modelview matrix-stack identity in effect when a
triangle is emitted. Two triangles in the same group are transformed by the
same joint, so they move together rigidly under every animation: a merge
proven safe at rest stays safe in all poses. Triangles in different groups
can be pulled apart arbitrarily and are never merged.

Ordinals are assigned in the order the real Fast3D interpreter would reach
the commands, so they are a stable identity across frames and poses. That is
what lets the safety analysis be an offline bytecode problem.
"""
from __future__ import annotations

from dataclasses import dataclass

# Macros the walker understands. Anything else marks its group unsafe rather
# than being skipped -- an unmodelled construct is an unknown envelope, and
# this compiler never treats unknown as safe.
_KNOWN = {
    "gsSPVertex", "gsSP1Triangle", "gsSP2Triangles", "gsSPDisplayList",
    "gsSPMatrix", "gsSPPopMatrix", "gsSPEndDisplayList",
    "gsSPLight", "gsSPSetGeometryMode", "gsSPClearGeometryMode",
    "gsSPTexture", "gsDPPipeSync", "gsDPSetCombineMode",
}


@dataclass(frozen=True)
class TriangleSite:
    ordinal: int
    rigid_group: int
    indices: tuple[int, int, int]
    unsafe: bool


def _ints(args: str) -> list[int]:
    out = []
    for token in args.split(","):
        token = token.strip()
        if token.lstrip("-").isdigit():
            out.append(int(token))
    return out


def walk_display_lists(
    lists: dict[str, list[tuple[str, str]]],
    entry: str,
) -> list[TriangleSite]:
    """Walk `entry` and return one TriangleSite per emitted triangle."""
    sites: list[TriangleSite] = []
    counter = {"ordinal": 0, "next_group": 1}

    def walk(name: str, group: int, unsafe: bool) -> None:
        stack: list[int] = []
        current = group
        poisoned = unsafe
        for macro, args in lists.get(name, []):
            if macro not in _KNOWN:
                # Unmodelled command: everything after it in this list is
                # suspect. Poison rather than guess.
                poisoned = True
                continue
            if macro == "gsSPMatrix":
                stack.append(current)
                counter["next_group"] += 1
                current = counter["next_group"]
            elif macro == "gsSPPopMatrix":
                current = stack.pop() if stack else current
            elif macro == "gsSPDisplayList":
                walk(args.strip(), current, poisoned)
            elif macro in ("gsSP1Triangle", "gsSP2Triangles"):
                values = _ints(args)
                triples = (
                    (values[0:3], values[4:7])
                    if macro == "gsSP2Triangles"
                    else (values[0:3],)
                )
                for triple in triples:
                    sites.append(TriangleSite(
                        ordinal=counter["ordinal"],
                        rigid_group=current,
                        indices=(triple[0], triple[1], triple[2]),
                        unsafe=poisoned,
                    ))
                    counter["ordinal"] += 1

    walk(entry, 0, False)
    return sites
