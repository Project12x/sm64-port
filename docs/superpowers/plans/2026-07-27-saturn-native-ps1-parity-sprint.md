# Saturn-Native PS1 Capability-Parity Sprint

**Goal:** Close the capability gaps that make the PS1 port materially faster
than sourceboot today—fixed-point geometry, transform-once vertices, compact
render packets, prepared fidelity tiers, resident animation data, explicit
visibility/order policy, and useful coprocessors—while retaining the original
SM64 game as the authority.

**Architecture:** The original game loop produces authoritative game, object,
camera, animation, collision, geo-layout, and display-list state. A guarded
Q16 render seam and a generated Saturn packet/asset layer convert that state
into bounded jobs. The master SH-2 owns game state and final command linkage;
the slave SH-2 transforms, lights, clips, and culls independent job batches;
SCU DMA moves bounded staging blocks and command banks; VDP1/VDP2 draw; the
MC68EC000 services SCSP PCM commands. The cartridge is cold storage, not a
per-primitive working set.

**Tech stack:** C and SH-2 assembly, `m68keb-elf`, pinned libyaul 0.3.1,
host-native contract/differential tests, Python asset tools, fixed-VMA
cartridge banks, Ymir headless evidence, and eventual retail validation.

**Sprint shape:** This is one capability sprint with twelve ordered gates. It
is bounded by evidence, not by a claim that the work fits in one week. A gate
does not land merely because it builds.

---

## 1. Decision this sprint implements

The literal “never modify the engine tree” rule is retired. The replacement is
the source-authority rule in
`docs/saturn/ENGINE_PORT_ARCHITECTURE.md`:

- SM64 gameplay and asset semantics remain authoritative.
- Prefer `src/port/saturn/`, `tools/saturn/`, generated IR, and existing
  platform boundaries.
- Permit a minimal, scene-neutral `TARGET_SATURN` engine seam when measurement
  proves that a boundary is insufficient or materially costly.
- Require a differential or behavior test and a non-Saturn fallback for every
  such seam.
- Generate fidelity tiers reproducibly from source assets; do not hand-edit
  replacements into `actors/`, `levels/`, or `textures/`.

This permits a continuous Q16 render representation through
`rendering_graph_node.c` when necessary. It does not permit a Saturn-specific
replacement game loop, camera, object system, or level script.

### Engine-exception checklist

Before editing `src/engine/`, `src/game/`, `levels/`, `actors/`, `lib/src/`, or
an inherited header under `include/`, record all five items in the implementing
commit. New target-owned contracts under `include/saturn/` are port-layer
files.

1. the measured boundary cost or missing target seam;
2. the smallest `TARGET_SATURN`-guarded change;
3. the source semantics that remain unchanged;
4. the differential/behavior fixture that proves it; and
5. the unguarded path that remains byte-for-byte or behaviorally unchanged.

No task below requires scene-specific edits to `levels/` or `actors/`.

---

## 2. Measured starting point

Freeze these as the pre-sprint comparison, after landing the currently open
MVP-cache work:

| Measure | Starting value |
| --- | ---: |
| Sourceboot render rate | 0.6718 FPS measured from paired long captures |
| 36,000-post-poke sample | `frame_serial=392` |
| Source triangles | 2,311 |
| Transformed triangles | 2,311 |
| VDP1-emitted primitives | 829 |
| Composed-MVP cache | 12 entries, lazy |
| Profile bytes | 248 |
| Renderer faults | 0 |
| Command-capacity rejects | 0 |
| VDP1-arena rejects | 0 |
| Known HWRAM free space | approximately 126,940 bytes |
| Linker hard floor | at least 4,096 bytes free |
| Slave SH-2 renderer share | 0% |
| MC68EC000 game-audio share | 0%; audio API is a no-op |

Evidence:

- `docs/saturn/evidence/reports/e2-sourceboot-mvp-cache-ppf12000-2026-07-27.json`
- `docs/saturn/evidence/reports/e2-sourceboot-mvp-cache-ppf36000-2026-07-27.json`
- `docs/saturn/HANDOFF_2026-07-26.md`
- `docs/saturn/SHIPPING_ENGINE_COMPARISON.md`

The 0.6718 FPS figure is an emulator measurement, not retail-hardware proof.
The 2.14 FPS ceiling in
`docs/superpowers/specs/2026-07-26-soft-float-replacement-design.md` is the
Amdahl bound for eliminating the measured soft-float share while leaving the
non-float remainder unchanged. Tasks 2–9 deliberately restructure that
remainder—transform frequency, interpretation, residency, visibility, order,
DMA, and CPU ownership—so it does not cap this broader sprint. The 15 FPS
number remains an exit gate, not a forecast.

---

## 3. What “PS1 parity” means here

It means capability parity with the useful completed ideas, not source or
pixel parity with the PS1 project.

| PS1 capability/lesson | Saturn-native completion |
| --- | --- |
| Low-precision soft float | Remove float from the hot renderer; retain exact soft-float only outside measured hot paths |
| Broad fixed-point math | Continuous Q16 render state plus SH-2 `MAC`, `xtrct`, and latency-hidden `DIVU` |
| 16-bit vectors/matrices | Bounded Q16/Q-format job records selected by range analysis, not blind narrowing |
| Simplified graph walker | Keep source geo traversal authoritative, compile static Fast3D subgraphs into compact Saturn packets |
| RSP display-list JIT | Build-time packet compiler plus runtime dynamic-state patches; no writable source-DL self-modification |
| Display-list preprocessing | Remove redundant static state, pre-index metadata, emit bounds/LOD/material records |
| Tessellation up to 2× | Offline, source-derived subdivision only where projected-span/UV tests require it |
| Mario animation residency | Versioned resident animation bank; optional real compression with exact decoded-output tests |
| Custom profiler | Phase, packet, cache, bus, slave, DMA, VDP1, audio-mailbox, and asset-tier counters |
| 4-bpp textures | Saturn CLUT16 encodes plus deterministic near/mid/far variants and residency manifest |
| Texture preparation | Area/actor texture working sets staged at transition, never individual blocking loads mid-frame |
| Shadow adaptation | Source shadow intent lowered to a bounded Saturn polygon/mesh tier |
| Large-polygon policy | Explicit subdivision, clipping, and overflow policy rather than accidental distortion |

