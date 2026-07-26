# General Quad Merging in the Fast3D-to-VDP1 Frontend — Design

Date: 2026-07-25. Follows the Mario model-load-gap cycle (merged to `master`
at `8adaafe`).

This is **sub-project 2 of 2** from the "get Mario rendering" initiative. Sub-
project 1 made Mario's geometry reach VDP1 at all. This one reduces what that
geometry costs, and does so for every model the frontend draws — not just
Mario.

## The opportunity

VDP1's native primitive is a four-corner quadrilateral. The frontend already
emits every triangle as one: `saturn_fast3d_frontend.c:363` builds the corner
index list as `(i0, i1, i2, i2)` — the last vertex duplicated — so a triangle
and a quad cost exactly the same one VDP1 command. Two coplanar triangles
sharing an edge could occupy a single command instead of two, at no cost to
the hardware.

Today **nothing in the sourceboot path ever emits a true quad.** Terrain, the
cannon, and now Mario all go through the degenerate-quad fallback, one command
per triangle. The quad-merging technique exists in this project only in the
`marioturntable`/`castleviewer` demo targets, which consume an offline-compiled
static mesh IR (`docs/saturn/SATURN_MESH_IR.md`) and never touch the real
engine's geo-layout or animation machinery.

The measured stakes, from sub-project 1's Task 3 and Task 5 captures: BOB
terrain alone submits ~1,307 transformed triangles at the probed frame; with
all 47 models registered and Mario on screen that rises to 2,011. Every one of
those is currently its own VDP1 command.

**Command count is a CPU-cost proxy, not a VDP1 throughput limit.** Existing
telemetry puts VDP1 wait at roughly 6% of frame time; the dominant costs are
CPU-side sort (~101 ms) and command construction (~63 ms). Halving the command
count for a mergeable region reduces work in exactly those two stages. This is
the reason to do it — not because VDP1 cannot keep up.

## Scope

Establish quad merging as a **general frontend capability**, not a Mario-
specific one. Static geometry (terrain, the cannon) and animated geometry
(Mario) both route through the same mechanism. Static geometry is in fact the
easier case — it has no deformation envelope to validate against at all.

## The key insight

The frontend performs no runtime joint or bone tracking, and this design adds
none.

In the Fast3D model, which joint transforms a given triangle is determined
entirely by a display list's static command *sequence* — which
`G_MTX`/`PUSHMTX`/`POPMTX` commands precede a given `G_VTX`/`G_TRI1`. Mario's
compiled display lists are the same bytes on every frame; only the matrix
*values* change as he animates. A triangle's position in that command sequence
is therefore a stable, deterministic identity across every frame and every
pose.

That makes the safety analysis an offline problem over bytecode, not a runtime
problem over geometry.

Two triangles that share a **rigid group** — the same matrix-stack state, i.e.
the same joint transforms both — move together rigidly under every possible
animation. A merge proven safe for them in the rest pose stays safe in all
poses, because animation applies one common transform to both. Two triangles in
*different* rigid groups can be pulled apart arbitrarily by animation and are
never merged, regardless of how they look at rest.

This generalizes the rule the intro-face mesh IR already established — forbid
merges across an animated-weighted vertex — and derives it from the engine's
own data instead of a hand-authored source document.

## Architecture

### Offline: display-list walker and quad compiler

A new host tool, built on the existing `tools/saturn/saturn_mesh_ir.py`
machinery, reads the real compiled source for a model — Mario's geo layout and
mesh data, BOB's terrain and cannon display lists — and walks the
`G_MTX`/`PUSHMTX`/`POPMTX`/`G_VTX`/`G_TRI1`/`G_TRI2`/`G_DL` structure exactly as
the real Fast3D interpreter would.

For each triangle-emitting command position (its **ordinal** in that walk) it
records:

- the **rigid group**: the matrix-stack identity in effect at that point;
- whether the triangle passes the existing mesh-IR safety checks — material
  agreement, shared-edge topology, consistent winding, normal alignment,
  convexity — against its candidate neighbor.

A pair is eligible only when both triangles are in the same rigid group **and**
all the existing checks pass. Cross-group pairs are rejected structurally,
before geometry is even considered.

Pair selection among eligible candidates reuses the exact maximum-cardinality
matching the mesh IR already uses (hash-pinned NetworkX blossom matching), so
the choice of *which* legal pairing to take is optimal rather than greedy.

### Build: generated quad map

Output is a generated C array per model, keyed by triangle-command ordinal,
classifying each as `quad` (paired with a named neighbor ordinal) or
`triangle_fallback`. This build step follows the precedent already set by
`mario_anim_data` generation in this same target, including declaring the map
as a proper Makefile dependency of its mesh source.

### Runtime: resolve-stage merge

`saturn_fast3d_frontend.c` maintains a cheap per-display-list ordinal counter
as it processes triangle commands, and consults the map. When two resolved
triangles are flagged as a pair, their four distinct corners are emitted as one
true VDP1 quad instead of two degenerate ones.

