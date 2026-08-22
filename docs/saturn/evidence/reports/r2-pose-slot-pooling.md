# R2 entry — the actor pose-slot pool: what needs a slot, what it costs, where it lives

- Date: 2026-08-22. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `1024ba41`.
- Baseline is the owner-accepted `id-8e643f9cc3e9fdee`: **6.8775 / 6.6667 at
  route tick 30**, **6.8235 / 6.6667 at tick 150**, `hwram_remaining`
  **9,944 B**, `lwram_remaining` **88,176 B**, cart free **482,160 B**.
- **No emulator GUI.** All observation is headless Ymir.
- Governing principle: phase plan §2b — functional correctness beats numerical
  correctness, this port descends a generation, every subsystem is a
  degradation decision. Sized against a **target** BOB scene (Mario + a handful
  of enemies + coins and props), not against today's one-actor route.
- **Scope held:** no Bob-omb, no coins, no trees, no level loading. This is the
  plumbing beneath them and it must not block them.

---

## 0. The answer, in one paragraph

**Most props need no pose slot at all, and this is much smaller than feared.**
Of SM64's 130 actor directories, **74 contain no `GEO_ANIMATED_PART`** — coin,
tree, star, metal_box, thwomp, warp_pipe, mushroom_1up, koopa_shell,
springboard, exclamation_box, breakable_box and 63 more — and in the generated
**BOB closure only 8 of 86 records carry an animation table at all**. Coins are
a `GEO_SWITCH_CASE` over four flat display lists and trees are a single static
display list; both need a transform and a variant index, not a posed vertex
buffer. So the pool is small. The second finding is that **1,280 B of every
shipped Mario slot was scratch nobody reads**: the joint matrix workspace is
consumed inside the evaluator and never appears in
`sm64_saturn_actor_pose_view_t`, so it does not need to be per-generation.
Removing it from the slot and sharing one workspace shrinks Mario's HWRAM
footprint from 8,560 B to 7,280 B while he stays byte-identical, and that
recovered margin is what funds the pool. The allocator is one arena-agnostic,
generation-checked, size-classed slot pool used twice: a **hot HWRAM arena**
whose only tenant is Mario (so no number of enemies can starve him and no enemy
pays for his 2,968-byte skinned pose) and a **cold 10,240 B LWRAM arena** for
everyone else. Measured results are in section 6. **The four figures: `hwram_remaining` 9,016 B (-928 against the baseline, and section 6.1 accounts for every byte of it); `lwram_remaining` 76,864 B (-11,312); cart free 482,160 B, unchanged; cadence 6.8775 / 6.6667 at tick 30, identical to the baseline to four decimals and to the interval histogram, and 6.7969 / 6.6667 at tick 150, -0.39% on the mean with the median and the 1% low unchanged.** Mario is proven unchanged over 200 route ticks with zero mismatches on every simulation field.

---

## 1. Which actor classes need a pose slot — the census

A pose slot is the *published* geometry of one actor for one in-flight render
generation: the bytes a render consumer reads while the producer is already
evaluating the next one. An actor needs one only if its drawable geometry
changes per frame. That is decidable from the source geo, mechanically.

`GEO_ANIMATED_PART` is the discriminator: it is the node that binds a display
list to an animated joint (`src/engine/graph_node.h:36`,
`src/game/rendering_graph_node.c:1286`). No `GEO_ANIMATED_PART` means no joint
hierarchy means nothing to evaluate per frame.

| class | count of 130 | needs a pose slot | examples |
|---|---:|---|---|
| **A. Jointed** (`GEO_ANIMATED_PART` > 0) | **56** | **yes** | mario, bowser, goomba, bobomb, koopa, whomp, chain_chomp, amp, bully |
| **B. Billboard-only** (no jointed parts) | **4** | **no** | bomb, snufit, water_mine, yellow_sphere |
| **C. Rigid / static** (no jointed parts, no billboard) | **70** | **no** | **coin, tree, star, heart, metal_box, thwomp, koopa_shell, mushroom_1up, springboard, warp_pipe, exclamation_box, breakable_box, purple_switch, treasure_chest, wooden_signpost** |

**74 of 130 actors — 57% — need nothing from this pool.** They need a
transform, which the existing instance queue already carries
(`saturn_actor_instance.h`), and in some cases a variant index.

Two of the owner's named cases specifically:

- **Coin** — `actors/coin/geo.inc.c` is `GEO_SHADOW` + `GEO_SWITCH_CASE(8,
  geo_switch_anim_state)` over eight `LAYER_ALPHA` display lists (four rotation
  frames, doubled). The "animation" is a *display-list index* driven by
  `animInfo.animFrame`. **A coin's per-frame published state is one integer.**
- **Tree** — `actors/tree/geo.inc.c` has five variants, each
  `GEO_CULLING_RADIUS` + one `GEO_DISPLAY_LIST`. Fully static. (Note: that
  `GEO_CULLING_RADIUS` is one of the 13 unsupported required capabilities the
  task-11 generic-actor closure reported, so trees are blocked on closure
  support, not on pose slots.)

