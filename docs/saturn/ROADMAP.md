# SM64 Saturn development roadmap

Last updated 2026-07-18.

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
  toggleable shine, persistent command storage, and split timing telemetry; and
- every accepted and rejected visual step is preserved in the
  [screenshot timeline](evidence/index.html).

The current proof still uses a specialized renderer, although its face mesh now
passes through the reusable IR compiler. It does not yet execute the Goddard
deformation system, consume the general SM64 display-list path, render gameplay
textures, or run Mario's gameplay state.

## Roadmap at a glance

| Milestone | Visible result | Primary proof | Rough focused-effort band | Status |
|---|---|---|---|---|
| M0 — Face proof | Source-derived Mario face on Saturn | Geometry, features, Gouraud, camera, telemetry | Delivered | Complete |
| M1 — Living title face | Animated face, title background, `PRESS START` | Goddard deformation subset, title presentation, deterministic input captures | 1–3 weeks | **Now** |
| M2 — Mario turntable | In-game Mario model renders and animates | General display-list IR, textures, skeleton, actor materials | 3–8 weeks | **Active: render-correctness exit** |
| M3 — Castle lobby renderer | Textured Castle Area 1 renders from fixed cameras | Static world banks, visibility, clipping, ordering, texture residency | 1–3 months | **Active: source-root / texture diagnostics** |
| M4 — Castle-lobby Mario | Mario runs and jumps in the lobby | Game update, lobby collision, camera, animation integration | 1–3 months | Planned |
| M5 — Castle entry visual slice | Title → lobby is a repeatable playable proof | HUD, basic door prompt, deterministic route, stable budgets | 1–2 months | Planned |
| M6 — Battlefield slice | A small star route is playable | Outdoor visibility, actors, objects, particles, minimal audio | 2–5 months | Planned |
| M7 — Castle loop | Castle → course → star → castle works repeatedly | Transitions, save state, asset-bank lifecycle, audio | 2–4 months | Planned |
| M8 — Content and optimization | Increasing level/effect coverage | Stress-class rollout and measured optimization | 12–24+ months | Planned |
| M9 — Hardware and release | Reproducible public source release | Retail validation, compatibility, packaging, documentation | Ongoing validation plus 1–3 release months | Deferred validation lane |

These are engineering effort bands, not calendar promises. They assume one
lead developer with agent assistance, usable decompilation source, no prolonged
licensing block, and strict scope control. Re-estimate after M2 and M5.

### Current execution choice: prove the room before polishing the actor

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
UVs, texture state, source display-list IDs, and render layer; texture bytes
remain outside the repository. The next implementation step is to compile its
opaque subset through the shared Saturn mesh IR and produce the first fixed
camera frame.

The initial opaque compiler is `tools/saturn/compile_castle_area.py`, invoked
as `make -f Makefile.saturn.mk compile-castle-area1`. It emits a deterministic
first-use indexed bank from the actual Area 1 root: 436 positions and 577
opaque source triangles, plus the source display-list IDs that produced them.
It is intentionally not yet a room-render claim: texture pixels, alpha/decal
layers, clipping, visibility, and camera framing remain separate M3 gates.
The same IR now keeps the original per-triangle texture identifier and Fast3D
UV triplet. That prevents the first texture conversion from guessing a wall
material or inventing replacement coordinates.

M3 has now reached a BIOS-backed fixed-camera render of the 577-triangle
opaque root with all six original source materials. The shared converter fixes
the N64/Saturn red-blue lane difference and uses the measured complete
C/B/A/C repeated-vertex mapping. The initial 16×16 bake occupied 295,424 VDP1
texture bytes. A target capture now accepts the 8×8, 2× box-filtered Castle
profile: it occupies 73,856 bytes with the same roughly 580 commands, freeing
221,568 bytes for Mario and future room banks while retaining the recognizable
lobby composition. Mario remains independently configurable at 16×16 until a
face-visible comparison justifies reducing it. The local-only bakers expose
per-build material, tile-size, source-scale, and subdivision controls; this is
the active source-display-list renderer, not a hand-painted lobby substitute.
The next gate is the source Mario/game-state path inside this textured room.

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

Gate: Mario can idle, run, turn, jump, land, and collide with the lobby using
source-derived model/collision data, with replayable final state and capture.

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

Audio is explicitly out of scope for this visual proof; silence is an accepted
M5 presentation state. The first minimal audio requirement belongs to M6.

## M6 — Bob-omb Battlefield slice

Goal: expand the proven actor/runtime path into an outdoor course and first
star route.

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

- inspect permissive upstream implementations before nontrivial new work;
- record pinned commits, licenses, files, and reuse mode;
- preserve required notices for copied or closely adapted permissive source;
- use GPL renderer sources as behavioral/architectural references only in new
  port work; and
- keep user-owned ROM-derived assets out of Git.

### Performance and memory

- track CPU update, deformation/transform, clipping, sort, command build,
  Gouraud/texture upload, VDP1 wait, and total frame cost separately;
- track peak internal WRAM, cartridge DRAM, VDP1 commands, VDP1 textures, CLUTs,
  and Gouraud tables;
- keep single-SH2 correctness as the baseline; and
- add slave-SH2 work only when a measured job is large, coarse, and bus-aware.

## Immediate execution queue

The next sequence should stay narrow enough to commit and capture frequently:

1. complete M1 face deformation, eye occlusion, title background, and `PRESS
   START` handoff as independently captured increments;
2. add duration-aware Ymir input holds plus deformation/update HUD telemetry;
3. run the M1 10,000-frame deterministic soak and close it with a budget
   report and accepted gallery entry;
4. begin M2 by compiling the in-game Mario actor through the general Saturn
   IR, then prove its textured turntable and one animation;
5. select the Area 1 Castle geometry/texture/collision subset and generate the
   M3 source-bank inventory, representation report, and PC reference frames;
6. build the fixed-camera lobby renderer before enabling Mario control; and
7. use the first controllable lobby route to establish the M5 title-to-castle
   evidence sequence before broadening to Battlefield.

## Roadmap rules

- A milestone is complete only when its visible result, automated gate, budget
  evidence, documentation, and screenshot portfolio all exist.
- A pretty screenshot alone is progress, not completion.
- Infrastructure work must name the next visible result it unlocks.
- Special-case code is acceptable for a bounded proof, but it must be retired
  or explicitly isolated before the following general milestone closes.
- When a budget fails, reduce fidelity or scope before hiding the failure.
- Commit and push coherent increments; preserve instructive failures.
