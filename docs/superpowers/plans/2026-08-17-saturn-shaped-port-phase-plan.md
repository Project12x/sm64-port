# Saturn-Shaped Port — Phase Plan

> **Scope note:** This is a **phase plan**, not an implementation plan. It sequences four
> independent workstreams against the program charter
> ([`2026-08-14-saturn-shaped-port-program.md`](2026-08-14-saturn-shaped-port-program.md)).
> Each workstream needs its own implementation plan written at entry, per
> `superpowers:writing-plans`. Do not execute from this document directly.

**Goal:** Stop paying the cost of running an N64 game's structure on Saturn hardware, so
that the frames exist to put the rest of the game back.

**Architecture:** The port currently runs SM64's simulation *and* its scene-graph
traversal, then renders through a Saturn-native path that reads simulation state by a
side channel. The traversal's output is discarded. Saturn-shaping means removing the
foreign structure while keeping the foreign *game*, and authoring data to a declared
budget instead of converting it.

**Amended 2026-08-17** to add W5 after the owner observed that the plan named no
simulation cost at all. That was a real omission: the original four workstreams account
for the render path only.

**Basis:** All figures are measured. Cadence from `summarize_cadence` only; profiles from
`capture_idle_attribution`'s exact census, never the burst sampler (T2.20 measured it at
±2.6 pp per symbol and it reported the soft-float trend with the wrong sign).

---

## 1. Where we are

| | caches-off | caches-on (T2.26) |
|---|---:|---:|
| FPS mean / median / 1% low | 6.7181 / 6.6667 / 6.0 | 6.2143 / 6.0000 / 5.4545 |
| VB per frame | 8.9310 | 9.6552 |
| master idle | **0.000000** | **0.000000** |
| master idle *(W6a, `id-586944bb637a920f`, tick 150, 1.98-frame window)* | ~~5.22%~~ **superseded by W7** | — |
| master idle *(W7, same build, tick 150, **4.83-frame** window)* | **2.1450%** | **0.000000** (`--sh2-cache`, 26.55 VB) |
| master idle *(W7, same build, **tick 210**, 2.63-frame window)* | **0.000000** | not measured |
| master idle *(W7, same build, **tick 30**, 3.84-frame window)* | **0.000000** | not measured |
| slave idle | 6.1095 VB (68.41%) | 6.0632 VB (62.80%) |

Sprint 2 took cadence **3.8753 → 6.7181, +73.4%**, past A9A's 5.294 / 11.333.

Historical T2.17 headless comparison build: `id-c0352f297034f653`. It was not
GUI-launched, live owner-observed, or owner-accepted; it is preserved as a
historical headless comparison/rollback baseline, and its unbounded-fence risk
motivated W0. The current owner-accepted artifact is **W2 candidate `id-28b8cb0e531f8476`**,
accepted 2026-08-22 at **7.3109 FPS mean / 7.5000 median** (tick 30; 7.2803 / 7.5000 at
tick 150), `closure_clean: true` at `da071fb7`. Owner: *"it looks and runs
fine. getting closer and closer to a playable frame rate and frame timing."* Its
tick-30 1% low of 6.0000 is a **documented, understood artifact** — see STATE.md; never
quote it without the tick-150 and tick-210 columns. It supersedes W6a rebuild
`id-57fb7312058b7f2e` (7.0445 / 6.6667), which becomes the rollback point.

> **~~STRUCTURAL CHANGE, W6a: the master is no longer saturated.~~ WITHDRAWN by W7
> (2026-08-21, same day). The 5.22% was a short-window measurement artifact, and I
> wrote it into this plan as a structural finding without checking the window length
> or whether its two inputs came from the same route position. They did not.**
>
> W7 re-measured master idle at three route ticks on both arms:
>
> | route tick | window | candidate master idle |
> |---|---|---:|
> | 30 | 3.84 frames | **0.000000** |
> | 150 | 1.98 frames | 5.2248% *(the figure I quoted)* |
> | 150 | **4.83 frames** | **2.1450%** |
> | 210 | 2.63 frames | **0.000000** |
> | 150, `--sh2-cache` | 26.55 VB | **0.000000** |
>
> **The wait is one event, not a per-frame cost** — a 4.83-frame trace found the same
> five entries and the same 399,412 cycles as a 1.98-frame trace, with no additional
> excursion. Normalising that by a short window inflates it.
>
> **And tick 30 — where W6a's +5.26% was actually measured — has no waiting at all.**
> W6a's residual argument paired a **tick-30 cadence** with a **tick-150 idle share**.
> Single-route cadence pairs at matched ticks give **+5.60%** and **+6.30%**: the gain
> *rises* with scene weight rather than being halved.
>
> **Planning figure for W2 and W6b: 80–100% of face value, not 50%.** The discount is
> withdrawn. W7's own top-ranked release is *do nothing* — continue the arithmetic
> programme at full expected value.
>
> **The wait that does exist is real but small and not offloadable.** It is a
> **dependency stall, not hardware-gated** — 100% of idle cycles are the
> `transfer-epoch` arm at `sourceboot/main.c:1929`, the only reachable one of two call
> sites in the ELF. Render is complete and the DMA is submitted; the master is waiting
> for the next observed field to poll an in-flight queue again, and **VDP1 does not
> move during the wait** (`EDSR 0x0001`, `LOPR/COPR 2092` byte-identical across 119
> strided samples). But it is 82,738 cycles/frame at *one* of three route positions and
> exactly 0 at the other two, **its release is an observed VBlank rather than master
> availability, and where the master has slack, offload deepens it.** See
> `w7-master-wait-attribution.md`.

## 2. The finding this plan is built on

**We are not slower because we draw more. We draw less.**

| | per-frame VDP1 commands | source |
|---|---:|---|
| this port | **517–552** | `sprint2-t2_19c-command-count-lod.md:117,146` |
| SlaveDriver (in-game) | **1,448** | `SRUINS.C:1879` |
| Z-Treme (whole-scene budget) | **1,900** polys / 2,800 verts | `Common.h:21-22` |

Both references push 2.7–3.7× our draw volume on the same hardware. The frame goes
somewhere other than drawing, and the two largest identified consumers are both artifacts
of running a decompiled N64 game:

- **The geo-graph walk region — 18.18% of the frame, but see W1: this is NOT a lever.**
  T2.15 measured `SKIP_GEO_WALK` at +0.9735 FPS / −1.7241 VB. **That diagnostic skips
  animation advance, camera publication and callback side effects — all load-bearing —
  so it sizes the region, not the removable part.** The removable part is the
  matrix-stack bookkeeping, which has never been sized. Display-list construction, the
  one piece that was measured, is 0.479% of the region and already declined.
- **Soft-float — ~19–20% of the master critical path.** Both references are fixed-point
  throughout.

**But the accounting is incomplete, and the gap is the simulation.** The two figures
above cover the *render* path. **SM64's game simulation has never been attributed as a
category** — and the instrument that would do it, the `simulation` vs `construction`
phase decomposition, is **broken**: the T2.11 concurrency rail needs its allowance on
**28 of 29 intervals** since T2.17, so the decomposition no longer closes and `STATE.md`
records that it must not be quoted.

Symbol censuses do not substitute. They surface simulation symbols — `_find_floor_from_list`
appears among T2.13's float leaders, T2.14 measured **43.471 `find_floor` calls/frame** —
but rank them low, and **that is an artifact of the route, not a property of the code**.
The replay has one actor, stationary until tick 121 and walking at 180: no Bob-ombs, no
goombas, no coins, no moving platforms. SM64's simulation cost scales with active
objects and we exercise almost none of it. For scale: **11 of those 43 `find_floor`
calls are Mario's shadow** — 25.3% of all floor collision work is one shadow, on a route
where collision has nothing else to do (T2.14).

So T2.20's "no hidden gameplay hot spots" establishes that **moving Mario does not
change the ranking** — *not* that gameplay load does not. Those are different claims.
**The simulation is the largest unmeasured block in the frame, and it is the most
structurally N64-shaped code in the tree**: float `vec3f` throughout, collision written
for an FPU, per-object interpreted behaviour scripts.

The renderer itself is near its floor: after spatial admission, meshlets and the epoch
stall all paid, **four consecutive investigations returned 0.07–0.5%** (shadows, display-list
construction, user clipping, command-count LOD). That is what a subsystem out of slack
looks like.

## 2a. Porting versus recreating — the choice that governs every workstream

**Owner ruling (2026-08-17):** *"not everything can be ported - it is better that its
recreated as saturn native."*

This is a per-subsystem decision, and **it determines the acceptance criterion, not just
the implementation**. Getting the pairing wrong is expensive in both directions: porting
what should be recreated buys a slow, foreign-shaped result; recreating what should be
ported throws away working behaviour for no gain.

| | **Port** | **Recreate** |
|---|---|---|
| Intent | preserve the original algorithm | solve the same problem the Saturn way |
| Verified by | equivalence or bounded divergence **against the original** | **behaviour**: does it play and look right |
| Divergence is | a defect to bound | **the point** |
| Cost | cheap when the algorithm already fits | design effort, and needs an owner judgement |

**Precedents already set in this project, both correct:**

- **Ported:** T2.13's shadow trig. The algorithm fit — an exact s16 angle was being sent
  on a detour through float degrees — so substituting a table was a *port*, verified by
  bounded, memoryless, sub-pixel divergence. Cheap and right.
- **Recreated:** shadows themselves. A generic painted sprite that does not follow light
  cues is **not** a degraded SM64 shadow; it is the Saturn answer to the same problem.
  Measuring its divergence from SM64's shadow would be measuring the wrong thing.

**Correction to W6b, recorded:** an earlier draft of this plan specified a per-tick state
divergence harness as W6b's acceptance criterion. **That criterion assumes porting.** If a
subsystem is recreated, it will diverge by design and the harness would reject the very
approach the owner has chosen. The harness is still the right tool for *converted*
subsystems; recreated ones need behavioural acceptance instead. See W6b as amended.

**Applying it to the workstreams:**

- **W1** (scene-graph traversal) — **recreate.** The renderer already has a Saturn-native
  path; the N64 walk is the foreign structure. Constraint: the surviving camera matrix and
  the callback side effects are *interfaces to gameplay*, so those are ported, not
  recreated.
- **W3** (bake) — **already recreation.** Authoring to a declared budget rather than converting
  N64 geometry is the whole point.
- **W6a** (soft-float library) — **port, strictly.** Bit-exactness is the entire value; there
  is nothing to redesign.
- **W6b** (simulation math) — **mixed, and the split is the design work.** See below.

---

## 2b. Functional correctness beats numerical correctness — and we are porting *down*

**Owner ruling (2026-08-22):** *"Numerical correctness isnt as important as functional
correctness as a game. remember, this is a saturn port, and it cant be direct as we're
basically reaching for a target a generation below instead of above the original
target."*

**The direction of travel is the whole point.** Most ports move *up* — a more capable
target, where fidelity is free and the question is what to add. This one moves **down
a generation**. The N64 is the stronger machine. So **every subsystem here is a
degradation decision**, and the question is never "how close can we get to the
original" — it is **"which degradation is invisible in play."**

**The acceptance bar is: does it play right.** Numerical exactness is *one cheap route*
to that guarantee. It is not the goal, and it is not the standard.

**Where exactness is free, take it — as a convenience, not a principle.** W6a replaced
the soft-float library with an exactly-equivalent one and got 43.3 billion checks at 0
failures; W2 replaced `sqrtf` with a correctly-rounded integer root, 0 ulp over all
4,294,967,296 bit patterns. Both were *cheap* to verify precisely because equivalence
was available, which removed an oracle-design problem entirely. **That is why those
workstreams ranked early — low risk and decidable — not because bit-exactness is the
bar.** Do not read those results as a precedent that future work must match.

**Where exactness costs cycles or scope, spend the fidelity.** Both reference ports
did, and shipped playable: `malucard/sm64-psx` quantises every surface normal to `s8`
at scale 120 (~0.83% error, with slope thresholds quantised alongside), drops
round-to-nearest in the `atan2` index so angles can land one table entry off, leaves
the rounding correction in `vec3q_to_vec3s` commented out, and truncates positions to
whole world units in distance and look-at maths. None of that is defensible as
numerics. All of it is fine as a game.

**The failure modes that actually matter are functional, and they are specific:**

- geometry that **swims, jitters or pops** between frames — a *fixed* offset is
  invisible, an *accumulating* one is a defect at any starting size, because
  simulation state feeds itself forward;
