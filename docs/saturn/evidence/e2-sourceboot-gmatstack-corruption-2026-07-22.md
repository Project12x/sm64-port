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

## Host-vs-target differential test: the algorithm is exonerated

With gGfxPool collision, degenerate camera input (exhaustively, not just
this-instant), never-initialized Lakitu state, sibling-node interference,
and mtxf_mul's own logic all directly ruled out at the C-source level, two
candidates remained: (a) a defect in the *compiled* `mtxf_lookat`/`mtxf_mul`
machine code specific to this SH-2 soft-float target, not visible from
source; or (b) a genuine algorithmic edge case in the two functions
themselves, independent of platform, that a source-level reading missed.
These are very different investigations, so before starting any
disassembly, a cheap, highly diagnostic differential test settled which one
to pursue.

**Test**: compile the real, unmodified `src/engine/math_util.c` — the exact
file containing `mtxf_lookat`/`mtxf_mul`, not a reimplementation — with the
**host** compiler (`cc`/GCC via MSYS2, not the SH-2 cross-compiler), and
call `mtxf_lookat`/`mtxf_mul` with the exact real captured
`gLakituState.pos`/`.focus` values from this investigation
(`pos=(-7208.26318359375, 264.13934326171875, 7050.0)`,
`focus=(-6566.8955078125, 124.66311645507812, 6454.2001953125)`), swept
across the full `s16` roll range (0-65535), reproducing the exact calling
context (`geo_process_camera`, `rendering_graph_node.c:327-328`:
`mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);
mtxf_mul(gMatStack[gMatStackIndex+1], cameraTransform,
gMatStack[gMatStackIndex])`, with `gMatStack[gMatStackIndex]` reproduced as
an explicit `mtxf_identity()` call rather than assumed). This uses the
*real* `gSineTable`/`gCosineTable` data (`math_util.c` directly
`#include`s `trig_tables.inc.c`), not an approximation — an improvement
over the earlier Python re-simulation, which used continuous
`math.sin`/`math.cos`.

New file: `tools/saturn/mtxf_lookat_host_diff_test.c`. New Makefile target:
`verify-mtxf-lookat-host-diff` (deliberately **not** wired into
`verify-all` — this tests vanilla SM64 engine math, not this port's own
platform contracts, so it stays a standalone, reproducible diagnostic
rather than a standing gate). Three externals referenced by *other*,
unrelated functions in `math_util.c`'s translation unit (`find_floor`,
`guMtxF2L`, `gVec3fZero`) needed link-only stubs, since the linker pulls in
`math_util.o` at whole-object-file granularity once any symbol from it is
needed — none of the three are reachable from `mtxf_lookat`/`mtxf_mul`
themselves. `M_PI` (used by an unrelated function, `atan2f`, elsewhere in
the same file) needed `-D_GNU_SOURCE` to unlock on the host's libm under
strict `-std=c11`; that flag is host-toolchain-only and changes nothing
about the code under test.

**Result** (`make -f Makefile.saturn.mk verify-mtxf-lookat-host-diff`,
exit 0):

```
Swept all 65536 roll values.
Exact 0xe200001c bit-for-bit matches: 0
Other huge-magnitude (>1e10) anomalies: 0

Sample result at roll=0: composed[2][2] = 0.672123253 (bits 0x3f2c1045)
composed row2 = (0.732654393, -0.107088104, 0.672123253, 0)

RESULT: HOST NEVER REPRODUCES THE CORRUPTION ACROSS ALL 65536 ROLL VALUES.
```

Every one of the 65,536 roll values produces a sane, small-magnitude result
(row2 components in the 0.1-0.7 range — exactly what a valid rotation
matrix row should look like). Not one produces `0xe200001c`, and not one
produces *any* value with magnitude above `1e10`. This directly matches the
earlier Python simulation's conclusion, now confirmed against the real C
engine code and the real trig table data rather than an approximation.

## Known-issue search: exact toolchain version and a real, relevant fact — but no confirmed matching bug report

Before any disassembly, checked whether this is already a documented,
known GCC SH/soft-float issue.

**Exact pinned toolchain**: `sh-elf-gcc (GCC) 14.3.0` (`sh-elf-gcc --version`,
`.yaul.env` sourced first), configured `--with-cpu=m2 --with-endian=big`
(SH2, big-endian, no hardware FPU — matches this project's own documented
target). Binutils `2.44`. Built via the `marsdev` toolchain-builder scripts
(`work/upstream/marsdev/sh-gcc-toolchain/`, a local clone, read as
reference — not modified), whose `Makefile` pins `GCC_DEFAULT_VER := 15.2.0`
and `BINUTILS_DEFAULT_VER := 2.44` as its *current* defaults, meaning `14.3.0`
was an explicit, older, deliberately-selected build, not a stale leftover.
The full GCC 14.3.0 source tree is present locally
(`.../sh-gcc-toolchain/gcc-14.3.0/`), letting every claim below be checked
against the actual compiler source rather than secondhand summaries.

