# General Quad Merging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit one VDP1 command for two coplanar, same-rigid-group triangles instead of two, for every model the sourceboot frontend draws.

**Architecture:** An offline host tool walks each model's real display-list bytecode, assigns every triangle a *rigid group* from the `G_MTX`/push/pop nesting in effect at that point, and runs the existing quad-pairing safety checks only within a group. The result is a generated C table keyed by triangle-command ordinal. At runtime the frontend counts ordinals, looks up the table, and merges flagged pairs in the resolve stage.

**Tech Stack:** Python 3 host tooling (`tools/saturn/`, hash-pinned NetworkX), C11 SH-2 target code (`src/port/saturn/gfx/`), Yaul SDK, `make -f Makefile.saturn.mk` host suites.

---

## Correction 0: rigid groups come from the geo layout, not from `gsSPMatrix`

**Found during Task 1 execution, 2026-07-25. This supersedes Task 1 as originally written.**

The plan assumed a triangle's joint is determined by `gsSPMatrix`/`gsSPPopMatrix`
nesting inside the display list. Measured against the real tree, that is wrong:

```
$ grep -rho "gsSPMatrix([^)]*)" actors/ levels/bob/ | wc -l
0
```

`gsSPMatrix` appears **zero times** in the actor and level display lists. Mario's
joint hierarchy lives entirely in his **geo layout** (`actors/mario/geo.inc.c`):

| Node | Count | Role |
|---|---|---|
| `GEO_ANIMATED_PART` | 336 | a joint — introduces a new transform |
| `GEO_OPEN_NODE` / `GEO_CLOSE_NODE` | 391 each | the actual push/pop |
| `GEO_DISPLAY_LIST` | 136 | binds a display list to the current node |
| `GEO_ROTATION_NODE` / `GEO_TRANSLATE_ROTATE` / `GEO_SCALE` | 36 / 16 / 37 | further transforms |
| `GEO_SWITCH_CASE` | 45 | costume/cap variant selection |
| `GEO_BRANCH` / `GEO_RETURN` | 53 / 48 | subroutine structure |

So the rigid-group walk is **two-level**: walk the geo layout to establish the
transform hierarchy, and where a `GEO_DISPLAY_LIST` binds a list, walk that list
for triangle ordinals — every triangle in it inheriting the geo node's group.

Precedent for the geo half already exists: `tools/saturn/extract_mario_actor.py`
has `geo_layout_parts()` (line 144), which parses this exact structure and
composes the matrices. Reuse its parsing conventions.

Transform-introducing nodes (`GEO_ANIMATED_PART`, `GEO_ROTATION_NODE`,
`GEO_TRANSLATE_ROTATE`, `GEO_SCALE`) each start a new group; `GEO_OPEN_NODE`
pushes and `GEO_CLOSE_NODE` pops. `GEO_SWITCH_CASE` and `GEO_ASM` poison their
subtree — the spec already called for switch-case subtrees to be forced to
fallback.

The DL-level `gsSPMatrix`/`gsSPPopMatrix` handling stays in the walker: it costs
nothing, and it is correct if any display list ever does use it.

