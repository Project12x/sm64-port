# SM64 Saturn development roadmap

Last updated 2026-07-19.

This is the visual-first execution roadmap for porting Super Mario 64 to the
Sega Saturn with a required 4 MiB DRAM cartridge. It complements the deeper
technical plan in [`PLAN.md`](PLAN.md): that document defines the architecture
and hard gates; this one defines the order in which the port should become
visibly more like SM64.

The next visual north star is a source-derived **Castle Lobby entry slice**:
the title face hands off through `PRESS START` into a textured Peach's Castle
Area 1 where a controllable Mario can idle, run, jump, and turn under a
following camera. Bob-omb Battlefield remains the first broader course and
gameplay-budget gate after that slice. Emulator results drive development.
Retail Saturn measurements are authoritative before performance claims or
release, but they do not block the next rendering milestones.

For a presentation-oriented view, open [the visual roadmap](roadmap.html).
Concrete upstream selections, destination modules, and non-adoptions are in
[the visual-slice upstream code ledger](UPSTREAM_CODE_LEDGER.md). A milestone
may not silently turn a studied source into a copied dependency: its reuse mode
and pinned revision must change there and in [PROVENANCE.md](PROVENANCE.md).
The PS1-port comparison is recorded separately in
[`PSX_PORT_ARCHITECTURE_LESSONS.md`](PSX_PORT_ARCHITECTURE_LESSONS.md): its
lesson is compact source-loop/runtime architecture, not a PS1 renderer to port.

## Current position

The project has moved beyond feasibility sketches:

- the pinned SH-2 C toolchain produces BIOS-authenticated Saturn discs;
- Ymir provides deterministic headless execution, memory/register access,
  controller pulses, frame hashes, and PNG capture;
- the hardware-test disc exercises the RAM-cart gate, DMA paths, and seven VDP1
  primitive modes in emulation;
- the asset classifier has an initial six-way triangle/quad representation,
  plus exact maximum-cardinality matching through hash-pinned NetworkX 3.6.1;
- Saturn mesh IR v1 now preserves source triangle IDs, independent vertex
  streams, all 506 Goddard face-weight records, and deformation-pose safety
  evidence without imposing a false four-weight limit;
- the actual 440-vertex / 877-triangle SM64 Goddard face, eyes, pupils,
  eyebrows, and moustache render through VDP1;
- the face has fixed-point camera control, topology-derived Gouraud depth,
  toggleable shine, persistent command storage, and split timing telemetry;
- every accepted and rejected visual step is preserved in the
  [screenshot timeline](evidence/index.html);
- source Mario now stands, responds to input, jumps, collides, and selects idle
  and walking animation banks inside the source-derived Castle Area 1 slice;
- the measured Ymir establishing-view baseline is approximately 7.5 FPS after
  source culling, transform caching, and a generated one-tile Mario texture
  tier, with 15 FPS the hard M4 target; and
- engine-port gate E0 has extracted shared frame profiling, fixed-point camera
  transforms, bounded memory arenas, and a source-identified render queue from
  the Castle harness.

The current proof renders real gameplay geometry and invokes selected original
input, collision, geo-layout, camera-support, and animation code, but it is not
yet the production game loop. `castleviewer/main.c` still owns manual movement,
jump, camera, animation cadence, and display submission. E0–E2 now retire that
scaffolding into a shared renderer and the original `game_loop_one_iteration()`
plus `exec_display_list()` path.

The PS1-port comparison sharpens this transition: asset conversion, compact
render packets, input/camera, and profiler state belong beneath the source game
loop. The next gains must therefore be scene-neutral command templates and
area-bank residency, not more Castle-specific renderer or movement code.

## Roadmap at a glance

