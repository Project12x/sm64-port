# Saturn Overlapped Render Pipeline Architecture

**Date:** 2026-08-03
**Status:** Approved architecture; implementation plan ready, execution pending
**Scope:** Full-game Saturn source port, with BOB as the deterministic proving
ground
**Implementation plan:**
[`2026-08-03-saturn-overlapped-render-pipeline.md`](../plans/2026-08-03-saturn-overlapped-render-pipeline.md)
**Living-document rule:** Update the status, decision ledger, implementation
map, and evidence links during every implementation task transition. Do not
reconstruct them at the end of a wave.

## Outcome

The Saturn port will preserve authoritative SM64 simulation while replacing
the current same-frame serial render path with an overlapped, snapshot-driven
pipeline. The display presents the framebuffer plotted from render snapshot
`N` while the master SH-2 advances fixed-step simulation `N+1`. The slave SH-2
consumes immutable, bounded render jobs opportunistically. Rendering has one
terminal completion boundary rather than a sequence of immediate joins and
DMA waits.

This is an architecture correction, not a game rewrite. Existing fixed-point
camera work, generated Saturn IR, BSP/fragment data, LOD metadata, template
commands, depth bins, dual-SH2 transport, DMA infrastructure, deterministic
replay, VDP2 HUD, and verification tooling remain inputs to the new pipeline.

## Why the completed sprint is still slow

The 2026-08-02 sprint improved individual stages, but the manual BOB build on
2026-08-03 remained in the low single digits because the stages are still
connected in a largely serial frame:

1. `game_loop_one_iteration()` still reaches `render_game()` and
   `geo_process_root()`. Display suppression skips final display-list execution
   and VBlank waiting, but not the original scene-graph traversal and display
   list construction. The separate Saturn IR renderer then prepares the scene
   again.
2. Gouraud transfer is queued and immediately followed by VDP1/DMA waits, so
   the current queue does not create useful overlap.
3. The planned DMA command upload is disabled; the active backend waits for
   VDP1 idle and copies staged command words from the master CPU.
4. Terrain and Mario each use same-frame fork/join phases. The master spins at
   their boundaries instead of advancing independent work.
5. Worker failure can cause a full serial replay of a phase, including work
   the master already completed.
6. Terrain LOD is selected after position admission/transform, and Mario
   transforms its full mesh before classification. Geometry suppression does
   not eliminate the expensive transform work.
7. Mario builds a full draw list and insertion-sorts it by repeatedly reading
   projected depth and dividing. At 644 primitives this can approach 207,000
   comparisons before drawing.

The existing `VDP1W` HUD field does not measure the earlier backend wait and
therefore cannot clear VDP1 upload/presentation as a bottleneck. Slow frames
also accrue simulation credits and may execute up to four catch-up ticks,
creating a feedback loop.

## Non-negotiable architecture invariants

- The master SH-2 alone owns authoritative SM64 state, input application,
  allocation, final command ordering, VRAM ownership, and presentation.
- The slave receives immutable integer snapshots and bounded job descriptors.
  It never dereferences live `MarioState`, graph nodes, allocator state, VDP1
  VRAM, or VDP2 registers.
- Simulation remains deterministic and fixed-step. Presentation may lag the
  authoritative simulation by one completed render snapshot.
- Saturn rendering must not construct the same scene through both the
  original Fast3D display-list path and the Saturn IR path.
- Admission, LOD, and cheap rejection happen before expensive transforms.
- A job has exactly one owner. Failure recovery processes only unclaimed work;
  it never replays the master and slave ranges together.
- There is at most one required render-worker fence and one required transfer
  retirement boundary per presented frame.
- RAM banks cannot be reused until their worker and DMA tickets retire.
- VDP1 owns scene geometry and sprites. VDP2 owns sky/background, HUD, and
  layer composition. Neither processor draws duplicate scene geometry.
- BOB-specific data may prove the design, but runtime interfaces and generated
  formats remain scene-neutral for the full game.
- Experimental CUEs may be built and manually tested before the strict static
  native-math publication census passes. Publication still requires the
  strict gates.

## Frame lifecycle

### Authoritative simulation and snapshot publication

The source loop advances gameplay, objects, animation, collision, and camera
on the master. Instead of constructing a second, unused Fast3D frame, the
Saturn boundary publishes a compact immutable render snapshot containing:

- camera/view/projection values already lowered to target integer formats;
- actor transforms, animation/pose selectors, visibility flags, and material
  state required by the Saturn IR;
- scene/area identity and immutable generated-bank references; and
- a monotonic generation plus explicit bank ownership state.

