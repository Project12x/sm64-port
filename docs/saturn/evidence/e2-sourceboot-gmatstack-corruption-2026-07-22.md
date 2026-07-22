# E2 sourceboot: the corruption predates gGfxPools — it is already in `gMatStack[1]` — 2026-07-22

Continues [`e2-sourceboot-bad-mtx-pointer-2026-07-22.md`](e2-sourceboot-bad-mtx-pointer-2026-07-22.md),
which traced the corrupted `G_MTX` pointer (`0x0608f970`) into `gGfxPools` and
ruled out a missing per-frame pool reset, cross-frame decode lag, and a
`sizeof(Mtx)`/`GBI_FLOATS` mismatch, leaving one open hypothesis: a genuine
same-frame forward/backward allocation overlap.

## Reference-engine consultation (per this session's standing instruction)

Before further live tracing, both GPL reference engines already cloned into
this project were re-consulted for this specific problem class (per-frame
matrix/command buffer management on real Saturn hardware). Recorded in
`docs/saturn/PROVENANCE.md` and `docs/saturn/UPSTREAM_CODE_LEDGER.md`'s new
M5 section (commit `48157a5`):

- **Sonic Z-Treme's `workarea.c`** (full file, 51 lines): every per-frame
  buffer region (`sort_list`, `zbuffer`, `spritebuf`, `pbuffer`, `clofstbuf`,
  `commandbuf`) is a fixed, compile-time, non-overlapping slot sized via the
  `AdjWork` cumulative-offset macro chain — never two pointers growing
  toward each other at runtime.
- **SlaveDriver's `WALLS.C`** (lines ~1240-1300): the slave-side polygon
  buffer (`slaveResult`, capacity `MAXNMSLAVEPOLYS=1300`) is a single
  fixed-capacity array indexed by a monotonic counter, with an explicit
  bounds check *before* every write (`if (height*width+nmSlavePolys+50>MAXNMSLAVEPOLYS)
  return;`) that drops data rather than risking overflow.

Both real, shipped Saturn engines avoid SM64's own `gGfxPool`/
`alloc_display_list` two-ends-bump-allocator pattern entirely. This is
useful characterization context, but as shown below, **it turned out not to
be the mechanism at all** — the actual finding is upstream of `gGfxPool`.

## The corruption is already present in `gMatStack[1]`, before the copy

A direct 2048-byte read of `gMatStack` (`_gMatStack`, `0x060b9000`, `Mat4
gMatStack[32]` — `src/game/rendering_graph_node.c:40`, unmodified SM64
engine code) at the same deterministic depth (240 BIOS frames + 25,000
post-poke frames) that reproduces the corrupted `G_MTX` command found the
identical corruption signature already present at **entry index 1** (byte
offset 104, address `0x060b9068`, matrix position `m[2][2]` — the same
position `dbg_root_mtx_*` diagnostics from earlier in this session already
flagged). Evidence:
[`e2-sourceboot-gmatstack-dump-2026-07-22.json`](reports/e2-sourceboot-gmatstack-dump-2026-07-22.json).

```
gMatStack entry index 1:
  row0: (-0.0, 0.11527838557958603, 2.4119628793186733e-35, -830.9569702148438)
  row1: (0.7326544523239136, 0.15734346210956573, -0.0, -0.7235284447669983)
  row2: (0.6721231937408447, 2.5331897520501786e-35, -5.902977806835426e+20, 3.102418748076572e-39)
  row3: (2.5762198477468564e-35, 2.576201480647625e-35, 2.624878885386168e-35, 2.625466632561579e-35)
```