| Milestone | Visible result | Primary proof | Rough focused-effort band | Status |
|---|---|---|---|---|
| M0 — Face proof | Source-derived Mario face on Saturn | Geometry, features, Gouraud, camera, telemetry | Delivered | Complete |
| M1 — Living title face | Animated face, title background, `PRESS START` | Goddard deformation subset, title presentation, deterministic input captures | 1–3 weeks | Active: closure evidence remains |
| M2 — Mario turntable | In-game Mario model renders and animates | General display-list IR, textures, skeleton, actor materials | 3–8 weeks | **Active: render-correctness exit** |
| M3 — Castle lobby renderer | Textured Castle Area 1 renders from fixed cameras | Static world banks, visibility, clipping, ordering, texture residency | 1–3 months | **Active: source-root / texture diagnostics** |
| M4 — Castle-lobby Mario | Mario runs and jumps in the lobby | Game update, lobby collision, camera, animation integration | 1–3 months | **Active: E0–E2 engine cutover** |
| M5 — Castle entry visual slice | Title → lobby is a repeatable playable proof | HUD, basic door prompt, deterministic route, stable budgets | 1–2 months | Planned |
| M6 — Battlefield slice | A small star route is playable | Outdoor visibility, actors, objects, particles, minimal audio | 2–5 months | Planned |
| M7 — Castle loop | Castle → course → star → castle works repeatedly | Transitions, save state, asset-bank lifecycle, audio | 2–4 months | Planned |
| M8 — Content and optimization | Increasing level/effect coverage | Stress-class rollout and measured optimization | 12–24+ months | Planned |
| M9 — Hardware and release | Reproducible public source release | Retail validation, compatibility, packaging, documentation | Ongoing validation plus 1–3 release months | Deferred validation lane |

These are engineering effort bands, not calendar promises. They assume one
lead developer with agent assistance, usable decompilation source, no prolonged
licensing block, and strict scope control. Re-estimate after M2 and M5.

### Current execution choice: retire the harness into the engine path

The visible room-and-actor proof has served its purpose. Further gameplay or
performance work must now land in scene-neutral stages that survive the
transition to the original game loop. E0 is closed: frame, transform, memory,
render-queue, and bounded VDP1 command-arena contracts are shared, and the
native Stage 116 capture exactly matches Stage 115. E1 now makes the Castle and turntable targets
clients of the same backend. Stage 117 shares their VDP1 list lifetime and
bounded texture-transfer destination; common source-identified job submission
remains before E1 closes. E2 then introduces Saturn `exec_display_list()`.

Mario is now standing from the source C5 idle animation. M2 remains open only
for bounded renderer correctness work: source texture patches must use an
explicit opaque/decal ordering path, and named turntable captures must expose
remaining UV or ordering defects. Do not spend this phase attempting an
artist-level reconstruction of every Mario texture.

The next major delivery is M3's **static Castle Area 1 fixed-camera slice**.
It will use the same source display-list/texture compiler as Mario and makes
texture conversion failures measurable on walls, doors, decals, and room
boundaries. Only after its accepted camera set should M4 add movement, source
collision, and a following camera. This preserves the path toward actual
SM64 code/data rather than a replacement game framework.

M3 intake has begun from the real source bank. The generated
[`Castle Area 1 inventory`](evidence/reports/castle-area1-inventory-2026-07-18.json)
currently identifies 26 model units, a 2,317-triangle static upper bound, 70
Fast3D texture-image commands, five root display-list entries across opaque,
alpha, and transparent-decal layers, and 1,563 collision vertices. These are
compiler inputs and pressure estimates—not a claim that the room already
renders on Saturn.

The first root flatten is also checked in as a source-only
[`static scene intake`](evidence/reports/castle-area1-static-scene-intake-2026-07-18.json):
the five Area 1 root lists resolve to 619 source triangles (577 opaque, 34
alpha, and 8 transparent-decal). It preserves per-triangle world positions,
UVs, texture state, nested display-list IDs, top-level root identity, and
render layer; texture bytes remain outside the repository.

The source-root compiler is `tools/saturn/compile_castle_area.py`, invoked
as `make -f Makefile.saturn.mk compile-castle-area1`. It emits a deterministic
first-use indexed bank from the actual Area 1 root: 489 positions and all 619
source triangles, including per-triangle top-level root, layer, texture key,
Fast3D UV triplet, and complete tile state. That prevents the target from
guessing a wall material, inventing coordinates, or hand-selecting room pieces.

