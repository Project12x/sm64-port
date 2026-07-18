# SM64 Saturn development roadmap

Last updated 2026-07-18.

This is the visual-first execution roadmap for porting Super Mario 64 to the
Sega Saturn with a required 4 MiB DRAM cartridge. It complements the deeper
technical plan in [`PLAN.md`](PLAN.md): that document defines the architecture
and hard gates; this one defines the order in which the port should become
visibly more like SM64.

The north star is a 30 Hz, source-derived, controllable Bob-omb Battlefield
slice running through the Saturn backend with visible timing and memory
telemetry. Emulator results drive development. Retail Saturn measurements are
authoritative before performance claims or release, but they do not block the
next rendering milestones.

For a presentation-oriented view, open [the visual roadmap](roadmap.html).

## Current position

The project has moved beyond feasibility sketches:

- the pinned SH-2 C toolchain produces BIOS-authenticated Saturn discs;
- Ymir provides deterministic headless execution, memory/register access,
  controller pulses, frame hashes, and PNG capture;
- the hardware-test disc exercises the RAM-cart gate, DMA paths, and seven VDP1
  primitive modes in emulation;
- the asset classifier has an initial six-way triangle/quad representation,
  plus exact maximum-cardinality matching through hash-pinned NetworkX 3.6.1;
- the actual 440-vertex / 877-triangle SM64 Goddard face, eyes, pupils,
  eyebrows, and moustache render through VDP1;
- the face has fixed-point camera control, topology-derived Gouraud depth,
  toggleable shine, persistent command storage, and split timing telemetry; and
- every accepted and rejected visual step is preserved in the
  [screenshot timeline](evidence/index.html).

The current proof is still a specialized renderer. It does not yet execute the
Goddard deformation system, consume the general SM64 display-list path, render
gameplay textures, or run Mario's gameplay state.

## Roadmap at a glance

| Milestone | Visible result | Primary proof | Rough focused-effort band | Status |
|---|---|---|---|---|
| M0 — Face proof | Source-derived Mario face on Saturn | Geometry, features, Gouraud, camera, telemetry | Delivered | Complete |
| M1 — Living face | The intro face animates and responds predictably | Goddard deformation subset and deterministic input captures | 1–3 weeks | **Now** |
| M2 — Mario turntable | In-game Mario model renders and animates | General display-list IR, textures, skeleton, actor materials | 3–8 weeks | Next |
| M3 — Course flythrough | A textured Bob-omb Battlefield view renders | Level banks, visibility, clipping, ordering, texture residency | 1–3 months | Planned |
| M4 — Controllable Mario | Mario runs and jumps in the course | Game update, input, camera, collision, animation integration | 1–3 months | Planned |
| M5 — Battlefield slice | A small star route is playable | Actors, objects, HUD, particles, minimal audio, stable budgets | 2–5 months | Planned |
| M6 — Castle loop | Castle → course → star → castle works repeatedly | Transitions, save state, asset-bank lifecycle, audio | 2–4 months | Planned |
| M7 — Content and optimization | Increasing level/effect coverage | Stress-class rollout and measured optimization | 12–24+ months | Planned |
| M8 — Hardware and release | Reproducible public source release | Retail validation, compatibility, packaging, documentation | Ongoing validation plus 1–3 release months | Deferred validation lane |

These are engineering effort bands, not calendar promises. They assume one
lead developer with agent assistance, usable decompilation source, no prolonged
licensing block, and strict scope control. Re-estimate after M2 and M5.

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

## M1 — Living intro face

Goal: turn the static mesh study into the first visibly animated SM64 subsystem.

Work:

1. Trace the minimum Goddard face update path: nets, joints, skin weights,
   expression state, and any dynamic vertex ownership.
2. Extend the extractor so source relationships are emitted as compact Saturn
   data rather than manually approximating expressions.
3. Add a fixed-step animation state separate from camera/view state.
4. Deform vertices before projection while preserving the neutral source pose.
5. Keep normals and shine correct under deformation; initially recompute only
   affected vertices, then measure whether a full rebuild is already cheap.