The first implementation may bypass only the rendering side of
`render_game()`/`geo_process_root()`. It must not skip gameplay callbacks that
affect authoritative state. Any callback discovered to mix gameplay mutation
with render construction must be split or explicitly retained before the
original render walk is disabled.

### One-frame relationship

At steady state:

1. The display presents the completed framebuffer for snapshot `N-1`; VDP1
   may plot the already-published VRAM command list for snapshot `N` into the
   other framebuffer.
2. The slave consumes immutable jobs for the next source snapshot/command bank
   while the master advances authoritative fixed-step simulation and then
   claims any remaining render jobs instead of waiting.
3. At the safe boundary, the completed framebuffer for snapshot `N` becomes
   display-visible. A separately completed source command bank is transferred
   and its VRAM list published only after its worker and transfer tickets
   retire.
4. An incomplete command bank is never published and an incomplete
   framebuffer is never displayed; the previous complete resources remain
   active and owned banks remain unavailable for reuse.

After the safe presentation transition, the display presents the framebuffer
for snapshot `N` while authoritative simulation is at `N+1`. Input affects
authoritative simulation immediately; the visual result appears one completed
render snapshot later. Camera, Mario, actors, and terrain all use the same
snapshot generation, preventing mixed-frame imagery.

## Shared opportunistic SH-2 work queue

Replace terrain and Mario's independent fixed-range fork/join calls with one
bounded queue. Jobs are coarse enough to amortize publication overhead and
small enough to balance uneven visibility. Initial job classes are:

- static-world cluster admission and compact position marking;
- admitted world transform/classify/lower chunks;
- actor/Mario meshlet admission and transform/classify/lower chunks; and
- optional compact translucent-bin production.

Each descriptor contains only immutable bank identifiers, integer snapshot
values, input spans, disjoint output spans, capacity, and generation. Job state
uses an explicit atomic/uncached progression such as `READY -> CLAIMED_MASTER`
or `READY -> CLAIMED_SLAVE`, followed by `DONE` or `FAILED`. A persistent slave
consumer claims ready work. When the slave is busy, the master claims another
ready job instead of waiting.

The master performs the sole terminal join after it exhausts useful simulation
and render work. It then merges compact outputs in deterministic source/bin
order. If a job fails, the master processes only jobs never claimed or
explicitly returned as failed. A merely timed-out `CLAIMED_SLAVE` span is never
reclaimed while the old worker may still write it. Recovery requires an
acknowledged cancellation/worker reset followed by positive retirement or
quarantine; only then may the missing job be retried in a fresh generation and
bank. If retirement cannot be established, the renderer enters an explicit
degraded state, retains the previous complete frame, and permanently
quarantines that bank. A late old-generation result is rejected.

## Early admission and real LOD

Generated scene data gains tight cluster bounds and per-LOD compact position
lists/remaps. The runtime order is:

1. coarse frustum/portal/BSP admission;
2. cheap cluster-bound near/backface/projected-window rejection only where it
   is derivable from generated metadata;
3. hysteretic LOD choice from snapshot camera state;
4. compact used-position admission for the chosen representation;
5. transform only admitted positions; and
6. perform per-primitive backface/zero-area/window rejection after projection,
   then classify, clip, shade, bin, and patch only surviving primitives.

Near-to-far traversal is retained so the most useful geometry survives bounded
capacity. Loose cubic bounds and experimental PVS are not adopted. Far
clipping is not credited to SlaveDriver because the pinned build disables it.

Mario and future actors are baked into meshlets with tight bounds, material
partitions, source-order identity, and compact position/remap lists for each
LOD. Opaque meshlets do not enter a global comparison sort. Translucent
primitives use stable fixed depth bins (or the already-proven bounded radix
alternative if evidence requires it), preserving source order within a bin.

### Dynamic actor and visibility scope correction (2026-08-04)

Mario is the proven reference actor, not a one-off exception: the legacy
Castle demonstration already exercised source-driven full Mario animation.
The Saturn work preserves that animation/pose bridge and reduces its render
cost through meshlets, rather than recreating an animation system. Enemies
follow the same renderer contract—live source pose plus generated bounds,
material partitions, and draw data—but require per-family extraction and
feature coverage before they are admitted to the target path.

Coarse BSP/frustum admission and generated portal/window metadata are planned
for scene-neutral levels. A general portal/PVS or occlusion system is not
currently an acceptance promise: it must be added only after measured
per-level VDP1 evidence shows that the coarse path is insufficient.

## Command construction, DMA, and presentation