M3 has now reached a BIOS-backed fixed-camera render of the 577-triangle
opaque root with all six original source materials. The shared converter fixes
the N64/Saturn red-blue lane difference and uses ordinary VDP1 A/B/C/D
character corners, with repeated destination C receiving source C and D. The
initial 16×16 bake occupied 295,424 VDP1
texture bytes. A target capture now accepts the 8×8, 2× box-filtered Castle
profile: it occupies 73,856 bytes with the same roughly 580 commands, freeing
221,568 bytes for Mario and future room banks while retaining the recognizable
lobby composition. Mario remains independently configurable at 16×16 until a
face-visible comparison justifies reducing it. The local-only bakers expose
per-build material, tile-size, source-scale, and subdivision controls; this is
the active source-display-list renderer, not a hand-painted lobby substitute.
The next gate is the source Mario/game-state path inside this textured room.

The ownership handoff is now running on SH-2: the Castle target links the
original `geo_layout.c`, `graph_node.c`, `graph_node_manager.c`, and
`math_util.c`, and executes an exact generated copy of `castle_geo_000F30`.
The BIOS-backed Stage 70 capture includes a paused target-memory proof of the
resulting five display-list nodes (two opaque, two alpha, one transparent
decal). Each live display-list identity now selects its matching generated
Saturn IR root through a verified `0x1F` root mask. Because VDP1 has no
Z-buffer, opaque and binary-alpha source geometry share one exact host-BSP
painter while the eight transparent-decal triangles use a late
half-transparency pass. The 352-node BSP records 144 source-plane splits and
inserts dynamic Mario by camera-side traversal; its 884-tile capture proves
that painter order is not the primary diagonal texture fault.

The accepted storage path now quantizes each of the nine original materials to
one transparent-plus-15-color VDP1 CLUT through pinned Yaul APIs. End-code
processing is explicitly disabled and index zero retains source binary alpha.
Correcting the probe's previously mislabeled RGB1555 lanes removes the apparent
corner permutation: native character corners are A/B/C/D. The unsplit exact
BSP profile now needs only 884 tiles, 28,288 indexed bytes, and roughly 887
static commands; the diagonal fans disappear without adaptive tessellation.
The source-camera extractor also distinguishes the lobby fixed base from its
local entrance trigger, and target lowering rejects off-screen primitives and
saturates projected coordinates before VDP1 command emission. The resulting
source-camera frame places the floor and central carpet/emblem in front of the
doors with Mario. Near-plane clipping, painter coverage, original collision,
actions, and graph-camera state remain open; no room coordinates are authored
on the target.

## M0 — Source-face proof

Status: complete as a development milestone.

Delivered:

- source-derived face, eye, eyebrow, and moustache meshes;
- source material colors and source-scale facial features;
- painter ordering that correctly places the nose over the moustache;
- topology-derived VDP1 Gouraud shading and fixed-point shine;
- yaw, pitch, zoom, reset, shine, and auto-orbit controls;
- persistent VDP1 command storage and update-only Gouraud uploads;
- a conservative Saturn render IR with 156 provably maximum safe true quads,
  565 explicit face-triangle fallbacks, and unchanged source topology;
- split sort/build/upload/wait counters; and
- BIOS-backed screenshots and capture manifests.

This milestone proves that nontrivial SM64 geometry can be compiled as C,
transformed on SH-2, and rendered by VDP1. It does not prove a general graphics
backend or a gameplay frame budget.

## M1 — Living title face

Goal: turn the static mesh study into an honest, interactive SM64 title-screen
proof that visibly hands off into the future game runtime.

Work:

1. Trace the minimum Goddard face update path: nets, joints, skin weights,
   expression state, and any dynamic vertex ownership.