The PS1 port’s own README also lists individual texture-load stutters,
tessellation errors, incorrect textures, animation crashes, an 8 MiB
non-retail benchmark, and unfinished systems. Those are warnings, not features
to reproduce.

### Important animation correction

At pin `3073845688ea273da78d539b20c45110d8a868c3`,
`tools/compress_mario_anims.c` says that no compression is currently performed.
The useful demonstrated behavior is a reduced/repacked resident bank. This
sprint must not cite the README’s size ratio as proof of a compression
algorithm. Any Saturn compression must earn its own exact decode and timing
evidence.

---

## 4. Reference-code and license ledger

This table is part of the implementation record required by the repository’s
reference-code-first rule.

| Reference | Pin and license | Files inspected | Reuse in this sprint |
| --- | --- | --- | --- |
| `malucard/sm64-psx` | `3073845688ea273da78d539b20c45110d8a868c3`; no root license found | `README.md`, `src/port/gfx/gfx_rsp_jit.c`, `src/port/psx/gfx_dl_exec_psx.c`, `gfx_texture_psx.c`, `tools/preprocess_graphics.py`, `pack_textures.py`, `compress_mario_anims.c` | **Behavior-only.** Copy and close-port are prohibited. |
| `Lobotomy-Software/SlaveDriver-Engine` | `a8986591557b6e680550d3c23970284d3b38ff8f`; GPL-3.0-or-later | `WALLS.C`, `WALLASM.S`, `SPR.C`, `SRUINS.C` | **Close-port/direct adaptation** for the DIVU schedule, bounded dual-CPU work/result discipline, adaptive split, and DMA patterns under `src/port/saturn/gpl/`; preserve notices and change notes. |
| `Maxime-XL2/SONIC-Z-TREME` | `cff75451c1616aac1236fc2b44223902b55c706b`; GPL-3.0 | `ZT_RENDERING.c`, `ZT_LOADING.c`, `ZT_LOAD_MODEL.c`, `ZT_CD.c`, `SRC/game.c` | **Pattern-only** for tri-state hierarchical culling, near-to-far traversal, LOD selection, and hot-WRAM promotion. Its octree and model format are an architecture mismatch. |
| `johannes-fetz/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b`; MIT root plus file-local BSD-style notices | `jo_engine/math.c`, `vdp1_command_pipeline.c`, `3d.c` | **Direct adaptation** of the small `jo_fixed_mult`/`xtrct` kernel with its file notice; **pattern-only** for command lifetime because the current libyaul arena is the better allocator. Do not adopt Jo’s immediate-read DIVU stall. |
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98`; MIT | `libmic3d/render.c`, `sort.c`, `sort.h`, dual-CPU/DMA/DIVU and VDP1 APIs | Existing dependency plus selective **close-port** of transform pools, buckets, and bounded staging when it fits. Preserve MIT attribution for copied implementation. |
| `yaul-org/libyaul-examples` | `66b648eb059bb8bb7392eac70821605a68205b85`; the checked-out pin lacks the referenced root `LICENSE` | `cpu-dual`, `cpu-divu`, `scsp-ponesound-pcm8/ponesound.c/.h`, example main | **Behavior-only** unless its license provenance is repaired. Do not copy `sdrv.bin` or wrapper source from this checkout. |
| `ponut64/SCSP_poneSound` | `31782e4c61337327f23eb9aa45ecd37fe0944ea0`; MIT | `LICENSE`, `PROJ/main.c`, `PROJ/linker`, `PROJ/makefile`, `jo_demo/pcmsys.c`, `pcmsys.h` | **Close-port/direct adaptation** of the source-built 68K driver, linker layout, mailbox, and minimum PCM wrapper. Preserve MIT license and credit. Exclude ADX/CDDA/Jo APIs until needed. |

Before the first code-copy commit, update:

- `docs/saturn/PROVENANCE.md`
- `docs/saturn/UPSTREAM_CODE_LEDGER.md`
- `THIRD_PARTY_LICENSES.md`

The implementation summary must name the upstream file and reuse mode for each
adapted file.

---

## 5. Sprint exit gates

The sprint is complete only when all of these are true:

1. The deterministic sourceboot route advances the original simulation at
   30 Hz independent of render cadence.
2. The hot transform/project/cull/viewport path has zero float helper calls.
3. A loaded source vertex is transformed at most once for one matrix/viewport
   generation, except explicit clip-created vertices.
4. Static Fast3D subgraphs use validated compact packets; unsupported or
   dynamic commands escape safely to the original interpreter.
5. Balanced builds use generated CLUT16 textures, geometry/large-polygon
   policy, LODs, resident animations, and a bounded shadow tier.
6. Area/actor package loading is versioned, bounded, epoch-invalidated, and
   causes no individual texture or animation CD read in the frame loop.
7. Hierarchical culling, depth ordering, command-bank overlap, and every
   overflow policy are explicit and counted.
8. The slave SH-2 completes useful render jobs and improves the same route
   over the serial feature-flag build.
9. The MC68EC000 runs a source-built driver and a real `play_sound()` request
   reaches an SCSP PCM voice without master-SH2 mixing.
10. The balanced profile reaches **at least 15 FPS median and 12 FPS 1% low**
    over the fixed 600-simulation-tick BOB route in the pinned Ymir setup.
11. `fault_flags`, stack overflow, command overflow, VDP1 arena overflow,
    Gouraud overflow, stale-bank use, packet validation failure, slave timeout,
    and audio mailbox overflow are all zero.
12. The linked image respects the 4 MiB cartridge decision and the HWRAM linker
    assertion without weakening its 4 KiB floor.
13. A second area and one dynamic actor reload correctly, proving that the
    result is not a BOB-only static cache.
14. The user accepts side-by-side full and balanced captures before either is
    promoted into `TIMELINE.md` or the evidence gallery.

If the balanced route misses 15 FPS, the sprint remains open. Do not lower the
gate or relabel an incomplete frame as a completed one.

---

## 6. Sequence and dependencies

```text
Task 0 baseline
    |
