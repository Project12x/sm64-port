# R2 entry — the rigid-part pose evaluator, and wiring despawn to release

- Date: 2026-08-22. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `d6657237`.
- Baseline is `id-0e0420373f40f11e` (the pool build): `hwram_remaining`
  **9,016 B**, `lwram_remaining` **76,864 B**, cart free **482,160 B**,
  cadence **6.8775 / 6.6667** at route tick 30 and **6.7969 / 6.6667** at tick
  150.
- **No emulator GUI.** All observation is headless Ymir.
- Governing principle: phase plan §2b — functional correctness beats numerical
  correctness, this port descends a generation, every subsystem is a
  degradation decision.
- Scope: items **1**, **2**, **4** and **5** of
  `r2-pose-slot-pooling.md` §8. Distance-ordered claiming (item 3), deleting
  the evidence scaffolding (item 7), the generic actor closure (item 6) and
  level loading are explicitly **not** built.

---

## 0. The answer, in one paragraph

**24 bytes per part, not 48 — and the measurement says the quantisation is
free.** The published rigid-part pose is a 3x3 rotation in Q1.15 plus an int16
translation triple; composition still happens in the existing Q16.16 workspace
with the already-proven `sm64_saturn_matrix_mul`, and the host fixture measures
the error the published form adds over that composition as **zero** — 2.890
units of divergence from a double-precision oracle on a deliberately oversized
1,000-unit probe vertex at hierarchy depth 5, and **2.890 units for the
Q16.16 composition it quantises**. The last 15 bits of rotation precision buy
nothing, so the class plan is re-derived at 256 B / 640 B and the cold arena
shrinks from 10,240 B to 7,168 B, returning **3,072 B of LWRAM**. The owner key
needed no invention: the runtime already publishes
`instance_key = (incarnation << 16) | pool_slot`, and that composite is exactly
stable across both in-flight generations and exactly unique across pool
recycling — the proof is in §3. `release_owner` is now reached from the geo
observer's live-to-dead edge, and on target that edge fired **13 times over the
route**, so the leak was real and is closed. **The four figures:
`hwram_remaining` 8,760 B (-256), `lwram_remaining` 79,920 B (+3,056), cart
free 480,320 B (-1,840), cadence 6.8775 / 6.6667 at tick 30 — identical to the
baseline — and 6.8235 / 6.6667 at tick 150, which is +0.39% and recovers the
one interval the pool build lost.** Mario is unchanged: zero mismatches on
every simulation field over route ticks 0-199. **The honest caveat is
residency: the new lane does not fit in HWRAM and is linked cold into cart
(§6.2), which is fine while it has no per-frame caller and is the first thing a
real enemy scene has to fix.**

---

## 1. The published layout decision, and what drove it

`r2-pose-slot-pooling.md` §2 sized the pool against **48 B per part** (a 3x4
Q16.16 matrix) and §8.2 left the decision open, noting that 12 B would shrink
the class plan and that "somebody has to make the call".

**The call is 24 B**, and the reasoning is in three parts.

### 1.1 What the consumer actually needs

A rigid part is a whole display list carried on one matrix. For a renderer to
draw it, it needs a transform it can concatenate with the view matrix — that is,
a **composed, model-space** transform, not a local one.

That is what rules out the 12 B form. Euler angles do not compose: a 12 B
published pose of `angle[3] + translation[3]` either forces the *consumer* to
walk the hierarchy every frame (which defeats the point of publishing a pose at
all), or forces the *producer* to decompose a composed rotation matrix back to
angles through an `asin`/`atan2` per part. The second costs more per part than
the 12 B it saves, on a machine with no FPU and no `asin` in the build. **12 B
was rejected on cost, not on precision.**

### 1.2 What precision is actually needed — measured, not asserted

A rotation entry lives in [-1, 1]. Q16.16 spends 16 fractional bits on it;
Q1.15 spends 15 and resolves it to 3.05e-5. The question is whether that last
bit is visible.

`verify-actor-rigid-pose` answers it directly. It composes a five-deep chain
with rotation and translation at every level, pushes four probe vertices —
including a deliberately oversized 1,000-unit one, far larger than any real
rigid part — through both the published Q1.15 form and the Q16.16 matrices it
was quantised from, and compares each against an independent double-precision
composition:

```
layout penalty on a 1000-unit probe at depth 5:
  published Q1.15        2.890 units
  Q16.16 composition     2.890 units
```

**The published form adds nothing.** The 2.890 units are the fixed-point
composition and the s16 sine table, which a 48 B layout would carry
identically. The fixture asserts this as a gate
(`worst_published <= worst_q16 + 1.0`), so a future layout change that starts
costing real precision fails the build rather than passing quietly.

Scaled to a real actor — a Goomba is on the order of 100 units, not 1,000 —
that is ~0.29 units of placement error, and §2b's actual test is whether it
*accumulates*: it does not. Every part is recomposed from the animation
channels from scratch each frame, so this is a fixed within-frame offset, which
§2b calls invisible, and never the swimming/creeping class, which §2b calls a
defect at any starting size.