- a threshold that **never fires** after quantisation — the reference's stuck Bowser
  state transition and its poles that never go down are the shape of this;
- a jump that **no longer clears a gap**, or a floor that stops holding Mario;
- **frame-time spikes** — the 1% low, which is what makes a frame rate feel steady
  rather than merely average well.

**Not a ulp.**

**Corollary for verification: a played route outranks an exhaustive numerical proof.**
Where the two disagree, the played route wins. Divergence harnesses are
**change-detectors that tell the owner where to look**, not gates that decide
acceptance. This is the same reasoning as §2a's port/recreate split, one level up: 2a
says *which* criterion applies per subsystem; 2b says what the criterion is ultimately
*for*.

**Consequences already in force:**

- **W6b leans behavioural.** Its scope is the data Mario reads, and each interface
  states its quantisation against a **named observable** — slope classification
  threshold, wall push distance, ledge-grab reach, object interaction radius — not
  against a tolerance.
- **W2's pending promotion decision is functional, not statistical.** It is held on a
  1% low that moved 6.6667 -> 6.0 at one route position, because that is a *stutter*
  question. Its exact-root proof does not answer it and was never going to.
- **W4's degradation-as-design policy is the same idea applied to visuals.** The shadow
  sprite is not a worse shadow; it is the Saturn answer. Extend that framing to LOD
  tiers, CLUT depth, draw distance and per-level budgets.

---

## 3. Sequencing against the charter

| Charter phase | Relationship to this plan |
|---|---|
| **R2** Normal Bob-omb | Costs frames. Wants W1 headroom first. |
| **R3** Mario animation + HUD | **Animated Mario ACCEPTED 2026-08-22** on `id-36ebaa12184cd661` — the complete path existed behind `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION`, which defaulted to 0; walk, jump and others confirmed working, pose data covers the set. Cadence cost reported: −6.64% at tick 150, −5.93% at tick 30, median 8→9 VBlanks (`r3-mario-animation-scoping.md`). **Options C and F landed 2026-08-22 on `id-8e643f9cc3e9fdee`, live-observed, owner gate open**: reduced 104-id bake + CART→LWRAM staging of the invariant bank slices, cart free 117,840 → 482,160 B at **no cadence cost** (6.8775 tick 30 unchanged, 6.8235 tick 150, +0.39%) — `r3-c-f-memory-recovery.md`. **HUD outstanding, and it does not fit in the remaining 2,008 B of HWRAM: decimation (option G) or an HWRAM→LWRAM eviction must come first.** |
| **S1** Any-level cart loader | Needs W3's generalized bake. |
| **S2** Shrink & degrade pipeline | **Is W3.** Pull forward. |
| **S6** All levels | Depends on S1 + W3. |
| **S7** Perf & hardware, 6–10 FPS band | W0–W2 serve this; hardware validation still open. |

