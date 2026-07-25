# Sourceboot Model-Load Stage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Register the 47 models the retail boot chain loads before any level script runs, so Mario's `sharedChild` stops being NULL and his geometry reaches the VDP1 pipeline.

**Architecture:** Mirror retail's `ALLOC_LEVEL_POOL` → 47 × `LOAD_MODEL_*` → `FREE_LEVEL_POOL` sequence inside sourceboot's own entry script (`src/port/saturn/sourceboot/source_entry.c`). Engine files stay unmodified. Incremental: register Mario alone first to prove the mechanism and measure his cost, then extend to all 47 with a measured pool gate.

**Tech Stack:** C (SH-2 cross-compile via Yaul), SM64 level-script interpreter, `ymir-headless` memory-probe captures.

Design spec: `docs/superpowers/specs/2026-07-25-mario-model-load-gap-design.md`

---

## Standing workflow facts

**Host suite** (Git-Bash is fine):
```bash
export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port
make -f Makefile.saturn.mk OS=Windows_NT verify-runtime-contracts SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe
```

**SH-2 cross-compile** — env vars do NOT survive the Git-Bash → msys64 boundary; source `.yaul.env` inside ONE msys64 session:
```bash
/c/msys64/usr/bin/bash.exe -lc 'cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && source .yaul.env && cd src/port/saturn/sourceboot && make -j2 && make verify'
```

**Capture recipe** (resolve `--probe-address` fresh each time with `sh-elf-nm`; addresses shift whenever statics change):
```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port
./.venv-saturn-tools/Scripts/python.exe tools/saturn/capture_hwtest.py \
  --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" \
  --ipl "C:/Users/estee/AppData/Local/Temp/Sega Saturn BIOS (USA).bin" \
  --game "build/saturn/sourceboot/e2-bob/sm64-saturn-sourceboot-e2.cue" \
  --dram-cart --bios-input --frames 240 --handoff-yield \
  --post-poke-frames 25000 --probe-address <FRESH> --probe-count <N> \
  --allow-invalid --timeout 1500 \
  --screenshot-output <PNG> --output <JSON>
```
`--probe-count` is a **byte** count (`capture_hwtest.py:149`). Captures take 5-10 minutes; run them in the background and do not run two at once (they contend and time out).

**Do not run captures while a cross-compile is in flight** — the capture reads the `.cue`/ISO the build overwrites.

**Struct offsets: never hand-derive.** Ground-truth with an `offsetof()` probe compiled under the host suite's own flags. Standing lesson: `docs/saturn/evidence/e2-sourceboot-bad-mtx-pointer-2026-07-22.md`.

## Baseline to beat (terrain-only, from `v0.2.0` capture evidence)

| Counter | Value |
| --- | --- |
| `triangles_transformed` | 1307 |
| `triangles_emitted` | 437 |
| `triangles_vdp1_emitted` | 0 at probed frame (profile resets per frame; sample lands pre-emit) |
| `fault_flags` | 0 |
| Mario `sharedChild` | `0x0` |

Symbols at `v0.2.0` + review fixes (**re-resolve every task**):
`_gMarioObject` `0x060a8938`, `_sPoolFreeSpace` `0x060952d8`, `_sourceboot_fast3d` `0x060bddf8`.

## Resolved before planning (do not re-investigate)

- **No segment loads are needed.** `segmented_to_virtual` is the identity function under `NO_SEGMENTED_MEMORY` (`src/game/memory.c:138-140`), and `level_cmd_load_model_from_geo` (`src/engine/level_script.c:427`) passes the geo pointer straight to `process_geo_layout` with no segment lookup. Do **not** add `LOAD_MIO0`/`LOAD_RAW` for group0 — those call `load_segment_decompress` → `dma_read` against N64 ROM addresses that do not exist on this target.
- **`FREE_LEVEL_POOL` is shrink-to-fit, not destroy** (`src/engine/level_script.c:363-369` calls `alloc_only_pool_resize(sLevelPool, sLevelPool->usedSpace)`). Registered models survive it. This is why retail can free the pool and still have models.
- **The model-load commands do not touch `sRegister`**, so they cannot disturb the load-bearing `SET_REG(LEVEL_BOB)` → `CALL` chain documented at `source_entry.c:63-124`.

