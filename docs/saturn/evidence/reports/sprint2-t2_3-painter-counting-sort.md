# Sprint 2 Task T2.3 — painter relink counting sort

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `71065ab6` (T2.2 STATE/ROADMAP commit).
- Plan: `docs/superpowers/plans/2026-08-15-sprint2-cadence-recovery.md`,
  Task T2.3. Design pinned by T2.0's reference sweep
  (`sprint2-t2_0-reference-sweep.md`, lessons **L7/L8/L9/L10**).
- Why now: T2.2 measured the memory-tier lever out
  (`sprint2-t2_2-reclaim-unsplit.md` — 67,584 B recovered, cadence
  1.068 vs 1.071, no material change). The evidence redirected to the
  algorithmic levers, and this is the first one.
- Question answered: how much of the frame does the per-bin painter
  rescan actually cost, and does removing it move cadence?

## Headline

**Correctness objective: MET.** The counting sort emits a **byte-identical**
painter chain to the predecessor on every case tested, including the
arena's full 1664-command capacity. Pinned by a three-way host harness and
mutation-verified.

**Cadence objective: PARTIALLY met — a real but small gain.** Sustained
cadence is **1.0866 FPS** (60 presentations / 3,313 VBlanks, 55.22
VBlanks/frame) against T2.2's directly comparable **1.0682** (60 / 3,370,
56.17 VBlanks/frame) — **+1.72%**. The saving is **0.97 VBlanks/frame**,
and its attribution is exact: master finalization fell 5.80 → 4.83
VBlanks/frame (−16.7% of that phase) while *every other phase is
bit-identical* to T2.2. This does not reach the sprint gate on its own.

Read honestly: the rescan was real waste and it is now gone, but it was
**3.9% of the 24.72-VBlank construction figure**, not a major share of it.
The remaining 23.75 VBlanks of construction are elsewhere. See
"What this does and does not prove".

## Commits

| SHA | Subject |
| --- | --- |
| `55449eb2` | `test(render)`: pin painter-chain equivalence before the T2.3 relink swap |
| `0a5b5ccd` | `perf(render)`: painter relink is a counting sort, not a per-bin rescan |

## The defect, measured

`sm64_saturn_vdp1_backend_link_depth_bins()` overloaded `cmd_link` as
**both** the depth-bin sort key **and** the output JUMP_ASSIGN link. Having
destroyed the key when it wrote the link, it could not learn a command's
bin on any later visit, so it re-walked the whole live command range once
per bin, using the link *type* field as an "already placed" marker:

```
for (bin = 0..bin_count-1)
    for (index = end-1 .. first)          /* full rescan, every bin */
        if (already linked || cmd_link != bin) continue;
```

Cost `N * (2 + bin_count)` record visits, where `N` is the drawable range
(`live_count - setup_count - 1`). Each visit touches a 32-byte record, so a
sequential sweep is one cache line per command through an array far larger
than the SH7604's 4 KB cache — 64 times per frame.

## Design — what was reused, and from where

**L7 is the whole design.** SGL never encodes draw order in `CMDLINK`: it
keeps a `Z` key and a `NEXT` chain out of band in the 6 bytes past the
30-byte payload (`SL_DEF.H:425-442`), buckets once, and realises order
through the DMA descriptor sequence (`SGLFAQ_F.TXT:1057-1112`). The
actionable half of that is **separating the sort from the link write**.

**What I reused vs wrote, stated plainly.** L8 points at
`src/port/saturn/gfx/saturn_terrain_depth_bins.h:74-95`
(`..._depth_bins_scatter`) as an in-tree stable scatter. I read it and
**reused its shape, not its code**, and here is why, so the decision is not
re-litigated:

- That scatter is a **prefix-sum** scatter over an out-of-line array of
  8-byte `{record, key}` references, with a caller-supplied `scratch`
  buffer of the same size. Applied here it would need a second
  command-count-sized side buffer (1,661 × 2 B minimum, and its own
  lifetime and placement decision) — and this function's documented
  contract is that it allocates **no per-frame side buffer**.
- SGL's own answer to exactly that problem is the intrusive `NEXT` chain,
  which needs no second buffer at all. So I took **L8's counting-sort
  structure and L7's out-of-band-chain mechanism**, and used the one field
  that is already dead by that point in the frame — `cmd_link` itself — as
  the chain pointer.

