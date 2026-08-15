# Sprint 2 Task T2.2 — reclamation package + un-split

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `a90f1628` (T2.1 evidence).
- Plan: `docs/superpowers/plans/2026-08-15-sprint2-cadence-recovery.md`,
  Task T2.2. Gated on T1's attribution
  (`sprint2-t1-hwram-attribution.md`), T2.0's reference sweep
  (`sprint2-t2_0-reference-sweep.md`, lessons L1-L6/L11) and T2.1's
  measured peaks (`sprint2-t2_1-peak-capture.md`).
- Question answered: does returning the renderer's hot working set to
  32-bit HWRAM — funded by the peak-cleared capacity shrinks — recover
  cadence from ~1.1 FPS?

## Headline

**Memory objective: MET.** 67,584 B of committed HWRAM recovered, the full
54,080 B hot working set returned to HWRAM, true slack over the `0x1F00`
floor up from **472 B to 13,944 B (29x)**. All host gates green.

**Cadence objective: NOT MET.** Sustained cadence is **1.068 FPS**
(60 presentations / 3,370 VBlanks, 56.2 VBlanks/frame) against the R1
baseline's directly comparable **1.071 FPS** (60 / 3,362, 56.0
VBlanks/frame) — a **-0.24% change, i.e. no material difference**. Every
phase is unchanged: construction 24.72 vs ~24.5 VBlanks/frame, master
finalization 5.80 vs 5.8, dropped VBlank credits 14.42 vs ~14.

The HWRAM/LWRAM placement of the 54,080 B working set is **not** the
cadence bottleneck this experiment can move. See "What this does and does
not prove" for the confound that keeps the T1 hypothesis only half-tested.

## Commits

| SHA | Subject | Revert notes |
| --- | --- | --- |
| `971f8f93` | `fix(render)`: master display-list overflow now a detected degrade, not corruption | Independent; a correctness fix worth keeping regardless of the rest |
| `26ae9c38` | `perf(mem)`: peak-cleared capacity shrinks — cmdt 1664, GFX pool 4096, libyaul pool 0x4000 | Revert together with `03c697f5` |
| `98dab715` | `perf(render)`: return hot working set to HWRAM — full un-split of `49370e31` | Requires `26ae9c38`'s bytes to link; revert this BEFORE the shrinks |
| `03c697f5` | `fix(build)`: margin gate command-bank extent tracks the 1664 capacity | Companion to `26ae9c38` |

## Safety checks before the cuts (T2.0 L5/L6) — findings

**L5 (clamp check) — VDP1 command arena: ALREADY SAFE, no code needed.**
`sm64_saturn_command_arena_reserve` (`saturn_command_arena.h:44-57`) fails
closed on exhaustion, always preserving the final END slot; it never
faults or overruns. Every live call site handles `NULL` by counting
`profile.reject_vdp1_arena_capacity` (+`pipeline_faults`) and continuing
(`saturn_demo_render.c:3462,3620,4156`), Mario's textured batch is
all-or-nothing with a flat per-primitive fallback (`:4103`), and
`sm64_saturn_vdp1_frame_bank_ready` rejects a count above capacity
(`saturn_vdp1_frame_bank.c:222-232`) so a bad frame is never published —
the previous complete frame stays on screen. This already matches
SlaveDriver's clamp discipline (`SPR.C:142-143,430-441`).

**L5 — master display list (`gGfxPool`): UNSAFE, clamp ADDED (commit A).**
Stock SM64's pool is a two-sided shared arena: `gDisplayListHead++` writes
are unchecked upward while `alloc_display_list()` (`src/game/memory.c:750`)
carves matrices/viewports downward and returns an **unchecked NULL** that
~90 call sites dereference. Overflow was silent corruption — first into
live top-down allocations (garbage matrices), then past the pool object
into `spTask`/adjacent `.bss`. Because this is exactly the capacity being
cut, the clamp landed first as its own commit: `gGfxPoolOverrun` latches
on either failure side, and `display_and_vsync()` drops only that frame's
`exec_display_list` (previous frame preserved, VBlank pacing untouched);
`gGfxPoolOverrunFrames` counts drops for host probes. Honest limitation:
detection is at frame end, so a pathological single-frame overrun larger
than the remaining runway can still write past the pool before being
caught — the guard converts the realistic incremental-growth class into a
detected degrade, it is not a bounds check on every write.

