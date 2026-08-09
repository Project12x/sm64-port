# Object-pool occupancy probe + headless measurement — 2026-08-09

Task 2 of `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`.
This report feeds **OWNER GATE G1** (Task 3): the pool-capacity decision.
Numbers below are the real, unmodified output of
`tools/saturn/capture_object_pool_occupancy.py` against a real headless
Ymir run of the canonical flags-on geo-walk build. Companion raw evidence:
`memcamp-object-pool-occupancy-2026-08-09.json`.

## Status

Complete. Contract test written RED-then-GREEN (11 tests), probe wired at
the real allocate/free sites, canary passed, canonical config built and
linked, probe symbol resolved via `sh-elf-nm`, and a real 21,600-emulated-
frame headless capture (20,100 of them post-BIOS-handoff, exceeding the
plan's >=20,000 floor) sampled the probe every 300 frames.

**Headline: real measured peak occupancy is 138 objects (57.5% of the
240-slot pool), not the ~64 previously-cited rendered-actor bound.** See
"Why this matters for G1" below — this is the exact failure mode the plan
warned about (`gObjectPool` occupancy != rendered-actor count) borne out
in real data.

## 1. Real allocate/free sites (Step 2)

Grepped `src/game/spawn_object.c` per the plan's instruction not to trust
the guessed names — they were right, but the exact call sites needed
reading:

- **Allocate: `try_allocate_object()`**, `src/game/spawn_object.c:86-134`.
  The free-list pop happens at line 89 (`nextObj = freeList->next`); the
  probe hook (`src/game/spawn_object.c:121-131`) was placed right before
  the function's single successful-return point (line 133,
  `return (struct Object *) nextObj;`), because that is the one place
  every successful path (free-list pop, or the `USE_SYSTEM_MALLOC`
  fallback Saturn never compiles) converges before handing the slot to
  the caller.
- **Free: `deallocate_object()`**, `src/game/spawn_object.c:155-173`
  (`static void`, called from `unload_object()` at line 247, itself the
  public free entry point used everywhere else). The free-list push is
  lines 161-162 (`obj->next = freeList->next; freeList->next = obj;`); the
  probe hook (decrement, underflow-guarded) sits immediately after,
  `src/game/spawn_object.c:164-172`.
- **True exhaustion path: `allocate_object()`**,
  `src/game/spawn_object.c:269` onward. When `try_allocate_object()`
  returns `NULL` (free list empty, line 277) *and*
  `find_unimportant_object()` also returns `NULL` (no evictable object
  either, line 282), the port hangs forever (`while (TRUE) { }` at line
  290, the original comment: "We've met with a terrible fate."). The probe
  hook (`alloc_failures++`) sits immediately before that infinite loop,
  `src/game/spawn_object.c:283-288` — the recoverable eviction on the
  `else` branch (line 292 onward, which frees one slot via
  `unload_object()` and retries `try_allocate_object()`) does **not**
  count as a failure, since nothing is actually lost; only the fatal,
  unrecoverable exhaustion does.
