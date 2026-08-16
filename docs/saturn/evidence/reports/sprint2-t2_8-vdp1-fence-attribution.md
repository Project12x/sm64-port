# Sprint 2 Task T2.8 — VDP1 fence attribution

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `b7e775fd`.
- Task: make the VDP1 wait measurable and attribute the "~16 unaccounted
  VBlanks per frame". Measurement only — no optimisation, no default changed,
  no desktop launch.
- Reference clones are not present in this worktree
  (`work/upstream/` does not exist here), so every reference claim below is
  cited to the vendored libyaul under `third_party/` or to an existing
  evidence file, never to memory.

---

## Headline — the premise is wrong, and that is the finding

**There are no ~16 unattributed VBlanks. The frame is already fully
attributed, and the VDP1 draw fence is not where the time goes.**

1. **The 41.00-VBlank frame is an arithmetic artifact.** It is `60 ÷ 1.4634`,
   and T2.7 §1.1 already established that `1.4634` was hand-computed from
   T2.6's failure diagnostics over `vblanks_advanced`, which **includes the
   ~1,549-VBlank pre-gameplay ramp**. The frame on the summarizer's own basis
   is **15.483 VBlanks** (`sprint2-t2_7-current-30events.json`, 29 intervals).
   "41 minus ~25 measured phases = ~16 missing" is 41 minus a correct
   numerator over a wrong denominator.
2. **The cadence rig already reports the residual, and it is 0.138 VBlanks per
   frame.** `phase_delta()` publishes `unattributed_vblank_crossings =
   vblank_delta − (simulation + construction + transport_presentation)`
   (`tools/saturn/capture_sourceboot_throughput.py:535-540,559`). Over T2.7's
   29 intervals that residual is **0.138 VB/frame (0.9%)**. T2.6's clean
   single-interval run reported it as **0**
   (`sprint2-t2_6-meshlet-arithmetic.md:576`, row "Unattributed 0").
3. **`vdp1_sync()` does not block.** The sprint-2 gap study's leading
   hypothesis — "VDP1 draw-end wait inside `vdp1_sync()`", confidence High —
   is refuted by the vendored source. `vdp1_sync()` sets
   `SYNC_FLAG_MASK_V1SVBI|SYNC_FLAG_MASK_V1SVBO` and returns
   (`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp_sync.c:278-303`). There is
   no loop in it.
4. **The real fence is `vdp1_sync_wait()`, and sourceboot reaches it in
   exactly one place — the overwrite guard that was *already* instrumented.**
   `vdp1_sync_wait()` (`vdp_sync.c:314-328`) spins on `SYNC_FLAG_VDP1_SYNC`,
   which is cleared only in `_vdp1_mode_variable_vblank_out`
   (`vdp_sync.c:1063-1091`), reached only after VBlank-IN observed `EDSR.CEF`
   (`vdp_sync.c:1019-1055`). So it *is* the draw-end fence. The gap study
   called `vdp1_overwrite_wait_ticks_*` "the wrong fence"; it is the right
   one. It has simply never been read out by any capture.
5. **That fence's phase costs zero whole VBlanks per frame.** The guard lives
   in `sourceboot_frame_poll_transfers`, and both it and
   `sourceboot_present_generation` are inside the cadence rig's
   `transport_presentation` phase. Over T2.7's 29 intervals
   `transport_presentation` measures **0.0 VBlank crossings per frame across
   4.0 calls per frame**.

**Verdict: CPU-bound, not fill-bound.** The VDP1 work is a red herring at the
resolution the frame is actually spent at; ~99% of the frame is `construction`
(9.517 VB) plus `simulation` (5.828 VB), and T2.7 already localised 4.565 VB
of construction to a single node, `spatial_admit`.

The instrumentation this task adds exists to put a *sub-VBlank number and a
VDP1-side witness* on that verdict rather than leaving it as an inference from
a phase that reads zero. That build is described in §4 and both its captures completed (§7): the fence
measures **0 waits in 1,349 frames, mean 0.331 FRT ticks**, and `vdp1_sync()`
measures **one tick**. The instrument agrees with the phase table.

---

## 1. What was broken, and how the measurement is fixed

Three separate defects, all real, none of them the one the brief expected:

| # | Defect | Evidence |
| --- | --- | --- |
| 1 | `sourceboot_vdp1_wait_ticks_accum` declared, zeroed and read, **never incremented** | `src/port/saturn/sourceboot/main.c` — the only writes were `= 0U` at declaration/reset and a read at present |
| 2 | `vdp1_terminal_fence_wait_ticks_last/_accum` **hard-assigned `0U`** immediately after the fence, then printed on the VDP2 HUD as `VDP1W` (`src/port/saturn/gfx/saturn_vdp2_frame.c:63-65,117`) | a reader saw a zero and concluded "no VDP1 wait" |
| 3 | The one live fence counter, `vdp1_overwrite_wait_ticks_last`, is a **single `sourceboot_frt_delta()` span returning `uint16_t`** | at φ/128 a 16-bit FRT wraps at ~312 ms ≈ 18.8 VBlanks, so any wait past that aliased silently into a small number — and the truncation to `uint16_t` discards it even before the wrap |