Task 1 Q16 kernels and DIVU
    |
Task 2 transform-once hot path
    |
Task 3 compact packets
   / \
  /   \----------------------\
Task 4 textures          Task 8 visibility/order/commands
  |                           |
Task 5 geometry/LOD/shadow    |
  |                           |
Task 6 animation residency    |
  \                           /
   Task 7 package/hot promotion
              |
          Task 9 slave SH-2

Task 10 MC68EC000/SCSP may begin after Task 0 and joins at Task 11.

Task 11 integrated route and exit evidence
```

Do not run a capture while a build is writing the CUE, and do not run two
captures concurrently.

---

## 7. Shared verification commands

Run host contract tests from Git Bash:

```bash
export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
make -f Makefile.saturn.mk OS=Windows_NT verify-runtime-contracts \
  SATURN_TOOLS_PYTHON="$PWD/.venv-saturn-tools/Scripts/python.exe"
make -f Makefile.saturn.mk OS=Windows_NT verify-tools \
  SATURN_TOOLS_PYTHON="$PWD/.venv-saturn-tools/Scripts/python.exe"
```

Run the cross-build and image verification in one MSYS2 shell:

```bash
C:/msys64/usr/bin/bash.exe -lc \
  "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/src/port/saturn/sourceboot &&
   source ../../../../.yaul.env &&
   make -j2 &&
   make verify"
