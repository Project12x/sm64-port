# R3 C + F — memory recovery: the reduced animation bake and the LWRAM staging

- Date: 2026-08-22. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `e3cdca0e`, commits `2ca7abf2` (C),
  `4bfe4afb` (F) and `4efa7082` (package generation 16).
- Options **C** and **F** from
  [`r3-mario-animation-scoping.md`](r3-mario-animation-scoping.md) §8, both
  owner-approved. **Mesh decimation is explicitly out of scope** and pose-slot
  pooling was not touched.
- Baseline is the owner-accepted `id-36ebaa12184cd661`: **6.8775 mean /
  6.6667 median at route tick 30**, **6.7969 / 6.6667 at tick 150**, HWRAM
  free **10,488 B**, cart free **117,840 B**.
- **No emulator GUI.** All observation is headless Ymir.
- Governing principle: phase plan §2b — functional correctness beats numerical
  correctness, this port descends a generation, every subsystem is a
  degradation decision.

---

## 0. The answer, in one paragraph

**Both changes landed in one build, `id-8e643f9cc3e9fdee`, and neither costs a
frame.** Cadence at route tick 30 is **6.8775 mean / 6.6667 median**, which is
the accepted baseline's figure to four decimal places and the same 8.7241
VBlanks per frame; at tick 150 it is **6.8235 / 6.6667**, **+0.39%** over the
baseline's 6.7969, with VBlanks per frame falling 8.8276 -> 8.7931. **Cart free
goes 117,840 -> 482,160 B, 2.81% -> 11.50%** -- more headroom than the
pre-animation `=0` baseline ever had (409,888 B) -- which is **364,320 B
recovered, not the ~495 KB R3 projected**, because the baked set is 104
evidence-derived ids rather than R3's estimated 38 (§1.4 prices the
difference and gives the curve for trimming it). **F does not relieve HWRAM
and never claimed to**: HWRAM free moves 10,488 -> **9,944 B**, i.e. the
staging code costs **544 B of `.text`**, and 4,528 B of LWRAM is spent
(92,704 -> 88,176 free). `verify-memory-map` is **OK**. On target, the reduced
bake's substitution is confirmed in guest memory -- unbaked id 29 reports
`frame_count 10` at `values_offset 76446`, byte-identical to baked id 28 --
and the bridge's last-resort fallback was **never used**: 239 evaluate calls,
0 fallback hits, with the route requesting only ids 114 and 195, both baked.

---

## 1. Task C — which animations are baked, and how they were chosen

### 1.1 The selection method

**Three tiers of evidence, in decreasing strength, and no guesses.**

1. **Measured from the accepted route.** R3 §2 read
   `sourceboot_mario_snapshot.animation_id` once per replay tick over ticks
   100–230 on the accepted build. The route requests exactly two ids:
   `MARIO_ANIM_IDLE_HEAD_LEFT` (195, 0xC3) ×22 and `MARIO_ANIM_RUNNING`
   (114, 0x72) ×109. This task re-measured the same field on the *new* build
   (§4.4) rather than assuming it carried over.

2. **Derived from the shipped action set.** Every `MARIO_ANIM_*` referenced by
   an action handler that the shipped sourceboot tuple can actually enter.
   The tuple is BOB (`SATURN_SOURCEBOOT_LEVEL_ID=9`, area 1) with live input,
   so the following are unreachable *today* and were excluded: water and
   swimming, cap powerups and flight, cannon, riding a shell, Hoot, Bowser,
   quicksand, lava and burning, tornado twirl, wire-net and ceiling hanging,
   doors and key/pocket cutscenes, credits, and everything in
   `mario_actions_cutscene.c`. Extracted mechanically from
   `src/game/mario_actions_{stationary,moving,airborne,object,automatic}.c`
   by mapping each `MARIO_ANIM_*` reference to its enclosing `act_*` handler.

3. **One forward-looking inclusion, stated as such.** The held-light-object
   family (16 ids) is not reachable in the *current* build because no
   grabbable object spawns — but **charter R2 is a Bob-omb**, and
   `act_picking_up`, `act_hold_walking` and `act_throwing` become reachable
   the moment one does. Baking them now costs 30,718 B and avoids re-baking
   as R2's first act.