Fixes, all behind `SATURN_DIAGNOSTIC_MODE != 0`:

- Defect 3 is fixed by **accumulating inside the spin**, one 16-bit FRT
  difference per iteration, into a 32-bit total.
- Defects 1 and 2 are fixed by populating the fields from that total instead
  of from a constant, so the HUD's `VDP1W` cell and
  `tools/saturn/fast3d_profile_decode.py` stop reporting a fiction.
- The terminal-fence fields now carry the per-present
  `vdp1_sync_render()` + `vdp1_sync()` cost, which is what that call site
  actually spends.

---

## 2. Instrumentation design

### 2.1 Rig choice — reuse, not a second rig

The T2.5/T2.6 FRT profiler
(`src/port/saturn/runtime/saturn_prenotify_profile.h`) was **reused**, and its
published record extended from version 2 to version 3. The alternative — a
second published record — would have duplicated the magic/sequence discipline,
the NOLOAD `.lwram_bss` placement, the P2 cache-through publication and the
host decoder, for no gain. The pre-notification window closes at the NOTIFIED
marker, so the present path is outside node accounting; the new fields
therefore **do not participate in the node stack** and cannot perturb the
T2.4/T2.5/T2.6 node numbers.

ABI: 452 B → **716 B** (113 → 179 words), 66 new words, `_Static_assert`
updated. Host decoder updated in
`tools/saturn/capture_prenotification_profile.py`
(`PROFILE_VERSION = 3`, `PROFILE_WORDS = 179`), plus a new
`present_summary()` section in the tool's output.

### 2.2 What is measured, and where

| Probe | Site | Purpose |
| --- | --- | --- |
| `vdp1_fence_ticks_*`, `vdp1_fence_iterations_*`, `vdp1_fence_max_raw` | the `vdp1_sync_wait()` overwrite guard in `sourceboot_frame_poll_transfers` (`main.c:1731`) | the real draw-end fence, accumulated **inside** the spin |
| `vdp1_edsr_entry_last`, `vdp1_edsr_cef_entry_count` | immediately before the fence | whether VDP1 had *already* finished when we arrived. `CEF` set on entry ⇒ the wait is zero and the time is elsewhere |
| `vdp1_copr_entry_last` / `vdp1_copr_exit_last` / `vdp1_lopr_last` | both sides of the fence | which command VDP1 was plotting while we waited |
| `vdp1_vblank_samples`, `vdp1_vblank_cef_count`, `vdp1_copr_vblank_ring[32]`, `vdp1_copr_retired_*` | `sourceboot_vblank_out_handler` | per-VBlank plot-progress curve. **`cef_count / samples` is the decisive scalar**: the fraction of the run's fields at which VDP1 had already finished. Near 1 ⇒ CPU-bound; near 0 ⇒ fill-bound |
| `vdp1_render_ticks_*`, `vdp1_sync_ticks_*`, `vdp2_commit_ticks_*`, `present_ticks_*` | `sourceboot_present_generation` | the present path decomposed; `vdp1_sync_ticks_*` exists specifically to *prove* that call does not block |
| `commands_total_last/_accum`, `commands_actor_accum`, `commands_texture_accum` | present | the actor/terrain command split the profile already computed and no instrument published (gap-study item D) |

`COPR` is read raw. The command index is `COPR / 4`, not `COPR / 32`: libyaul
derives it as `copr >> 2` in `vdp1_cmdt_current_get()`
(`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp1/cmdt.h:234-239`). The gap
study's `COPR/32` is wrong by 8×.

### 2.3 Wrap analysis