```

For each capture:

- resolve `_sourceboot_fast3d` fresh with `sh-elf-nm`;
- derive `--probe-count` from the current profile layout, in bytes;
- use `--post-poke-frames 12000` for a quick check and `36000` for the retained
  performance comparison;
- require `triangles_vdp1_emitted > 0`;
- decode `probe_window` with `tools/saturn/fast3d_profile_decode.py`; and
- record the ELF/CUE hashes, feature flags, route version, tier, and both CPU
  modes in the report.

Mutation-test every new test family by deliberately breaking its kernel,
packet validation, capacity guard, or epoch check and observing failure before
restoring the implementation.

---

### Task 0: Land the MVP cache and freeze a reproducible route baseline

**Files:**

- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Modify: `src/port/saturn/gfx/saturn_matrix.h`
- Modify: `tools/saturn/runtime_contract_test.c`
- Modify: `src/port/saturn/sourceboot/source_demo_data.c`
- Create: `tools/saturn/routes/bob_parity_v1.json`
- Create: `tools/saturn/compare_route_reports.py`
- Modify: `tools/saturn/test_tools.py`
- Modify after the cache commit lands:
  `docs/saturn/HANDOFF_2026-07-26.md`

**Steps:**

- [ ] Finish and commit the existing 12-entry lazy composed-MVP cache as an
  isolated commit. Do not mix later fixed-point or packet work into it.
- [ ] Run the host contract suite and both retained captures. Confirm
  `frame_serial=392`, 2,311 transformed triangles, 829 VDP1 primitives, zero
  faults, and the measured 0.6718 FPS rate within the established variance.
- [ ] Only after the cache commit and retained evidence exist, update the
  handoff's current baseline from the committed-HEAD value of 0.5320 FPS to
  0.6718 FPS, citing both retained reports. Until then, 0.5320 remains the
  correct baseline for HEAD.
- [ ] Add a target-test-only controller replay that feeds `OSContPad` samples;
  it must not write Mario, camera, object, or level state directly.
- [ ] Define a 600-simulation-tick BOB route with neutral startup, a bounded
  movement loop, jump, camera variation through normal input, and a stable end
  marker.
- [ ] Add a report comparator that checks route version, build hashes, complete
  frames, phase timings, memory, primitive counts, overflows, slave counters,
  asset tier, and audio counters.
- [ ] Prove determinism with two serial runs: matching simulation checkpoint
  hashes and primitive counts within documented animation/camera tolerance.

**Gate:** One clean baseline commit and two comparable reports. If replay does
not reach the same source checkpoint twice, no performance task may begin.

**Suggested commits:**

- `perf(saturn): cache composed Fast3D MVP matrices`
- `test(saturn): freeze the BOB parity route and report contract`

---

### Task 1: Add reference-derived SH-2 Q16 multiply and scheduled divide kernels

**Files:**

- Create: `src/port/saturn/gfx/saturn_q16_sh2.h`
- Create: `src/port/saturn/gpl/slavedriver_projection.S`
- Create: `src/port/saturn/gpl/slavedriver_projection.h`
- Modify: `src/port/saturn/gfx/saturn_matrix_kernels.h`
- Modify: `tools/saturn/runtime_contract_test.c`
- Modify: `Makefile.saturn.mk`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: provenance/license documents named in §4

**Reuse:**

- Directly adapt Jo Engine’s `jo_fixed_mult` `dmuls.l`/`xtrct` idiom, retaining
  its file notice.
- Close-port SlaveDriver’s `WALLASM.S` projection schedule: start DIVU after Z,
  compute X/Y and bookkeeping during the divide shadow, then collect the
  quotient. Keep that close-port under `src/port/saturn/gpl/`.
- Do not adapt Jo’s fixed divide because it reads the result immediately and
  stalls for the full DIVU latency.

**Steps:**

- [ ] First add host reference tests for signed Q16 multiply, saturation,
  negative rounding, divide-by-zero, DVCR overflow, near-W, and extreme legal
  SM64 coordinates.
- [ ] Add SH-2 target-vector tests that write kernel results to a probe block
  and compare them with the host model.
- [ ] Split projection into `divide_start()` and `divide_collect()` so callers
  cannot accidentally serialize the 39-cycle hardware divider.
- [ ] Include an explicit DVCR clear/check and counted fallback for invalid
  projection inputs.
- [ ] Disassemble the target object and assert the expected `dmuls.l`, `xtrct`,
  DIVU write, intervening work, and delayed quotient read sequence.
- [ ] Mutation-test operand order, fixed-point shift, and immediate quotient
  collection.

**Gate:** Bit-exact Q16 multiply and defined divide behavior across the fixture
corpus; the target disassembly proves a nonempty DIVU latency shadow.

**Suggested commit:** `perf(saturn): add scheduled SH2 Q16 projection kernels`

---

### Task 2: Convert the hot frontend to Q16 transform-once vertices

**Files:**

- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Modify: `src/port/saturn/gfx/saturn_transform.h`
- Modify: `src/port/saturn/gfx/saturn_matrix.h`
- Modify: `src/port/saturn/gfx/saturn_projected_workarea.h`
- Modify only if required by measured boundary cost:
  `src/game/rendering_graph_node.c`, `lib/src/guMtxF2L.c`
- Modify: `tools/saturn/runtime_contract_test.c`
- Create: `tools/saturn/fast3d_q16_diff_test.c`
- Modify: `Makefile.saturn.mk`

**Steps:**

- [ ] Capture real BOB matrices, viewports, vertex batches, geometry modes,
  lights, and expected float-path results as a host differential corpus.
- [ ] Add a generation-stamped transformed-vertex cache keyed by source vertex
  slot, modelview/projection generation, viewport generation, and lighting
  generation.
- [ ] Move transform/light into `G_VTX`; triangle commands consume cached clip
  and screen values rather than transforming three corners repeatedly.
- [ ] Perform homogeneous trivial reject and backface tests before DIVU.
  Divide only surviving vertices.
- [ ] Convert perspective, viewport map, depth, span, and winding tests to Q16
  or narrower range-proven integers.
- [ ] Intercept raw-float `guMtxIdent`/matrix producers at the smallest guarded
  seam. If keeping Q16 truth through the render graph removes measured
  conversion tax, apply the source-authority checklist rather than forcing a
  port-boundary round trip.
- [ ] Retain a `SATURN_RENDER_FLOAT_REFERENCE=1` build for differential tests,
  not shipping.
- [ ] Add counters for vertices loaded/transformed/reused, divides
  started/collected/avoided, early clip rejects, and conversion fallbacks.
- [ ] Assert no hot frontend symbol reference remains to `__addsf3`,
  `__mulsf3`, `__divsf3`, `__floatsisf`, or `__fixsfsi`.
- [ ] Compare the route: simulation checkpoints exact; visible coordinates
  within one pixel; depth bucket within one; reject-category differences
  explained only at fixed-point boundaries; no new missing primitives.

**Gate:** Zero hot-path float helpers and at least a 3× route speedup over Task
0 before keeping the change. If it misses 3×, profile the generated SH-2
assembly before continuing—do not assume “fixed point” alone solved it.

**Suggested commit:** `perf(saturn): transform Fast3D vertices once in Q16`

---

### Task 3: Compile static Fast3D subgraphs into compact Saturn render packets

**Files:**

- Create: `include/saturn/saturn_render_packet_v1.h`
- Create: `tools/saturn/compile_fast3d_packets.py`
- Create: `tools/saturn/render_packet.py`
- Modify: `tools/saturn/dl_rigid_groups.py`
- Modify: `tools/saturn/quad_map.py`
- Modify: `tools/saturn/test_tools.py`
- Create generated: `build/saturn/sourceboot/generated/saturn_render_packets.c`
- Create generated: `build/saturn/sourceboot/generated/saturn_render_packets.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Modify: `src/port/saturn/sourceboot/Makefile`

**Steps:**

- [ ] Specify a versioned, endian-explicit, offset-based packet format with
  source-DL identity, source hash, required dynamic inputs, material state,
  vertex batches, primitive indices, bounds, LOD links, texture IDs, and an
  interpreter-escape record.
- [ ] Parse source C/display-list macros using existing in-tree tooling. Do not
  copy the PS1 preprocessor or JIT.
- [ ] Fold redundant static state and nested static display lists only when a
  state-equivalence test proves the output command trace is unchanged.
- [ ] Leave matrices, animated vertices, environment colors, scrolling
  textures, geo callbacks, and unsupported opcodes as explicit runtime patches
  or safe interpreter escapes.