**The VDP1 emission backend needs no changes.** It already speaks four-corner
quadrilateral; a true quad is the same command shape with four distinct corners
rather than three plus a duplicate. The entire feature lives in the resolve
stage, upstream of emission.

## Error handling

**A missing, absent, or unrecognized map entry is never "safe to merge."** The
only two runtime states are "merge, because the map says this exact ordinal
pairs with that one" and "emit exactly as today." There is no third state in
which the frontend infers mergeability at runtime.

**Unsupported display-list constructs force fallback.** Where the offline
walker meets something it cannot model with certainty — a `GraphNodeSwitchCase`
selecting a cap or costume variant, an unexpected recursive `G_DL` shape — every
affected triangle is classified `triangle_fallback`. This mirrors the existing
`pairing_forbidden_triangles` mechanism in the mesh IR: the compiler asserts
safety only where it can prove it, and treats an unknown envelope as unsafe
rather than as a neutral-pose guarantee.

**A map/binary mismatch is detectable.** A profile counter in the established
`fault_flags` style records when a display list's live triangle count diverges
from what its map expected. It costs nothing in the normal case and surfaces a
stale-map or wrong-model error during development as a number, rather than as a
subtly wrong picture.

## Testing

**Host differential tests for the walker, against synthetic fixtures, before it
is ever pointed at real assets:** nested `G_DL` invocation, multiple joints via
push/pop, a known-safe coplanar pair, a known-unsafe cross-joint pair, a
material-mismatched pair. The walker's rigid-group assignment and eligibility
verdict are the units under test.

**Host unit tests for the safety predicates** reused from the mesh IR compiler,
extended with the rigid-group exclusion rule.

**Mutation testing** on both of the above, per this project's standing rule that
AI-generated tests are not trusted until mutations demonstrably fail them.

**Capture-based measurement:** VDP1 command count and
`triangles_transformed`/`triangles_emitted` for the same free-roam scene,
before and after. The expected result is a command-count reduction concentrated
in mergeable regions.

**An honest expectation, stated up front:** Mario's quad rate will land well
below the intro face's ~55%. That face is nearly static, so almost all of it
shares one rigid group; Mario's skeleton partitions his mesh into many small
joint groups, and every group boundary is a merge boundary. Terrain should fare
better than Mario. These are predictions to measure against, not targets to
hit — and a low rate on Mario is a correct result, not a failed one.

**Visual regression is a hard gate.** This is a pure optimization. Mario's
silhouette, the terrain, and the cannon must be indistinguishable before and
after. Any visible difference means a merge was unsafe and is a bug to fix —
never a cost to accept. Per this project's standing rule, the user's eyes are
the acceptance gate, and no TIMELINE or gallery entry is written before that
confirmation.

## Out of scope

- **Textures.** The offline-bake cycle remains deferred; the mesh IR already
  forces textured triangles to fallbacks, and that stays true here.
- **Runtime joint tracking.** Explicitly avoided by design — the whole approach
  exists to keep this an offline bytecode problem.
- **Transform-once caching**, geometry LOD, and other performance levers. Named
  in the roadmap, separate work.
- **The residual level-script pool-frame leak**, tracked and being fixed
  separately.
- Any change to `levels/`, `src/engine/`, or `src/game/` — the engine tree stays
  unmodified, per this port's standing discipline.

## Success criteria

1. The walker's rigid-group and eligibility logic is host-tested against
   synthetic fixtures and survives mutation testing.
2. Quad maps generate for at least Mario and BOB's terrain, as a proper build
   dependency.
3. True quads are emitted at runtime; the frontend's counters show a
   command-count reduction against the sub-project 1 baseline.
4. `fault_flags`, `modelview_stack_overflow`, and the new map-mismatch counter
   are all 0 in a free-roam capture.
5. The user confirms the scene is visually unchanged.
6. No regression: all four host suites, the SH-2 cross-compile, and
   `make verify` exit 0.

Both outcomes are acceptable completions: a measurable command-count reduction,
or a demonstration that the achievable quad rate on this geometry is too low to
be worth the runtime lookup — provided that conclusion rests on measurements
rather than estimates.

## Fact base

All citations verified against the working tree on 2026-07-25 at `master`
commit `8adaafe`. The degenerate-quad emission path is
`src/port/saturn/gfx/saturn_fast3d_frontend.c:363`. Mesh IR semantics are
`docs/saturn/SATURN_MESH_IR.md` and `tools/saturn/saturn_mesh_ir.py`. Triangle
counts are from
`docs/saturn/evidence/reports/e2-sourceboot-mario-freeroam-2026-07-25.json`
(2,011 transformed) and this project's recorded terrain-only baseline (1,307).
Frame-cost proportions are from the performance telemetry recorded in
`docs/saturn/ROADMAP.md`.