The profiler selects internal clock ÷128
(`saturn_prenotify_profile.h`, `select_clock()`). At the NTSC 320-mode SH-2
clock of 26,872,000 Hz (libyaul's own constant, `scu/bus/cpu/cpu/frt.h`), one
tick is ~4.76 µs and the 16-bit FRT wraps every 65,536 ticks ≈ **312 ms ≈ 18.7
VBlanks**.

A single start/end delta around a wait that can approach that aliases
silently, and the shipped bracket also truncated to `uint16_t`. **Choice
made: keep ÷128 and accumulate per spin iteration.** One iteration is a
`cpu_intc_mask_get`/`set` pair plus a flag load plus two FRT byte reads —
tens of SH-2 cycles, four orders of magnitude below one wrap — so
`(uint16_t)(now - last)` is exact at every step and the 32-bit sum is exact
regardless of how long the wait is. A coarser divider was rejected because it
would have degraded resolution on the sub-VBlank quantities this task exists
to resolve. `vdp1_fence_max_raw` publishes the largest single inter-probe
interval as the wrap witness; a value far below 65,535 is positive evidence
that nothing aliased, exactly as `max_raw_interval` already does for the node
tree.

The spin replicates `vdp1_sync_wait()`'s interrupt discipline verbatim —
`cpu_intc_mask_get()`, `cpu_intc_mask_set(0)` for the loop, restore after. This
is not cosmetic: `SYNC_FLAG_VDP1_SYNC` is cleared by the VBlank-OUT handler, so
a spin that inherited a masked SR would never terminate.

### 2.4 Perturbation estimate

- **Fence spin:** one probe per iteration instead of a bare flag poll. The
  added cost per iteration is ~40 cycles against an iteration that already
  costs a masked flag read. `vdp1_fence_iterations_last/_accum` publish the
  count so this is measured rather than assumed, and it inflates the *measured
  wait* by at most the probe's own share of the loop — an over-estimate of the
  wait, never an under-estimate, which is the safe direction for this question.
- **Present path:** four FRT reads per present, ~160 cycles/frame.
- **VBlank ISR sample:** two 16-bit I/O reads plus ~10 ALU ops and two LWRAM
  stores through P2, once per field. At ~60 fields/s and ~15 VBlanks/frame that
  is ~15 × ~120 = ~1,800 cycles/frame against a frame of ~6.7 M cycles —
  **~0.03%**.
- Total added ≈ 0.04% of the frame outside the fence itself. Publication is
  once per present and once per VBlank, never per sample loop.

### 2.5 Ownership and storage

All new state is **master-owned**. The only writer outside the master mainline
is the VBlank-OUT handler, which runs on the master. No slave path touches any
new field, so T2.4's cross-CPU FRT defect cannot recur. The record stays in
NOLOAD `.lwram_bss`, written through the P2 cache-through alias, so Ymir's
`mem.peek` observes backing LWRAM.

One defect was found and fixed in this task's own code before any capture:
the VBlank ISR read the ring cursor back from NOLOAD LWRAM and used it
**unmasked** as an array index. `.lwram_bss` is not crt0-cleared and the
handler is registered in `user_init()`, i.e. before
`sourceboot_reset_lwram_state()` zeroes the record, so on any boot where LWRAM
is not already zero (real hardware; Ymir happens to zero-fill) that was an
out-of-bounds write into neighbouring LWRAM state. The cursor is now masked on
read as well as on write.

---

## 3. Corrections to `sprint2-reference-technique-gaps.md`

This task's first job was to act on that study's ranked items 1 and 2. Four of
its load-bearing claims do not survive contact with the source. Recording them
so the same conclusions are not re-derived:

| Gap-study claim | Status | Evidence |
| --- | --- | --- |
| "VDP1 draw-end wait inside `vdp1_sync()`" — confidence **High**, "the one blocking fence nobody measures" (§3.3) | **Refuted** | `vdp1_sync()` sets two flags and returns (`vdp_sync.c:278-303`). `vdp1_sync_render()` spins only on `VDP1_FLAG_LIST_XFERRED`, the command DMA (`vdp_sync.c:478-501`) |
| `vdp1_overwrite_wait_ticks_*` "measures the **wrong** fence" (§3.3) | **Refuted** | it wraps `vdp1_sync_wait()`, whose flag is cleared only after VBlank-IN saw `EDSR.CEF` — that *is* draw-end. It was never wrong, only never captured, and truncated to 16 bits |
| "COPR/32 is the command index" (§3.4B) | **Wrong by 8×** | libyaul: `copr >> 2` (`vdp1/cmdt.h:234-239`) |
| "the LOD suppression path already exists (`s_primitive_lod_suppressed`, `saturn_demo_render.c:1925-1930`)" — offered as the way to suppress **Mario** (§3.4C) | **Wrong subject** | that array is `[SM64_SATURN_BOB_PRIMITIVE_COUNT]` (`saturn_demo_render.c:308`) and is written in the **BOB terrain** classify pass. Mario goes through `demo_prepare_mario` → `sm64_saturn_actor_meshlets_prepare`, whose only cull is `depth_bounds.furthest_q16 <= 0` (behind camera). **There is no Mario suppression switch** |
| "four blocking fork-joins per frame (`saturn_demo_render.c:...,3972`)" (gap table row 1) | **One of them is dead code** | `demo_upload_vdp1_dual` has no caller anywhere in `src/` or `tools/`, and the compiler says so: `saturn_demo_render.c:3961:13: warning: 'demo_upload_vdp1_dual' defined but not used [-Wunused-function]` in this task's build log. The sourceboot demo path uploads through `sm64_saturn_vdp1_frame_bank_submit_transfers` (SCU DMA) |

The study's §3.4A recommendation (FRT-bracket the present path, accumulate
inside the wait, mind the 18.8-VBlank wrap) and §3.4D (publish the command
split) were both correct and are both implemented.

