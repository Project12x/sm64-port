# Sprint 2 Task T2.1 — Instrumented peak capture and shrink verdicts

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `9a472e8d` (T2.0 evidence).
- Plan: `docs/superpowers/plans/2026-08-15-sprint2-cadence-recovery.md`,
  Task T2.1. Candidates and gates from
  `docs/saturn/evidence/reports/sprint2-t1-hwram-attribution.md`.
- Question answered: over a full scripted-route run, what are the observed
  peaks for (1) per-frame VDP1 `command_count`, (2) the libyaul TLSF
  `_private_pool` high-water, (3) `gGfxPool` master display-list usage, and
  (4) the SMPC peripheral pool — and is each proposed capacity shrink
  SAFE / UNSAFE / NEEDS-MARGIN against them?

## Verdict table (headline)

| # | Candidate | Proposed new capacity | Observed peak (this route) | Margin | Verdict |
| --- | --- | --- | --- | --- | --- |
| 1 | `SOURCEBOOT_VDP1_COMMAND_CAPACITY` 2048→1664 | 1,664 cmdts/bank | **653** cmdts published (incl. 3 setup) | 1,011 (61% headroom) | **SAFE** on this route |
| 2 | libyaul `_private_pool` 0xA000→0x4000 | 16,384 B pool | historical extent **8,276 B** from pool base (durable payload 4,980 B, 6 boot-time blocks) | 8,108 B | **SAFE** on this route |
| 3 | `GFX_POOL_SIZE` 6400→4096 | 4,096 Gfx entries | **443** entries | 3,653 (89% headroom) | **SAFE** on this route |
| 4 | SMPC peripheral pool 14→2 | 2 blocks | **exactly 2** blocks ever allocated (proof below) | 0 | **NEEDS-MARGIN** — 14→2 leaves zero headroom for any connect event; 14→4 (≈5,320 B) is the safe variant |

Every SAFE verdict carries the route-coverage caveat in "Honesty" below:
this capture exercises BOB entry plus ~600 ticks of scripted movement and an
idle tail — not owner free-roam, not object interactions, and not the
route's remaining 1,400 ticks (live-input bootstrap truncates the replay).

Bonus telemetry captured for other T1 rows (not gates here):

- `_gObjectPool` (T1 #8): `peak_allocated` **155** of 208, `alloc_failures`
  0 — a true accumulator (spawn-site increments), so this peak is exact.
- Gouraud staging (T1 HOLD row): published gouraud count peak **345** of
  the 1,536-entry staging capacity on this route.

## Capture identity

Diagnostic build, sealed identity **`id-ae6740c94b1a409e`**
(`build/saturn/sourceboot/e2-bob-identity-id-ae6740c94b1a409e/`), identity
v2, release manifest SHA-256
`16c8e3ac7e65f656f58a26fc6cbacac0a61f3e94a2f0bc636ab64ce36f3bb7be`.
`effective_config` = the accepted R1 tuple (level 9/BOB, pool 208,
`route_replay_mode=1`, `live_input_mode=1`, bootstrap 600, pipeline 4,
slave 1, poly tier 2) with exactly one field different:
**`diagnostic_mode=2`** (product = 0; see "Build attempts").

| Artifact | SHA-256 |
| --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `17c3af661d23bfa305bf7c0de8ab1fd60dfbf9d71cea851f8048da76c6c10c8a` |
| `sm64-saturn-sourceboot-e2.iso` | `5727884a1d91b8bab64a9ace8e0aacf53f661b3b18c0b391ea7603cdbfa9261e` |
| `sm64-saturn-sourceboot-e2.cue` | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |

The capture tool re-verifies the sealed identity bytes inside live target
RAM before sampling (`target_identity.match = true` in the JSON).

The profile flip (`tools/saturn/profiles/sourceboot-bob-demo-v1.json`
`release_config.diagnostic_mode` 0→2) is an **uncommitted, temporary edit**
per the stage-1 precedent (the identity bootstrap requires Make-provided
config == profile `release_config`); it was reverted after the capture. The
committed product profile remains `diagnostic_mode: 0`.

Pre-build preservation (mandatory rule): accepted candidates
`id-b3aceeb28570230b`, `id-782c9c8f323a01a0`, and the owner-accepted
`id-86d3880727ed1d10` copied to `releases/2026-08-15_peakcap-pre-build/`
before any build ran. Per-identity build directories mean no accepted
artifact was overwritten.

## Build attempts (constitution: two-attempt log)

1. **Attempt 1 — `SATURN_DIAGNOSTIC_MODE=1` (as tasked): LINK FAILED** on
   the linker script's HWRAM floor ASSERT ("HWRAM margin below libyaul's
   TLSF control-block floor"). Mode 1 compiles the pre-existing animation
   sweep in addition to the new probes; measured object growth vs the
   product build was +420 B `.text` +92 B `.data` (main.o +296/+92 —
   dominated by the sweep — game_init.o +36, saturn_demo_render.o +88)
   against the R1 tuple's 504 B slack above the 0x1F00 floor. Sealed (no
   ELF): `id-c47c8f23704c992b`.