**The result is 104 ids, not the ~38 R3 §8 estimated.** R3's 38 was an
estimate written before the action set was enumerated; the enumeration says
38 does not cover normal play in BOB. The task brief's own rule —
*"under-including is recoverable; a jarring wrong pose in normal play is the
failure that matters"* — resolves the difference in favour of coverage.
§1.4 gives the exact price of that choice and the curve for trimming it.

### 1.2 The ids kept

104 ids, 3,073 of the bank's 8,140 frames. The authoritative list is
`animation_selection.keep` in `tools/saturn/manifests/actors/mario.json`;
the tiers below are recorded there too, so the rationale travels with the
data.

| tier | what | ids |
|---|---|---|
| **T0** route-measured | idle-head-left, running | 114, 195 |
| **T1** ground locomotion | walk, tiptoe ×2, skid, stop-skid, turning ×2, pushing, sidestep ×2, crouch ×3, crawl ×3, stand-against-wall, idle-centre, idle-right | 15, 16, 72, 108, 126, 127, 128, 146, 150, 151, 152, 153, 154, 155, 188, 189, 196, 197, 202 |
| **T2** air | single/double/triple jump and their landings, backflip, side flip, general fall/land, ledge idle, long jump ×2 and its crouches, wall kick ×2, ground pound ×4, dive ×3, slide-kick ×3, air kick, butt slide ×2, rollout spins | 4, 17, 18, 19, 20, 51, 58, 59, 60, 61, 75, 76, 77, 78, 79, 80, 86, 87, 90, 111, 112, 136, 137, 140, 141, 143, 144, 145, 190, 191, 192, 193, 203, 204 |
| **T3** combat and damage | punch ×4, ground kick, breakdance, knockbacks ×6, ground bonk, land-on-stomach, fall-over-backwards | 1, 2, 44, 45, 102, 103, 104, 105, 106, 113, 116, 117, 123, 124, 138 |
| **T4** ledge and pole | ledge grab ×2, climb-down, pole idle/climb/grab ×4, handstand ×4 | 0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 28, 52 |
| **T5** misc reachable | star dance ×2, dying-on-back, A-pose, first person | 3, 14, 194, 205, 206 |
| **T6** held light object (R2) | pick up, idle/walk/run/slow-walk, jump/fall and their landings, throw ×2, place, slide variants | 22, 23, 24, 63, 64, 65, 66, 67, 68, 69, 70, 82, 101, 107, 110, 139 |

### 1.3 The other 105 ids are substituted, not deleted

**Every animation id keeps its record slot, enum name, source path and source
SHA-256.** The bank is still a complete 0–208 table with 209 records, 193
source files and its unchanged repository pin, and `legacy_differential`
still reports `exact: true` with 0 posed-vertex and 0 light-input mismatches
against both legacy banks (ids 197 and 72, both baked).

What changes is only an unselected id's **channel streams**: they are
replaced by those of the **nearest selected id, ties resolved to the lower
id**. The packer's existing blob deduplication then collapses all of those
into one shared copy, so a substituted id costs nothing beyond its 16-byte
record.

This was chosen over emitting empty (`frame_count == 0`) records — which the
runtime does fail closed on — for three reasons. It needs **no change to any
of the four format validators** or their contract tests, so it cannot destabilise
the S64B format. It fixes the degradation at bake time where it is
**inspectable in the report** rather than at runtime where it is not. And it
applies to **every** bank consumer — the meshlet path and the demo-render
path evaluate the same records the bridge does — instead of only to the
bridge.

Maximum substitution distance is **16 ids**. The worst cases are the swimming
block, which has no baked neighbour nearer than the crawl/turn animations:

```
  d=16  172 MARIO_ANIM_FLUTTERKICK      -> 188 MARIO_ANIM_TURNING_PART1
  d=16  171 MARIO_ANIM_SWIM_PART2       -> 155 MARIO_ANIM_START_CRAWLING
  d=15  173 MARIO_ANIM_WATER_ACTION_END -> 188 MARIO_ANIM_TURNING_PART1
```

**This is the honest weak spot of nearest-id substitution and it is recorded
rather than hidden: if water is ever ported, ids 158–180 must be baked, not
substituted.** They are unreachable in BOB, which is why they are not baked
now.