**Ordering rule (re-ranked 2026-08-17, after W1's headline figure was withdrawn):**
**W0 first — correctness**, and note the open audit finding below before treating it as
closed. **W6a next**: it is bit-exact, whole-program, provable by an existing gate, needs
no dependency enumeration, and pays proportionally to whatever float's share turns out to
be — the only large lever here whose correctness is *decidable*. **Then W5a**, because
until the phase decomposition closes again nothing can attribute simulation cost, and
several of the remaining decisions depend on that number. **W3 is offline and independent**
of all of them and can run in parallel at any time. **W2 after W6a**, since W6a makes its
remaining sites cheaper and may change which are worth converting. **W1 is demoted to a
measurement task** — its 18.18% headline was withdrawn and its real size is unknown;
size it before scoping it. **W6b last**, after W5b's actor-scaling curve says which parts
of the simulation are hot. **W4 is standing policy, not a task.**

---

## W0 — Bound the overwrite fence *(correctness, blocks the owner gate)*

**Why:** `vdp1_sync_wait()` has **no deadline** and T2.17 made it load-bearing for the
first time — the frame is 2.28 VB shorter with the plot unchanged, so the fence is now
reachable where T2.8 never saw it fire. `STATE.md` records it as "no longer optional";
T2.8 §9 item 3 already listed it. This is the hang path in the build awaiting observation.

**Status:** `complete; owner-accepted`; normal
mode 0 linked, passed both memory floors, and passed the identity-bound
headless product observation. The diagnostic mode-2 arm linked and passed both
floors at a tool/docs-only descendant revision, so it is target-equivalent but
not artifact-identical to the frozen normal candidate. Independent review
returned `PASS WITH FINDINGS` and authorized desktop owner launch; the two
review-evidence findings are closed without a product-code change. The exact
normal candidate launched on desktop Ymir and remained alive after its bounded
monitor. The owner then explicitly accepted the candidate after controls,
camera, collision, actor, audio, presentation-integrity, and permanent-freeze
checks.

**Execution records:**

- Design:
  [`2026-08-17-vdp1-overwrite-fence-design.md`](../specs/2026-08-17-vdp1-overwrite-fence-design.md)
- Implementation plan:
  [`2026-08-17-vdp1-overwrite-fence.md`](2026-08-17-vdp1-overwrite-fence.md)
- Active ledger:
  [`w0-vdp1-overwrite-fence.md`](../../saturn/evidence/reports/w0-vdp1-overwrite-fence.md)

**Gate state:**

- [x] W0.1 scheduler acknowledgement — host contract passed; exact-generation
  acknowledgement and mutation evidence are recorded in the active ledger.
  Wrong-generation and duplicate acknowledgements now compare the full relevant
  scheduler state for immutability.
- [x] W0.2 sourceboot deferral and diagnostics — `host-contract-passed;
  normal live-observed; diagnostic target-compiled`. `verify-frame-pipeline`, `verify-vdp1-frame-bank`,
  `verify-vdp1-transfer-pipeline`, and `verify-render-overlap-integration`
  passed on 2026-08-17; the review-evidence rerun reconfirmed
  `verify-frame-pipeline` and `verify-vdp1-transfer-pipeline`. The
  presentation-boundary gate remains unchecked only
  for its known pre-W0 `bootstrap must contain exactly one null-snapshot VDP2
  begin` literal-drift failure; it did not identify a W0 presentation-path
  change. The busy branch is `host-proven; target-compiled; live occurrence
  unproven`; normal live evidence does not itself observe a busy deferral.
- [x] W0.3 unique normal build and earliest Ymir product observation —
  `id-e8720d58595d9a62` passed 30 events/29 intervals at 6.6923 mean and 6.0
  1% low FPS; diagnostic `id-5f27c53e9ae67c9c` linked and passed both floors
  at a tool/docs-only descendant revision. T2.17 remains the preserved
  historical/headless comparison baseline and was not owner-observed or
  owner-accepted; W0 `id-e8720d58595d9a62` is the current
  owner-accepted artifact.
- [x] W0.4 independent review and desktop owner observation — `complete;
  owner-accepted`. Review: `PASS WITH FINDINGS` with no critical/important
  issue and both review-evidence findings closed. `id-e8720d58595d9a62` was
  identity-bound launched on desktop Ymir. The owner accepted it after stating
  that controls, camera, collision, and actors behave normally, there is no
  tearing/flicker/partial-or-duplicate plot/permanent freezing, and that it
  looks and sounds good at 6–7 FPS.

**Size:** ~1 hour of code plus target build, live observation, review, and owner
gate time. Not a cadence item.

**Exit:** the fence performs one busy observation and immediately defers the exact
`READY` bank/generation to a later observed VBlank. The busy branch starts no DMA,
does not re-present while VDP1 is plotting, and contains neither `vdp1_sync_wait()` nor
an equivalent diagnostic spin. The scheduler preserves its existing two-tick
unpublished-generation budget: a transient busy state recovers automatically, while a
permanent busy state retains the last complete framebuffer and pauses with simulation
advancement capped rather than overrunning snapshot ownership. `verify-frame-pipeline` and
`verify-vdp1-frame-bank` still pass with no assertion weakened (T2.17 kept all eleven
passing verbatim — hold that standard). Execute only through the linked implementation
plan and record each transition in the active ledger.

**Design-decision status (2026-08-17):** the historical A8 transfer-runtime
source test had drifted from the immutable base: the persistent poison flag is
validly annotated `SOURCEBOOT_LWRAM_STATE`, and the VDP1 pointer conversion is
initialized in `sourceboot_post_cart_init()`, not `main()`. Its assertions now
accept that storage annotation and inspect the actual initializer while
retaining both semantic contracts. Runtime code was not changed to satisfy
obsolete spelling.

---

## W1 — Delete the discarded transform bookkeeping *(size unknown — measure first)*

> **Corrected 2026-08-17 after owner challenge: "isn't the n64 scene graph traversal
> needed for actual gameplay?"** It is. The previous title ("Remove the N64
> scene-graph traversal") and its headline figure (18.18%) were both wrong, and the
> error is recorded here rather than quietly edited away — see *Sizing error* below.

**The traversal must stay. Two hard dependencies:**

- **Animation frames advance inside the walk.** `rendering_graph_node.c:1302` calls
  `geo_update_animation_frame(node, &node->animFrameAccelAssist)`. The Saturn
  renderer then reads `animInfo.animFrame` through `saturn_actor_bridge.c:133-134`
  to select poses — **so the walk is what currently feeds the renderer's animation.**
  Remove it and animation stops advancing. (Note `mario.c:188` calls the same
  function separately; which is authoritative for Mario needs establishing before any
  change.)
- **The camera matrix is read by gameplay** — `src/game/behaviors/bowser.inc.c:1760`,
  `king_bobomb.inc.c:11`, `ukiki.inc.c:51`, `src/game/behavior_actions.c:167` each
  call `create_transformation_from_matrices(..., *gCurGraphNodeCamera->matrixPtr)`;
  also `level_geo.c:32-33` and `mario_misc.c:409`. `matrixPtr` points at
  `&gMatStack[gMatStackIndex]` (`rendering_graph_node.c:730`), so **one matrix must
  survive** — but not the stack discipline around it. `gCurGraphNodeCamFrustum`
  (`:562`, `:572`) has the same class of consumer via `graph_node.c:639-642`.

**What is actually removable, and it is narrow:** nothing outside
`src/game/rendering_graph_node.c` reads `gMatStack`, `gMatStackQ` or
`gMatStackFixed`. Those stacks are maintained per node solely to stamp transforms
into display-list entries that are then discarded. **That bookkeeping is the target — not
the traversal.**

The list is genuinely dead: under `SATURN_DEMO_PATH=1` display submission is
suppressed around all of `game_loop_one_iteration()` (`sourceboot/main.c:547-559`),
`src/game/game_init.c:464` never calls `exec_display_list`, and `main.c:2151`
passes `NULL, NULL` to `sm64_saturn_source_runtime_configure`. T2.14 witnesses over a
50 s product capture: `submitted_tasks` **0.000/frame**, `command_count` **0.000/frame**,
`scene_graph_walks` **1.000/frame**.

**Correction to an earlier claim in this plan:** `gShadowAboveWaterOrLava` and
`gMarioOnIceOrCarpet` were listed as gameplay interfaces the walk owns. They are not —
grep finds no reader outside `rendering_graph_node.c` and `shadow.c`. They are
render-only, so that constraint is smaller than stated. The geo-callback side effects
that **do** need to keep running are `gMovtexCounter`, `gPaintingUpdateCounter` and
`gEnvFxMode`.

### Sizing error, recorded

**The 18.18% figure does not belong to this workstream.** It came from
`SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1`, which sets `scene_graph_suppressed` and — by
its own comment at `sourceboot/main.c:549-556` — deliberately invalidates animation,
warp, camera, water, moving-texture and matrix state. **It measures skipping everything,
and most of what it skips is load-bearing.** Quoting it as W1's prize repeated exactly the
error T2.14 made with "~10x the shadow path" (24-fold high) and that T2.15 caught by
measuring instead of extrapolating.

**Known lower bound:** T2.15 measured display-list *construction* at **0.479%** of the
region — already declined at +0.005 FPS. The matrix-stack maintenance is the untested
remainder and **has never been sized separately**.

**First task is therefore measurement, not deletion:** isolate the cost of the matrix-stack
push/pop and `gMatStackFixed[]` writes specifically — with animation advance, camera
publication and callback side effects all still running. **If that number is a fraction of
a percent, W1 closes as declined**, like the display-list half before it.

**Correctness bar (unchanged, if it proceeds):** byte-identical published state — surviving
camera matrix, bridge snapshot fields, animation IDs, callback counters — asserted across a
frame sweep, with mutations on a perturbed matrix entry, a perturbed animation ID and a
suppressed callback side effect, each of which must fail.

**Risk:** this is `src/game/` — decomp-adjacent, non-GPL, and the four behaviour files
above are gameplay. Any change must be guarded so the non-Saturn arm is untouched.

---

## W2 — Finish fixed-point on the master

> **See also W6.** W2 converts *call sites* to fixed-point. **W6a instead replaces the
> soft-float library underneath every remaining site** — bit-exact, whole-program, and
> provable by an existing gate. If W6a lands first, W2's remaining sites get cheaper
> without being touched, which may change which of them are still worth converting.

**Why:** ~19–20% of the master critical path remains soft-float, on a compiler this
project was founded on distrusting. Both references are fixed-point throughout.

**✅ SIZED AND HEAD TAKEN — 2026-08-22, `w2-softfloat-call-sites.md`.** The exact
census was re-derived over one contiguous **4.83-frame** window at `--warmup-ticks 150`,
normalised by that tick's own cadence (8.6207 — W6a had used a tick-30 8.5172 for a
tick-150 census, and that is corrected here). Soft-float is **26.580% of master /
2.291 VB per frame**, so **2.0718 is confirmed in magnitude and corrected upward by
10.6%**. The ranking is by **call site** for the first time, from exact PR-at-entry
attribution added to `capture_idle_attribution.py`.

