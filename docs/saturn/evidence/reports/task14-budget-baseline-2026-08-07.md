# Task 14 fresh RED link baseline + residual memory inventory — 2026-08-07

Scope: `docs/superpowers/plans/2026-08-07-task14-completion.md`, Task 1. No
source changes were made to produce this report.

## Summary

A real, from-source link at current HEAD (`dd31e2ad`) was attempted in this
session using the exact serialized command shape the ledger's prior
baselines used (sourced worktree `.yaul.env`, `SATURN_SOURCE_CART_STAGE_SECTORS=4`,
`SATURN_DEMO_HOT_PROMOTION=0`). It compiled the entire tree successfully but
**failed before reaching the `sourceboot-cart.x` memory-budget asserts**, on
an unrelated linker-script defect (below). No fresh `.elf`/`.map` at current
HEAD was produced by this session as a result.

The most recent `.map` that *did* reach assert evaluation anywhere in this
worktree is 3 commits stale (predates `465fb8b0`/`fce3f4b5`/`0ebd5b05`, the
three HWRAM-reduction commits landed earlier today). Its measured numbers —
**HWRAM short by exactly 3,128 bytes, LWRAM over by exactly 784 bytes** —
reproduce the plan doc's cited pre-reduction baseline exactly, byte for
byte, on two independent linker assertions. That is real, measured
confirmation of the *pre*-reduction number. It is **not** confirmation of
the plan's reconstructed post-reduction estimate (~2,512 B HWRAM / 784 B
LWRAM) — that estimate remains unconfirmed by any fresh link, exactly as
the plan doc already states. The HWRAM ranking below is taken from this
same near-fresh map and is explicitly annotated where the three newer
commits are known to have since changed a listed symbol.

## Step 1: real link attempt at HEAD — exact failure