### 1.4 What it costs, and the curve for trimming it

| | baked ids | frames | compact channel bytes | recovered |
|---|---:|---:|---:|---:|
| full bake (accepted build) | 209 | 8,140 | 574,274 | — |
| T0 only | 2 | 102 | 6,804 | 567,470 |
| T0–T1 | 21 | 882 | 56,156 | 518,118 |
| T0–T2 | 56 | 1,518 | 104,726 | 469,548 |
| T0–T3 | 71 | 1,910 | 138,254 | 436,020 |
| T0–T4 | 83 | 2,327 | 161,408 | 412,866 |
| T0–T5 | 88 | 2,473 | 179,226 | 395,048 |
| **T0–T6 — shipped** | **104** | **3,073** | **209,944** | **364,330** |

**The recovery is 364,332 B of cart, not the ~495 KB R3 §8 projected, and
that shortfall is the direct price of covering 104 ids instead of 38.** The
`.s64b` payload goes 596,896 → 232,564 B. Trimming is a one-line edit to
`animation_selection.keep` plus a rebake; T6 alone is 30,718 B and T4 is
23,154 B if the owner wants them back.

---

## 2. The fallback — what shows when an unbaked id is requested

### 2.1 Specified behaviour

**Layer 1, at bake time (the one that actually runs).** A requested unbaked
id resolves through its own record to the nearest baked id's channel streams.
The evaluator sees a complete, valid animation: non-zero `frame_count`,
63 in-bounds channels. It **cannot fail**, so it cannot return a zeroed pose
and `vertex_count == 0`, which is what would have removed Mario for the
frame. The engine's `animFrame` is carried through unchanged and clamped into
the substitute's range by the evaluator's existing
`frame >= record.frame_count` guard, so the substitute **advances and wraps
like a real animation** rather than holding one pose.

**Layer 2, at runtime (defence in depth).** If
`sm64_saturn_actor_pose_evaluate_staged` fails for any reason at all — a
corrupt bank, a validation failure, an id beyond the table — the bridge
retries once with `MARIO_ANIM_IDLE_HEAD_CENTER` (197, always baked) **at the
requested frame**, and only publishes an invalid slot if that also fails.

**The first-request case is safe by construction.** Neither layer consults a
previous slot, a previous frame, or any cached state: the pose is computed
from immutable bank bytes plus the requested id and frame. The first
animation a session requests is handled identically to the thousandth, and
there is no interval during which a stale pose could be held.

**What it is not.** Nearest-*numeric*-id is not nearest-*semantic*-id. Within
the baked coverage this never fires; outside it, a substituted id plays a
neighbour's motion. §1.3 names the worst case.

### 2.2 Verification

| claim | how it was checked | result |
|---|---|---|
| all 209 ids are evaluable from the shipped bank | host sweep of every id at frame 0 and frame `n-1`, decoding all 63 channels through `decode_animation_channels` against the shipped `.s64b` bytes | **0 failures** |
| every substituted id aliases exactly its nearest baked id | host check of `values_offset`, `indices_offset` and `frame_count` against the recomputed nearest-with-lower-tie rule for all 105 | **0 failures** |
| the shipped bank still validates on target | `verify-actor-pose-bank` (target `saturn_actor_bank.c` + `saturn_actor_pose.c` compiled for host, run against `mario.s64b`) | **PASS** |
| the format contract still holds | `verify-actor-meshlets`, `python -m unittest tools/saturn/test_actor_bank*.py` (24 tests) | **PASS** |
| an unbaked id really aliases on the *target*, in guest memory | `r3_probe_cf.py` reads `sm64_saturn_complete_actor_probe` out of LWRAM: ids 29 (unbaked) and 28 (its expected donor) | **id 29 reports `frame_count` 10 at `values_offset` 76,446 — byte-identical to id 28** (§4.4) |
| the route never needed the layer-2 fallback | same probe: `fallback_hits` against `evaluate_calls` | **0 of 239** |
| the route stays inside the baked set | same probe: requested ids over ticks 100–240 cross-checked against `baked` in the bank report | **`{114: 119, 195: 22}`, both baked** |

---