| rank | call site | % master | VB/frame |
| ---: | :--- | ---: | ---: |
| **1** | **`_sqrtf`** (incl. its own body) | **8.462%** | **0.7294** |
| 2 | `_saturn_geo_enter_object` | 2.707% | 0.2333 |
| 3 | `_detect_object_hitbox_overlap` | 2.121% | 0.1829 |
| 4 | `_dist_between_objects` | 1.578% | 0.1360 |
| 5 | `_calculate_vertex_xyz` | 1.410% | 0.1216 |

**Rank 1 landed: `sqrtf` is now a 24-step integer digit-by-digit square root**
instead of four Newton iterations in soft float (four `__divsf3` + four `__addsf3` +
four `__mulsf3` per call, 1,559 cycles). Measured **+3.78% at route tick 30** (7.0445 ->
7.3109) and **+4.60% at tick 150** (6.9600 -> 7.2803), converting at close to full face
value exactly as W7 predicted. Correctly rounded on all 2^32 inputs, <=1 ulp from what it
replaced, and **downstream movement measured at zero** — Mario's state is identical on
every complete route tick 0-199. **The 1% low fell 10% at tick 30** (two 10-VBlank
intervals) and did not move at tick 150; that must be re-measured before promotion.

**Below rank 1 the recommendation is to STOP.** Ranks 2-5 total 7.8%; everything below
rank 5 is under 1% each; **962 distinct functions** in the linked image hold 3,482 static
soft-float references. The **Saturn render path is already essentially float-free** —
30 render-side functions hold 132 of those 3,482 — and the rest is SM64 simulation, which
is **W6b's** design problem, not a call-site sweep. If exactly one more site is taken it
should be `_saturn_geo_enter_object`: its remaining float is the inlined `obj_is_in_view`
reading the derived float mirror `gMatStack` when the authoritative `gMatStackQ` sits
beside it, plus a per-frame-constant `sins/coss` recomputed per object.

**Leads, resolved:**
- ~~`_sm64_saturn_ztreme_frustum_aabb` ~0.649 VB/frame~~ — **withdrawn on both counts.**
  It contains **zero floating point** (checked in the linked ELF: it references only
  `_support_radius_q16` and `_scaled_limit`), so W2 cannot apply to it at all; and the
  per-frame figure does not survive re-derivation, because the symbol is bursty exactly
  as W7 warned — **981.5 cycles per call, flat to four significant figures across three
  windows spanning 2.7x in length, while the per-frame number moves 1.8x**. The honest
  figure is a range, 0.466-0.714 VB/frame depending on whether `_support_radius_q16` is
  counted and on the assumed burst rate. **Quote cycles/call and calls/frame for it, never
  a three-decimal per-frame number.** It remains a live target for a *different*
  workstream — ~256 calls/frame of 64-bit integer work, where the lever is call count —
  and T2.10 item 3 stands as the warning against pricing its divides serially.
  (Free finding: `_scaled_limit` records **0 cycles**, so item 3's cross-multiplied fast
  path is taken on 100% of calls and its divided fallback is dead code on this route.)
- `_saturn_geo_enter_object` **confirmed** as rank 2 at 2.707%; `_saturn_geo_enter_camera`
  remains the fully-Q16 sibling template.
- The 0x1400-entry Q16 trig table exists and was **not needed** for rank 1.
- Census items 2 and 5 are static phantoms on this route — deprioritised, and the static
  scan here confirms the general point: static reference counts misrank badly.

**Correctness bar:** byte-identity is **not** required — owner's standing position from
T2.13: *"byte identical is not important for this case at all."* Use an error-bounded
oracle that catches **blunders** (sign flip, wrong quadrant, wraparound, table off-by-one,
degenerate input), state the tolerance, report worst case. Measure downstream movement and
report it; do not gate on it. **Frame-to-frame stability still matters** — accumulated
error that makes geometry swim is a defect even though a fixed offset is not.

---

## W3 — Author data to a budget *(charter S2, pulled forward)*

**Why:** every runtime data lever this sprint dead-ended in the bake.

- Terrain is **867 atomic primitives at exactly 1 VDP1 command each**, fan-out at
  `saturn_demo_render.c:2856`; **no subdivision exists anywhere in the tree**, so there is
  no grid for a MIPDIST-style LOD to halve (T2.19c).
- Merging is exhausted: of 622 shared edges, **1** joins same-texture neighbours and
  **0** are same-texture *and* coplanar — `quad_pairing.py` already consumed the
  compatible ones, taking 144 quads from 788 triangles, 18.3%, losslessly (T2.19c, T2.24).
- The only surviving terrain lever is a hole-punching drop capped at 94 of 867, and its
  bake rule is literally `source0 % 8 == 0` (`emit_bob_scene.py:196-204`) — **an arbitrary
  stride, not a hole-safety analysis** (T2.19c).