`row2[2] = -5.902977806835426e+20`, whose raw bits (`0xe200001c`) are **the
exact same bit pattern** already found both at the *decoded* corruption site
(`eb5bbbf`, this session's very first Thread-1 finding, `decoded.m[2][2] ==
INT32_MIN`) and in the raw `gGfxPools` bytes (`5d8fe9a`). This closes the
loop between the two separate investigation threads that opened this
session: **they were always the same bug.** `guMtxF2L`'s `GBI_FLOATS`
memcpy path was independently re-confirmed via `.asm` disassembly
(`06006a7c <_guMtxF2L>`, `mov #64,r6` / `jmp @_memcpy`) to faithfully copy
whatever is already in `gMatStack[1]` — it does not create the corruption,
it propagates it.

**This rules out `gGfxPool` entirely as the mechanism.** `geo_process_root`
(`rendering_graph_node.c:1061`) sets `gMatStackIndex = 0` unconditionally at
the start of every single frame's traversal, and `mtxf_identity(gMatStack[0])`
two lines later (`:1076`) makes `gMatStack[0]` a clean identity every frame —
neither can carry stale cross-frame garbage. `geo_process_camera`
(`:315-339`) then computes `gMatStack[1] = mtxf_mul(cameraTransform,
gMatStack[0])`, where `cameraTransform` is a **local stack variable**
freshly computed by `mtxf_lookat(cameraTransform, node->pos, node->focus,
node->roll)` every frame. There is no code path by which `gMatStack[1]`
could hold data left over from an earlier frame or a `gGfxPool` bank reuse —
whatever is in it was produced fresh, this exact frame, by unmodified SM64
engine math.

## Reproducibility re-confirmed, plus `gMatStackIndex` and `gMatStack[0]` directly

Before drawing conclusions from a single earlier snapshot, the same read was
repeated at the identical deterministic depth, in a single combined probe
(2050 bytes from `0x060b9000`, covering all 32 `gMatStack` entries plus the
2-byte `gMatStackIndex` immediately following it in memory, confirmed
contiguous by a fresh `nm` resolution: `_gMatStack=0x060b9000`,
`_gMatStackIndex=0x060b9800`):

- **`gMatStack[0]` is a perfect, textbook 4x4 identity matrix** (`1,0,0,0 /
  0,1,0,0 / 0,0,1,0 / 0,0,0,1`), exactly as `mtxf_identity` should produce.
- **`gMatStack[1]` reproduces the identical corruption, byte-for-byte** —
  same four rows, same anomalous word (`0xe200001c` /
  `-5.902977806835426e+20`) at the same position (row 2, col 2). This is a
  **stable, deterministic, fully reproducible** corruption, not a one-time
  fluke or a stale read.
- **`gMatStackIndex` reads back as `0`** at this snapshot instant — the
  traversal has correctly unwound back to root depth by the time this read
  landed. This weighs against (though does not by itself fully exclude,
  since the snapshot is a single instant) the originally-suspected
  "unprotected 32-entry stack overflow" mechanism: the push/pop bookkeeping
  itself is not stuck or corrupted, only the transient *content* written at
  depth 1 sometime during the frame is wrong.

Evidence: [`e2-sourceboot-gmatstack-reverify-2026-07-22.json`](reports/e2-sourceboot-gmatstack-reverify-2026-07-22.json).

## Hypotheses tested and ruled out for *why* `gMatStack[1]` itself is bad

### 1. Degenerate camera geometry feeding `mtxf_lookat` — ruled out

Initially suspected: `mtxf_lookat`'s first two normalization steps
(`src/engine/math_util.c:211,223`) each divide by `sqrtf(...)` of a vector
that could be near-zero for a degenerate pos/focus pair (e.g., looking
straight up/down, or focus == pos).

Read `gCamera->pos`/`gCamera->focus` directly (`struct Camera`,
`src/game/camera.h:546-547`) at the same depth:
`focus=(-6566.99, 125.0, 6454.02)`, `pos=(-7208.26, 264.14, 7050.0)`.

**Correction made during this check**: `geo_process_camera` does not read
`gCamera` — it reads the `GraphNodeCamera` node's own `pos`/`focus`/`roll`
fields (`src/engine/graph_node.h:185-188`), which `update_graph_node_camera`
(`camera.c:3521-3529`) syncs from **`gLakituState.pos`/`.focus`**, not from
`gCamera` directly. Re-read the correct fields (`gLakituState`, ground-truthed
at `src/game/camera.h:629-631`: `focus` at struct offset `0x80`, `pos` at
`0x8C`; live symbol `_gLakituState` at `0x06094164`):
`focus=(-6566.90, 124.66, 6454.20)`, `pos=(-7208.26, 264.14, 7050.0)` —
essentially identical to `gCamera`'s values (confirming the sync is working
correctly), and definitively **not degenerate**: horizontal separation
`dx²+dz²=766,330`, full 3D separation `785,784` (camera-to-focus distance
≈886 units), pitch angle ≈ -9°. Both far from the zero that would make
`mtxf_lookat`'s normalizations degenerate.