And the scene-level number that actually sizes the pool: the generated BOB
closure (`build/saturn/packages/bob/1/closure.json`, 86 records) carries an
`animation_table` on exactly **8**: `bhvBobomb`, `bhvBobombBuddy`,
`bhvBobombBuddyOpensCannon`, `bhvChainChomp`, `bhvGoomba`, `bhvKingBobomb`,
`bhvKoopa`, `bhvKoopaFlag`. Of the 86, **33 carry no material feature bits at
all**.

**Class A is not one shape either.** Mario is baked as a single 424-vertex
*skinned* mesh with a joint ordinal per vertex — a §2a *recreation*, not what
the N64 does. Every other jointed actor in SM64 is a **rigid-part hierarchy**:
each `GEO_ANIMATED_PART` is a whole display list carried by one matrix. A
Goomba has 8 animated parts; the class median is 14 against Mario's baked 20
joints and 336 raw geo parts. **A rigid-part actor's published pose is an array
of part transforms, not an array of posed vertices** — order 20 x 48 B = 960 B
at worst, against Mario's 2,968 B, and it does not grow with mesh density.

---

## 2. Sizing — fixed slots, tiers, or a byte pool

**Decision: a byte-extent pool partitioned at init into a small number of
fixed-size classes, with per-class free lists.**

The options and what each costs:

| option | cost |
|---|---|
| N Mario-sized fixed slots | 3,000 B each. A Goomba would pay 2,000 B for 960 B of matrices. Does not fit HWRAM and wastes most of LWRAM. Rejected. |
| Two hard tiers (big/small) | Cheap, but bakes today's two shapes into the type system; the third shape breaks it. Rejected as under-general for the cost saved. |
| True variable-extent allocation (first fit + coalescing) | No waste, but fragments under mid-scene spawn/despawn (coins collected, enemies killed and respawned), needs coalescing, and has no bounded worst case. Rejected: unbounded behaviour is worse than bounded waste on this machine. |
| **Size classes over one arena, partitioned at init** | **Chosen.** O(1) claim and release, *zero* fragmentation by construction, worst case fixed at init, waste bounded by the class step. The class plan is data, so re-tiering is a one-line edit, not a redesign. |

The extent a caller asks for is in **bytes**, so the allocator never learns
what a pose is. Skinned actors ask for `align4(v*6) + align4(v*1) + header`;
rigid-part actors will ask for `parts * 48 + header`. Both are just numbers.

**The shipped plans.**

| pool | class | bytes | blocks | total |
|---|---|---:|---:|---:|
| hot (HWRAM) | Mario skinned pose | 3,000 | 2 | 6,000 |
| cold (LWRAM) | small jointed pose | 256 | 8 | 2,048 |
| cold (LWRAM) | large jointed pose | 1,024 | 8 | 8,192 |
| | | | | **10,240 B of LWRAM** |

1,024 B holds a 20-part rigid pose with room for a header; 256 B holds a
5-part one, or a compact angle-only 20-part pose. Sixteen cold blocks is
**eight enemies at two in-flight generations each**, which is the "handful of
enemies" the owner named. 10,240 B is 11.6% of the 88,176 B of free LWRAM.

**The 2 in "2 slots" is pipeline depth, not actor count.** It matches
`sm64_saturn_render_snapshot_bank_t.slot[2]` — two render generations may be in
flight, so every animated actor needs two published poses. That is why a pool
is the right shape and a per-actor pair of statics is not: generation A and
generation B need not hold the same actor set, and a pool absorbs the
difference instead of reserving for the union.

### 2.1 The 1,280 bytes that were never published

`complete_actor_pose_slot_t` carried `int32_t joints[20*16]` — 1,280 B — per
slot. **Nothing downstream reads it.** `sm64_saturn_actor_pose_view_t`
(`saturn_actor_pose.h:21-30`) exposes vertices, light intensity, ids and the
root translation and no joint pointer; `sm64_saturn_mario_actor_pose_t`
exposes less still; and the render worker — master and slave, which split
Mario's vertices between them in `demo_transform_mario_range` — reads only
`pose->vertices` and `pose->light_intensity`. The joint matrices are
**workspace consumed inside the evaluator**.

They are therefore per-*evaluator*, not per-*slot*, and there is exactly one
evaluator: `complete_actor_evaluate` is reached only from the master's sim
tick, once per tick, and the diagnostic caller at `main.c:453` is behind
`SATURN_DIAGNOSTIC_MODE == 1` which the product tuple sets to 0. One shared
workspace is sufficient and safe. **This alone returns 1,280 B of HWRAM**, and
it is recorded in the header so that a future second evaluator lane knows it
costs 1,280 B, not zero.

---

## 3. Where the pool lives

**Two arenas, one allocator. Residency is the priority mechanism.**

`sm64_saturn_pose_pool_init` takes the arena as an argument and has no opinion
about where it is. That is the whole design of the residency split:

- **Hot arena — HWRAM, 6,000 B, one tenant: Mario.** R3-F already established
  the reason not to move him: the slots are written by the evaluator and read
  by *both* CPUs every frame, and LWRAM is 16-bit and slower. Nothing about
  that changed, so Mario's bytes stay exactly where they were and his per-frame
  access path is unchanged — the only difference is that the address now comes
  from a generation-checked resolve instead of a static array index, which is
  two loads and a compare, twice per frame.
- **Cold arena — LWRAM, 10,240 B, everybody else.** LWRAM has 88,176 B free
  against HWRAM's 9,944 with a 7,936 B floor. F proved `.lwram_bss` relocates
  cleanly (`0x0020xxxx`, symbol table verified) and this uses the same section.

**Separating the arenas *is* the priority policy**, and it is stronger than a
priority field would be: an enemy cannot starve Mario because an enemy can
never reach his arena, and Mario cannot be evicted because nothing in the
allocator evicts.

**The cycle cost of the cold arena is real and is stated, not hidden.** A
rigid-part enemy pose read out of LWRAM costs 16-bit accesses on a per-frame
path. The mitigation is that a rigid-part pose is ~960 B against Mario's 2,968,
and that a part matrix is read once per part rather than once per vertex — the
read is O(parts), not O(vertices). If measurement later says otherwise, the
fix is to move the nearest N enemies into a second hot arena, which is a plan
edit, not a redesign, because the allocator does not care.

**Memory record (vertical-slice rule).**

| | source | destination | max bytes | alignment | lifetime / owner |
|---|---|---|---:|---|---|
| hot arena | evaluator writes | HWRAM `.bss` | 6,000 | 4 | process; `saturn_actor_bridge.c` |
| joint workspace | evaluator scratch | HWRAM `.bss` | 1,280 | 4 | process; same |
| cold arena | pool tenants | LWRAM `.lwram_bss` | 10,240 | 4 | process; same |
| pool control x2 | allocator state | LWRAM `.lwram_bss` | 2 x ~540 | 4 | process; same |

Producer: the master sim tick. First real consumer: the render service via
`sm64_saturn_mario_actor_pose_from_selector`. Transport: none — the handoff is
an index inside the immutable render snapshot; no DMA crosses this boundary.
Failure atomicity: a claim that fails changes nothing; a resolve that fails
publishes nothing and the actor is skipped for that frame. Reset owner: bank
re-validation, which happens once.

---

## 4. Claim, release, exhaustion — and what degrades

`sm64_saturn_pose_pool_claim(pool, owner_key, extent_bytes)` pops the smallest
class that holds `extent_bytes`, widening to a larger class when the exact one
is empty. `release` returns the block and **bumps the slot's generation**, so
every outstanding handle to it dies immediately. `resolve` returns NULL for a
stale handle and counts it. `find(owner_key)` re-finds a live slot without
carrying a handle through an immutable snapshot. `release_owner(owner_key)` is
the despawn / scene-transition path.

**Handles are (index, generation), never pointers.** This is the single
defence against the failure this pool exists to prevent — two actors reading
each other's vertex data — and it is why the render snapshot carries an index
rather than an address: a generation that arrives late cannot reach a block
that has since been re-let.

**Exhaustion policy, as a §2b degradation decision:**

1. **A failed claim never evicts.** An actor that is already animating keeps
   animating; a newcomer is the one that goes without. This is deliberate: an
   enemy that pops into animation late is a non-event, whereas an enemy that
   *stops* animating because a coin spawned is a visible glitch.
2. **An actor with no slot still renders.** It draws at its last published
   pose, or at the bind pose if it never had one. It does not disappear:
   constitution rule 6 says degrade at the smallest safe unit, and a
   momentarily stiff Goomba is smaller than a missing Goomba.
3. **Never a wrong-actor pose.** The generation check makes that
   unrepresentable rather than unlikely.
4. **Claim order is the caller's policy, not the pool's.** The pool is
   first-come. The next wave must claim nearest-to-camera first, so the actors
   that lose animation under pressure are the distant ones. **This is not
   built** — see §8.
5. **Mario cannot be starved**, because he is the only tenant of a different
   arena (§3).
6. **Everything is counted.** `claim_failures`, `stale_resolves`,
   `peak_live_count` and `live_count` are live in the LWRAM probe, so pressure
   is observable rather than inferred.

---

## 5. Tests

### 5.1 Host fixture — `verify-actor-pose-pool`

`tools/saturn/actor_pose_pool_test.c`, C11 `-pedantic -Wall -Wextra -Werror`.
`verify-actor-pose-pool`: **all checks passed, exit 0**. The fixture partitions a 4,096 B arena into 3 x 64 + 2 x 256 + 2 x 512 = seven blocks over 1,728 B and exercises the whole surface.

The load-bearing case is checked by **writing a distinct byte pattern over the
whole of every live block and reading all of them back**, not by comparing
pointers — an overlap of one byte is destructive and observable.