2. Extend the v1 mesh IR adapter so source relationships and evaluated pose
   extrema are emitted as compact Saturn data rather than manually
   approximating expressions. The schema and deformation-safety gate are done;
   source Goddard relationship extraction remains.
3. Add a fixed-step animation state separate from camera/view state.
4. Deform vertices before projection while preserving the neutral source pose.
5. Keep normals and shine correct under deformation; initially recompute only
   affected vertices, then measure whether a full rebuild is already cheap.
6. Add duration-aware controller holds to Ymir automation.
7. Capture deterministic neutral, yawed, zoomed, shine-off, auto-orbit, and
   animated-expression frames.
8. Convert and display the source title background as a Saturn texture/VDP2
   plane, retaining a local-only ROM-derived asset workflow.
9. Render a source-layout `PRESS START` prompt and transition on Start into a
   visibly distinct, deterministic placeholder handoff screen.
10. Put eyes into the same painter/depth ordering domain as face and feature
   surfaces; they must no longer overpaint eyelids, nose, or brows merely
   because they were submitted last.
11. Implement the title backdrop and Start prompt using the Yaul-specific
    adaptation rules and measurable separate layers in
    [`UPSTREAM_CODE_LEDGER.md`](UPSTREAM_CODE_LEDGER.md).

Gate:

- at least one source-derived facial motion is clearly visible;
- camera and animation can run together for 10,000 emulator frames;
- shine on/off produces deterministic frame hashes;
- the HUD separates deformation, sort, command-build, Gouraud, and wait costs;
- the screenshot gallery preserves both the first failure and accepted result.
- the accepted title frame visibly contains the animated source face, title
  background, `PRESS START`, and correct eye occlusion.

Explicit limit: do not spend this milestone recreating every mouse-pull and
presentation detail from the original intro. Its purpose is to prove dynamic
source geometry on Saturn and expose the cost.

## M2 — In-game Mario turntable

Goal: replace the specialized face path with the first reusable actor path.

Work:

- define the compact Saturn render IR for vertices, materials, textures,
  triangles, mergeable quads, transforms, and draw state;
- convert the in-game Mario model through that IR;
- implement repeated-vertex triangles and conservative triangle-pair merging;
- add CI4/CI8/RGB1555 conversion, padding, CLUT allocation, and deduplication;
- implement hierarchical actor transforms and one gameplay animation;
- add shade-preserving near-plane clipping;
- replace whole-scene insertion sorting with coarse opaque buckets and stable
  local ordering; and
- render the same IR in a small PC reference viewer for comparison.

Gate:

- source-derived, textured Mario rotates through a full turn without missing
  limbs, UV corruption, or systematic ordering faults;
- idle and one moving animation play deterministically;
- every source primitive has a recorded representation decision and rejection
  reason;
- command, texture, CLUT, Gouraud, internal-WRAM, and cartridge budgets appear
  on-screen and in a machine-readable report.

This is the decisive renderer-architecture milestone. If Mario only fits by
special-casing his source data, the IR is not ready for a course.

## M3 — Castle lobby renderer

Goal: render the first room of Peach's Castle from source assets before
introducing broad world/gameplay complexity.

Scope boundary: Castle Interior Area 1/lobby entrance only. Doors, paintings,
lights, and room geometry may begin as static scenery. Do not add warps, save
state, moving objects, or the full level-script interpreter here.

Source ownership:

| Concern | Primary source inputs | Saturn deliverable |
|---|---|---|
| Static geometry/materials | `levels/castle_inside/areas/1`, `leveldata.c` | converted mesh/material/texture banks |
| Scene graph | `levels/castle_inside/geo.c` | bounded offline traversal/export |
| Collision | `levels/castle_inside/areas/1/collision.inc.c` | compact floor/wall query data |
| Special interior light | `src/game/geo_misc.c` | initially baked/static lighting rule |

Work:

- extend the Saturn IR compiler to consume a bounded Fast3D display-list
  subset: vertex loads, material state, textures, triangles, and nested lists;
- convert Area 1 geometry, textures, collision, and source IDs into a
  cartridge-resident bank with a PC reference render;