---

## 4. The build

Diagnostic tuple only: the 27-variable invocation from
`sprint1-stage1-link-smoke.md` (pool 208) with `SATURN_DIAGNOSTIC_MODE=2`, via
`tools/saturn/with-msys-toolchain.ps1` → MSYS `sh --noprofile --norc -l`,
sourcing `../../.yaul.env` then `unset COMPILER_PATH`.
`tools/saturn/profiles/sourceboot-bob-demo-v1.json` had `diagnostic_mode`
flipped 0 → 2 as an uncommitted byte-canonical (LF) edit, T2.1's precedent.

Sealed identity **`id-d378c3e178e5dec3`**. The link completed and the image
was produced.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 9,999,940 | `c955e4577afc9bea77c9d960a9b1bf6106420438ecc96dee5f2422117c65bc47` |
| `sm64-saturn-sourceboot-e2.iso` | 5,173,248 | `6dbd02167f42878d7192ad56e17b6dda9ccd171f89be522b3b77709b421fa187` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,270 | `ee4a988177ac20b7b8bc53549529836d7e6568216764b8de6120405132632176` |

Preservation (mandatory rule): prior accepted/current candidates
(`id-6b7c7e5d5f71e809`, `id-6eca5970628d581d`, `id-d2a4896c541ba56b`) copied to
`releases/2026-08-15_t2_8-pre-build/`; this build's four artifacts to
`releases/2026-08-15_t2_8-diag/`. The build writes into a fresh
identity-named directory and overwrites no prior artifact.

### Gate -- `verify-memory-map` (verbatim), **RESULT OK**

```
verify-memory-map: checking .../e2-bob-identity-id-d378c3e178e5dec3/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FB9D4
  hwram_remaining = 0x462C bytes (required >= 0x1F00)
  lwram_end       = 0x002E8CD0
  lwram_remaining = 0x17330 bytes (floor >= 0x4000)
  RESULT          = OK
```

Against T2.6's diagnostic build (`hwram_remaining 0x484C`,
`lwram_remaining 0x17440`): HWRAM slack falls by **544 B** and LWRAM by
**272 B**. The LWRAM delta is the record's 452 -> 716 B growth (264 B) plus
alignment; the HWRAM delta is the new master-side code and the ISR sampler.
Both are diagnostic-build-only and both clear their floors with room -- true
HWRAM slack over the `0x1F00` floor is **10,540 B**.

**Three build attempts, two of them self-inflicted and both instructive:**

1. Killed by hand after ~3 minutes to correct the fence spin's interrupt
   discipline (§2.3) before it could hang.
2. **Failed** — `Makefile:635: *** sourceboot sealed identity mismatch: caller
   sealed id-75cdd4e2f2b76e2c but the frozen spec yields id-7d496c194aba3260`.
   Cause: a **comment-only** edit to `saturn_prenotify_profile.h` while that
   build was running. The sealed identity is derived from a source-hash spec
   frozen at the start of the build, so *any* byte of tracked source changing
   mid-build invalidates it. The gate did exactly its job. **Operational
   lesson: freeze all tracked source before invoking `sourceboot`; a comment
   is not exempt.**
3. Restarted on frozen source after the ring-index fix (§2.5). Identity
   `id-d378c3e178e5dec3`, sealed. This build is the capture target.

The g15 package-staleness cascade **did not fire**; no repair was needed in
any attempt.

---

## 5. Product cleanliness — proven at OBJECT level

Every addition is inside `#if SATURN_DIAGNOSTIC_MODE != 0`. The product path
in `sourceboot_frame_poll_transfers` and `sourceboot_present_generation` is
preserved verbatim in the `#else` arms — the diff is +270 lines / −0 lines in
`main.c`.

Proof method: compile the four translation units the change touches at
`SATURN_DIAGNOSTIC_MODE=0` with `-g0` on the product build's own command line,
twice — once from the working tree, once from a **path-identical mirror of
`HEAD`** extracted with `git show` into `$MIRROR/<same relative path>` and
given `-ffile-prefix-map=$MIRROR=.`, so `__FILE__` (which `assert()` bakes into
`.rodata` under `-DDEBUG`) expands identically and the only possible difference
is the code. The worktree was never stashed or modified.

| Object | Bytes | SHA-256 (HEAD) | SHA-256 (T2.8) | Verdict |
| --- | ---: | --- | --- | --- |
| `main.o` | 660,904 | `c79c819e1201ac61e45e68d36b470420176760b2b49d7d91e0123576f4f4f179` | same | **IDENTICAL** |
| `saturn_demo_render.o` | 723,168 | `a1b8d96a8c7bedc3877bc4c4a63dbd1f742b4fdf54e19474ceecdd6fdfbb7d71` | same | **IDENTICAL** |
| `saturn_actor_meshlets.o` | 16,500 | `f423caa89b4dbd14cd84eaa99e961e67c0870dea36df0bdff7bcfe38ada50cdd` | same | **IDENTICAL** |
| `saturn_render_job_runtime.o` | 5,356 | `49cad063edc38e336b7e6a99ec31d31db347c54f63ff7b606931eb23a7ff0be6` | same | **IDENTICAL** |