| case | what it asserts |
|---|---|
| init contract | a well-formed plan is accepted; a descending, oversized, unaligned or NULL-arena plan is refused and leaves the pool unusable |
| claim | 40 B lands in the 64 B class, not a larger one |
| release | a released handle stops resolving, the stale resolve is counted, double release is refused |
| reuse | the freed block is immediately re-lettable, at the same bytes, with a new generation, and the old handle stays dead |
| find / release_owner | a live owner is found, a released one is not, a despawn releases exactly its own slot |
| **full-arena disjointness** | every planned block is claimed, filled with its own pattern, and **all of them still read back their own bytes** |
| exhaustion | a claim on a full pool returns the invalid handle, is counted, and **does not disturb any live tenant** |
| oversize | a request larger than every class fails |
| **three tenants across 16 frames** | three actors of three different sizes hold slots simultaneously, each rewriting its own slot every frame, and each reads back its own frame pattern every time |
| mid-session despawn | one tenant leaves; the survivors keep their bytes and the departed handle is dead |

### 5.2 Mutation check — `verify-actor-pose-pool-mutation`

Three mutations, each compiled in by define, each of which **must** make the
fixture fail:

| mutation | what it breaks | result |
|---|---|---|
| `MUTATE_CLAIM_REUSE` | `claim` hands back an already-held block instead of a free one | **caught** — exit 1, 41 failing assertions, first at the disjointness sweep |
| `MUTATE_SKIP_RELEASE` | `release` reports success without releasing | **caught** — exit 1, 16 failing assertions, first at "a released handle no longer resolves" |
| `MUTATE_ALIAS` | every slot resolves to the arena base, so two live tenants share bytes | **caught** — exit 1, 35 failing assertions, first at the disjointness sweep |

0 of 3 escaped.

### 5.3 Target exercise — the pool on the real SH-2

A host test proves the algorithm; it does not prove the allocator works in
LWRAM on a 16-bit bus. The build therefore runs the same sequence on target,
once at bring-up, and records the result in the LWRAM probe:

- hot and cold pools initialise; Mario claims both hot blocks; **his two
  generations are proven to be distinct memory**;
- two cold tenants of *different* classes are claimed and filled with
  complementary patterns and read back;
- the cold pool is drained to exhaustion — the next claim must be refused, and
  **both long-lived tenants must still hold their own bytes**;
- one block is released: its handle must stop resolving, and the re-let must
  succeed while the dead handle stays dead;
- an oversize claim must be refused;
- the fillers are released and the two tenants re-verified.

and then, **once per sim tick for the whole session**, both long-lived cold
tenants are re-striped with a tick-derived pattern and both are read back. A
single crossed byte would raise `pool_alias_faults`. Cost is two 32-byte
stripes and two 32-byte compares per tick.

**Result on `id-0e0420373f40f11e`, read out of guest LWRAM with
`docs/saturn/evidence/reports/r2_probe_pose_pool.py` over route ticks
100-200. All 17 checks pass; `status: complete`.**

```
pool_selftest_mask                0x7FF   all 11 boot checks passed
  hot_init                        true    hot pool partitioned in HWRAM
  cold_init                       true    cold pool partitioned in LWRAM
  mario_claimed                   true    both hot blocks granted to Mario
  mario_generations_disjoint      true    his two generations are distinct memory
  two_tenants_claimed             true    two cold tenants of two classes
  two_tenants_disjoint            true    each reads back its own pattern
  exhaustion_refuses              true    the claim past the end is refused
  exhaustion_harmless             true    both tenants intact after exhaustion
  release_kills_handle            true    a released handle stops resolving
  reuse_ok                        true    the block is re-let, old handle stays dead
  oversize_refused                true    0xFFFF bytes is refused

pool_ready                        1
pool_hot_live                     2       Mario, two generations
pool_hot_slot_bytes               3000    was 4,280 with the joint workspace in it
pool_cold_live                    2       the two long-lived synthetic tenants
pool_cold_peak_live               16      the whole cold arena was let at once
pool_cold_slot_count              16      8 x 256 B + 8 x 1024 B
pool_cold_arena_bytes             10240
pool_cold_claim_failures          2       exhaustion + oversize, both deliberate
pool_cold_stale_resolves          2       released handles rejected, as designed
pool_frames_checked               199
pool_alias_faults                 0       <-- the whole point
evaluate_calls                    199     one check per evaluation, no gaps
fallback_hits                     0       the R3 bank fallback still never fires
staged_slices                     1       R3-F's LWRAM staging still live
```

`pool_frames_checked == evaluate_calls == 199` is the check that the witness
actually ran on every evaluation rather than on some of them, and
`pool_alias_faults == 0` across all 199 is the on-target statement of the
property the host fixture proves structurally.

---

## 6. Measurements

### 6.1 Memory map