The runtime alternates at least two source command/Gouraud staging banks. Each
bank has independent worker, command, transfer, and presentation generations.
Static command/material fields remain compile-once templates; per-frame work
patches only dynamic coordinates, links, depth/bin metadata, and genuinely
dynamic shade entries.

The source regions are intentionally split by hardware capability and memory
budget. Two 2,048-entry `vdp1_cmdt_t` banks require 131,072 bytes, while the
sourceboot linker preserves only `0x1B00` bytes of HWRAM margin; command banks
therefore remain in `.lwram_cmdts` and use SH-2 CPU-DMAC (or the measured CPU
copy fallback) into VDP1 VRAM. Gouraud staging remains HWRAM-resident and may
use SCU DMA. This replaces the earlier generic HWRAM wording; it is required
by the pinned Yaul memory map and the documented SGL/SCU LWRAM restriction.

The completed bank is transferred at a hardware-safe boundary using Yaul's
supported CPU-DMAC/SCU-DMA mechanisms for the source and destination regions.
LWRAM is not submitted to SCU DMA. Indirect DMA may gather disjoint command
spans when the descriptor and alignment costs are justified. The master waits
only when it is about to reuse a still-owned bank or when presentation requires
the final transfer to be complete.

The first implementation retains one VDP1 VRAM list unless the verified VRAM
budget supports two. SlaveDriver source-demonstrates two VDP1 command/Gouraud
VRAM regions in its engine, while Z-Treme's bundled SGL material documents
alternating RAM sprite-control staging halves and an indirect transfer table
into the standard VDP1 list. These are separate valid strategies and must not
be conflated. VRAM double-banking is an optional measured extension, not a
prerequisite for removing immediate waits.

VDP2 commits sky, background, HUD, and layer state during the same presentation
boundary. HUD timing fields must measure the actual earlier worker, transfer,
and VDP1-idle waits rather than only the later nonblocking sync calls.

## Cadence and overload behavior

The engine never launches an unbounded catch-up burst. A slow rendered frame
may advance a tightly bounded number of fixed simulation steps; excess wall
time is dropped or amortized according to a documented policy. Rendering may
reuse the previous complete command bank when the next snapshot misses its
deadline. It must never publish a partial bank.

This preserves simulation determinism while preventing a slow render from
causing four simulation ticks, which causes another slower render and further
backlog.

## Preservation map

| Existing work | Role in the corrected architecture |
| --- | --- |
| Q16 camera and graph math | Produces snapshot camera state without hot float helpers. |
| Generated Saturn mesh IR | Immutable scene/actor input to early admission and job creation. |
| BSP node spans and fragments | Coarse scene-neutral visibility/order metadata. |
| Required-position transform set | Becomes per-chosen-LOD compact position admission. |
| Stable depth bins | Orders translucent/dynamic survivors without comparison sort. |
| Cache-through handoff | Carries queue ownership, generations, and completion records. |
| DMA queue | Retained after removing immediate retirement and correcting region/channel use. |
| Compile-once VDP1 templates | Forms alternating patchable command banks. |
| Hysteretic LOD | Moves before transform and selects genuinely compact representations. |
| Mario snapshot/worker | Becomes actor meshlet jobs in the shared queue. |
| Shade classification | Runs after admission and avoids unnecessary Gouraud allocation. |
| VDP2 frame API/HUD | Owns composition and reports the true terminal waits. |
| Replay/native-math tools | Protect deterministic behavior and final publication; they do not block experimental CUEs. |

## Reference-code record

### SlaveDriver

- Repository: `Lobotomy-Software/SlaveDriver-Engine`
- Pin: `a8986591557b6e680550d3c23970284d3b38ff8f`
- License: GPL-3.0-or-later
- Inspected: `SPR.C`, `WALLS.C`, `WALLASM.S`, `DMA.C`, `DMA.H`,
  `V_BLANK.C`, and the scene-loop call sites recorded in the upstream ledger.
- Reuse mode: existing small worker/projection adapters remain attributed
  close ports under `src/port/saturn/gpl/`; the new scheduler, snapshot, and
  pipeline are pattern-informed project code unless a later task explicitly
  records a close port.

Adopted lessons are two VDP1 command/Gouraud regions, buffered chunk uploads,
root-link bank selection, early portal/window rejection, pre-transform
mip/subdivision selection for tiled rectangular walls, disjoint master/slave
ownership, late join, and overlapping world rendering with object work.

The new design's explicit wait-at-source-bank-reuse and publish-after-all-
tickets-retire rules are project hardening, not claims about SlaveDriver.