### 1.3 The published form

```c
typedef struct sm64_saturn_rigid_part {
    int16_t rotation[9];    /* row-major 3x3, Q1.15 */
    int16_t translation[3]; /* model space, actor units */
} sm64_saturn_rigid_part_t;             /* 24 B, 4-byte aligned */

typedef struct sm64_saturn_rigid_pose {
    uint16_t animation_id, frame, frame_count, part_count;
    int16_t root_translation[3];
    uint16_t flags;                     /* 16 B header */
    sm64_saturn_rigid_part_t part[];
} sm64_saturn_rigid_pose_t;
```

`rotation` follows the same row-vector convention the skinned lane's
`matrix_apply` uses, so a consumer transforms a part-local vertex with

```
out[c] = ((v[0]*rot[0*3+c] + v[1]*rot[1*3+c] + v[2]*rot[2*3+c]) >> 15)
         + translation[c]
```

and never walks the hierarchy, because `translation` is already model space.

### 1.4 The re-derived class plan

| | 48 B/part (assumed) | **24 B/part (shipped)** |
|---|---:|---:|
| 20-part pose | 976 B | **496 B** |
| 24-part pose | 1,168 B | **592 B** |
| small class | 256 B = 5 parts | **256 B = 10 parts** |
| large class | 1,024 B = 21 parts | **640 B = 26 parts** |
| cold arena (8 + 8 blocks) | 10,240 B | **7,168 B** |

Sixteen blocks is unchanged — still eight enemies at two in-flight render
generations, which is what the pipeline depth demands, not an actor count. The
arena is 3,072 B smaller and `lwram_remaining` moved by exactly that much
minus the probe's growth (§6.1).

**Ceiling, stated:** `SM64_SATURN_RIGID_POSE_MAX_PARTS` is 24, which is what
the 640 B class holds. The `.s64b` bake for enemies does not exist yet, so
this is bounded from the raw geo counts (goomba 8, class median 14, king_bobomb
30 raw across several geo trees). A deeper actor is a **class-plan data edit**,
one line, not a redesign — but see §8 for the workspace ceiling that binds
first.

---

## 2. The evaluator

`src/port/saturn/gfx/saturn_actor_rigid_pose.{h,c}`. **A second lane, not a
replacement — Mario's skinned path is untouched** (§5).

Two entry points, deliberately split:

- **`sm64_saturn_actor_rigid_pose_compose(joints, part_count, angles,
  root_translation, workspace, out, out_bytes)`** — the pure core. It takes a
  joint hierarchy and one frame of s16 Euler samples, and knows nothing about
  banks. This is what the host fixture drives, which is why a known hierarchy
  can be checked against a known matrix without synthesising a bank file.
- **`sm64_saturn_actor_rigid_pose_evaluate(bank, animation_id, frame,
  staged_joints, workspace, out, out_bytes)`** — samples the animation channels
  out of the actor bank and calls the core. Channels 0-2 are the root
  translation and `3 + joint*3 + axis` are the per-joint Euler samples, exactly
  as the skinned lane reads them, because it is the same bank format and the
  same animation stream. `staged_joints` is the same CART-avoidance staging the
  skinned lane already takes.

**Composition reuses proven code and adds no new numeric path.** Each part's
local matrix comes from `sm64_saturn_mtxq_rotate_xyz_and_translate` and is
composed with its parent by `sm64_saturn_matrix_mul` — the same two functions
the skinned evaluator uses, in the same order (`mul(&local, &parent, &out)`,
which under the row-vector convention means apply local then parent). The only
new arithmetic in the whole lane is the one-line Q16.16-to-Q1.15 halving with a
saturating clamp.

**The workspace is shared, not new.** `r2-pose-slot-pooling.md` §2.1 established
that the 1,280 B joint workspace is per-*evaluator*, not per-*slot*, and that
there is exactly one evaluator lane — the master sim tick. The rigid lane takes
its composition workspace as a caller argument and the bridge hands it the same
`complete_actor_joint_workspace`, because the two calls are sequential within
one tick and neither publishes it. **Zero additional HWRAM data.**

**Fail-closed everywhere.** A NULL output, an undersized block, more parts than
the ceiling, a forward parent reference, an out-of-range animation id, or a
bank with more joints than the layout holds all clear `flags` and publish
nothing — so a consumer sees an invalid pose, never a half-composed one. That
is constitution rule 6 at the smallest safe unit: one bad actor draws at its
last pose or its bind pose, it does not blank the frame.

---

## 3. The owner key, and why it is stable

`r2-pose-slot-pooling.md` §8.4 asked for "a stable key per *instance* that
survives a frame — the instance-queue slot id is the obvious candidate, but it
must be stable across the two in-flight generations or `find` will mis-match."

**No invention was needed. The runtime already computes it.**
`sm64_saturn_actor_instance_bank_capture` (`saturn_actor_instance.c:130`)
publishes