## 3. Task F — what moved to LWRAM, and what did not

### 3.1 What moved

**The two per-frame-invariant bank slices: the vertex table and the joint
table.**

| slice | records | record size | bytes |
|---|---:|---:|---:|
| `sm64_saturn_actor_vertex_t` | 424 | 10 | 4,240 |
| `sm64_saturn_actor_joint_t` | 20 | 12 | 240 |
| | | | **4,480** |

which is R3 §5's figure exactly.

**The structural point is not the 4,480 bytes.** The actor bank is linked
into `.cart_rodata` — it lives in CART, on the A-bus — so
`sm64_saturn_actor_bank_vertex` and `sm64_saturn_actor_bank_joint` were
crossing that bus **444 times per frame** to re-read two tables that never
change for the life of the bank. HWRAM has 10,488 B free against a 7,936 B
floor while LWRAM has 92,704 B free. **We are starving in one pool while
another sits idle**, and the staging buffer is funded from the idle one.

Mechanically: `sm64_saturn_actor_pose_evaluate_staged` takes the two tables
as optional arguments, `sm64_saturn_actor_pose_evaluate` forwards `NULL,
NULL`, and the meshlet and demo-render callers are therefore unchanged. The
bridge stages once, at bank validation, into `.lwram_bss`, revalidating each
record as it copies; a failed staging pass leaves the flag at 0 and the
evaluator silently reads the bank as before.

### 3.2 What did not move, and why

- **The animation channel index and value streams (209,944 B).** Their
  *storage* is invariant but the *read is not*: `sample_channel` is addressed
  by `(animation_id, frame, channel)` and reads a different 63 values every
  frame. They also do not fit — 209,944 B against 92,704 B free LWRAM. Staging
  only the *active* animation's blob on an id change is a real follow-up, but
  it is not frame-invariant work and it is not this task.
- **`complete_actor_pose_slots` (8,560 B of HWRAM, the largest single
  consumer).** This is exactly the case the task warned about. The slots are
  written by the evaluator and read by the render worker **every frame**;
  LWRAM is 16-bit and slower than HWRAM, so moving them would trade 8,560 B
  of the tight pool for cycles on the hottest path in the feature. Left in
  HWRAM.
- **`complete_actor_bank` (the 180-byte view).** Invariant, but its fields
  are read on the per-frame path and 180 B does not move the constraint.
  Moving it would be a pure loss.

### 3.3 F does not relieve HWRAM — a correction to the task premise

The task framed both changes as buying HWRAM headroom back. **C recovers
cart; F recovers neither.** R3 §8's own table is right: option F's HWRAM-free
column is `10,488 (+4,480 B of 92,704 LWRAM)` — unchanged HWRAM, LWRAM spent.
F is a *cycles* option funded by idle LWRAM, and it costs a little HWRAM
`.text` for the staging code. The measured HWRAM figure is in §4.1 and it is
reported as it came out, not as it was hoped.

---

## 4. Measurements

### 4.1 Memory map

```
verify-memory-map: e2-bob-identity-id-8e643f9cc3e9fdee
  ___end          = 0x060FD928
  hwram_remaining = 0x26D8 bytes (required >= 0x1F00)
  lwram_end       = 0x002EA790
  lwram_remaining = 0x15870 bytes (floor >= 0x4000)
  RESULT          = OK
```

| | baseline `id-36ebaa12184cd661` | this build | delta |
|---|---:|---:|---:|
| `___end` | 0x060FD708 | 0x060FD928 | +544 B |
| **`hwram_remaining`** | **0x28F8 = 10,488 B** | **0x26D8 = 9,944 B** | **-544 B** |
| margin over the 0x1F00 (7,936 B) floor | 2,552 B | **2,008 B** | -544 B |
| **`lwram_remaining`** | **0x16A20 = 92,704 B** | **0x15870 = 88,176 B** | **-4,528 B** |

**The HWRAM figure is F's real cost and it is a cost, not a saving.** 544 B of
`.text` for `sm64_saturn_actor_pose_evaluate_staged`'s extra arguments and the
staging pass. The 4,528 B of LWRAM is 4,240 (vertices) + 240 (joints) + 48
(the probe), exact.