To close this out completely rather than relying on one instant's reading,
`mtxf_lookat`'s exact formula (all four normalization/cross-product steps)
was reimplemented in Python using these real captured `pos`/`focus` values
and swept across **all 65,536 possible `s16` roll values** (the one
remaining unknown input, since `GraphNodeCamera.roll` is set once at camera
creation and never synced per-frame, and its live runtime address inside a
dynamically-allocated pool was not resolved). Zero degenerate cases found —
no roll value drives any of the four `sqrtf` denominators near zero with
this pos/focus pair. **Degenerate camera input is conclusively ruled out**
regardless of the actual live roll value.

Evidence: [`e2-sourceboot-camera-lookat-2026-07-22.json`](reports/e2-sourceboot-camera-lookat-2026-07-22.json)
(`gCamera` read), [`e2-sourceboot-lakitu-pos-focus-2026-07-22.json`](reports/e2-sourceboot-lakitu-pos-focus-2026-07-22.json)
(`gLakituState` read, the fields that actually feed `mtxf_lookat`).

### 2. Never-initialized Lakitu interpolation speeds — ruled out

Hypothesis: this port's direct boot into `level_bob_entry` might skip
`reset_camera()`/`init_camera()` (both unmodified engine code, normally
triggered by the castle-grounds warp flow this port bypasses — the same
class of gap as the five init-order bugs already fixed earlier this
session). `reset_camera` is only called from three sites in
`level_update.c`, one of which (`init_level`'s `reset_camera(gCurrentArea->camera)`,
`:1192`) is gated behind `gCurrentArea != NULL` and runs inside the very
first, and only, pass through `INIT_LEVEL()` in `source_entry.c`'s script —
plausible for the gate to fail depending on exact call ordering. If
`init_camera` never runs, `gLakituState.posHSpeed/posVSpeed/focHSpeed/focVSpeed`
would stay at raw `.bss`-zero instead of their intended `0.3/0.3/0.8/0.3`
defaults, which (given `set_or_approach_f32_asymptotic`'s pure multiply-based
approach, `camera.c:4033`: `*current += (target-current)*multiplier`) would
freeze `gLakituState.curPos`/`curFocus` at their own `.bss`-zero start —
i.e., camera position and focus both stuck at the origin, which **would**
make `mtxf_lookat` degenerate (`dx=dz=0`).

**Directly refuted**: the live `gLakituState.pos`/`.focus` read above shows
normal, well-separated, non-zero values matching `gCamera` closely — not
frozen at the origin. Whatever the exact call-graph nuance around
`reset_camera`/`init_level`'s gate, the interpolation state is not stuck.

### 3. A sibling geo-node reusing stack depth 1 later in the same frame — ruled out

Read the full Bob-omb Battlefield area-1 geo layout
(`levels/bob/areas/1/geo.inc.c`) to check whether any node *other than* the
camera also touches `gMatStackIndex`/`gMatStack[1]` within the same frame.
The tree is: root → `GEO_ZBUFFER(0)`/ortho/skybox (a separate, earlier
sibling branch, before the camera in traversal order) → `GEO_ZBUFFER(1)` →
`GEO_CAMERA_FRUSTUM` → `GEO_CAMERA` (the only child of its zbuffer branch;
its own `GEO_CLOSE_NODE()` immediately follows its subtree) → six
`GEO_DISPLAY_LIST` nodes + `GEO_RENDER_OBJ` + `GEO_ASM(geo_envfx_main)` (all
direct children of the camera) → back out to root level → a final
`GEO_ZBUFFER(0)`/`GEO_ASM(geo_cannon_circle_base)` sibling branch, running
*after* the camera's entire subtree has fully unwound `gMatStackIndex` back
to 0.