## File structure

- **Modify: `src/port/saturn/sourceboot/source_entry.c`** — the only file this plan changes. Gains includes for the actor geo symbols and a model-registration block inside `level_script_entry[]`.

No new files. No engine files. No test files (this is level-script integration; there is no host-testable unit — verification is cross-compile + memory probe + capture, the same posture as Task 6 of the Gouraud plan).

---

### Task 1: Baseline LWRAM pool measurement

Pure measurement, no code change. Establishes the gate that decides whether all 47 models fit.

**Files:** none modified.

- [x] **Step 1: Resolve the pool symbol fresh**

```bash
/c/msys64/usr/bin/bash.exe -lc 'cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && source .yaul.env && sh-elf-nm build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf | grep -E "_sPoolFreeSpace|_sPoolStart|_sPoolEnd"'
```
Expected: three addresses. `sPoolFreeSpace` is a `u32` holding remaining main-pool bytes (`src/game/memory.c:315-317`: `main_pool_available()` returns `sPoolFreeSpace - 16`).

- [x] **Step 2: Capture the baseline pool state**

Probe 12 bytes starting at `_sPoolStart` (covers `sPoolStart`, `sPoolEnd`, `sPoolFreeSpace` — they are adjacent at `0x060952d4/d0/d8`; confirm ordering from Step 1's addresses and probe the lowest address for 16 bytes to cover all three).

Run the capture recipe with `--probe-address <lowest of the three>` `--probe-count 16`, output to the session scratchpad (NOT the repo).

- [x] **Step 3: Decode and record**

```python
import json, struct
d = json.load(open('<baseline json>'))
b = bytes(d['probe_window']['data']); base = d['probe_window']['address']
u32 = lambda o: struct.unpack('>I', b[o:o+4])[0]
for name, addr in [('sPoolStart', 0x060952d4), ('sPoolEnd', 0x060952d0), ('sPoolFreeSpace', 0x060952d8)]:
    print(f'{name:16s} = {u32(addr-base):#x} ({u32(addr-base)})')
```
Record `sPoolFreeSpace` — this is the headroom budget for Task 4's decision.

- [x] **Step 4: Commit the measurement note**

No code changed, so commit only if you add an evidence file. If you do:
```bash
git add docs/saturn/evidence/reports/<baseline pool json>
git commit -m "test(saturn): baseline LWRAM main-pool headroom before model registration"
```
Otherwise record the number in the Task 2 commit message and skip this step.

Committed as `a49f871`. `sPoolFreeSpace = 163,424` bytes;
`main_pool_available() = 163,408` (~159.6 KiB) after the function's own
internal `-16`. Spec-compliance review independently re-derived every
number down to the 16-byte alignment sentinels in `main_pool_init`
(`sPoolStart = ALIGN16(start)+16`, `sPoolEnd = ALIGN16(end-15)-16`) and
confirmed `SOURCEBOOT_MAIN_POOL_BYTES = 0x60000` (exactly 384 KiB)
accounts for the full observed span — a genuine re-derivation, not a
re-print of the implementer's numbers.

---

### Task 2: Register MODEL_MARIO alone

Smallest change that proves the mechanism end to end.

**Files:**
- Modify: `src/port/saturn/sourceboot/source_entry.c` (includes near line 8-16; script body at line 44)

- [x] **Step 1: Add the actor headers**

In `src/port/saturn/sourceboot/source_entry.c`, after the existing `#include "levels/bob/header.h"`, add:

```c
/* Model geo/DL symbols for the registration block below. Retail declares
 * these in levels/scripts.c, which this target never executes -- see the
 * block comment on the registration sequence. group0 carries mario_geo;
 * common0/common1 carry the effect, coin, star, and cap models. */
#include "actors/common0.h"
#include "actors/common1.h"
#include "actors/group0.h"
#include "model_ids.h"
```

- [x] **Step 2: Add the registration block** (see completion note — placement changed from the sketch below during review)

In `level_script_entry[]` (`source_entry.c:44`), immediately after `INIT_LEVEL(),` and **before** `SET_REG(/* value */ 1),`, insert:

```c
    /* MODEL REGISTRATION -- restores the stage retail performs in
     * level_main_scripts_entry (levels/scripts.c:67-115) before any level
     * script runs. This target never executes that script (see the
     * lvl_init_from_save_file comment below, which documents the same
     * gap for a different consequence), so gLoadedGraphNodes stayed
     * entirely empty: every MARIO()/OBJECT() command read a NULL model
     * pointer into spawnInfo->unk18, which became a NULL sharedChild, and
     * geo_process_object (src/game/rendering_graph_node.c:1127) skipped
     * the whole subtree. Mario has never had geometry submitted.
     *
     * Placed before the act/level SET_REG chain because these commands do
     * not touch sRegister, so they cannot perturb the load-bearing
     * ordering documented below.
     *
     * FREE_LEVEL_POOL is shrink-to-fit, not destroy
     * (src/engine/level_script.c:363-369 resizes the pool to usedSpace),
     * so these registrations survive into BOB's own ALLOC_LEVEL_POOL at
     * levels/bob/script.c:67 -- exactly as they do in retail.
     *
     * No LOAD_MIO0/LOAD_RAW segment commands are needed or wanted: this
     * build defines NO_SEGMENTED_MEMORY, under which segmented_to_virtual
     * is the identity function (src/game/memory.c:138-140) and
     * level_cmd_load_model_from_geo passes the geo pointer straight
     * through (src/engine/level_script.c:427). The segment commands would
     * call load_segment_decompress -> dma_read against N64 ROM addresses
     * that do not exist on this target. */
    ALLOC_LEVEL_POOL(),
    LOAD_MODEL_FROM_GEO(MODEL_MARIO,                   mario_geo),
    FREE_LEVEL_POOL(),
```

- [x] **Step 3: Cross-compile**

```bash
/c/msys64/usr/bin/bash.exe -lc 'cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && source .yaul.env && cd src/port/saturn/sourceboot && make -j2 && make verify'
```
Expected: both exit 0. If a geo symbol is undefined, the linker names it — add the header that declares it (find with `grep -ln "<symbol>" actors/*.h`).

Expected side effect: **`SOURCE.DAT` grows**, because `mario_geo` and everything it references stop being garbage-collected. `make verify` enforces the 4 MiB cap; note the new size.

- [x] **Step 4: Verify `sharedChild` is now non-NULL**

Re-resolve `_gMarioObject` (it will have moved):
```bash
/c/msys64/usr/bin/bash.exe -lc 'cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && source .yaul.env && sh-elf-nm build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf | grep -E " _gMarioObject$"'
```

Capture with `--probe-address <fresh gMarioObject>` `--probe-count 96`, then decode:
```python
import json, struct
d = json.load(open('<json>'))
b = bytes(d['probe_window']['data'])
u32 = lambda o: struct.unpack('>I', b[o:o+4])[0]
u16 = lambda o: struct.unpack('>H', b[o:o+2])[0]
print('node.flags  =', hex(u16(0x02)))
print('sharedChild =', hex(u32(0x14)))   # MUST be non-zero
print('areaIndex   =', struct.unpack('>b', b[0x18:0x19])[0])
```
Expected: `sharedChild` non-zero. `flags` still `0x21` (`GRAPH_RENDER_ACTIVE | GRAPH_RENDER_HAS_ANIMATION`).

**If `sharedChild` is still `0x0`, stop and report.** The registration did not take effect and nothing downstream is worth measuring.

- [x] **Step 5: Commit**

```bash
git add src/port/saturn/sourceboot/source_entry.c
git commit -m "fix(saturn): register MODEL_MARIO so his geometry reaches the render graph"
```
Record in the message: baseline `sPoolFreeSpace` from Task 1, the new `SOURCE.DAT` size, and the observed `sharedChild` value.

Committed as `a130003`, then revised twice more during review. `sharedChild`
went from `0x0` to non-zero, hardware-verified: `node.type = 0x18`
(`GRAPH_NODE_TYPE_OBJECT`), `node.flags = 0x21`
(`GRAPH_RENDER_ACTIVE | GRAPH_RENDER_HAS_ANIMATION`), `sharedChild` a
real LWRAM address. `SOURCE.DAT` grew from 1,715,520 to 1,849,904 bytes
(both under the 4 MiB cap).

**Real deviation from the plan's literal sketch, found during Step 4:**
`gMarioObject` is a `struct Object *` — a pointer variable, not the
object struct itself. Probing directly at its `nm`-resolved address (as
the plan's Step 4 literally describes) reads the pointer's own 4-byte
storage, not `GraphNodeObject` fields. Every capture in this task and its
reviews instead resolved the pointer's stored value first, then probed
that dereferenced address. Confirmed structurally sound by checking
`struct Object`'s layout (`include/types.h`): its first member is
`struct ObjectNode header`, whose first member is
`struct GraphNodeObject gfx` — so one dereference lands exactly on the
`GraphNodeObject` header at offset 0, no further offset needed.
Independently reproduced by both the spec-compliance and code-quality
reviewers, byte-for-byte identical each time.

**Two-stage review, plus a genuine fix-and-reverify loop:**

Spec-compliance ✅ — every comment citation checked against real source
line numbers, the build reproduced from a forced-clean rebuild
(`SOURCE.DAT` size matched exactly), and the two-step capture reproduced
independently with identical results.

Code-quality found a real Important-severity bug: the registration was
placed AFTER `INIT_LEVEL()`, which calls `main_pool_push_state()`
(`src/engine/level_script.c:336`). Nothing in this script ever pops that
frame, so the trailing `JUMP(level_script_entry)` — which re-runs
`INIT_LEVEL()` on every level-exit/reentry cycle — orphaned the
registration's allocation every cycle. Not a problem before this commit
(nothing was allocated in the unpopped frame); a real, non-zero leak
once something was.

Fixed by moving the registration before `INIT_LEVEL()`, matching
retail's own `level_main_scripts_entry` (`levels/scripts.c:67-115`),
which genuinely never calls `INIT_LEVEL()` before its own model loads —
independently confirmed. Committed as `2f365a9`.

**Re-review of the fix found it incomplete**, not wrong in direction:
the reorder only closes the leak for the array's FIRST pass. Because
`main_pool_push_state()`/`pop_state()` track cumulative stack state
across the WHOLE execution rather than being scoped to array position,
cycle 2's re-run of the (now-earlier) registration block still lands
inside cycle 1's own unpopped `INIT_LEVEL()` frame. Retail avoids this
because its registration prologue is structurally OUTSIDE the loop that
revisits level scripts; matching retail's command *order* inside a
self-looping array isn't the same as matching retail's script
*topology*. A full fix needs the registration hoisted into a true
one-time prologue ahead of a separate looping body.

**Deliberately not fixed as part of this task** (`f8b4535` corrects the
comment to say so plainly instead of overclaiming closure): this plan's
own test methodology is a single continuous boot with no level-exit/
reentry, so the residual leak is never exercised by anything this plan
measures or ships; `lvl_init_or_update` is real, unmodified retail
transition logic, so reachability can't be ruled out once warp/star
mechanics work end-to-end; and a full topology restructuring is
separate, larger work with its own risk to the boot sequence every task
in this plan depends on. Spawned as a tracked background task
(`task_7109ee0d`) rather than silently accepted.

---

### Task 3: Measure Mario's rendering cost

The measurements that size sub-project 2. Record them even if Mario looks wrong.

**Files:** none modified.

- [x] **Step 1: Ground-truth the profile offsets**

The profile struct has not changed since `v0.2.0`, but re-derive rather than trust. Compile an `offsetof()` probe in the scratchpad with the host suite's exact flags:

```bash
export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port
cc -std=c11 -Wall -Wextra -Werror \
  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
  -I"$PWD/include" -I"$PWD/src" -I"$PWD/src/port/saturn/gfx" -I"$PWD/src/port/saturn/platform" \
  <scratchpad>/offset_probe.c \
  src/port/saturn/gfx/saturn_fast3d_frontend.c src/port/saturn/gfx/saturn_trig_q16.inc.c \
  -o <scratchpad>/offset_probe.exe && <scratchpad>/offset_probe.exe
```
The probe prints `offsetof(sm64_saturn_fast3d_profile_t, <field>)` for every field plus `sizeof`. Size `--probe-count` from that `sizeof` plus margin.

- [x] **Step 2: Capture the frontend profile**

Re-resolve `_sourceboot_fast3d`, capture with that address and the probe count from Step 1.

- [x] **Step 3: Difference against the terrain-only baseline**

Decode all counters. Compute Mario's contribution:

| Counter | Terrain baseline | New | Mario's share |
| --- | --- | --- | --- |
| `triangles_transformed` | 1307 | ? | new − 1307 |
| `triangles_emitted` | 437 | ? | new − 437 |
| `reject_backface` | 693 | ? | — |
| `modelview_stack_overflow` | 0 | ? | must stay 0 |
| `reject_w_nonpositive_overflow_suspect` | 0 | ? | must stay 0 |
| `fault_flags` | 0 | ? | must stay 0 |

The last three are the sensors for whether the **animated-part Q16 matrix path** is sane — this is the first time a skinned actor has ever been traversed in this target. Non-zero values there are a finding, not a nuisance.

- [x] **Step 4: Record the measurements**

Write the numbers into the commit message and into `docs/saturn/evidence/` as a report JSON. These are sub-project 2's inputs: Mario's triangle count decides the quad-pairing payoff, and total commands price him against the ~650-command 15 FPS floor / ~450-command 20 FPS stretch (`docs/saturn/ROADMAP.md`).

- [x] **Step 5: Commit the evidence**

```bash
git add docs/saturn/evidence/reports/<mario cost json>
git commit -m "test(saturn): Mario rendering cost through the real Fast3D path"
```

Committed as `f83a4a4`. Ground-truthed `sizeof(sm64_saturn_fast3d_profile_t)
= 228`; captured `_sourceboot_fast3d` fresh with `--probe-count 256`.
The three "must stay 0" sensors this task exists to check
(`modelview_stack_overflow`, `reject_w_nonpositive_overflow_suspect`,
`fault_flags`) **all held at 0** — no evidence the animated/skinned Q16
matrix path faults on its first-ever traversal.

**The raw triangle-count differencing produced a genuine surprise**: every
counter went DOWN with Mario present (`triangles_transformed`
1307→895, `triangles_emitted` 437→301), the opposite of what adding
geometry should do. The implementer correctly declined to trust the
naive subtraction and flagged it rather than papering over it, hypothesizing
a one-time CD-load delay from Mario's larger `SOURCE.DAT` (1,715,520→
1,849,904 bytes, Task 2) shifting where the fixed `--post-poke-frames`
budget lands.

**Spec-compliance review tested that hypothesis with a real second
capture** (same build, +10,000 post-poke-frames) rather than accepting
it as plausible-sounding: `frame_serial` 151→214, but
`triangles_transformed` kept *declining* (895→664) instead of recovering
toward baseline — a durable rate reduction, not a startup artifact. Also
ruled out a capacity ceiling (`reject_command_capacity`/
`reject_vdp1_arena_capacity` are 0 in every sample). The three "must stay
0" sensors held clean at this second, longer-runway sample too — two
independent confirmations now. Root cause of the durable reduction
(Mario's own per-frame cost vs. CD-streaming contention) is not
established; that needs real cycle instrumentation, out of scope for a
measurement task. Corrected and recorded in `4c50ce5`, with the
verification capture committed as supporting evidence rather than left
in scratch.

**Guidance recorded for sub-project 2's sizing work**: do not compare
single frames at matched `post_poke_frames` across builds — compare at
matched `frame_serial`, or sample a range and compare steady-state
rates, since the free-roam camera trajectory is still moving across the
whole observed window.

Screenshot (scratchpad only, not committed — Task 5 owns the formal
user-facing capture): Mario visibly rendering, red hat and blue
overalls, standing on the terrain near the cannon — an independent,
incidental confirmation that the RGB1555 lane fix from the prior sprint
holds on genuinely new content, not just the object it was originally
diagnosed against.

---

### Task 4: Extend to all 47 models

**Files:**
- Modify: `src/port/saturn/sourceboot/source_entry.c` (the registration block from Task 2)

- [x] **Step 1: Replace the single load with the full block**

Replace the lone `LOAD_MODEL_FROM_GEO(MODEL_MARIO, mario_geo),` line with all 47 entries, copied verbatim from `levels/scripts.c:68-114`. Keep `ALLOC_LEVEL_POOL()` before and `FREE_LEVEL_POOL()` after. The full list:

```c
    LOAD_MODEL_FROM_GEO(MODEL_MARIO,                   mario_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SMOKE,                   smoke_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SPARKLES,                sparkles_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BUBBLE,                  bubble_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SMALL_WATER_SPLASH,      small_water_splash_geo),
    LOAD_MODEL_FROM_GEO(MODEL_IDLE_WATER_WAVE,         idle_water_wave_geo),
    LOAD_MODEL_FROM_GEO(MODEL_WATER_SPLASH,            water_splash_geo),
    LOAD_MODEL_FROM_GEO(MODEL_WAVE_TRAIL,              wave_trail_geo),
    LOAD_MODEL_FROM_GEO(MODEL_YELLOW_COIN,             yellow_coin_geo),
    LOAD_MODEL_FROM_GEO(MODEL_STAR,                    star_geo),
    LOAD_MODEL_FROM_GEO(MODEL_TRANSPARENT_STAR,        transparent_star_geo),
    LOAD_MODEL_FROM_GEO(MODEL_WOODEN_SIGNPOST,         wooden_signpost_geo),
    LOAD_MODEL_FROM_DL( MODEL_WHITE_PARTICLE_SMALL,    white_particle_small_dl,     LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO(MODEL_RED_FLAME,               red_flame_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BLUE_FLAME,              blue_flame_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BURN_SMOKE,              burn_smoke_geo),
    LOAD_MODEL_FROM_GEO(MODEL_LEAVES,                  leaves_geo),
    LOAD_MODEL_FROM_GEO(MODEL_PURPLE_MARBLE,           purple_marble_geo),
    LOAD_MODEL_FROM_GEO(MODEL_FISH,                    fish_geo),
    LOAD_MODEL_FROM_GEO(MODEL_FISH_SHADOW,             fish_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SPARKLES_ANIMATION,      sparkles_animation_geo),
    LOAD_MODEL_FROM_DL( MODEL_SAND_DUST,               sand_seg3_dl_0302BCD0,       LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO(MODEL_BUTTERFLY,               butterfly_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BURN_SMOKE_UNUSED,       burn_smoke_geo),
    LOAD_MODEL_FROM_DL( MODEL_PEBBLE,                  pebble_seg3_dl_0301CB00,     LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO(MODEL_MIST,                    mist_geo),
    LOAD_MODEL_FROM_GEO(MODEL_WHITE_PUFF,              white_puff_geo),
    LOAD_MODEL_FROM_DL( MODEL_WHITE_PARTICLE_DL,       white_particle_dl,           LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO(MODEL_WHITE_PARTICLE,          white_particle_geo),
    LOAD_MODEL_FROM_GEO(MODEL_YELLOW_COIN_NO_SHADOW,   yellow_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BLUE_COIN,               blue_coin_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BLUE_COIN_NO_SHADOW,     blue_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_WINGED_METAL_CAP, marios_winged_metal_cap_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_METAL_CAP,        marios_metal_cap_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_WING_CAP,         marios_wing_cap_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_CAP,              marios_cap_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_CAP,              marios_cap_geo), // repeated upstream
    LOAD_MODEL_FROM_GEO(MODEL_BOWSER_KEY_CUTSCENE,     bowser_key_cutscene_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BOWSER_KEY,              bowser_key_geo),
    LOAD_MODEL_FROM_GEO(MODEL_RED_FLAME_SHADOW,        red_flame_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_1UP,                     mushroom_1up_geo),
    LOAD_MODEL_FROM_GEO(MODEL_RED_COIN,                red_coin_geo),
    LOAD_MODEL_FROM_GEO(MODEL_RED_COIN_NO_SHADOW,      red_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_NUMBER,                  number_geo),
    LOAD_MODEL_FROM_GEO(MODEL_EXPLOSION,               explosion_geo),
    LOAD_MODEL_FROM_GEO(MODEL_DIRT_ANIMATION,          dirt_animation_geo),
    LOAD_MODEL_FROM_GEO(MODEL_CARTOON_STAR,            cartoon_star_geo),
```

The duplicated `MODEL_MARIOS_CAP` line is present upstream at `levels/scripts.c:103-104`; keep it so the block diffs cleanly against its origin.

- [x] **Step 2: Verify the count**

```bash
grep -c "LOAD_MODEL_FROM" src/port/saturn/sourceboot/source_entry.c
```
Expected: `47`.

- [x] **Step 3: Cross-compile**

Same command as Task 2 Step 3. Expected: exit 0 on both. `SOURCE.DAT` grows further; `make verify` enforces the 4 MiB cap. If it now exceeds the cap, that is the degradation trigger — go to Step 5.

- [x] **Step 4: Measure pool headroom after registration**

Re-resolve `_sPoolFreeSpace`, capture, decode as in Task 1 Step 3.

**The gate:** `sPoolFreeSpace` must remain comfortably positive after BOB's own area/collision load, which happens later in the same boot. BOB's collision alone needs roughly 166 KiB of the 384 KiB pool. If the post-registration figure leaves less than ~32 KiB of slack, treat it as failing and go to Step 5.

Watch specifically for the failure mode this project has already suffered: an under-sized pool does **not** report an error — `alloc_only_pool_alloc` returns NULL and callers write through it (see the `SOURCEBOOT_MAIN_POOL_BYTES` history comment in `src/port/saturn/sourceboot/main.c`). A boot that still renders is not proof the pool was sufficient. Check the number.

- [x] **Step 5: Degradation path, only if Step 3 or Step 4 failed** — not triggered; both gates passed (see completion note below)

Cut the block to a named subset: `MODEL_MARIO` plus the models BOB actually spawns. Do not silently truncate — keep every retained entry and add a comment above the block stating which entries were removed, the measured figure that forced it, and that the full list lives at `levels/scripts.c:68-114`. Then re-run Steps 2-4.

- [x] **Step 6: Commit**

```bash
git add src/port/saturn/sourceboot/source_entry.c
git commit -m "fix(saturn): register the full master-script model set in sourceboot"
```
Record: pool headroom before/after, `SOURCE.DAT` before/after, and whether the full 47 or a subset shipped.

**Completion note (2026-07-25):** Full 47-entry block shipped, no degradation needed. `source_entry.c`'s registration block matches `levels/scripts.c:68-114` byte-for-byte (independently diffed twice — once by spec-compliance review, once by code-quality review), including the upstream `MODEL_MARIOS_CAP` duplicate. `#include "sm64.h"` was added for `LAYER_ALPHA` (traced: no other included header pulls it in transitively; retail's own `levels/scripts.c:2` includes the same header for the same symbol).

Numbers, independently reproduced by the spec-compliance reviewer on a from-scratch rebuild (not just read off the implementer's report):
- `grep -c "LOAD_MODEL_FROM"` → 47 (after one fix — see below).
- `make -j2` / `make verify` → exit 0 both.
- `SOURCE.DAT`: 1,849,904 → 2,049,200 bytes (+199,296 for 46 additional models), 640 KiB well under the 4 MiB cap.
- `_sPoolFreeSpace` fresh-resolved at `0x060959d8` (moved from Task 1's `0x060952d8` as the binary grew — not trusted stale).
- Pool headroom: 163,424 bytes (Task 1, 0-model baseline) → 124,128 bytes (47 models registered), independently re-captured and re-decoded, exact match. Clears the ~32 KiB slack gate by ~3.8x.
- Pool-math reasoning independently re-derived, not just trusted: traced `alloc_surface_pools()` → `level_cmd_free_level_pool()` (`level_script.c:373`) to confirm BOB's own ~166 KiB surface-pool draw fires on BOB's `FREE_LEVEL_POOL()` within the first few frames of level entry (no `SLEEP` precedes it in `levels/bob/script.c`), well before the 25000-post-poke-frame capture point — so the 124,128-byte figure is genuinely post-BOB-load and does not need a further deduction. The implementer's own initial mis-read of this (treating 166 KiB as a separate subtraction) was corrected before commit; the reviewer's independent trace confirms the correction was right.

**One real finding, fixed:** spec-compliance review's mandated `grep -c "LOAD_MODEL_FROM"` gate returned 48, not 47, on first commit — the includes-block comment (added for the `sm64.h`/`LAYER_ALPHA` justification) contained the literal substring `LOAD_MODEL_FROM_DL`, which the pattern matched as a 48th line. The actual code block always had exactly 47 real entries; this was a verification-gate artifact, not a functional bug. Fixed by rewording the comment (commit `2743ea5`) to avoid the substring; re-verified `grep -c` → 47 and re-ran the full cross-compile + `make verify` afterward (both exit 0, output unchanged). Code-quality review then confirmed the reworded comment reads naturally, with no trace of the two-commit fix history visible to a fresh reader of the final file.

Both review stages closed clean: spec-compliance ISSUES_FOUND → fixed → re-verified compliant; code-quality APPROVED with no Important or Minor issues (one non-blocking observation noted: no dedicated docs-sync commit for this task, consistent with Tasks 1-2's pattern — Task 3 was the outlier that got one).

---

### Task 5: Full regression, live capture, user gate

**Files:** evidence only.

- [ ] **Step 1: Full regression**

All four host targets:
```bash
export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port
for t in verify-tools verify-runtime-contracts verify-mtxq-ctors verify-mtxf-lookat-host-diff; do
  make -f Makefile.saturn.mk OS=Windows_NT $t SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe >/dev/null 2>&1
  echo "$t EXIT: $?"
done
```
Expected: all four exit 0. Then the SH-2 cross-compile + `make verify`, both exit 0.

- [ ] **Step 2: Live capture at free-roam depth**

Re-resolve `_sourceboot_fast3d`, capture with a screenshot into `docs/saturn/evidence/screenshots/e2-sourceboot-mario-freeroam-2026-07-25.png` and report into `docs/saturn/evidence/reports/e2-sourceboot-mario-freeroam-2026-07-25.json`.

- [ ] **Step 3: Judge honestly**

| Metric | Expectation |
| --- | --- |
| Mario `sharedChild` | non-NULL |
| `triangles_transformed` | materially above 1307 |
| `fault_flags` | 0 |
| `modelview_stack_overflow` | 0 |
| Screenshot | Mario visible, animated, Gouraud-shaded, near his spawn |

Both outcomes are acceptable completions: Mario renders, or he does not and the counters attribute the next cause. He has never been traversed before, so a second-order failure here is a real result, not a botched task. Report raw numbers either way.

- [ ] **Step 4: Commit the evidence**

```bash
git add docs/saturn/evidence/reports/e2-sourceboot-mario-freeroam-2026-07-25.json \
        docs/saturn/evidence/screenshots/e2-sourceboot-mario-freeroam-2026-07-25.png
git commit -m "test(saturn): Mario free-roam capture evidence"
```

- [ ] **Step 5: Present the screenshot to the user — do not write gallery entries**

Show the user the screenshot and the raw counters. **Do not** write `docs/saturn/evidence/TIMELINE.md` or `index.html` entries until the user confirms what they see. Standing rule: the user's eyes are the acceptance gate.

Note for whoever presents it: this project has twice had an on-screen object mis-identified from inference rather than evidence. Describe what the counters prove and let the user identify what they see.

---

## Out of scope

Quad-optimized Mario IR (sub-project 2, sized from Task 3's measurements); textures; transform-once caching and any other performance work; geometry LOD; any change to `levels/scripts.c` or other engine files. This cycle may legitimately make the frame slower — that gets measured, not fixed.

## Self-review (performed at write time)

- **Spec coverage:** root cause (Tasks 2/5), engine files unmodified (file structure — only `source_entry.c` changes), pool measured not estimated (Tasks 1/4), named-subset fallback (Task 4 Step 5), segment question resolved (Resolved section, with evidence), success criteria (Task 5 Step 3), sub-project 2 measurements (Task 3), user gate (Task 5 Step 5). No spec requirement is unassigned.
- **Placeholder scan:** no TBD/TODO. `<FRESH>`/`<N>`/`<scratchpad>` are deliberate run-time-resolved values with the exact command that produces each, not unspecified work.
- **Type consistency:** `sharedChild` at offset `0x14`, `flags` at `0x02`, `areaIndex` at `0x18` used identically in Tasks 2 and 5, matching `struct GraphNodeObject` in `include/types.h`. Counter names match `sm64_saturn_fast3d_profile_t` throughout.
- **Known ordering hazard, stated in-plan:** captures and cross-compiles must not overlap; captures must not run concurrently with each other.