The terrain scatter is therefore left untouched: it is the right shape for
its own path (it sorts references that must survive the sort), and wrong
for this one. No second implementation of a general sort was added; the new
code is ~25 lines inside the existing function.

**L9 honoured:** `SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT` stays 64,
`_SHIFT` stays 7, and the key derivation is untouched, so the output is
directly comparable. **L10 recorded, not done:** if the relink shows up hot
again, the precedented next step is coarsening the ordering unit to
per-BSP-leaf (SlaveDriver's per-frame link writes number in the tens —
`WALLS.C:2263,2269`, `1888-1889`). It is now far too cheap to be worth it.

### The three passes

```
pass 1  validate every raw tag, writing nothing            -> N
pass 2  walk backwards, prepend onto heads[bin] via
        cmd_link (intrusive NEXT); descending + prepend
        leaves each chain in ascending producer order      -> N + bin_count
pass 3  drain bin_count-1 .. 0, walk each chain forward,
        JUMP_ASSIGN the previous command at this one       -> N + bin_count
```

Working memory: **one 128-byte stack table** (`uint16_t heads[64]`). No
`.bss`, no heap, no side buffer. `end` — the END command's index — doubles
as the chain terminator and the "nothing emitted yet" sentinel, which is
unambiguous because every live draw index is strictly below it. Each chain
link is read before it is overwritten, so the walk and the link write share
`cmd_link` safely.

The predecessor's separate link-type **strip pass is deleted**:
`vdp1_cmdt_jump_assign()` already does `cmd_ctrl &= 0x8FFF` before setting
the type (`third_party/libyaul/.../vdp1/cmdt.h:497-521`), and every live
command is now assigned exactly once, so the strip is subsumed.

### Step counts

| | drawable `N` | predecessor `N*(2+64)` | counting sort `3N + 2*64` | ratio |
| --- | ---: | ---: | ---: | ---: |
| T2.1 measured peak (653 published) | 650 | **42,900** | **2,078** | 20.6x |
| arena capacity (1664) | 1,661 | 109,626 | 5,111 | 21.4x |

40,822 record visits per frame removed at the measured peak.

Correction for the record: commit `0a5b5ccd`'s message and the CHANGELOG
entry quote **2,087 / 43,098**, computed with `N` = the *published* command
count 653 instead of the *drawable* count 650 (`live_count` minus the 2
setup commands minus the END slot). The exact figures are **2,078 /
42,900**; the CHANGELOG is corrected in this commit. The ratio is unchanged
to one decimal.

### Cost in the image

`.text` 539,960 → 540,040 (**+80 B**); `.rodata`, `.data`, `.bss`,
`.uncached`, `.lwram_bss` all **byte-identical** to T2.2. `sh-elf-nm`
confirms the change is entirely inside the one caller:
`_demo_render_finalize` 0x1714 → 0x1766 (+82 B), same address `0x06073C74`.
The relink is fully inlined there and is the only text that moved.

## The equivalence guarantee

The binding requirement was a byte-identical chain. Approach:

1. **The oracle was committed before the algorithm changed** (`55449eb2`).
   The predecessor is retained verbatim as
   `sm64_saturn_vdp1_backend_link_depth_bins_reference()` behind
   `SM64_SATURN_VDP1_BACKEND_LINK_REFERENCE`, which **only**
   `tools/saturn/vdp1_painter_chain_test.c` defines — so no Saturn image
   carries it. This is the arrangement `saturn_terrain_depth_bins.h`
   already uses for its predecessor merge oracle
   (`SM64_SATURN_TERRAIN_DEPTH_BINS_COMPARE`); I followed the existing
   in-tree convention rather than inventing a second one. Verified in the
   product image: `SATURN_DIAGNOSTIC_MODE=0` build's `.text` grew by only
   the +80 B of the new code, and the reference symbol is absent.
2. **Three independent statements of the contract** are cross-checked on
   every case: the reference implementation, the shipped implementation,
   and a from-first-principles model in the test that stable-sorts the draw
   indices by descending bin and derives the links that ordering implies.
   The assertion is `memcmp` over the **entire command array**, so a
   divergence in link value, link type, END handling, the prefix command,
   or any field that should not have moved fails.