**Checked against the symbol table, not the source** — `--gc-sections` is on:

```
00200048 00000030 B _sm64_saturn_complete_actor_probe        LWRAM,   48 B
00200078 000000f0 b _complete_actor_staged_joints            LWRAM,  240 B
00200168 00001090 b _complete_actor_staged_vertices          LWRAM, 4240 B
060de0b8 00002170 d _complete_actor_pose_slots               HWRAM, 8560 B  (deliberately not moved)
060711e8 00000698 T _sm64_saturn_actor_pose_evaluate_staged  HWRAM, 1688 B
22751814 00038c74 T _sm64_saturn_mario_actor_bank_data       CART, 232564 B
```

Two things that table proves and the source cannot. The staged arrays are at
`0x0020xxxx`, i.e. **actually in LWRAM** and not silently relocated. And
`_sm64_saturn_actor_pose_evaluate` — the non-staged wrapper — **is absent from
the link**: `--gc-sections` dropped it because nothing in this tuple calls it
any more, which is direct evidence that the per-frame path really is taking
the staged entry point. The bank symbol at `0x22751814` confirms both its CART
residency, which is the whole premise of F, and its reduced 232,564-byte size.

### 4.2 Cart

| | baseline `id-36ebaa12184cd661` | this build | delta |
|---|---:|---:|---:|
| `mario.s64b` payload | 596,896 | **232,564** | -364,332 |
| `SOURCE.DAT` / `.cart_rodata` | 4,076,464 | **3,712,144** | **-364,320** |
| **cart free of 4,194,304** | **117,840 (2.81%)** | **482,160 (11.50%)** | **+364,320** |
| ISO | 5,480,448 | 5,117,952 | -362,496 |

For scale: the pre-animation `=0` baseline `id-28b8cb0e531f8476` had 409,888 B
(9.77%) free. **This build has 72,272 B more cart headroom than the port had
before animated Mario existed, while keeping animated Mario.**

### 4.3 Cadence

`capture_sourceboot_throughput.py`, `summarize_cadence` only, 30 presentation
events / 29 intervals, **caches off** (`sh2_cache_emulation: false`),
on-target identity **MATCH** on both runs, `status: complete`.

| route tick | arm | mean | median | 1% low | VB/frame |
|---|---|---:|---:|---:|---:|
| 30 | `id-36ebaa12184cd661` (accepted baseline) | 6.8775 | 6.6667 | 6.6667 | 8.7241 |
| 30 | **`id-8e643f9cc3e9fdee` (C + F)** | **6.8775** | **6.6667** | 6.6667 | 8.7241 |
| | | **+0.00%** | unchanged | unchanged | unchanged |
| 150 | `id-36ebaa12184cd661` (accepted baseline) | 6.7969 | 6.6667 | 6.6667 | 8.8276 |
| 150 | **`id-8e643f9cc3e9fdee` (C + F)** | **6.8235** | **6.6667** | 6.6667 | 8.7931 |
| | | **+0.39%** | unchanged | unchanged | -0.0345 |

**Read the median, not the 1% low** (`w2-onepercent-low-recheck.md`): at n=29
the 1% low resolves to `rates[0]` on a `60/k` grid, so ±10% is its smallest
expressible step and it must never be quoted alone. It is unchanged at 6.6667
on all four runs, which is consistent with the median and carries no
independent information here.

**The tick-30 run is not merely close to the baseline, it is identical** —
same mean to four decimals and the same 8.7241 VBlanks per frame over the same
29 intervals. The interval histogram is the same shape at both ticks: tick 30
is 21×9VB + 8×8VB, tick 150 is 23×9VB + 6×8VB against the baseline's 24×9VB.

**What this does and does not establish.** C's zero cost was predicted
structurally — R3 §8 showed the animation count never enters the per-frame
path — and the route probe confirms the mechanism directly: the two ids the
route requests are both baked, and `fallback_hits == 0`, so the reduced-bake
code paths that could have cost anything did not execute. **The residual
attribution therefore belongs to F**, and it is at worst zero and at best a
0.39% gain at tick 150. **It is one grid step short of mattering**: R3 §9
established that anything returning less than ~0.59 VB/frame cannot move the
median off 6.6667, and 0.0345 VB/frame is not that. F should be kept because
it is free and it un-starves the tight pool structurally, not because it is a
frame-rate lever.