2. **Attempt 2 — probes regated to `SATURN_DIAGNOSTIC_MODE != 0`
   (mirroring `saturn_demo_render.c`'s established any-nonzero guard),
   built with `SATURN_DIAGNOSTIC_MODE=2`: BUILD OK** (the animation sweep
   stays `== 1` and is compiled out; mode 2 also removes the sweep's
   Mario-animation override from the measured route — a measurement-purity
   win, since the sweep would have forced non-route poses for the first
   209 render frames). One tooling slip on the way: the first attempt-2
   invocation died pre-compile at "target profile is stale or invalid"
   because the profile edit had been written with CRLF line endings — the
   identity bootstrap requires canonical LF JSON byte-for-byte; rewritten
   canonically and relaunched. Mode-2 object growth vs product: +192 B
   `.text` total (main.o +68, game_init.o +36, saturn_demo_render.o +88,
   the last being its pre-existing diagnostic telemetry block), probe
   state 32 B in NOLOAD `.lwram_bss`.

Deviation from the tasking (`SATURN_DIAGNOSTIC_MODE=1`) is exactly the
guard change plus the knob value 1→2; all 27 build variables other than the
diagnostic knob are identical to the accepted-candidate invocation in
`docs/saturn/evidence/reports/sprint1-stage1-link-smoke.md`.

## Diagnostic-build margins (`verify-memory-map`, verbatim)

```
verify: ...\e2-bob-identity-id-ae6740c94b1a409e\obj\sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FE028
  hwram_remaining = 0x1FD8 bytes (required >= 0x1F00)
  lwram_end       = 0x002F5D60
  lwram_remaining = 0xA2A0 bytes (floor >= 0x4000)
  RESULT          = OK
```

HWRAM: 0x1FD8 (8,152 B) remaining — 216 B above the floor; the diagnostic
build consumes 288 B more HWRAM than the product ELF (`___end` 0x060FE028
vs 0x060FDF08). LWRAM: the gate tool excludes `.lwram_geo_traversal` (T1
reconciliation); geo-adjusted to-top margin = 0xA2A0 − 0xC00 = **0x96A0**
(38,560 B) — the 32 B probe took exactly the expected bite out of T1's
0x96C0. The **product** build is untouched by this task: the only
non-diagnostic-gated source change is a header `#include`, and every
definition, store, and the probe symbol sit behind
`#if SATURN_DIAGNOSTIC_MODE != 0` (grep-verified; mode-0 preprocessing of
`saturn_peak_probe.h` yields only a typedef and macros).

## What was already observable vs what was added

| Peak | Existing rail | Sufficient? | Action |
| --- | --- | --- | --- |
| VDP1 command_count max | `sourceboot_fast3d.profile.vdp1_commands_last` (live, per frame) and `profile.vdp1_command_highwater` — but `sm64_saturn_fast3d_frontend_submit()` memsets the whole profile every frame and `vdp1_command_highwater` is **not** in its preserved-field list (`saturn_fast3d_frontend.c:1520-1563`), so the "highwater" only ever holds the latest published frame | NO — host sampling of a live value misses peaks between samples | **Added** run-long accumulator `g_sm64_saturn_peak_probe.vdp1_commands_highwater`, updated in `sourceboot_frame_update_telemetry()` (`main.c`) |
| `gGfxPool` DL usage max | `create_gfx_task_structure()` computes `entries = gDisplayListHead - gGfxPool->buffer` (`game_init.c:260`) then discards it | NO — same live-value problem | **Added** `gfx_pool_entries_highwater` update at that site |
| TLSF `_private_pool` high-water | `mm_stats_walk()` exists (libyaul `mm_stats.h`) but is target-side and current-state only; **no code needed**: the pool is boot-zeroed (verified: the ELF's load image for `__private_pool` is 40,960 zero bytes) so a host-side full dump supports a stain scan plus a physical TLSF block walk (layout from `third_party/libyaul/libyaul/kernel/mm/tlsf.c`, pinned submodule `6012f79f`, same commit as `work/upstream/libyaul` HEAD) | YES (host-side) | Dump + analyze in `capture_sprint2_peaks.py`; no target change |
| SMPC pool usage | `_peripherals_memb` memb_t (`alloc_count` at +20, `next_index` at +16) and the `memb_ref` refcount array are peekable local symbols | YES (host-side) | Sampled every interval + refs read at end; no target change |

Added instrumentation (all diagnostic-gated, product byte-clean):
`src/port/saturn/runtime/saturn_peak_probe.h` (new),
`src/port/saturn/sourceboot/main.c` (definition in NOLOAD `.lwram_bss`,
explicit init in `sourceboot_reset_lwram_state()`, telemetry mirror),
`src/game/game_init.c` (entries accumulator). All stores go through the
SH-2 P2 cache-through alias (the `sourceboot_cadence_trace` pattern).
Harness: `tools/saturn/capture_sprint2_peaks.py` (pattern-copied from
`capture_object_pool_occupancy.py`: release-manifest binding,
sealed-identity wait, chunked `exec.run_for` at 300 frames, DLL-safe
`sh-elf-nm`; adds a locals-capable nm resolver for libyaul statics and a
Mario-position movement witness). Host tests:
`tools/saturn/test_capture_sprint2_peaks.py`, 18 tests; four mutations
(walk stride, memb field order, size-flag masking, movement gate) each
made the suite fail before restoration.

## Method

- Emulator: headless Ymir build-agent2
  (`ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`),
  BIOS `sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`,
  absolute `--cue`, `--dram-cart`.
- Run shape: proven USA-BIOS input handoff, wait for sealed-identity match
  in target RAM, then 24,000 post-BIOS frames in 300-frame
  `exec.run_for` chunks with a full sample at every boundary (80 samples);
  private-pool dumps at halfway and end. 26,183 emulated frames total,
  ~291 s wall. Movement phases covered: the cadence trace reached 770
  simulation ticks — the whole 600-tick scripted-replay window plus a
  ~170-tick idle tail — satisfying the two-emulated-minutes floor
  (24,000 vblanks ≈ 400 s emulated).
- Probed symbols (P2 cache-through aliases of):
  `g_sm64_saturn_peak_probe` 0x002E5CB8 (LWRAM), `sourceboot_mario_snapshot`
  0x002DAD50, `sourceboot_cadence_trace` 0x002E5C6C,
  `g_sm64_saturn_object_pool_probe` 0x06092258, `__private_pool`
  0x060EF830 (40,960 B), `__peripherals_memb` 0x060923DC,
  `__peripherals_memb_memb_ref` 0x060FC9C4, `sAreaYaw`,
  `sourceboot_boot_trace`, `sourceboot_exception_record`.
- Three full runs were executed; the peak set was **identical across all
  three** (653 / 443 / 345 / 2 / 155). Run 1
  (`sprint2-t2_1-peak-capture-run1.json`) predates the movement witness
  and failed its own movement gate on the inert `sAreaYaw`; run 2
  (`sprint2-t2_1-peak-capture-run2.json`) added the Mario-position witness
  and passed; run 3 (`sprint2-t2_1-peak-capture.json`, the artifact of
  record) added the stain-below-terminals analysis. All three JSONs are
  committed beside this report.
- Movement proof (runs 2 and 3): 43 distinct sampled Mario world
  positions; x from −6558 to −343, z from 6464 to 4706, jumps to y 320;
  walking and airborne action values observed; every scene-state counter
  froze after simulation tick ~600 (replay end), exactly as the
  live-input bootstrap predicts.
- Acceptance (run 3, all true): capture_completed, peak_probe_seen,
  exception_record_clear, vdp_generations_climbing,
  route_movement_observed.

## Peak 1 — VDP1 command_count (gates cmdt 2048→1664)

Raw source (run 3 `peaks`): `"vdp1_commands_highwater": 653`,
`"vdp1_commands_last_max_sampled": 653` (accumulator and sampled cross-check
agree). This is the published frame-bank `command_count` — every cmdt in
the bank including the 3 setup commands
(`SM64_SATURN_VDP1_SETUP_COMMANDS`, `saturn_vdp1_frame_bank.c:13`).
Time series: 572 by the first valid sample, creeping to 653 at simulation
tick ~557, flat thereafter. Against the proposed 1,664: **margin 1,011**.
Against the conservative 1,792 variant: margin 1,139. Note the T1 floor
discussion (resolve cap 1536 + 128 control/HUD) concerns the fast3d
*resolve* buffer; the *bank* peak measured here is what the cmdt capacity
actually stores, and the quad-merge stage keeps it well below the resolved
triangle count.

## Peak 2 — libyaul TLSF `_private_pool` (gates 0xA000→0x4000)

Raw source (run 3 `private_pool_final`): physical block walk valid, chain
of 8 blocks ending in the zero-size sentinel at offset 40,956: 6 used
blocks totalling **4,980 B payload** (12, 128, 128, 104, 4,096, 512), one
68 B free hole, one 32,688 B top free block starting at offset 8,264.
`"stain_below_top_free_block": 8263`, `"stain_below_sentinel": 8275` —
i.e. **no byte beyond offset 8,275 of the 40,960-byte pool was ever
written** (the pool is boot-zeroed; block headers stain on creation, so a
transient allocation deeper than the surviving chain would have left a
header stain — none exists). Mid-run and end-of-run analyses are
identical: the allocations are boot-time (cdfs state/sector buffers, vdp
sync) with **zero churn over 24,000 frames**. TLSF control is 3,188 B, so
the historical extent is 8,276 B total from pool base. A 0x4000 (16,384 B)
pool provides 13,188 B of arena after control+sentinel: **margin 8,108 B**
over the historical extent. Conservative 0x6000 variant adds 8,192 B more.

## Peak 3 — `gGfxPool` display-list usage (gates GFX_POOL_SIZE 6400→4096)

Raw source (run 3 `peaks`): `"gfx_pool_entries_highwater": 443`,
`"gfx_pool_entries_last_max_sampled": 427`,
`"gfx_pool_task_count_final": 770` (the accumulator saw every one of the
770 gfx tasks — one per simulation tick — not just sample instants).
Usage is tiny because the Saturn render path routes geometry through its
own IR; the master DL carries frame scaffolding and HUD. Against 4,096:
**margin 3,653**. Even the observed peak times nine fits. `gGfxPools` is a
single pool on this target (51,276 B = 6400×8 + SPTask), so 6400→4096
recovers 18,432 B as T1's table states.

## Peak 4 — SMPC peripheral pool (reserve candidate, 14→2)

Raw source (run 3): every one of the 77 valid samples read
`alloc_count = 2`; `next_index = 2` at every sample; final refcount array
`[1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]`. Because `memb_alloc`
advances the round-robin cursor on every allocation and the refs slots
stain per allocation, cursor 2 plus slots {0,1} proves **exactly two
allocations occurred in the entire run** — this is a stain proof, not a
sampled live value, so the between-samples caveat does not apply. The two
blocks are the port-1/port-2 parent records; the single digital pad
produced no further allocations and no churn. 14→2 therefore matches
observed usage with **zero headroom**: any multitap/second-pad/hot-plug
event would exhaust the pool into `memb_alloc` assert territory.
**NEEDS-MARGIN**: take 14→4 (≈5,320 B of the 6,384–7,900 B estimate) if
this reserve is exercised, or pin the single-digital-pad constraint into
the tuple documentation before taking 14→2.

## Honesty and coverage limits

- **True accumulators vs sampled values.** `vdp1_commands_highwater`,
  `vdp1_gouraud_highwater`, `gfx_pool_entries_highwater` (this task's
  probe) and `peak_allocated` (object-pool probe) are run-long maxima
  updated at source — they cannot miss peaks between samples. The SMPC
  numbers are proven exact by the cursor/refs stains above. Nothing in the
  verdict table rests on a sampled live value alone.
- **Route coverage.** The tuple's live-input bootstrap consumes only the
  first 600 of the scripted route's 2,000 ticks, then the pad is neutral:
  this capture covers BOB entry, ~480 ticks of scripted movement/jumps,
  and idle. It does **not** cover owner free-roam, object interactions
  (`dynamic_actor_closure=0` in this tuple), later route segments, other
  camera angles, or other levels. Every SAFE verdict is "safe against the
  measured route", not "safe against all reachable states". T2.2 keeps
  the owner look-and-listen gate and the fail-open rules as the backstop.
- **Peak-1 context.** T1 grounded the resolve stage at 1,365–1,431
  resolved triangles on real BOB frames; the bank peak here (653) is
  after quad merge/admission. A future scene that defeats merging could
  push the bank closer to the resolve count; 1,664 still covers the worst
  observed resolve count halved plus setup/HUD with room, but this is the
  number to re-examine if T2.4's Mario diet changes composition.
- **TLSF caveats.** The stain scan cannot see an allocation whose payload
  was written with zeros and whose header space was later reused exactly —
  but every block creation stains its header, and the walk shows a
  boot-stable chain, so the realistic blind spot is negligible. Allocator
  placement in a smaller pool is not byte-identical to placement in the
  large pool; the 8.1 KB margin absorbs that.
- **Ymir vs hardware.** All numbers are from headless Ymir (the project's
  standard observation instrument). Command counts, pool usage, and DL
  entries are state, not timing; cadence-sensitive values (tick pacing)
  matched the known ~2 FPS profile but were not the subject here.
- **Pre-existing test failures.** `tools/saturn/test_render_snapshot_source.py`
  has 2 failures (`painter_chain_uses_all_existing_master_depth_tags`,
  `mario_textured_path_uses_fixed_gouraud_tables`) present at base HEAD
  `9a472e8d` before any T2.1 change (verified by stashing the changes);
  not touched here.
- **Misleading pre-existing field.** `profile.vdp1_command_highwater`
  looks like a run-long high-water but is cleared by the per-frame profile
  memset; anyone consuming it for capacity decisions would under-measure.
  Documented here; fixing it in the product path is out of T2.1's scope.

## Reproduction

```
# diagnostic build (attempt-2 form): stage-1 27-variable invocation with
# SATURN_DIAGNOSTIC_MODE=2, profile release_config.diagnostic_mode
# temporarily set to 2 (canonical LF JSON), via with-msys-toolchain.ps1
#   -> sh --noprofile --norc -l -c "source ../../.yaul.env &&
#      unset COMPILER_PATH && make -f Makefile.saturn.mk -j1 sourceboot ..."
python tools/saturn/capture_sprint2_peaks.py \
  --ymir .../ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl "sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game  build/saturn/sourceboot/e2-bob-identity-id-ae6740c94b1a409e/sm64-saturn-sourceboot-e2.cue \
  --elf   build/saturn/sourceboot/e2-bob-identity-id-ae6740c94b1a409e/obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest build/saturn/sourceboot/e2-bob-identity-id-ae6740c94b1a409e/saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_1-peak-capture.json
```

## What this proves / does not prove

Proves: measured peaks for all four gates over the accepted R1 tuple's own
scripted-route configuration, reproduced identically across three runs,
with movement positively witnessed; package (a) of T1 (cmdt 1664 +
`_private_pool` 0x4000 = 49,152 B) and the `GFX_POOL_SIZE` member of
package (b) are peak-cleared on this route; SMPC 14→2 is not cleared as
specified (14→4 is).

Does not prove: safety under owner free-roam or uncovered gameplay states;
any FPS claim; anything about visuals or audio (T2.2's gates own those).