- implement fixed-point transforms, backface culling, near-plane clipping,
  coarse opaque depth buckets, and explicit later passes for decals/effects;
- use fixed cameras first, then replayable camera rails, to expose ordering,
  texture residency, and clipping failures before player control; and
- capture the same camera checkpoints in the PC reference viewer and Ymir.

Gate:

- recognizable textured lobby frames render without missing surfaces, UV
  corruption, or systematic doorframe/decal/wall ordering defects;
- all camera checkpoints fit provisional command, VDP1 texture, CLUT,
  internal-WRAM, and 4 MiB cartridge budgets; and
- the gallery includes source reference, first failure, correction, and
  accepted camera set.

## M4 — Castle-lobby Mario

Goal: demonstrate that the rendered room is running game code, not merely a
flythrough.

Work:

- connect the M2 Mario actor path to Castle Area 1 collision;
- integrate digital and 3D Control Pad input behind the platform API;
- run a fixed 30 Hz update with idle, run, turn, jump, gravity, floor/wall
  response, and a following camera;
- show position, action, floor, camera, collision, and frame-budget telemetry;
- record deterministic short movement and jump routes against a PC reference.

Architecture gate: M4 gameplay must converge on the original source loop, not
accumulate a second implementation in the Castle harness. Follow E0–E2 in
[`ENGINE_PORT_ARCHITECTURE.md`](ENGINE_PORT_ARCHITECTURE.md): extract the
shared renderer/runtime records, accept source-identified jobs, then drive the
slice through `game_loop_one_iteration()` and Saturn `exec_display_list()`.
The current manual movement, jump, camera, and animation bridge is scaffolding
and cannot close M4.

The implementation reference is summarized in
[`PSX_PORT_ARCHITECTURE_LESSONS.md`](PSX_PORT_ARCHITECTURE_LESSONS.md): compile
area command templates and residency banks offline, then patch only
source-selected dynamic state per frame. The source loop, not a Castle viewer,
selects the area, camera, animation, behavior, and render work.

Gate: Mario can idle, run, turn, jump, land, and collide with the lobby using
source-derived model/collision data, with replayable final state and capture.

Performance acceptance for this slice is explicit: 15 FPS full game-loop is
the hard floor, 20 FPS is the stretch target, and 30 FPS is expected only for
simple/low-load views until retail hardware says otherwise. The current
BIOS-backed Ymir baseline is approximately 7.5 FPS in the establishing view
(the earlier `/8` 16-bit FRT counter wrapped and was invalid). Emulator timing
is comparative evidence, not a retail claim. Reaching the gate requires:

- a typical establishing view at no more than roughly 650 live VDP1 commands;
- a 20 FPS stretch profile near 450 commands, both provisional until measured;
- transform-once actor/world job records instead of repeated projection in
  visibility, scene ordering, and command construction;
- a source-derived Mario Saturn LOD bank, with full and reduced banks staged
  through the 4 MiB cartridge boundary; and
- phase telemetry for update, sort, command build/upload, VDP wait, and VBlank.

Visual quality is part of the same gate. The current small affine-tile painter
is a bring-up path, not the target look: material clustering, correct source
culling, native quad retention, near clipping, and bounded LOD must reduce
both texture discontinuities and command pressure together.

## M5 — Castle entry visual slice

Goal: produce the concise visual proof a viewer immediately recognizes as
“Mario 64 running from source on Saturn.”

Required sequence:

1. animated title face over the source title background;
2. `PRESS START` responds to controller input;
3. deterministic transition/fade into Castle Area 1;
4. controllable, animated Mario in the textured lobby; and
5. HUD, timing/memory overlay, and a bounded door prompt.

Gate: the complete route is captured as screenshots and a short deterministic
video/replay, has a machine-readable budget report, and survives a 10,000-frame
emulator soak without command, texture, or allocator overflow.