**The concrete, verified fact**: this exact target configuration
(`sh-*-elf*`, matching `sh-elf` precisely) selects the **legacy `fp-bit.c`
generic soft-float implementation** for every single- and double-precision
floating-point operation, including the multiply/add that `mtxf_lookat`
and `mtxf_mul` compile down to. Traced directly through the real build
system, not assumed:
`libgcc/config.host`'s `sh-*-elf* | sh[12346l]*-*-elf*)` case sets
`tmake_file="$tmake_file sh/t-sh t-crtstuff-pic t-fdpbit"`, and
`libgcc/config/t-fdpbit` sets `FPBIT = true` / `DPBIT = true`, which
(`libgcc/Makefile.in`'s `ifneq ($(FPBIT),)` block) compiles
`FPBIT_FUNCS = ... _mul_sf _div_sf ...` directly from `libgcc/fp-bit.c`.
`fp-bit.c` is GCC's original (early-1990s) generic, portable, bit-manipulation-based
soft-float library — read its `multiply()`/`_fpmul_parts()` implementation
in full (`fp-bit.c:753-932`) directly rather than assuming; it is a
standard extended-mantissa multiply with exponent renormalization loops
and careful (commented, deliberate) round-to-even handling. One comment
(`fp-bit.c:891-895`) explains a specific rounding-tie design choice with a
worked example (`0xfff * 0x3f800400`) — this is **not** a bug report and
is nowhere near the right order of magnitude to explain a ~21-orders
corruption (a sub-ULP rounding tradeoff cannot produce `1e20`-scale
garbage); flagged here explicitly so it is not mistaken for a match to
this symptom.

**GCC 15 replaced `fp-bit.c` with the modern `soft-fp` library for
bare-metal `sh-elf` specifically** — confirmed via GCC 15's own release
notes (`gcc.gnu.org/gcc-15/changes.html`: "Bare metal `sh-elf` targets are
now using the newer soft-fp library for improved performance of
floating-point emulation") and the actual patch discussion (`gcc-patches`
mailing list, July 2024, `[RFC/PATCH] libgcc: sh: Use soft-fp for
non-hosted SH3/SH4`, fetched directly rather than summarized secondhand).
Both sources frame this **purely as a performance change**
(~3x speedup measured via Whetstone on real SH4 hardware, "fp-bit.c is
quite slow"), with **no mention anywhere in either source of a known
correctness bug, wrong-code report, or incorrect floating-point result**
being fixed. Notably, `sh-*-linux*` and `sh-*-rtems*` targets were
deliberately left on `fp-bit.c` even after this patch — inconsistent with
`fp-bit.c` having a severe, known-broken correctness defect for SH (the
GCC maintainers would be unlikely to leave other SH targets on a
compiler demonstrably producing wrong answers).

**Searched and found no match**: GCC Bugzilla (via search-engine queries —
direct `bugzilla.gcc.gnu.org` fetches are blocked by bot-protection/Anubis
for this tool; the one superficially-matching title found, bug 11040
"Wrong Floating Point Calculations", was checked and is an unrelated
i386/m68k x87-excess-precision issue, GCC's "bug 323" family — a real,
verified non-match, not assumed irrelevant); GCC's own bug-fix changelogs
for 14.x point releases and the 15.x release notes; and `yaul-org/libyaul`'s
GitHub issues and discussions (`gh api` search across all states for
"float" in title/body: zero results).

**Honest conclusion for this step**: no specific, confirmed bug report
matching this exact symptom (a soft-float multiply producing a
non-degenerate-input, non-NaN, ~21-orders-of-magnitude garbage result) was
found. What *was* found is real, verifiable technical context: this exact
toolchain uses a soft-float library GCC's own maintainers subsequently
replaced (for this exact target) for reasons stated as performance, not
correctness — so this is suggestive, relevant background, not a confirmed
match. A GCC 15.2.0 build is available via the same local toolchain-builder
infrastructure (source tarballs already present) but is **not currently
built**; building and testing against it would be a genuine, separate
undertaking (a full cross-compiler bootstrap), not attempted here.

## Conclusion: SH-2-target-specific, algorithm exonerated; no matching known issue found — stop and report

Per the differential test's outcome, this is settled at the algorithm
level: **`mtxf_lookat`/`mtxf_mul` are exonerated.** The same real inputs,
the same real unmodified engine code, produce a completely sane result on
host and a massively corrupted one on the SH-2 target. The known-issue
search did not turn up a confirmed matching bug report, but did establish
that this exact configuration runs on a soft-float library GCC's own
maintainers have since replaced for the same target, for performance
reasons that were never characterized as a correctness fix in any source
found.

Per the explicit instruction accompanying this investigation: **disassembly
forensics of the compiled `mtxf_lookat`/`mtxf_mul` machine code is a
separate, larger decision, not something to start unilaterally now.** This
document stops here and reports the finding plainly — including the honest
"searched and found nothing conclusive" outcome of the known-issue search —
rather than proceeding into compiler/codegen-level investigation.

No SM64 engine or platform source code was changed in this investigation
pass — only the new diagnostic test file and its Makefile target were
added. `docs/saturn/PROVENANCE.md`/`UPSTREAM_CODE_LEDGER.md`'s
reference-engine re-consultation was committed separately (`48157a5`); this
document, its underlying capture JSONs, and the differential test harness
are committed together.
