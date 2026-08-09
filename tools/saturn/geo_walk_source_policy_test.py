#!/usr/bin/env python3
"""Reject direct recursive scene-graph calls in the Saturn production source.

The full per-node-type handler set was migrated off native recursion onto
the bounded iterative geo-walk runtime across waves 1-4 (see CHANGELOG.md).
Three call sites are permanent-by-design and intentionally excluded from
that migration -- converting any of them means turning
`geo_process_node_and_siblings` itself into a bounded-walk entry point,
a materially larger change than converting a per-node-type handler:

  * `saturn_geo_walk_process_children`'s `sSaturnGeoWalkActive` reentrancy
    guard fallback -- deliberate real recursion when a walk is already in
    flight, so as not to touch the shared frame array from a nested call.
  * `geo_try_process_children` -- the generic children-only bridge used by
    node types with no per-type handler (`ROOT`/`START`/`CULLING_RADIUS`).
  * `geo_process_root` -- the top-level walk kickoff.

This test allowlists exactly those three call sites by enclosing function
name and still fails on any direct recursive call found anywhere else, or
on a missing/duplicated allowlisted site -- both are regressions.
"""
from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "game" / "rendering_graph_node.c"

# Enclosing function name -> required number of direct recursive calls.
ALLOWED_ENCLOSING_CALL_SITES = {
    "saturn_geo_walk_process_children": 1,
    "geo_try_process_children": 1,
    "geo_process_root": 1,
}

# Top-level (column-0) function definition: "<ret type> name(...) {" on one line.
FUNC_START_RE = re.compile(r"^\w[\w \*]*?\b(\w+)\s*\([^;\n]*\)\s*\{\s*$", re.MULTILINE)


def _enclosing_function_for(text: str, call_start: int, func_starts: list[tuple[int, str]]) -> str | None:
    enclosing = None
    for pos, name in func_starts:
        if pos > call_start:
            break
        enclosing = name
    return enclosing


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    # Exclude the public definition itself; every remaining call is either a
    # handler recursion edge (must be on the bounded runtime by now) or one
    # of the three permanent-by-design call sites allowlisted above.
    def_match = re.search(r"void\s+(geo_process_node_and_siblings)\s*\([^)]*\)\s*\{", text)
    assert def_match is not None, "geo_process_node_and_siblings definition not found"
    def_name_start = def_match.start(1)

    func_starts = sorted(
        (m.start(), m.group(1)) for m in FUNC_START_RE.finditer(text)
    )

    direct_calls = [
        m for m in re.finditer(r"\bgeo_process_node_and_siblings\s*\(", text)
        if m.start() != def_name_start
    ]

    counts: dict[str, int] = {}
    unaccounted: list[int] = []
    for m in direct_calls:
        enclosing = _enclosing_function_for(text, m.start(), func_starts)
        if enclosing in ALLOWED_ENCLOSING_CALL_SITES:
            counts[enclosing] = counts.get(enclosing, 0) + 1
        else:
            unaccounted.append(m.start())

    assert not unaccounted, (
        f"production Saturn geo path has {len(unaccounted)} direct recursive "
        "dispatcher call(s) outside the allowlisted permanent-by-design "
        f"call sites {sorted(ALLOWED_ENCLOSING_CALL_SITES)}: this is a "
        "regression -- new handler recursion must go through the bounded "
        "geo-walk runtime instead"
    )
    for name, expected in ALLOWED_ENCLOSING_CALL_SITES.items():
        actual = counts.get(name, 0)
        assert actual == expected, (
            f"expected exactly {expected} direct recursive call(s) inside "
            f"'{name}', found {actual}"
        )

    # Confirms the bounded runtime type is actually instantiated in this
    # file (not merely reachable via a header include). Was previously
    # "saturn_geo_walk_runtime_frame_t", a name that never existed in this
    # source (missing "sm64_" prefix, spurious "_frame" -- the real,
    # instantiated type is "sm64_saturn_geo_walk_runtime_t", declared at
    # the local `walk` variable in saturn_geo_walk_process_children());
    # that made this assert dead/unsatisfiable since the test's original
    # commit, unrelated to and predating the wave 1-4 handler conversion.
    assert "sm64_saturn_geo_walk_runtime_t" in text
    assert ".lwram_geo_traversal" in (ROOT / "src/port/saturn/runtime/saturn_geo_walk_storage.c").read_text(encoding="utf-8")
    print("geo walk source policy: PASS (3 allowlisted permanent call sites, 0 unaccounted)")


if __name__ == "__main__":
    main()