3. **16 cases**, chosen for the awkward inputs: empty draw range; a single
   command in bin 0 and in bin 63; all commands in one bin (nearest,
   farthest, interior); bins occupied only at both extremes, interleaved so
   a stability break inside either bin reorders the chain; one command per
   bin in ascending and descending tag order; sparse bins with ties;
   pseudo-random mixtures at 500 and at 653 (T2.1's measured peak); the
   arena's **full 1664-entry capacity** (1,661 draws) in four tag patterns
   — random, all-nearest, all-farthest, round-robin; narrower 8-bin and
   1-bin tables; and invalid-tag atomicity at four positions (first,
   second, middle, last), where both implementations must return false and
   leave the array bit-identical to its pre-call state.

Seeded control words vary the command-type nibble, the link-type field and
the END bit per draw, so the predecessor's strip pass and the new
implementation's reliance on `jump_assign`'s masking are both exercised.

### Mutation proofs

Run against the **equivalence harness alone** — the three pre-existing
legacy cases were excluded from `main()` for these runs, so each kill is
attributable to the new harness and not to an older assertion. Mutations
were applied to the working tree, run, and reverted; **none is committed**.

| # | Mutation | Result | First case that caught it |
| --- | --- | --- | --- |
| M1 | Within-bin stability broken (pass 2 traverses ascending, so prepending reverses producer order) | **KILLED** | `all commands one bin` |
| M2 | Bin direction inverted (pass 3 drains near-to-far) | **KILLED** | `both extremes interleaved` |
| M3 | END/tail handling dropped (final `jump_assign(previous, end)` removed) | **KILLED** | `single command near bin` |

The model itself was separately verified non-vacuous: inverting **its** bin
direction makes the harness fail at `both extremes interleaved`, so it is
constraining direction rather than agreeing by construction.

### Callers and intermediate state — checked

- **Nothing observes intermediate state.** The only caller is
  `demo_render_finalize` (`saturn_demo_render.c:4689`), which calls the
  relink synchronously immediately after
  `sm64_saturn_vdp1_backend_finish()`. The bank only becomes visible to the
  transport when `sm64_saturn_vdp1_frame_bank_ready()` runs afterwards
  (`sourceboot/main.c:1326`), so no other CPU, DMA engine or ISR can read
  the array mid-relink. Both the old and the new implementation leave a
  mixed raw-tag/linked array mid-loop; the new one is not a new hazard
  class.
- **Same point in the frame, same inputs.** Call site, argument, and
  ordering are unchanged; only the function body moved. The lowerers still
  write raw bin tags into `cmd_link`
  (`saturn_demo_render.c:3664-3757,3895,3920,4170,4343`) exactly as before.
- **One deliberate precondition narrowing.** `bin_count` above
  `SM64_SATURN_VDP1_BACKEND_MAX_DEPTH_BINS` (64) now fails closed, since
  the chain-head table is fixed. `saturn_demo_render.c` carries a
  `_Static_assert` that `SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT` stays inside
  the bound, and the test pins the fail-closed behaviour (array untouched,
  and 64 still succeeds). The reference oracle keeps the old open domain
  and the header documents the difference, so the equivalence claim is
  scoped to `1 <= bin_count <= 64`.

## Build

Same 27-variable product invocation as `sprint1-stage1-link-smoke.md` /
T2.2 (pool 208, `SATURN_DIAGNOSTIC_MODE=0`), via
`tools/saturn/with-msys-toolchain.ps1` → MSYS `sh --noprofile --norc -l`,
sourcing `../../.yaul.env` then `unset COMPILER_PATH`.

**Profile: no edit required, and checked rather than assumed.** The
capacity constants T2.2 introduced are plain source-level defaults
(`SOURCEBOOT_VDP1_COMMAND_CAPACITY 1664U` at `sourceboot/main.c:752`,
`GFX_POOL_SIZE 4096` at `src/game/game_init.h:26`) — not Make variables and
not identity scalars. The committed profile
`tools/saturn/profiles/sourceboot-bob-demo-v1.json` already agrees with the
invocation (`object_pool_capacity: 208`, `diagnostic_mode: 0`), and no Make
default changed in this task, so it is untouched.

**One attempt, build succeeded.** The g15 package-staleness cascade did
**not** trigger this time (T2.2's source edits changed the actor-bundle
input hashes; this task's edits are confined to `gfx/` translation units
that the bundle does not record), so no republication repair was needed.

Sealed identity **`id-aa57d83c898e3af1`**, label
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfgaa57d83c898e`.

| Artifact | SHA-256 |
| --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` (9,970,012 B) | `c92c2efdceb4c9411c2c4a6e00b62c14af4ef9c0a95bcbf2382910c92e64f99d` |
| `sm64-saturn-sourceboot-e2.iso` (5,169,152 B) | `4f9140b783bed3674287bd4371d70e3d0c1ac682e6acb5664c436e9574d770de` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | `e80aa2b6246291febb09f23605386c7fb7f687d623375380af2f363806164394` |

Preservation: the current and accepted candidates' primary artifacts
(`id-6b7c7e5d5f71e809`, `id-86d3880727ed1d10`, `id-b3aceeb28570230b` —
ELF/ISO/CUE/manifest, hashes re-verified against T2.2's recorded values)
copied to `releases/2026-08-15_1528_t2_3-pre-build/` **before** the build
ran; this build's artifacts to `releases/2026-08-15_1544_t2_3/`. Object
trees were not re-copied — `id-6b7c7e5d5f71e809`'s complete tree is already
preserved at `releases/2026-08-15_1310_t2_2/`, and the volume is at 98%.

## Gate 1 — `verify-memory-map`

Verbatim:

```
verify-memory-map: checking /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery/build/saturn/sourceboot/e2-bob-identity-id-aa57d83c898e3af1/obj/sm64-saturn-sourceboot-e2.elf
verify: D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\saturn-recovery\build\saturn\sourceboot\e2-bob-identity-id-aa57d83c898e3af1\obj\sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FAAE8
  hwram_remaining = 0x5518 bytes (required >= 0x1F00)
  lwram_end       = 0x002E89E0
  lwram_remaining = 0x17620 bytes (floor >= 0x4000)
  RESULT          = OK
```

**RESULT OK.** Margins essentially unchanged, as expected — the sort's
working memory is 128 bytes of stack, not storage:

| | T2.2 (`id-6b7c7e5d5f71e809`) | T2.3 (`id-aa57d83c898e3af1`) | Delta |
| --- | ---: | ---: | ---: |
| `___end` | `0x060FAA88` | `0x060FAAE8` | +96 |
| `hwram_remaining` | `0x5578` (21,880) | `0x5518` (21,784) | −96 |
| True slack over the `0x1F00` floor | 13,944 B | **13,848 B** | −96 |
| `lwram_remaining` | `0x17620` (95,264) | `0x17620` (95,264) | 0 |

The 96 B is the +80 B of `.text` plus alignment; no section other than
`.text` moved by a single byte. This is not material growth.

## Gate 2 — host contracts

| Suite | Result |
| --- | --- |
| `verify-vdp1-painter-chain` (the focused defect regression) | **PASS** — 3 legacy cases + 16 equivalence cases + 4 atomicity positions + fail-closed |
| `verify-audio-loop-contracts` | **OK — 24 tests** |
| `verify-pcm68k-model` | **OK — 18 tests** |
| `test_dual_sh2_work_storage_contract` | **OK — 4 tests** |
| `test_vdp1_staging_relocation` | **OK** |
| `verify-terrain-depth-bins` | **PASS** |
| `verify-vdp1-frame-bank` | **OK — 4 tests** |
| `verify-demo-render-overlap` (+ `verify-render-overlap-integration`) | **PASS**, all 7 fixture mutations caught |

**Pre-existing failures, not touched and not caused here.**
`verify-dma-queue`, `verify-frame-pipeline` and `verify-render-snapshot-bank`
all fail to compile with
`fatal error: port/saturn/platform/saturn_cart_code.h: No such file or directory`
— those three recipes are missing `-I src`. Proven pre-existing by stashing
this task's three modified files and re-running `verify-dma-queue` at base
HEAD: byte-identical failure. `verify-vdp1-transfer-pipeline` is blocked
only because `verify-dma-queue` is its prerequisite. Separately,
`tools/saturn/test_render_snapshot_source.py` fails at
`test_vdp1_painter_chain_uses_all_existing_master_depth_tags` on an
assertion about `records[local].sort_key >> 16`; verified identical at base
HEAD by the same stash procedure, and already recorded as a pre-existing
failure in T2.1/T2.2. Note that `verify-all` lists `verify-frame-pipeline`,
so `verify-all` is broken at base HEAD for a reason unrelated to this task.

## Gate 3 — FPS capture vs the 1.068 baseline

Instrument: headless Ymir **build-agent2**
(`ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`,
SHA-256 `fcc88d82...3943`), BIOS
`sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`, absolute
paths, `--startup-vblanks 4096`, release manifest bound — identical
parameters to T2.2. On-target identity **MATCH** (16 bytes at
`0x22400000`) at startup attempt **681**/4096, the same attempt count as
T2.2 and R1. **No desktop launch; the owner owns the look-and-listen.**

**Run 1 (tool-complete, `--max-vblanks 3600`, default 2 presentation
events)** — `docs/saturn/evidence/reports/sprint2-t2_3-throughput.json`,
status `complete`:

| Metric | T2.2 | T2.3 |
| --- | ---: | ---: |
| Guest FPS (mean = median = 1%-low, n=1) | 2.0 | **2.069** |
| `vblank_delta` | 30 | **29** |
| Construction | 25 | **24** |
| Master finalization | 5 | **4** |
| Simulation | 5 | 5 |
| Slave work overlap | 2 | 2 |
| Dropped VBlank credits | 14 | **13** |
| Unattributed | 0 | 0 |

Queue terminal record: `master_failures 0, slave_failures 0, qf 0, qw 0`.
All 64 retained `instance.stopped` notifications have reason `frame_limit`
— **no SH-2 exceptions**.

**Run 2 (sustained, `--presentation-events 60`)** — the tool aborted in
`summarize_cadence` with "phase VBlank crossings exceed the observed
interval", the **same known tool invariant** R1 and T2.2 both hit at 60
events; as before the sustained figure comes from the run's preserved
cadence-trace diagnostics (artifact:
`releases/2026-08-15_1544_t2_3/sprint2-t2_3-throughput-sustained.json`):

| Metric | R1 baseline | T2.2 | **T2.3** | T2.3 vs T2.2 |
| --- | ---: | ---: | ---: | ---: |
| Presentations / VBlanks | 60 / 3,362 | 60 / 3,370 | **60 / 3,313** | −57 |
| **Sustained FPS** | 1.071 | 1.0682 | **1.0866** | **+1.72%** |
| VBlanks per frame | 56.0 | 56.17 | **55.22** | −0.95 |
| Construction | ~24.5 | 24.72 | **23.75** | **−0.97** |
| — of which pre-notification | — | 18.92 | **18.92** | **0.00** |
| — of which master finalization | 5.8 | 5.80 | **4.83** | **−0.97** |
| Simulation | — | 6.07 | 6.07 | 0.00 |
| Slave work overlap | — | 2.73 | 2.73 | 0.00 |
| Dropped VBlank credits | ~14 | 14.42 | **13.95** | −0.47 |

### The attribution is exact, not inferred

`sm64_saturn_render_overlap_phase_terminal()` computes
`construction = start_construction + finalization`
(`saturn_render_overlap_phase.c:95-113`) — **master finalization is a strict
sub-interval of construction**, not a sibling. Reading the raw counters:

| Counter (60-frame totals) | T2.2 | T2.3 | Delta |
| --- | ---: | ---: | ---: |
| `construction_vblank_crossings` | 1,483 | 1,425 | **−58** |
| `master_finalize_vblank_crossings` | 348 | 290 | **−58** |
| derived pre-notification window | 1,135 | **1,135** | **0** |
| `slave_work_vblank_crossings` | 164 | **164** | **0** |
| `simulation_vblank_crossings` | 370 | **370** | **0** |

Three counters are **bit-identical** across two separate builds and runs,
and the two that moved moved by exactly the same 58 crossings. That is as
clean an attribution as this rig can produce: the entire saving is inside
the master-finalization window — from slave retirement to terminal, which
is where `demo_render_finalize` and therefore the relink runs — and nothing
else in the frame changed. The determinism also rules out run-to-run noise
as the explanation for a 57-VBlank total.

**Verdict: a real, attributable, and small gain. Kept.** It does not reach
the sprint gate.

## What this does and does not prove

**Proves:**

- The per-bin rescan was real waste: 40,822 record visits per frame at the
  measured peak, worth **0.97 VBlanks/frame (1.72% of cadence)** on this
  route.
- The counting sort is a **byte-identical** replacement over
  `1 <= bin_count <= 64`, verified against the retained predecessor and an
  independent model on 16 cases up to the arena's full capacity, with three
  mutation kills.
- It costs +80 B of `.text`, zero storage, and no side buffer.
- No regression: queue clean, zero SH-2 exceptions, identity matched on
  target at the same startup attempt, every unrelated phase counter
  bit-identical.

**Does not prove — and this is the honest headline:**

- **The 24.72-VBlank construction figure was not mostly this.** T2.0's L8
  framed the rescan as "~115,200 steps vs ~1,864", which implied a large
  share of the frame. At the *measured* peak the real figure was 42,900
  visits, not 115,200 — L8's estimate used ~1,800 live commands, but T2.1
  measured the peak at **653 published** (650 drawable). The stage was
  ~21x more expensive than it needed to be, but it was only ~4% of
  construction. Removing 100% of it recovers 1.7% of cadence.
- **Where the other 23.75 VBlanks/frame go is still unmeasured.** The
  pre-notification window (dispatch → slave notification, 18.92
  VBlanks/frame, unchanged) is now the single largest attributed block in
  the frame and has never been decomposed. The residual master
  finalization is still 4.83.
- Visuals and audio are unproven here. The chain is byte-identical for
  identical input, which is a strong argument that nothing owner-visible
  changed, but the owner gate is the adjudicator. **T2.2's owner
  look-and-listen on `id-6b7c7e5d5f71e809` also remains open**, and this
  build inherits its capacity cuts.

## Next rung

The evidence points **away** from the relink and at two places:

1. **Decompose the pre-notification window (18.92 VBlanks/frame).** It is
   79.7% of construction and completely opaque. T2.0's **L14** already
   specifies the instrument: SlaveDriver's `PROFILE.C` FRT tree profiler —
   fixed tables, no allocation, **FRT reads rather than VBlank counts**, so
   a sub-VBlank breakdown is possible where the current rig quantises to
   whole VBlanks.
2. **T2.0 L12 — measure the master's spin-wait on the slave** before any
   master/slave rebalance. `slave_work_vblank_crossings` has now been
   bit-identical across three builds (164 over 60 frames, 2.73/frame),
   which is suspiciously stable and worth understanding before assuming the
   split is balanced.

**L10 is explicitly NOT the next step.** Coarsening the ordering unit to
per-BSP-leaf would attack a stage that now costs 2,078 record visits per
frame; the remaining upside there is a fraction of 1.7%.

## Reproduction

```
# build (27-variable product invocation, pool 208, diag 0) via
# tools/saturn/with-msys-toolchain.ps1 -> MSYS sh -l, source ../../.yaul.env,
# unset COMPILER_PATH, make -f Makefile.saturn.mk -j1 sourceboot <27 vars>

make -f Makefile.saturn.mk verify-vdp1-painter-chain
make -f Makefile.saturn.mk verify-memory-map
make -f Makefile.saturn.mk verify-audio-loop-contracts
make -f Makefile.saturn.mk verify-pcm68k-model
make -f Makefile.saturn.mk verify-terrain-depth-bins verify-vdp1-frame-bank
make -f Makefile.saturn.mk verify-demo-render-overlap
python tools/saturn/test_dual_sh2_work_storage_contract.py
python tools/saturn/test_vdp1_staging_relocation.py

python tools/saturn/capture_sourceboot_throughput.py \
  --ymir <abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game <abs>/build/saturn/sourceboot/e2-bob-identity-id-aa57d83c898e3af1/sm64-saturn-sourceboot-e2.cue \
  --elf  <abs>/build/saturn/sourceboot/e2-bob-identity-id-aa57d83c898e3af1/obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest <abs>/.../saturn-release-manifest-v1.json \
  --output <out>.json --startup-vblanks 4096 --max-vblanks 3600
# sustained: add --max-vblanks 4096 --presentation-events 60 (aborts in
# summarize_cadence by the known tool invariant; read
# observation_diagnostics.last_cadence_trace.record)
```

Mutation reproduction (do **not** commit): apply one of M1/M2/M3 above to
`sm64_saturn_vdp1_backend_link_depth_bins()` in
`src/port/saturn/gfx/saturn_vdp1_backend.h` and run
`verify-vdp1-painter-chain`; each must fail.