Correction: although `DMA.C` contains queue routines and comments describing
interrupt-driven chaining, repository-wide inspection found no callers of
`qDMA`/`startNextDma` outside their definitions and no active VBlank queue
drain. The active engine path serializes consecutive transfers. SlaveDriver is
therefore prior art for the queue data structure and bank lifetime, not proof
that its active DMA path is asynchronous.

### Sonic Z-Treme and bundled SGL material

- Repository: `Maxime-XL2/SONIC-Z-TREME`
- Pin: `cff75451c1616aac1236fc2b44223902b55c706b`
- License: GPLv3 for XL2-authored code under the repository's license and
  owner authorization.
- Inspected: `ZT_RENDERING.c`, `ZT_FRUSTUM.c`, `ZT_LOADING.c`, `workarea.c`,
  relevant project loop code, and bundled SGL documentation/disassembly
  references.
- Reuse mode: Z-Treme code may inform attributed GPL-compatible close ports
  when recorded per destination; bundled Sega SGL documentation/disassembly
  remains pattern-only/clean-room because its redistribution license is not
  established. Sonic assets, music, maps, and SGL binaries are not reusable.

Adopted lessons are static/dynamic shading separation, fixed work arenas,
early AABB/LOD admission, near-to-far traversal, alternating RAM
sprite-control staging halves, indirect transfer tables into the standard
VDP1 list, opportunistic master/slave work ownership, a persistent slave
consumer, one terminal fence, and render/simulation overlap. Loose map bounds
and the unsuccessful PVS experiment are rejected.

## Live implementation map

These checkboxes describe the approved architecture, not completed code. The
follow-on implementation plan must expand them into test-first tasks and link
each task back here.

- [ ] **A1 — duplicate-render removal:** identify every stateful callback in
  the source render walk; retain/split required mutations; bypass unused
  Fast3D construction on Saturn; produce an experimental BOB CUE immediately.
- [ ] **A2 — snapshot banks:** source-complete immutable scene/camera/actor
  snapshot generations, ownership states, mixed-generation rejection, and host
  tests. Runtime-contract closure and independent reviews remain required.
- [ ] **A3 — pre-transform admission:** generate tight cluster/meshlet bounds
  and compact per-LOD used-position/remap lists; select before transform.
- [ ] **A4 — Mario/actor ordering:** remove full-mesh transform and quadratic
  insertion sort; batch opaque meshlets and bin only order-dependent work.
- [ ] **A5 — shared work queue:** replace terrain/Mario fixed splits and joins
  with persistent opportunistic jobs and exactly-once ownership.
- [ ] **A5.6 — live payload/callback cutover:** migrate the physical
  master/slave terrain and actor payload partitions to exact descriptor-owned
  output slots, then install the queue as the one CPU-DUAL polling owner. A
  queue release record alone is insufficient: it cannot make an existing
  fixed-split payload cache-safe.
- [ ] **A5.7 — dependency-aware job graph:** preserve terrain and actor phase
  dependencies and terrain's ordered multi-result identity before A5.6
  migration. Consumers remain ineligible until producers are `DONE`; failed
  producers quarantine ready dependents. Graph metadata is P2-visible but
  renderer-local, so queue descriptors remain pointer-free and fixed-width.
- [ ] **A6 — localized recovery:** recover only unclaimed/failed jobs, reject
  stale generations, and allow previous-complete-frame presentation.
- [ ] **A7 — alternating source banks:** source-complete, review pending. Patch compile-once templates in
  LWRAM command banks and HWRAM Gouraud banks with explicit
  worker/DMA/presentation tickets.
- [ ] **A8 — deferred transfer/presentation:** enable valid command/Gouraud
  DMA, remove immediate waits, use one terminal boundary, and measure the real
  wait sites.
- [ ] **A9 — frame overlap and cadence:** render snapshot `N` while fixed-step
  simulation advances, enforce a bounded catch-up policy, and prevent partial
  publication.
- [ ] **A10 — full-game hardening:** prove arbitrary scene/actor banks,
  preserve gameplay/camera parity, run strict native-math/publication gates,
  and test on retail hardware when available.