- Six LOD telemetry counters are declared with **no writer anywhere in the tree**, so
  per-tier populations are unmeasurable (T2.19c).

**By contrast:** Z-Treme declares its whole-scene budget in a header (`Common.h:21-22`).
Ours is whatever the N64 geometry converted to.

**Scope:** per-level command/vertex/byte budgets enforced by the bake; real LOD tiers
emitted offline; replace the `% 8` stride with hole-cost analysis; write the dead LOD
counters. **This is host-testable and generalizes to every level**, which is what makes it
S1 and S6 infrastructure rather than a BOB optimization.

**Mario, decided separately:** meshoptimizer (`zeux/meshoptimizer`, MIT, pinned
`97bbdce4716f6257c9527b051515136882f33e79`) gives 694 → 564 cmds at 75% and 418 at 50%.
Owner judges **75% near-identical, 50% visibly degraded**. Quad remeshing was evaluated
and **rejected on evidence** (T2.24): floor 452 cmds against a 418 target, open material
boundaries not preserved, every patch shrinking to ~84% of its bbox diagonal. **Open
decision (owner, 2026-08-22): decimate at 75% using PER-MATERIAL ratios in
meshoptimizer** rather than a uniform ratio -- protect the face and hands, coarsen
the limbs. Materials 1, 3, 4, 5 and 6 are *exactly* the 50 texture-locked primitives
(T2.19a), so they are cleanly holdable. Uniform 75% gives 694 -> 564 commands
(-18.7%) and the owner judged it visually free; per-material spends the same budget
where it is least visible, and plausibly approaches 50%'s command count at 75%'s
appearance -- the question quad remeshing failed to answer (T2.24).
**Sequencing: this runs AFTER R3, not alongside it.** Both touch the actor pipeline,
and T2.21 established that meshlet records are contiguous primitive-index spans, so
removing primitives regenerates the meshlet, render-cluster and tile-start tables
together. R3's complete pose path also holds full per-vertex position and light
arrays per slot, so decimating 424 -> ~330 vertices *changes what R3 costs* --
running decimation first would invalidate R3's measurements, running it second only
improves them.

**Constraint:** the 50 textured primitives are `pairing_forbidden`, exactly one source
triangle each, and **both their Gouraud polygon and their distorted sprite are visible**
(T2.19a: the sprite never sets SPD, 56.38% of texture words are transparent, 6 of 50 tiles
fully transparent). They cannot be merged, split or reordered.

---

## W4 — Degradation as design *(standing policy, not a task)*

Era-appropriate ports budgeted for reduced fidelity and did not apologise for it. Decisions
already taken in this frame:

- **Shadows** → a generic painted sprite under the actor, not light-following (owner,
  2026-08-16). T2.14 showed a sprite reading `gMarioState->floorHeight` needs **zero**
  floor queries, capturing 100% of what deletion would save while keeping the platforming
  depth cue; Mario's shadow currently issues **11 `find_floor` calls/frame, ≥25.3% of all
  floor collision work**. SlaveDriver's `COMPO_SHADOW` sprite is emitted immediately before
  its character — also the ordering answer for a renderer with **no depth bias anywhere**.
- **LOD popping** → engineer against it with hysteresis rather than disclose it. Bands
  already exist on depth and span; T2.19c pinned them with a dither oracle.
- **Mesh density** → a chosen point on a measured curve, not a compromise.

**Applies going forward to:** CLUT depth, draw distance, actor count per scene, and
per-level budgets in W3.

---

## W5 — Make the simulation measurable, then measure it

**Why:** everything above accounts for the render path. The simulation is unattributed,
and on the current route it is unattributable — there is no gameplay in it to measure.
This workstream removes both obstacles, in order.

**W5a — restore the phase decomposition.** The T2.11 allowance is spent on 28 of 29
intervals, so `simulation` and `construction` no longer close and cannot be quoted.
T2.11's fix bounded a provable cross-CPU double-count with
`min(simulation, master_finalization)`; at 8.93 VB/frame that bound is saturated. Re-derive
it so category attribution works again at current cadence. **Until this lands, no one can
say what fraction of the frame is game logic.** `summarize_cadence` reads presentation-edge
deltas independently and is unaffected — FPS figures stand throughout.

**W5b — treat charter R2 as the load-bearing measurement, not only a feature.** Putting a
normal Bob-omb in the scene is the first time behaviour scripts, object-list traversal
and collision-against-something-that-moves are exercised at all. Plan the R2 capture as
deliberate instrumentation: profile with **one** actor, then several, and report how master
work scales with active object count. That scaling curve is what decides whether the
simulation is a Saturn-shaping target or a rounding error — and it is currently unknown
in either direction.

**Sequencing:** W5a is a prerequisite for interpreting W5b. Both are prerequisites for
deciding whether a sixth workstream (Saturn-shaping the simulation itself — fixed-point
`vec3f`, collision rework, behaviour-script cost) is justified. **Do not open that
workstream on intuition; open it on the scaling curve.**

**Risk if skipped:** R2, R3 and S6 each add actors. If simulation cost scales steeply,
cadence regresses as the game is restored and we will be optimizing the render path
while the frame is spent elsewhere — the same error this plan was written to correct,
one level down.

---

## W6 — Make the math appropriate for the SH-2

**Owner ruling (2026-08-17):** *"sm64 can be recreated with q16 math with enough
effort."* Recorded as decided direction. The two parts below differ enormously in
risk and must not be conflated — **W6a is verifiable by an existing gate; W6b needs a
harness that does not exist yet.** Do W6a first regardless: it is free, it is
whole-program, and it buys time on the very code W6b would later convert.

### W6a — Replace the soft-float *implementation* (bit-exact, zero semantic risk)

**Why:** we link **GCC's generic portable C reference implementation** —
`third_party/gcc-soft-fp/soft-fp/` (`addsf3.c`, `mulsf3.c`,
`divsf3.c`, `adddf3.c` …), built by `SOFTFP_VENDOR` at `Makefile.saturn.mk:2090`.
That code is written for correctness across every target GCC supports, then compiled for
SH-2. It is not tuned for this CPU, and it leaves `dmuls.l` on the table for mantissa
work.

**A tuned SH-2 soft-float speeds up every float site in the program at once** — including
the entire unmeasured simulation (W5) — with **no change to results**.