- [ ] Replace the current linear quad-map list search with a generated sorted
  index or bounded hash keyed by source pointer plus bank epoch.
- [ ] Validate every offset, count, recursion bound, source hash, and package
  version before a packet becomes reachable.
- [ ] Differential-test packet and interpreter traces for static level pieces,
  Mario, cannons, nested lists, dynamic material state, and deliberately
  unsupported commands.
- [ ] Add packet hit/miss/escape/validation counters and bytes interpreted
  versus bytes executed from packets.

**Gate:** At least 90% of static BOB triangle submissions use packets, every
escape matches the reference interpreter, and no stale pointer survives a bank
epoch change.

**Suggested commit:** `perf(saturn): compile static Fast3D into render packets`

---

### Task 4: Build deterministic CLUT16 texture tiers and area residency

**Files:**

- Create: `tools/saturn/compile_texture_tiers.py`
- Modify: `tools/saturn/vdp1_texture.py`
- Modify: `tools/saturn/export_rgba16_textures.py`
- Modify: `tools/saturn/test_tools.py`
- Create: `include/saturn/saturn_texture_package_v1.h`
- Create: `src/port/saturn/assets/saturn_texture_package.c`
- Create: `src/port/saturn/assets/saturn_texture_package.h`
- Modify: `src/port/saturn/gfx/saturn_texture_residency.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`
- Modify: `src/port/saturn/sourceboot/Makefile`

**Steps:**

- [ ] Inventory every texture reached by the BOB route and classify opacity,
  alpha cutout, animation, dimensions, UV span, and current VDP1 cost.
- [ ] Generate source-derived CLUT16 encodes with transparent index zero,
  deterministic palette selection, RGB1555 lane tests, and source hashes.
- [ ] Generate three explicit tiers:
  `full` for validation/near use, `balanced` for normal use, and `minimum` for
  far/pressure use. The manifest—not ad hoc runtime guessing—selects legal
  variants.
- [ ] Record per-texture source dimensions, output dimensions, palette error,
  alpha-mask mismatch, encoded bytes, and tier.
- [ ] Pack area and resident-actor texture working sets contiguously. Stage
  through the existing bounded WRAM ring and upload outside the primitive
  traversal.
- [ ] Make runtime lookup O(1) or bounded-logarithmic by generated texture ID;
  count residency hits, misses, promotions, evictions, bytes staged, and any
  frame-loop load attempt.
- [ ] Prohibit an individual blocking CD read from `G_SETTIMG`,
  `G_LOADBLOCK`, or primitive emission.
- [ ] Side-by-side capture full and balanced tiers at near, mid, and far route
  checkpoints. The user’s visual decision is final.

**Gate:** 100% of route textures resolve to a prepared variant, zero mid-frame
CD texture reads, no alpha regression, and accepted balanced captures.

**Suggested commit:** `feat(saturn): add prepared CLUT16 texture tiers`

---

### Task 5: Generate geometry LOD, large-polygon, and shadow tiers

**Files:**

- Create: `tools/saturn/compile_geometry_tiers.py`
- Modify: `tools/saturn/saturn_mesh_ir.py`
- Modify: `tools/saturn/quad_pairing.py`
- Modify: `tools/saturn/test_tools.py`
- Extend: `include/saturn/saturn_render_packet_v1.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`
- Create: `src/port/saturn/gfx/saturn_lod.h`

**Steps:**

- [ ] Derive rigid groups and legal simplification boundaries from source
  display lists. Never merge across material, UV seam, alpha, skinning,
  animation, geo-callback, or culling-mode boundaries.
- [ ] Generate full/balanced/minimum mesh tiers with a deterministic error
  metric and source-triangle provenance.
- [ ] Generate at most 2× subdivision for polygons whose world-space edge,
  UV-span, and projected-span bounds prove that VDP1 distortion or clipping
  needs it. Do not tessellate every area polygon as the PS1 tool attempted.
- [ ] Put bounding sphere/AABB, error radius, LOD thresholds, and hysteresis in
  the packet. Use camera distance and projected error; count transitions.
- [ ] Lower source shadow intent to a bounded Saturn tier: source-equivalent
  full geometry where affordable, a simple translucent or subtractive polygon
  in balanced mode, and a small blob/disabled tier only under explicit minimum
  policy.
- [ ] Add tests for silhouette bounds, UV/material preservation, deterministic
  output, subdivision cap, degenerate rejection, and LOD hysteresis.
- [ ] Capture representative terrain, cannon, Mario, tree/alpha, and shadow
  checkpoints in all tiers.

**Gate:** Balanced geometry reduces transformed vertices or VDP1 commands by
at least 25% at the route’s far checkpoints, does not increase the near-view
full tier, produces no capacity overflow, and passes the user visual gate.

**Suggested commit:** `feat(saturn): generate bounded geometry and shadow tiers`

---

### Task 6: Build a resident, versioned animation bank

**Files:**

- Create: `tools/saturn/compile_animation_bank.py`
- Modify: `tools/saturn/test_tools.py`
- Create: `include/saturn/saturn_animation_bank_v1.h`
- Create: `src/port/saturn/assets/saturn_animation_bank.c`
- Create: `src/port/saturn/assets/saturn_animation_bank.h`
- Modify: `tools/saturn/prepare_sourceboot_assets.py`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify only at the target boundary if necessary:
  `include/mario_animation_ids.h` or the existing animation load seam

**Steps:**

- [ ] Inventory animation headers, indices, values, frame ranges, actual route
  reachability, and raw bytes. Keep all source animation IDs stable.
- [ ] First emit an uncompressed offset-based package and prove exact decode
  against source arrays for every animation/frame/channel.