M5 also requires E3: Castle Area 1 is loaded as a versioned, offset-based area
package through the disc/WRAM/4 MiB cartridge lifecycle. Linked Castle arrays,
a fixed scene collision arena, or production dependence on
`castleviewer/main.c` leave M5 open.

Audio is explicitly out of scope for this visual proof; silence is an accepted
M5 presentation state. The first minimal audio requirement belongs to M6.

## M6 — Bob-omb Battlefield slice

Goal: expand the proven actor/runtime path into an outdoor course and first
star route.

M6 begins with architecture gate E4: the additional area and one dynamic
object actor must use the same source-loop, package, render-job, residency, and
VDP back ends without scene-named branches in shared runtime code.

Work:

- convert one representative section of Bob-omb Battlefield into cartridge
  geometry, collision metadata, texture banks, and visibility groups;
- stage hot transform/command data into internal WRAM;
- implement frustum and coarse spatial rejection before fine clipping;
- add opaque ordering buckets, decal/shadow ordering rules, and explicit
  transparency fallbacks;
- measure texture residency and command peaks from several fixed cameras; and
- capture an automated flythrough with identical camera checkpoints in the PC
  reference renderer and Ymir.

M6a renderer sub-gate:

- a textured course view survives near-plane, horizon, and dense-camera tests;
- fixed camera checkpoints have stable frame hashes;
- no asset or command arena silently overflows;
- representative views fit provisional VDP1 and 4 MiB cartridge envelopes;
- visual differences from the PC reference are classified and documented.

Scope:

- Mario, terrain, coins, one enemy class, one interactive object, shadows,
  particles, HUD, pause, star collection, and a minimal licensed SCSP audio
  path;
- a bounded route from course entry through star collection;
- course-local asset loading from disc through WRAM into cartridge banks; and
- failure-visible allocators and command/texture budget guards.

M6 completion gate:

- the route completes repeatedly without leaks or stale cartridge pointers;
- update cadence remains 30 Hz under the chosen render policy;
- audio does not force a renderer or memory-layout rewrite;
- the complete route has a screenshot sequence, video capture, state trace,
  budget report, and known-differences list.

This is the main course-scale go/no-go milestone for a full port.

## M7 — Castle loop

Goal: prove that the port is a game runtime rather than a single loaded scene.

Deliver:

- title/intro handoff, castle entry, course selection, course load, star return,
  save/load, controller selection, and repeated asset-bank transitions;
- a stable global/current-level/current-actor cartridge allocation policy; and
- the first end-to-end audio lifecycle across scene changes.

Gate: castle → Battlefield → star → castle repeats at least 25 times in an
automated soak without leaks, stale resources, active-play CD stalls, or
transition-dependent visual corruption.

## M8 — Content breadth and measured optimization

Port content by stress class rather than original level order:

1. simple outdoor courses;
2. dense interiors and camera-heavy spaces;
3. water, translucency, shadows, and decals;
4. moving and deforming geometry;
5. Bowser, particles, and actor-heavy scenes;
6. menus, ending, and credits.

Only optimize from profiles. Candidate optimizations include quad merging,
material batching, lower-cost Gouraud tiers, display-list caching, spatial
visibility, assembly kernels, and coarse slave-SH2 transform jobs. Every change
must keep a before/after capture and timing report.

## M9 — Retail validation, compatibility, and release

Retail testing is deliberately deferred in calendar order, but not removed as
an authority:

- validate cartridge ID, full 4 MiB memory, CPU/DMAC/SCU transfers, VDP1 draw
  behavior, and frame timings on physical hardware;
- compare official and representative third-party 4 MiB cartridges;
- repeat cold boots and long soaks on available regional consoles;
- maintain Ymir plus a second-emulator compatibility lane;
- reconcile emulator-only assumptions before locking performance budgets;
- publish build instructions, provenance, licenses, known issues, and capture
  evidence; and
- ship no ROM, extracted Nintendo assets, audio, or prebuilt game image.

Retail measurements can revise earlier budgets. They should not require a new
renderer architecture if the earlier milestones keep transfer paths, memory
arenas, and frame policies explicit.