**This is the only large lever in the project where correctness is *decidable* rather
than argued.** `verify-softfp-bitexact` already exists and pins the library specifically:
**43.3 billion checks, 0 failures** (T2.13 ran it by hand; note T2.26-era tooling notes it
has an MSYS path defect that must be worked around, not ignored). A replacement is
bit-exact or the gate fails. No oracle to design, no tolerance to argue, no gameplay risk.

**Why it de-risks the whole question:** it pays proportionally to whatever float's share
turns out to be. If float is 20% of the frame a 2x implementation buys 10%; if it is
50% it buys 25%. **It does not require W5's measurement to land first.**

**Open before promising a figure:** verify live what permissively-licensed SH-2
soft-float exists and how fast it actually is. GCC's own SH `lib1funcs.S` is a
candidate and **GPL is not a barrier** — the project is GPL-compatible (owner, 2026-08-17)
and `UPSTREAM_CODE_LEDGER.md` authorises direct reuse; GPL-derived code lands in
`src/port/saturn/gpl/` with notices preserved. Pin the SHA, record licence and
reuse mode. **Do not hand-roll before searching** — per `CLAUDE.md`'s reference-code-first
rule, and note `find-library`'s vetted register had **no rows** for the analogous mesh
search (T2.24), so expect to do live verification rather than lookup.

### W6b — Q16 the simulation

> **PREMISE INVERTED by the PS1-port study (2026-08-22). Read this before scoping.**
> **The most successful no-FPU SM64 port did not convert the simulation. It made
> floats cheap instead** — a hand-written, deliberately non-IEEE soft-float, plus
> fixed point applied only where the *storage representation* had to change anyway
> (render matrices, object fields, surface normals) or where cost was concentrated
> (camera, collision queries, shadows).
>
> `malucard/sm64-psx` keeps `MarioState.pos/vel/forwardVel/floorHeight` as `f32`, and
> `mario_actions_airborne.c` — the jump arcs — is **untouched**. **Second independent
> data point:** the Nintendo DS port (`Hydr8gon/sm64`, ARM9, no FPU, **CC0-1.0**) did
> **zero** fixed-point conversion of game logic and relies on stock soft-float. Two
> no-FPU ports, both playable, both declined to convert the tuned simulation.
>
> **That is the strategy W6a already executed here**, independently — replacing the
> soft-float library rather than call sites. W6a is therefore not merely a first step;
> it is the same primary move both reference ports made. **W6b's core deliverable
> should shrink to a soft-float quality bar plus a small set of representation
> changes, not a math conversion.**

**What transfers (no-FPU-driven):**

- **The parallel-twin migration architecture, and it is the single best idea in the
  reference.** Every converted function gets a `q`-suffixed twin; the original float
  name survives as a one-line shim converting in and out. Call sites migrate
  individually, there is never a flag day, and an unmigrated caller is always correct
  — just slower.
- **Storage representation as a compile-time switch.** `FIXED_POINT_FIELDS` flips
  object fields between `f32` and `q32` storage, with fixed/float/integer accessor
  triads all defined in *both* configurations so nothing breaks at the flip.
- **A per-function float/fixed equivalence oracle** (`USE_FLOATS`) that delegates to
  the float original in the same running build. **Note the reference let this rot** —
  its Makefile says "hasn't been needed for a while" — and it has no CI. **We have PC
  source-state traces already, so we can hold this to a standard they never did.**
- **Bake precision offline, quantise exactly once.** Surface plane constants and
  normals are computed in full float at level load and stored already converted;
  per-frame collision never touches a float.
- **Rescaling both sides of a sign-only predicate**, which cannot change the answer but
  buys large headroom against intermediate saturation.

**What does NOT transfer (GTE-driven) — and the trap:**

- **`FRACT_BITS = 12` is a GTE constant**, not a considered precision choice: the
  GTE's `SF` flag shifts by exactly 12 and its rotation registers are 1.3.12.
  **Inheriting Q19.12 for Saturn would be copying a PS1 hardware detail for no
  reason.** W6b must include an explicit **range analysis of SM64 world-space values**
  before picking a fraction width. A 16-bit fraction is arguably cheaper on SH-2 (the
  high word of a 32x32->64 product is the answer with no shift) but halves the integer
  headroom the reference *relies on* for its truncate-to-integer-units shortcuts.
- `ShortMatrix`'s `s16[3][3]` union with `u32[5]` is literally the GTE control-register
  file; their matrix multiply is four coprocessor ops. **All of that is CPU work for
  us, so the *renderer* half of W6b is strictly bigger than theirs.**
- **Their soft-float uses a GTE register as a count-leading-zeros instruction.** The
  SH-2 has no CLZ at all, so **every cycle intuition taken from their soft-float is
  optimistic for us.** Measure an SH-2 `__addsf3`/`__mulsf3` before committing to
  "make floats cheap" as the primary strategy.
- Their `s8` normals (scale 120) and `s16` whole-unit matrix translations are **2 MB
  memory-pressure decisions**, not precision decisions. We have the 4 MB cart. Adopt
  the principle; **re-derive the numbers.**

**Scope recommendation:** do **not** convert Mario's step, action state machine or
airborne physics. Convert the *data they read* — surfaces, object fields, camera — and
for each interface state the quantisation introduced and its worst-case effect on a
named observable (slope classification threshold, wall push distance, ledge-grab
reach, object interaction radius). **This is gated on the SH-2 soft-float measurement
above:** if our soft-float is too slow, leaving Mario in float may not be affordable
and this scope grows back.

**Budget for one specific bug class.** The reference's "fix floating trees" commit is
a **one-character sign error** in a hand-converted matrix expression that shipped and
persisted. Hand-transcribed fixed-point compiles cleanly and looks plausible; only
looking at the screen finds it. The equivalence oracle above is the mitigation.

**Verification note:** the reference has **no test suite, no reference trace and no
N64 cross-check**. It also ships SM64's attract-mode demos — a free determinism
oracle — but cannot run them because its title screen is unfinished. **We should run
ours.**

**Source:** `malucard/sm64-psx` at `b8d1c550a0d2` (archived; migrated to
`codeberg.org/malucart/sm64-psx`), `Hydr8gon/sm64` (CC0-1.0). Study is
behaviour/architecture only — no code transcribed. **See the D8 note below.**