**L6 (drop-order check) — ALREADY CORRECT, capacity kept at 1664.**
The active demo path's admission already drops **far** geometry first:
`demo_render_finalize` computes
`sm64_saturn_command_arena_budget_before_tail` against Mario's essential
command count, then skips the leading `terrain_first` entries of the
far-to-near painter stream (`saturn_demo_render.c:4645-4655`), with the
in-tree comment already citing Z-Treme's `ZT_RENDERING.c:494-503`
rationale. Near geometry and Mario are the retained tail. L6 is therefore
satisfied without a change, and the conservative 1792 variant was **not**
required. Recorded for completeness: the dormant
`sm64_saturn_fast3d_vdp1_emit` path (non-demo pipelines, compiled out
under `SATURN_DEMO_PATH=1`) iterates far-to-near and *its* clamp would
drop near geometry — irrelevant to this tuple, but the place to fix if a
non-demo pipeline is ever revived.

## The capacity shrinks (all peak-gated by T2.1)

| Member | Change | T2.1 peak | Margin | Bytes |
| --- | --- | ---: | ---: | ---: |
| `SOURCEBOOT_VDP1_COMMAND_CAPACITY` | 2048 -> 1664 | 653 cmdts | 1,011 | -24,576 |
| `GFX_POOL_SIZE` | 6400 -> 4096 | 443 entries | 3,653 | -18,432 |
| libyaul `_private_pool` | 0xA000 -> 0x4000 | 8,276 B extent | 8,108 B | -24,576 |
| SMPC peripheral pool | 14 -> 4 | 2 blocks | — | **skipped** |
| | | | **total** | **-67,584** |

`GFX_POOL_SIZE` caveat, stated plainly: T2.1 measured only the **bottom-up**
display-list high-water (443). The pool's **top-down** `alloc_display_list`
high-water was never measured. The L5 guard is what makes that unmeasured
side safe to cut against; a future rung should measure it directly.

SMPC 14->4 was skipped deliberately. It is not reachable through the same
configuration surface as the private pool: `_private_pool` lives in a
self-contained 182-line allocator TU that can be superseded whole, whereas
the SMPC pool lives inside the 413-line `smpc_peripheral.c` driver, so
taking it would mean superseding a live driver — deeper surgery than this
task's arithmetic needs (67,584 B recovered vs 54,080 B spent). It remains
the ranked reserve.

## How the libyaul pool was changed (patch, not configuration)

`TLSF_POOL_PRIVATE_SIZE` was first checked for a configuration surface and
has none: it is a bare `#define ... (0xA000)` at
`third_party/libyaul/libyaul/kernel/mm/internal.c:22` with no `#ifndef`
guard, no `YAUL_OPTION_*` hook in `env.mk`, and the library is consumed as
a prebuilt `libyaul.a` from `work/yaul-install`. `third_party/` is
READ-ONLY and was not modified.