- [ ] Add deterministic delta/RLE or another small decoder only if it reduces
  the complete required bank. Reject the codec if decoder cost or temporary
  memory exceeds the raw residency benefit.
- [ ] Include source hash, version, decoded size, encoded size, animation
  table, and per-entry bounds. Validate before publishing pointers.
- [ ] Load the resident Mario bank at startup/area transition, not during an
  animation frame. Use actor-slot epochs for nonresident actors.
- [ ] Differential-test every route animation plus edge cases that failed in
  the PS1 port: final frame, zero-length channel, repeated index, animation
  switch, and bank reload.
- [ ] Add bytes raw/encoded/resident, decode time, load count, cache epoch, and
  invalid animation counters.

**Gate:** Exact decoded animation samples, zero frame-loop loads, zero invalid
animation accesses, and either at least 40% size reduction or a documented
decision to ship the exact uncompressed resident package because compression
costs more than it saves.

**Suggested commit:** `feat(saturn): prepare resident versioned animation banks`

---

### Task 7: Generalize fixed-VMA packages and promote the hot working set

**Files:**

- Create: `tools/saturn/generate_level_manifest.py`
- Create: `tools/saturn/build_saturn_packages.py`
- Modify: `tools/saturn/test_tools.py`
- Create: `include/saturn/saturn_asset_manifest_v1.h`
- Create: `src/port/saturn/assets/saturn_asset_loader.c`
- Create: `src/port/saturn/assets/saturn_asset_loader.h`
- Modify: `src/port/saturn/sourceboot/source_cart.c`
- Modify: `src/port/saturn/sourceboot/source_cart.h`
- Modify: `src/port/saturn/sourceboot/sourceboot-cart.x`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: packet, texture, and animation caches to consume bank epochs

**Steps:**

- [ ] Generate shared, level, actor, texture, animation, and skybox manifests
  from source `LOAD_*` commands and geo/display-list reachability.
- [ ] Retain the accepted fixed-VMA native-pointer compatibility slots:
  shared, level, two actor slots, skybox, and spare cartridge space.
- [ ] Store generated packet/texture/animation IR as validated offsets inside
  versioned packages, even while source symbols use fixed VMAs.
- [ ] Reload through a bounded internal-WRAM staging ring; never traverse
  uncached cartridge records per primitive.
- [ ] Promote touched packet headers, vertex batches, bounds, texture metadata,
  and current actor animation tables into measured HWRAM/LWRAM hot arenas.
- [ ] Increment a bank epoch before publishing a replacement bank and make all
  pointer-keyed caches reject old generations.
- [ ] Add manifest/link-map reports with ROM/cart/HWRAM/LWRAM/VDP1/SCSP bytes
  by class and worst-case simultaneous residency.
- [ ] Prove BOB → second area → BOB and actor-slot replacement without stale
  packet, texture, animation, or source pointers.

**Gate:** Second-area reload proof, zero stale-epoch uses, no hot traversal from
cartridge, 4 MiB compliance, and at least 4 KiB HWRAM linker headroom.

**Suggested commit:** `feat(saturn): add versioned fixed-VMA asset packages`

---

### Task 8: Add hierarchical visibility, bounded ordering, and command overlap

**Files:**

- Create: `src/port/saturn/gfx/saturn_visibility.h`
- Create: `src/port/saturn/gfx/saturn_visibility.c`
- Modify: `src/port/saturn/gfx/saturn_render_queue.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`
- Modify: `src/port/saturn/gfx/saturn_command_arena.h`
- Modify: `src/port/saturn/gfx/saturn_vdp1_backend.h`
- Modify: `src/port/saturn/gpl/slavedriver_dma_queue.c`
- Modify: `tools/saturn/runtime_contract_test.c`

**Steps:**

- [ ] Build a tri-state `OUTSIDE/INTERSECT/INSIDE` culling walk over generated
  bounds attached to the existing source geo/room hierarchy.
- [ ] Propagate `INSIDE` so children skip redundant plane tests; an
  `INTERSECT` parent must continue testing children.
- [ ] Traverse near-to-far so a bounded command arena degrades by dropping the
  least important far work, never by corrupting or blacking the frame.
- [ ] Replace the multi-pass depth sweep and any `__divdi3` key calculation
  with a power-of-two bucket/radix structure derived from libmic3d patterns.
- [ ] Preserve explicit material/order classes for opaque, alpha-test,
  translucent, decals, HUD, and backgrounds; depth sorting must not reorder
  across semantic barriers.
- [ ] Double-bank CPU command construction and SCU-DMA upload. VDP1 consumes
  bank N while CPUs prepare bank N+1; ownership changes only at an explicit
  fence.
- [ ] Count hierarchy tests/skips, nodes rejected, primitives avoided, bucket
  occupancy, far drops, DMA bytes/waits, CPU-bank waits, and VDP1 waits.
- [ ] Test parent-inside propagation, partial intersections, near-first
  overflow, stable equal-depth order, semantic barriers, and both bank fences.

**Gate:** Identical visible route coverage, no `__divdi3` in the ordering path,
zero bank races/overflows, and a measured reduction in transform/order or
command phase time.

**Suggested commit:** `perf(saturn): add hierarchical culling and overlapped command banks`

---

### Task 9: Put transform/light/clip/cull jobs on the slave SH-2

**Files:**