```
verify-memory-map: e2-bob-identity-id-0e0420373f40f11e
  ___end          = 0x060FDCC8
  hwram_remaining = 0x2338 bytes (required >= 0x1F00)
  lwram_end       = 0x002ED3C0
  lwram_remaining = 0x12C40 bytes (floor >= 0x4000)
  RESULT          = OK
```

| | baseline `id-8e643f9cc3e9fdee` | this build | delta |
|---|---:|---:|---:|
| `___end` | 0x060FD928 | 0x060FDCC8 | +928 B |
| **`hwram_remaining`** | **9,944 B** | **9,016 B** | **-928 B** |
| margin over the 7,936 B floor | 2,008 B | **1,080 B** | -928 B |
| **`lwram_remaining`** | **88,176 B** | **76,864 B** | **-11,312 B** |
| **cart free of 4,194,304** | **482,160 B (11.50%)** | **482,160 B (11.50%)** | **0** |
| `SOURCE.DAT` | 3,712,144 B | 3,712,144 B | 0 |
| ISO | 5,117,952 B | 5,117,952 B | 0 |

**HWRAM got 928 B worse and that is reported as it came out, not as it was
hoped.** The per-symbol accounting says exactly where it went, and it is worth
reading because it separates the allocator from the evidence scaffolding:

| symbol | baseline | this build | delta |
|---|---:|---:|---:|
| `complete_actor_pose_slots` | 8,560 | **absent** | **-8,560** |
| `complete_actor_hot_arena` | — | 6,000 | +6,000 |
| `complete_actor_joint_workspace` | — | 1,280 | +1,280 |
| `complete_actor_mario_handle` + `_exercise` + `_pools_ready` | — | 17 | +17 |
| **data subtotal** | **8,560** | **7,297** | **-1,263** |
| `sm64_saturn_pose_pool_init` | — | 340 | +340 |
| `sm64_saturn_pose_pool_claim` | — | 196 | +196 |
| `sm64_saturn_pose_pool_release_owner` | — | 140 | +140 |
| `sm64_saturn_pose_pool_release` | — | 108 | +108 |
| `pool_slot_for_handle` | — | 94 | +94 |
| `sm64_saturn_pose_pool_resolve` | — | 64 | +64 |
| `sm64_saturn_pose_pool_largest_free_extent` | — | 64 | +64 |
| **allocator `.text` subtotal** | — | **1,006** | **+1,006** |
| `complete_actor_stripe` + `_stripe_ok` + `_slot` | — | 172 | +172 |
| `sm64_saturn_mario_actor_pose` (inlines bring-up + selftest) | 840 | 1,904 | **+1,064** |
| `sm64_saturn_mario_actor_pose_from_selector` | 148 | 120 | -28 |

**Read it this way.** The allocator itself is close to memory-neutral: it
returns 1,263 B of data and spends ~1,050 B of code (1,006 B of pool plus
60 B of `complete_actor_slot`). What actually costs the 928 B is the **target
evidence scaffolding** — the +1,064 B that `sm64_saturn_mario_actor_pose` grew
by inlining `complete_actor_pools_bring_up` and `complete_actor_pool_selftest`,
the 112 B of `stripe`/`stripe_ok`, and the 204 B of `release_owner` +
`largest_free_extent` which exist in this link only because the selftest calls
them. **That is scaffolding by construction and §8.7 says to delete it when
real tenants arrive**, which returns roughly 1.1 KB and puts the margin back at
or above the baseline's 2,008 B. It is stated as an accounted-for estimate,
not as a measurement: no arm was built without it.

**LWRAM spend is exact**: 10,240 (cold arena) + 2 x 524 (pool control) + 24
(probe growth) = 11,312, matching the observed 11,312 to the byte. 76,864 B
remains free against a 16,384 B floor.

### 6.2 Symbol table — checked, not inferred

`--gc-sections` is on. ```
00201210 00002800 b _complete_actor_cold_arena         LWRAM,  10240 B
00203a10 0000020c b _complete_actor_cold_pool          LWRAM,    524 B
00203c1c 0000020c b _complete_actor_hot_pool           LWRAM,    524 B
00200048 00000048 B _sm64_saturn_complete_actor_probe  LWRAM,     72 B
00200180 00001090 b _complete_actor_staged_vertices    LWRAM,   4240 B  (R3-F, unchanged)
060de968 00001770 d _complete_actor_hot_arena          HWRAM,   6000 B
060e00d8 00000500 d _complete_actor_joint_workspace    HWRAM,   1280 B
06071d98 00000154 T _sm64_saturn_pose_pool_init        HWRAM,    340 B
06071eec 000000c4 T _sm64_saturn_pose_pool_claim       HWRAM,    196 B
```

Four things this proves that the source cannot.

1. **`_complete_actor_pose_slots` is gone from the link entirely.** The static
   array this task set out to remove is not merely unused; it does not exist.
2. **The cold arena and both pool control blocks are at `0x0020xxxx`** — really
   in LWRAM, not silently relocated, which is the same check R3-F had to make.