**Also corrected while here (both flagged by Task 1's implementer, both real):**

- **`gsSPMatrix` without `G_MTX_PUSH` must not push.** Treating every
  `gsSPMatrix` as a push desyncs the walker's stack from the hardware's by one,
  which can assign *the same* group to triangles under genuinely different
  matrices — an unsafe-merge direction. Allocate a new group always (the matrix
  changed), but push only when `G_MTX_PUSH` is present in the arguments.
- **`gsSPPopMatrix` on an empty stack must poison,** not silently keep the
  current group. Same unsafe direction.

## Correction 0b: the test runner is `unittest`, not `pytest`

`pytest` is not installed in `.venv-saturn-tools` and is not in the hash-pinned
`tools/saturn/requirements.txt`. The canonical runner is:

```bash
make -f Makefile.saturn.mk OS=Windows_NT verify-tools SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe
```

which ends in `unittest.main()`. **Bare module-level `test_*` functions are
silently never executed by `unittest.main()`** — the tests in Tasks 1, 2 and 5 as
originally written would have been dead code in the only path that runs them.
Write them as `unittest.TestCase` methods, preserving the test names and
assertion semantics given in each task. Task 1 already did this.

## Two corrections to the design spec

The spec was written before the emit adapter was read closely. Both corrections are load-bearing; do not follow the spec where it disagrees with this section.

**1. The spec says "the VDP1 emission backend needs no changes." That is true of the *backend* but false of the *emit adapter*.** `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c:53-60` hardcodes the degenerate quad by repeating index 2:

```c
INT16_VEC2_INITIALIZER(tri->x[2], tri->y[2]),
INT16_VEC2_INITIALIZER(tri->x[2], tri->y[2])
```

and line 77 does the same for the Gouraud table (`table->colors[3] = tri->corner_rgb1555[2]`). A true quad needs a real fourth corner, so `saturn_fast3d_vdp1_emit.c` **is** in scope. `saturn_vdp1_backend.*` genuinely is not.

**2. `sm64_saturn_resolved_triangle_t` only holds three corners** (`saturn_fast3d_frontend.h:272-277`: `x[3]`, `y[3]`, `corner_rgb1555[3]`, `depth_bucket`). It must grow to four. Cost, measured not estimated:

- current struct: 20 bytes; four-corner struct: 26 bytes
- `SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES` is 1536 (`saturn_fast3d_frontend.h:26`)
- growth: `1536 × 6 = 9,216` bytes of HWRAM

Measured margin on `master` at `8adaafe`: `___end = 0x060dc0a4`, HWRAM top `0x06100000`, so **147,292 bytes free** against the linker's 4,096-byte TLSF floor (`sourceboot-cart.x`). 9,216 bytes is affordable with large margin. Task 4 re-measures anyway rather than trusting this figure.

---

## File structure

| File | Responsibility |
|---|---|
| `tools/saturn/dl_rigid_groups.py` (create) | Walk display-list bytecode; assign each triangle-emitting command an ordinal and a rigid-group id. Nothing about quads. |
| `tools/saturn/quad_map.py` (create) | Combine the walker's groups with `quad_pairing.candidates()`; emit the ordinal-keyed quad map and its C rendering. |
| `tools/saturn/test_tools.py` (modify) | Host tests for both new modules. |
| `tools/saturn/quad_pairing.py` | **Unchanged.** Reused as-is. |
| `src/port/saturn/gfx/saturn_fast3d_frontend.h` (modify) | Four-corner `resolved_triangle`; quad-map lookup declarations; map-mismatch counter. |
| `src/port/saturn/gfx/saturn_fast3d_frontend.c` (modify) | Ordinal counter, map lookup, resolve-stage merge. |
| `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c` (modify) | Read the real fourth corner instead of duplicating index 2. |
| `Makefile.saturn.mk` (modify) | Generate the quad map as a build dependency. |

`dl_rigid_groups.py` and `quad_map.py` are deliberately separate: the walker answers "which joint owns this triangle," the map answers "which pairs are safe." They are tested independently.

---

### Task 1: Display-list walker — rigid groups

Precedent to read first: `tools/saturn/extract_mario_actor.py:245-320` (`flatten`) already walks `gsSPVertex`/`gsSP1Triangle`/`gsSP2Triangles`/`gsSPDisplayList` and models Fast3D's persistent vertex cache. Follow its parsing conventions. It does **not** track matrix state — that is what this task adds.

**Files:**
- Create: `tools/saturn/dl_rigid_groups.py`
- Modify: `tools/saturn/test_tools.py`

- [ ] **Step 1: Write the failing tests**

Append to `tools/saturn/test_tools.py`:

```python
from dl_rigid_groups import walk_display_lists, TriangleSite


def test_single_list_all_one_rigid_group():
    lists = {
        "body": [
            ("gsSPVertex", "v_body, 3, 0"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
            ("gsSP1Triangle", "0, 2, 1, 0"),
        ]
    }
    sites = walk_display_lists(lists, "body")
    assert [s.ordinal for s in sites] == [0, 1]
    assert sites[0].rigid_group == sites[1].rigid_group


def test_push_pop_creates_distinct_groups():
    """Triangles under different matrix pushes must never share a group."""
    lists = {
        "root": [
            ("gsSPVertex", "v_a, 3, 0"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
            ("gsSPMatrix", "arm_mtx, G_MTX_MODELVIEW | G_MTX_PUSH"),
            ("gsSPVertex", "v_b, 3, 0"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
            ("gsSPPopMatrix", "G_MTX_MODELVIEW"),
            ("gsSP1Triangle", "0, 2, 1, 0"),
        ]
    }
    sites = walk_display_lists(lists, "root")
    assert [s.ordinal for s in sites] == [0, 1, 2]
    assert sites[0].rigid_group != sites[1].rigid_group
    assert sites[0].rigid_group == sites[2].rigid_group, "pop must restore the prior group"


def test_gssp2triangles_emits_two_ordinals():
    lists = {
        "body": [
            ("gsSPVertex", "v, 4, 0"),
            ("gsSP2Triangles", "0, 1, 2, 0, 0, 2, 3, 0"),
        ]
    }
    sites = walk_display_lists(lists, "body")
    assert [s.ordinal for s in sites] == [0, 1]
    assert sites[0].indices == (0, 1, 2)
    assert sites[1].indices == (0, 2, 3)


def test_nested_display_list_continues_ordinals_and_inherits_group():
    lists = {
        "root": [
            ("gsSPVertex", "v, 3, 0"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
            ("gsSPDisplayList", "child"),
        ],
        "child": [
            ("gsSPVertex", "v2, 3, 0"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
        ],
    }
    sites = walk_display_lists(lists, "root")
    assert [s.ordinal for s in sites] == [0, 1]
    assert sites[0].rigid_group == sites[1].rigid_group


def test_unknown_macro_marks_sites_unsafe():
    """An unmodelled construct must poison the group, never be ignored."""
    lists = {
        "root": [
            ("gsSPVertex", "v, 3, 0"),
            ("gsSPBranchList", "somewhere_else"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
        ]
    }
    sites = walk_display_lists(lists, "root")
    assert sites[0].unsafe is True
```

- [ ] **Step 2: Run to verify they fail**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && ./.venv-saturn-tools/Scripts/python.exe -m pytest tools/saturn/test_tools.py -k rigid -v
```
Expected: FAIL, `ModuleNotFoundError: No module named 'dl_rigid_groups'`.

- [ ] **Step 3: Implement the walker**

Create `tools/saturn/dl_rigid_groups.py`:

```python
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
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && ./.venv-saturn-tools/Scripts/python.exe -m pytest tools/saturn/test_tools.py -k rigid -v
```
Expected: 5 passed.

- [ ] **Step 5: Commit**

```bash
git add tools/saturn/dl_rigid_groups.py tools/saturn/test_tools.py
git commit -m "feat(saturn): display-list walker assigning triangle rigid groups"
```

---

### Task 2: Quad map compiler

**Files:**
- Create: `tools/saturn/quad_map.py`
- Modify: `tools/saturn/test_tools.py`

- [ ] **Step 1: Write the failing tests**

Append to `tools/saturn/test_tools.py`:

```python
from quad_map import build_quad_map, QuadMapEntry


def _square_lists():
    """Two triangles forming a unit square, same group, shared edge 0-2."""
    return {
        "flat": [
            ("gsSPVertex", "v, 4, 0"),
            ("gsSP2Triangles", "0, 1, 2, 0, 0, 2, 3, 0"),
        ]
    }


_SQUARE_VERTS = [(0, 0, 0), (100, 0, 0), (100, 100, 0), (0, 100, 0)]


def test_same_group_coplanar_pair_merges():
    entries, stats = build_quad_map(_square_lists(), "flat", _SQUARE_VERTS)
    assert entries[0].partner_ordinal == 1
    assert entries[1].partner_ordinal == 0
    assert stats["quads"] == 1


def test_cross_group_pair_never_merges():
    """Identical geometry, but a matrix push between them: must not merge."""
    lists = {
        "flat": [
            ("gsSPVertex", "v, 4, 0"),
            ("gsSP1Triangle", "0, 1, 2, 0"),
            ("gsSPMatrix", "joint, G_MTX_MODELVIEW | G_MTX_PUSH"),
            ("gsSP1Triangle", "0, 2, 3, 0"),
        ]
    }
    entries, stats = build_quad_map(lists, "flat", _SQUARE_VERTS)
    assert entries[0].partner_ordinal is None
    assert entries[1].partner_ordinal is None
    assert stats["quads"] == 0
    assert stats["rejected_cross_group"] == 1


def test_unsafe_site_is_forced_to_fallback():
    lists = {
        "flat": [
            ("gsSPVertex", "v, 4, 0"),
            ("gsSPBranchList", "elsewhere"),
            ("gsSP2Triangles", "0, 1, 2, 0, 0, 2, 3, 0"),
        ]
    }
    entries, stats = build_quad_map(lists, "flat", _SQUARE_VERTS)
    assert all(e.partner_ordinal is None for e in entries)


def test_every_ordinal_has_exactly_one_entry():
    entries, _ = build_quad_map(_square_lists(), "flat", _SQUARE_VERTS)
    assert [e.ordinal for e in entries] == list(range(len(entries)))


def test_pairing_is_symmetric():
    entries, _ = build_quad_map(_square_lists(), "flat", _SQUARE_VERTS)
    for entry in entries:
        if entry.partner_ordinal is not None:
            assert entries[entry.partner_ordinal].partner_ordinal == entry.ordinal
```

- [ ] **Step 2: Run to verify they fail**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && ./.venv-saturn-tools/Scripts/python.exe -m pytest tools/saturn/test_tools.py -k quad_map -v
```
Expected: FAIL, `ModuleNotFoundError: No module named 'quad_map'`.

- [ ] **Step 3: Implement the compiler**

Create `tools/saturn/quad_map.py`:

```python
"""Compile an ordinal-keyed quad map from real display-list bytecode.

Eligibility is the intersection of two independent gates:

1. Structural: both triangles share a rigid group (dl_rigid_groups) and
   neither sits in an unsafe region.
2. Geometric: the pair passes the existing mesh-IR safety checks --
   material, shared edge, winding, normal alignment, convexity -- via
   quad_pairing.candidates(), reused unchanged.

Gate 1 runs first and is structural, so no amount of favourable rest-pose
geometry can merge across a joint boundary.
"""
from __future__ import annotations

from dataclasses import dataclass

import quad_pairing
from dl_rigid_groups import walk_display_lists


@dataclass(frozen=True)
class QuadMapEntry:
    ordinal: int
    partner_ordinal: int | None


def build_quad_map(
    lists: dict[str, list[tuple[str, str]]],
    entry: str,
    vertices: list[tuple[int, int, int]],
) -> tuple[list[QuadMapEntry], dict[str, int]]:
    sites = walk_display_lists(lists, entry)
    stats = {"sites": len(sites), "quads": 0, "rejected_cross_group": 0,
             "rejected_unsafe": 0, "rejected_geometry": 0}

    # quad_pairing works on (material, i0, i1, i2) faces indexed positionally.
    # Use the rigid group as the material channel: quad_pairing already
    # refuses to merge faces whose material differs, so encoding the group
    # there gets the structural gate enforced by construction.
    faces = [(site.rigid_group, *site.indices) for site in sites]
    forbidden = {s.ordinal for s in sites if s.unsafe}
    stats["rejected_unsafe"] = len(forbidden)

    options, rejected = quad_pairing.candidates(
        vertices, faces, pairing_forbidden_triangles=forbidden
    )
    stats["rejected_geometry"] = sum(rejected.values())

    # Count structurally-rejected adjacencies for reporting: shared-edge
    # neighbours that differ only by rigid group.
    edges: dict[tuple[int, int], list[int]] = {}
    for index, site in enumerate(sites):
        i0, i1, i2 = site.indices
        for edge in ((i0, i1), (i1, i2), (i2, i0)):
            edges.setdefault(tuple(sorted(edge)), []).append(index)
    for linked in edges.values():
        if len(linked) == 2:
            first, second = linked
            if sites[first].rigid_group != sites[second].rigid_group:
                stats["rejected_cross_group"] += 1

    matched = quad_pairing.maximum_weight_matching(options)
    entries: list[QuadMapEntry] = []
    for index in range(len(sites)):
        option = matched.get(index)
        if option is None:
            entries.append(QuadMapEntry(index, None))
            continue
        partner = option.second if option.first == index else option.first
        entries.append(QuadMapEntry(index, partner))
    stats["quads"] = sum(1 for e in entries if e.partner_ordinal is not None) // 2
    return entries, stats
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && ./.venv-saturn-tools/Scripts/python.exe -m pytest tools/saturn/test_tools.py -k quad_map -v
```
Expected: 5 passed.

- [ ] **Step 5: Run the whole host tool suite for regressions**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && ./.venv-saturn-tools/Scripts/python.exe -m pytest tools/saturn/test_tools.py -v
```
Expected: all pass, including the pre-existing mesh-IR tests.

- [ ] **Step 6: Commit**

```bash
git add tools/saturn/quad_map.py tools/saturn/test_tools.py
git commit -m "feat(saturn): compile ordinal-keyed quad maps from display-list bytecode"
```

---

### Task 3: Mutation-test the safety gates

This project does not trust generated tests until mutations demonstrably fail them. The structural gate is the whole correctness argument for animated geometry, so it gets mutated first.

**Files:** none committed — this task edits, verifies, and reverts.

- [ ] **Step 1: Mutation A — collapse rigid groups**

In `tools/saturn/dl_rigid_groups.py`, in `walk_display_lists`, change the `gsSPMatrix` branch so it does **not** allocate a new group:

```python
            if macro == "gsSPMatrix":
                stack.append(current)   # group deliberately NOT changed
```

Run: `./.venv-saturn-tools/Scripts/python.exe -m pytest tools/saturn/test_tools.py -k "rigid or quad_map" -v`
Expected: `test_push_pop_creates_distinct_groups` and `test_cross_group_pair_never_merges` both FAIL. Revert.

- [ ] **Step 2: Mutation B — ignore unknown macros**

In `walk_display_lists`, change the unknown-macro branch to `continue` without setting `poisoned = True`.

Run the same command.
Expected: `test_unknown_macro_marks_sites_unsafe` and `test_unsafe_site_is_forced_to_fallback` FAIL. Revert.

- [ ] **Step 3: Mutation C — drop the forbidden set**

In `tools/saturn/quad_map.py`, pass `pairing_forbidden_triangles=set()` instead of `forbidden`.

Run the same command.
Expected: `test_unsafe_site_is_forced_to_fallback` FAILS. Revert.

- [ ] **Step 4: Mutation D — break pop restoration**

In `walk_display_lists`, change the `gsSPPopMatrix` branch to `pass`.

Run the same command.
Expected: `test_push_pop_creates_distinct_groups` FAILS on its `pop must restore` assertion. Revert.

- [ ] **Step 5: Mutation E — asymmetric pairing**

In `build_quad_map`, always set `partner = option.second`.

Run the same command.
Expected: `test_pairing_is_symmetric` FAILS. Revert.

- [ ] **Step 6: Confirm the tree is clean and record the result**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && git diff --exit-code tools/saturn/
```
Expected: no output (exit 0) — every mutation reverted.

All five mutations must have been caught. If any survived, the test set is insufficient: add a test that fails under that mutation, then re-run the whole mutation sequence before proceeding. Record the outcome in the Task 3 completion note.

---

### Task 4: Four-corner resolved triangle

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h:272-277`
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c:53-77`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c:419-480`

This task changes representation only. No merging happens yet, and the rendered output must be byte-identical — every primitive still duplicates its last corner, just explicitly instead of implicitly.

- [ ] **Step 1: Measure the HWRAM margin before the change**

```bash
/c/msys64/usr/bin/bash.exe -lc "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port; source .yaul.env; sh-elf-nm build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf | grep ' ___end'"
```
Record the address. Margin is `0x06100000 - ___end`. On `master` at `8adaafe` this was `0x060dc0a4`, i.e. 147,292 bytes free against the linker's 4,096-byte floor. Re-measure rather than trusting that figure.

- [ ] **Step 2: Widen the struct**

In `saturn_fast3d_frontend.h`, replace the `sm64_saturn_resolved_triangle_t` definition:

```c
/* Four corners, not three. VDP1's native primitive is a quadrilateral, so
 * a triangle and a quad cost the same one command; corner 3 is the real
 * fourth vertex for a merged quad, or a copy of corner 2 for a triangle.
 * Making the duplication explicit here (rather than implicit in the emit
 * adapter) is what lets the resolve stage produce true quads without the
 * emit stage needing to know which it is looking at.
 *
 * HWRAM cost: 26 bytes per entry, up from 20, times
 * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES -- see the budget comment on
 * that macro before raising either number. */
typedef struct sm64_saturn_resolved_triangle {
    int16_t x[4];
    int16_t y[4];
    uint16_t corner_rgb1555[4];
    uint16_t depth_bucket;
} sm64_saturn_resolved_triangle_t;
```

- [ ] **Step 3: Populate corner 3 in the resolve stage**

In `saturn_fast3d_frontend.c`, where the resolved entry is filled (around line 426, after `out->depth_bucket` is set), append the explicit duplication:

```c
    /* Triangle: corner 3 repeats corner 2, the degenerate-quad convention
     * VDP1 has always been given here. Task 5 overwrites this for merged
     * pairs; until then every primitive is still a triangle. */
    out->x[3] = out->x[2];
    out->y[3] = out->y[2];
    out->corner_rgb1555[3] = out->corner_rgb1555[2];
```

- [ ] **Step 4: Read the real fourth corner in the emit adapter**

In `saturn_fast3d_vdp1_emit.c`, replace the hardcoded duplication at lines 53-60:

```c
            /* Four real corners. For a triangle the resolve stage has
             * already set corner 3 == corner 2, so this stays a degenerate
             * quad; for a merged pair it is the genuine fourth vertex. */
            const int16_vec2_t quad_vertices[] = {
                INT16_VEC2_INITIALIZER(tri->x[0], tri->y[0]),
                INT16_VEC2_INITIALIZER(tri->x[1], tri->y[1]),
                INT16_VEC2_INITIALIZER(tri->x[2], tri->y[2]),
                INT16_VEC2_INITIALIZER(tri->x[3], tri->y[3])
            };
```

and the Gouraud table at line 77:

```c
                table->colors[3] = tri->corner_rgb1555[3];
```

- [ ] **Step 5: Cross-compile and re-measure**

```bash
/c/msys64/usr/bin/bash.exe -lc "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port; source .yaul.env; cd src/port/saturn/sourceboot; make -j2 && make verify"
```
Expected: exit 0 on both. The linker's `ASSERT` in `sourceboot-cart.x` is the real gate — if HWRAM had been exhausted the link would fail with the TLSF-floor message, not silently corrupt memory. Re-run the Step 1 `nm` command and confirm the new margin is still comfortably above 4,096 bytes.

- [ ] **Step 6: Run the host suites**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port
export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
for t in verify-tools verify-runtime-contracts verify-mtxq-ctors verify-mtxf-lookat-host-diff; do
  make -f Makefile.saturn.mk OS=Windows_NT $t SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe >/dev/null 2>&1
  echo "$t EXIT: $?"
done
```
Expected: all four exit 0.

- [ ] **Step 7: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.h src/port/saturn/gfx/saturn_fast3d_frontend.c src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c
git commit -m "refactor(saturn): four-corner resolved triangle, explicit degenerate quad"
```

Record the before/after HWRAM margin in the commit message.

---

### Task 5: Generate and wire the quad map

**Files:**
- Modify: `tools/saturn/quad_map.py` (add the C renderer)
- Modify: `tools/saturn/test_tools.py`
- Modify: `Makefile.saturn.mk`

- [ ] **Step 1: Write the failing test for the C renderer**

Append to `tools/saturn/test_tools.py`:

```python
from quad_map import render_quad_map_c


def test_render_quad_map_c_encodes_partners_and_sentinel():
    entries = [QuadMapEntry(0, 1), QuadMapEntry(1, 0), QuadMapEntry(2, None)]
    text = render_quad_map_c("mario", entries)
    assert "sm64_saturn_quad_map_mario" in text
    assert "0xFFFFU" in text, "unpaired ordinals need an explicit sentinel"
    assert "3U" in text, "the map must carry its own length"
```

- [ ] **Step 2: Run to verify it fails**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && ./.venv-saturn-tools/Scripts/python.exe -m pytest tools/saturn/test_tools.py -k render_quad_map -v
```
Expected: FAIL, `ImportError: cannot import name 'render_quad_map_c'`.

- [ ] **Step 3: Implement the renderer**

Append to `tools/saturn/quad_map.py`:

```python
QUAD_MAP_NONE = 0xFFFF


def render_quad_map_c(name: str, entries: list[QuadMapEntry]) -> str:
    """Render an ordinal-keyed quad map as a C table.

    QUAD_MAP_NONE marks an unpaired ordinal. There is deliberately no
    'unknown' encoding: an ordinal the compiler could not prove safe is
    written as unpaired, so a missing or truncated map degrades to today's
    one-command-per-triangle behaviour rather than to an unsafe merge.
    """
    rows = ", ".join(
        str(QUAD_MAP_NONE if e.partner_ordinal is None else e.partner_ordinal) + "U"
        for e in entries
    )
    return (
        "/* Generated by tools/saturn/quad_map.py -- do not edit. */\n"
        "#include <stdint.h>\n\n"
        f"const uint16_t sm64_saturn_quad_map_{name}[] = {{{rows}}};\n"
        f"const uint16_t sm64_saturn_quad_map_{name}_count = {len(entries)}U;\n"
    )
```

- [ ] **Step 4: Run to verify it passes**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && ./.venv-saturn-tools/Scripts/python.exe -m pytest tools/saturn/test_tools.py -k render_quad_map -v
```
Expected: 1 passed.

- [ ] **Step 5: Wire generation into the build**

In `Makefile.saturn.mk`, follow the existing generated-source pattern used for `mario_anim_data` (find it with `grep -n "mario_anim_data" Makefile.saturn.mk`) and add a rule that regenerates the quad map whenever its mesh source changes, so a stale map is not reachable within a build.

- [ ] **Step 6: Commit**

```bash
git add tools/saturn/quad_map.py tools/saturn/test_tools.py Makefile.saturn.mk
git commit -m "feat(saturn): generate quad maps as a build dependency"
```

---

### Task 6: Runtime merge in the resolve stage

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`

- [ ] **Step 1: Add the ordinal counter and mismatch sensor**

In `saturn_fast3d_frontend.h`, add to `sm64_saturn_fast3d_profile_t` (at the end, preserving every existing offset — this project ground-truths offsets with `offsetof()` probes and reordering would invalidate every recorded figure):

```c
    uint32_t quads_merged;        /* pairs emitted as one command */
    uint32_t quad_map_mismatch;   /* live ordinal outside the map's range */
```

and to `sm64_saturn_fast3d_frontend_t`, beside `geometry_mode` (persistent state, not per-frame profile):

```c
    /* Triangle-command ordinal within the display list being walked. The
     * quad map is keyed by this. Reset per submit(), like resolved_count. */
    uint16_t triangle_ordinal;
```

- [ ] **Step 2: Reset the ordinal per frame**

In `saturn_fast3d_frontend.c`, beside the existing `frontend->resolved_count = 0U;` in `submit()` (around line 1007):

```c
    frontend->triangle_ordinal = 0U;
```

- [ ] **Step 3: Look up and merge**

In the resolve path, after a triangle is successfully resolved, consult the map. The merge rule: when ordinal *N* pairs with *N+1*, hold *N* back, and when *N+1* resolves, write its non-shared corner into the held entry's corner 3 and emit one primitive.

```c
    /* A partner that is not exactly the next ordinal is not merged. The map
     * is free to pair any two ordinals, but this frontend only buffers one
     * primitive, so non-adjacent pairs degrade to two commands rather than
     * requiring an unbounded hold buffer. Counted, not silent. */
```

Guard every lookup:

```c
    if (ordinal >= sm64_saturn_quad_map_count) {
        profile->quad_map_mismatch++;
        /* Out of range: emit as a triangle. A missing map entry is never
         * "safe to merge". */
    }
```

- [ ] **Step 4: Cross-compile and run the host suites**

```bash
/c/msys64/usr/bin/bash.exe -lc "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port; source .yaul.env; cd src/port/saturn/sourceboot; make -j2 && make verify"
```
then the four host targets exactly as in Task 4 Step 6. Expected: all exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.h src/port/saturn/gfx/saturn_fast3d_frontend.c
git commit -m "feat(saturn): merge mapped triangle pairs into true VDP1 quads"
```

---

### Task 7: Measure, verify, and gate on the user's eyes

**Files:** evidence only.

- [ ] **Step 1: Full regression**

All four host suites (Task 4 Step 6), the SH-2 cross-compile, and `make verify`. Expected: every one exits 0.

- [ ] **Step 2: Live capture**

Re-resolve `_sourceboot_fast3d` fresh against the just-built ELF, then capture at the established free-roam depth:

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port
./.venv-saturn-tools/Scripts/python.exe tools/saturn/capture_hwtest.py \
  --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" \
  --ipl "C:/Users/estee/AppData/Local/Temp/Sega Saturn BIOS (USA).bin" \
  --game "build/saturn/sourceboot/e2-bob/sm64-saturn-sourceboot-e2.cue" \
  --dram-cart --bios-input --frames 240 --handoff-yield \
  --post-poke-frames 25000 --probe-address <FRESH> --probe-count 228 \
  --allow-invalid --timeout 1500 \
  --screenshot-output docs/saturn/evidence/screenshots/e2-sourceboot-quadmerge-freeroam-2026-07-25.png \
  --output docs/saturn/evidence/reports/e2-sourceboot-quadmerge-freeroam-2026-07-25.json
```

`--probe-count` is a byte count. Run it as one foreground call with a tool timeout of at least 1700 s; do not background it behind a polling monitor. Never run a capture concurrently with a build or another capture.

- [ ] **Step 3: Judge against the baseline**

Baseline is `docs/saturn/evidence/reports/e2-sourceboot-mario-freeroam-2026-07-25.json`.

| Metric | Expectation |
| --- | --- |
| `command_count` | below baseline |
| `quads_merged` | above 0 |
| `quad_map_mismatch` | exactly 0 |
| `fault_flags` | 0 |
| `modelview_stack_overflow` | 0 |
| `triangles_transformed` | unchanged — merging changes commands, not geometry |

Decode offsets with an `offsetof()` probe compiled against the real header rather than counting bytes by hand. This plan's predecessor had two hand-counts disagree on a "must stay 0" gate; the programmatic decode settled it.

Report the raw numbers whichever way they fall. A low quad rate is a real measurement, not a failure — see the spec's stated expectation that Mario will merge far less than the intro face because his skeleton partitions the mesh into many small rigid groups.

- [ ] **Step 4: Commit the evidence**

```bash
git add docs/saturn/evidence/reports/e2-sourceboot-quadmerge-freeroam-2026-07-25.json \
        docs/saturn/evidence/screenshots/e2-sourceboot-quadmerge-freeroam-2026-07-25.png
git commit -m "test(saturn): quad-merge free-roam capture evidence"
```

- [ ] **Step 5: Present to the user — write no gallery entry yet**

Show the screenshot and the raw counters. **Do not** write `docs/saturn/evidence/TIMELINE.md` or `index.html` until the user confirms.

This is a pure optimization: the scene must look *identical* to the baseline screenshot. Any visible difference is an unsafe merge and a bug to fix, never a cost to accept. Present both images and describe what the counters prove; let the user judge what they see. This project has twice had an on-screen object misidentified from inference rather than evidence.

---

## Out of scope

Textures (the next sub-project — `castleviewer` is the reference for tile/CLUT work); runtime joint tracking; transform-once caching; geometry LOD; the level-script pool-frame leak; any change to `levels/`, `src/engine/`, or `src/game/`.

## Self-review (performed at write time)

- **Spec coverage:** offline walker (Task 1), rigid-group gate (Tasks 1-2), reuse of `quad_pairing` (Task 2), mutation testing (Task 3), generated map as build dependency (Task 5), missing entry never merges (Tasks 5 Step 3, 6 Step 3), mismatch counter (Task 6 Step 1), command-count measurement (Task 7 Step 3), visual gate (Task 7 Step 5). Every spec requirement maps to a task.
- **Two spec corrections** are stated up front rather than left for an implementer to trip over: the emit adapter is in scope, and the resolved-triangle struct must widen. Both are backed by exact line citations and a measured HWRAM figure.
- **Type consistency:** `TriangleSite` (`ordinal`, `rigid_group`, `indices`, `unsafe`) and `QuadMapEntry` (`ordinal`, `partner_ordinal`) are used identically in Tasks 1, 2, 3 and 5. `walk_display_lists`, `build_quad_map`, `render_quad_map_c`, `QUAD_MAP_NONE` keep one signature throughout.
- **Placeholder scan:** no TBD/TODO. `<FRESH>` in Task 7 is a run-time-resolved address with the command that produces it given in the same step.
- **Known ordering hazard:** Task 4 must land before Task 6 — the merge has nowhere to write a fourth corner until the struct is widened.