- Create: `src/port/saturn/platform/saturn_dual_cpu.c`
- Create: `src/port/saturn/platform/saturn_dual_cpu.h`
- Create: `src/port/saturn/gfx/saturn_render_jobs.c`
- Create: `src/port/saturn/gfx/saturn_render_jobs.h`
- Create: `src/port/saturn/gfx/saturn_slave_renderer.c`
- Create: `src/port/saturn/gfx/saturn_slave_workarea.h`
- Create: `src/port/saturn/gpl/slavedriver_worker_notes.md`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `tools/saturn/runtime_contract_test.c`

**Reuse:**

- Close-port SlaveDriver’s bounded result count, private slave work area,
  cache-through result publication, timer/interrupt signalling, and adaptive
  master/slave split.
- Use libyaul dual-CPU APIs as the hardware dependency.
- Do not port SlaveDriver’s wall/sector topology or Z-Treme’s octree.

**Steps:**

- [ ] Define immutable render jobs containing packet/vertex IDs, matrix
  generation, material/light state, output slice, and bank epoch. Neither CPU
  may mutate source game state.
- [ ] Give each CPU private transform/clip scratch. Give each job a disjoint
  output range with capacity checked before dispatch.
- [ ] Publish jobs/results through cache-through or explicitly purged cache
  lines; document the bus-visible ownership transition.
- [ ] Start the slave batch before the master handles dynamic/interpreter
  escapes and final linkage.
- [ ] Join at a bounded deadline. On timeout, count the fault and complete
  safely on the master in diagnostic builds; shipping completion requires zero
  timeouts.
- [ ] Add a serial feature flag that runs the exact same job records and output
  merge order for differential comparison.
- [ ] Add an adaptive split based on prior master/slave completion time, with
  minimum batch size to avoid dispatch overhead and a fixed deterministic mode
  for tests.
- [ ] Compare serial and dual output records byte-for-byte before VDP1 pointer
  relocation. Compare route simulation hashes and captures afterward.
- [ ] Report eligible jobs, master/slave jobs, cycles, dispatch latency, wait
  cycles, result bytes, cache maintenance, and estimated bus contention.

**Gate:** Slave executes at least 30% of eligible work, master join wait is
under 10% of render time, output matches serial mode, and dual mode improves
the same balanced route by at least 20%. If it regresses, keep serial mode and
profile bus/cache behavior; do not claim the second SH-2 is “used” merely
because it wakes.

**Suggested commit:** `perf(saturn): distribute render jobs across both SH2s`

---

### Task 10: Replace the audio no-op with a source-built MC68EC000 PCM service

**Files:**

- Create: `src/port/saturn/audio/ponesound/LICENSE`
- Create/adapt: `src/port/saturn/audio/ponesound/driver.c`
- Create/adapt: `src/port/saturn/audio/ponesound/driver.ld`
- Create: `src/port/saturn/audio/saturn_audio_mailbox.h`
- Create: `src/port/saturn/audio/saturn_audio.c`
- Create: `src/port/saturn/audio/saturn_audio.h`
- Create: `tools/saturn/compile_pcm_bank.py`
- Create: `tools/saturn/m68k_smoke.c`
- Create: `tools/saturn/m68k_smoke.ld`
- Modify: `tools/saturn/test_tools.py`
- Modify: `tools/saturn/bootstrap-toolchain.ps1`
- Modify: `tools/saturn/bootstrap-toolchain.sh`
- Modify: `Makefile.saturn.mk`
- Replace: `src/port/saturn/sourceboot/source_audio_stub.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: provenance/license documents named in §4

**Reuse:**

- Close-port the minimum MIT-licensed PoneSound source at
  `31782e4c61337327f23eb9aa45ecd37fe0944ea0`: vector/linker layout, SCSP slot
  programming, mailbox, one-shot PCM lifecycle, and sound-CPU reset sequence.
- Build it from source with the configured `m68keb-elf` tools. Do not ship the
  untraceable `sdrv.bin` from libyaul-examples.
- Exclude ADX, streaming, CDDA, Jo Engine APIs, and unused 93-voice machinery
  from the first slice.

**Steps:**

- [ ] Make `work/upstream/SCSP_poneSound` under the repository root the
  canonical reference checkout. If absent, clone
  `https://github.com/ponut64/SCSP_poneSound.git`; then verify the origin,
  detach at `31782e4c61337327f23eb9aa45ecd37fe0944ea0`, and verify `LICENSE`
  plus every file named in §4 before adapting code. A checkout in the parent
  workspace does not satisfy this gate.
- [ ] Extend both portable bootstrap scripts to require
  `${YAUL_ARCH_M68K_PREFIX}-gcc`, assembler, linker, and objcopy inside the
  pinned Yaul Docker image. Print and retain their versions before any audio
  source is compiled. A prefix variable alone is not proof of a toolchain.
- [ ] Add `verify-m68k-toolchain` to `Makefile.saturn.mk`. Compile
  `m68k_smoke.c` as freestanding MC68000 code, link it with `m68k_smoke.ld`,
  objcopy a nonempty binary, and verify the ELF machine and reset-vector
  layout. Run this target from the bootstrap container so a host lacking
  `m68keb-elf-gcc` still has one reproducible build path. If the pinned image
  lacks any tool, stop before driver work and pin the replacement toolchain
  source/image in `BUILDING.md` and `PROVENANCE.md`.
- [ ] Add a host ABI/layout test for the big-endian SH-2/68K mailbox, ring
  indices, voice command, heartbeat, and status words.
- [ ] Build a position-independent 68K image at the documented sound-RAM
  address with vector table, bounded stack, mailbox, and at least four PCM
  voices.
- [ ] Follow the bounded reset lifecycle: stop sound CPU, clear the declared
  region, copy driver and bank, publish mailbox, restart within the documented
  limit, wait for heartbeat.