## Continuous workstreams

These run through every milestone:

### Visual evidence

- capture the first output, important failures, accepted correction, and final
  gate for every milestone;
- embed images in `docs/saturn/evidence/index.html` rather than leaving them as
  loose files;
- attach emulator version, disc hash, frame hash, and capture report;
- label source-derived geometry versus stand-ins; and
- never describe emulator timing as retail-hardware proof.

### Automation and debugging

- keep one-command build and capture workflows;
- extend Ymir with duration-aware inputs, watchpoints, breakpoints, traces, and
  structured performance samples only as each becomes necessary;
- prefer deterministic replay routes over manual-only demonstrations; and
- make allocator, command, texture, and DMA failures visible and machine-readable.

### Provenance and licensing

- inspect license-compatible upstream implementations before nontrivial new work;
- record pinned commits, licenses, files, and reuse mode;
- preserve required copyright, license, NOTICE, attribution, and change records
  for copied or closely adapted source;
- permit direct-copy, close-port, fork, or dependency reuse from GPL sources in
  this GPL-compatible project, while retaining corresponding source and all
  applicable copyleft obligations; and
- keep user-owned ROM-derived assets out of Git.

### Performance and memory

- track CPU update, deformation/transform, clipping, sort, command build,
  Gouraud/texture upload, VDP1 wait, and total frame cost separately;
- track peak internal WRAM, cartridge DRAM, VDP1 commands, VDP1 textures, CLUTs,
  and Gouraud tables;
- keep single-SH2 correctness as the baseline; and
- add slave-SH2 work only when a measured job is large, coarse, and bus-aware.

Wave 1 of the 2026-08-01 PS1-parity FPS sprint is **blocked, not accepted or
rejected**. The initial `3fef49c` standalone-template compile failure was
fixed at `5244b7b`, but the guarded hot1/clip1 retry fails at link: the
`.lwram_camera_capture` section overflows `lwram` by 15840 bytes. No fresh
ELF/CUE or Ymir counter/capture evidence exists. Commit `23c3cdd` remains the
next comparison baseline; the target link budget and unchanged truthful
native-math gate must both permit a fresh image before the serial BOB
comparison resumes. See
`docs/saturn/evidence/reports/ps1-parity-wave1-bob-2026-08-01.md`.

## Immediate execution queue

The next sequence now retires the successful visual-slice scaffolding while
preserving its measured gains:

1. E0 (**complete, Stage 116**): extract common timing, transform-cache,
   render-job, command-arena, and memory-arena records from
   `castleviewer/main.c` with identical captures;
2. E1: make Castle and Mario independent clients of one scene-neutral render
   queue and VDP1/VDP2 backend;
3. keep the current one-command Mario texture tier as generated source-derived
   LOD IR, not a renderer special case, while pursuing the 15–20 FPS gate;
4. E2: add the Saturn `exec_display_list()` front end and boot the original
   `game_loop_one_iteration()` path;
5. retire manual movement, jump, camera, and animation ownership as the source
   action/object/camera/geo code comes online;
6. E3: package Castle Area 1, collision, textures, and actor dependencies into
   the versioned disc/WRAM/4 MiB cartridge lifecycle; and
7. E4: prove the same path with a second area and a dynamic object before
   broadening the visual slice.

## Roadmap rules

- A milestone is complete only when its visible result, automated gate, budget
  evidence, documentation, and screenshot portfolio all exist.
- A pretty screenshot alone is progress, not completion.
- Infrastructure work must name the next visible result it unlocks.
- Special-case code is acceptable for a bounded proof, but it must be retired
  or explicitly isolated before the following general milestone closes.
- A performance win is accepted into the production path only when it belongs
  to a scene-neutral stage defined in `ENGINE_PORT_ARCHITECTURE.md`; a faster
  Castle-only loop is evidence, not engine completion.
- When a budget fails, reduce fidelity or scope before hiding the failure.
- Commit and push coherent increments; preserve instructive failures.