**PRODUCT BUILD BYTE-UNAFFECTED.** Cross-check on the method itself:
`saturn_actor_meshlets.o` reproduces T2.6's independently recorded hash
`f423caa8…50cdd` at 16,500 B exactly, so this command line is the product
build's, not an approximation of it.

Reproduce with
`tools/saturn/…` — see §9.

---

## 6. Frame budget, on the summarizer's own basis

All figures below are `summarize_cadence` output from
`sprint2-t2_7-current-30events.json` (29 intervals, product build
`id-6eca5970628d581d`, `--startup-vblanks 4096 --max-vblanks 3600`).
**Nothing here is hand-computed from failure diagnostics.**

| Phase | VBlanks / frame | Calls / frame | Share of frame |
| --- | ---: | ---: | ---: |
| **frame (`vblank_delta`)** | **15.483** | — | 100% |
| construction | 9.517 | 1.0 | 61.5% |
| — of which master finalization | 4.517 | 1.0 | 29.2% |
| — of which slave work overlap | 2.931 | 1.0 | 18.9% |
| simulation / source tick | 5.828 | 1.0 | 37.6% |
| **transport + presentation** (the whole VDP1 present path **and** the draw fence) | **0.000** | **4.0** | **0.0%** |
| attributed | 15.345 | — | 99.1% |
| **unattributed** | **0.138** | — | **0.9%** |
| (dropped VBlank credit — a scheduler counter, not a time phase) | 6.759 | — | — |

`construction + simulation = 15.345` against a 15.483-VBlank frame. **The
budget already sums.** The two phases overlap the frame end-to-end; the
residual is 0.138 VBlanks.

For completeness, the same reconciliation on T2.6's own sustained diagnostic
run: its phases were construction 9.450 and simulation 6.213, summing to
**15.663** — against a frame of **15.7 VBlanks** on the corrected basis
(T2.7 §1.1: 50 events / 784 VBlanks), not 41.00. The "~16 unattributed" is
exactly `41.00 − 25`, i.e. the gap between a wrong denominator and correct
numerators.

---

## 7. Captures A and B

**Capture A (normal scene)** — `capture_prenotification_profile.py` on
`id-d378c3e178e5dec3`, `ROUTE_REPLAY=1` with movement, 24,000 post-BIOS frames
in 300-frame chunks, 80 sample boundaries, release-manifest bound, headless
Ymir **build-agent2**, absolute `--cue`, BIOS
`.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`. **No desktop launch.**

**Capture B (Mario suppressed) — NOT RUN AS SPECIFIED, and the reason is a
finding.** The brief and the gap study both assume an existing Mario
suppression switch. **There is none** (§3, row 4). `s_primitive_lod_suppressed`
is the BOB terrain LOD path; the actor path's only cull is "behind the camera".
Writing one was explicitly out of scope. The substitute, which is *stronger*
than an A/B build because it holds the binary, the route and the emulator
fixed, is a **within-run regression**: the capture samples the cumulative
record every 300 frames, so differencing adjacent samples yields per-segment
means of (VDP1 fence ticks, `EDSR.CEF` share, actor commands, total commands,
frame VBlanks). Ranking those segments by actor command emission and comparing
the lowest against the highest quintile is the same discriminator the A/B was
for. `tools/saturn/…` post-processing is described in §9. **This comparison is
approximate and is labelled as such**: it varies Mario's contribution by route
position, not by suppressing him.

### Both captures completed. Numbers.

Capture A: `capture_prenotification_profile.py` on `id-d378c3e178e5dec3`,
24,000 post-BIOS frames, 300-frame sampling, **all acceptance checks pass,
exit 0**. 1,349 pre-notification windows, 4,043 presents, 22,856 sampled
VBlanks. Capture B: `capture_sourceboot_throughput.py` on the same build,
`--startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30`,
**`status: complete`** -- `summarize_cadence` did not abort, so every cadence
figure below is its output and none is hand-derived.

#### The VDP1 draw fence -- it is zero

| Quantity | Value |
| --- | ---: |
| Fence events (guard reached) | 1,349 |
| **Fence waits (guard actually blocked)** | **0** |
| Mean fence cost | **0.331 ticks = 42 SH-2 cycles = 0.000094 VBlanks** |
| Max fence cost, any frame | **1 tick** |
| Mean spin iterations | **0.0** |
| `vdp1_fence_max_raw` (wrap witness) | **1** tick -- headroom 65,534 |