- [ ] Compile a small source-derived PCM bank with stable sound IDs, sample
  rate, format, loop, pan, and length metadata.
- [ ] Replace `play_sound()` with a bounded enqueue; map at least jump,
  coin/pickup, and impact route events. `audio_signal_game_loop_tick()` advances
  the transport once per source tick.
- [ ] Keep sequence/music APIs explicitly counted as unsupported in this
  sprint rather than silently pretending they played.
- [ ] Add mailbox enqueue/drop/high-water, 68K heartbeat, voices started/
  completed, unknown ID, bank bytes, and master-SH2 audio cycles.
- [ ] Prove a sound request reaches the 68K and SCSP slot in Ymir telemetry,
  then perform an audible user check. Later retail proof remains separate.

**Gate:** The canonical checkout has the expected origin and pin; the
freestanding compiler/linker/objcopy smoke gate passes in the documented
bootstrap environment; the 68K heartbeat is nonzero; real source
`play_sound()` events start and finish PCM voices; mailbox drops are zero on
the route; and master-SH2 audio work is bounded to transport rather than
sample mixing.

**Suggested commit:** `feat(saturn): run source sound effects through the SCSP 68K`

---

### Task 11: Integrate fidelity profiles and close the sprint with evidence

**Files:**

- Create: `include/saturn/saturn_fidelity_profile.h`
- Modify: `Makefile.saturn.mk`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.c`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.h`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `tools/saturn/runtime_contract_test.c`
- Modify: `tools/saturn/compare_route_reports.py`
- Modify: `docs/saturn/HANDOFF_2026-07-26.md` or create the next dated handoff
- Modify only after user acceptance:
  `docs/saturn/evidence/TIMELINE.md`, gallery metadata

**Profiles:**

| Profile | Purpose |
| --- | --- |
| `full` | Highest prepared source fidelity; correctness and visual reference |
| `balanced` | Shipping target; selected texture/geometry/animation tiers, dual SH-2, 68K audio |
| `minimum` | Explicit pressure/far fallback; never silently selected as “full” |
| `reference-serial` | Float/interpreter/one-SH2 diagnostic only, excluded from shipping |

**Steps:**

- [ ] Make profile selection one versioned build contract. Reports must include
  profile, packet/package versions, feature flags, source hashes, and CPU mode.
- [ ] Add a tested 30 Hz source-update accumulator at the platform frame
  boundary. Slow rendering may skip presentation work, but it may not skip or
  duplicate authoritative simulation ticks. Test VBlank sequences covering
  60, 50, 30, 20, 15, and irregular render cadence.
- [ ] Run all host tool, runtime contract, Q16 differential, packet
  differential, asset decode, epoch, dual-CPU serial-equivalence, and audio ABI
  tests.
- [ ] Build/verify all four profiles without weakening linker or capacity
  assertions.
- [ ] Run the 600-tick route for full, balanced serial, balanced dual, and
  minimum. Retain two balanced-dual runs for variance.
- [ ] Measure median and 1% low completed-frame rate from complete frames only;
  report simulation ticks separately from render frames.
- [ ] Require the balanced-dual result to meet 15 FPS median / 12 FPS 1% low,
  all zero-fault gates, the second-area reload, and a dynamic actor reload.
- [ ] Capture matched full/balanced checkpoints for near terrain, distant
  terrain, Mario animation, alpha texture, shadow, large polygon, and the
  reloaded area.
- [ ] Present the matched captures and an audible audio build to the user.
  Record their decision verbatim.
- [ ] Only after approval, update the timeline/gallery and write the next
  handoff with retained negative results and exact hashes.

**Gate:** Every exit gate in §5 passes. This is the only task allowed to call
the sprint complete.

**Suggested commit:** `docs(saturn): close the native PS1 parity sprint`

---

## 8. Per-task retention rule

A performance change is retained only if:

1. the relevant differential/behavior tests pass;
2. complete-frame and zero-overflow gates pass;
3. it improves its named phase or supplies a required capability;
4. memory and bus costs are recorded; and
5. the result is compared against the immediately preceding retained build.

Record and revert misleading wins, including changes that:

- merely shift time into an unmeasured phase;
- reduce submitted work by losing visible geometry;
- improve Ymir by exploiting emulator-specific behavior;
- wake the slave while increasing total wall time;
- reduce ROM while increasing frame-time decode cost;
- produce partial frames faster; or
- silently choose a lower fidelity tier.

---

## 9. Explicitly out of scope

- Rewriting SM64 gameplay, object, camera, collision, or level-script systems.
- Copying code from the unlicensed PS1 port.
- Adopting the PS1 port’s 8 MiB benchmark assumptions.
- Porting SlaveDriver wall/sector content structures or Z-Treme’s exact octree.
- Hand-editing source assets to look acceptable on Saturn.
- Full SM64 sequenced music, ADX streaming, CDDA soundtrack policy, or every
  sound effect. This sprint proves and uses the 68K PCM transport; the full
  music architecture remains its own milestone.
- Claiming retail performance from Ymir. Retail capture follows once the
  integrated emulator gate is stable.
- Full-game/120-star completion, title-screen repair, pause-menu completion,
  or camera changes that the PS1 port itself has not completed.

---

## 10. Immediate next action

Finish Task 0 before starting another optimization:

1. run the host contract suite against the open MVP-cache changes;
2. retain the two existing long-capture reports;
3. commit the cache alone; and
4. add the deterministic BOB input route and comparison contract.

Then begin Task 1. Do not start packet, asset, slave, or audio integration on a
moving renderer baseline.