```c
destination->instance_key =
    ((uint32_t)observer->incarnation[source->pool_slot] << 16) | source->pool_slot;
```

where `pool_slot` is the source object-pool index and `incarnation` is bumped by
the geo observer on a dead-to-live edge. **The pose-pool owner key is the
instance key** — `saturn_actor_pose_owner.h` names that coupling as a call site
rather than leaving it a convention.

### 3.1 The stability proof

| property | why it holds | where |
|---|---|---|
| **Non-zero** | `incarnation` is bumped to at least 1 before a slot goes live and forced to 1 on wrap, so the high half is never 0. It can therefore never collide with `SM64_SATURN_POSE_POOL_OWNER_NONE`. | `saturn_geo_state_observer.c` `begin_object` |
| **Stable across both in-flight generations** | `incarnation[slot]` is written **only** inside `begin_object` and **only** when `live[slot]` is false. A continuously observed actor keeps `live[slot] == 1` across `end_frame`, so its key is byte-identical every frame it is alive. | same |
| **No simultaneous alias** | Two live actors have different `pool_slot`s: one object occupies one pool slot, and `seen[]` rejects a second observation of the same slot within a frame. | same |
| **No alias across time** | A respawn into a recycled slot sees `live[slot] == 0` and bumps `incarnation`, so the successor's key differs from the dead one's in the high half. | same |
| **The despawn edge names the departing actor** | `end_frame` fires the edge *before* any later `begin_object` bumps the slot, so the release always names who left, never who arrives next. | `end_frame` |