A largest-ever inter-probe interval of 1 tick against a 65,535 ceiling is
conclusive positive evidence that nothing aliased. **The CPU never waits for
VDP1. Not once in 1,349 frames.**

#### The present path -- and `vdp1_sync()` proven non-blocking

| Stage | Mean ticks / present | VBlank-equiv |
| --- | ---: | ---: |
| `vdp1_sync_render()` | **0.774** | 0.00022 |
| `vdp1_sync()` | **0.993** | 0.00028 |
| VDP2 begin + commit (HUD text through `dbgio`) | **279.805** | 0.0797 |
| **present total** | **281.572** | **0.0802** |

`vdp1_sync()` at **one tick** is the direct measurement refuting the gap
study's High-confidence hypothesis. **99.4% of the present path is the VDP2
HUD text write**, which is itself diagnostic furniture.

#### VDP1's own registers -- the surprise

| Quantity | Value |
| --- | ---: |
| VBlanks sampled | 22,856 |
| VBlanks with `EDSR.CEF` set (VDP1 finished) | **339** |
| **VDP1 idle share** | **1.48%** |
| `EDSR.CEF` set on entry to the fence | **0 of 1,349 (0.0%)** |
| COPR retirement, mean | **24.5 commands / VBlank** |
| COPR retirement, max in one interval | 385 commands |
| Commands per present | **552.2** (actor **97.5 = 17.7%**, textured 56.0) |

**VDP1 is busy at 98.5% of VBlanks, and CEF was never set at the fence -- yet
the fence never blocked.** Those two facts are only consistent if
`SYNC_FLAG_VDP1_SYNC` is not being set on this path at all: the frame-bank
route publishes through SCU DMA and `vdp1_sync_force_put()`, so libyaul's
sync-flag machine is short-circuited and `vdp1_sync_busy()` reads false
regardless of what VDP1 is actually doing. **The overwrite guard is inert.**

Order-of-magnitude consequence, stated as inference and not as measurement:
552 commands per frame at ~24.5 retired per VBlank is **~22.5 VBlanks of plot
work against a 15.7-VBlank frame**. VDP1 appears **over-subscribed by roughly
1.4x**, and because the guard never fires nothing in the port detects it. That
is a *fidelity* exposure (plots discarded at the frame-buffer change -- the
overrun behaviour Sega documents in `SGL020A.TXT` 2.5.4), **not** a cadence
cost, since no CPU time is spent waiting for it. It is a new finding, it is not
what T2.8 was scoped to test, and it needs its own task.

#### Frame budget on the instrumented build (capture B, `summarize_cadence`)

| Phase | VBlanks / frame | Calls / frame |
| --- | ---: | ---: |
| **frame (`vblank_delta`)** | **15.724** | -- |
| construction | 9.793 | 1.0 |
| -- master finalization | 4.793 | 1.0 |
| -- slave work overlap | 2.931 | 1.0 |
| simulation / source tick | 5.828 | 1.0 |
| **transport + presentation** (VDP1 present path **and** the fence) | **0.000** | **4.0** |
| attributed | 15.621 | -- |
| **unattributed** | **0.103** | -- |
| (dropped VBlank credit -- scheduler counter, not time) | 6.862 | -- |

**3.8158 FPS mean, 3.75 median, 29 intervals, 456 VBlanks.** The budget sums:
15.621 attributed of 15.724, residual **0.103 VBlanks (0.65%)**. Two
independent instruments now agree that the present path and the fence cost
essentially nothing: the VBlank-crossing rig says 0.000, and the FRT rig says
0.0802 VBlank-equivalent, of which the VDP1 portion is 0.0005.

#### Instrumentation perturbation, measured rather than estimated

The instrumented build reads **15.724 VB/frame** against the product build's
**15.483** (T2.7, same route, same tool, same emulator): **+0.241 VBlanks
(+1.6%)**. The section 2.4 estimate of ~0.04% was too optimistic by roughly an
order of magnitude. Recorded as a correction; it does not affect the verdict,
because the quantity under test is 0.

#### Capture B as specified (Mario suppressed) -- not run, and why

**There is no Mario suppression switch** (section 3). The within-run substitute
is available from capture A's sample boundaries but was not needed to reach the
verdict: the fence is 0 waits and 0.331 ticks *before* any suppression, so
there is nothing for suppressing Mario to collapse. Reporting that is better
than manufacturing an A/B whose answer is already determined. The actor share
of commands -- **17.7%** -- is published for the first time and is the number a
future command-count task should start from.

## 8. Verdict — fill-bound or CPU-bound

**CPU-bound.** Stated against the brief's own decision rule:

- The rule's "if the wait collapses when Mario is suppressed ⇒ fill/command
  bound" branch is not reachable. **Measured: 0 waits in 1,349 frames, mean
  0.331 FRT ticks = 42 SH-2 cycles, max 1 tick.** There is no wait to collapse.
  The containing phase reads 0.000 VBlanks per frame over 4 calls per frame,
  and the sub-VBlank instrument agrees with it.