- **Per-tick counter: `sourceboot_run_source_tick()`**,
  `src/port/saturn/sourceboot/main.c:485-548`. `frames_sampled++` sits at
  line 524 (unconditional, right after the `SATURN_DEMO_PATH`-gated block
  and before the tick's timing bookkeeping) — the same per-generation
  guarantee `sourceboot_boot_trace`'s own `SOURCE_TICK_BEFORE`/`_AFTER`
  boundary markers rely on, one call per `game_loop_one_iteration()`.

## 2. Probe struct and storage

`src/port/saturn/runtime/saturn_object_pool_probe.h` — `magic`
(`0x4F504F4C`, `'OPOL'`), `current_allocated`, `peak_allocated`,
`alloc_failures`, `frames_sampled`, all `volatile uint32_t` (20 bytes).
Storage (`src/game/object_list_processor.c:77-85`, right after
`gObjectPool[]`'s own definition) and every counter update site are gated
`#ifdef TARGET_SATURN` — `object_list_processor.c` and `spawn_object.c` are
portable (`src/game/`) files shared with every other SM64 port target.

**Resolved address (Step 5):** `g_sm64_saturn_object_pool_probe` ==
`0x0608d6d8` (P1) / `0x2608d6d8` (P2 cache-through alias), via
`sh-elf-nm -g --defined-only` against the canonical build's ELF (type `D`,
i.e. initialized `.data`, since the `magic` field is pre-seeded and the
rest are zero — same placement pattern as `sourceboot_boot_trace`).

## 3. Canary + canonical build (Steps 4-5)

- `verify-saturn-object-pool-probe-contract` (the new contract test):
  **11/11 PASS**.
- `verify-saturn-geo-walk-runtime` (canary, unrelated subsystem): **PASS**
  — unaffected by this change.
- Canonical flags-on geo-walk config (`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
  SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_SEMANTIC_AUDIO=0
  SATURN_RENDERER_PIPELINE=4 SATURN_DIAGNOSTIC_MODE=0`, via
  `verify-sourceboot`): **links clean**, sealed identity
  `id-9f17358cf257f515`. HWRAM linker asserts both pass:
  `___end = 0x060f9d3c` against the `0x06100000` ceiling — **0x62c4
  (25,284 B) of HWRAM surplus**, consistent with the plan's ground truth
  that this non-demo config (unlike the demo-path 18-flag config) links
  fine. `spawn_object.c` and `object_list_processor.c` compiled clean under
  the SH-2 cross-compiler's full warning set (`-Wall -Wextra -Wshadow ...`);
  the one pre-existing `-Wshadow` warning in `object_list_processor.c`
  (`spawn_objects_from_info`, line 490) is unrelated to this change.

## 4. Measurement (Step 6)

**Command:**

```
python tools/saturn/capture_object_pool_occupancy.py \
  --ymir  .../ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl   .../.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin \
  --game  build/saturn/sourceboot/e2-bob-identity-id-9f17358cf257f515/sm64-saturn-sourceboot-e2.cue \
  --elf   build/saturn/sourceboot/e2-bob-identity-id-9f17358cf257f515/obj/sm64-saturn-sourceboot-e2.elf \
  --output docs/saturn/evidence/reports/memcamp-object-pool-occupancy-2026-08-09.json \
  --timeout 1800
```

67 post-BIOS-handoff samples at the required 300-frame interval, 20,100
frames total (>= the plan's 20,000 floor), 21,600 emulated frames overall
including the 1,500-frame BIOS boot macro. Wall time: 332.2 s.

### Real numbers

| Metric | Value |
|---|---|
| `peak_allocated` (real max observed) | **138** |
| `alloc_failures` (real max observed) | **0** |
| Pool capacity (compiled-in, unmodified) | 240 |
| Peak occupancy vs. capacity | 138 / 240 = **57.5%** |
| `current_allocated` at final sample | 137 |
| `frames_sampled` at final sample | 665 game ticks |
| Post-BIOS samples captured / valid | 67 / 66 (one early sample, `post-bios-300` at emulated frame 1,800, predates the CD-boot sequence finishing the ELF `.data` load — expected and reported, not an error; see below) |

### Time-series shape: single-step ramp, then a dead-flat plateau

The probe's `.data` image isn't live in target RAM until the BIOS's CD-boot
sequence actually loads the executable — confirmed against
`sourceboot_boot_trace` (already-proven, pre-existing probe) reading the
same all-zero/garbage pattern at the same checkpoints, not a defect in the
new probe. Magic became valid at emulated frame 2,100 (`post-bios-600`).

From there the shape is almost the simplest possible curve:

- **First 3 valid samples** (emulated frames 2,100 / 2,400 / 2,700, i.e.
  `post-bios-600/900/1200`): `current_allocated = 0`, `peak_allocated = 0`
  — level not yet spawned in.
- **One-step jump** to `current_allocated = 137, peak_allocated = 138`
  between `post-bios-1200` (emulated 2,700) and `post-bios-1500` (emulated
  3,000) — i.e. within a single 300-emulated-frame (~10-game-tick) window,
  `frames_sampled` goes from 0 to 1. This matches SM64's own architecture:
  an area's macro object list spawns essentially all at once at level load
  (not incrementally as the player approaches), so BOB's static/logic
  object roster is fully present almost immediately after boot, independent
  of player position.
- **Dead-flat plateau** for the entire remaining measurement window —
  18,600 more emulated frames / 664 more game ticks (`post-bios-1500`
  through `post-bios-20100`, all 64 remaining valid samples): every single
  sample reads exactly `current_allocated = 137, peak_allocated = 138,
  alloc_failures = 0`. Zero variance. `peak_allocated` (138) never exceeds
  `current_allocated`'s final steady value (137) after the initial jump,
  by exactly 1 — one object existed transiently during spawn-in and was
  freed shortly after (consistent with a one-shot init/loading helper
  object), then occupancy never moved again for the rest of the capture.

Full 76-row time series (9 BIOS-handoff + 67 post-BIOS checkpoints, every
field) is in the companion JSON's `"samples"` array.

## 5. Route coverage — read this before deciding G1

**This capture is an idle-boot capture, not a played route.** The
canonical config Task 2 was told to build (`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_SEMANTIC_AUDIO=0
SATURN_RENDERER_PIPELINE=4 SATURN_DIAGNOSTIC_MODE=0`) does **not** set
`SATURN_SOURCEBOOT_LIVE_INPUT` or `SATURN_SOURCEBOOT_ROUTE_REPLAY`. Mario
never leaves BOB's default spawn point for the entire 21,600-frame capture
— there is no player movement, no coin collection, no enemy combat, and
critically **no pickup-and-hold interaction, the exact scenario this whole
campaign exists to unblock.**

What this measurement *does* honestly cover, and why the flat plateau is
not a red flag: SM64 spawns an area's full macro-object roster at level
load via the area's geo/behavior scripts, not incrementally as the player
approaches individual objects. The single sharp 0->137 jump landing
entirely within the first ~3,000 emulated frames (well before any input
would matter even if it were enabled) and then staying perfectly flat for
the remaining 18,600 frames is consistent with that: **this number very
likely already includes essentially all of BOB's static/logic/spawner
object roster** — signs, coins-in-view, enemies, camera/Mario helper
objects, King Bob-omb, the cannon, etc. — not just a handful of nearby
objects.

What it does **not** cover, and where the real peak could go higher:
- **Transient effect objects** Mario's own actions spawn: jump/landing
  dust, ground-pound shockwave, coin-collection sparkle/ring effects,
  footstep particles, star-collect fanfare objects. None of these exist
  in an idle capture because Mario never acts.
- **The pickup/hold interaction itself** — grabbing an object (e.g. a
  Bob-omb) may spawn or reparent additional held-object state that a
  standing-still Mario never triggers.
- **Any proximity- or trigger-gated spawns** beyond simple area-load, if
  BOB has any (not audited here — out of this task's scope).

**Conclusion for G1: treat 138 as a measured floor, not a worst-case
ceiling.** It is real, not estimated, and it is almost certainly close to
the *static* content ceiling for this area — but a live-route capture
(coin collection, combat, and specifically holding an object) would very
plausibly push the true peak higher. The plan's own recommended safety
margin (peak x 1.5) partly exists for exactly this gap.

## Why this matters for G1 (read before the AskUserQuestion)

The plan's own "Measured ground truth" section (written before this task
ran) cited "**at most 64 live rendered actors**" as the prior number
informing candidate pool capacities, and explicitly flagged the risk:
*"pool occupancy != rendered actors... The 64-live bound is attested for
RENDERING only."* That caution was correct and this measurement proves it:
**real pool occupancy (138) is more than double the previously-cited
rendered-actor bound (64)**, because the pool also holds invisible logic
objects (spawners, triggers, Mario/camera helpers) exactly as the plan
warned.

Practically: the plan's own worked examples for candidate capacities (96,
128 slots) are **both unsafe** against this real number — 96 and 128 are
both *below* the measured peak of 138, before even applying the
recommended 1.5x safety margin (138 x 1.5 = 207) or accounting for route
coverage gaps above. Task 3 (OWNER GATE G1) should present capacity
candidates computed from 138, not from the earlier 96/128 examples.

## Test counts

`tools/saturn/test_object_pool_probe_contract.py`: 11 tests, all passing
(struct field/volatile/magic contract, `TARGET_SATURN` gating for storage
and all three counter-update sites, and three "guard is not a tautology"
mutation-detection tests proving each of the three source-text checks
would actually fail if the corresponding hook were removed).

## Files

- New: `src/port/saturn/runtime/saturn_object_pool_probe.h`
- New: `tools/saturn/capture_object_pool_occupancy.py`
- New: `tools/saturn/test_object_pool_probe_contract.py`
- Modified: `src/game/spawn_object.c`, `src/game/object_list_processor.c`,
  `src/port/saturn/sourceboot/main.c`
- Modified: `Makefile.saturn.mk` (`verify-saturn-object-pool-probe-contract`
  target, wired into `verify-all` and the master `.PHONY` list)
- Evidence: this report + companion
  `memcamp-object-pool-occupancy-2026-08-09.json`