## Live decision and deviation ledger
| 2026-08-05 | A7 owns source-bank truth but does not claim deferred transfer. | The old XOR selector could overwrite or mislabel a source bank, while the current upload APIs block internally and return no async ticket. A7 therefore adds FREE/BUILDING/READY/TRANSFERRING/PUBLISHED/QUARANTINED ownership, exact worker/transfer obligations, an explicit zero-Gouraud NOOP, a synchronous-complete adapter, failure quarantine, publish-new-before-retire-old fallback retention, and distinct build/published/displayed generations. A8 must replace the adapter with real submission/polling. |
| 2026-08-05 | A7 uses original project code with pattern-only prior art. | Inspected SlaveDriver `a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later, `WALLS.C`), Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0, `ZT_GAME.c`), and Yaul `6012f79f237773378c8014e70d8998ad95a38d98` (MIT, `cpu_dmac.c`). No upstream code was copied or close-ported. SlaveDriver/Z-Treme informed lifetime/synchronization shape; Yaul inspection records that its convenience transfer waits before start, a constraint for A8. |

Update this table in the same commit that changes an invariant, interface,
ownership rule, acceptance condition, or prior-art interpretation.

| Date | Decision or deviation | Status / evidence |
| --- | --- | --- |
| 2026-08-03 | Allow one completed render snapshot of visual latency while authoritative simulation remains fixed-step. | Approved by owner. |
| 2026-08-03 | Preserve completed sprint components but supersede same-frame serial scheduling. | Approved by owner after manual BOB test remained extremely slow. |
| 2026-08-03 | First visible checkpoint is duplicate source-render preparation removal; strict native-math census does not block this experimental CUE. | Approved architecture policy. |
| 2026-08-03 | A1 may suppress source display-list construction only after every authoritative mutation in the geo walk is preserved through a behavior-tested state-only seam. | Quality-fix round 1/5 blocks `4a8fe1ce^..70cb3fe1`: the current `geo_process_root()` guard skips animation timers/frames, painting and DDD-warp state, WDW water-region writes, moving-texture/flying-carpet counters, camera FOV evolution, and matrix-derived object-position writes. No target/CUE gate may start from this range. |
| 2026-08-04 | A5.5's descriptor-to-result bridge is a source-only metadata prerequisite, not an activation seam. The live renderer's fixed physical payload arrays and generic CPU-DUAL owner require A5.6: descriptor-owned payload migration plus one atomic callback cutover. | A watched static integration gate is RED before implementation; no partial queue callback registration is allowed. The accepted A3+A4 3–4 FPS CUE remains rollback evidence. |
| 2026-08-03 | Normal/promotable sourceboot must fail closed by never enabling the reserved scene-graph suppression policy. Final display submission may remain suppressed while the Saturn IR renderer owns presentation; only the separately authorized sealed D1 diagnostic may activate graph suppression. | Implemented in `77ee306c`; `geo_process_root()` runs on every normal source tick, normal-walk telemetry increments, and suppressed-walk telemetry remains zero. The D1 exception is non-promotable and cannot supply A1 correctness evidence. |
| 2026-08-03 | Permit one sealed, compile-time-only `diag-skip-geo` CUE to measure the upper bound of removing the source geo walk. | Owner authorized. It is default-off, requires demo + replay configuration, must restore the setter within the source tick, receives a distinct output tag, and knowingly invalidates graph-owned state. It is neither an A1 resolution nor a full-game/replay candidate; BOB is only the demonstrator. |
| 2026-08-03 | The project is Saturn-exclusive; PC/N64 build compatibility is unsupported and not an A1 gate. “Ordinary interpreted” means the Saturn interpreted renderer. | Owner clarification; `a00cdd17` removes the N64 syntax gate. Non-Saturn behavior matters only when it affects Saturn code or host test tooling. |
| 2026-08-03 | D1 is the sole owner-authorized exception to the fail-closed A1 rule: one default-off, demo+replay-sealed, non-promotable `diag-skip-geo` upper-bound diagnostic. It is not an A1 reauthorization, replay baseline, full-game mode, or evidence that graph-owned state is preserved. | Runtime containment is implemented at `98f26f26`; both scoped rereviews are GO after `98670c48`/`1e828879`. The serial target CUE and project-profile Ymir run completed; owner observed approximately 2 FPS with no obvious improvement. D1 is complete as a negative upper-bound result and does not reopen A1. |
| 2026-08-03 | Pull a full-game-safe single-VBlank presentation boundary ahead of A2–A8 and the broader A9 overlap task. | Initially prioritized from a counter reading later contradicted by the valid desktop observation. A9.0 still must allow at most one VDP1 submission per observed VBlank generation, never re-add credit during tick execution, permit one normal plus at most one recovery tick, count/drop excess credit, reuse the previous completed list, and commit VDP1/VDP2 together. Reference mode is close-port/pattern-only from pinned SlaveDriver/Z-Treme cadence and terminal-sync paths; no source is copied. |
| 2026-08-03 | Implement the emergency presentation boundary as a sourceboot-wide observed-VBlank fence. | `main.c` samples elapsed credit once, retains the previous completed list until a fresh generation, caps simulation to one normal plus one recovery tick, and records only whole eligible credit discarded after recovery. `sourceboot_present_generation(generation)` owns the one VDP1 sync pair and geometry-free VDP2 commit; appended profile counters expose the generation and dropped credit. Focused mutation evidence is green; independent reviews and target evidence remain pending. |
| 2026-08-04 | Redirect optimization priority from VDP2 cadence to VDP1 work reduction. | The owner observed VDP1 ≈2 FPS and VDP2 ≈60 FPS in the stable desktop-Ymir run. This invalidates the earlier VDP2-bottleneck inference and rules out treating headless trace data as runtime proof. A2 starts as the full-game-safe publication boundary for A3 admission/LOD: future scene-neutral clusters must reject geometry before transformation and before it becomes VDP1 command/plot work. |
| 2026-08-04 | A2 source bank stores no live game or presentation pointer. | The master captures scalar Mario/camera state after each source tick and publishes only fixed-width records plus generated vertex/material bank identifiers. Separate release words fence `WRITING → READY`; stale/mixed generations, double acquire, and quarantined reuse fail closed. This is a source-complete A3 prerequisite, not an FPS result; runtime-contract closure, target evidence, and independent review remain open. |
| 2026-08-04 | A2 reset is not a recovery escape hatch. | Reset may clear only already-free initialization slots; it cannot release complete/in-flight or quarantined generations. SH-2 release words and peer payloads are addressed through P2 cache-through aliases, with host identity aliases solely for contract tests. A public validator centralizes expected-generation and camera/actor coherence checks. |
| 2026-08-04 | A2 ready claims must have exactly one winner. | The uncached fixed-width release record carries a claim byte. SH-2 `tas.b` atomically converts zero to held before payload validation and `RENDERING` publication; the claimant releases only after the state is no longer `READY`. Host atomics preserve the same contention contract for the focused fixture. |
| 2026-08-04 | A2 payload visibility precedes release visibility. | Producer payload access uses the same SH-2 P2 cache-through alias as peer consumption; `begin_write` returns that address and all lifecycle bulk clears use it. Thus the master cannot leave dirty P1 payload cache lines behind while it publishes uncached `READY`. Compiler fences retain ordering only; target coherency evidence remains required. |
| 2026-08-04 | A2 terminal state ownership is serialized with ready claims. | Quarantine, completion, and positive retirement acquire the same uncached claim byte, revalidate their predecessor state while held, publish once, then release. If quarantine owns the transition first, a claimant observes a non-`READY` state and cannot overwrite it; if another owner already holds the byte, quarantine fails closed for caller retry. The focused host race fixture is evidence of ordering only; target multicore/cache evidence remains open. |
| 2026-08-03 | Retain the predecessor's bootstrap VDP2 queue-retirement barrier separately from the post-bootstrap terminal presentation boundary. | `815c4352` restores exactly one null-snapshot VDP2 begin/commit followed by `vdp2_sync_wait()` after `dbgio_flush()` and before frontend/scheduler initialization. It does not perform VDP1 work, simulation, geometry, or generation publication; fresh displayed generations still use only `sourceboot_present_generation()`. The focused source-mutation gate is RED→GREEN, but independent reviews and a replacement CUE/Ymir observation must determine whether it resolves the post-BIOS freeze. |
| 2026-08-03 | Before another timing repair, retain a persistent post-BIOS sourceboot trace in target RAM. | `30123c1b` exports `sourceboot_boot_trace`: magic/version, monotonic write sequence, last boundary ID, observed VBlank, scheduler credit, and VDP1/VDP2 presentation generations. Scalar-only writes bracket bootstrap retirement, thread5, stale wait, source tick, VDP1 render/sync, and VDP2 commit. The symbol-aware headless Ymir reader is bounded and diagnostic-only; focused source/reader gates are green, but independent review and target capture remain required. |
| 2026-08-03 | Use a shared opportunistic terrain/actor queue, early LOD/admission, alternating RAM banks, and one terminal fence. | Approved hybrid derived from pinned SlaveDriver and Z-Treme/SGL study. |
| 2026-08-04 | Queue claimant ownership must be bridged to exact descriptor-indexed result reads before any fixed terrain/Mario worker is replaced. | A5.5's bridge maps descriptor index + kind + actual queue claim to cached/P2 selection and rejects consumers before that exact job is `DONE`; it records one source-side arm but cannot register, wake, or claim to activate CPU-DUAL beside the legacy worker. The current renderer still uses fixed splits, so no live binding or target claim follows from this source-only bridge. |
| 2026-08-04 | A5.6 requires an explicit phase graph before physical payload migration. | Independent descriptors and physical lanes do not encode terrain transform→classify→ordered multi-result merge or Mario transform→classify dependencies. A5.7 adds P2-visible masks, terminal consumer eligibility, failure quarantine, and `(job_index, output_index)` identities while keeping live activation forbidden. |
| 2026-08-04 | A5.7 must reject cycles and propagate failure to a fixed point. | A one-pass dependency scan could leave a reverse-chain descendant READY, while independent READY work is correctly eligible. Publication now rejects self/cyclic masks deterministically; failure propagation repeats until no ready dependent can remain exposed before merge/reset. |
| 2026-08-04 | The sole CPU-DUAL runtime must claim through the A5.7 graph, never by a raw READY scan. | A raw queue drain can claim a consumer that happens to precede its producer in descriptor storage. A5.8.1 adds graph-aware master/slave drains and activation; its host contract publishes `WORLD_LOWER` before `WORLD_ADMIT` and proves the prerequisite still executes first. This is not live renderer activation: descriptor-owned terrain/actor payload backing and all reader migration remain required. |
| 2026-08-04 | Graph claim does not authorize a cached descriptor read. | After every graph claim, the runtime obtains the exact descriptor through the queue's P2/cache-through claimed-job accessor, which revalidates its generation and claimant state before exposing it. The source gate rejects direct `s_runtime.queue->jobs` access. |
| 2026-08-04 | A5.8 terrain producers receive their lane and span explicitly; terminal completion remains runtime-owned. | The dormant WORLD_LOWER callback passes descriptor `input_offset/input_count`, bridge-derived writer lane, and descriptor-owned arena into the common compact producer. It seals before returning true, after which graph runtime alone publishes `DONE`; terminal readers obtain both record/command aliases only from the exact DONE job index. WORLD_ADMIT still needs descriptor-indexed transformed-position publication, and WORLD_LOWER needs persistent per-job count/sequence metadata before the master can assemble a real exact-DONE merge. The fixed-range adapter remains legacy-only until Mario is converted and one atomic cutover is reviewable. |
| 2026-08-04 | Queue output offsets use four physical payload-kind namespaces rather than one synthetic global arena. | WORLD_ADMIT positions, WORLD_LOWER records/commands, ACTOR_ADMIT vertices, and ACTOR_LOWER references already occupy separate bounded arrays. Publication derives kind from the exact type/callback pair, permits cross-kind local-offset reuse, rejects same-kind overlap and malformed pairs, and preserves the pointer-free 16-byte descriptor ABI. A global layout would consume offset range without adding alias safety. |
| 2026-08-04 | Queue-reachable terrain classification must carry the claimant lane end-to-end. | The initial dormant callback repaired result routing but `demo_classify_range` still inferred its cache lane from range start. `demo_classify_exact` now takes the lane passed through `demo_terrain_compact_exact`; the only remaining `begin == 0` choice is the legacy fixed-worker adapter. The route contract pins a legal slave claim at input offset zero so it cannot silently select master/P1 ownership. |
| 2026-08-04 | The A5 live cutover is one four-phase graph and one process-lifetime CPU-DUAL owner. | Terrain admit/lower and Mario admit/lower are coarse descriptors with exact dependencies. The master publishes immutable descriptor/context state before notify, both SH-2s steal eligible work through the same table, and payload lifetime ends only after terminal queue state plus positive return from the notified slave polling entry. The master then validates/assembles terrain commands and Mario results before beginning the VDP1 backend. Failure preserves the prior complete command list; no accepted-path full-range replay or competing legacy registration remains. |
| 2026-08-04 | One coarse WORLD_ADMIT is a single transform producer even though either SH-2 may claim it. | The queue snapshot disables the legacy peer rendezvous. The actual admit claimant is recorded as the sole position producer; WORLD_LOWER reconstructs its own cached owner map from that exact DONE metadata before selecting local versus P2 position payload. This keeps legal slave-admit/master-lower and the reverse safe across generations without putting the large transform arrays in uncached memory. |
| 2026-08-04 | Observe exact phase claims before changing the coarse A5 graph. | The flat 3–4 FPS cutover result does not distinguish idle slave scheduling from a serial critical path. A bounded uncached record now captures master/slave claims by phase, exact notify/retire generation, terminal-wait iterations, failures, and quarantines; HUD/profile fields are evidence only and never scheduling inputs. The delayed-slave host fixture proves zero overlap is permitted, not that it occurs on target. |
| 2026-08-03 | SlaveDriver's active path is not evidence of asynchronous queued DMA; only its queue/bank patterns are prior art. | Corrected after pinned-source call-site audit. |
| 2026-08-03 | Keep the two 64 KiB command banks in LWRAM and transfer them with CPU-DMAC; reserve SCU DMA for HWRAM Gouraud staging. | File-map correction: two 2,048 × 32-byte command banks exceed the linker's `0x1B00` HWRAM margin, and SCU DMA cannot source LWRAM. |
| 2026-08-03 | Execute the architecture as ten reviewer-sized tasks with A1 producing the earliest manual CUE. | Implementation plan linked above; every task updates this ledger, its own steps/status, and the evidence report before transition. |
| 2026-08-04 | Generic A3 cluster depth is view-space Z, not the world-Z coordinate. | The immutable render view now carries a Q16 camera-forward vector. Admission conservatively projects each Q16 AABB onto it before optional behind rejection and hysteretic LOD selection; direct yaw/pitch fixtures prove front/behind and compact-span behavior. This resolves the final review's Critical depth defect at source level, but a fresh independent rereview and target visual/counter evidence remain open. |
| 2026-08-04 | A3 gfx headers own cross-directory GPL includes. | `saturn_render_cluster.h` reaches the isolated promotion contract via `../gpl/`, matching sourceboot's declared `gfx` include boundary. The host cluster gate deliberately omits a `gpl` include flag, so a future bare include cannot be masked by host-only compiler configuration. This is build integration evidence only; target compilation and visual/counter evidence remain open. |
| 2026-08-04 | Keep A3's bulk cluster LOD/result scratch in linker-owned CPU-only LWRAM, not HWRAM. | The A3 target link showed `s_render_cluster_lod` (0x5ED4) plus `s_admitted_cluster_results` (0x3630) overflowed HWRAM by 29,680 bytes. They neither feed SCU DMA nor cross a producer/consumer ownership boundary, so the existing `.lwram_bss` CPU-only section is the correct owner. The repaired route-0 candidate ends HWRAM at `0x060FDEF0`, leaving 0x2110 above the 4 KiB libyaul TLSF floor, and ends LWRAM at `0x002E33A0`; target visual/counter evidence is still open. |
| 2026-08-04 | A4 actor ordering is bounded by generated material/opacity meshlets, not a global primitive comparison sort. | Original project code generates at most 32 source-ordered primitives per meshlet with tight actor-local bounds and compact near/mid/far primitive/position lists. The serial master projects every meshlet's live pose vertices after Mario yaw: furthest extent rejects only wholly-behind work and orders translucent bins, while nearest extent selects conservative LOD. It passes the exact globally deduplicated generated position union to transforms, emits opaque meshlets in stable source order, and uses 64 stable far-to-near bins only for textured/translucent references. Gouraud, texture slots, terrain-relative insertion, VDP1, and presentation remain master-owned. This is host-source evidence only; fresh independent review and target visual/counter evidence remain open. |

## Verification and visible-progress policy

The goal is an obvious manual improvement from the current low-single-digit
BOB experience, followed by evidence that the result is correct. Percentage
thresholds are diagnostic, not arbitrary promotion gates.

- A1 produces the earliest manual-test CUE so duplicate render preparation is
  tested directly rather than waiting for the complete architecture.
- Every task records host tests, linked-code inspection where relevant, exact
  commit, independent-review verdict, and remaining target gates in the active
  plan and execution ledger.
- Target builds and Ymir runs remain serial because the owner CPU is busy.
- Every Ymir run uses the project profile with the 32-Mbit DRAM cart.
- A manual improvement does not excuse broken gameplay, camera, visible
  geometry, overflow, ownership, or stale-generation behavior.
- Final publication requires strict closure/native-math, coherency,
  deterministic replay, content identity, command bounds, and fresh ELF/CUE
  evidence. Experimental CUEs do not.

## Rejected alternatives

- **Continue micro-optimizing the current serial frame:** rejected because it
  retains duplicate render preparation and immediate synchronization.
- **SlaveDriver-style fixed half split as the final scheduler:** rejected as
  the default because BOB visibility and actor work are uneven; retained only
  as a simple fallback/reference mode.
- **Rewrite SM64 gameplay or camera behavior:** rejected. The source game is
  authoritative; the Saturn boundary changes representation and scheduling.
- **Treat VDP2 as a second polygon renderer:** rejected. It owns background,
  HUD, and composition, avoiding duplicate geometry.
- **Require exact measurements before architectural work:** rejected for the
  experimental lane. The current 2–3 FPS-class behavior makes obvious manual
  improvement useful, while final claims still require evidence.