### 4.4 Route probe

`docs/saturn/evidence/reports/r3_probe_cf.py`, reading
`sm64_saturn_complete_actor_probe` out of guest LWRAM and
`sourceboot_mario_snapshot.animation_id` once per replay tick over ticks
100-240. `mem.peek` does not advance the machine. `status: complete`, all
eight checks pass.

```
probe_magic_ok                    true      probe struct was filled from the shipped bank
lwram_slices_staged               true      staged_slices == 1
bridge_fallback_never_used        true      fallback_hits == 0 of 239 evaluate_calls
target_bank_matches_report        true      target records == the bank report, all 4 probed ids
unbaked_id_has_frames_on_target   true      no probed id reports frame_count 0
unbaked_id_aliases_its_substitute true      id 29 values_offset == id 28 values_offset
route_ids_are_all_baked           true      {114: 119, 195: 22}, both baked
evaluate_ran                      true      239 calls
```

| probed id | | baked | target `frame_count` | target `values_offset` |
|---:|---|---|---:|---:|
| 29 | `MARIO_ANIM_CREDITS_WAVING` | **no**, substitutes 28 | 10 | **76,446** |
| 28 | `MARIO_ANIM_CLIMB_DOWN_LEDGE` | yes | 10 | **76,446** |
| 195 | `MARIO_ANIM_IDLE_HEAD_LEFT` | yes | 30 | 197,518 |
| 114 | `MARIO_ANIM_RUNNING` | yes | 72 | 128,110 |

**This is the fallback verified live, in guest memory, on the shipped
artifact.** An unbaked id does not read as absent on target: it reads as a
complete 10-frame animation whose value stream is byte-for-byte the one
belonging to its baked neighbour. That is the layer-1 behaviour of §2.1 doing
exactly what was specified, and it is why layer 2 never fired.

**The route is unchanged by the reduced bake**: the same two ids at the same
proportions R3 measured on the accepted build (`{195: 22, 114: 109}` over 131
ticks then; `{195: 22, 114: 119}` over 141 ticks here — the difference is the
longer span, not different behaviour).

### 4.5 Per-frame census

**Not completed within this task's budget, and stated as missing rather than
substituted.** `capture_idle_attribution.py --warmup-ticks 150 --windows 1
--window-steps 18000000 --slave-stride 16 --io-stride 20000
--vblanks-per-frame 8.7931` was started against
`id-8e643f9cc3e9fdee` and was still stepping when this report was written.
R3 §5 is empty for the same reason.

**It is not load-bearing for either conclusion here.** C's per-frame cost is
zero by construction and confirmed by the probe (`fallback_hits == 0`, both
route ids baked); F's is bounded above by the cadence measurement in §4.3,
which is the observable that actually decides. **The burst profiler was not
used as a substitute** — per the standing rule it is not admissible, and an
absent census is better than an inadmissible one.

---

## 5. Identity and hashes

| field | value |
|---|---|
| `effective_config_sha256` | `8e643f9cc3e9fdeee3669c2fac015603394157354bbbec38571ef76b9a79563b` |
| `identity_sha256` | `e39c81e8bf94d80702fe8e1559ff561fe633acf1e696f33e3cb15848ede330a2` |
| `source_closure_sha256` | `b638f78488bf0e73c5e2b86b8bbeb88e134ac223bcde2443dc5d1c2ab9eceed7` |
| `package_set_sha256` | `89b2dd28c448771aa8359379d27c0c1347188017b1f1b60e86b54dddc7f831ae` |
| ELF SHA-256 / size | `5d1980e674b921d8c7f3d89fe978ab21076a93fdae745ee960786d91032673fd` / 10,245,224 |
| ISO SHA-256 / size | `a8de4d89f24626460708ec304b16d5e1818ce2e7eecd3c873c32e5ac2f91d242` / 5,117,952 |
| `SOURCE.DAT` SHA-256 / size | `627275d5191f5236927c785fc704013fcd3575477dfceaea59901d934a3f6b1a` / 3,712,144 |
| `mario.s64b` SHA-256 / size | `e86b5c14bc7694873264036e8dcba7f58c51bb29ef8d0658f74bde8e1aa9c8ae` / 232,564 |
| `provenance.git_revision` | `4efa7082` |
| **`closure_clean`** | **`true`** |
| profile / mode | `sourceboot-bob-demo` / `development` |
| package generation | **16** (was 15 — see §7) |