6. Add duration-aware controller holds to Ymir automation.
7. Capture deterministic neutral, yawed, zoomed, shine-off, auto-orbit, and
   animated-expression frames.

Gate:

- at least one source-derived facial motion is clearly visible;
- camera and animation can run together for 10,000 emulator frames;
- shine on/off produces deterministic frame hashes;
- the HUD separates deformation, sort, command-build, Gouraud, and wait costs;
- the screenshot gallery preserves both the first failure and accepted result.

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

## M3 — Bob-omb Battlefield flythrough

Goal: prove the world path before adding gameplay complexity.

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

Gate:

- a textured course view survives near-plane, horizon, and dense-camera tests;
- fixed camera checkpoints have stable frame hashes;
- no asset or command arena silently overflows;
- representative views fit provisional VDP1 and 4 MiB cartridge envelopes;
- visual differences from the PC reference are classified and documented.

## M4 — Controllable Mario

Goal: connect real SM64 update code to the Saturn renderer.

Work:

- integrate digital and 3D Control Pad input behind the platform API;
- run Mario state, course collision, camera, and animation at a fixed 30 Hz;
- render one frame per update unless measurements justify a documented fallback;
- add floor, wall, slope, ledge, jump, and camera-wall regression routes;
- expose position, action, floor, camera, and frame-budget telemetry; and
- add deterministic state traces that can be compared with the PC port.

Gate:

- Mario can idle, run, turn, jump, land, and recover from camera collisions;
- a recorded input route replays to the same final state and frame hashes;
- there are no systematic geometry holes, UV failures, or stale actor poses;
- single-SH2 cost is measured before any gameplay work moves to the slave SH-2.

## M5 — Playable Battlefield slice

Goal: complete one small but honest gameplay route.

Scope:

- Mario, terrain, coins, one enemy class, one interactive object, shadows,
  particles, HUD, pause, star collection, and a minimal licensed SCSP audio
  path;
- a bounded route from course entry through star collection;
- course-local asset loading from disc through WRAM into cartridge banks; and
- failure-visible allocators and command/texture budget guards.

Gate:

- the route completes repeatedly without leaks or stale cartridge pointers;
- update cadence remains 30 Hz under the chosen render policy;
- audio does not force a renderer or memory-layout rewrite;
- the complete route has a screenshot sequence, video capture, state trace,
  budget report, and known-differences list.

This is the main go/no-go milestone for a full port.

## M6 — Castle loop

Goal: prove that the port is a game runtime rather than a single loaded scene.

Deliver:

- title/intro handoff, castle entry, course selection, course load, star return,
  save/load, controller selection, and repeated asset-bank transitions;
- a stable global/current-level/current-actor cartridge allocation policy; and
- the first end-to-end audio lifecycle across scene changes.

Gate: castle → Battlefield → star → castle repeats at least 25 times in an
automated soak without leaks, stale resources, active-play CD stalls, or
transition-dependent visual corruption.

## M7 — Content breadth and measured optimization

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

## M8 — Retail validation, compatibility, and release

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

1. add duration-aware Ymir input holds and capture yaw/zoom/shine-off controls;
2. add a fifth HUD counter for deformation/update cost;
3. document the exact Goddard face update call graph and data ownership;
4. extend the face extractor with the minimum joint/skin relationship data;
5. animate one source-derived expression at a fixed update rate;
6. correct normals/shine under deformation;
7. run a 10,000-frame deterministic soak;
8. capture failure, intermediate, and accepted animation frames;
9. close M1 with a machine-readable budget report; and
10. begin the general Saturn IR with the in-game Mario actor as its first client.

## Roadmap rules

- A milestone is complete only when its visible result, automated gate, budget
  evidence, documentation, and screenshot portfolio all exist.
- A pretty screenshot alone is progress, not completion.
- Infrastructure work must name the next visible result it unlocks.
- Special-case code is acceptable for a bounded proof, but it must be retired
  or explicitly isolated before the following general milestone closes.
- When a budget fails, reduce fidelity or scope before hiding the failure.
- Commit and push coherent increments; preserve instructive failures.