- `geo_process_display_list` (`rendering_graph_node.c:477-484`) does not
  touch `gMatStackIndex` at all.
- `geo_cannon_circle_base` (`src/game/screen_transition.c:294-304`) is
  gated behind `gCurrentArea->camera->mode == CAMERA_MODE_INSIDE_CANNON`
  (false — Mario is idle at spawn, not in a cannon) and does not reference
  `gMatStackIndex`/`gMatStack` even when active.
- `geo_envfx_main` (`src/game/level_geo.c:16-56`) and `geo_skybox_main`
  (`:62-79`) are both **consumers** of the current stack value (they copy
  `gMatStack[gMatStackIndex]`/`gLakituState.pos`/`.focus` into their own
  freshly-allocated `Mtx`), not producers that push new content onto the
  stack.

No sibling or later node reuses or overwrites stack depth 1 after the
camera. `gMatStack[1]`'s content is exclusively attributable to
`geo_process_camera`'s own computation this frame.

### 4. `mtxf_mul` itself — checked, no defect visible from source

Read `mtxf_mul` (`math_util.c:491` onward) in full: a standard,
straightforward multiply-accumulate with no division, no `sqrtf`, no
branches. Multiplying by `gMatStack[0]` (confirmed clean identity every
frame) is mathematically a no-op (`A·I=A`) and the source correctly
implements that (verified by hand-checking the identity-multiply algebra
against the code). If `cameraTransform` (mtxf_lookat's output) is clean,
`mtxf_mul` should pass it through unchanged. No defect visible at the
C-source level.

## Where this leaves the investigation

With gGfxPool collision, degenerate camera input (exhaustively, not just
this-instant), never-initialized Lakitu state, sibling-node interference,
and mtxf_mul's own logic all directly ruled out, the corruption's immediate
cause is narrowed to one of:

- **(a) A defect in the *compiled* `mtxf_lookat`/`mtxf_mul` machine code on
  this SH-2 soft-float target**, not visible at the C source level — e.g. a
  codegen or soft-float-library edge case in `sqrtf`/division specific to
  this cross-compiler/target, that a source-level reading cannot catch.
  Confirming this would require disassembling the compiled `mtxf_lookat`/
  `mtxf_mul` and single-instruction-stepping through the *actual* live
  computation for this exact frame — a materially deeper investigation than
  this pass, and one that inspects compiler/library output rather than
  project source.
- **(b) Something not yet identified** in how `node->pos`/`node->focus`/
  `node->roll` (the exact values `geo_process_camera` reads, as opposed to
  `gLakituState.pos`/`.focus` which are confirmed clean) reach `mtxf_lookat`
  — e.g. if `update_graph_node_camera`'s sync itself races with something,
  though no such mechanism was found and none is expected in this
  single-threaded frame loop.

Both remaining candidates point at **unmodified SM64 engine code**
(`src/engine/math_util.c`'s `mtxf_lookat`/`mtxf_mul`, `src/game/camera.c`'s
`update_graph_node_camera`, `src/game/rendering_graph_node.c`'s
`geo_process_camera`) or the cross-compiler's soft-float codegen/library —
neither is this port's own platform code, and neither is a capacity
constant or Saturn-specific timing/staging issue. Per
`docs/saturn/ENGINE_PORT_ARCHITECTURE.md`'s engine-ownership boundary, this
is the point to stop and report rather than modify protected engine code or
guess at a fix in code this session did not write and has now spent
substantial, direct, live-evidence effort trying to narrow down.

No code changes were made in this investigation pass — pure evidence
gathering. `docs/saturn/PROVENANCE.md`/`UPSTREAM_CODE_LEDGER.md`'s
reference-engine re-consultation was committed separately (`48157a5`); this
document and its underlying capture JSONs are committed alongside it.