3. **The hot arena is at `0x060dxxxx`, in HWRAM, at 6,000 B** — Mario's two
   published poses did not move to slow memory, which was the explicit
   constraint carried forward from R3-F §3.2.
4. **`_sm64_saturn_pose_pool_find` and `_sm64_saturn_pose_pool_resolve_const`
   are absent.** `--gc-sections` dropped them because nothing in this tuple
   calls them yet. They are tested on the host and they are what the next wave
   needs (§8.4); their absence here is evidence the linker is doing its job,
   not evidence they are dead.

### 6.3 Cadence

`capture_sourceboot_throughput.py`, `summarize_cadence` only, 30 presentation
events / 29 intervals, caches off, on-target identity **MATCH** on both runs,
`status: complete`.

| route tick | arm | mean | median | 1% low | VB/frame | interval histogram |
|---|---|---:|---:|---:|---:|---|
| 30 | `id-8e643f9cc3e9fdee` (accepted baseline) | 6.8775 | 6.6667 | 6.6667 | 8.7241 | 21x9 + 8x8 |
| 30 | **`id-0e0420373f40f11e` (pool)** | **6.8775** | **6.6667** | 6.6667 | 8.7241 | 21x9 + 8x8 |
| | | **+0.00%** | unchanged | unchanged | unchanged | **identical** |
| 150 | `id-8e643f9cc3e9fdee` (accepted baseline) | 6.8235 | 6.6667 | 6.6667 | 8.7931 | 23x9 + 6x8 |
| 150 | **`id-0e0420373f40f11e` (pool)** | **6.7969** | **6.6667** | 6.6667 | 8.8276 | 24x9 + 5x8 |
| | | **-0.39%** | unchanged | unchanged | +0.0345 | one interval moved 8VB -> 9VB |

**Read the median, not the 1% low** (`w2-onepercent-low-recheck.md`): at n=29
the 1% low resolves to `rates[0]` on a `60/k` grid, so +/-10% is its smallest
expressible step. It is 6.6667 on all four runs and carries no independent
information here.

**Tick 30 is not merely close, it is identical** — same mean to four decimals,
same 8.7241 VBlanks per frame, same 21x9 + 8x8 interval histogram over the same
29 intervals.

**Tick 150 loses one interval of 29.** 24x9 + 5x8 against the baseline's
23x9 + 6x8 is the smallest change the instrument can express, and it lands the
mean exactly on 6.7969 / 8.8276 — which is, to four decimals, the figure the
*pre-F* baseline `id-36ebaa12184cd661` had at this tick. R3 §9 established that
anything under ~0.59 VB/frame cannot move the median off 6.6667, and 0.0345 is
not that. The median and the 1% low are unchanged; the accepted FPS floor is
not touched.

**What it is attributable to, stated rather than hidden.** Two things were
added to the per-tick path: the generation-checked `resolve` that replaced a
static array index (two loads and a compare, twice per tick, against LWRAM pool
control state), and the synthetic alias witness (two 32-byte stripes written
and two read back, §5.3). The second is scaffolding and goes away with §8.7.

### 6.4 Mario is unchanged

**Method: the W2 route-timeline comparison.**
`tools/saturn/capture_route_timeline.py` on both sealed builds, one headless
boot each, walking the deterministic replay route from boot to tick 200 at a
3-VBlank fine stride — 612 samples on the baseline, 613 on the candidate — and
reading, per sample, `gMarioState`'s action, all three face angles, position,
velocity, forward velocity and floor height; the route input the runtime
actually applied (`sState.last_applied_{buttons,stick_x,stick_y}` and
`replay_ticks`); `gGlobalTimer`; and the Mario draw-slot counters.

**Result: over route ticks 0-199, 200 ticks compared, ZERO mismatches on every
Mario simulation field and every applied route input.**

| field | ticks differing (of 200) |
|---|---:|
| `action` | **0** |
| `face_angle[0]`, `face_angle[1]`, `face_angle[2]` | **0** |
| `pos[0]`, `pos[1]`, `pos[2]` | **0** |
| `vel[0]`, `vel[1]`, `vel[2]` | **0** |
| `forward_vel` | **0** |
| `floor_height` | **0** |
| `global_timer` | **0** |
| `applied_buttons`, `applied_stick_x`, `applied_stick_y` | **0** |

**Two apparent differences, both examined rather than waved away.**

1. **Tick 200 only.** The capture loop terminates the instant
   `replay_ticks >= 200`, so the two arms stop at different sub-tick phases.
   At tick 199 both arms read `pos[0] = -4796.266`, `vel[0] = 25.5012`,
   `forward_vel = 25.5012`, identically; the baseline's tick-200 sample still
   reads *those same values* (it sampled before the tick advanced Mario) while
   the candidate's reads the post-advance values. It is the terminal sample's
   phase, not a divergence — which is why the comparison is stated over
   0-199 and the boundary sample is excluded with its reason.