**The target profile did not move.** `git diff` against
`tools/saturn/profiles/sourceboot-bob-demo-v1.json` is empty;
`SCENE_PACKAGE_GENERATION` is not part of `release_config`, so the generation
bump does not touch the sealed tuple. The sealed identity changed only because
the bank bytes did.

Preserved at `releases/2026-08-22_r3-cf-product/id-8e643f9cc3e9fdee/`
(CUE, ISO, manifest, ELF, linker map, `SOURCE.DAT`). The accepted baseline
`releases/2026-08-22_r3-scoping-anim1/id-36ebaa12184cd661/` is untouched, and
a pre-build snapshot of it is at
`releases/2026-08-22_1145_r3-cf-pre-build/id-36ebaa12184cd661/`.

**Note on the `.cue` hash.** It is `cdbf0bfa...`, which is also the historical
A9A rollback's CUE hash recorded in `HOWTO.md`. That is not a mix-up: the
`.cue` is an 88-byte track-layout text file that is identical for every build
with this layout. **It is not an identity signal — the ISO and ELF hashes
are.**

---

## 6. What remains

1. **The owner gate is open.** This is `live-observed`, not
   `owner-accepted`: cadence, memory, identity and the on-target probe are all
   measured, but no human has watched Mario move in this build. Per
   constitution rule 5, only `live-observed` or `owner-accepted` advances the
   milestone, and the visual check is the owner's.

2. **Mesh decimation is next and was deliberately not started.** It changes
   vertex counts, which would invalidate every figure above, and it compounds
   with this work: the pose slots are per-vertex, so halving 424 vertices
   returns ~2,968 B of the HWRAM that F did not. R3 §8 option G.

3. **HWRAM is still the binding constraint, and it got slightly worse.** 2,008
   B of margin over the link's own floor, down from 2,552. **The HUD does not
   fit in that**, and the HUD is R3's second half. Decimation (G) or an
   eviction of an existing HWRAM consumer to LWRAM — which still has 88,176 B
   free — has to happen before HUD work starts.

4. **`complete_actor_pose_slots[2]` is still a single static global.** 8,560 B
   for exactly two slots for exactly one actor. R2's Bob-omb cannot use this
   path as written. Pooling is R2-entry design work and was explicitly out of
   scope here; what this task adds to the record is that the array is now the
   single largest HWRAM item in the feature by a wide margin, and that it is
   the *right* item to attack next after decimation shrinks its per-slot size.

5. **The channel streams are the remaining CART traffic.** F moved the two
   invariant slices; `sample_channel` still crosses the A-bus 63 times per
   frame. Staging only the *active* animation's blob into LWRAM on an id change
   is the obvious follow-up — after the reduced bake, the largest single
   animation value stream is ~7 KB, which fits easily — but it is not
   frame-invariant work and it needs an invalidation rule, so it was not done
   here.

6. **If water is ever ported, ids 158-180 must be baked.** §1.3. They are the
   worst substitution distances in the shipped bank precisely because nothing
   near them is baked, and they are unreachable in BOB today.

7. **Two inherited degradations are unchanged and still owed to the owner**
   (both from R3 §9.5): Gouraud lighting is the 30-frame idle stream for every
   animation, and the mesh is one geo variant (normal cap, front eyes, open
   hand).

---

## 7. Reproduction

**Build** (PowerShell, `-j12`; the tuple is the accepted one, unchanged):