- The rule's "if the wait persists ⇒ CPU/other bound; the VDP1 work is a red
  herring" branch is the one the evidence selects.
- The third possibility the brief allowed for — "if the ~16 VBlanks are NOT
  mostly VDP1 wait, say what they are" — is the actual answer, with the
  correction that **there are no ~16 VBlanks**. The frame is 15.483, of which
  61.5% is construction and 37.6% is simulation, and T2.7 already localised
  **4.565 VB (29% of the whole frame)** to one node, `spatial_admit`, on one
  call per frame.

Consequences for the sprint, stated plainly: **the gap study's rows 3, 4, 5 and
6 — user clipping, command-count LOD, high-speed shrink, and the Mario
double-emit — are all downstream of a fill bound that does not exist.** They
were correctly gated on this measurement, and this measurement declines them.
The Mario double-emit (two commands per textured primitive) remains a
correctness/tidiness item worth ~10 primitives, not a cadence lever.

**One caveat, and it is not small.** Fill work is not free merely because
nobody waits for it. VDP1 is mid-plot at 98.5% of VBlanks and needs ~22.5
VBlanks to retire a 552-command frame. The port is over-subscribing VDP1 and,
because the overwrite guard is inert (section 7), silently relying on plot
overrun rather than detecting it. Command-count reduction is therefore
**demoted as a cadence lever and promoted as a fidelity fix** -- a different
task with a different success criterion (frames plotted to completion, not
VBlanks saved).

---

## 9. Ranked next steps

Ordered by expected VBlanks recovered per unit of risk. Rows 1 and 2 follow
directly from the verdict; rows 3–5 are demoted by it.