**Sequencing:** after W6a (free speedup on the same code) and after W5a (so the category
is measurable and the win is attributable). W5b's actor-scaling curve tells you which
parts of the simulation are worth converting first — convert the hot ones, not all 1,827
static sites, six of whose top nine never execute on this route (T2.20).

**Scope discipline:** this is the largest workstream in the plan and the only one that
can change gameplay. It should be split into its own implementation plan per subsystem
(collision, camera, object physics), each with its own divergence budget, rather than
attempted as one conversion.

---

## Explicit non-goals

- **Further renderer micro-optimization.** Four consecutive investigations at 0.07–0.5%.
- **Slave offload — until its dependencies are broken.** Capacity is not the constraint:
  T2.26 measured **99.24% of the 6.11 VB slave window surviving** the coherency tax, with
  only ~1.18× to move a unit of work across. What blocks it is shared state — the admission
  BFS over visited/queued/seen (`saturn_scene_admission.c:487-745`, ~0.996 VB) and the
  sequential VDP1 command arena with its shared Gouraud bank
  (`saturn_demo_render.c:4729-4775`, ~1.435 VB). **SlaveDriver leaves both serial too**
  (T2.25), which is evidence the dependency-breaking is genuinely hard rather than merely
  unattempted. Largest currently eligible item is **~0.367 VB** (terrain depth-bin merge as
  a fifth graph job), which keeps its payoff under the tax (+4.29% → +4.47%).
  **⚠ OPEN — external research not done.** A survey of how other Saturn projects broke
  shared-state dependencies was dispatched and died on a transient 529. Until it runs, we
  know only what our two in-tree references do.
- **Widening the slave dispatch window — CLOSED AGAIN by W7 (2026-08-21).** The
  "partially reopened" text below stands as history only. W7 established that the
  master's wait is a `transfer-epoch` stall on an already-submitted DMA (VDP1's
  `EDSR`/`COPR` are unchanged across the whole wait), that it is worth
  **82,738 cycles/frame at route tick 150 and 0 at ticks 30 and 210**, and that offload
  cannot convert it because the release is an observed VBlank, not master
  availability. `docs/saturn/evidence/reports/w7-master-wait-attribution.md` §5.
- ~~**Widening the slave dispatch window — PARTIALLY REOPENED by W6a.**~~ T2.25 closed
  this because master idle was **0.000000**, so overlap converted zero VB. **W6a moved
  master idle to 5.22%**, so that premise no longer holds as stated. What still stands
  from T2.25: SlaveDriver's adaptive controller partitions from the master's join spin
  count and **its fixed point is our current configuration**; we already split at 100%
  rather than its tuned fraction; and the two blocking dependencies (admission BFS over
  shared visited/queued/seen state, ~0.996 VB; sequential VDP1 command arena with its
  shared Gouraud bank, ~1.435 VB) are the same two **upstream also leaves serial**.
  **What changed is only that there is now stall to overlap into.** Do not reopen this
  on the idle number alone — first establish *what* the master waits on, because if it
  is a hardware-gated wait rather than a dependency stall, offload still converts
  nothing. T2.26's measurement stands: 99.24% of the slave window survives the
  coherency tax at ~1.18x transfer cost, and bus contention remains unmodelled.
- **User clipping / HSS.** System clipping is already the full screen rect
  (`main.c:2328` → `saturn_vdp1_backend.h:69-70`), so a screen-sized user clip removes zero
  pixels; and `VDP1CalcCommandTiming` (`ymir-agent/.../vdp.cpp:1101`) never consults
  `sysClip`, `userClip` or `CMDPMOD`, so this class of lever is **unmeasurable on this rig**
  regardless (T2.19b).

---

## Charter D8 should be re-decided — the licence blocker has changed

D8 records `malucard/sm64-psx` as **behaviour-only because "License not
established."** That was accurate when written. It is now out of date:

- The repo is a **fork of `n64decomp/sm64`, which is CC0-1.0** — the substrate is
  already public-domain-dedicated.
- On **2026-08-05**, after the prior study, the owner stated publicly in
  [issue #34](https://github.com/malucard/sm64-psx/issues/34) (`author_association:
  OWNER`) that he does not claim ownership of the whole, that his own work "can be
  considered to be in the public domain and not enforceable by me", and that he has
  "no interest in limiting it further". The vendored `ps1-bare-metal` is MIT.

**Why this is not automatically a green light:** it is an issue comment, not an SPDX
file or a CC0 deed, and it is one contributor's statement about a repo with merged
third-party PRs — it does not by itself relicense anyone else's commits. The repo is
also now archived on GitHub.

**The 2026-08-22 study was conducted behaviour-only regardless**, so nothing in this
plan depends on the answer. But the owner should re-decide D8 deliberately rather than
leaving it resting on a superseded fact. **And note `Hydr8gon/sm64` is CC0-1.0 with a
real licence field — unencumbered, and a legitimate architectural comparison for our
platform boundary.**

---

## Standing hazards for every workstream

- **Eight host gates have been found silently verifying nothing**, most by literal-text
  drift. Two would have caught real defects: `verify-render-snapshot-bank` (the double-emit
  deletion) and five cross-SH-2 scheduler gates dead for 130 commits **while Sprint 2 was
  reasoning about master/slave partitioning** (T2.25). A dedicated sweep is unscheduled and
  is the highest-confidence non-cadence work available.
- **The T2.11 concurrency rail needs its allowance on 28 of 29 intervals.** The phase
  decomposition no longer closes and **must not be quoted**; `summarize_cadence` reads
  presentation-edge deltas independently and stands.
- **Ymir models no inter-SH-2 bus arbitration** (`Bus::GetAccessCycles` is a static
  per-page lookup with no notion of the peer CPU). Cache coherency *is* now modelled
  (`--sh2-cache`, T2.26) but miss cost is under-priced (`// TODO: stall bus for 4 accesses`;
  the active table is calibrated for caches-off at HWRAM 2 cycles against a commented-out
  8/16). **Any cross-CPU figure is an upper bound.**
- **Warm-up must be `--warmup-ticks 150`.** The route holds 120 ticks of no input by
  design; Mario renders from tick 7 but is stationary until 121 (T2.20).
- **Never mix caches-on and caches-off figures in one table.**
