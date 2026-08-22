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
motivated W0. The current owner-accepted artifact is W0
candidate `id-e8720d58595d9a62` (manifest
`fd9e1ffbf2b2e23e2f14706a6173b1e72cf207f36b4328c02636cb109cc01990`, CUE
`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`, ELF
`021f5fee2e0d5a24a1077bc7c41728453e9dc6fc4f4d1cc4c595bdf108b96c50`).

> **STRUCTURAL CHANGE, W6a (2026-08-21): the master is no longer saturated.**
> Idle went from exactly **0.000000 to 5.22%**. W6a shed **398,163 soft-float
> cycles/frame** but the frame shortened by only **200,589** — and the **197,574
> residual is independently confirmed by the new idle share** (5.2248% x 3,810,963 =
> 199,115, agreeing to 0.8%). **The master converted almost exactly half its saving
> into VBlank waiting.**
>
> Two consequences, both of which change this plan:
>
> 1. **Roughly half of any further arithmetic win will be absorbed the same way** until
>    we find what the master now waits on. That discounts W2 and W6b directly.
> 2. **The argument that closed the slave workstream is partially reopened.** T2.25
>    declined offload because "overlap converts zero VB when the critical path has no
>    stall in it" — which was true at 0.000000 idle and **is no longer true at 5.22%**.
>    The non-goal below is amended accordingly.
>
> **BOTH CONSEQUENCES ARE WITHDRAWN BY W7 (2026-08-21).** See
> `docs/saturn/evidence/reports/w7-master-wait-attribution.md`.
>
> - **The 5.22% does not survive a longer window or a second route position.** The
>   same route position traced for **4.83 contiguous frames** instead of 1.98 contains
>   the **same single wait block** and reports **2.1450%**; route tick 210 reports
>   **exactly 0.000000** over 2.63 frames; **route tick 30 — the position the +5.26%
>   cadence pair was measured at — reports 0.000000 over 3.84 frames**; `--sh2-cache`
>   at tick 150 reports **0.000000** over 26.55 VB. The master is still essentially
>   saturated on most of the route.
> - **Consequence 1 is wrong.** Absorption is 27.7% at tick 150 and **≤ 0% at ticks
>   30 and 210**, where the frame shortened by 200,577 and 262,294 cycles with zero
>   candidate idle — and tick 210, the heaviest scene sampled, is where W6a's FPS gain
>   is *largest* (+6.30%, against +5.60% at tick 150 and +5.26% at tick 30).
>   **W2 and W6b are worth 80–100% of face value, not 50%.**
> - **Consequence 2 is wrong.** The wait is a `transfer-epoch` stall on an
>   already-submitted DMA, waiting for the next field to be allowed to poll it again.
>   Offloading master work does not make that poll arrive sooner, and at tick 210
>   there is no stall to overlap into at all. **The slave non-goal is closed again.**
> - **The W6a residual arithmetic mixed route positions**: a tick-30 cadence against a
>   tick-150 idle share. Single-route pairs are in W7 §3.2.
>
> **Finding what the master now waits on is the highest-value next measurement.**

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

## 3. Sequencing against the charter

| Charter phase | Relationship to this plan |
|---|---|
| **R2** Normal Bob-omb | Costs frames. Wants W1 headroom first. |
| **R3** Mario animation + HUD | Costs frames. Blocked on the two-bank selector (below). |
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

**⚠ OPEN — sizing not re-derived.** `STATE.md` carries **2.0718 VB/frame**, but that
figure descends from the burst profiler T2.20 discredited. **The first task of this
workstream is to re-derive the ranking from `capture_idle_attribution`'s exact census at
`--warmup-ticks 150`** and confirm or correct it. Do not act on 2.0718 until then.
(T2.27 was dispatched twice to do exactly this and died both times on transient
server-side 529s; the brief is written and ready to re-run.)

**Leads, to verify not to trust:**
- `_sm64_saturn_ztreme_frustum_aabb` ~0.649 VB/frame (T2.16) — **and this is the function
  T2.10 item 3 made 0.075 VB *worse*** by cross-multiplying divides against a mispriced
  serial-latency model. Read that history first.
- `_saturn_geo_enter_object` was T2.13's top float caller; `_saturn_geo_enter_camera` is a
  fully-Q16 sibling available as a template.
- A 0x1400-entry Q16 trig table already exists in tree (used by T2.13's shadow path).
- Census items 2 and 5 are static phantoms on this route — deprioritised.

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
recommendation: per-material decimation ratios in meshoptimizer** — protect the face,
coarsen the limbs — no new dependency, no rebake, and it keeps the
collapse-to-existing-endpoint property that made pose survival free.

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

**The premise is sound and the codebase already half-agrees.** SM64's rotations are
**already fixed-point**: angles are s16 binary (BAM), `atan2s` returns an exact s16, and
90 degrees is exactly `0x4000`. T2.13's finding makes the point sharply — the shadow path
was taking an angle `atan2s` had **already produced exactly**, scaling it to degrees, then
back to radians, to call a double-precision polynomial. **The float was the detour.**
Positions, velocities and collision are the f32 remainder.

**The real cost is verification, not conversion.** Unlike render-path float (T2.13:
bounded error, sub-pixel movement, owner ruled byte-identity irrelevant), simulation
arithmetic decides **what the game does** — jump arcs, floor-detection epsilons,
wall-push distances. A divergence here is not a fidelity tradeoff; it is Mario falling
through a floor or failing a gap he used to clear.

**The harness this needs, and why it is tractable here:** a **dual-run state-divergence
comparison over the deterministic replay route**. Run the f32 simulation and the Q16
simulation against the same route and compare `gMarioState` and object state
**per tick**, reporting divergence as a curve rather than a boolean. The infrastructure
already exists: the route is deterministic, `sState.input_replay_ticks` indexes it exactly
(T2.19d proved it is the index, not a correlate), and captures are tick-addressable.

**Acceptance depends on whether a given subsystem is ported or recreated (see 2a) —
and that decision is design work owed before any conversion starts.**

- **Where the algorithm already fits** — anything already keyed to s16 angles, integer
  positions, or Q16 quantities on a detour through float — treat as a **port**. Accept on
  bounded, **non-accumulating** divergence, the same standard T2.13's memoryless
  substitution met. **Divergence that compounds across ticks is a defect at any starting
  size**, because simulation state feeds itself forward; design the harness to detect
  compounding specifically rather than magnitude.
- **Where the algorithm does not fit** — f32 collision epsilons, physics tuned to float
  rounding — **recreate**, and accept on **behaviour**: does Mario clear the gap he used to,
  does the floor hold him, does the jump feel right. **Per-tick divergence is the wrong
  measure here and will reject good work.** This needs owner play-testing, not a
  bit-comparison, and it should be scoped so the owner is asked a small number of
  answerable questions rather than handed a diff.

**The dual-run harness is still worth building** — the route is deterministic,
`sState.input_replay_ticks` indexes it exactly (T2.19d), and captures are
tick-addressable — but as an **instrument for the ported parts and a change-detector for
the recreated ones**, not as the gate for both.

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