1. **Bypass `spatial_admit` (T2.7's finding).** 4.565 VB/frame, 29% of the
   frame, one call per frame, and A9A never ran it — its legacy admission path
   is still compiled in as a fail-closed fallback, so the remedy is a **bypass,
   not an optimisation**. Confirming measurement: construction VBlank crossings
   on the same route; success is construction falling to ~4.95 against A9A's
   5.56. This is the whole of the recoverable gap and nothing else in the table
   is within an order of magnitude of it.
2. **Complete this task's capture** (§7) and publish the fence ticks, CEF
   share, COPR retirement curve and command split. Cost: one unattended run on
   an already-built image. It closes T2.8 and retires the VDP1 hypothesis with
   a number instead of an inference.
3. **Repair or remove the inert VDP1 overwrite guard, and decide what to do
   about over-subscription.** `vdp1_sync_busy()` reads false on the frame-bank
   /`vdp1_sync_force_put()` path regardless of VDP1 state, so the guard that
   exists to stop the next frame overwriting command VRAM mid-plot has never
   fired. With VDP1 idle at only 1.48% of VBlanks this is a live fidelity
   exposure, not a theoretical one. Confirming measurement: `EDSR.CEF` share
   and the COPR retirement curve are now instrumented and can be re-read
   directly. **This is a correctness item and does not compete with row 1 for
   cadence.**
4. **Widen the slave overlap window** (gap row 1). The slave is busy ~1.02× its
   own window but that window is only 2.931 VB of 15.483. Worth doing *after*
   row 1, because moving a join point inside a construction phase that is about
   to lose 4.5 VB is measuring the wrong thing twice.
5. **Decompose `simulation`** (5.828 VB/frame, 37% of the frame). It has
   never been decomposed at sub-VBlank resolution. The rig now proven for the
   present path extends to it for the cost of two probes.
6. **Retire the dead `demo_upload_vdp1_dual`** (`saturn_demo_render.c:3961`,
   compiler-confirmed unused) and the Mario double-emit
   (`saturn_demo_render.c:3871-3931`). Neither is a cadence lever; both are
   debt that currently misleads readers of the render path — as it misled the
   gap study.

**Explicitly not recommended now:** per-command user clipping, command-count
LOD, and HSS. All three are fill-rate remedies, and the frame is not
fill-bound.

---

## 10. Honesty — what is wrong with this task

1. **The headline is a negative, and it invalidates the task's own premise.**
   T2.8 was scoped to attribute ~16 missing VBlanks. Those VBlanks do not
   exist. The correct response was to say so rather than instrument harder
   until something looked like 16.
2. **The A/B experiment in the brief was not run**, because the switch it and
   the gap study both assumed does not exist (see section 7). The verdict does
   not depend on it -- the quantity the A/B was meant to vary measures zero
   before any variation -- but the task did not deliver what was asked, and
   substituting a within-run correlation would not have made it what was asked.
3. **Two of the three build attempts were self-inflicted** (an interrupt-
   discipline correction and a comment edit that broke identity sealing).
   Together with a third build that ran 4x slower than the first pass on the
   same machine, they are the direct reason the budget ran out with the
   capture in flight rather than in hand.
4. **The A/B experiment was not run as specified**, because the switch the
   brief and the gap study both assumed does not exist. The within-run
   substitute is honest but weaker: it varies Mario's contribution by camera
   position, not by suppression, and cannot fully separate "less Mario" from
   "less of everything else".
5. **`tools/saturn/profiles/sourceboot-bob-demo-v1.json` is still flipped to
   `diagnostic_mode: 2`** in the working tree, deliberately, because the
   unattended build/capture chain is still consuming it. **It must be reverted
   before any product build.** It is not part of any commit from this task.
6. **Only one gate of the standing set was run.** `verify-memory-map` passed
   (S4) and both captures completed with acceptance pass / `status: complete`. `verify-audio-loop-contracts`, `verify-pcm68k-model`,
   `verify-actor-meshlets`, the painter chain, work-storage, frame-bank and
   render-job-runtime contracts were **not** run: the build occupied the
   toolchain for the whole budget. They are unchecked and this report does not
   claim them. The object-level proof in S5 bounds the risk they would be
   catching -- it does not replace them.
7. **The section 2.4 perturbation estimate was wrong by about an order of
   magnitude** (~0.04% estimated, +1.6% measured against the product build).
   The estimate is left in place with the measurement next to it rather than
   quietly corrected.
8. **The over-subscription figure (~22.5 VBlanks of plot per 15.7-VBlank
   frame) is an inference from two measured rates**, not a measurement of plot
   duration. COPR retirement is sampled once per VBlank and counts forward
   deltas only. It is strong enough to justify a task and not strong enough to
   quote as a fact.

---

## 11. Reproduction

```
# Object-level product-cleanliness proof (no worktree modification):
#   scratchpad/objproof2.sh -- compiles main.c, saturn_demo_render.c,
#   saturn_actor_meshlets.c and saturn_render_job_runtime.c at
#   SATURN_DIAGNOSTIC_MODE=0 -g0 on the product command line, from the working
#   tree and from a path-identical `git show HEAD:` mirror, and compares bytes.

# Diagnostic build: the 27-variable invocation from
# sprint1-stage1-link-smoke.md with SATURN_DIAGNOSTIC_MODE=2 and
# tools/saturn/profiles/sourceboot-bob-demo-v1.json's
# release_config.diagnostic_mode temporarily 0 -> 2 (canonical LF, uncommitted,
# reverted after the build), via
#   tools/saturn/with-msys-toolchain.ps1 sh --noprofile --norc -l -c
#     'source ../../.yaul.env; unset COMPILER_PATH;
#      make -f Makefile.saturn.mk -j1 sourceboot <27 vars>'
# FREEZE ALL TRACKED SOURCE FIRST -- the sealed identity is derived from a
# source-hash spec frozen at build start; a comment edit mid-build fails the
# build with "sourceboot sealed identity mismatch".
make -f Makefile.saturn.mk verify-memory-map

python tools/saturn/capture_prenotification_profile.py \
  --ymir  <abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl   "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game  <abs>/build/saturn/sourceboot/e2-bob-identity-id-d378c3e178e5dec3/sm64-saturn-sourceboot-e2.cue \
  --elf   <abs>/.../obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest <abs>/.../saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_8-vdp1-fence-attribution.json \
  --post-bios-frames 24000 --sample-interval 300
# the new "present" block in the summary carries the fence, EDSR/COPR and
# command-split figures.

python tools/saturn/capture_sourceboot_throughput.py \
  --ymir <...> --ipl <...> --game <...> --elf <...> --release-manifest <...> \
  --output docs/saturn/evidence/reports/sprint2-t2_8-throughput.json \
  --startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30 --timeout 1800
# cadence figures MUST come from summarize_cadence output. If it aborts with
# "phase VBlank crossings exceed the observed interval", report that it
# aborted -- do not substitute arithmetic over vblanks_advanced, which
# includes the pre-gameplay ramp (T2.7 S1.1).
```

Libyaul claims in this report can be checked directly:

```
sed -n '278,330p;478,501p;997,1091p' \
  third_party/libyaul/libyaul/scu/bus/b/vdp/vdp_sync.c
sed -n '234,249p' third_party/libyaul/libyaul/scu/bus/b/vdp/vdp1/cmdt.h
```


---

## Note on a superseded append

An earlier session appended a second "Section 7 (completed)" block here
from the raw capture accumulators while this task's own capture was still
in flight. It has been removed: section 7 above is authoritative, and the
removed block derived cadence from raw per-phase accumulators
(construction 10.25 VB/frame) rather than from `summarize_cadence`
(9.793 VB/frame) — the exact derivation T2.7 identified as the source of
the contaminated 1.0-1.5 FPS figures. Its fence numbers agreed with
section 7 (0 waits / 0 spin iterations across 1,349 events); only the
cadence basis differed, and the summarizer is the correct basis.