Mechanism (`tools/patches/libyaul-private-pool-0x4000.patch` +
`src/port/saturn/sourceboot/Makefile`): the build stages a patched **copy**
of that single MIT translation unit into the generated-sources tree and
compiles it as an ordinary port object. Pipeline, re-validated on every
build: `git show` the pinned submodule commit's blob (canonical LF —
immune to the checkout's CRLF smudge) -> SHA-256 gate on the pristine bytes
-> GNU `patch` -> SHA-256 gate on the patched bytes -> atomic `mv`. A
verbatim copy of `kernel/internal.h` (not installed by yaul) is staged
beside it. The object precedes `-lyaul` on the link line and defines **all
eleven** globals of the archive's `mm/internal.o` member, so the linker
never pulls the upstream member — the same supersede-by-link-order
mechanism the tree already uses for `libsm64softfp` (documented in the
Makefile's LINK ORDER note). `-DMALLOC_IMPL_TLSF` is carried inside the
patch so the user-pool paths compile identically to the installed library.

Pinned values (the rule fails loudly on any mismatch; `check-libyaul`
independently fails the build on a submodule bump):

| Item | SHA-256 |
| --- | --- |
| submodule HEAD | `6012f79f237773378c8014e70d8998ad95a38d98` |
| pristine `internal.c` | `a6f81201fc598abaadfa641295537513f8d170e2672f16efd6fc12bc88fd83ef` |
| pristine `internal.h` | `e9cb1859e9957a9bb6fd92f2f7b437f16df959c092ce7f3fa464b65d431cb09e` |
| patched `internal.c` | `1837d35ee9831477150e226a535462b8692fcb794603d7ea849db0984f8bd58e` |

`tools/patches/**` is pinned `-text` in `.gitattributes` so the patch
survives a CRLF checkout byte-for-byte; `THIRD_PARTY_LICENSES.md` records
the patched copy, its reuse mode, and the retained MIT notice.

## Build

Same 27-variable product invocation as `sprint1-stage1-link-smoke.md`
(pool 208, `SATURN_DIAGNOSTIC_MODE=0`) via `with-msys-toolchain.ps1` ->
MSYS `sh --noprofile --norc -l`, sourcing `../../.yaul.env` then
`unset COMPILER_PATH`.

**Profile/identity note (differs from T2.1): no profile edit was needed.**
The capacity constants are **not** sealed identity scalars —
`gen_build_identity.SCALAR_FIELDS` / `COMPILER_CONFIG_FIELDS` contain no
capacity field. They enter identity through `source_hash` via the source
closure, so a fresh identity sealed automatically and the committed
product profile stayed at `diagnostic_mode: 0` untouched.

Attempts (constitution two-attempt log — all three are the same single
causal change; 1 and 2 are the known package-staleness cascade, not new
implementation attempts):

1. **FAILED** at `source-actor-family-bundle`: "actor family bundle
   publication is stale for current inputs". Diagnosed exactly: the bundle
   embeds per-source-file `{"path","sha256"}` records, and this task's
   committed source edits re-derive them (35 byte-ranges; payload size
   identical at 171,548 B, only hash records and the header digest
   differ). Sanctioned stage-2 repair: deleted `actors-v3-g15`,
   republished generation 15 from current inputs, `--verify-publication`
   PASS.
2. **FAILED** one level up at `compile-actor-scene-package`: "publication
   target exists: .../scene-v3-g15/scene.s64p" — publish-or-verify decided
   to republish (its actor input had changed) but the immutable target was
   present. Same cascade; sanctioned stage-1 repair: deleted
   `scene-v3-g15`. Sealed `id-6f66d2a1935edd7a` (no ELF; superseded).
3. **BUILD SUCCEEDED.** Sealed identity **`id-6b7c7e5d5f71e809`**, label
   `feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfg6b7c7e5d5f71`.

| Artifact | SHA-256 |
| --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `3f4e1c86690af9d33e343ef7fd62f9fc035efd14ff50d6d21063a5eeae0cc894` |
| `sm64-saturn-sourceboot-e2.iso` (5,169,152 B) | `9645ebb077cb7aaedfd7c2d8d6349a0f1f19e0a8d05d0a97ef58f07a380f4785` |
| `sm64-saturn-sourceboot-e2.cue` (not identity-bearing) | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | `03f3c20109d333a8ae7f5e32276165cc5bb5df3c1732e4030b8694962b722392` |

Pre-build preservation (mandatory rule): accepted candidates
`id-b3aceeb28570230b`, `id-782c9c8f323a01a0`, and the owner-accepted
`id-86d3880727ed1d10` copied to `releases/2026-08-15_1241_t2_2-pre-build/`
before any build ran, ISO hashes re-verified against the stage-2 record.
This build's artifacts preserved to `releases/2026-08-15_1310_t2_2/`.

## Gate 1 — `verify-memory-map`

First run **FAILED**, correctly: the gate pins the VDP1 transport bank's
exact extent through `VDP1_COMMAND_BANK_BYTES = 2 * 2048 * 32`, so the
capacity cut tripped `RESULT = FAIL: ELF command banks are not the exact
aligned HWRAM range`. That is the gate doing its job. The constant was
updated to `2 * 1664 * 32` (commit `03c697f5`), leaving the exactness,
32-byte alignment and HWRAM-containment checks untouched; both the stale
2048 value and a 1663 off-by-one were verified to still fail the gate.

Verbatim output after the constant fix:

```
verify-memory-map: checking /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery/build/saturn/sourceboot/e2-bob-identity-id-6b7c7e5d5f71e809/obj/sm64-saturn-sourceboot-e2.elf
verify: D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\saturn-recovery\build\saturn\sourceboot\e2-bob-identity-id-6b7c7e5d5f71e809\obj\sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FAA88
  hwram_remaining = 0x5578 bytes (required >= 0x1F00)
  lwram_end       = 0x002E89E0
  lwram_remaining = 0x17620 bytes (floor >= 0x4000)
  RESULT          = OK
```

Margins and section deltas vs the R1 candidate (`id-b3aceeb28570230b`):

| Section | R1 | T2.2 | Delta |
| --- | ---: | ---: | ---: |
| `.text` | `0x83D08` | `0x83D38` | +48 (the L5 guard) |
| `.rodata` | `0x1DFD` | `0x1DFD` | 0 |
| `.data` | `0x857C` | `0x857C` | 0 |
| `.bss` | `0x6AC90` (437,392) | `0x677D0` (423,888) | **-13,504** |
| `.uncached` | `0xF78` | `0xF78` | 0 |
| `.lwram_bss` | `0xE5D38` (941,368) | `0xD89D8` (887,256) | -54,112 |

The `.bss` delta is exactly `54,080 - 67,584 = -13,504`. `___end`
`0x060FDF28` -> `0x060FAA88`; `hwram_remaining` `0x20D8` (8,408) ->
`0x5578` (21,880); **true slack over the `0x1F00` floor: 472 B ->
13,944 B.** The `0x1F00` floor holds with 29x the previous headroom, so
the "any 472-byte growth reopens the overflow" fragility recorded in
stage 1b is retired. LWRAM margin `0x17620` (95,264 B) against its
`0x4000` floor.

Placement proof (`sh-elf-nm` on the linked ELF):

| Symbol | Address | Tier |
| --- | --- | --- |
| `_s_bob_hot_workarea` | `0x060DD790` | HWRAM (was LWRAM) |
| `_s_actor_gouraud` | `0x060E900C` | HWRAM (was LWRAM) |
| `_s_actor_gouraud_addresses` | `0x060E85FC` | HWRAM (was LWRAM) |
| `_s_actor_queue_merge_ids` | `0x060EA432` | HWRAM (was LWRAM) |
| `_s_actor_slots` | `0x060E9F2A` | HWRAM (was LWRAM) |
| `_s_actor_texture_slots` | `0x060E9A22` | HWRAM (was LWRAM) |
| `_sourceboot_vdp1_cmdts` | `0x06092340` | HWRAM (same tier, now 0x1A000) |
| `__private_pool` | `0x060AC3AC` | HWRAM; next symbol at `0x060B03AC` = exactly `0x4000` |
| `_sourceboot_fast3d` | `0x002CDAC4` | **LWRAM (deliberately deferred)** |

## Gate 2 — host contracts

| Suite | Result |
| --- | --- |
| `verify-audio-loop-contracts` | **OK — 24 tests** |
| `verify-pcm68k-model` | **OK — 18 tests, exit 0** |
| `test_dual_sh2_work_storage_contract` (retargeted) | **OK — 4 tests** |
| `test_vdp1_staging_relocation` (retargeted + repaired) | **OK** |

The work-storage contract was retargeted to pin the NEW policy (all 23
scratch symbols + the workarea in HWRAM) at the same guard strength, and
mutation-verified: re-evicting the workarea, re-evicting
`DEMO_ACTOR_WORK_CACHE`, and re-evicting `DEMO_CPU_WORK_CACHE` were each
confirmed **KILLED**.

`test_vdp1_staging_relocation.py` was found **already failing at base HEAD
`a90f1628`** (verified by stashing): its assertion that the declaration
carries no section attribute predated the deliberate
`.sourceboot_vdp1_cmdts` own-section placement, and no make target runs it
— an orphaned, silently-broken contract. It is now repaired to pin the
current placement positively (own section required, `lwram` forbidden,
both linker inputs required) and retargeted to 1664.

Pre-existing failures NOT touched:
`tools/saturn/test_render_snapshot_source.py` has 2 failures at base HEAD
(recorded in T2.1's evidence and unchanged here).

## Gate 3 — FPS capture vs the 1.1 baseline

Instrument: headless Ymir **build-agent2**
(`ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`,
SHA-256 `fcc88d82...3943`), BIOS
`sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`, absolute
paths, `--startup-vblanks 4096`, release manifest bound. On-target identity
**MATCH** (16 bytes at `0x22400000`, `d88b17a9...d23b5` expected and
observed) at startup attempt **681**/4096 — consistent with R1's 683.
No desktop launch; the owner owns the look-and-listen.

**Run 1 (tool-complete, `--max-vblanks 3600`, default 2 presentation
events)** — `docs/saturn/evidence/reports/sprint2-t2_2-throughput.json`,
status `complete`:

- 1 measured interval, **2.0 guest FPS**, `vblank_delta` 30 — mean =
  median = 1%-low = 2.0 by construction from n=1.
- Phase attribution: construction 25, master finalization 5, simulation 5,
  slave overlap 2, dropped VBlank credits 14, unattributed 0.
- **This is bit-for-bit the same interval R1 recorded** (R1:
  `vblanks_advanced` 1595 / 2 events / same phase counts; T2.2: 1594).
- Queue terminal record: `master_failures 0, slave_failures 0, qf 0,
  qw 0`. All 64 retained `instance.stopped` notifications have reason
  `frame_limit` — **no SH-2 exceptions**.

**Run 2 (sustained cadence, `--presentation-events 60`)** — the tool
aborted in `summarize_cadence` with "phase VBlank crossings exceed the
observed interval", the **same known tool invariant R1 hit at 60 events**;
as in R1 the sustained figure comes from the run's preserved cadence-trace
diagnostics (artifact:
`releases/2026-08-15_1310_t2_2/sprint2-t2_2-throughput-sustained.json`):

| Metric | R1 baseline (60-event run) | T2.2 | Delta |
| --- | ---: | ---: | ---: |
| Presentations / VBlanks | 60 / 3,362 | 60 / 3,370 | — |
| **Sustained FPS** | **1.071** | **1.068** | **-0.24%** |
| VBlanks per frame | 56.0 | 56.17 | +0.17 |
| Construction | ~24.5 | 24.72 | ~0 |
| Master finalization | 5.8 | 5.80 | 0 |
| Simulation | — | 6.07 | — |
| Slave work overlap | — | 2.73 | — |
| Dropped VBlank credits | ~14 | 14.42 | ~0 |

R1's other cross-check (68 presentations / 3,600 VBlanks = 1.133 FPS) used
a different event budget and is not directly comparable; the 60-event runs
are the like-for-like pair.

**Verdict: no material cadence change. The sprint's cadence objective is
not met by this task.** There is also no regression: cadence, queue
health, and exception count are all unchanged.

## What this does and does not prove

**Proves:**

- The reclamation package is real and safe on this route: 67,584 B
  recovered, all four host gates green, no protocol/queue faults, no SH-2
  exceptions, identity matched on target.
- The full 54,080 B hot working set is back in 32-bit HWRAM, pinned by a
  mutation-verified contract test.
- HWRAM slack over the floor is now 13,944 B instead of 472 B — the tree
  can absorb ordinary `.bss` growth again.
- **Returning that 54,080 B, by itself, does not move cadence.**

**Does not prove — the confound, stated plainly:** this is only a *partial*
test of T1's hypothesis. T1 measured that restoring complete A9A hot-set
residency needs ~98,192 B, because `_sourceboot_fast3d` (44,616 B — the
interpreter's per-frame matrix/vertex/resolved arrays) is *also* in LWRAM.
This task returned 54,080 B and left fast3d there. The result therefore
narrows the hypothesis rather than refuting it: the workarea and actor
scratch are not the cadence bottleneck, but "hot set in 16-bit LWRAM" has
not been tested end-to-end while the single largest hot tenant is still in
LWRAM.

Also unproven here: visuals, audio, and any owner-facing quality claim.
The capacity cuts are quality risks the owner look-and-listen must
adjudicate — in particular the cmdt cut (peak 653 vs 1,664 on the measured
route only; a scene that defeats quad-merge could approach the cap and
drop far geometry) and the `GFX_POOL_SIZE` cut's unmeasured top-down side.

## Next rung (deliberate deferral, recorded per tasking)

`_sourceboot_fast3d` (44,616 B, `main.c:212`, per-frame hot) **stays in
LWRAM**: the arithmetic does not close — 67,584 B recovered minus 54,080 B
spent leaves ~13,504 B, well short of 44,616 B. The unlock is T2.0 **L3**:
SlaveDriver's whole VDP1 work-RAM cost is a 10,240 B staging window
(`SPR.C:20-21,35-36`) with both command banks in VDP1 VRAM filled by block
DMA (`SPR.C:71-76,141-157`). Adopting that
build-in-VRAM-through-a-small-staging-window architecture would return
~96,256 B of our now-106,496 B command staging outright — an order of
magnitude more than this whole package, and enough to rehome fast3d with
room to spare. It changes the transport contract, so it needs its own CUE
and its own gate.

Given this task's negative cadence result, the evidence now points away
from further memory-tier work and toward the **algorithmic** levers:
construction is 24.72 VBlanks/frame (44% of a 56-VBlank frame). That is
T2.3's painter relink counting sort (T2.0 L7/L8: ~115,200 steps ->
~1,864) and T2.4's scene-construction profiling, with T2.0 L12's
master-spin measurement as the prerequisite for any master/slave
rebalance.

## Documentation corrections made in this task

Two pre-existing stale comments were corrected (comment-only; they
post-date the measured artifact and change no behavior, but they will
change the sealed `source_hash` of the *next* build):

- `sourceboot-cart.x:128` claimed the command banks "total 0x20000 bytes"
  — now 0x1A000.
- `saturn_fast3d_frontend.h:608` claimed "Task 10's VDP1 command list lives
  in LWRAM, not HWRAM, so it doesn't compete with this budget". That has
  been false since Task 14 and directly contradicts T1's central finding
  (the command banks are the largest single HWRAM tenant).

## Reproduction

```
# build (27-variable product invocation, pool 208, diag 0) via
# tools/saturn/with-msys-toolchain.ps1 -> MSYS sh -l, source ../../.yaul.env,
# unset COMPILER_PATH, make -f Makefile.saturn.mk -j1 sourceboot <27 vars>

make -f Makefile.saturn.mk verify-memory-map
make -f Makefile.saturn.mk verify-audio-loop-contracts
make -f Makefile.saturn.mk verify-pcm68k-model
python tools/saturn/test_vdp1_staging_relocation.py

python tools/saturn/capture_sourceboot_throughput.py \
  --ymir .../ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl "sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game build/saturn/sourceboot/e2-bob-identity-id-6b7c7e5d5f71e809/sm64-saturn-sourceboot-e2.cue \
  --elf  build/saturn/sourceboot/e2-bob-identity-id-6b7c7e5d5f71e809/obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest build/saturn/sourceboot/e2-bob-identity-id-6b7c7e5d5f71e809/saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_2-throughput.json \
  --startup-vblanks 4096 --max-vblanks 3600
# sustained: add --presentation-events 60 (the tool aborts in
# summarize_cadence; read observation_diagnostics.last_cadence_trace.record)
```