2. **`s_actor_command_count` / `s_actor_texture_count`.** These are *render*
   counters — the Mario draw slots reserved for the frame currently under
   construction — sampled on a VBlank stride that is not tick-aligned, so their
   value depends on where in the frame the sample lands. The arms differ by one
   interval at tick 150 (§6.3), which moves that phase. What matters is that
   the population is the same: a nonzero Mario draw batch on **167 of 200**
   ticks for the baseline and **168 of 200** for the candidate, over the same
   ~300-310 command range. **Mario is drawn, with the same number of commands,
   on the same ticks.**

**Conclusion: this is plumbing beneath Mario and it is plumbing beneath Mario.**
The simulation is bit-identical over the compared route; the pose bytes he is
drawn from are the same bytes at the same size, produced by the same evaluator
with the same staged inputs; the probe confirms the R3 bank fallback still
never fires (`fallback_hits == 0`) and the R3-F LWRAM staging is still live
(`staged_slices == 1`).

---

## 7. Identity and hashes

| field | value |
|---|---|
| build id | **`id-0e0420373f40f11e`** |
| `effective_config_sha256` | `0e0420373f40f11e45f5b8ed8fc59163c1f42d6b434ba9266e383a8161725ffc` |
| `identity_sha256` | `7918a856a8ddfc7b716257f309bc17f2058686395005b8ed40863371feb96068` |
| `source_closure_sha256` | `8ddb93762b3014c1f1b558cb226f5707226dbc07b869d0b7dd00d5ba97effa2d` |
| `package_set_sha256` | `89b2dd28c448771aa8359379d27c0c1347188017b1f1b60e86b54dddc7f831ae` (**unchanged**) |
| `target_profile_sha256` | `bccdd297fd7792d0bdcbc5d4d2594e34faaa1d219e4e745cc7a6cfa02348a45c` (**unchanged**) |
| ELF SHA-256 / size | `380f4ee23b41a6a357fd3266c791090938d597f25032ce8f031e82ec88ba8b0c` / 10,260,736 |
| ISO SHA-256 / size | `96cf9f625bea221301cc2972c516d67604f651e9ac0d84fc5f77b595860d941c` / 5,117,952 |
| `SOURCE.DAT` SHA-256 / size | `68aa2c09c4d9d341a612fba6f9e73afb01529920aa3e7582286d096749b5e47a` / 3,712,144 |
| `.cue` SHA-256 / size | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` / 88 |
| profile / mode | `sourceboot-bob-demo` / `development` |
| package generation | 16 (unchanged) |
| `provenance.git_revision` | `1024ba41` |
| **`closure_clean`** | **`false`** |

**The target profile did not move and no feature flag flipped.** The sealed
tuple is byte-for-byte the accepted one; the identity changed only because the
source closure did (one new translation unit, one edited one). No
`tools/saturn/profiles/*.json` was touched — `git diff` against it is empty.

**`closure_clean` is `false` and that is stated, not hidden.** The artifact was
linked from a working tree that had not yet been committed; the commit that
carries this report contains exactly that source and nothing else, and
`source_closure_sha256` is content-derived, so a rebuild from the commit
produces the same closure hash and the same `effective_config_sha256`
(`closure_clean` and `git_revision` live under `provenance`, outside the sealed
tuple). It has not been re-linked to demonstrate that, because a rebuild would
consume the budget that produced the measurements above.

Preserved at `releases/2026-08-22_r2-pool-product/id-0e0420373f40f11e/`
(cue, ISO, manifest, ELF, linker map, `SOURCE.DAT`; 1,321 files). The accepted
baseline is untouched and a pre-build snapshot of it sits at
`releases/2026-08-22_1249_r2-pool-pre-build/id-8e643f9cc3e9fdee/`.

**Gates run.**

| gate | result |
|---|---|
| `verify-actor-pose-pool` | **PASS** (new) |
| `verify-actor-pose-pool-mutation` | **PASS** — 3 of 3 mutations caught (new) |
| `verify-memory-map` | **OK** |
| `verify-actor-pose-bank` | **PASS** |
| `verify-actor-runtime-handoff` | **PASS** |
| `verify-actor-instance-queue` | **PASS** |
| `verify-actor-feature-off-wrapper` | **FAIL — pre-existing and unrelated.** It asserts `demo_generic_actor_emit(transaction, true)` appears in `demo_render_finalize` in `saturn_demo_render.c`, a file this task did not modify (`git status` shows it clean). Recorded, not chased. |

**Attached evidence**

```
docs/saturn/evidence/reports/r2-pool-cadence-tick30.json
docs/saturn/evidence/reports/r2-pool-cadence-tick150.json
docs/saturn/evidence/reports/r2-pool-probe.json
docs/saturn/evidence/reports/r2-pool-route-timeline-base-8e643f9c.json
docs/saturn/evidence/reports/r2-pool-route-timeline-cand-0e042037.json
docs/saturn/evidence/reports/r2_probe_pose_pool.py
```

**Reproduce.** The build command is the accepted tuple, unchanged, exactly as
recorded in `r3-c-f-memory-recovery.md` §7. Then:

```powershell
.\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk `
  verify-actor-pose-pool verify-actor-pose-pool-mutation
.\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk `
  verify-memory-map SOURCEBOOT_CANDIDATE_ELF=<abs path to the candidate ELF>
```

```sh
D=<releases/2026-08-22_r2-pool-product/id-0e0420373f40f11e>
for T in 30 150; do
  python tools/saturn/capture_sourceboot_throughput.py --ymir "$YMIR" --ipl "$IPL" \
    --game "$D/sm64-saturn-sourceboot-e2.cue" --elf "$D/obj/sm64-saturn-sourceboot-e2.elf" \
    --release-manifest "$D/saturn-release-manifest-v1.json" --startup-vblanks 4096 \
    --warmup-ticks $T --presentation-events 30 --max-vblanks 3600 --timeout 3000 \
    --output docs/saturn/evidence/reports/r2-pool-cadence-tick$T.json
done

python docs/saturn/evidence/reports/r2_probe_pose_pool.py --tools "$PWD/tools/saturn" \
  --ymir "$YMIR" --ipl "$IPL" --game "$D/sm64-saturn-sourceboot-e2.cue" \
  --elf "$D/obj/sm64-saturn-sourceboot-e2.elf" --nm "$NM" \
  --output docs/saturn/evidence/reports/r2-pool-probe.json

# the Mario-unchanged timeline; addresses are per-ELF, resolve with $NM
python tools/saturn/capture_route_timeline.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$D/sm64-saturn-sourceboot-e2.cue" --elf "$D/obj/sm64-saturn-sourceboot-e2.elf" \
  --state-address 0x060f34a8 --global-timer-address 0x060bd7a4 \
  --mario-state-address 0x060929f8 --profile-address 0x002d18a8 \
  --actor-address 0x060ecc2c --until-tick 200 \
  --output docs/saturn/evidence/reports/r2-pool-route-timeline-cand-0e042037.json
```

(The baseline arm uses `0x060f30f8 / 0x060bcf04 / 0x06092158 / 0x002cec78 /
0x060ec87c` against `releases/2026-08-22_r3-cf-product/id-8e643f9cc3e9fdee`.)

---

## 8. What the next wave needs that this does not build

1. **A rigid-part pose evaluator.** `sm64_saturn_actor_pose_evaluate_staged`
   produces *skinned* vertices. Every non-Mario jointed actor is a rigid-part
   hierarchy and wants per-part matrices out of the same animation channels.
   The pool holds whatever bytes that produces; the producer does not exist.
2. **A published rigid-part pose layout.** §2 assumes 48 B per part (a 3x4 Q16
   matrix). If the renderer would rather have angles + translation (12 B), the
   class plan should shrink accordingly — a one-line edit, but somebody has to
   make the call.
3. **Distance-ordered claiming.** The pool is first-come (§4.4). The next wave
   must sort claimants by distance so that pressure costs the far actors their
   animation, not the near ones.
4. **A per-actor owner key.** The bridge uses a constant for Mario. Generic
   actors need a stable key per *instance* that survives a frame — the
   instance-queue slot id is the obvious candidate, but it must be stable
   across the two in-flight generations or `find` will mis-match.
5. **Release on despawn, wired to the object lifecycle.**
   `release_owner` exists and is tested; nothing calls it, because nothing
   spawns. Whoever adds spawning must add the release, or the pool leaks to
   exhaustion within a level.
6. **Coins, trees and the other 74.** They need no pose slot — but they do need
   the generic actor closure, which is sealed off
   (`features.dynamic_actor_closure = 0`) and blocked on 13 unsupported
   required capabilities, twelve of which are `GEO_CULLING_RADIUS` (which is
   what every tree variant uses). **That, not pose slots, is what gates props.**
7. **The synthetic target exercise should be deleted when real tenants
   arrive.** It holds two cold blocks and costs two 32-byte stripes per tick.
   It is scaffolding, and it is labelled as such in the source.
8. **A second evaluator lane, if pose evaluation ever moves to the slave.**
   The shared joint workspace assumes one evaluator (§2.1). A second lane is
   1,280 B of HWRAM.

---

## 9. Honest weak spots

- **The cold arena has no measured consumer.** 10,240 B of LWRAM is committed
  against a projected scene, per the owner's explicit instruction to design for
  the target state. If the real enemy pose turns out to be 96 B rather than
  960 B, this is 10x too generous and should shrink; the class plan is data.
- **The per-tick alias witness is synthetic.** It proves the allocator, not a
  Bob-omb. It is labelled `host-contract-passed` plus a target exercise, not a
  product gate.
- **Rigid-part vertex counts are unbaked estimates.** §1's raw N64 counts
  (goomba 464, whomp 98, spindrift 30) include per-display-list duplicates and
  will not survive the W3 bake to a declared budget. They bound the problem;
  they do not size it.