All five are asserted on the host, driving the **real observer**, not a model:
`test_key_is_stable_while_alive` walks eight consecutive frames (four times the
pipeline's two in-flight generations) and requires one key throughout, then
despawns and respawns into the same slot and requires a different one;
`test_keys_do_not_alias_between_actors` requires eight simultaneous actors to
hold eight distinct keys.

### 3.2 The key space, partitioned structurally

```
0x00000000                       OWNER_NONE, reserved by the pool
any << 16 | slot,  slot <  240   a generic actor instance
any << 16 | slot,  slot >= 240   reserved: Mario, and the evidence scaffolding
```

The **low** half is the discriminator, and that choice is load-bearing.
`pool_slot < SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY` is checked in both
`begin_object` and `instance_bank_capture`, so it is the half a generic key is
*bounded* on. Reserving on the high half instead would not be sound —
`incarnation` is free to reach any 16-bit value, so a long-lived slot could
eventually mint a key colliding with a reserved tag.

**This found and fixed a live latent defect.** The pool build's synthetic
tenants used `0xE0000001`, `0xE0000002`, `0xE0000010 + n`, `0xE0000020`,
`0xE0000040`, `0xE0000050` — low halves of 1, 2, 16-23, 32, 64, 80, **all
inside the source pool slot range**. A real actor in pool slot 1 whose
incarnation reached 0xE000 would have named the scaffolding's block. Those
constants now sit at low half 0xFF00 and up, and the fixture proves the
partition exhaustively: it sweeps **every one of the 65,535 reachable
incarnations** against four reserved ordinals and requires that none is ever
classified generic. Mario's `'MARO'` key (low half 0x524F) was already outside
the range; a `_Static_assert` in the bridge now says so, so nobody can silently
pick a Mario key that is not.

---

## 4. The leak, and the wiring that closes it

`release_owner` shipped tested and **uncalled** (`r2-pose-slot-pooling.md`
§8.5). The pool would have leaked one block per enemy per despawn until
exhaustion, and the symptom would have been enemies quietly going stiff — a
failure that looks like nothing.

**The edge already existed.** `sm64_saturn_geo_state_observer_end_frame` walks
all 240 pool slots and flips `live[slot]` to 0 for anything not seen this
frame, counting `despawned_count`. That transition *is* the despawn. It now
also calls an optional sink:

```c
typedef void (*sm64_saturn_geo_despawn_fn)(uint16_t pool_slot,
                                           uint16_t incarnation, void *context);
void sm64_saturn_geo_state_observer_set_despawn_sink(
    sm64_saturn_geo_despawn_fn sink, void *context);
```

The sink is handed the two halves rather than the composed key so the observer
does not have to depend on the pose pool. `saturn_actor_bridge.c` registers
`complete_actor_despawn_sink`, which composes the key and calls
`sm64_saturn_pose_pool_release_owner` on the cold pool.

**Two design points, stated rather than assumed.**

1. **Phase.** `end_frame` runs on the master inside
   `sourceboot_capture_render_snapshot`, after the authoritative source tick and
   before the frame's poses are published. That is the same phase a claim
   happens in, so the release never races a render consumer.
2. **No deferral.** The release is immediate, not deferred by a generation. The
   pool's generation check kills every outstanding handle to the block, so a
   render consumer still holding last frame's handle resolves NULL and skips
   the actor — which is the correct picture for an actor that is despawning,
   and it can never be shown wearing the next tenant's pose. Deferring would
   buy one frame of a corpse and cost a block.

**Known behaviour, recorded:** if the observer overflows its capacity in a
frame, a still-live actor is not seen, is treated as despawned, and re-claims
next frame under a *new* incarnation. That is safe — no aliasing — but it is
claim churn under pressure, and it is the kind of thing distance-ordered
claiming (§8, not built) would interact with.

### 4.1 The leak test

`verify-actor-pose-owner`, driving the **real** observer and the **real** pool:

- **40 cycles** of "spawn a wave of 6 enemies, each claims a pose slot sized
  from the real `SM64_SATURN_RIGID_POSE_BYTES`, each stripes its own block,
  each reads its own bytes back and each re-`find`s its own slot; then kill the
  wave."
- After **every** cycle, `live_count` must be back at baseline (0), every
  dead handle must stop resolving, and every dead key must `find` nothing.
- At the end: `peak_live_count == 6` — **one wave, not a creeping total** — and
  `claim_failures == 0`, i.e. the pool was never once short.
- `released_total == 240` — every one of the 6 x 40 claimed slots was released
  by its own despawn.

**And the defect is demonstrated, not just the fix.**
`test_unwired_pool_exhausts` runs the same 40 cycles with the sink
unregistered and requires that claims **do** start failing. If someone later
removes the wiring, that test fails too, so the fixture cannot pass on a pool
that never needed fixing.

`test_despawn_is_not_collateral` covers the other half: one of two tenants
despawns, and the survivor must still read back its own 32-byte pattern while
the departed handle is dead and the recycled slot's next tenant gets a new key.

---

## 5. Tests and mutations

### 5.1 Host gates — all PASS

| gate | result |
|---|---|
| `verify-actor-rigid-pose` | **PASS** (new) — includes a real-data arm against the shipped `mario.s64b` |
| `verify-actor-rigid-pose-mutation` | **PASS** — 3 of 3 caught (new) |
| `verify-actor-pose-owner` | **PASS** (new) |
| `verify-actor-pose-owner-mutation` | **PASS** — 3 of 3 caught (new) |
| `verify-actor-pose-pool` | **PASS** (unchanged) |
| `verify-actor-pose-pool-mutation` | **PASS** — 3 of 3 (unchanged) |
| `verify-memory-map` | **OK** |

The rigid fixture checks, against two oracles: the Q1.15 conversion at its
boundary values (+1.0 saturates rather than wrapping); the documented rotation
formula written out longhand in the fixture for four single-part cases,
including exact identity; a five-deep parent/child chain against an independent
double-precision composition through four probe vertices; that rotating the
root actually moves the deepest child; that two actors evaluated back to back
each keep their own bytes; the degenerate cases (**zero parts** publishes a
valid empty pose, **one part** composes into an exactly-sized block); and six
fail-closed rejections. The real-bank arm evaluates the shipped 20-joint
hierarchy through the rigid lane, requires one part per bank joint, requires
that two frames of a real animation differ, and requires that an out-of-range
id and a header-only block are both refused.

### 5.2 Mutations — 6 of 6 caught, 0 escaped

| mutation | what it breaks | result |
|---|---|---|
| `RIGID_POSE_MUTATE_TRANSPOSE` | a part matrix is published transposed | **caught** — exit 1, 21 failing assertions, first at `z+90 rotation[1] (got -32768, want 32767)` |
| `RIGID_POSE_MUTATE_SKIP_PARENT` | parent composition is skipped; every part publishes its local transform | **caught** — exit 1, 9 failing assertions, first at `chain part 1 probe 0 axis 0 off by 477.397` |
| `RIGID_POSE_MUTATE_ALIAS` | two actors' evaluations land on the same bytes | **caught** — exit 1, 62 failing assertions |
| `GEO_MUTATE_NO_DESPAWN` | the despawn edge never reaches the sink | **caught** — exit 1, 755 failing assertions, first at `the despawn edge fires exactly once` |
| `GEO_MUTATE_STICKY_INCARNATION` | a recycled slot reuses the dead actor's incarnation, so the key aliases across time | **caught** — exit 1, 2 failing assertions, first at `a respawn into a recycled slot mints a different key` |
| `GEO_MUTATE_LATE_DESPAWN` | the edge reports incarnation+1, so `release_owner` misses by one | **caught** — exit 1, 753 failing assertions, first at `the live slot count returns to baseline after a wave (got 6, want 0)` |

The three required by the brief — transpose a part matrix, skip parent
composition, let two actors' evaluations alias — are the first three, and each
fails.

### 5.3 On target — `id-c7ef9bd81c762a6d`

`docs/saturn/evidence/reports/r2_probe_rigid.py`, read out of guest LWRAM over
route ticks 100-200. **All 21 checks pass; `status: complete`.**

```
rigid_selftest_mask               0x3F    all 6 rigid-lane checks passed
  claimed                         true    a cold block was granted for the pose
  evaluated                       true    the real bank composed into it, flagged valid
  part_count_matches_bank         true    one published part per bank joint
  frames_differ                   true    two frames of a real animation are not the same pose
  undersize_refused               true    a block one byte short is refused, not overrun
  released                        true    the scaffolding block was handed back

rigid_part_bytes                  24      the published layout, on target
rigid_pose_bytes                  496     20 parts + header; fits the 640 B class

despawn_edges                     13      the lifecycle wiring is LIVE and fired 13 times
despawn_releases                  0       nothing generic holds a cold block yet (see below)

pool_selftest_mask                0x7FF   all 11 pool checks still pass
pool_cold_arena_bytes             7168    the re-derived plan, on target
pool_cold_slot_count              16      8 x 256 B + 8 x 640 B
pool_cold_peak_live               16      the whole arena was let at once
pool_alias_faults                 0       <-- still the whole point
pool_frames_checked               199
evaluate_calls                    199     one check per evaluation, no gaps
fallback_hits                     0       the R3 bank fallback still never fires
staged_slices                     1       R3-F's LWRAM staging still live
```

**`despawn_edges == 13` is the load-bearing number.** It proves the observer is
running, objects are being observed and despawned in the *product* tuple, and
the release path is genuinely reached — so the leak `release_owner` was written
to prevent was a real defect on a real route, not a hypothetical. **13 despawns
in 200 ticks is roughly one every fifteen ticks**; against a 16-block arena the
pool would have been exhausted inside the first thirty seconds of a level once
enemies claimed.

`despawn_releases == 0` because nothing generic claims yet
(`dynamic_actor_closure = 0`, §8). The sink is called, computes the key, and
correctly finds nothing to free. That is the honest reading and it is stated
here rather than dressed up.

`rigid_part_count == 0` in the probe is **not** a failure: the last evaluation
the boot exercise performs is the deliberate undersize refusal, which fails
closed and clears the header. `part_count_matches_bank` in the mask records
the successful evaluation that preceded it.

---

## 6. Measurements

### 6.1 Memory

```
verify-memory-map: e2-bob-identity-id-c7ef9bd81c762a6d
  ___end          = 0x060FDDC8
  hwram_remaining = 0x2238 bytes (required >= 0x1F00)
  lwram_end       = 0x002EC7D0
  lwram_remaining = 0x13830 bytes (floor >= 0x4000)
  RESULT          = OK
```

| | baseline `id-0e0420373f40f11e` | this build | delta |
|---|---:|---:|---:|
| `___end` | 0x060FDCC8 | 0x060FDDC8 | +256 B |
| **`hwram_remaining`** | **9,016 B** | **8,760 B** | **-256 B** |
| margin over the 7,936 B floor | 1,080 B | **824 B** | -256 B |
| **`lwram_remaining`** | **76,864 B** | **79,920 B** | **+3,056 B** |
| **cart free of 4,194,304** | **482,160 B (11.50%)** | **480,320 B (11.45%)** | **-1,840 B** |
| `SOURCE.DAT` | 3,712,144 B | 3,713,984 B | +1,840 B |
| ISO | 5,117,952 B | 5,120,000 B | +2,048 B |

**HWRAM cost 256 B and every byte is the despawn path**, which is per-frame
code and belongs in fast memory: `_complete_actor_despawn_sink` 76 B,
`_sm64_saturn_geo_state_observer_set_despawn_sink` 20 B, `_s_despawn_sink` 4 B
of data, and the balance in the observer's `end_frame` edge test and the
bridge's bring-up.

**LWRAM gained 3,056 B**: the cold arena shrank 3,072 B (10,240 to 7,168) and
the probe grew 12 B for the six new counters, with the remainder alignment.
The exact figure the smaller part layout paid for itself with.

**Cart lost 1,840 B**, which is the rigid lane's cold text (§6.2).

### 6.2 Residency — the honest finding

**The rigid lane does not fit in HWRAM at today's margin.** The first link with
it in ordinary `.text` was refused outright:

```
sh-elf-ld: HWRAM margin below libyaul's TLSF control-block floor: the heap
libyaul builds at ___end would overrun the top of HWRAM. Shrink a static
HWRAM consumer.
```

The cause is **duplication, not bloat**.
`sm64_saturn_mtxq_rotate_xyz_and_translate` (six trig lookups, eleven Q16.16
multiplies) and `sm64_saturn_matrix_mul` (a 4x4 multiply with per-term overflow
checking) are `static inline` in headers, so a second translation unit that
composes matrices gets its own copy of both. The skinned lane already has one,
and HWRAM had roughly a kilobyte of margin, not two.

So the lane is annotated `SM64_SATURN_CART_COLD` and linked into
`.cart_cold_text`. **That is correct today and wrong tomorrow, and both halves
are stated.** Today the lane has no per-frame caller — nothing spawns — and its
only caller is the boot-time exercise, for which cart residency is exactly
right. The moment a real enemy is wired to it per frame, the annotation must
come off and roughly 1.8 KB of HWRAM has to be found.

**Note the arithmetic that follows from this, because it changes the plan.**
`r2-pose-slot-pooling.md` §8.7 identified deleting the pool's evidence
scaffolding as ~1.1 KB of recoverable HWRAM. **That is not enough on its own.**
The measured lane is ~1.7 KB of code plus the 824 B of margin now standing:
`1,698 + 824 = 2,522`, against `1,100` recoverable. A real enemy scene needs
either a second HWRAM source or — much more likely the right answer — the
inline duplication removed by giving both lanes one out-of-line
`rotate_xyz_and_translate` and one out-of-line `matrix_mul`, which would pay
for itself twice over. That refactor touches Mario's evaluator and was
therefore **not** attempted here (§5, byte-identical requirement).

### 6.3 Symbol table — checked, not inferred

`--gc-sections` is on. Verified against the linked ELF with `sh-elf-nm`:

```
2274ebe8 00000338 T _sm64_saturn_actor_rigid_pose_compose   CART,  824 B
2274ef20 000001d8 T _sm64_saturn_actor_rigid_pose_evaluate   CART,  472 B
2274eb5e 0000001a T _sm64_saturn_rigid_q15_from_q16          CART,   26 B
2274e118 00000178 t _complete_actor_rigid_selftest.constprop CART,  376 B
0606f050 0000004c t _complete_actor_despawn_sink            HWRAM,   76 B
06076d74 00000014 T _..._geo_state_observer_set_despawn_sink HWRAM,   20 B
060f34fc 00000004 d _s_despawn_sink                         HWRAM,    4 B
060720dc 0000008c T _sm64_saturn_pose_pool_release_owner    HWRAM,  140 B
0020121c 00001c00 b _complete_actor_cold_arena              LWRAM, 7168 B
00202e1c 0000020c b _complete_actor_cold_pool               LWRAM,  524 B
00200048 00000054 B _sm64_saturn_complete_actor_probe       LWRAM,   84 B
060dea48 00001770 d _complete_actor_hot_arena               HWRAM, 6000 B
060e01b8 00000500 d _complete_actor_joint_workspace         HWRAM, 1280 B
```

Six things this proves that the source cannot.

1. **The rigid lane survived `--gc-sections`** — all four of its symbols are in
   the link, so the evaluator is real code that something reaches, not a header
   that compiles.
2. **It is at `0x2274xxxx`** — genuinely in cart, which is the §6.2 claim
   measured rather than intended. Its four symbols total **1,698 B**.
3. **`_complete_actor_despawn_sink` is at `0x0606xxxx`** — genuinely in HWRAM.
   The per-frame half of this work is in fast memory and the boot-only half is
   not, which is the residency split done deliberately.
4. **`_sm64_saturn_pose_pool_release_owner` is present *and now reachable from
   the object lifecycle*.** In the baseline it survived the link only because
   the pool self-test called it. That is the difference between "tested" and
   "wired", and it is visible here.
5. **The cold arena is 0x1c00 = 7,168 B at `0x0020xxxx`** — the re-derived class
   plan, really in LWRAM.
6. **`_complete_actor_hot_arena` is still 0x1770 = 6,000 B at `0x060dxxxx` and
   `_complete_actor_joint_workspace` is still 0x500 = 1,280 B.** Mario's two
   published poses did not move, did not resize, and did not gain a second
   workspace — the sharing claim of §2, checked in the symbol table.

### 6.4 Cadence

`capture_sourceboot_throughput.py`, `summarize_cadence` only, 30 presentation
events / 29 intervals, `status: complete` on both runs.

| route tick | arm | mean | median | 1% low | VB/frame | interval histogram |
|---|---|---:|---:|---:|---:|---|
| 30 | `id-0e0420373f40f11e` (baseline) | 6.8775 | 6.6667 | 6.6667 | 10.6552 | 2x9 + 6x10 + 21x11 |
| 30 | **`id-c7ef9bd81c762a6d`** | **6.8775** | **6.6667** | 6.6667 | 10.5862 | 3x9 + 6x10 + 20x11 |
| | | **+0.00%** | unchanged | unchanged | -0.069 | one interval moved 11 -> 9 |
| 150 | `id-0e0420373f40f11e` (baseline) | 6.7969 | 6.6667 | 6.6667 | 10.4828 | 4x9 + 7x10 + 18x11 |
| 150 | **`id-c7ef9bd81c762a6d`** | **6.8235** | **6.6667** | 6.6667 | 10.4483 | 3x9 + 10x10 + 16x11 |
| | | **+0.39%** | unchanged | unchanged | -0.034 | two intervals moved 11 -> 10 |

**Read the median, not the 1% low** (`w2-onepercent-low-recheck.md`): at n=29
the 1% low resolves to `rates[0]` on a `60/k` grid, so ±10% is its smallest
expressible step. It is 6.6667 on all four runs and carries no independent
information.

**Tick 30 is identical on the mean and the median.** Tick 150 is **+0.39%**,
which recovers exactly the interval the pool build lost — the baseline moved
from the pre-pool `6.8235` down to `6.7969`, and this build is back at
**6.8235**. The honest reading is that this is the same one-interval quantum
moving back, not a demonstrated speed-up; the median is 6.6667 on every arm and
the accepted FPS floor is nowhere near being touched. The plausible mechanism is
the smaller cold arena (7,168 B against 10,240 B) changing LWRAM layout and
pool-control locality, but no arm was built to isolate it, so it is stated as
an observation and not a claim.

### 6.5 Mario is unchanged

**Method: the W2 route-timeline comparison.** `capture_route_timeline.py` on
both sealed builds, one headless boot each, walking the deterministic replay
route from boot to tick 200 at a 3-VBlank fine stride — **613 samples on each
arm**, reading per sample `gMarioState`'s action, all three face angles,
position, velocity, forward velocity and floor height; the route input the
runtime actually applied; `gGlobalTimer`; and the Mario draw-slot counters.

**Result: over route ticks 0-199, 200 ticks compared, ZERO mismatches on every
Mario simulation field and every applied route input.**

| field | ticks differing (of 200) |
|---|---:|
| `action` | **0** |
| `face_angle[0..2]` | **0** |
| `pos[0]`, `pos[1]`, `pos[2]` | **0** |
| `vel[0]`, `vel[1]`, `vel[2]` | **0** |
| `forward_vel` | **0** |
| `floor_height` | **0** |
| `global_timer` | **0** |
| `applied_buttons`, `applied_stick_x`, `applied_stick_y` | **0** |

**The sampling convention, stated because it matters.** The 3-VBlank fine
stride puts 2-3 samples inside most ticks, so the comparison is over the
**settled (last) sample at each tick** — the post-advance state, which is the
only phase-stable choice when two arms have different frame cadence. Both arms
have an identical samples-per-tick distribution (1 tick with 25, 1 with 10, 181
with 3, 17 with 2, 1 with 1), so the sampling structure is the same and this is
not a selection that could hide a divergence.

**The naive first-sample comparison was also run and its differences were
examined rather than waved away.** It reports 34 ticks differing on `pos[0]`
and 21 on `vel[0]`/`forward_vel`, and every one of them is the same artifact:

```
tick 123  base pos[0] [-6539.279, -6528.602, -6528.602]
          cand pos[0] [-6528.602, -6528.602, -6528.602]
```

The candidate's first sample is a value the baseline **also visits at the same
tick** — the arms simply cross the tick boundary at different sub-tick phases,
and both settle on the identical value within the tick. That was checked
programmatically across every differing tick, not eyeballed on one. Only the
X-axis quantities differ, which are the fastest-changing along this route;
`action`, `face_angle`, `pos[1]`, `pos[2]`, `vel[1]`, `vel[2]`, `floor_height`
and `global_timer` are identical even under the naive comparison.

The Mario draw-slot counters (`s_actor_command_count`,
`s_actor_texture_count`) read 0 at every compared sample on **both** arms, so
they agree exactly and carry no signal at this stride.

**Conclusion: this is a second lane beside Mario and it is a second lane beside
Mario.** His pose bytes are the same bytes at the same size at the same
address (§6.3), produced by the same evaluator from the same staged inputs;
the probe confirms the R3 bank fallback still never fires and the R3-F LWRAM
staging is still live.

---

## 7. Identity and hashes

| field | value |
|---|---|
| build id | **`id-c7ef9bd81c762a6d`** |
| `effective_config_sha256` | `c7ef9bd81c762a6d6be2c50aebc5eda9ec7734eb95cce898661bd124bca563a2` |
| `identity_sha256` | `d8a2b1512c303be6bcfd04c42018ee6121f0c0a7070656d9053c3a7a8afe528c` |
| `source_closure_sha256` | `c90722dd87378a758b6326dc7be7fc345606171573a6433580dc22dd3a1d7bb7` |
| `package_set_sha256` | `89b2dd28c448771aa8359379d27c0c1347188017b1f1b60e86b54dddc7f831ae` (**unchanged**) |
| `target_profile_sha256` | `bccdd297fd7792d0bdcbc5d4d2594e34faaa1d219e4e745cc7a6cfa02348a45c` (**unchanged**) |
| ELF SHA-256 / size | `cd92949f4578c7703ba12c1ef415f98620f7a981ecee9381d0a5d5778e26a89a` / 10,277,528 |
| ISO SHA-256 / size | `8e730d45fd97e9122e19031e980ab8eaa95dbc79e655abe5edaa9a06981dbadb` / 5,120,000 |
| `SOURCE.DAT` SHA-256 / size | `65d29c117d95375d77cb23f7dc7b9439e7e4a1f4824e4a9685d6a13b9d1f25e2` / 3,713,984 |
| `.cue` SHA-256 / size | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` / 88 |
| profile / mode | `sourceboot-bob-demo` / `development` |
| package generation | 16 (unchanged) |
| **`closure_clean`** | **`false`** |

**The target profile did not move and no feature flag flipped.** `git diff`
against `tools/saturn/profiles/` is empty; the sealed tuple is byte-for-byte
the accepted one and the identity changed only because the source closure did
(three new translation units/headers, four edited files).

**`closure_clean` is `false`, and that is stated rather than hidden.** The
artifact was linked from a working tree that had not yet been committed. The
commit that carries this report contains exactly that source and nothing else,
and `source_closure_sha256` is content-derived, so a rebuild from the commit
produces the same closure hash and the same `effective_config_sha256`
(`closure_clean` and `git_revision` live under `provenance`, outside the sealed
tuple). It has not been re-linked to demonstrate that: the build budget went to
the measurements above, and a clean-closure relink with no new numbers would
have been the worse trade. **This is the same gap the baseline carries, and it
is not closed here.**

Preserved at `releases/2026-08-22_r2-rigid-product/id-c7ef9bd81c762a6d/`
(cue, ISO, manifest, ELF, linker map, `SOURCE.DAT`; 1,329 files). The accepted
baselines are untouched.

**Attached evidence**

```
docs/saturn/evidence/reports/r2-rigid-cadence-tick30.json
docs/saturn/evidence/reports/r2-rigid-cadence-tick150.json
docs/saturn/evidence/reports/r2-rigid-probe.json
docs/saturn/evidence/reports/r2-rigid-route-timeline-cand-c7ef9bd8.json
docs/saturn/evidence/reports/r2_probe_rigid.py
```

**Known pre-existing failures, not chased and not caused here:**
`verify-render-clusters`, `verify-sourceboot-presentation-boundary`,
`verify-vdp1-transfer-pipeline`, `verify-render-snapshot-bank`,
`verify-graph-q16-contract`, `test_gen_actor_identity_registry.py`,
`verify-tools`, `verify-bob-bsp-header`, `verify-actor-feature-off-wrapper`.

---

## 8. What a scene with real enemies still needs that this does not build

This is the part the owner needs, so it is specific and it is ordered by what
blocks what.

1. **A Bob-omb or Goomba `.s64b` bake. This is the blocker.** The rigid
   evaluator has no enemy to evaluate because **no enemy actor bank exists** —
   `mario.s64b` is the only bank in the tree, and the rigid lane was exercised
   against it precisely because there was nothing else. Everything below is
   downstream of this. Until an enemy is baked to a declared budget (W3), no
   amount of plumbing puts a Goomba on screen.
2. **The generic actor closure.** `features.dynamic_actor_closure = 0`, and
   `despawn_releases == 0` on target is the direct consequence: actors are
   observed and despawned (13 times), but none of them reaches the pose pool.
   The closure reports 13 unsupported required capabilities, twelve of them
   `GEO_CULLING_RADIUS`. **That, not the evaluator, is what gates enemies
   appearing.** Explicitly out of scope here.
3. **A consumer for the published rigid pose.** The layout and its consumer
   contract are defined and tested (§1.3), but no renderer reads it yet. The
   render worker reads `pose->vertices` for Mario; a rigid tenant needs a draw
   path that walks parts, transforms each part's display list by its published
   matrix, and emits. That is real work and it is not started.
4. **HWRAM for the lane, which is now a measured number, not a worry.** §6.2:
   the lane is ~1,698 B of cold text and the margin is 824 B. Deleting the
   pool's evidence scaffolding (~1.1 KB) is **not sufficient**. The right fix
   is almost certainly to de-duplicate the inlined
   `rotate_xyz_and_translate` / `matrix_mul` between the two lanes, which pays
   twice — but that touches Mario's evaluator and needs its own byte-identical
   proof.
5. **The shared workspace caps the lane at 20 parts, not 24.**
   `complete_actor_joint_workspace` is 20 Q16.16 matrices, so the bridge
   refuses a hierarchy deeper than that even though the published layout and
   the 640 B class hold 24. Lifting it costs 64 B of HWRAM per extra part
   (256 B for the last four). The class plan is already sized for 24; only the
   workspace is not.
6. **Distance-ordered claiming** (`r2-pose-slot-pooling.md` §8.3, deliberately
   not built). The pool is still first-come. The claim site is the natural
   place: whatever loop turns instance snapshots into pose claims should sort
   by `draw_distance_q16` — already carried on the snapshot — so that under
   pressure the *far* actors go stiff. Note the interaction with §4's
   observer-overflow churn: an actor that loses its slot to overflow re-claims
   with a new key, and under distance ordering it should re-claim in distance
   order too.
7. **The per-tick alias witness is still synthetic** and still holds two cold
   blocks. It proves the allocator, not a Bob-omb. It stays until a real tenant
   can prove the same thing, per the brief.
8. **Nothing here demonstrates an enemy on screen, and it cannot.** Stated
   plainly because the owner asked for enemies: with no enemy bank (1), no
   closure (2) and no rigid draw path (3), the framework beneath enemies is now
   complete and tested end to end — pool, owner identity, lifecycle release,
   and a producer that turns animation channels into part matrices — but the
   first enemy still needs items 1, 2 and 3, and item 1 is the one to do next.