Command (worktree `.yaul.env` sourced, matching Task 1 Step 1's shape):

```
make -f Makefile.saturn.mk -j1 SATURN_MSYS_MAKE=/usr/bin/make sourceboot \
  SATURN_DEMO_PATH=1 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 \
  SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=0 SATURN_DEMO_NEAR_CLIP=0 \
  SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=4 \
  SATURN_SOURCE_CART_STAGE_SECTORS=4
```

(`build_sourceboot_variant.py`'s preflight was tried first per the task's
primary instruction; it refuses immediately with `YAUL_INSTALL_ROOT is not
set` because it does not source `.yaul.env` itself, so the task's documented
fallback — the serialized direct make invocation — was used instead.)

The build compiled the full source tree (identity directory
`e2-bob-identity-id-a8c8def44de49966`) and reached the final link step, then
failed:

```
/d/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/ld: cannot open linker script file saturn_geo_depth_manifest.ld: No such file or directory
collect2: error: ld returned 1 exit status
make[1]: *** [.../build.post.bin.mk:129: .../e2-bob-identity-id-a8c8def44de49966/obj/sm64-saturn-sourceboot-e2.elf] Error 1
make: *** [Makefile.saturn.mk:199: sourceboot] Error 2
```

No `.elf` or `.map` was produced for this attempt.

**This is a real, reproducible failure, not a fabricated one, and it is
distinct from the memory-budget asserts this task exists to measure.** It
was captured twice today, independently: once by this session, and once in
a leftover scratch attempt (`.tmp-fresh-link-attempt3.err`, 09:38 local)
from earlier the same day. In both cases the missing file,
`build/saturn/sourceboot/generated/saturn_geo_depth_manifest.ld`, was
confirmed present on disk (checked after this session's failure: it exists,
176 bytes, timestamped at the start of the run, well before the link step
that failed to open it) and the final link command line does carry
`-L.../build/saturn/sourceboot/generated` (verified against the captured
command in this session's build log). `sourceboot-cart.x:18` resolves the
file via a bare `INCLUDE saturn_geo_depth_manifest.ld`, which GNU ld
resolves against the working directory and any `-L` search paths at parse
time.

The most plausible explanation found by inspection (not confirmed, and no
source change was made to test it, per this task's no-source-changes
scope): `src/port/saturn/sourceboot/sourceboot.specs` defines
`*link: -T sourceboot-cart.x` via `%rename link old_link` but never chains
back to `%(old_link)`, i.e. it wholesale replaces the target's link spec
rather than extending it. Whether that is actually what drops the
`INCLUDE` search path, versus an LTO/`collect2` two-stage-link artifact
(`-flto -ffat-lto-objects` is also set in this specs file), was not
isolated further — that investigation is out of scope for a task that makes
no source changes, and belongs to whoever picks up the fresh-link retry.
`build/saturn/sourceboot/generated/` is also not identity/session-scoped
(`SOURCEBOOT_GENERATED` is a single shared path under `build/saturn/`), so a
second build process touching the same worktree concurrently while this one
runs cannot be ruled out as a contributing race, though this session did not
independently confirm one was running at the exact moment of failure.

## Step 2: real deltas (from the best-available assert-evaluated `.map`)

No fresh `.map` at current HEAD exists. The most recent `.map` anywhere in
this worktree that reached assert evaluation is
`build/saturn/sourceboot/e2-bob-identity-id-a1e5dae21d84a81e/obj/sm64-saturn-sourceboot-e2.map`,
timestamped 2026-08-07 07:37 local — after `91f02ffd` (07:29) but **before**
`465fb8b0` (08:11), `fce3f4b5` (08:32), and `0ebd5b05` (10:02), i.e. it
predates all three HWRAM-reduction commits cited in the plan's ground truth.
No `.elf` sits beside it (consistent with an assert failure: ld computes and
writes the map, then fails the link on the `ASSERT` line, producing no
final binary). Values pulled directly from that map's linker-script
evaluation trace:

**HWRAM** (`MEMORY.ram`: `ORIGIN=0x06004000 LENGTH=0x000FC000` →
physical top `0x06100000`, per `sourceboot-cart.x:21`):

- `___end = 0x060ff138`
- Physical-top margin: `0x06100000 - 0x060ff138 = 0xEC8` = **3,784 bytes**
- Required margin (`sourceboot-cart.x:135`): `0x1B00` = **6,912 bytes**
- **Assert `ORIGIN(ram)+LENGTH(ram) - ___end >= margin` evaluates to `0x0`
  (FALSE) → FAILS, short by `6912 - 3784 = 3,128 bytes`.**
- The other HWRAM assert (`___end <= top`) evaluates `0x1` (TRUE, passes) —
  HWRAM does not physically overflow, it just doesn't clear the TLSF floor.

**LWRAM** (`MEMORY.lwram`: `ORIGIN=0x00200000 LENGTH=0x00100000` → physical
top `0x00300000`):

- `__lwram_actor_runtime_start/end = 0x002eb310 / 0x002fb310` → size
  `0x10000` (65,536 B) — matches its fixed-size assert (passes).
- `__lwram_geo_traversal_start/end = 0x002fb310 / 0x002fc310` → size
  `0x1000` (4,096 B) — matches `__sourceboot_geo_traversal_expected_size`
  (passes; this is the as-generated 256-frame capacity, not yet the
  172-proven-depth right-size Task 4 will consider).
- `__lwram_camera_capture_start/end = 0x002fc310 / 0x002fc310` → size `0`
  (no capture linked; passes the "absent or exactly 0x2F7C0" assert).
- `__sourceboot_lwram_slave_stack_base = 0x002fc000` (top `0x00300000` minus
  the fixed `0x4000` reservation).
- **Assert `__lwram_camera_capture_end <= slave_stack_base` evaluates `0x0`
  (FALSE) → FAILS**: static LWRAM work (`0x002fc310`) already runs
  `0x310` bytes past the reserved stack's base (`0x002fc000`).
- Physical-top margin: `0x00300000 - 0x002fc310 = 0x3CF0` = **15,600
  bytes**. Required margin (`sourceboot-cart.x:227`): `0x4000` = **16,384
  bytes**.
- **Assert `top - capture_end >= margin` evaluates `0x0` (FALSE) → FAILS,
  short by `16384 - 15600 = 784 bytes`.**

**Reconciliation:** both figures (3,128 / 784) match the plan doc's
"Current ground truth" pre-reduction baseline exactly. They do **not**
match the plan's reconstructed post-reduction estimate (~2,512 / 784) —
that arithmetic assumed the three later HWRAM commits' savings apply
directly to this margin, which this map (being from before those commits)
cannot confirm or deny. **The measured numbers that exist are the
pre-reduction ones; the post-reduction number is still unconfirmed by any
fresh link, as the plan doc itself already flags.** Whoever runs Task 1's
retry after the linker-script defect above is fixed should re-derive both
figures from a clean, current-HEAD `.map` rather than trust this
reconciliation further.

## Step 3: ranked HWRAM `.bss`/`.data` occupants (top 25, from the same map)

Sizes are per-symbol input-section contributions (`-fdata-sections` gives
most project statics their own `.bss.<name>`/`.data.<name>` section, which
the map lists with an explicit size and owning object file). Library/yaul
objects that used `COMMON` allocation instead are not separately broken out
here; none appeared large enough to affect the top 25.

| # | Size (B) | Symbol | Object | Category |
|---|---------:|--------|--------|----------|
| 1 | 201,216 | `gAudioHeap` | `src/buffers/buffers.o` | (b) CPU-only mutable — audio heap, allocated even though this build is `--audio 0`; worth checking whether it can shrink/gate on the audio feature flag (not evaluated here, out of Task 1 scope) |
| 2 | 145,920 | `gObjectPool` | `src/game/object_list_processor.o` | (b) CPU-only mutable — core SM64 object pool, required at runtime |
| 3 | 131,072 | `sourceboot_vdp1_cmdts` | `src/port/saturn/sourceboot/main.o` | (a) SCU/DMA-visibility-constrained — VDP1 command staging; `sourceboot-cart.x` explicitly forbids moving this class (`.lwram_cmdts` size-0 assert) |
| 4 | 53,248 | `gDecompressionHeap` | `src/buffers/buffers.o` | (b) CPU-only mutable |
| 5 | 51,276 | `gGfxPools` | `src/buffers/buffers.o` | (b) CPU-only mutable (display-list/graphics scratch pools) |
| 6 | 40,960 | `_private_pool` | `libyaul.a(internal.o)` | (d) libyaul-owned — out of scope |
| 7 | 24,624 | `dynlist_mario_master` | `src/goddard/dynlists/dynlist_mario_master.o` | (c) write-once candidate — Goddard dynamic-list source data, currently `.data` not `const`; same class of fix as the four already-landed const sweeps |
| 8 | 24,576 | `sourceboot_gouraud_staging` | `src/port/saturn/sourceboot/main.o` | (a) SCU/DMA-visibility-constrained — Gouraud staging, not movable per the runtime-contract history |
| 9 | 20,480 | `gSineTable` | `src/engine/math_util.o` | (c) write-once candidate — a lookup table; strong candidate to become `const` and route to cart the same way the four landed commits did |
| 10 | 9,840 | `animdata_red_star_1` | `src/goddard/dynlists/anim_group_2.o` | (c) write-once candidate |
| 11 | 9,840 | `animdata_silver_star_1` | `src/goddard/dynlists/anim_group_2.o` | (c) write-once candidate |
| 12 | 9,840 | `animdata_mario_intro_1` | `src/goddard/dynlists/anim_group_2.o` | (c) write-once candidate |
| 13 | 9,600 | `sHilites` | `src/goddard/renderer.o` | (b)/(c) unclear without reading the source — Goddard highlight render state; needs a write-site trace before classifying, per the per-item discipline Task 5 already specifies |
| 14 | 8,192 | `gThread5Stack` | `src/buffers/buffers.o` | (b) CPU-only mutable — thread stack, LWRAM-move candidate gated on LWRAM headroom (currently negative, see Step 2) |
| 15 | 8,192 | `gThread4Stack` | `src/buffers/buffers.o` | (b) same as above |
| 16 | 8,192 | `gThread3Stack` | `src/buffers/buffers.o` | (b) same as above |
| 17 | 7,448 | `_peripherals_memb_memb_mem` | `libyaul.a(smpc_peripheral.o)` | (d) libyaul-owned — out of scope |
| 18 | 7,016 | `mario_Face_FaceData` | `src/goddard/dynlists/dynlist_mario_face.o` | (c) write-once candidate |
| 19 | 6,144 | `gDynamicSurfacePartition` | `src/engine/surface_load.o` | (b) CPU-only mutable — collision partition, LWRAM-move candidate |
| 20 | 6,144 | `gStaticSurfacePartition` | `src/engine/surface_load.o` | (b) CPU-only mutable — collision partition, LWRAM-move candidate |
| 21 | 5,120 | `gUnusedThread2Stack` | `src/buffers/buffers.o` | **flag for Task 5**: named "Unused" — worth confirming it is genuinely dead before proposing it as a lever, since removing dead code is out of this plan's stated scope (only right-sizing/relocation is authorized) |
| 22 | 4,920 | `animdata_mario_ear_left_1` | `src/goddard/dynlists/anim_group_1.o` | (c) write-once candidate |
| 23 | 4,920 | `animdata_mario_lips_4_1` | `src/goddard/dynlists/anim_group_1.o` | (c) write-once candidate |
| 24 | 4,920 | `animdata_mario_lips_3_1` | `src/goddard/dynlists/anim_group_1.o` | (c) write-once candidate |
| 25 | 4,920 | `animdata_mario_cap_1` | `src/goddard/dynlists/anim_group_1.o` | (c) write-once candidate |

Observations for Task 5:

- The Goddard `dynlist`/`animdata_*` family (rows 7, 10–12, 18, 22–25, and
  many more of the same shape below the top 25 — every `anim_group_*.o` and
  `dynlist_*.o` object contributes at least one `.data.animdata_*` or
  `.data.dynlist_*` entry) is by far the largest *repeated* pattern in the
  list and the same class of fix as the four already-landed const sweeps
  (`91f02ffd`/`465fb8b0`/`fce3f4b5`/`0ebd5b05`). A handful of these already
  clear the entire measured 3,128-byte HWRAM shortfall on their own if they
  are genuinely write-once (each write site needs tracing before commit,
  per the established per-item discipline).
- `gSineTable` (row 9) is a classic LUT and a strong single-item candidate.
- Rows 1–2 (`gAudioHeap`, `gObjectPool`) dwarf everything else combined but
  are runtime-mutable state this plan's standing constraints do not permit
  shrinking without owner sign-off — they are listed for completeness, not
  as unilateral levers.
- Rows 14–16, 19–20 are CPU-only mutable and are LWRAM-move candidates only
  once Task 4 clears LWRAM headroom (currently negative by 784 B — moving
  more into LWRAM before that closes only makes the second assert worse).

## Open items handed to later tasks

- **Task 1 is not fully closed by this report**: the fresh-link defect
  above still blocks producing a current-HEAD `.map`. Re-running Task 1
  Step 1 after that defect is fixed (or after confirming/ruling out a
  concurrent-build race) is a prerequisite for trusting any of Task 5's
  relocation proposals against the *current* number rather than the
  3-commits-stale 3,128/784 figures reconstructed here.
- Task 4 (LWRAM) and Task 5 (HWRAM) should treat 3,128 B HWRAM / 784 B
  LWRAM as the last *confirmed* figures, and explicitly re-confirm against
  a fresh link before finalizing any owner-approved relocation list — the
  three landed commits since this map may have already closed some of the
  gap, or something else may have grown it back; neither is confirmed
  either way by current evidence.