```powershell
$env:YAUL_INSTALL_ROOT='D:\Code\RetroDev\sm64-saturn-port\work\yaul-install'
cd D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\saturn-recovery
.\tools\saturn\with-msys-toolchain.ps1 sh --noprofile --norc -l -c 'source ../../.yaul.env; unset COMPILER_PATH; make -f Makefile.saturn.mk -j12 sourceboot SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=0 SATURN_FEATURE_SEMANTIC_AUDIO=1 SATURN_RENDERER_PIPELINE=4 SATURN_SOURCEBOOT_LEVEL_ID=9 SATURN_SOURCEBOOT_AREA_ID=1 SATURN_SOURCEBOOT_ROUTE_ID=0 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_LIVE_INPUT=1 SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_CAMERA_VARIANT=3 SATURN_ATAN2_VARIANT=2 SATURN_DIAGNOSTIC_MODE=0 SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0 SATURN_CART_MBIT=32 SATURN_SOURCE_CART_STAGE_SECTORS=8 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=2 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_DEMO_FRAGMENT_MODE=0 SATURN_DEMO_BSP_FRAGMENT_FLAT=0 SATURN_OBJECT_POOL_CAPACITY=208 SATURN_CAMERA_IDLE_START_TICK=0 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_FAST3D_Q16_TRACE=0'
```

**Two operational traps this task hit, recorded so the next one does not.**

1. **`$(SOURCEBOOT_ACTOR_BANK)` had no prerequisites**, so a changed bake list
   silently reused the stale `mario.s64b` and the link kept the old bank. Fixed
   in `2ca7abf2`; if you are on an older tree, delete
   `build/saturn/actors/mario/mario.s64b` before building.
2. **A new package generation could not bootstrap itself.** The
   identity-registry header listed `actors-v3-g<N>/actor-family-bundle.json` as
   a hard file prerequisite alongside the phony target that publishes it, so
   the first `g16` build died with "no rule to make target". Fixed in
   `4efa7082`; if you are on an older tree, run
   `make -f Makefile.saturn.mk compile-actor-family-bundle SCENE_PACKAGE_GENERATION=16`
   once first.

**Cadence** (per tick in {30, 150}):

```sh
.venv-saturn-tools/Scripts/python.exe tools/saturn/capture_sourceboot_throughput.py \
  --ymir "$YMIR" --ipl "$IPL" --game "$D/sm64-saturn-sourceboot-e2.cue" \
  --elf "$D/obj/sm64-saturn-sourceboot-e2.elf" \
  --release-manifest "$D/saturn-release-manifest-v1.json" \
  --startup-vblanks 4096 --warmup-ticks <TICK> --presentation-events 30 \
  --max-vblanks 3600 --timeout 3000 --output <out>.json
```

**Memory map:**

```powershell
.\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-memory-map `
  SOURCEBOOT_CANDIDATE_ELF=<abs path to the candidate ELF>
```

**Route/fallback probe** (kept beside this report, not promoted to
`tools/saturn/`, because it answers one question and is not part of the gate
surface):

```sh
.venv-saturn-tools/Scripts/python.exe docs/saturn/evidence/reports/r3_probe_cf.py \
  --tools "$PWD/tools/saturn" --ymir "$YMIR" --ipl "$IPL" \
  --game "$D/sm64-saturn-sourceboot-e2.cue" \
  --elf "$D/obj/sm64-saturn-sourceboot-e2.elf" --nm "$NM" \
  --bank-report build/saturn/actors/mario/mario-actor-bank.json \
  --output docs/saturn/evidence/reports/r3-cf-probe.json
```

**Attached evidence:**

```
docs/saturn/evidence/reports/r3-cf-cadence-tick30.json
docs/saturn/evidence/reports/r3-cf-cadence-tick150.json
docs/saturn/evidence/reports/r3-cf-probe.json
docs/saturn/evidence/reports/r3_probe_cf.py
build/saturn/actors/mario/mario-actor-bank.json   (animation_selection, per-id baked/substitute)
```

**Gates run:** `verify-memory-map` **OK**; `verify-actor-pose-bank` **PASS**;
`verify-actor-meshlets` **PASS**; `python -m unittest tools/saturn/test_actor_bank*.py`
**24 tests OK**; the full `sourceboot` target's in-line host suites **OK**
(27 tests, 2 skipped). Known pre-existing failures were not chased:
`verify-render-clusters`, `verify-sourceboot-presentation-boundary`,
`verify-vdp1-transfer-pipeline`, `verify-render-snapshot-bank`,
`verify-graph-q16-contract`, `test_gen_actor_identity_registry.py`,
`verify-tools`, `verify-bob-bsp-header`.
