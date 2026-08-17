# Changelog

## [Unreleased]

### Changed

- Sprint 2 T2.22 (build): eight Saturn asset generators now publish
  write-if-changed, through a new shared `tools/saturn/write_if_changed.py`.
  Every build linked the ELF **four times** and wrote four byte-identical
  52,289,956-byte `objdump -S` listings; it now links **once**.
  `-j12` from scratch **694.9 s → 417.0 s (−277.9 s, −40.0%)`; `-j1` from
  scratch **1039 s → 797.1 s (−241.9 s, −23.3%)**. Re-running `make sourceboot` with
  nothing changed used to perform four links and four listings and now
  performs **zero compiles, zero links and zero listings**.

  **Root cause.** One build is four fresh parses of the sourceboot makefile:
  the build sub-make, the `pre-build-iso` and `post-build-iso` hooks that
  `third_party/libyaul/libyaul/build/build.post.iso-cue.mk:24,33` re-enters,
  and the `verify-sealed-inputs seal-release` sub-make. Several generated
  sources are produced by rules with PHONY prerequisites — deliberately, so a
  stale package generation or a renamed geo source cannot survive into a
  build — and those generators rewrote their outputs unconditionally. Identical
  bytes, fresh mtimes. The `.d` files then make those outputs ordinary
  prerequisites of the objects, so each parse restaled
  `src/game/rendering_graph_node.o` and `src/port/saturn/gfx/saturn_demo_render.o`,
  recompiled them, relinked, and re-ran `nm` and `objdump -S`. The BOB chain
  was worse than wasteful: PHONY `compile-bob-area` rewrites
  `bob_area1_intake.json`, which is a *prerequisite* of the tiles rule, whose
  output is a prerequisite of the BSP rule, so each parse handed the next one a
  fresh reason to run.

  **Changed generators.** `extract_bob_area.py`, `saturn_mesh_ir.py`,
  `bake_bob_tiles.py`, `compile_bob_bsp.py`, `emit_bob_scene.py`,
  `geo_depth_manifest.py`, `gen_actor_identity_registry.py`,
  `compile_sourceboot_sfx_bundle.py`. `compile_scene_package.py` was **not**
  changed — contrary to what T2.18 section 6 predicted, it has always published
  through `publish_or_verify_set`, which already compares bytes.

  **The PHONY prerequisites were kept.** Each buys a guarantee a file
  prerequisite cannot express — a package-generation variable change, an
  `rglob` over source directories, a cross-makefile sub-make goal. With
  write-if-changed they cost one Python process instead of a relink. An
  order-only prerequisite is not a substitute: `$(SH_OBJS_UNIQ)` already
  depended on two of these order-only and the loop happened anyway.

  **Consumer impact: the sealed identity moves once.** The makefiles and every
  generator script are source-closure inputs, and the closure hash is compiled
  into the ELF by `src/port/saturn/platform/saturn_build_identity.c`. The tuple
  that sealed `id-0fade22f26a95c0c` now seals `id-bed197e0c5e928d3`, so the
  next build writes a new `e2-bob-identity-id-*` tree and
  `release_manifest.py compare` reports six differing fields, all of them that
  identity chain. Nothing else moved: **279 of 280 object files are
  byte-identical** (the exception is `saturn_build_identity.o`), `SOURCE.DAT`
  and the CUE are byte-identical, and the two ELFs are the same size and differ
  in exactly one 64-byte run, wholly inside the 500-byte identity blob.

  **Migration note.** Any tooling that pins `id-0fade22f26a95c0c` must move to
  `id-bed197e0c5e928d3`. The A9A baseline under
  `build/saturn/baselines/a9a-2026-08-05/` is untouched.

  Evidence, including the mutation-checked regeneration tests that prove a
  corrupted generated header is still repaired and still re-links:
  `docs/saturn/evidence/reports/sprint2-t2_22-build-relink-loop.md`.

- Sprint 2 T2.17 (scheduler): the frame pipeline's per-field epoch rule is
  narrowed from "one SERVICE and one POLL per observed field, and neither for a
  promoted bank in the publication field" to the two gates that actually carry
  a hardware guarantee. `src/port/saturn/runtime/saturn_frame_pipeline.c`.

  **Root cause.** T2.16 attributed 24 of 24 master idle entries to one call
  site -- `sourceboot/main.c:2013`, the `SM64_SATURN_FRAME_WAIT_VBLANK` arm --
  and measured **2.1903 VB/frame of raster spin interleaving 0.141 VB/frame of
  transport work, a 1:14 ratio**, in one contiguous block that repeats
  identically every frame. The master is the critical path with no residue
  (9.0166 work + 2.1903 idle = 11.2069 exactly), so every VB of that block is
  a VB of frame time. Two of the three costly waits were the whole of it: one
  field burned between the poll that *submits* the command-list DMA and the
  poll that *retires* it, and one field burned because publication stamped the
  render-service epoch and so refused the promoted snapshot's first service.

  **What the rule guaranteed, and why it existed.** It was added as an
  independent-review repair during A9 Steps 1--4
  (`overlapped-render-pipeline-2026-08-03.md:1825-1828`): "Publication also
  resets generation-local SERVICE/POLL flags, permitting more work for a
  promoted generation during the same observed VBlank." Field-global epochs
  bound that. The guarantee they buy on the hardware is that at most one
  publication -- and therefore one `vdp1_sync_render()` plot start and one
  frame-buffer change request -- happens per observed field, and that a
  generation's command-VRAM overwrite never lands in the field whose
  publication started the plot reading that same resident VRAM.

  **Why the narrowing is safe.** Both guarantees ride on `POLL_TRANSFERS`
  and `PUBLISH_FRAME`, not on `SERVICE_RENDER_JOBS`. `SERVICE` builds into the
  frame bank publication has just retired (`sourceboot_frame_publish` publishes
  the new bank and retires the previous one before presenting, leaving exactly
  one FREE bank of the two for the promoted generation) and it touches no VDP1
  register and no VDP1 VRAM, so it cannot race the plot publication started.
  Publication therefore keeps its transfer stamp -- which alone still bounds
  publication to one per field -- and drops its service stamp. Separately, only
  the *submitting* poll fences VDP1 and DMAs over resident command VRAM; its
  follow-ups re-read a DMA status word. Follow-ups are admitted inside the
  submit field only, so an unretired queue falls back to the previous
  one-poll-per-field schedule with its previous-frame presentations, and the
  free window is bounded by the field itself.

  **Consumer-facing impact.** Presentation cadence and displayed image are
  unchanged in content; only *when* the master issues each action moves. One
  visible scheduling consequence: because `render_service_started` gates the
  simulation arm, admitting service in the publication field also makes that
  generation's recovery sim tick admissible one field earlier. The
  normal-plus-recovery budget itself is unchanged.

  **New prerequisite.** This makes the `vdp1_sync_busy()`/`vdp1_sync_wait()`
  overwrite fence in `sourceboot_frame_poll_transfers` load-bearing for the
  first time: T2.8 measured 0 waits in 1,349 fence events on a 15.483 VB/frame
  instrumented build, where slack hid it. At a shorter frame the fence, not the
  scheduler, becomes what separates the next DMA from the running plot.

### Added

- Sprint 2 T2.22 (build/tests): `tools/saturn/write_if_changed.py` and
  `tools/saturn/test_write_if_changed.py` (11 tests, wired into
  `verify-tools`) — a content-comparing publish helper for generators whose
  rules are intentionally always out of date, plus the tests that keep it from
  degrading into silent staleness: a corrupted or truncated output must still
  be rewritten, a genuine input change must still move the bytes, and
  CRLF-on-disk must not be accepted as matching an LF-pinned write.
- Sprint 2 T2.22 (evidence):
  `docs/saturn/evidence/reports/sprint2-t2_22-build-relink-loop.md`.
- Sprint 2 T2.17 (evidence):
  `docs/saturn/evidence/reports/sprint2-t2_17-epoch-stall.md`,
  `sprint2-t2_17-throughput-30events.json`,
  `sprint2-t2_17-idle-attribution-fullframe.json`.

  **Measured: 6.7181 FPS mean / 8.9310 VB per frame** on `id-c0352f297034f653`
  (ELF SHA-256 `2933c5d5…8c2fecd`), against the `id-a61d5203793986e7` baseline
  of 5.3538 FPS / 11.2069 VB — **+25.5% FPS, -2.2759 VB/frame**, median 6.6667,
  1% low 6.0, `summarize_cadence`, 30 presentation events, on-target identity
  MATCH. `presentation_generation_delta == 1` on all 29 intervals and the frame
  bank queue retired 30 of 30 generations with zero faults, which are the two
  scheduler-level checks against a dropped or duplicated field.

  **Confirming measurement: the raster spin is gone, directly observed.** A
  contiguous 16.3566-VBlank (1.83-frame) dual-CPU trace measures master idle at
  **0.000000** with **zero** excursions into
  `_sm64_saturn_source_runtime_wait_vblank` and an empty wait-call-site table,
  against T2.16's 19.544% / 2.1903 VB / 24 excursions. Every other master
  symbol's absolute VB/frame is within ~7% of where T2.16 measured it, so the
  stall was removed without disturbing the work.

  **VDP1 is now the wall: `EDSR.CEF` set in 6.33% of samples, so VDP1 plots
  93.67% of the frame**, up from 88.0%. This promotes T2.8's fill-rate list —
  user clipping, command-count LOD, the Mario double-emit — for the first time
  with a cadence success criterion.

  **Two caveats that bind readers of these numbers.** (1) The T2.11 concurrency
  allowance is needed on **28 of 29** intervals against 0 of 29 at T2.13, so the
  phase decomposition no longer closes and must not be quoted; `summarize_cadence`
  reads presentation-edge deltas and is independent of it, so the cadence figures
  stand. (2) Both idle-attribution captures warm up by a *fixed* 1,800 VBlanks,
  so a 25% faster build samples a different point on the replay route — which is
  the likeliest reason the absolute VDP1 plot time reads ~8.37 VB here against
  T2.16's ~9.86 VB. Warm-up should count simulation ticks before anyone sizes
  fill-rate work from it.

- Sprint 2 T2.17 (tests): three frame-pipeline contract tests and three
  mutation executables in `verify-frame-pipeline`.
  `test_publication_field_admits_service_but_not_transfer`,
  `test_publication_field_consumes_the_transfer_slot` (the case that isolates
  publication's own stamp -- a frame whose transfer retires after its last poll
  publishes in a field that issued no poll, so nothing but that stamp refuses
  the promoted generation's overwrite), and
  `test_followup_polls_are_bounded_to_the_submit_field`. The new mutations
  `SM64_SATURN_FRAME_PIPELINE_TEST_PUBLISH_OPENS_TRANSFER`,
  `..._FREE_POLL_EVERY_FIELD` and `..._SUBMIT_UNGATED` perturb the
  bank-ownership condition, the swap point and the transport interleave
  respectively; each must fail, and the first of them survived the suite's
  first draft, which is why the isolating test exists.

### Added

- Sprint 2 T2.16 (diagnostics): `tools/saturn/capture_idle_attribution.py`, a
  contiguous dual-CPU trace that attributes idle SH-2 cycles to a wait site and
  a cause on the shipped ELF, with no rebuild and no instrumentation.

  **Why a new tool and not the T2.13 sampler.** `capture_softfloat_profile.py`
  answers "what is executing at this PC" and cannot answer "what is this
  processor waiting on", for three reasons this tool fixes. (1) It samples the
  two SH-2s in *alternating* bursts, so it never observes the pair
  (master PC, slave PC) at one emulated instant -- and "is the slave idle
  because the master is computing, or because the master is idle too?" is a
  joint question. `Saturn::StepMasterSH2()` advances the slave by exactly the
  master's cycles, so pausing after a master step and reading both register
  files is one coherent snapshot. (2) It attributes by PC only.
  `_sm64_saturn_source_runtime_wait_vblank` is a leaf -- both
  `vdp2_tvmd_vblank_in_wait` and `..._out_wait` are `__always_inline` in
  libyaul -- so PR holds the caller's return address for the whole spin, and
  reading PR attributes the wait to its exact call site at a 100% hit rate,
  where T2.14 section 7.1 measured call-edge sampling as far too sparse to
  attribute anything. (3) **Burst sampling after `exec.run_for` is
  raster-phase-locked**: `Saturn::RunFrameImpl()` runs until the vertical phase
  *enters* `BlankingAndSync`, so every burst begins at the start of VBlank --
  precisely where the master's VBlank spin lives. This tool therefore traces
  contiguously, with no gap and no phase selection.

  **New quantities.** Per-CPU cycle residency normalised per CPU rather than
  pooled; the run-length structure of every symbol (how many times the master
  entered its spin and how long each excursion lasted); PR-based wait-site
  attribution; the master x slave joint contingency table, whose both-idle cell
  is the pure scheduler stall; an ordered run log that reconstructs the frame's
  real action sequence; and a strided VDP1/VDP2 register witness (EDSR/LOPR/
  COPR, TVSTAT) so the VDP1 starvation question is answered on the same build
  in the same run.

  **Prerequisite for readers of any prior profile.** Ymir's headless rig has
  `m_emulateSH2Caches = false` (`ymir-core/src/ymir/sys/saturn.cpp:156`) and no
  inter-SH-2 bus arbitration; per-region access latency *is* modelled. Every
  cadence and profile number this project holds is therefore blind to the
  cache-through cost of handing work to the slave and to HWRAM bus contention,
  and any "releasable cycles" figure derived from this rig is an upper bound on
  what hardware would return.


### Changed

- Sprint 2 T2.15 (measurement): the geo walk's cadence ceiling is now measured
  rather than estimated, and the estimate it replaces was wrong by 24x.

  **What changed and why.** T2.14 closed by naming "the geo walk builds a
  display list nobody reads" as the next lever, at "roughly 10x the entire
  shadow path", and pointed at the pre-existing sealed diagnostic
  `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1` to bound it in one build. T2.15 made
  that build (`id-137ecb7d231a34d6`) and measured **6.3273 FPS mean / 9.4828
  VBlanks per frame** against the 5.3538 / 11.2069 baseline -- **+0.9735 FPS,
  +18.18%**, with median and 1% low both improving. That is the largest single
  lever measured on this route.

  **Root cause of the bad estimate.** T2.14 arrived at "10x" by attributing
  `_saturn_geo_enter_object` (1.7048% of sampled cycles) and
  `_saturn_mtxq_refresh_float_mirror` (0.5854%) to display-list construction.
  A site-by-site classification of `src/game/rendering_graph_node.c` shows both
  are dominated by state the demo renderer and the simulation read back: the
  list-building inside `saturn_geo_enter_object` is exactly three statements
  (`:1643`, `:1647`, `:1650`), and `saturn_mtxq_refresh_float_mirror` is
  entirely state -- its own comment at `:1602-1607` records that it cannot even
  be *deferred*, because `cameraToObject` and `obj_is_in_view` read the float
  mirror first. The genuinely removable set is the four symbols
  `_geo_append_display_list`, `_alloc_only_pool_alloc`, `_alloc_display_list`
  and `_saturn_mtxq_write_wire`: **0.0737% of cycles, 3,695.7 cycles/frame**,
  which is **0.41x** the shadow path, not 10x.

  **Tradeoff taken.** No code change was landed. Removing the display-list
  construction is worth **+0.004 to +0.011 FPS** (0.07%-0.21%), about half the
  shadow-path gating T2.14 already declined; `gDisplayListHead` is written at
  443 sites across 13 files, four of the highest-value sites both compute state
  and write it into a list, and the byte-identical oracle the change would need
  costs more than the prize. The classification is the durable deliverable.

  **Consumer-facing.** The ceiling build is a diagnostic and must never ship:
  `area.c:400` gates the whole walk plus `render_hud()` and
  `render_text_labels()` in one `if`, so besides the animation/warp/camera/
  water/moving-texture/carpet/matrix state its own comment names, it also
  freezes `sPowerMeterHUD`, which `sourceboot/main.c:505-506` publishes into the
  render snapshot -- a snapshot field, newly documented here. Anyone chasing the
  remaining 99.5% must also re-check the T2.11 concurrency rail: the allowance
  goes from needed in 0 of 29 intervals to 29 of 29 at the ceiling.

  Prerequisite recorded for the next builder: the known g15 repair is delete
  **and then explicitly republish** `build/saturn/packages/bob/1/actors-v3-g15/`
  -- deleting alone fails the build with "actor family bundle publication is
  incomplete".

### Fixed

- Sprint 2 T2.15 (docs): recorded two further instances of the MSYS/path
  recipe-defect class in `STATE.md`, bringing the known count to four.
  `verify-source-geo-state-diff` hands an MSYS-form path to a native-Windows
  Python `subprocess.run` and dies with `FileNotFoundError`, though its binary
  passes when run by hand; `verify-graph-q16-contract` omits
  `build/saturn/sourceboot/generated` from its include path while
  `saturn_geo_walk_storage.h:8` needs the generated
  `saturn_geo_depth_manifest.h`, so it has been dying at the preprocessor and
  verifying nothing inside `verify-all`. Recipes not repaired here; recorded so
  they are not rediscovered a fifth time.

### Added

- Sprint 2 T2.14 (diagnostics): `tools/saturn/capture_route_counters.py`, which
  peeks the shipped ELF's live counter blocks over a running Ymir session --
  no rebuild, no instrumentation, no diagnostic tuple, ~50 s end to end. It
  reads three globals that already existed and had **never once been
  captured**: `sourceboot_fast3d.profile` (the per-triangle funnel and every
  `reject_*` bucket), `sState` (`submitted_tasks`/`unhandled_tasks`/
  `scene_graph_walks`, i.e. whether the source display list is submitted at all
  and whether the geo walk ran), and `gNumCalls` (the engine's own exact
  `find_floor`/`find_ceil`/`find_wall` tally).

  **Root cause of the long-standing gap this closes.** The evidence tree has
  carried the funnel counters as "wired but never captured" since they were
  added, and the reason was never diagnosed: the publisher that copies them
  into `sourceboot_route_checkpoint` sits behind
  `#if SATURN_SOURCEBOOT_ROUTE_REPLAY && !SATURN_SOURCEBOOT_LIVE_INPUT`
  (`src/port/saturn/sourceboot/main.c:599`), and the shipped profile sets
  `live_input_mode: 1`. `sourceboot_route_checkpoint` is therefore **not even
  present in the product ELF's symbol table**, so every tool that went looking
  for it found nothing. The underlying struct is live regardless, so this tool
  reads it directly and sidesteps the publisher entirely.

  Two consequences are baked into the tool rather than left for the next reader
  to rediscover: the counters are **cumulative, not per-frame**, because the
  per-frame `memset` lives in `sm64_saturn_fast3d_frontend_submit` which never
  runs under `SATURN_DEMO_PATH=1` -- so rates are reported as deltas per unit
  of `frame_serial`; and `gNumCalls` is three `s16`, so its deltas are
  wrap-corrected. Struct offsets are pinned in the module from `offsetof()` on
  a host compile of `saturn_fast3d_frontend.h`, and addresses are passed
  explicitly so a report always names the build it measured.

  Consumer-facing: this makes "does this code path execute, and how much
  geometry does each stage actually kill" answerable against a **product**
  build. No behaviour change, no target source touched.

- Sprint 2 T2.13 (gates): `verify-shadow-trig` and
  `verify-shadow-trig-mutation`, the error-bounded oracle for the shadow trig
  substitution, both wired into `verify-all`. **This is the first oracle in
  Sprint 2 that is deliberately not an identity oracle** -- replacing a
  double-precision polynomial with a 4096-step table changes the answer by
  construction, and asserting byte-identity here would be a lie. It instead
  (a) bounds the movement over all 65,536 s16 angles against the libultra
  polynomial copied verbatim from `lib/src/math/{sinf,cosf}.c` and fed through
  the same f32 rounding chain the caller applied, (b) names the blunders that
  must never ship -- quadrant error, sign flip, off-by-one table index, a flat
  floor that is not the identity -- so a failure reports which one happened
  rather than "a number got big", and (c) measures the largest single-angle
  step discontinuity, because a fixed small offset is acceptable here but
  geometry that pops between frames is not. Measured worst case
  **1.486145e-3 against a derived bound of 1.4871e-3** -- the derivation is
  confirmed to four significant figures. Four mutations, all killed; the
  tolerance mutation is deliberately tight (2.0e-5, landing at 1.506e-3, just
  past the 1.5e-3 bound) because a mutation that overshoots by an order of
  magnitude proves only that a comparison exists.

- Sprint 2 T2.13 (float, bit-exact): `saturn_geo_enter_held_object` builds its
  Q16 translation with an integer multiply. It was computing
  `node->translation[i] / 4.0f` on `s16` input and then converting the result
  straight back to Q16.16 with `sm64_saturn_float_to_q16` -- three
  `___floatsisf`, three `___divsf3` and three `___fixsfsi` per held object per
  frame, to compute a value that is exactly `n * 16384`. `n / 4` is
  representable in `f32` without loss (dividing by four only decrements the
  exponent) and its Q16.16 image is an integer, so the conversion's truncation
  has nothing to discard. **Bit-identical over all 65,536 s16 inputs**, proven
  exhaustively rather than argued. A multiply and not `<< 14`: the input is
  signed and left-shifting a negative value is undefined. Flagged by the
  arithmetic census as its stand-alone free win. Dynamic value measured
  separately, and it is small: `_saturn_geo_enter_held_object` did not appear
  in the sampled profile at all on the BOB route.
- Sprint 2 T2.13 (divide, bit-exact): `push_clamped_int`
  (`src/port/saturn/gfx/saturn_hud_layout.c`) uses a fixed decimal ladder.
  `value / divisor` with a *runtime* divisor is a call to `___sdivsi3`, GCC's
  32-step software divide, and the loop made `max_digits` of them per call
  across seven call sites per HUD build -- up to ~21 per frame, the highest
  confirmed per-frame divide count in the census. Every call site passes 2 or
  3, so a three-step ladder with constant divisors covers them, and GCC
  strength-reduces a constant divide to a `dmuls.l` reciprocal multiply. The
  general loop is **retained** for `max_digits > 3` rather than asserted away,
  so the function stays total over its declared domain and the ladder can be
  proven identical against it instead of merely believed. Measured value: also
  small -- `___sdivsi3` does not appear in the sampled dynamic profile at all,
  and the whole `divide` class is 0.12% of cycles, all of it `___udiv_qrnnd_16`
  reached from `___divsf3`.

- Sprint 2 T2.13 (float, **owner-visible**): the shadow path stops evaluating
  double-precision trigonometry. `calculate_vertex_xyz` (`src/game/shadow.c`)
  called `cosf` three times and `sinf` twice per shadow vertex, and both are
  the libultra five-term polynomial *in `double`* (`lib/src/math/sinf.c`) on a
  CPU with no FPU. Measured, that was essentially the whole of this build's
  soft-double cost: 26 of 28 sampled `___muldf3` entries, 18 of 19 `___adddf3`,
  15 of 15 `___subdf3` and 12 of 13 `___truncdfsf2` came from `_cosf`, and
  every sampled `_sinf` and `_cosf` entry came from `_calculate_vertex_xyz`.
  Soft-double plus its conversions plus the `sinf`/`cosf` bodies were **2.73%
  of all sampled SH-2 cycles and 9.2% of non-idle cycles**.
  Root cause of the waste: both angles are produced by `atan2_deg`, which is
  `atan2s` -- already an exact s16 binary angle -- scaled into degrees;
  `calculate_vertex_xyz` then scaled that back into radians and called `sinf`.
  The fix keeps the binary angle: `struct Shadow` gains `floorDownwardAngleBam`
  and `floorTiltBam` (Saturn only), and the Saturn arm indexes the engine's own
  `sins`/`coss` table, which is a pure `f32` load with no arithmetic at all.
  90 degrees is exactly 0x4000 BAM, so the tilt subtraction stays exact.
  **This is not bit-exact and the owner may be able to see it.** `sins`/`coss`
  discard the low 4 bits of the angle, so trig values move by up to
  1.486e-3 absolute (measured worst case over all 65,536 angles, against a
  derived bound of 1.4871e-3), which moves a shadow vertex by up to **0.147
  world units** at Mario's shadow scale of 100 -- about 0.04 screen pixels at a
  typical camera distance, and it cannot accumulate across frames because the
  substitution is memoryless. The quantisation
  is the *same* one every `mtxf_*` constructor in this engine already applies,
  and the same one Sega's own SGL documents for `slSin`/`slCos`, so the shadow
  is now consistent with the geometry it sits on rather than more precise than
  it. The non-Saturn build is untouched: the `sinf`/`cosf` arm is preserved
  verbatim under `#else`.

- Sprint 2 T2.13 (measurement): `tools/saturn/capture_softfloat_profile.py`,
  a cycle-attributed statistical profiler for the running target that needs no
  rebuild and no instrumentation. `ymir-headless`'s `exec.stepi` already
  returns `pc_before`, `pc_after` and `cycles_advanced` per executed
  instruction, which is a complete profile sample; the tool advances at full
  speed for a VBlank, single-steps a short burst, and repeats, on both SH-2s.
  It exists because the arithmetic census (`sprint2-arithmetic-census.md`) is
  *static reachability* and its 1,827 soft-float call sites say nothing about
  per-frame frequency -- and the dynamic ranking turns out to disagree with the
  static one sharply. `_find_wall_collisions_from_list`, the census's largest
  static float caller at 110 sites, is near the bottom of the measured profile;
  `_calculate_vertex_xyz` is the top one. Symbol attribution comes from
  `sh-elf-nm -S`, and entries are attributed to their caller from the
  `pc_before` of the transferring instruction. Also reports an explicit idle
  class, because `___slave_polling_entry` plus the master's VBlank wait are
  70% of raw cycles on this route and silently deflate every other share.

- Sprint 2 (gate repair): `verify-render-clusters` compiles again. Its
  recipe passed only `-I src/port/saturn/gfx`, but `1ec76248` gave
  `ztreme_hot_promotion.c` an include of
  `port/saturn/platform/saturn_cart_code.h`, which is `src`-relative --
  so the gate had been failing at the preprocessor since that commit and
  was verifying nothing. Adding `-I src` is load-bearing and was checked
  both ways: without it the compile is a fatal error, with it the seven
  generation tests and the C test both pass. Same class as the 22
  Makefile recipes still carrying the MSYS path defect T2.10 repaired in
  three of them; a green-by-accident gate is worse than a missing one,
  because it reports success.

- Sprint 2 T2.12 (coverage): `verify-scene-admission` gains negative cases for
  the five hierarchy checks `metadata_valid()` grew. Without them the checks
  were exercised only by valid trees, which is how a fail-closed guard becomes
  silently dead. Cases: a child index at or below its parent's, a child range
  past the node count, a leaf with a non-zero `child_first`, child bounds not
  contained in the parent's, and one node claimed by two parents — plus a
  positive case binding the fixture's three nodes as a hierarchy and asserting
  the admitted count does not move. Two cases had to be constructed so that
  the check under test is the *only* one that rejects them; as first written,
  containment rejected them first and the mutations survived.
  **Mutation result: four of the five checks killed.** The child-range check
  survives and cannot be killed from C — removing it makes the fixture read
  one node past the array, so what rejects the case is undefined-behaviour
  garbage rather than the guard. Test-only: no product code changes, so the
  measured build is unaffected.
- Sprint 2 T2.12 (`spatial_admit`, T2.9 item 4 half a): `emit_bob_scene.py`
  bakes a real spatial index. `SM64_SATURN_BOB_ADMISSION_NODE_COUNT` goes from
  **1 to 255** -- 128 leaves, depth 7 -- over the same 867 cluster refs.
  Before this, BOB's 'hierarchy' was a single node holding every cluster: a
  flat list with a tree's type signature, which is why T2.9 measured
  `clusters_tested` at 867 with min = max = 867 in every one of 1,330 frames
  while only 230-353 were admitted, and why the stage's cost did not fall when
  less was visible. **Shape:** median split on the widest axis of the node's
  bounds, leaves capped at 8 clusters, node bounds the exact union of the
  cluster bounds owned, node indices assigned breadth-first so every child
  index exceeds its parent's. Those three properties are exactly what
  `metadata_valid()` re-proves at bind time -- the baker is not trusted.
  **Leaf size 8 is measured, not chosen by taste:**
  `verify-admission-hierarchy` reports total frustum tests per pose for leaf
  sizes 1 through 128 over 964 poses x 5 frustum templates, and 8 is the
  minimum at 31.6% of the flat pass (36.0% at 4, 33.2% at 16, 67.8% at 1 where
  node tests swamp the saving). **Consumer impact:** the node array grows from
  36 to 9,180 bytes of `.cart_rodata` -- cartridge, not work RAM -- and no
  admitted cluster moves: the generated tree is compared against the flat pass
  over the whole corpus with zero divergences.
- Sprint 2 T2.12 (`spatial_admit`, T2.9 item 4 half b): admission traversal is
  hierarchical. `sm64_saturn_scene_admission_node_t` gains
  `child_first`/`child_count` in place of its `reserved` word -- children are a
  contiguous node range, `sizeof` is unchanged at 36 bytes -- and
  `sm64_saturn_scene_admit_with_scratch()` descends them carrying each node's
  frustum classification. A node inheriting INSIDE is not tested and neither
  are its clusters; a node testing OUTSIDE prunes its subtree; only INTERSECTS
  descends and re-tests. This is Z-Treme's `ztCheckBoxInFrustum` short-circuit
  (`ZT_RENDERING.c:425, 486-492`, recorded in
  `sprint2-t2_0-reference-sweep.md` 4.3) and the pattern already written in
  this repository at `saturn_demo_render.c:772-785`, adapted rather than
  reinvented. The inherited state is packed into the two spare high bits of
  the existing queue word, so no scratch grew.
  **Two things this required that are not obvious.**
  (1) *Pruning is not free of a correctness argument.* The classifier
  quantises each AABB to an integer centre/half-extent pair and then floors
  the projected centre and ceils the support radius. Both steps only ever
  widen a box, but they widen node and cluster independently, so a contained
  cluster's widened projection can poke past its node's -- the oracle found
  102 poses where the flat pass admitted a cluster whose node the hierarchy
  pruned, all within a couple of world units of the near plane. Node tests
  therefore widen the node by a derived 8-world-unit margin
  (`ADMISSION_NODE_MARGIN`); widening is conservative in both directions and
  cannot admit less. Removing it is killed by `verify-admission-hierarchy`.
  (2) *Output order had to stop depending on traversal shape.* The traversal
  now only marks, and one ordered pass emits in ascending cluster index,
  folding in the mandatory sweep. Without it every mandatory cluster in a
  pruned subtree would move from its place in the sequence to the tail, and
  admission order feeds the downstream depth-bin scatter's tie order. On the
  flat pass this is a no-op -- BOB's ref list is already ascending and the
  trailing sweep admitted nothing -- which is what makes the two forms
  byte-identical rather than merely set-equal. `verify-frustum-equivalence`
  reports the same admitted-set digest `0f70643abf026a13` as T2.10.
  **Fail-closed additions to `metadata_valid()`:** child ranges in bounds, a
  child index strictly above its parent's (so the descent terminates and the
  hierarchy is acyclic), child bounds contained in parent bounds (the property
  the prune and the short-circuit rest on), and a forest check -- no node is
  claimed by two parents and the root is claimed by none, without which the
  admitted set would depend on which edge the queue reached first.
  **Consumer impact:** any package publishing admission nodes must zero both
  new fields for a leaf; a non-zero `child_first` on a leaf is now malformed
  metadata, where it used to be a non-zero `reserved`.
- Sprint 2 T2.12 (equivalence oracle): `verify-admission-hierarchy`, a host
  gate that pins the admitted cluster index array of `spatial_admit` before
  the traversal is allowed to change shape. **Why it is needed:** T2.9 item 4
  proposes replacing a flat 867-cluster frustum pass with a hierarchical
  descent that prunes and short-circuits. That changes *which* tests run, and
  the only acceptable observable difference is none -- any move in the
  admitted set is a visual change. **How it works:** the pre-T2.12 flat
  traversal is pinned verbatim as
  `sm64_saturn_scene_admit_reference_with_scratch()` in
  `src/port/saturn/gfx/saturn_scene_admission.c`, compiled only under
  `SM64_SATURN_SCENE_ADMISSION_REFERENCE`, which only the new recipe defines
  (the in-tree convention T2.10 used for `ztreme_frustum.c`). The fixture
  `tools/saturn/admission_hierarchy_test.c` drives it and the shipped entry
  point over the **real generated BOB cluster bank** -- 867 clusters from
  `bob_scene.h`, not a synthetic stand-in -- across 964 camera poses x 5
  frustum limit templates = 4,820 comparisons, and requires the emitted index
  arrays to be byte-equal. The corpus deliberately includes the poses a
  hierarchy gets wrong: cameras inside the scene bounds, cameras far outside
  looking away, a near plane deep enough that cluster bounds straddle it, and
  limits that admit everything (867) and nothing but the mandatory floor
  (125). **Consumer impact:** none at runtime -- the reference body does not
  exist in a product object. Anyone changing admission traversal must now keep
  this gate green.

### Fixed

- Sprint 2 T2.11 (measurement): `summarize_cadence` in
  `tools/saturn/capture_sourceboot_throughput.py` no longer aborts with
  `ObservationError: phase VBlank crossings exceed the observed interval` on
  builds materially faster than ~15.5 VBlanks per frame. **This changes when a
  capture aborts; it does not change any FPS or VBlank figure the tool
  computes.** **Root cause:** `phase_delta()` required
  `simulation + construction + transport_presentation <= vblank_delta`, which
  asserts the accounted phases are wall-disjoint sub-windows of the interval.
  For a single-threaded master differencing one monotone VBlank counter that
  holds exactly -- but one boundary in the v2 cadence schema is not stamped by
  the master. `master_finalization` is `terminal_vblank - retirement_vblank`,
  and `retirement_vblank` is written from the **slave SH-2** at the instant the
  slave retires the job (`sourceboot_render_runtime_marker`, RETIRED arm,
  `src/port/saturn/sourceboot/main.c`). The master does not begin finalizing
  then; it is still inside the action it was dispatched to run, and
  `sm64_saturn_frame_pipeline_step()` admits the next source tick while the
  render is in flight. Every whole VBlank crossing between the slave's stamp and
  the master's first poll after it is therefore charged **twice** -- once to
  `master_finalization`, hence to `construction`, and once to `simulation`.
  **Why it surfaced now rather than earlier:** the overshoot is bounded by one
  crossing per affected interval, and the previous product build
  `id-6eca5970628d581d` had exactly enough idle time per frame to absorb it
  (`attributed == vblank_delta` in 25 of its 29 intervals, margin zero). T2.10
  removed 1.586 VBlanks per frame of that slack, so four of 29 intervals tipped
  one crossing past the interval and the whole summary was discarded.
  **Fix:** the double charge is a sub-window of both phases, so it is bounded
  above by `min(simulation, master_finalization)`, and the check is now
  `attributed - concurrent_allowance <= vblank_delta`. **The bound is read out
  of the same trace, not chosen:** it is exactly zero when no finalization
  window exists -- a v1 trace with no overlap fields, or a phase aborted before
  notification -- and it shrinks with either phase. It cannot excuse an
  overshoot in `transport_presentation`, whose boundaries are both master
  stamped. **Tradeoff:** the interval-level margin on the current build family
  widens from 0 to about 5 crossings of 15, because the cadence trace carries no
  clock finer than whole VBlank crossings with which to resolve the overlap
  exactly. **Consumer-facing impact:** `attributed_vblank_crossings` and
  `unattributed_vblank_crossings` keep their existing definitions, so
  `unattributed` may now be reported **negative** where it previously forced an
  abort; that is the honest raw margin and readers must not treat it as idle
  time. Intervals decoded from a v2 trace gain one new field,
  `concurrent_phase_allowance_vblank_crossings`; v1 intervals are unchanged.
  **Prerequisite removed:** every FPS measurement in the project was blocked on
  this, including T2.10's, whose cadence is recorded in
  `docs/saturn/evidence/reports/sprint2-t2_11-cadence-summarizer-fix.md`.
  **Owner follow-up, not fixed here:** stamping `retirement_vblank` from the
  master's observation of retirement, or adding an FRT field to a cadence trace
  v3, would remove the ambiguity instead of bounding it.

### Changed

- Sprint 2 T2.10 item 3 (perf): the frustum AABB classification derives its
  lateral limits by cross-multiplication instead of by division, and the result
  is **bit-identical**. **Root cause of the cost:** `scaled_limit()` was called
  four times per AABB test and each call launched an SH-2 64/32 hardware
  division. T2.9 Finding E counted **3,472 divisions per frame** and confirmed
  them in the linked image -- four `jsr` to `_scaled_limit`, which writes
  DVSR/DVDNTH/DVDNTL and reads DVCR at `0xFFFFFF00/08/10/14`. **Fix:** comparing
  `v` against `trunc(N/focal)` is comparing `v*focal` against `N`, exactly, when
  `N >= 0` and `focal > 0`; each comparison becomes one `dmuls.l` pair.
  **`N >= 0` is load-bearing, not decorative:** the divided form truncates
  toward zero, so for `N = -5`, `focal = 2`, `a = -2` the two forms disagree.
  The shipped guard therefore requires a positive focal length, non-negative
  half extents and depths, operands that fit `int32` so no product leaves
  `int64`, and each numerator at or below `INT32_MAX * focal` -- because the
  divided form *clamps* a limit that will not fit `int32`, and where that clamp
  is active the two forms genuinely differ. Outside the guard the divided form
  runs unchanged, which is what makes the change bit-identical **for every
  input**, not merely for inputs BOB produces. **Equivalence result:** 516,090
  classifier cases (269,968 in the cross-multiply domain, 246,122 outside it),
  **0 divergences against the pinned pre-T2.10 body and 0 against the exact
  128-bit model**; the cross-multiplied path was taken on **269,968 of 269,968**
  domain cases, so the shipped guard and the independently computed domain agree
  in both directions. Driving the real `sm64_saturn_scene_admit()` with the
  classifier switched underneath it over 1,024 camera poses and 29,109
  admissions produced **byte-equal cluster index arrays**. **Consumer-facing
  impact: none.** No admitted cluster moves, so no LOD tier and no painter bin
  moves, and there is no visual-risk item for the owner to adjudicate.
  **Tradeoff accepted:** the guard costs roughly sixteen 64-bit comparisons and
  one extra multiply per test, against four hardware divisions plus four call
  frames removed; the saving is therefore smaller than T2.9's ~0.55 VBlank
  estimate implied, which assumed the divisions were replaced by two multiplies
  and nothing else.

- Sprint 2 T2.10 item 2 (perf): scene admission validates package metadata once
  per binding instead of once per frame. **Root cause of the cost:**
  `metadata_valid()` was called unconditionally on entry and re-proved
  properties of arrays that are `static const` in a generated header. T2.9
  measured it at a flat **1,700-1,701 FRT ticks in every one of 1,330 frames**
  -- 217,614 cycles, 10.6% of `demo_spatial_admit()`, 0.485 VBlanks -- for
  ~3,470 loop iterations and ~19,400 cache-through cartridge reads. Nothing
  else in the port measures that constant. **Fix:** a bind-scoped memo keyed on
  every scalar and pointer the validator reads -- version, the valid byte, both
  reserved fields, all five array pointers, all five counts, and the root node.
  **Why the key is sufficient, and where it is not:** it covers every field the
  validator reads, but it cannot see *through* a pointer, so a content change
  behind an unchanged pointer is exactly what it would miss -- and the existing
  fixture does precisely that, mutating `nodes[0].cluster_ref_count` and
  expecting rejection. Rather than serve a stale verdict, the memo is gated on
  a new **opt-in** field, `sm64_saturn_scene_admission_view_t::metadata_immutable`,
  which is **fail-closed at zero**: a caller that does not set it revalidates
  every call, exactly as before. Sourceboot sets it because the arrays live in
  `.cart_rodata`, linked into the A-bus cartridge window -- read-only hardware,
  not merely `const`-qualified. **Coverage is mechanical, not a promise:** a
  `_Static_assert` requires the memo key struct to have the same size as the
  view's prefix up to `frustum`, so adding a field the validator can read
  without adding it to the key fails the build. **Rejection is never cached**,
  so a malformed package is re-validated and re-reported through `stats` on
  every attempt. **Consumer-facing impact: none** -- the equivalence fixture's
  admitted-set digest is unchanged at `a6be5aa07ddca26c` over 1,024 poses and
  28,461 admissions. **New prerequisite for package authors:** a view whose
  metadata lives in mutable storage must leave `metadata_immutable` at zero.

- Sprint 2 T2.10 item 1 (perf): scene admission's duplicate test is O(1)
  instead of a linear scan of the output list. **Root cause of the cost:**
  `output_has_cluster()` walked `output->cluster_indices[0 .. cluster_count)`
  once per cluster that survived the frustum test, so K admitted clusters cost
  K(K-1)/2 comparisons. T2.9 measured that at 39,903 comparisons per frame for
  K = 283 -- 27.7% of `demo_spatial_admit()`, 567,198 cycles -- plus a further
  6.6% (135,720 cycles) for the 96 scans the trailing mandatory sweep embedded,
  **and it found zero duplicates in 1,330 frames.** For BOB it cannot find one:
  `metadata_valid()` proves every cluster is referenced and
  `cluster_ref_count == cluster_count == 867` forces a bijection. **Fix:** test
  and set a byte in `s_admission_cluster_seen[]`, which was already allocated
  in the traversal scratch for `metadata_valid()`'s coverage sweep and dead for
  the rest of the call. Zero new memory. **Correction to T2.9's Finding D,
  which matters:** that array is *not* already clear when the traversal starts
  -- the coverage sweep leaves it marked `1` for every referenced cluster -- so
  this change adds an explicit `memset` of `scene->cluster_count` bytes in the
  admission path (867 bytes for BOB, roughly 360 cycles against the ~700,000
  removed). Putting the clear there rather than inside `metadata_valid()` is
  also what keeps the invariant true once that function is memoised.
  **Consumer-facing impact: none by construction** -- the emitted index
  *sequence* is unchanged, because the decision is identical and the append
  order is untouched. Witnessed: the equivalence fixture's admitted-set digest
  over 1,024 camera poses and 28,461 admissions is `a6be5aa07ddca26c` when
  built against HEAD's admission unit and `a6be5aa07ddca26c` when built against
  this one. On-target witness for the next diagnostic capture: `dedup_compares`
  falls from 39,903 to 0 while `output_count` holds. **New coverage:**
  `verify-scene-admission` now includes a scene where one cluster is reachable
  from two nodes, because neither the previous fixture nor BOB itself ever
  produced a duplicate, which left the duplicate branch untested.

### Added

- Sprint 2 T2.10 (test): an equivalence oracle for the frustum classification,
  landed **before** the arithmetic it guards. **Why now:** T2.9 Finding E
  identified four SH-2 hardware 64/32 divisions per AABB test (3,472 per
  frame) that a cross-multiply removes, and flagged it as the one remediation
  that can move the admitted cluster set. Pinning the current behaviour first
  is what makes that change reviewable instead of a leap. **What it is:**
  `tools/saturn/frustum_cross_multiply_test.c`, run by
  `make -f Makefile.saturn.mk verify-frustum-equivalence`, comparing three
  independent statements per case — the pinned pre-T2.10 divided body
  (compiled into `ztreme_frustum.c` only under
  `SM64_SATURN_ZTREME_FRUSTUM_REFERENCE`, which only this target defines),
  whatever `sm64_saturn_ztreme_frustum_aabb()` currently is, and an exact
  128-bit restatement written from the algebra that divides where the
  cross-multiplied form multiplies and never clamps. It also drives the real
  `sm64_saturn_scene_admit()` twice per camera pose with the classifier
  switched underneath it and requires the emitted cluster index arrays to be
  byte-equal, so the claim is about admitted **sets** rather than individual
  comparisons. **Result at this commit:** 487,418 classifier cases (270,095
  inside the cross-multiply domain, 217,323 outside it), 1,024 poses, 28,461
  cluster admissions, **0 divergences**. **Tradeoff accepted:** the pinned
  reference is a verbatim copy of the shipped body, so the two share a
  prologue; the oracle's independent content is the lateral algebra, and the
  copy is what covers the prologue. **Consumer-facing impact: none** —
  `ztreme_frustum.c` compiled without the macro is byte-identical to HEAD's
  object (`6cb04fcb…c0840`, 1,584 B, host `gcc -O2 -g0`).

- Sprint 2 T2.9 (diagnostics): `demo_spatial_admit()` is decomposed at loop
  resolution, answering the owner's question "does spatial_admit need to cost
  as much as it does?" with **no**. **Why this was needed:** T2.7 localised
  4.565 VBlanks/frame — 29% of the whole frame — to this single node and
  T2.8 confirmed the frame is CPU-bound, but the node was opaque, and the
  brief's per-unit arithmetic assumed a 1,183-node BSP descent that the
  production path does not perform. **What the measurement found:**
  `SM64_SATURN_BOB_ADMISSION_NODE_COUNT` is **1** — the production spatial
  index is a single node holding all 867 cluster references, so no
  hierarchical early-out is reachable at any granularity, and the BSP belongs
  to a fail-closed fallback that ran 0 times in 1,330 frames. 867 clusters are
  frustum-tested every frame (`clusters_tested` min = max = 867) while only
  230-353 are admitted, and the test bucket measures 8,978-9,154 ticks
  regardless. 27.7% of the stage is `output_has_cluster()`, a linear scan
  costing exactly `K(K-1)/2` = 39,903 comparisons per frame that has found
  **zero** duplicates; 10.6% is `metadata_valid()` revalidating `static const`
  package metadata at a flat 1,700-1,701 ticks; 6.6% is a trailing mandatory
  sweep that admits nothing. **71.8% of the stage is constant and 27.7% gets
  more expensive the more you can see — nothing in it falls when less is
  visible.** No soft-float and no software divide are on the path (T2.5's
  `___divdi3` pathology does not recur), but each AABB test performs four SH-2
  hardware 64/32 divisions — 3,472 per frame — for a projected limit a
  cross-multiply yields in two `dmuls.l`. **Consumer-facing impact: none** —
  the answer changes no default and no product code path. **Prerequisite for
  the follow-up task:** the ranked remediation list is generic, not a BOB
  bypass; items 1 and 2 (reuse the already-allocated
  `s_admission_cluster_seen` bitset for the duplicate test; memoise
  `metadata_valid()` on the view's identity) are worth ~918,000 cycles ≈ 2.04
  VBlanks/frame between them, cannot change a single admitted cluster, and are
  the recommended first commit. Defensible cost for the stage is 0.5-0.8
  VBlanks against the measured 4.565, anchored on `work_order`'s own measured
  880 cycles per cluster touched. Evidence:
  `docs/saturn/evidence/reports/sprint2-t2_9-spatial-admit-audit.md`.

### Fixed

- Sprint 2 T2.10 (build): `verify-scene-admission`, `verify-portal-windows`
  and `verify-ztreme-frustum` can run again. **Root cause:** both recipes compiled their fixture and then
  launched it through the Saturn venv Python with
  `subprocess.run([r'$(SATURN_REPO_ROOT)/...'])`. Under the repository's own
  MSYS2 GNU Make, `$(SATURN_REPO_ROOT)` is an MSYS path (`/d/Code/...`) that
  the *native* Windows `python.exe` cannot resolve, so every invocation died
  with `FileNotFoundError: [WinError 2]` **after** a clean compile — the
  fixtures themselves were always passing. **Fix:** run the built fixture
  directly through the recipe shell, which is the pattern the working sibling
  targets in this same file already use (`verify-terrain-depth-bins`,
  `verify-actor-meshlets`, `verify-terrain-clip`); MSYS `sh` resolves the MSYS
  path natively and the exit code still reaches Make. **Consumer-facing
  impact:** the two standing gates that cover `saturn_scene_admission.c` are
  green rather than red, which is a prerequisite for verifying any change to
  that module. **Not fixed here:** 23 other recipes in `Makefile.saturn.mk`
  still launch their fixture through the same `python -c subprocess.run`
  indirection and carry the same latent defect; they are out of scope for
  T2.10 and are flagged for a dedicated pass.

- Sprint 2 T2.8 (diagnostics): the VDP1 draw fence is now measurable, and the
  VDP2 HUD stops reporting a fiction. **Root cause, three separate defects:**
  (a) `sourceboot_vdp1_wait_ticks_accum` was declared, zeroed and read but
  never incremented; (b) `vdp1_terminal_fence_wait_ticks_last/_accum` were
  hard-assigned `0U` immediately after the fence and then printed on the HUD as
  `VDP1W`, so every reader saw a zero and concluded there was no VDP1 wait; and
  (c) the one live fence counter, `vdp1_overwrite_wait_ticks_last`, was a
  single `sourceboot_frt_delta()` span truncated to `uint16_t`, so at phi/128 a
  wait past ~18.8 VBlanks aliased silently. Diagnostic builds now accumulate
  one 16-bit FRT difference **per spin iteration** into a 32-bit total, which is
  exact for any wait length, and publish `vdp1_fence_max_raw` as the wrap
  witness. **Consumer-facing impact: none** — every addition is behind
  `SATURN_DIAGNOSTIC_MODE != 0`, and the product path is preserved verbatim in
  the `#else` arms. Proven at object level: `main.o`, `saturn_demo_render.o`,
  `saturn_actor_meshlets.o` and `saturn_render_job_runtime.o` compiled at
  `SATURN_DIAGNOSTIC_MODE=0` with `-g0` on the product build's own command line
  are **byte-identical** to the same objects built from a path-identical
  `git show HEAD:` mirror (660,904 / 723,168 / 16,500 / 5,356 B;
  `saturn_actor_meshlets.o` reproduces T2.6's recorded `f423caa8...50cdd`).
- Sprint 2 T2.8: an out-of-bounds write introduced and fixed within the same
  task. The new VBlank-OUT sampler read its ring cursor back from NOLOAD
  `.lwram_bss` and used it **unmasked** as an array index. `.lwram_bss` is not
  crt0-cleared and the handler is registered in `user_init()`, before
  `sourceboot_reset_lwram_state()` zeroes the record, so on any boot where
  LWRAM is not already zero (real hardware; Ymir happens to zero-fill) that
  wrote past the ring into neighbouring LWRAM state. The cursor is now masked
  on read as well as on write.

### Changed

- Sprint 2 T2.9 (diagnostics): `sm64_saturn_prenotify_profile_t` ABI version
  3 -> 4, 716 -> 860 bytes (179 -> 215 words). **The node table is deliberately
  UNCHANGED at 24 entries and ids 0-23 keep their T2.4-T2.8 meaning**, so every
  earlier ranked table stays directly comparable; the 36 new words are flat FRT
  sub-spans that subdivide the `spatial_admit` node without participating in the
  node stack. **Why flat spans rather than node-tree children:** the admission
  cluster loop runs 867 times per frame, a `push`/`pop` pair costs ~100 SH-2
  cycles through the `__uncached` working state, and three pairs per iteration
  would have added ~13% to the very stage under measurement — the T2.5
  per-vertex mistake repeated. One `uint16_t` cursor threads the whole call, so
  the named buckets sum to the whole by construction (measured residue: -0.0
  ticks). **Consumers must update together:** any reader of this record must
  move to `PROFILE_VERSION = 4` / `PROFILE_WORDS = 215`;
  `tools/saturn/capture_prenotification_profile.py` is updated in the same
  commit and gains a `spatial_admit` summary block carrying the ranked
  sub-stage table, the count table and the per-unit derivations. **Perturbation,
  measured rather than estimated:** ~50 cycles per span against a predicted ~14
  (the prediction was wrong by 3.5x and is corrected rather than quietly
  adjusted); +3.52% on the stage, +0.22% on the frame against T2.8's build by
  `summarize_cadence`. Unprobed control nodes reproduce T2.6/T2.8 to within
  0.44%.

- Sprint 2 T2.8 (diagnostics): `sm64_saturn_prenotify_profile_t` ABI version
  2 -> 3, 452 -> 716 bytes (113 -> 179 words). The T2.5/T2.6 FRT rig is
  **reused** rather than duplicated: the present path and the VDP1 draw fence
  publish into the same master-owned NOLOAD LWRAM record, through the same P2
  cache-through alias, decoded by the same capture tool. New fields cover the
  fence (ticks/iterations/max-raw), `EDSR` and `COPR`/`LOPR` on both sides of
  it, a 32-entry per-VBlank `COPR` progress ring plus an `EDSR.CEF` hit count
  sampled in the VBlank-OUT handler, the present path decomposed
  (`vdp1_sync_render`, `vdp1_sync`, VDP2 commit, total), and the actor/texture
  vs total command split that the profile already computed and no instrument
  ever published. **Prerequisite for consumers:**
  `tools/saturn/capture_prenotification_profile.py` must be at
  `PROFILE_VERSION = 3` / `PROFILE_WORDS = 179` to decode a T2.8-or-later
  image; it is updated in the same commit and emits a new `present` summary
  block. None of the new fields participates in the profiler's node stack, so
  T2.4/T2.5/T2.6 node numbers are unperturbed.


- Sprint 2 T2.6 follow-up: T2.6's own doc comments broke
  `tools/saturn/test_render_snapshot_source.py`. **Root cause:** its
  `function_body(text, name)` helper locates a C function with
  `text.index(f"{name}(")` and takes the first `{` after that offset, so *any*
  earlier mention of the name followed by a parenthesis wins — including one
  inside a comment. T2.6's comments referred to `actor_meshlet_core()` and
  `sm64_saturn_actor_meshlets_prepare()` with call parentheses above their
  definitions, so the extractor read the new depth-carry typedef instead of the
  function body and `test_opaque_actor_meshlets_use_depth_bins` failed against
  correct code. The comments are reworded and the constraint is recorded in
  `src/port/saturn/gfx/saturn_actor_meshlets.c`. The suite now aborts at
  `test_vdp1_painter_chain_uses_all_existing_master_depth_tags`, the
  pre-existing failure T2.1–T2.5 all recorded. **Consumer-facing impact: none**
  — comment text only, and proven so: the translation unit compiled at
  `SATURN_DIAGNOSTIC_MODE=0` with `-g0` on the product build's own command line
  is **byte-identical** before and after (16,500 B, `f423caa8…50cdd`), so the
  T2.6 measurements describe this tree. Rewording comments is a workaround; the
  extractor's fragility is filed separately.

### Changed

- Sprint 2 T2.6 step 2 (performance): the actor depth walk **no longer
  performs a 64-bit software division per multiply**
  (`src/port/saturn/gfx/saturn_actor_meshlets.c`). **Root cause:** every
  per-vertex product went through `actor_saturating_mul_i64()`, which checks
  overflow by *dividing* — the SH-2 has no 64-bit divide, so each call emitted
  a libgcc `___divdi3`. T2.5 counted 10 per position visit, 1,408 visits per
  frame, **~14,080 software divisions per frame**, and identified
  `actor_saturating_mul_i64` as the only 64-bit-division caller on any hot
  path in the entire linked image. Three of the ten were literally a multiply
  by 2^16. On top of that the loop recomputed per vertex what is per-actor
  algebra, and recomputed the yaw sine/cosine once per meshlet. **Fix:** a
  per-actor `actor_depth_kernel_t` prepared once per
  `actor_meshlet_core()` call, holding the trig and the constant
  `SUM_a ((P_a - C_a) * F_a >> 16)`; the per-vertex body then collapses to four
  `dmuls.l` for the rotation and three for the dot product, with no division,
  no 64-bit multiply and no libgcc call. **Why it is exact, not approximate:**
  `q_a * F_a` is an integer, so it pulls straight out of the per-axis floor —
  `((B_a + (q_a << 16)) * F_a) >> 16 == ((B_a * F_a) >> 16) + q_a * F_a` — and
  under unit scale the reference's two `>>16` narrowings compose into one
  `>>32`, letting the 2^16 factor out of the numerator. **New prerequisite:**
  the rewrite is only exact while no saturating helper would have saturated,
  so it is gated on per-actor preconditions (unit scale, `|position_q16| <=
  2^40`, `|view_forward_q16| <= 2^20`, `|sin|,|cos| <= 2^16`) that bound every
  intermediate inside `int64`. Outside them the pre-T2.6 `actor_depth_reference()`
  still runs — which is why it was retained rather than deleted; the generic
  bank path admits arbitrary per-axis scales and does not qualify.
  **Equivalence result: bit-identical.** The T2.6 oracle sweeps 685,456 cases,
  268,816 through the new kernel and 416,640 through the saturating fallback,
  with **zero divergences** and zero LOD-tier or painter-bin changes; the
  file's pinned Mario output hashes are unchanged. Mutation-verified with six
  kills (precondition dropped, rotation shift, `base_depth` shift, dot-product
  axes swapped, sine/cosine swapped, `rotated_z` sign) against a surviving
  no-op control.

- Sprint 2 T2.6 step 1 (performance): `actor_meshlet_core()`'s **emission pass
  no longer recomputes the depth bounds** the admission pass already produced
  (`src/port/saturn/gfx/saturn_actor_meshlets.c`). **Root cause:** the two
  passes walk the same 31 meshlets over the same `const` inputs, and pass 2
  called `actor_meshlet_live_depth_bounds()` again for every one of them,
  discarding the result with a `(void)` cast and re-deriving identical
  `depth_bounds` and `span`. T2.5 measured the two walks at 20,491.7 and
  20,491.9 FRT ticks — 0.001% apart, which is what identical work looks like —
  worth **5.72 VBlanks/frame, 10.4% of the frame**. **Fix:** a 524-byte
  master-only `static` carry (`ACTOR_MESHLET_DEPTH_CARRY_CAPACITY` 64 entries,
  HWRAM `.bss`, `_Static_assert`-ed against `SM64_MARIO_MESHLET_COUNT`) holds
  pass 1's bounds for pass 2. **Tradeoff and why it is safe:** this is a
  memoisation of provably identical inputs, not a numeric change — `source`,
  `transform` and `view` are `const` parameters neither pass writes, and the
  only state mutated between the passes (bin cursors, the `position_seen`
  re-clear) does not alias the pose vertices or the geometry tables. A
  generation/meshlet-count/pose-pointer stamp is checked before serving, and a
  mismatch recomputes rather than serving stale bounds. **Deliberately scoped
  to the Mario entry point:** the bank entry point is dispatched on either
  SH-2 (`saturn_demo_render.c:3129` selects a workspace lane from the claim),
  so a shared static would be a cross-CPU race; it passes NULL and keeps the
  two-walk behaviour. It is not on the measured hot path. Pinned by a new host
  test that requires the carried emission to be byte-identical to a forced
  fresh recompute across 4,000 randomised cases spanning all three pose banks,
  four output capacities and z biased across the LOD-tier thresholds;
  mutation-verified with three kills (carry index pinned to 0, off-by-one,
  stored bounds swapped). The file's pre-existing pinned output hashes are
  unchanged.

### Added

- Sprint 2 T2.6 (oracle, landed before any behaviour change): a **host
  equivalence harness for the actor depth arithmetic**, in
  `tools/saturn/actor_meshlet_test.c` with a test-only probe in
  `src/port/saturn/gfx/saturn_actor_meshlets.c` behind
  `SM64_SATURN_ACTOR_MESHLET_DEPTH_REFERENCE` (defined only by
  `verify-actor-meshlets`, so no Saturn image carries it). **Why:** T2.5
  named `actor_meshlet_live_depth_bounds()` as 20.7% of the frame and
  `actor_saturating_mul_i64()`'s divide-based overflow check as the
  mechanism. Replacing that arithmetic can change numbers, and the numbers
  feed `actor_lod_tier()` and `actor_depth_bin()` — i.e. what is drawn — so
  the contract is pinned before the swap, the same sequencing T2.3 used for
  the painter chain. The per-vertex body was lifted verbatim into
  `actor_depth_reference()`; **this commit changes no arithmetic**, which the
  file's pre-existing pinned output hashes confirm. Three independent
  statements are cross-checked per case: the reference, whatever the file
  currently ships, and a model written here from the algebraic identity
  `depth(v) = SUM_a floor((P_a - C_a)*F_a / 2^16) + SUM_a q_a*F_a` — the
  floor pull-out that any per-actor hoist depends on, evaluated on both sides
  so the model constrains the identity rather than restating it. **685,456
  cases**, 268,816 of them inside the hoist domain (real Mario pose vertices,
  BOB-scale coordinates) and the rest deliberately outside it (INT64/INT32
  extremes both signs, zero and negative scales, saturating positions —
  exactly what the divide-based check existed to handle). Divergences are
  reported in LOD-tier and painter-bin units, not just raw Q16 deltas.
  Mutation-verified non-vacuous: perturbing the reference shift, the model's
  quantisation shift, and the model's floor pull-out each kill the harness.
  **Consumer-facing impact: none** — test-only code plus a refactor with
  identical behaviour.

- Sprint 2 T2.5 (instrumentation): the T2.4 FRT profiler now **decomposes
  `demo_prepare_mario()`**, the block T2.4 measured at 69.24% of the
  pre-notification window and ~21% of the whole frame and then left opaque.
  Eight sub-nodes were added under it —
  `src/port/saturn/runtime/saturn_prenotify_profile.h` (node table 16 -> 24,
  record ABI 288 -> 452 bytes, version 1 -> 2), probes in
  `src/port/saturn/gfx/saturn_demo_render.c` and, newly instrumented,
  `src/port/saturn/gfx/saturn_actor_meshlets.c`, plus the decoder and the
  derived per-vertex arithmetic in
  `tools/saturn/capture_prenotification_profile.py`. **Why the probes stop
  where they do:** the two depth-bounds nodes are pushed once per *meshlet*
  (31 per pass), never per vertex. An FRT read plus its charge arithmetic is
  tens of cycles and `actor_meshlet_live_depth_bounds()` makes 704 tier-0
  position visits per pass, so a per-vertex probe would have measured mostly
  itself at exactly the granularity where the answer lives. Per-vertex cost
  is instead derived by dividing by that 704, which is sound because it is a
  compile-time property of `sm64_mario_meshlet_lod_position_offsets` and not
  a runtime quantity — the function walks every meshlet's whole tier-0 span
  before any cull test or LOD choice can shorten it. A new
  `node_calls_last[]` field makes the per-call count a measurement rather
  than a source-reading assumption (it reads 31 on target, as predicted).
  **Consumer-facing impact: none.** The gating is unchanged
  (`SATURN_DIAGNOSTIC_MODE != 0 && defined(__sh__)`), and all three modified
  translation units were proven **byte-identical** at
  `SATURN_DIAGNOSTIC_MODE=0` — stronger than T2.4, which had to except two
  `assert` `__LINE__` literals. Diagnostic-build cost: 492 B of HWRAM and
  160 B of LWRAM, with `verify-memory-map` still OK at 11,756 B of slack.

- Sprint 2 T2.4 (instrumentation): a **diagnostic-gated FRT sub-stage
  profiler** for the pre-notification window —
  `src/port/saturn/runtime/saturn_prenotify_profile.h` (new), its state and
  NOLOAD `.lwram_bss` record in `src/port/saturn/sourceboot/main.c`, stage
  probes in `src/port/saturn/gfx/saturn_demo_render.c`, a slave-busy scope
  in `src/port/saturn/gfx/saturn_render_job_runtime.c`, and the host
  harness `tools/saturn/capture_prenotification_profile.py`. **Why:** T2.3
  left the pre-notification window (dispatch → slave NOTIFIED marker) as
  the largest block in the frame at 18.92 of the 55.22 VBlanks/frame, and
  the existing cadence rig counts whole VBlank crossings, so it is
  structurally incapable of decomposing a block measured in VBlanks. The
  new instrument reads the SH-2 free-running timer directly, which is the
  shape T2.0's lesson **L14** extracted from SlaveDriver's `PROFILE.C`
  (fixed node table, zero allocation, nestable push/pop that charges the
  elapsed interval to the node on top of the stack at every transition).
  **Divergences from the reference, and why:** nodes are keyed by a
  compile-time id rather than SlaveDriver's runtime string-pointer tree,
  because the nesting here is static; and the internal clock is selected
  as φ/128 rather than SlaveDriver's φ/32, because a 16-bit FRT at φ/32
  wraps every ~4.7 VBlanks while sub-stages here can plausibly reach ten.
  Totals are extended to 32 bits by summing `(uint16_t)(now - last)` per
  probe and re-seeding at every window begin, and the largest single
  inter-probe interval is published as the wrap-safety witness — a value
  near 0xFFFF invalidates the totals, and the harness gates on it.
  **Consumer-facing impact: none.** Every declaration that can emit code
  or data sits behind `SATURN_DIAGNOSTIC_MODE != 0 && defined(__sh__)`,
  mirroring T2.1's `saturn_peak_probe.h`; a `SATURN_DIAGNOSTIC_MODE=0`
  translation unit differs from its predecessor only by an enum, a
  typedef, and a `_Static_assert`, none of which emit a byte. **New
  prerequisite for readers of the diagnostic build's other FRT
  telemetry:** `sim_frt_ticks_*` / `render_frt_ticks_*` /
  `dma_wait_ticks_*` are φ/128 units in a diagnostic build and φ/8 units
  in a product build, because the profiler retunes the shared FRT divider.
  Those fields are telemetry only — nothing in the port makes a decision
  from an FRT count — but the unit change is real and is why the published
  record carries the read-back TCR. Mode 2, not mode 1: mode 1 also
  compiles the animation sweep, whose Mario-animation override would
  perturb the route being measured (T2.1's established choice).

- Sprint 2 T2.3 (step 1 of 2): a painter-chain **equivalence harness** in
  `tools/saturn/vdp1_painter_chain_test.c`, and a host-only copy of the
  current per-bin-rescan relink as
  `sm64_saturn_vdp1_backend_link_depth_bins_reference()` behind
  `SM64_SATURN_VDP1_BACKEND_LINK_REFERENCE` (defined only by that test, so
  no Saturn image carries it — the same arrangement
  `saturn_terrain_depth_bins.h` already uses for its predecessor merge
  oracle). Why: T2.3 replaces the relink algorithm, and the replacement's
  binding requirement is that it emit a **byte-identical** painter chain,
  which needs an oracle committed before the swap. The harness cross-checks
  three independent statements of the contract — the reference
  implementation, the shipped implementation, and a from-first-principles
  model that stable-sorts draw indices by descending bin and derives the
  links that ordering implies — and asserts byte identity of the entire
  command array, so any divergence in link value, link type, END/tail
  handling, or an untouched field fails. Sixteen cases cover the awkward
  inputs: empty draw range, a single command at either end of the bin
  table, all commands in one bin (nearest, farthest, interior), bins
  occupied only at both extremes and interleaved, one command per bin in
  both tag orders, sparse bins with ties, pseudo-random mixtures at 500 /
  653 (T2.1's measured peak) / 1,661 commands, the arena's full 1664-entry
  capacity in four tag patterns, narrower 8-bin and 1-bin tables, and
  invalid-tag atomicity at four positions. The model is mutation-verified
  non-vacuous: inverting its bin direction makes the harness fail.

- Sprint 2 T2.2 evidence: the reclamation package + un-split measured end
  to end on candidate `id-6b7c7e5d5f71e809`. **Memory objective met,
  cadence objective not met** — 67,584 B of HWRAM recovered and the full
  54,080 B hot working set returned to 32-bit HWRAM (true slack over the
  0x1F00 floor 472 B → 13,944 B, `verify-memory-map` RESULT OK), but
  sustained cadence is **1.068 FPS against the R1 baseline's comparable
  1.071 — a −0.24% change, i.e. no material difference**, with every
  phase unchanged (construction 24.72 VBlanks/frame, master finalization
  5.80, dropped credits 14.42). No regression either: queue clean
  (0 master/slave failures), zero SH-2 exceptions, identity matched on
  target at startup attempt 681.
  Why it matters: returning the workarea + actor scratch to HWRAM is
  **not** sufficient to recover cadence, so the sprint's remaining budget
  belongs to the algorithmic levers (construction is 44% of a 56-VBlank
  frame — T2.3's counting sort), not to further memory-tier work. The
  T1 hypothesis is narrowed rather than refuted: `_sourceboot_fast3d`
  (44,616 B, per-frame hot) is still in LWRAM, and T1 measured full A9A
  hot-set residency at ~98,192 B, so "hot set in 16-bit LWRAM" has not
  been tested end to end. Owner look-and-listen on the capacity cuts
  remains open. Evidence:
  `docs/saturn/evidence/reports/sprint2-t2_2-reclaim-unsplit.md`
  (+ `sprint2-t2_2-throughput.json`).

### Fixed

- Sprint 2 T2.5: the three instrument defects T2.4 recorded against itself
  in its own evidence (`sprint2-t2_4-prenotification-profile.md` section 7).
  1. **Two published fields were measuring nothing**, and are removed rather
     than re-derived: `notify_to_retire` and `finalize_ticks` bracketed
     intervals whose start was stamped on the master SH-2 and whose end was
     stamped on the slave — `runtime_publish_retirement_marker()` is called
     from `render_job_slave_entry`, and the FRT is a per-CPU on-chip block,
     so the subtraction differenced two unrelated free-running counters.
     Root cause: the marker-observer callback's CPU affinity was assumed
     rather than traced. The cadence rig already reports both intervals
     correctly in VBlank crossings, so nothing is lost; `slave_busy` is
     unaffected because it is begun and ended on the same CPU, and it
     reproduces T2.4's 11,043 ticks exactly.
  2. **The slave no longer writes master-owned cached state.** Removing the
     retirement/terminal marks removes the only slave writes to
     `g_sm64_saturn_prenotify_profile_state` (T2.4's three bytes:
     `retire16`, `notified`, `retired`, all three members deleted), and the
     object additionally now carries `__uncached` — the discipline the
     shipped cadence rig already applies to
     `sourceboot_render_overlap_phase`. Verified in the linked image: the
     symbol resolves to `0x260FA8E4`, i.e. the `.uncached` section through
     the SH-2 P2 alias rather than cached `.bss`. T2.4 had to argue from
     consistency that its window measurement was uncorrupted; that argument
     is no longer load-bearing. Consumer-facing cost: each probe now does
     uncached HWRAM accesses, raising total perturbation from 0.013% to a
     measured 0.208% of the window — cross-checked against the unprobed
     `spatial_admit` stage, which reproduces across two builds to five
     significant figures.
  3. **The acceptance gate now means something.** `end()` counted the
     deliberately still-pushed NOTIFY node as a stack fault, so
     `faults == windows` by construction and
     `capture_prenotification_profile.py` exited 1 on a completely healthy
     798-window run. The closing depth is now published as `end_depth_max`
     (1 is the design: `end()` is reached from inside the notify call, so
     there is no instant at which NOTIFY could have been popped first) and
     only a real imbalance — depth > 1 — counts as a fault. A new
     `profiler_stack_balanced` check gates on the published depth directly.
     The capture now exits 0 with all twelve checks passing,
     `faults = 0` and `end_depth_max = 1`.
  T2.4's fourth recorded defect — 17% FRT wrap headroom, where the binding
  interval *was* `prepare_mario` itself — is resolved by construction:
  subdividing that stage shortens the longest inter-probe interval, and
  `max_raw_interval` fell from 54,192 to 18,591 (71.6% headroom), now equal
  to `spatial_admit`'s maximum to the tick.


- Two stale in-tree comments corrected (comment-only, no behavior change):
  `sourceboot-cart.x` claimed the VDP1 command banks "total 0x20000 bytes"
  (now 0x1A000 after the T2.2 capacity cut), and
  `saturn_fast3d_frontend.h` claimed "Task 10's VDP1 command list lives in
  LWRAM, not HWRAM, so it doesn't compete with this budget" — false since
  Task 14 moved the banks to HWRAM, and a direct contradiction of T1's
  central finding that they are the largest single HWRAM tenant. Both
  would have misled the next capacity decision.

### Changed

- Sprint 2 T2.3: the VDP1 painter relink
  (`sm64_saturn_vdp1_backend_link_depth_bins`) is now a **counting sort with
  intrusive per-bin chains** instead of one full rescan of the live command
  range per depth bin. Root cause of the waste: the predecessor overloaded
  `cmd_link` as both the sort key and the output link, so it could not read a
  command's bin after linking it and had to re-walk all ~N 32-byte records
  once for each of the 64 bins — `N * (2 + bin_count)` record visits, every
  one of them a 32-byte stride through a ~21 KB array that does not fit the
  SH7604's 4 KB cache. The fix follows T2.0's L7/L8: separate the sort from
  the link write the way SGL does (sort metadata out of band, bucket once,
  drain once — `SGLFAQ_F.TXT:1057-1112`), reusing the stable-scatter shape
  already proven in-tree at `saturn_terrain_depth_bins.h`. Three passes now:
  validate (no writes, preserving the invalid-tag atomicity contract),
  scatter backwards through the range prepending each command onto its bin's
  chain through `cmd_link` itself (SGL's intrusive `NEXT`, so no second
  command-sized buffer — working memory is one 128-byte stack table of chain
  heads), then drain far-to-near writing each JUMP_ASSIGN once. Cost falls to
  `N * 3 + bin_count * 2`: **2,078 record visits against 42,900** at T2.1's
  measured peak (653 published commands = 650 drawable; 20.6x), 5,111
  against 109,626 at the arena's 1,661-command capacity. The predecessor's separate link-type strip pass is
  gone because `vdp1_cmdt_jump_assign()` already clears that field and every
  live command is now assigned exactly once. Ordering is unchanged and pinned
  byte-for-byte by the equivalence harness above — far-to-near by descending
  bin, stable producer order within a bin, tail to END, empty range reverting
  the prefix to JUMP_NEXT/0 — and the three required mutations were verified
  KILLED by the harness alone (within-bin order reversed, bin direction
  inverted, END/tail link dropped). One deliberate precondition narrowing:
  `bin_count` above `SM64_SATURN_VDP1_BACKEND_MAX_DEPTH_BINS` (64) now fails
  closed rather than being ordered, because the chain-head table is fixed;
  `saturn_demo_render.c` static-asserts that its
  `SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT` stays inside that bound, and the bin
  count and key derivation are otherwise untouched (T2.0 L9) so the output
  stays directly comparable.

- Sprint 2 T2.2 un-split: the renderer's full 54,080 B hot working set is
  back in 32-bit HWRAM `.bss`, reverting commit `49370e31`'s placement —
  `DEMO_ACTOR_WORK_CACHE` is an empty macro again (the five actor-lane
  arrays, 10,304 B, kept as a distinct placement class so the sets stay
  independently steerable) and `s_bob_hot_workarea` (43,776 B) is plain
  `aligned(16)` with no section attribute. Why: T1 attributed the ~1.1 FPS
  cadence to hot per-frame state reading 16-bit LWRAM (the A9A baseline
  ran the same set in HWRAM at 5.29 FPS), and T2.0's reference sweep
  corroborates the shape — L2: neither SlaveDriver nor Z-Treme places any
  per-frame working set in LWRAM; Z-Treme's loader function is literally
  named "move the vertices to high work ram" (ZT_LOADING.c:320-353). The
  companion capacity-shrink entry below funds the return (67,584 B
  recovered vs 54,080 B spent, ~13.5 KB to margins).
  `test_dual_sh2_work_storage_contract.py` retargeted to pin the new
  all-HWRAM policy at the same guard strength (4 tests; the three
  placement mutations — workarea re-evicted, actor macro re-evicted,
  terrain macro re-evicted — each verified KILLED). Explicitly deferred:
  `_sourceboot_fast3d` (44,616 B, per-frame hot, `main.c`) stays in LWRAM
  — the arithmetic does not close for it; next rung via T2.0 L3's
  build-in-VRAM staging-window lever (~120 KB), recorded in the T2.2
  evidence. FPS impact measured at this task's capture gate; the owner
  look-and-listen remains the accepting gate.

- Sprint 2 T2.2 capacity shrinks — the peak-cleared reclamation package,
  recovering 67,584 B of committed HWRAM to fund returning the renderer's
  hot working set (see the companion un-split entry). Every member is
  gated on T2.1's instrumented peaks (`sprint2-t2_1-peak-capture.md`),
  not on estimates, and each is independently revertable:
  - `SOURCEBOOT_VDP1_COMMAND_CAPACITY` 2048 → 1664 (−24,576 B from the
    double-buffered command staging). Measured run-long bank peak: 653
    commands including the 3 setup commands — 1,011 headroom. Reference
    practice brackets the value (T2.0 L4: SlaveDriver ships 1,540,
    Z-Treme reserves 1,052, stock SGL 1,569). Safety preconditions
    verified in-tree before cutting (T2.0 L5/L6): the command arena
    clamps (reserve fails clean, END slot preserved, counted via
    `reject_vdp1_arena_capacity`) and exhaustion drops the FAR head of
    the painter stream with Mario's batch tail-reserved
    (`demo_render_finalize`'s `budget_before_tail` selection, citing
    ZT_RENDERING.c:494-503). The VDP1 VRAM partition arithmetic follows
    the macro, so the VRAM command region shrinks in step. The orphaned
    Task-14 contract `test_vdp1_staging_relocation.py` was retargeted to
    1664 and repaired: it had been failing at base HEAD `a90f1628` on the
    deliberate `.sourceboot_vdp1_cmdts` own-section attribute its old
    no-section assert predated (pre-existing failure, verified by stash).
  - `GFX_POOL_SIZE` 6400 → 4096 (−18,432 B). Measured bottom-up DL peak:
    443 entries (9.2x headroom) — the Saturn path routes geometry through
    its own IR, so the master DL carries scaffolding and HUD. Honestly
    recorded caveat: the pool is two-sided and the top-down
    `alloc_display_list` high-water was NOT separately measured by T2.1;
    the new L5 overflow guard (see Fixed) converts any exhaustion into a
    detected dropped-submission degrade rather than corruption.
  - libyaul `_private_pool` 0xA000 → 0x4000 (−24,576 B). Measured
    historical extent 8,276 B from pool base, boot-time-only, zero churn
    over 24,000 frames; 8,108 B margin in the 0x4000 pool. Mechanism —
    the dependency is READ-ONLY and its `TLSF_POOL_PRIVATE_SIZE` is a
    bare `#define` (verified not user-configurable): a build-time-staged
    patched copy of the one MIT translation unit
    (`tools/patches/libyaul-private-pool-0x4000.patch`, applied by the
    sourceboot Makefile from the pinned submodule commit's canonical-LF
    blob via `git show`, triple-SHA-256-pinned, atomic staging) is
    compiled as a port object ahead of `-lyaul`, so the linker never
    pulls the archive's `mm/internal.o` member — the same
    supersede-by-link-order mechanism as `libsm64softfp`. The staged TU
    carries `-DMALLOC_IMPL_TLSF` inside the patch so user-pool behavior
    stays identical to the installed `libyaul.a`. `third_party/` is not
    modified; `tools/patches/**` is checked out byte-for-byte
    (`.gitattributes -text`); THIRD_PARTY_LICENSES.md records the copy.
  - SMPC peripheral pool 14 → 4 (the T2.1 NEEDS-MARGIN reserve, ≈5,320 B):
    **deliberately skipped.** It is not reachable through the same
    configuration surface — it would require superseding libyaul's entire
    413-line `smpc_peripheral.c` driver TU (vs the 182-line self-contained
    allocator shim), and the arithmetic closes without it (67,584 B
    recovered vs 54,080 B + margin required). It remains the ranked
    reserve if a future rung needs it.

### Fixed

- `verify_sourceboot_memory_map.py`'s `VDP1_COMMAND_BANK_BYTES` tracks the
  T2.2 capacity cut (`2 * 2048 * 32` -> `2 * 1664 * 32`). The margin gate
  pins the VDP1 transport bank's *exact* extent, so it fails closed on any
  capacity change until updated — it did exactly that on the first T2.2
  gate run (`RESULT = FAIL: ELF command banks are not the exact aligned
  HWRAM range`), which is the gate working as designed. The constant and
  `SOURCEBOOT_VDP1_COMMAND_CAPACITY` must move together; both the stale
  2048 value and an off-by-one were verified to still fail the gate.
  Revert note: reverting the capacity commit requires reverting this too.

- Master display-list pool overflow now degrades instead of corrupting
  (Sprint 2 T2.2, T2.0 lesson L5 prerequisite for the `GFX_POOL_SIZE`
  shrink). Root cause: stock SM64's `gGfxPool` is a two-sided shared arena
  — `gDisplayListHead++` grows up with no bounds check while
  `alloc_display_list()` carves matrices/viewports down from `gGfxPoolEnd`
  and returns an unchecked NULL on exhaustion. On overflow the head writes
  cross into live top-down allocations (garbage matrices), then past the
  pool object into `spTask`/adjacent `.bss` (memory corruption); NULL
  allocations are dereferenced by ~90 call sites. Every reference engine
  clamps this class (SlaveDriver `SPR.C:142-143,430-441`; SGL halts —
  T2.0 L5), so shrinking capacity without a clamp would convert a capacity
  cut into a correctness risk. Fix: `gGfxPoolOverrun` latches on either
  failure side (refused `alloc_display_list`, or head/end crossing checked
  at the presentation boundary); `display_and_vsync()` then skips only that
  frame's `exec_display_list` — the previously complete VDP1 frame stays
  presented and the VBlank wait keeps pacing (constitution: degrade at the
  smallest safe unit). `gGfxPoolOverrunFrames` (u32, nm-locatable) counts
  dropped frames for host probes. Limitation recorded honestly: detection
  is at frame end, so a pathological single-frame overrun larger than the
  remaining pool runway can still write past the pool object before being
  caught; the guard converts the realistic incremental-growth class into a
  detected, presented-frame-preserving degrade.

### Added

- Sprint 2 T2.1 evidence: instrumented peak capture over the scripted BOB
  route (three reproducible runs, identical peaks) with a per-candidate
  shrink verdict table — cmdt capacity 2048→1664 SAFE (observed bank peak
  653), libyaul `_private_pool` 0xA000→0x4000 SAFE (historical extent
  8,276 B, boot-time allocations only), `GFX_POOL_SIZE` 6400→4096 SAFE
  (observed peak 443), SMPC pool 14→2 NEEDS-MARGIN (exactly 2 blocks ever
  allocated — zero headroom for any connect event; 14→4 is the safe
  variant). Why it matters: T2.2's reclamation package (a) is now gated on
  measured, not borrowed, peaks; verdicts are scoped to the measured route
  (600 replay ticks + idle; no owner free-roam, no object interactions).
  Evidence: `docs/saturn/evidence/reports/sprint2-t2_1-peak-capture.md`
  (+ 3 JSON captures beside it).
- Sprint 2 T2.1 diagnostic peak instrumentation, **diagnostic-gated —
  the product build is unaffected** (every definition, store, and the
  probe symbol itself sit behind `SATURN_DIAGNOSTIC_MODE == 1`; a
  `SATURN_DIAGNOSTIC_MODE=0` build sees only a typedef). Why: two of the
  four capacity-shrink gates had no durable rail — the fast3d profile's
  `vdp1_command_highwater` is erased by the per-frame profile clear in
  `sm64_saturn_fast3d_frontend_submit()` (it only ever holds the latest
  frame), and `create_gfx_task_structure()` computes the master
  display-list usage then discards it. New `g_sm64_saturn_peak_probe`
  (`src/port/saturn/runtime/saturn_peak_probe.h`, NOLOAD `.lwram_bss`,
  written through the P2 cache-through alias) accumulates run-long maxima
  for VDP1 `command_count`, gouraud count, and `gGfxPool` entries; updated
  from `sourceboot_frame_update_telemetry()` and
  `create_gfx_task_structure()`. Consumed by the new
  `tools/saturn/capture_sprint2_peaks.py` (headless Ymir; also samples
  libyaul's `_peripherals_memb` and stain-scans/walks the TLSF
  `_private_pool` dump host-side — those two needed no target code).
  Host tests: `tools/saturn/test_capture_sprint2_peaks.py` (13 tests,
  mutation-checked). Evidence:
  `docs/saturn/evidence/reports/sprint2-t2_1-peak-capture.md`.
- `make verify-audio-loop-contracts`: wires the four previously-orphaned
  regression suites from the Sprint 1 defect loop (24 tests) into a runnable
  target — the DSP-quiesce-before-staging ordering, the single-owner music
  start, the HWRAM/LWRAM work-storage split, the music-bundle degrade
  contract, and the wav-to-pcm8 loop cap. Post-gate review found these
  hardware-safety pins existed but were referenced by no make target, so a
  future regression would have passed every documented suite silently.

### Milestone

- **Sprint 1 (R0+R1) accepted by the owner on 2026-08-15 with candidate
  `id-86d3880727ed1d10`.** For the first time in the project's history the
  source game's own `play_music` call produces audible, looping music through
  the semantic API, the MC68000/SCSP driver, and the SCSP's hardware loop —
  with owner-accepted visuals. Measured cadence ~1.1-2 FPS was recorded and
  ruled non-blocking for this milestone by owner instruction; it becomes the
  next sprint's first objective. The one owner-observed artifact (a periodic
  piercing noise) was first dispositioned as a Ymir host-audio underrun
  rather than a port defect; **that disposition is retracted.** A sound-RAM
  staging verification found it to be a port defect with register-level
  evidence -- the SCSP's own effect DSP writing its reverb ring over the
  staged sample bank -- and it is fixed under Fixed below. Evidence:
  `docs/saturn/evidence/reports/sprint1-r1-owner-gate.md`.

### Added

- Recovery branch `saturn/recovery` (forked from `sh2/native-math-purge` @
  `b2447f67`) now carries the donor worktree's complete unlanded state,
  transplanted 2026-08-14 as five curated cluster commits (audio, memory
  relief, render/actor/HUD, build wiring, catch-all). This preserved 117
  tracked modifications byte-identically plus 53 files with no prior git
  history — including the SCSP write-only-latch readback fix, the SFXB
  bundle packager, the `source_audio_live` bridge, the cart-cold memory
  refactor, and `docs/saturn/PRODUCT_RECOVERY_HANDOFF_2026-08-14.md`
  itself. Generated scratch (~105 MB of `.tmp-*`, capture dumps, stale
  package generations) was deliberately excluded. Rationale: 14+ of these
  files were unrecoverable outside the forensic donor worktree, and the
  committed tree did not link without them. See the Sprint 1 plan
  (`docs/superpowers/plans/2026-08-14-sprint1-recovery-baseline-audio.md`)
  for the curation and verification record.

- `verify-memory-map` gate (`Makefile.saturn.mk` + a new `verify` subcommand
  on `tools/saturn/verify_sourceboot_memory_map.py`): the HWRAM/LWRAM link
  margin becomes a build output instead of a ledger note. Rationale: the last
  full-feature link closed with 24 bytes of HWRAM slack and nothing in the
  build loop would have said so. The `verify` mode inspects ONE built ELF
  (`--elf`, `--required-final-margin`, default 0x1F00) and prints `___end`,
  HWRAM remaining vs required, and the LWRAM floor, exiting nonzero on any
  violation; it binds camera route and cart stage from the sealed identity
  spec (`generated/saturn_build_identity_spec.json`) rather than trusting
  path tags, and runs the full existing `validate_layout` region checks.
  Infrastructure failures (missing ELF, missing toolchain binary, nonzero
  readelf/nm) also emit the `RESULT          = FAIL: ...` contract line and
  exit 1 instead of raising an uncaught traceback, so downstream grep
  consumers see a uniform failure shape for every failure class. The
  make target defaults `SOURCEBOOT_CANDIDATE_ELF` to the newest
  `e2-bob-identity-*/obj/*.elf` (same location pattern as
  `verify-sourceboot-hud-target`) and accepts an explicit override. The
  tool's toolchain paths are now environment-derived (`YAUL_INSTALL_ROOT`,
  `MSYS2_ROOT`) with the old developer-machine literals kept as warned
  fallbacks so existing phase-chain invocations keep working unchanged.

- `tools/saturn/wav_to_pcm8.py`: converts an owner-provided WAV (any 8/16-bit
  mono/stereo source) to the raw signed 8-bit mono PCM the SCSP plays —
  linear resample to `--rate` (default 8000 Hz) plus a duration trim.
  Looped music is one SCSP sample, so the real gate is the 65,535-byte cap
  set by the SFXB row's u16 sample count and the SCSP's 16-bit loop-end
  addressing — not the sound-RAM bank: the default trim is
  min(28 s, 65535 / rate), about 8.19 s at 8000 Hz, and an explicit
  `--max-seconds` above the cap warns and still clamps so the tool can
  never emit output the packager must reject. The source WAV is
  ROM-derived and must never be committed; `.gitignore` now covers
  `bob_theme.us.wav` and `*.pcm8`.

- `tools/saturn/render_m64_wav.py`: host M64-to-WAV sequence renderer used
  to produce the gitignored owner music WAV (`bob_theme.us.wav`) from
  ROM-extracted assets (decomp sound-bank JSONs plus extracted AIFF
  samples). The tool is pure format logic — it embeds zero Nintendo
  sample or sequence bytes; all copyrighted inputs stay gitignored on the
  owner's machine. Opcode semantics are a close port of the US
  `src/audio/seqplayer.c` switch statements (sequence, channel, and layer
  scripts) with `src/audio/effects.c` volume math and `src/audio/heap.c`
  tick timing (tempo 14360, 240 updates/s, 48 tatums/beat). Documented
  simplifications (vibrato, portamento, reverb, envelope shape, pan) are
  logged per render, and the tool refuses to write output when fewer than
  20 notes parse rather than emit garbage. Stdlib only; runs on Python
  3.12+ (parses AIFF with `struct`, not the removed `aifc` module).

- Owner music is now wired into the sourceboot SFX-bundle build
  (`src/port/saturn/sourceboot/Makefile`). `SOURCEBOOT_MUSIC_WAV ?=
  bob_theme.us.wav` (worktree-root relative; ROM-derived and gitignored,
  rendered by `render_m64_wav.py`) is converted to
  `build/saturn/audio/bob_theme_8k.pcm8` by `wav_to_pcm8.py` at
  `SOURCEBOOT_MUSIC_RATE` (8000 Hz; the tool's rate-aware default caps the
  payload at the 65,535-byte SCSP sample limit), and the packager is
  invoked with `--music-pcm`/`--music-rate` so the looped music row is
  appended after the SFX PCM in sound RAM. Rebuild chain: the pcm8 rule
  depends on the WAV and the converter, and the bundle rule depends on the
  pcm8, so a changed WAV reflows into the packaged bundle. Graceful
  degrade: when the WAV is absent, a parse-time `$(warning)` announces
  "music WAV absent; building music-less bundle" and the music flags are
  omitted — the music-less bundle contract remains valid (SEQ_START finds
  `music_sample_index` 0 and stays silent by design), so clean checkouts
  without the owner's WAV still build. `wav_to_pcm8.py` joins
  `SOURCEBOOT_GENERATOR_INPUTS` so the hermetic closure attests the
  converter alongside the other bundle generators.

### Changed

- Three-way work-storage split in `saturn_demo_render.c` (Sprint 1
  Task 10 stage 1b), partially reverting the Task 8 full-HWRAM promotion
  below. Root cause: Task 8 assumed the R1 feature-off build
  (`COMPLETE_MARIO_ANIMATION=0 DYNAMIC_ACTOR_CLOSURE=0`) freed the HWRAM
  pressure that had forced the 2026-08-07 LWRAM eviction, but the stage-1
  link smoke (`docs/saturn/evidence/reports/sprint1-stage1-link-smoke.md`)
  proved the `.bss` growth is committed and always-on: at pool 208 the
  link still overflowed HWRAM by 41,712 bytes, i.e. 49,648 bytes of
  relief were required including the 0x1F00 heap-margin gate. This was
  the ladder's rung (b), refined per review into a split rather than a
  blanket re-eviction:
  - Terrain/primitive scratch (18 arrays, `DEMO_CPU_WORK_CACHE` still an
    empty macro) **stays in 32-bit HWRAM** — it is the multi-pass
    inner-loop working set this build exercises every frame, and its
    16-bit LWRAM placement was the mechanism of the 5.29 FPS → ~1 FPS
    collapse.
  - Actor-path-only scratch moves to LWRAM via the new
    `DEMO_ACTOR_WORK_CACHE __attribute__((section(".lwram_bss")))` macro:
    `s_actor_queue_merge_ids` (2,576 B), `s_actor_slots` (1,288 B),
    `s_actor_texture_slots` (1,288 B), `s_actor_gouraud` (2,576 B),
    `s_actor_gouraud_addresses` (2,576 B) — 10,304 B. Each was verified
    actor-path-only by reading every use: they are touched exclusively in
    `demo_actor_queue_assemble_done` (dormant A5.8 actor queue),
    `demo_reserve_mario_gouraud`, `demo_emit_mario`, and
    `demo_emit_mario_range`, never in the terrain path. With
    `DYNAMIC_ACTOR_CLOSURE=0` the queue path is dead weight; the Mario
    emission arrays are touched once per actor primitive on the master
    only, not in the terrain multi-pass loop.
  - `s_bob_hot_workarea` (43,776 B) returns to LWRAM `.lwram_bss`
    (the `91f02ffd` placement, 16-byte alignment kept).
  Total HWRAM relief 54,080 B against the 49,648 B requirement (~4.4 KB
  slack). Tradeoff: Mario/actor emission and hot-promotion population now
  read/write 16-bit LWRAM; FPS impact is measured at the Task 11 gate
  (the 4 FPS floor still blocks retention). The dual-SH2 work-storage
  contract test is retargeted to pin the split (terrain set: empty macro,
  `lwram` forbidden; actor set: `DEMO_ACTOR_WORK_CACHE` required;
  workarea: `.lwram_bss` + `aligned(16)` required), so no symbol can
  change sets silently.

- `tools/saturn/probe_audio_mailbox.py` is parameterized from its donor-
  scratch form: argparse CLI (`--ymir`, `--ipl`, `--cue`, `--output`,
  `--frames`, `--timeout`, `--sfx-metadata`) with a `main()` guard replaces
  module-level execution, hardcoded absolute Ymir/BIOS paths, the stale
  `e2-bob-identity-id-7deb747eb230b595` artifact pin, and a broken
  `sys.path` insert left over from the script's repo-root origin (its
  `ROOT`-relative SFXB metadata path silently pointed inside `tools/saturn/`
  after promotion). No behavior change to the mailbox/SCSP peek or decode
  logic — addresses, sizes, field decoding, and the sorted-JSON report shape
  are byte-identical; defaults (3600 frames, 300 s timeout, stdout report)
  match the old constants. `tools/saturn/test_probe_audio_mailbox.py` pins
  the pure `peek` payload normalization (list / nested dict / hex-string)
  and `be16` decoding. Live exercise happens in Task 11; a 600-frame smoke
  run against the donor CUE produced a well-formed report.

- The demo-path renderer's hot working set returns to 32-bit HWRAM,
  reverting the 16-bit LWRAM eviction from the 2026-08-07 memory-budget
  relief (`ec7b992a` introduced the `DEMO_CPU_WORK_CACHE` `.lwram_bss`
  macro over 23 per-primitive scratch arrays; `91f02ffd` moved the
  43,776-byte promoted-geometry work area `s_bob_hot_workarea`). These are
  inner-loop operands touched per visible primitive per frame on a
  memory-bound render loop; 16-bit LWRAM vs 32-bit HWRAM is the mechanism
  behind the accepted A9A 5.29 FPS collapsing to ~1 FPS. The eviction was
  forced by HWRAM pressure from feature work (VDP2 HUD, 64 KiB actor
  arena, geo-walk storage); the R1 build runs with
  `COMPLETE_MARIO_ANIMATION=0 DYNAMIC_ACTOR_CLOSURE=0`, which frees that
  pressure, so the placement reverts. Honesty note: there is no host test
  that can validate SH-2 section placement, and the SH-2 link happens at
  Task 10 — this change is validated by (a) being provably placement-only
  (attribute/comment hunks, zero logic), and (b) Task 10's link, margin
  gate, and FPS capture. Fallback ladder if Task 10's link fails on HWRAM:
  (1) drop `OBJECT_POOL_CAPACITY` to 208; (2) return only the
  per-primitive scratch arrays to HWRAM and leave `s_bob_hot_workarea` in
  LWRAM; (3) revert this commit entirely and record the margins. The
  `tools/saturn/test_dual_sh2_work_storage_contract.py` placement pin —
  which asserted the exact `.lwram_bss` macro string — is retargeted in
  the same commit to pin the new HWRAM policy (empty macro, same 23-symbol
  list, `lwram` now forbidden on those declarations), so re-eviction can
  never happen silently. Placements that were already LWRAM at A9A
  (terrain result/command banks, actor queue payload banks, LOD storage,
  resident copies, templates, transform cache) are untouched.

- `SEQ_START` on the MC68000 driver now starts the SFXB bundle's looped
  music sample on SCSP slot 0, which is pinned to music; SFX round-robin is
  confined to slots 1-3 and can never evict the music voice. Replace
  semantics: every `SEQ_START` keys any active music off before evaluating
  the new request, so a start against a music-less or invalid bundle also
  silences stale music instead of leaking it. Presence discrimination per
  the Task 5 review carry-over: music exists iff the trailer's
  `music_sample_index` (+30) is nonzero AND the row carries
  `SM64_SATURN_PCM_SAMPLE_LOOP` — a zero index is silent and not a fault,
  while a nonzero index without a valid looped row counts a `music_fault`
  and an SCSP start refusal counts a `music_scsp_failure`. `SEQ_STOP` (and
  RESET/MUTE stop-all) key slot 0 off idempotently. The MUSIC_SEQUENCE
  diagnostic word (0x7F0A) keeps its position but is now published live
  with the last `SEQ_START` source sequence id (words[1]) instead of a
  boot-only zero; no sequence-id filtering happens on the driver (one-song
  contract for R1). The SFX rotor wraps by comparison because the
  freestanding image links no libgcc (`% 3` would need `__umodsi3`); the
  image grew 5,643 -> 5,883 bytes. Consumer impact: with a music-bearing
  bundle (`--music-pcm`) the game's `play_music` now produces audible
  hardware-looped music — the first time the semantic music path reaches
  the SCSP; SFX-only bundles behave exactly as before.

- Music is now packaged as one looped SFXB sample instead of an m64 sequence
  trailer. Why: the Task 4 driver diet removed the sequence VM (the trailer's
  only consumer), and the old fallback plan assumed a 240 Hz retrigger timer
  that does not exist — the SCSP's hardware gapless loop, which the driver
  already keys from a sample-row flag (`scsp_pcm8.c` sets `LOOP_NORMAL` when
  `SM64_SATURN_PCM_SAMPLE_LOOP` is set), replaces both. What changed:
  `compile_sourceboot_sfx_bundle.py` gained `--music-pcm <raw-pcm8>` and
  `--music-rate` (default 8000) options that append the owner-supplied PCM
  as the final sample row carrying exactly the loop flag; the bank-22
  instrument row and the raw m64 append are deleted, so the SFXB header's
  music sequence offset/byte words are now always zero and the music word at
  +30 holds only the looped row's index (without `--music-pcm` every trailer
  word is zero). The MC68000 bundle validator (`pcm_voice.c`) now accepts
  exactly the loop bit in sample-row flags — any other bit still fails the
  bundle closed. Budget failures name the fix (`wav_to_pcm8.py
  --max-seconds` or a lower `--music-rate`); the sound-RAM budgets are
  unchanged. Consumer impact: Task 6 rewrites `SEQ_START` to key the
  trailer-indexed looped row (it currently keys music off); existing
  packager invocations without the new options keep producing SFX-only
  bundles the relaxed validator accepts, and the owner's WAV/PCM8 files stay
  uncommitted.

- Removed the M64 sequence VM, its 20-voice software allocator, and the
  software envelope engine from the linked MC68000 driver image and moved the
  driver state off the reserved stack. Root cause of diagnostic failure
  0x0340 (garbage note velocity, zeroed envelope fields): the voice state
  struct had grown to 2,080 bytes — the embedded allocator alone is 1,760 —
  while remaining a stack local in `pcm68k_main` against the 1,020-byte
  reserved stack (`linker.ld` 0x3C00..0x3FFC), so deep call frames corrupted
  the note bindings. The state is now a 102-byte file-scope `.bss` object
  (guarded by a `_Static_assert` at 768 bytes and a host regression test),
  and `start.S` already byte-clears `.bss` before `pcm68k_main`. Removed
  from the image only: `sequence_vm`, `audio_engine`, `voice_allocator`,
  `desired_voice` objects — the sources stay in the tree (still compiled by
  the TASK17 relocatable-module target and their host tests) banked for a
  future sprint, which also moots the secondary defect of channel scripts
  being decoded as layer scripts. The image shrank from 13,520 to 5,643
  bytes, leaving 9,612 bytes between `__driver_end` and `__stack_bottom`.
  Consumer impact: music via the sequence VM is gone — `SEQ_START`
  temporarily keys music off until the looped-sample music path lands in the
  next tasks; the semantic SFX path, mailbox protocol offsets, and SFXB wire
  format are unchanged (VM-sourced diagnostic words now publish 0). The
  heartbeat host test also regained a buffer large enough for the music
  diagnostics window at 0x7F00 that `publish_boot` already wrote (its
  previous 0x4040-byte model buffer made the newly-linking test overflow).

- Packed the unchanged, 32-byte-aligned sourceboot VDP1 command double-buffer
  first in HWRAM BSS after a master-only 72-byte camera transition record moved
  to explicitly reset NOLOAD LWRAM. This removes alignment-only heap loss
  without reducing VDP1, Gouraud, object, actor, or audio capacity; the fresh
  normal BOB link retains 24 bytes above its enforced TLSF floor. The linked
  CUE is available for manual validation, while visual and audible product
  acceptance remain open.

- Corrected the sourceboot PCM68K cold-boot handoff to keep the SCSP CPU
  stopped through 512-KiB mode selection, the full SCSP-RAM clear, and the
  validated driver/bundle copies, then start it once. The standalone warm
  enable remains rejected because it can execute uncleared RAM, but the actual
  black-screen cause was a false readback check on the write-only SCSP mode
  latch: it aborted after `SNDOFF`, before `SNDON` or the game loop. The
  issued write is now validated by the existing post-start READY/heartbeat
  protocol instead. Sourceboot remains the sole cold-boot SCSP-RAM writer and
  the MC68000 remains its consumer; audible success still requires a fresh
  combined target/manual check.

- Corrected the normal BOB Mario texture-detail lowerer so an RGB1555 detail
  command consumes its already-reserved fixed Gouraud table instead of always
  replacing it. This keeps the existing bounded fallback when no table is
  available, but makes the visible textured surface obey the same shading
  contract as its base polygon; the repair was exposed by the first live
  normal Bob-omb replay rather than by a package or renderer redesign.

- Made the seven-file S64P generation publication a single ownership-aware
  transaction after rereview showed that per-file no-clobber could still leave
  a partial generation when a later sidecar conflicted. The compiler now
  computes root, payload manifest, assembly, validation, generated header, ABI,
  and report bytes before the first link; canonicalizes each target through its
  physical parent, rejects output symlinks and canonical duplicates, and
  acquires OS-held per-target locks in stable sorted order from the host temp
  namespace. Reordered or partially overlapping output sets therefore share
  locks instead of racing under different textual aliases. It preflights every target; stages every byte
  privately; and rolls back only links whose filesystem identity still belongs
  to the failing transaction. The metadata report is linked last as the
  consumer-ready marker, and an existing marker with missing siblings is
  rejected as an incomplete set. Identical concurrent producers converge, while
  divergent producers fail without mixing generations. Standalone header/ABI
  emission uses the same pair transaction. This preserves the fixed Saturn
  artifacts while making interruption, contention, and late conflicts
  fail-closed.

- Repaired the canonical actor scene-bundle owner after independent review
  found three fail-closed gaps. Sourceboot now initializes both VDP1 command
  prefixes only after the boot-only 2,560-byte cold-upload borrow has retired,
  so the first frame cannot select a bank whose system/local/END commands were
  overwritten by staging. Root activation additionally binds the exact
  `bob-area1-actors-v3` stable ID and scene lifetime, not merely its resealed
  content digest and generation. S64P roots, metadata, payload manifests,
  generated assembly, validation reports, and headers now use one exclusive
  no-clobber publisher: identical existing bytes are verification-only,
  divergent same-generation bytes fail without replacement, and the assembly
  is generated by the package compiler instead of an unconditional Make
  redirection. Existing CRLF-only derived reports require a one-time
  recoverable migration to canonical LF bytes. This fixes target ownership and
  reproducibility defects; it is unrelated to MSYS2 DLL loading.

- Added the canonical sourceboot owner for the generic BOB actor dependency.
  The root package build now emits one relocation-neutral S64P root plus its
  exact S64F-v3 CART dependency and a deterministic assembly wrapper; the
  sourceboot target only consumes and seals those bytes instead of rebuilding
  them inside the hermetic identity boundary. A fixed 64-byte, generation-last
  bundle publication resolves any validated family/model snapshot through the
  common S64F/S64B path, leases one of two aligned workspace lanes, and feeds
  the existing generic pose/meshlet preparation without model-, behavior-, or
  Cannon-specific branches. Boot validation scratch and the 1,280-byte runtime
  workspace now share one typed 3,348-byte LWRAM lifetime, while the complete
  source owner including the 2,064-byte texture publication is 5,556 bytes.
  Scene validation reuses state-owned views/identity slots, reducing exact SH-2
  stack frames from 3,920/3,420 bytes to 92/88 bytes (112 for section load);
  source initialization is 220 bytes. Cold CART spans use 2,560 bytes borrowed
  from the idle VDP1 command bank only during boot, so this task adds no
  persistent HWRAM upload buffer and waits for every checked DMA before that
  storage becomes frame-visible. The all-resident actor texture/CLUT regions
  remain 16,640/2,816 bytes with 33,216 bytes of Yaul `remaining` capacity
  left. Package tools now require an explicit payload root and reject absolute
  or escaping dependency paths before hashing or header emission. This is
  source/package ownership plus host and freestanding SH-2 evidence for the 14
  currently compiler-supported textured BOB families; the other 20 drawable
  families, production job/emitter cutover, linked target, Ymir, release, and
  manual gates remain open in Task 9 and later.

- Added master-owned, fixed-capacity actor texture residency for the validated
  BOB S64F-v3 dependency. Scene activation now prevalidates every selected
  S64B-v2 bank, hash, source/destination span, independent texture/CLUT bound,
  VDP1 offset/index limit, and the complete canonical 128-entry plan before it
  submits any transfer through the existing checked SCU-DMA queue. The CART-
  resident bundle is never submitted directly to SCU DMA: the caller supplies
  one bounded, aligned HWRAM stage, whose physical range and nonoverlap with
  bundle, publication, and VDP1 ownership are proved before the first copy.
  Every queue request is read-only-preflighted, then one cold span is CPU-
  copied and retired before the stage is reused. Successful
  activation publishes only a 2,064-byte pointer-free scalar table, writing
  the nonzero residency generation and committed flag last; every lifecycle,
  malformed-input, stale-generation, submit, or wait failure invalidates both
  old and partial publication, so dirty VRAM can never become reachable. One
  complete publication validator now gates replacement and lookup, including
  duplicate IDs, ordering, totals, unused rows, and scalar corruption, while
  generation replacement uses explicit unsigned half-range serial ordering.
  Scene staging checks scan the owned scalar fields in place instead of
  materializing a 2,064-byte zero publication on the SH-2 stack; exact
  `-fstack-usage` evidence removed the former 2,076-byte helper frame.
  The HWRAM upload-stage classifier also accepts only SH-2 P0 cached and P2
  cache-through address shapes before physical normalization. P1/P3/P4 cache
  purge, address-array, and data-array aliases can no longer masquerade as
  HWRAM and receive a CPU copy merely because their masked low bits fall in
  `0x06000000..0x06100000`; hostile `0x460`, `0x660`, and `0xC60` shapes are
  covered directly. Native host fixture pointers bypass Saturn area decoding,
  removing an ASLR-dependent false failure from the checked-queue double.
  The residency verification target also rebuilds and C-validates an absent-
  output real BOB bundle instead of accepting a fixture-only contract. The
  real BOB subset is measured at 14 mappings, 16,640 texture bytes, and 2,816
  CLUT bytes, including Cannon, while opaque v1 banks remain unmapped. Scene
  residency owns and clears this state on failed staging, mismatched commit,
  reset, and matching unload without yet activating the renderer. The legacy
  texture initializer now delegates to an explicit bounded-region initializer;
  this authorized file-list correction and moving Task 6's unchanged 16-byte
  mapping ABI into a Yaul-free header preserve existing material binders. This
  is host and freestanding SH-2 module evidence only: Task 8 streaming/runtime,
  renderer, Ymir, release, and full-game proof remain deliberately closed.

- Hardened the new S64B-v2 material-to-VDP1 boundary after independent review
  found three fail-open edges: texture/CLUT address checks now cover the final
  required byte of every nonempty IR tile and actor-bank aggregate before any
  command write, CLUT tiles are rechecked against their bank-local dense
  palette count before the global residency offset is added, and the stale
  family-bank test anchor is resealed only after two deterministic Task 5
  rebuilds produced the same payload/header identities. Empty aggregate spans
  deliberately touch no hardware address, preserving flat-only banks without
  inventing residency requirements. This remains a host/freestanding target-
  module repair only; residency, runtime activation, rendering, and Ymir proof
  are still deferred.

- Added the master-owned final-emission boundary for validated S64B-v2 actor
  materials. Stable bank recipes now translate explicitly to Yaul flat,
  CLUT16, RGB1555, Gouraud, replace, and half-transparent command fields while
  preserving scalar-only cross-SH-2 ownership and leaving every command byte
  unchanged on a stale generation, wrong bank, invalid ordinal/recipe/tile,
  or VRAM span/address failure. The shared IR texture binders now accept the
  Saturn VDP1 width range `8..504` as `uint16_t`, retain exact 8/248 command
  encoding, support 256/504 without truncation, disable texture end codes, and
  validate complete format-derived texture/CLUT spans before mutation. This is
  a target module and host/freestanding proof only: texture residency,
  generation publication, runtime activation, renderer integration, and Ymir
  evidence remain deliberately deferred to Tasks 7 and later.

- Added host-only orchestration for the exact bounded BOB actor dependency:
  fourteen closure-attested S64B-v2 banks now pack into one deterministic
  S64F-v3 with canonical S64P dependency metadata, exact unsupported inventory,
  relocation-neutral bytes, and atomic no-clobber/report-last publication.
  The planner keeps all 5,288 source ceilings unchanged while labeling their
  85,512-output / 65,788-command / 29,352-Gouraud sum as a non-acceptance
  diagnostic, and instead proves the Saturn frame policy's dedicated 2,718
  output, post-Mario 1,351 command, and 892 Gouraud shares. The measured
  46/24/46 maxima guarantee any 19 supported actors; texture and CLUT demand
  remain separately reported at 16,640/2,816 bytes. The complete 446,432-byte
  post-command/Gouraud reservation equation is retained, and Task 7 must
  repartition Yaul's resulting 52,672-byte remainder, leaving 33,216 bytes.
  Later runtime admission, residency, rendering, and Ymir evidence remain
  deliberately unchanged and fail-closed. The generated two-lane workspace
  capacity applies the design-owned 256-byte rounding to the 1,091-byte BOB
  maximum, reserving 1,280 bytes with 189 bytes of static margin. The host
  capability fixture's generated-family trust anchor is also resealed to the
  twice-reproduced PNG-attested closure digest; this is expected-output
  maintenance only and does not change target validation semantics. The build
  now recomputes and exactly reconciles closure-derived family semantics before
  consuming them, routes the ten package classes through the canonical target-
  profile containment/collision/ownership validator, and tracks real source,
  descriptor, payload, and host-tool prerequisites. Unchanged generations are
  verify-only no-ops; a changed input for an already-published generation
  invokes no-clobber publication and fails closed instead of silently reusing
  stale bytes. Every verification pass checks all four sidecars and compares
  them byte-for-byte with a private deterministic rebuild from current inputs.
  The publisher's Make prerequisites now cover its complete repository-local
  Python import closure, with a recursive AST regression preventing transitive
  compiler changes from bypassing the publisher/no-clobber freshness path.

- Added host-only, closure-attested Fast3D material capture and S64B-v2
  lowering for the exact 14 measured direct-textured BOB keys. The existing
  strict display-list compiler now owns texture/load/tile/combiner/geometry/
  layer/opacity/call/tail/UV state, binds checked-in PNG paths and SHA-256s
  into v2 source identity, and bakes one deterministic 16x16 or 32x32 CLUT16
  input per textured source triangle before Task 2's exact resource
  deduplication. This makes real family 29 / `MODEL_CANNON_BASE` `0x0080` /
  `bhvCannon` pack as pointer-free v2 without pairing while every unmeasured,
  computed, partial, ambiguous, unknown, `GEO_SHADOW`, `GEO_SCALE`, and
  `GEO_ASM` state remains fail-closed. The change deliberately adds no target
  residency, renderer, Ymir, or full-game claim; those remain later gates.

- Added the freestanding Saturn S64B-v2 parser and opaque mixed-bank S64F-v3
  delegation boundary. The target now dispatches once on v1/v2, validates the
  exact 192-byte v2 header, canonical hot/cold spans, stable material/tile
  records, transparency data, dense first-use ordinals, and exact draw/
  texture/Gouraud/residency aggregates before exposing bounded accessors.
  S64F resolve no longer reconstructs v1 header fields itself, so later bank
  versions cannot bypass their owning parser. This keeps workers and bundles
  pointer-free and preserves historical v1 callers/bytes while malformed v2
  content fails before publication; material compilation and VDP1 residency
  remain deliberately closed for later tasks.

- Added the canonical pointer-free host S64B-v2 contract needed for Saturn
  actor textures. A validated v1 core is promoted from its 104-byte header to
  the additive 192-byte layout with every bank-absolute pose/span offset
  rebased, while `GEO1`-relative offsets remain exact. Target-ready bindings,
  stable material recipes, dense first-use texture/CLUT ordinals, checked
  payload equations, zero padding, transparency words, and per-instance
  aggregates now fail closed through the version-owned parser. Exact tile and
  palette deduplication stays offline and linear; the serialized bank contains
  no host path, pointer, Fast3D stream, or VDP1 address. Historical S64B-v1
  Mario and S64F-v3 bytes remain unchanged, and later BOB material admission
  remains closed until the measured lowering table is implemented.

- Consolidated the duplicate freestanding SHA-256 implementations used by the
  S64P scene-package and S64F v2 actor-family-bank validators into one
  incremental target primitive. Existing serialized bytes and hashes remain
  unchanged; the shared implementation now rejects null nonempty inputs and
  byte-accounting overflow explicitly.

### Fixed

- The SCSP's effect DSP no longer overwrites the staged PCM sample bank --
  the root cause of the R1 owner-observed periodic piercing noise, and of
  the unrecognizable SFX. Root cause: the port never programmed any part of
  the SCSP effect-DSP state, so it inherited whatever the BIOS left behind.
  `tools/saturn/probe_sound_ram_verify.py` read SCSP common register `0x402`
  back as `0x0118` on candidate `id-b3aceeb28570230b` -- RBP = 24, RBL = 2,
  i.e. a reverb ring buffer occupying `[0x30000, 0x40000)` of sound RAM --
  with a live BIOS microprogram still in `MPRO` (377 of 1024 bytes nonzero,
  COEF 51, MADRS 24). The DSP rewrote that entire 64 KiB window every sample
  period, on top of data the cold boot had just staged there: 65,354 bytes
  of the 273,225-byte PCM bank differed from the packager's blob (23.9%),
  including 21,320 bytes of the music sample at `0x3ACB8` -- 2.665 s of
  every 8.146 s loop -- plus 8 whole SFX samples that sat inside the ring.
  Every byte outside the window matched exactly; the staging `memcpy` and
  the ISO were always clean, which is why six previous eliminations (control
  path frozen, slot registers correct, loop seam clean, LEA math right, PCM
  file on disk correct) all passed while the defect persisted: none of them
  had ever compared the bytes actually resident in sound RAM at playback
  time.
  Fix: `sm64_saturn_sound_cpu_yaul_set_512k()` (the SCSP configuration step
  that already runs with the sound CPU stopped, after SNDOFF and before the
  sound-RAM clear and the driver/metadata/PCM copies) now calls a new
  `sm64_saturn_sound_cpu_program_effect_dsp()` which zeroes `MPRO`
  (`0x25B00800`-`0x25B00BFF`), `COEF` (`0x700`-`0x77F`) and `MADRS`
  (`0x780`-`0x7BF`), then writes `0x402 = 0x0128` (RBP = 0x28, RBL = 2),
  moving the ring to `[0x50000, 0x60000)` -- above the bank end at `0x4AB49`
  and inside the 512 KiB of sound RAM. An all-zero microprogram asserts
  neither MWT nor MRD, so the DSP issues no sound-RAM access at all; the
  ring move is defence in depth. Register offsets and the RBP/RBL encodings
  were verified against Ymir's own SCSP core rather than assumed
  (`ymir-core/include/ymir/hw/scsp/scsp.hpp` `WriteReg402` for the bit
  layout, `scsp_dsp.hpp` `UpdateRBP`/`UpdateRBL` for `RBP << 12` words and
  `0x2000 << RBL` words).
  This is programmed explicitly rather than dodged by relocating our own
  data out of the one observed window, because RBP, RBL and `MPRO` are BIOS
  leftovers that vary by BIOS revision and region: a bank placed to miss the
  USA BIOS's ring would still be destroyed under another. It also removes
  the whole class of failure rather than one instance of it -- the same
  inherited-hardware-state class as open follow-up 1 (uninitialized slot
  registers `0x0E`/`0x12`/`0x14`/`0x18`).
  Verified on target, not inferred: re-running the same probe against the
  new candidate `id-782c9c8f323a01a0` reports SCSP `0x402` = `0x0128`
  (RBP = 40, RBL = 2, ring `[0x50000, 0x60000)`), `MPRO` 0 of 1024 bytes
  nonzero, COEF 0, MADRS 0, and all three staged regions EXACT MATCH -- the
  1,232-byte SFXB metadata, the whole 273,225-byte PCM bank (was 65,354
  differing) and the 65,169-byte music sample (was 21,276 differing) -- 0
  differing bytes everywhere.
  Consumer impact: the music sample and all 8 affected SFX samples now play
  from the bytes the packager built. Compile-time `_Static_assert`s pin the
  `0x402` encoding and keep the ring inside sound RAM and above the bank
  base; `tools/saturn/test_full_game_audio_source.py` gains
  `test_effect_dsp_is_programmed_before_any_sound_ram_staging`, which pins
  the register writes, the RBP/RBL decode (ring must clear the staged bank
  and must not land back on `0x30000`), and the ordering in both the boot
  state machine and `source_audio_live.c` (quiesce before clear, before the
  metadata copy, before the PCM bank copy). Six mutations of the fix were
  each killed by that test. The Task 7 audio-init failure handling and the
  cold-boot ordering contract are untouched.

- `src/port/saturn/sourceboot/main.c` no longer issues its own bootstrap
  `play_music(SEQ_PLAYER_LEVEL, SEQUENCE_ARGS(4, SEQ_LEVEL_GRASS), 0U)`; the
  level script is the sole owner of the level music start. The call carried a
  comment claiming "the generic BOB path has no full-game level-update caller
  yet". That claim was wrong: dumping the audio control ring found exactly
  `SET_MASTER(12)`, `SEQ_START(seq 3)`, `RESET`, `SEQ_START(seq 3)`, and the
  trailing `RESET`+`SEQ_START` pair is the game's own
  `init_mario_after_warp` -> `set_background_music` (which calls
  `sound_reset` then `play_music`) responding to `levels/bob/script.c`'s
  `SET_BACKGROUND_MUSIC(0x0000, SEQ_LEVEL_GRASS)`. Root cause of the
  duplication: the bootstrap call bypassed `src/game/sound_init.c`'s
  `sCurrentMusic` bookkeeping, so the game's own duplicate-suppression guard
  could not see it and the sequence was started twice — restarting the music
  sample. Removing the call restores that bookkeeping, so later warps are
  suppressed correctly. The now-unused `#include "seq_ids.h"` went with it;
  the Task 7 audio-init failure handling in the same
  `SATURN_FEATURE_SEMANTIC_AUDIO` block is untouched.
  `tools/saturn/test_full_game_audio_source.py` replaces its source-presence
  assertion with `test_level_script_owns_music_not_a_sourceboot_bootstrap_call`,
  which pins both halves of the new contract: `main.c` issues no `play_music`,
  and `levels/bob/script.c` still carries `SET_BACKGROUND_MUSIC` with
  `SEQ_LEVEL_GRASS` so music ownership provably still exists.

- Continuous SFX are no longer re-keyed on every game-loop tick — the R1
  owner-reported "periodic piercing noise". Root cause: SM64 re-asserts every
  *continuous* (non-discrete) sound once per tick and expects the sound driver
  to KEEP it playing, but `sm64_saturn_pcm_play_sample()`
  (`src/port/saturn/audio68k/pcm_voice.c`) had no already-playing check, so
  each `PLAY_REFRESH` ran the full `sm64_saturn_scsp_pcm8_start()` sequence:
  KEY_OFF, reprogram SA/LSA/LEA/EG/PITCH/PAN, KEY_ON — a hard restart from
  sample offset 0. At the candidate's measured ~1.1 FPS that is a re-trigger
  every ~0.9 s, which is both the owner's periodic burst (its period is the
  *frame* period, not the 8.15 s music loop) and the reason SFX were
  unrecognizable: no continuous sound ever played longer than one frame.
  Fix: `play_sample` first scans the SFX slots (1..3 — never slot 0, the
  pinned music slot) for a voice already carrying the same resolved sample.
  On a hit it clamps and stores the new volume/pan, calls the new
  `sm64_saturn_scsp_pcm8_update()`, and returns — the rotor does not advance
  and `voices_started` does not increment, so a held sound occupies exactly
  one slot for as long as it is held. `sm64_saturn_scsp_pcm8_update()`
  (`scsp_pcm8.c`) writes only the attenuation (0x0C) and pan/send (0x16)
  words; both level words are now composed by a single shared
  `put_level_words()` helper that the key-on path also uses, so the two paths
  cannot drift. Key-on still happens for a genuinely new sample. Consumer
  impact: `voices_started` no longer counts held-sound refreshes — the new
  `SM64_SATURN_PCM_SFX_REFRESHES_OFFSET` diagnostic word does. It is appended
  after the music diagnostics at `0x7F20`, moving no existing mailbox, ring,
  or diagnostic offset (the v2 protocol layout is frozen), and
  `tools/saturn/probe_audio_timeseries.py` reads it. Regression tests in
  `tools/saturn/pcm68k_model_test.c`
  (`test_repeated_play_refresh_updates_without_rekey`,
  `test_repeated_play_refresh_still_applies_volume_and_pan`,
  `test_distinct_sounds_still_key_on_separately`) are mutation-verified:
  disabling the already-playing check and dropping either level-register
  write each fail the intended test. Two pre-existing tests replayed the same
  sound to fill slots and were retargeted accordingly — the budget test now
  asserts 1 start + 6 refreshes, and the slot-0 pinning test uses three
  distinct sounds.

- `tools/saturn/profiles/sourceboot-bob-demo-v1.json`: reconciled
  `release_config` to the Sprint 1 R1 tuple
  (`features.complete_mario_animation 0`,
  `features.dynamic_actor_closure 0`; pool 208 and semantic audio 1 were
  already correct). Root cause: a plan defect — the transplanted profile
  still pinned the donor's all-features crunch tuple, and
  `bootstrap_sourceboot_identity_spec.py` requires the Make-provided
  config to exactly equal `release_config`, so the sprint plan's mandated
  stage-1 variable set was unbuildable as-committed. Sprint tasks 1–9
  never owned this reconciliation; the plan overlooked the equality gate.
  Discovered and reconciled during the Task 10 stage-1 link smoke
  (`docs/saturn/evidence/reports/sprint1-stage1-link-smoke.md`).

- `tools/saturn/manifests/sourceboot-bob-demo/audio-sourceboot-sfx-v1.json`
  is now committed. Root cause: a transplant gap — the tracked profile's
  `package_descriptors` list references this audio package descriptor, but
  the file was never committed in the donor worktree (it sat untracked
  there), so the recovery branch could not self-build its SFX bundle. The
  descriptor carries only metadata (input paths under
  `build/saturn/audio68k/` and `build/saturn/audio/generated/
  sourceboot-sfx/`); no sample data is embedded, and the path is not
  gitignored (`git check-ignore` clean), so this was purely a missing
  commit.

- Extended the fail-open audio-boot fix to real hardware: the fallback's
  fail-closed contract relied on the modules' workspace pointers reading
  `NULL` when unbound, but both `s_state` statics (and the public
  `gAudioErrorFlags`/`gGlobalSoundSource`/`gAudioRandom` ABI globals) live
  in NOLOAD `.lwram_bss`, which is never crt0-zeroed — on a non-zeroing
  power-up a failed audio init would have left garbage pointers that every
  game audio call then dereferences, worse than the original spin. Each
  module now exposes a tiny reset (`..._semantics_reset`, `..._live_reset`)
  that parks it unbound without trusting current static contents (the live
  module's `deactivate` writes through `s_state`, so it cannot be the first
  touch); `sourceboot_audio_init` calls both before any bind attempt, so
  fresh boots and failed inits both end provably unbound with zeroed ABI
  globals. Also fixed a reviewer-found edge in `live_boot`: a refused
  SET_MASTER enqueue immediately after activation previously returned false
  with the layer still active — it now falls through to the power-off /
  deactivate tail. The stale donor-era ordering assertion in
  `test_sourceboot_cold_stage_return.py` (sound_init-before-live_boot) is
  rewritten to guard the new bind-last order, and a new source-policy test
  asserts both resets precede any bind.

- Audio-boot failure previously hung the console in an infinite `for (;;)`
  spin at the sourceboot audio-init failure site, making a bad SFXB bundle
  or a sound-CPU handshake timeout indistinguishable from a renderer hang.
  It now logs over dbgio, sets an ELF-visible LWRAM breadcrumb
  (`sourceboot_audio_live_failed`), leaves the semantic audio layer unbound
  — every `audio/external.h` entry is a fail-closed safe no-op until its
  workspace binds — and continues boot, satisfying the constitution's
  audio-mutes-only rule. Root cause beyond the spin itself: the old init
  order bound the semantic workspace before the sound-CPU boot, so a late
  failure could have left a live semantic layer feeding a dead mailbox.
  There is no semantic unbind API, so `sourceboot_audio_init` now binds the
  semantic workspace only as the last step of a fully successful init (the
  live bridge already self-deactivates on every `live_boot` failure path).
  Guarded by `test_audio_init_failure_does_not_hang`, which brace-counts the
  audio-init function and its feature-gated caller block so the main frame
  loop's legitimate `for (;;)` stays exempt.

- Sealed the host-only BOB material compiler after independent review. Exact
  admission now covers the evaluated command-local trace and final state for
  every recognized relevant Fast3D command, including ambient/diffuse lights
  and state changes after the final triangle, so unused tail state cannot
  bypass the measured whitelist. Scene closure now resolves each reached
  `gsDPSetTextureImage`/`gsDPLoadTextureBlock` symbol to one declaration and
  checked-in `.rgba16`/`.ia16` PNG, records its path and SHA-256 on the owning
  actor before closure validation/publication, and makes decoding consume the
  already-attested bytes; missing, ambiguous, computed, unsupported, or
  noncanonical paths fail closed. Fast3D scalar shifts also validate the
  uint32 operand and 0..31 count before Python evaluates them, preventing
  adversarial host work without changing historical S64B-v1 Mario bytes.

- Checked target animation stream word-count multiplication before any span
  or pointer use. A hostile `0x80000000` value-count previously wrapped its
  byte length to zero in freestanding C even though the host parser rejected
  it; the target now fails closed with host-equivalent uint32 arithmetic. The
  v2 tile walk also proves each aligned padding endpoint remains inside the
  declared texture payload before inspecting those bytes.

- Hardened the new S64B-v2 host boundary after independent review. V2 now
  rejects structurally valid v1 cores whose required meshlet or primitive
  count is zero, while historical v1 acceptance remains unchanged. The packer
  also preflights the complete promoted core, resource directories, every
  aligned texture/CLUT accumulation, every record/copy span, and final size
  against a checked bound before its sole output allocation. Oversized input
  therefore fails with a named format error instead of growing intermediate
  bytearrays or leaking `MemoryError`; canonical fixture and Mario bytes stay
  exact.

- Made the final two bytes of the S64B-v1 104-byte header an explicit
  zero-reserved field in the version-owned parser. Nonzero padding now fails
  closed instead of being structurally unowned; historical Mario and S64F-v3
  bytes remain exact.

- Corrected generic actor variant selection for families that mix drawable
  models with the explicit non-drawable `MODEL_NONE` sentinel. Numeric model
  identities are now resolved from attested source before drawable GeoLayout
  provenance is required, so a valid unselected `{none, null}` alternate no
  longer poisons `MODEL_METALLIC_BALL`. Drawable variants remain strictly
  validated even when unselected; selected/invalid model-less variants fail
  named without fabricated geometry. Model-ID parsing also accepts repository
  CRLF lines without relying on a trailing comment. S64B schema/runtime and
  historical Mario bytes are unchanged.

- Modeled repository-valid terminal `gsSPBranchList` actor display lists as
  unconditional tail transfers. This fixes strict generic S64B selection
  misclassifying the real explosion actor's final branch as a missing
  `gsSPEndDisplayList`, while preserving ordinary `gsSPDisplayList` call/return
  behavior. Tail targets must be one exact closure-resolved identifier in the
  final command position; suffixes, bad arity, missing/ambiguous targets,
  cycles, and depth overflow fail named, and downstream state/geometry is
  walked rather than omitted. S64B v1's textured-state rejection and the
  historical Mario bytes remain unchanged.

- Hardened actor-source closure discovery after scoped review found three ways
  strict provenance could be bypassed: repository-valid conditional display-
  list branches now seal their reached Gfx sources, with missing/computed/
  malformed targets rejected from exact macro semantics rather than a
  successful symbol-index lookup. Known unmodeled Fast3D source-address forms
  fail named while an explicit compiler-derived allowlist retains known
  scalar/render state; every command absent from the reference, unsupported-
  reference, and state tables now fails regardless of its arguments. Each collection
  now uses fresh actor-definition and root/animation-definition inventories
  rather than process-stale root-only caches. Actor-asset traversal also fails
  with a bounded domain error at depth 256 instead of leaking Python
  `RecursionError`; the closure schema, public ABI, and historical Mario bytes
  remain unchanged.

- Extended the authoritative scene-closure generator to seal every uniquely
  reached actor GeoLayout, display-list, vertex, and light source into each
  record's existing sorted source attestations. This fixes generic S64B
  compilation failing on model data reached from an otherwise attested root
  GeoLayout, while keeping `root_provenance.geo_source` singular and
  root-defining. Missing, ambiguous, recursive, malformed, or computed
  reachability now fails before publication; downstream compilation remains
  closure-only, and historical Mario artifacts remain byte-identical.

- Distinguished the repository's two-argument `LOAD_MODEL_FROM_GEO` and
  three-argument `LOAD_MODEL_FROM_DL` source bindings in the generic actor
  compiler. Valid neighboring direct-display-list commands no longer reject a
  selected GeoLayout, while a selected direct list retains its authoritative
  source layer and feeds that layer through material, opacity, typed-report,
  source-identity, and S64B semantics. Unsupported selected layers and
  malformed, empty, duplicate, conflicting, unterminated, or suffix-tainted
  bindings still fail closed; the closure schema and historical Mario bytes
  are unchanged.

- Bound generic actor model selection to the attested binding-source bytes,
  not just closure metadata: the selected numeric model must now have one
  exact `LOAD_MODEL_FROM_GEO/DL` (or model-ID comment) mapping to the declared
  GeoLayout, while missing, duplicate, conflicting, malformed, and trailing-
  token bindings fail closed. Selected generic animation table and header
  parsers also preserve comma positions and reject leading, trailing, or
  doubled empty fields, preventing malformed source from being normalized into
  a valid S64B identity. Historical Mario parsing and artifact bytes remain
  unchanged.

- Hardened the generic S64B variant compiler after review: the requested
  numeric model ID now selects exactly one attested model/GeoLayout provenance
  instead of merely labeling the primary model's bytes; declared root layouts
  no longer fall back to same-named definitions elsewhere; and selected
  GeoLayout, display-list, vertex, and animation initializers require complete
  token coverage, exact arity, and bounded encoded scalars. Fast3D texture,
  combine, culling, environment, alpha, tile/load, and alternate-light states
  are now rejected because S64B v1 cannot encode them, preventing a validly
  sealed but visually incorrect bank. Expected extraction, Mesh IR, and packing
  failures are translated to the public named actor-variant error contract.

- Matched the S64F v3 host's embedded-S64B geometry validation to the target's
  contiguous tier-0 primitive-order contract, preventing a host-sealed bundle
  from failing master validation. Variant-record decoding now also publishes
  its candidate only after every scalar and span check succeeds, preserving
  zeroed lookup outputs for shortened or corrupted bundle views.

- Reserved worst-case leading alignment headroom in actor-bank dependency
  scratch and aligned the raw `payload + byte_count` address inside that span.
  Either SH-2 lane now binds safely for every address modulo four with the
  exact advertised capacity; one-byte-short and scratch/output overlap remain
  fail-closed. Mario keeps 5,520 usable bytes per lane (11,040 for both) while
  S64B now advertises the honest 11,043-byte worst-case reservation. This
  removes an accidental dependency on the current payload-end alignment
  without moving scratch into the fixed actor arena or changing package kind.

- Made the combined object-pool smoke wait, one VBlank at a time and within a
  fixed startup bound, until both immutable target code and the initialized
  sealed build identity match before telemetry begins. This fixes exact
  release captures failing during normal sourceboot CD handoff while keeping
  the requested 20,100-frame observation interval unchanged and fail-closed.

- Made extracted-asset cleanup hold Task 7's reviewed directory namespace
  guards across validation and mutation, using directory-relative deletion on
  POSIX and exact opened-handle deletion on Windows. This closes the remaining
  ancestor-swap race where a validated generated path could be redirected to
  an outside file before `unlink`; missing platform capability now fails
  closed, and empty-directory pruning still stops below the output root.

- Regenerated and pinned audit v4 around the reproducible round-2 release
  identity after the sealed asset-cleanup generator gained pinned-namespace
  deletion. The measured total remains 700 with both forbidden atan2 callers
  absent, while the contract now fails closed against the new manifest,
  identity, profile, effective configuration, and ELF hashes.

- Revalidated every declared source-closure digest and an unchanged Git HEAD
  at the final release-manifest publication boundary. Release cleanliness now
  reads one NUL-delimited index inventory per repository/submodule instead of
  launching `git ls-files` for every row, preserving bounded status batches
  while making large full-game closures both race-resistant and scalable.

- Bound sourceboot archive creation and every post-link symbol gate directly
  to the attested `sh-elf-ar` and `sh-elf-nm` backends. This removes Yaul's
  PATH-resolved `gcc-ar`/`gcc-nm` wrapper delegation, so an unsealed backend can
  no longer change a release gate; missing or mutated backends fail closed and
  reseal the build identity.

- Pinned native-math audit contracts and route oracles to LF checkouts so
  Windows `core.autocrlf` cannot alter release-identity bytes. A filtered Git
  checkout test now exercises the effective Windows representation rather
  than assuming the current worktree happens to be canonical.

### Added

- Added a deterministic, source-closure-selected generic S64B compiler for
  rigid, articulated, switch, billboard, alpha, and translucent actor
  variants. Reached GeoLayout, display-list, vertex, animation, and binding
  sources now form the canonical per-variant source identity, while malformed,
  unattested, ambiguous, or unsupported source constructs fail by named error
  instead of dropping geometry or selecting a fallback. The shared big-endian
  encoder preserves the historical Mario payload and report bytes exactly;
  scene-level S64F orchestration and production sourceboot wiring remain
  intentionally deferred.

- Added the canonical, deterministic S64F v3 scene-local actor-family bundle
  writer and matching host/SH-2 validators. The additive format binds sorted
  family and drawable-variant directories, canonical metadata, complete S64B
  payloads, per-variant source/payload identities, and the two-lane workspace
  ceiling with fail-closed overflow, padding, mutation, and resolver checks.
  Historical S64F v2 tooling bytes remain unchanged. Production sourceboot
  selection is intentionally not wired yet, so this is a validated data
  boundary rather than a target-runtime activation claim.

- Added a generated, immutable BOB actor-identity registry that binds each
  supported drawable `(model/geo, behavior)` pair to the current S64F family
  record, fully validated payload identity, and build-owned scene-package
  generation. Generation now rejects malformed S64F identity/layout/internal
  digest/record spans, report-to-payload drift, and stale package generations
  instead of trusting only an outer file hash. The Saturn object observer now
  admits registry hits with the complete four-field identity and
  leaves misses—including unsupported families and `MODEL_NONE` controllers—
  fully zero/fail-closed; shared geometry is disambiguated by exact behavior
  identity rather than a model-only fallback. Authoritative frustum, selected
  render-range, switch-case, and opacity evaluation now update the bound typed
  observation at their source-owned geo seams, while the reviewed no-parent
  rule remains intact and generic feature bits never substitute for typed
  state. The two affected MSYS host-test recipes directly execute their fresh
  binaries, avoiding native-Python `/d/...` path rejection so the prescribed
  combined registry/snapshot Make gate is runnable on Windows.

- Added bank-driven actor meshlet preparation over immutable validated S64B
  geometry and selected pose records. Queue and meshlet paths now share one
  renderer-neutral eight-byte output/quarantine ABI, so the fixed 2,718-record
  arena binds directly without casts or copies. Package-declared scratch now
  accounts for two aligned claimant lanes containing pose vertices, lights,
  4x4 joint matrices, positions, and uniqueness bytes; exact/short/overlap
  binding fails closed without consuming the 65,536-byte actor arena. Per-draw
  preparation checks family/model/source hash and render generation without
  rescanning unrelated bank records; Task 2 still owns numeric bank-ID/view and
  scene-generation selection, while the legacy Mario API, split output layout,
  behavior bytes, and feature-off call sites remain unchanged.
  Queue/batch host gates now execute their MSYS-built binaries directly so a
  Windows Python launcher cannot reinterpret `/d/...` fixture paths.

- Made normal release-bound audit-v4 acceptance publish a canonical,
  no-overwrite JSON result after every audit gate passes. This closes the gap
  where `--json-output` was reserved for non-accepting observation mode even
  though the release workflow required a durable acceptance record; v2/v3 and
  producer-commit observation semantics remain unchanged, while v4 output
  aliases and preexisting destinations fail before target tools run.

- Sealed the reproducible identity-v2 BOB release into native-math audit v4
  after one-shot target measurement proved a total of 700 from
  `_game_loop_one_iteration` and no `_atan2_lookup` or `_atan2s` caller. The
  verifier now pins the exact 606-byte contract and release-manifest/ELF
  identity, so later target changes fail closed instead of silently redefining
  the acceptance baseline; immutable v2/v3 contracts remain unchanged.

- Made fresh Saturn release candidates derive the complete US asset set from
  the allowed baserom inside their own generated build tree. The sourceboot
  asset boundary now orders extraction before every raw-asset consumer, keeps
  the legacy root extractor CLI compatible, avoids populating ignored PNGs in
  the checkout, and seals a canonical 1,540-row generated-input inventory so a
  missing or host-dependent extracted byte cannot evade release identity or
  post-link rediscovery. This fixes clean worktrees failing at the first BOB
  texture while preserving the verified `build/us_pc` prerequisite unchanged.

- Added release-bound native-math audit v4 support. The verifier now parses
  version-specific manifest, identity, effective-config, target-profile, and
  exact-ELF hashes; verifies identity-v2 release bytes into a private immutable
  snapshot before any SH tool runs; and can write only an explicitly
  `measured-unsealed` canonical report. A separate one-shot sealer rejects
  mismatched manifests/ELFs, wrong roots, historical identity v1, and either
  forbidden atan2 caller before exclusively creating canonical v4 text. The
  accepted v4 digest was deliberately left unpinned until the exact Task 9
  target was measured, so measurement could not masquerade as acceptance;
  audit v2/v3 files and their historical manifest-free invocation behavior
  remain byte-for-byte unchanged.

- Added deterministic post-link Saturn release sealing and profile-neutral
  deployment staging. The canonical manifest binds the exact ELF,
  `SOURCE.DAT`, ISO, CUE, identity-v2 effective configuration, resolved
  profile, source closure, package set, toolchain attestation, Git revision,
  and closure-clean fact without timestamps, dirty-path listings, or absolute
  host paths. Verification rejects output, embedded-identity, CUE/ISO, or input
  root drift before capture, launch, or staging; staging accepts only a missing
  or empty destination and never copies unsealed siblings. Throughput,
  occupancy, HUD, and desktop-Ymir evidence now require the release manifest
  SHA-256, with occupancy deriving v2 pool capacity from the verified effective
  config while retaining an explicit identity-v1 spec compatibility path. This
  closes the gap where individually plausible artifacts from different builds
  could be combined or published as one release.

- Integrated Saturn sourceboot identity v2 into a guarded five-stage
  assets/discovery/seal/build/post-link pipeline. Compiler dependency scans now
  use the real Yaul C flags/specs with repository prefix normalization, class
  generated and derived inputs without an identity cycle, seal exact source
  closure and live toolchain roots, and recheck C/C++ plus freshly scanned
  assembly dependencies before release sealing. Development remains the
  default; release mode additionally rejects dirty or untracked checked-in
  closure inputs, preventing stale build reuse from becoming a release.

- Composed sourceboot build identity v2 exclusively from the validated target
  profile/package manifests, compiler-derived source closure, toolchain
  attestation, and exact Make configuration. The bootstrap no longer hashes
  broad repository roots, so unrelated capture, test, and evidence edits do
  not reseal a build; stale descriptors, profile/config drift, or generated
  sibling-manifest drift now fail before atomic spec replacement. The nine
  legacy package fields retain their one-to-one class mapping while texture
  remains covered by the aggregate package-set root, preserving v1 parsing.

- Added backward-compatible Saturn build identity v2. New target identities
  preserve the exact 404-byte v1 binary layout as their prefix and append
  target-profile, package-set, and toolchain-attestation roots for a 500-byte
  ABI, while specs without `identity_version` still emit historical v1 bytes.
  Capture tools now read exactly the ELF symbol's declared supported size and
  reject all other sizes; v2 JSON exposes its effective configuration only
  after its canonical SHA-256 matches the embedded digest, preventing truncated
  target proofs or build-tree-path-dependent release evidence.

- Added a deterministic Saturn toolchain-attestation v1 seal. It measures the
  exact invoked Yaul SH-ELF binaries and compiler version plus every
  compiler-discovered external dependency, while serializing only
  component-relative paths so install locations cannot affect release identity.
  Duplicate, case-colliding, missing, unclassified, ambiguous, stale, or
  malformed inputs now fail closed before atomic publication, preventing an
  external SDK move or byte drift from silently reusing a release seal.

- Added a compiler-derived, canonical source-closure v2 seal for Saturn
  builds. It records only classed target inputs and their byte hashes while
  keeping permitted absolute toolchain dependencies outside the serialized
  manifest. Post-build verification reparses actual depfiles, rejects any
  missing/extra/class-owner/external dependency drift, rehashes every sealed
  input, and in release mode checks only checked-in closure paths. This
  prevents stale depfiles, host-path leakage, TOCTOU byte changes, or unrelated
  workspace edits from silently being accepted as a reproducible release.

- Added deterministic Saturn target profiles, strict canonical package
  descriptors, per-class aggregate manifests, and a package-set v2 root. This
  lets the playable BOB demo identify only its selected measured payloads while
  reserving an explicitly non-release-enabled full-game profile; malformed,
  duplicate, escaping, case-colliding, or missing package inputs now fail
  closed instead of reusing a stale seal.

### Fixed

- Bounded candidate-local asset cleanup to strict relative manifest rows and
  the resolved output-root namespace. Absolute, traversal, NUL, symlink, and
  Windows reparse escapes now fail before any deletion, and empty-directory
  pruning stops below the output root; this prevents a malformed extraction
  manifest from deleting or pruning unrelated checkout/host paths.

- Applied the repository prefix-map contract to the separately compiled
  software-float runtime. Main sourceboot objects were already normalized, but
  soft-fp's deliberately independent flags retained absolute checkout paths in
  ELF debug sections; release artifacts from two clean worktrees therefore
  differed despite identical executable/package bytes. File, macro, and debug
  paths now share the same canonical repository spelling without changing
  soft-fp optimization, math semantics, or cross-tool binding.

- Made sourceboot's generated `.incbin` assembly relocatable between release
  worktrees. The first independent candidate pair differed only because five
  texture/sky/fragment directives serialized absolute checkout paths; they now
  use canonical repository-relative operands resolved from the sourceboot Make
  working directory. The generating Makefile is an explicit prerequisite, so
  a retained generated tree cannot silently reuse the old host-bound source.

- Deferred the immutable native-math audit-v2 exact-total overlay during
  release-candidate construction. The first otherwise-verified Task 9
  candidate measured 700 while the historical v2 contract requires 582, but
  audit v4 cannot be selected until two unsealed candidates reproduce. Release
  verification still runs the baseline and route-oracle gates; development
  verification and the explicit historical audit target retain v2 unchanged,
  while release acceptance still requires the newly measured, sealed, and
  pinned v4 contract before staging.

- Reused the bounded, submodule-aware source-closure verifier for the release
  manifest's final Git provenance check. Manifest publication had performed a
  second independent all-path `git status`, hitting Windows `WinError 206`
  after post-link cleanliness already passed; release provenance now reruns
  the exact tracked/clean, pinned-gitlink, and CRLF-normalized checks in bounded
  batches immediately before publication instead of trusting a stale boolean.

- Corrected the release-enabled BOB profile's declared ELF, ISO, and CUE
  basenames to the sourceboot program's real `-e2` outputs. The profile had
  named nonexistent unsuffixed files, so a fully compiled and cleanliness-
  verified target correctly failed manifest sealing; a checked-in integration
  contract now keeps the profile names synchronized with `SH_PROGRAM` while
  leaving the future full-game profile's distinct deployment names unchanged.

- Aligned release-manifest source-owner validation with the canonical closure
  schema. Recipe records deliberately use the schema-defined
  `linker/build-recipe` class as their owner, but the manifest validator had
  rejected its slash as though it were an arbitrary identifier; schema class
  owners are now accepted exactly while malformed arbitrary owners, case
  collisions, and every other input-document check remain fail-closed.

- Bounded release-cleanliness Git status checks into deterministic commands
  below Windows' process command-line limit. The complete checked-in closure
  had previously been expanded into one argv after a successful target build,
  causing `WinError 206` before the release manifest could be sealed; every
  path is still checked exactly once, in closure order, and any dirty batch or
  individually overlong path fails closed. The same bounded check now covers
  relevant pinned-submodule paths for eventual full-game-sized closures.

- Bound Windows release-cleanliness checks to the checkout's CRLF
  normalization instead of whichever Git happens to lead the target-build
  `PATH`. MSYS Git lacks Windows Git's system `core.autocrlf=true` setting and
  falsely marked the clean libyaul recipe checkout modified; cleanliness now
  supplies that status-only normalization explicitly on Windows. Closure
  hashes still cover the exact working bytes, so semantic changes remain
  rejected while tool-selection differences cannot invent dirt.

- Made release cleanliness understand tracked Git submodule inputs without
  exempting `third_party`. For every relevant nested path it now requires an
  enclosing mode-160000 superproject index entry, an initialized checkout at
  that exact pinned commit, and a tracked, clean nested file; it also rejects a
  dirty/staged gitlink while ignoring unrelated submodule dirt outside the
  closure. This lets pinned libyaul recipes pass the same tracked/clean gate as
  superproject files instead of being misreported as untracked.

- Distinguished generated-header children from compiler-search includes while
  sealing copied source assets. `water_skybox.c` legitimately quotes
  `types.h`, but that header lives on the repository include path rather than
  beside the generated C file; the first transitive walk falsely required
  `build/us_pc/bin/types.h`. Missing local children now fail closed for
  generated headers, while generated C targets remain directly inventoried
  and their real compiler-resolved headers stay covered by depfile discovery.

- Restored transitive generated-header sealing for the Saturn source-asset
  inventory. The direct target list included `text_strings.h` but not its
  generated `text_menu_strings.h` child, leaving the compiler-discovered child
  as an untracked generic header at release cleanliness. Verified inventory
  publication now follows quoted includes only while they remain under the
  bounded `build/us_pc` root, fails closed on a missing child, and hashes every
  resulting byte without broadening the generated-input cleanliness rule.

- Made the canonical `build/us_pc` inventory the sole explicit owner of its
  required `water_skybox.c` and `text_strings.h` rows. Those two paths had
  remained in the legacy static generated-input list, so Windows/MSYS path
  normalization correctly exposed duplicate ownership and stopped release
  discovery; filtering them from the static list preserves strict duplicate
  rejection while retaining the same hashed bytes and generated-input class.

- Classified every verified candidate-local `build/us_pc` source asset as an
  explicit generated input before Saturn release sealing. Sourceboot now
  publishes the exact selected asset paths through a canonical, atomically
  written inventory and feeds that bounded list to closure discovery; the
  inventory file is transport-only, while each asset's semantic bytes remain
  hashed and rechecked. This preserves release-mode rejection for dirty or
  untracked checked-in headers while allowing the copied, inventoried ROM-
  derived prerequisite tree to pass the intended `generated-input` exception.

- Matched pre-seal and post-link dependency discovery to Yaul's real `-MD`
  system-header coverage. The previous `-MM` scans excluded headers reached
  through `-isystem`, sealing only four external inputs while actual compile
  depfiles named 103; discovery now uses `-M -MG`, so all GCC and Yaul header
  bytes actually consumed by the compiler enter the external handoff and
  toolchain attestation before release identity is formed.

- Made every post-link preprocessed-assembly dependency scan an independent
  Make target ordered after the linked ELF. A recipe-level `foreach` had joined
  all GCC invocations into one shell command, treating later compiler paths as
  input operands and emitting only the final depfile; release verification now
  requires each fresh scan as an explicit prerequisite before closure
  rediscovery, so no missing assembly input can be skipped or silently stale.

- Gave pinned Yaul packaging helpers a writable candidate-local temporary
  directory. `wrap-error` otherwise fell back to `/tmp`, which is unwritable
  when the build runs under a restricted identity, after the entire ELF had
  compiled and linked. The directory is created under the ignored,
  identity-tagged object tree before `make-ip` and inherited by ISO/CUE hook
  sub-makes; temporary paths and captured stderr stay outside canonical
  identity while the recipe and every semantic package byte remain sealed.

- Bound GCC's compiler, assembler, and linker helper lookup to the exact
  `sh-elf-` tool family with an explicit `-B` prefix, and attest the concrete
  `sh-elf-as` and `sh-elf-ld` files. Pinned Yaul prepends `/mingw64/bin` after
  the outer wrapper starts, so PATH order alone still selected host binutils.
  The binding now survives that nested Make boundary and applies equally to
  dependency discovery, C/C++, preprocessed assembly, final link, and the
  separately compiled software-float runtime. The latter owns an independent
  flag set, so leaving it unbound allowed 75 main objects to compile before a
  late host-assembler failure; every GCC invocation now selects the same
  attested cross-tool helpers.

- Put Yaul's selected cross-tool `bin` directory ahead of MSYS host programs
  in the Windows toolchain wrapper. The SH GCC driver locates its unprefixed
  assembler helper through `PATH`; previously the wrapper selected MSYS's host
  `as.exe`, which rejected SH-2's `-big` option on the first target object.
  MSYS runtime directories still precede inherited PATH, preserving required
  DLL discovery and establishing the correct top-level tool environment; the
  sourceboot GCC `-B` binding below additionally survives Yaul's nested PATH
  rewrite.

- Made sourceboot invoke the build-identity generator from the repository root.
  Identity-v2 specs intentionally carry repository-relative sealed-artifact
  paths, but the sourceboot Makefile previously consumed them from its nested
  directory and failed immediately after bootstrap. Label discovery, directory
  tagging, and output generation now share one root-bound invocation while the
  identity CLI's existing relative-path behavior remains compatible.

- Canonicalized the checked-in release profile's `release_config` key order.
  The late-added `area_id` value was semantically correct but appended after
  `slave_render`, so the byte-strict identity bootstrap correctly rejected the
  profile even after LF checkout normalization. The accepted configuration is
  unchanged; a repository fixture now verifies the real release profile and
  every descriptor it selects are exact canonical JSON bytes.

- Pinned every tracked JSON checkout to LF in `.gitattributes`. Git's Windows
  `core.autocrlf` conversion had changed otherwise-canonical target-profile
  bytes to CRLF in fresh Task 9 worktrees, so identity bootstrap correctly
  rejected the selected profile before compilation. Fresh checkouts now retain
  the canonical bytes stored in Git on every host; the profile, package, and
  release identity semantics are unchanged.

- Bound sourceboot's custom SH inspection-tool paths to the host executable
  suffix. MSYS can launch an extensionless `sh-elf-readelf` command on Windows,
  but the attestation deliberately measures a concrete file and correctly
  rejected that non-file spelling while the installation contains
  `sh-elf-readelf.exe`. Objdump, readelf, and addr2line now select `.exe` only
  under `OS=Windows_NT`; non-Windows paths and all pinned tool bytes are
  unchanged, and later native-math verification consumes the same exact files.

- Deduplicated exact CLI tool paths when composing the sourceboot toolchain's
  canonical binary set. Yaul invokes `sh-elf-gcc.exe` as both compiler and
  linker driver, so treating role repetition as two binary records rejected
  the real pinned toolchain even though the attestation schema measures files,
  not role labels. Every role remains a required explicit argument; identical
  spellings are measured once, while direct duplicate component inputs and
  differently cased aliases retain their existing fail-closed checks.

- Bound compiler-dependency interpretation to the sourceboot Make working
  directory and added a unique, fail-closed alias for explicitly declared
  derived outputs. Real depfiles contain valid local rows such as `main.c`,
  `source_cart.h`, and `../runtime/...`; discovery previously treated those as
  repository-root paths. GCC `-MG` also emits the not-yet-sealed identity
  include by basename, so a unique basename may now resolve only to an exact
  declared derived output, while duplicate candidates are rejected. The same
  base is used for pre-link discovery and post-link rediscovery.

- Applied the existing Windows/MSYS drive-path transport normalization to
  compiler dependencies parsed inside depfiles, not only to canonical path-list
  rows. Windows Python otherwise interpreted GCC's `/d/...` spelling as
  `<current-drive>\d\...`, making an in-repository header look like an absent
  external dependency after all discovery scans. Both dependency resolution
  and canonical classification now share one host-path conversion boundary;
  serialized closure paths remain repository-relative and host-independent.

- Normalized sourceboot-local translation units to absolute paths before the
  bounded compiled-source handoff. Yaul intentionally accepts entries such as
  `main.c` relative to the sourceboot Make directory, but the release closure
  tool resolves its canonical lists from the repository root; passing the raw
  spelling therefore misclassified a real source as missing at `<root>/main.c`
  after all dependency scans. The handoff now preserves the exact file Yaul
  compiled while remaining independent of candidate-worktree location.

- Made hermetic release-mode sourceboot asset preparation verify the complete
  selected `build/us_pc` input set by candidate-local presence instead of Make
  timestamps. Fresh worktree source mtimes otherwise made byte-identical,
  independently inventoried prerequisites appear stale, first rebuilding every
  unrelated host tool and then trying a missing `textconv`. Release candidates
  now reject a missing or root-escaping selected input and let source-closure
  hashing bind its bytes; development builds retain the legacy root Make path
  that materializes stale/missing targets and rebuilds host tools as before.

- Fixed the sourceboot BOB scene delegation to forward its sealed asset root
  through the scene target's nested tile/BSP prerequisites. The initial tile
  bake could otherwise succeed from candidate-local assets and then be repeated
  against the empty checkout root while emitting the scene header.

- Fixed the BOB tile compiler to honor its public `BOB_ASSET_ROOT` override,
  matching the fragment compiler. The hermetic sourceboot caller can now read
  the sealed candidate-local extraction tree; previously the recipe silently
  replaced that input with the repository root and clean release candidates
  still failed after successfully deriving all assets.

- Fixed sourceboot closure discovery and post-link verification on Windows by
  replacing unbounded repeated path arguments with strict canonical path-list
  handoffs. The BOB closure currently exceeds 50 KiB across 228 depfile paths,
  so MSYS could finish every dependency scan and still fail before cleanliness
  verification with `Argument list too long`. GNU Make now writes LF-only,
  sorted list transports directly, and the verifier rejects malformed,
  duplicate, or noncanonical rows before deriving the unchanged semantic
  source closure. MSYS drive spellings are converted only at the Windows
  Python boundary; the list transport itself is not release identity input.

- Fixed Windows target-build dispatch so the documented `mingw32-make`
  wrapper spelling resolves to MSYS2 GNU Make and fails closed below 4.3.
  Sourceboot requires grouped-target syntax, but an MSYS2 installation without
  a same-named executable previously fell through to Qt Make 4.2.1, which
  misparsed Yaul dependency discovery before any SH-2 compilation. Existing
  build commands keep their spelling while now selecting the runtime they were
  documented to require.

- Hardened native-math audit-v4 publication and CLI preflight after review
  exposed three fail-closed gaps. The one-shot sealer now writes and fsyncs a
  privately owned same-directory file before atomically publishing the exact
  held object (handle-based exclusive rename on Windows; held-fd exclusive
  hardlink on POSIX), so a substituted private name cannot become the contract
  and readers observe only absent or complete bytes. Ambiguous private state is
  retained with diagnostics instead of unlinking a potentially replaced path.
  Measurement output now uses the same complete-private, atomic no-clobber
  publication and refuses preexisting outputs, so late aliases introduced
  after tool execution cannot redirect writes. Its preflight also rejects
  lexical, Windows-casefold, symlink, and hardlink aliases of every read input
  before release verification or SH tools, and `--release-manifest` is accepted
  only for parsed audit v4 or explicitly unsealed measurement mode. Historical
  v2/v3 contracts and their manifest-free CLI behavior are unchanged.

- Made atomic Saturn release publication platform-explicit. Linux now selects
  only `renameat2(RENAME_NOREPLACE)`, while macOS and BSD-family hosts require
  libc's directory-relative `renameatx_np(RENAME_EXCL)`; missing symbols and
  other POSIX hosts fail before staging mutation instead of calling a
  Linux-only ABI or degrading to check-then-rename. Windows still removes a
  proven preexisting-empty backup by retained handle. Hosts without equivalent
  identity-conditional deletion retain that empty sibling under the documented
  quarantine prefix and emit its path, trading automatic cleanup for the
  guarantee that staging never path-deletes a concurrently replaced object.

- Closed the remaining exact-release namespace races. Canonical manifests are
  now parsed and hashed only from no-follow opened-file snapshots whose object
  and full ancestor identities are checked around the read. Staging assembles
  and exactly verifies a manifest-last private sibling tree before atomic
  no-replace publication; Windows directory handles pin active namespaces and
  handle-based cleanup removes only a proven empty backup. Concurrent extras,
  replacements, or failed final verification quarantine the complete affected
  namespace and restore the requested missing/empty state for retry instead of
  unlinking a path that may now belong to another process. This prevents
  symlink/junction swaps from redirecting release writes or rollback from
  deleting foreign data; retained quarantines are named in failure diagnostics
  for explicit operator inspection.

- Hardened exact Saturn release handling after review. Verification now
  materializes private manifest-bound output snapshots, and capture, SH-tool,
  staging, and desktop-Ymir consumers use only those verified bytes; a live
  desktop process retains its snapshot until child exit. Staging is
  transactional for missing or empty destinations, rolls back only paths it
  can still prove it owns, preserves concurrent foreign replacements, and
  publishes the manifest last. The writer now semantically binds resolved
  profile configuration and output names, supports real historical identity
  v1 inputs, and rejects host-neutral case/path/alias collisions. Comparison
  now ignores host layout and provenance while deterministically comparing
  canonical identity inputs and output bytes. These changes close TOCTOU,
  partial-publication, cross-platform alias, v1 compatibility, and false
  reproducibility failures without changing the four-artifact release format.

- Corrected hermetic sourceboot dependency discovery after review. C, C++, and
  preprocessed assembly now use distinct scans matching Yaul's real compiler,
  flags, and language-specific specs; every discovery invocation rescans even
  when old depfiles exist; and C++ receives all repository prefix maps. The
  source-closure CLI also rejects aliased closure/handoff output paths before
  writing, preventing configuration drift, eventual full-game C++ inputs, or
  a path alias from producing a stale or self-overwritten identity seal.

- Closed the remaining sourceboot identity publication review gaps. The staged
  target-profile copy is now digest-checked immediately around resolver use and
  again before publication, preventing an unchecked staging generation from
  selecting outputs. Rollback now attempts every target, preserves the original
  publication exception with restore/cleanup diagnostics, and removes owned
  sibling/spec temporary files on success and failure, so recovery trouble no
  longer masks the triggering failure or strands transaction debris.

- Hardened sourceboot identity-v2 composition after review. Source closure,
  target profile, and toolchain-attestation documents are now validated and
  hashed from one immutable byte snapshot, then rechecked with every selected
  source/package input immediately before publication. Generated manifests and
  the canonical repository-relative spec are built in staging and published as
  one rollback-safe set, so validation or replacement failure cannot corrupt
  files referenced by a prior valid spec, while identical repositories at
  different host paths now emit identical spec bytes.

- Hardened Saturn identity v2 after review. ELF identity probes now resolve
  symbol metadata and extract PT_LOAD bytes from one immutable file snapshot,
  v2 root descriptors and recursive CLI JSON reject unknown, duplicate, or
  case-colliding keys, and identity manifests use compact canonical JSON with
  exactly one trailing newline. This closes mixed-generation ELF evidence and
  ambiguous or non-reproducible identity inputs while preserving historical v1
  binary and programmatic descriptor behavior.

- Hardened Saturn toolchain attestation after review: component version strings
  and GCC `--version` output now reject embedded POSIX or Windows absolute
  paths before canonical serialization or atomic publication. This prevents a
  host-specific compiler banner from changing a portable release identity; the
  pinned Yaul metadata and every explicit tool path remain required.

- Closed the remaining toolchain-version path form bypasses from rereview.
  The shared validator now also rejects case-insensitive local `file:` URI
  paths and forward-slash network roots while accepting ordinary relative
  version metadata, so compiler banners cannot encode an install host through
  an alternative absolute-path spelling.

- Hardened source-closure release cleanliness after review. Generated inputs
  outside `build/` now receive the same Git tracking/dirty check as checked-in
  source inputs, while deterministic ignored build outputs remain allowed.
  Every Git-checked closure path is first proven tracked, so ignored or
  untracked headers, sources, recipes, generators, and out-of-build generated
  inputs fail closed instead of being hidden from porcelain status.

- Hardened deterministic target-profile sealing after review: payload identity
  now preflights and rejects duplicate or case-fold-colliding paths across
  descriptors before a missing spelling can hide the collision,
  rehashes every measured descriptor and payload immediately before each
  aggregate publication, and rejects relative output-name escapes. This closes
  cross-package ambiguity and mutation windows that could otherwise publish a
  manifest for bytes no longer present on disk.

### Docs

- Wrote the memory residency campaign plan
  (`docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`): the
  gate before Lane A can resume. Driven by three same-day research inputs
  (all evidence-cited in the plan): the measured 12,408 B flags-on HWRAM
  link deficit, the SlaveDriver/Z-Treme work-RAM technique mining (headline:
  `gObjectPool`'s 240x608 B always-resident footprint vs. the port's own
  attested 64-live actor bound), and the SeamAwareDecimater fidelity-scaling
  scoping (terrain decimation is an LWRAM/cart/VDP1 lever, not an HWRAM
  one). Critical path: occupancy probe -> owner-gated pool capacity cut
  with fail-closed overflow latch -> flags-on textured link gate -> owner
  manual acceptance of the original object-holding crash scenario. The
  decimation prototype runs as a parallel offline lane; identity-wired
  build integration is explicitly deferred to a future plan pending the
  owner's renderer-route decision.
- Reconciled STATE.md, ROADMAP.md, and the post-manual-gate sprint plan
  with reality: both docs' current-lane narratives still described the
  now-finished geo-walk cutover (`5ba8d85c`..`dd81d616`, policy gate 0
  unaccounted recursive calls) and memory/exception repair as in-flight
  work, when six further fix commits (`1eb30fee`, `2c08b009`, `b1f456a5`,
  `a6c2032a`, `16007c4d`, `b9679f57`) and the first owner-played manual
  session on the textured demo build (`id-1335252b7f9383a6`, stable
  2--4 FPS) had already closed that lane. Both docs now name the
  memory-residency campaign as the current lane and its measured
  12,408 B flags-on HWRAM deficit as the gate. Also added a "Discovered
  constraint" note to the sprint plan's Lane A preamble: A2/A4 target
  builds won't link until that deficit closes.

### Changed

- Extended the Saturn object-pool occupancy capture into an artifact-bound
  combined nonvisual smoke harness. Every paused sample now reads the pool,
  signed camera yaw, cart copy probe, exception record, boot trace, and stable
  cadence trace through cache-through SH-2 addresses resolved from one
  DLL-safe `sh-elf-nm` listing. The JSON now reports strict acceptance booleans
  and exits nonzero when any gate fails, so a future flags-on target run cannot
  be mistaken for a passing smoke based on pool occupancy alone. Its route
  description is derived from the sealed identity's replay/live-input modes
  rather than a stale disabled-route assertion. Host decoder tests cover every
  acceptance failure branch. Before post-BIOS sampling, the harness also
  proves target code and its P2-loaded identity against the supplied ELF and
  sealed spec; the required artifact-bound >=20,000-frame target run remains
  open.

- Cut the Saturn sourceboot object pool from its portable 240-slot default
  to the owner-approved 208 slots when SATURN_OBJECT_POOL_CAPACITY=208 is
  requested. The pool consumes 608 B per slot, so the fresh target map
  shrinks gObjectPool by exactly 19,456 B (145,920 B to 126,464 B), enough
  to turn the known 12,408 B flags-on HWRAM deficit into projected positive
  link margin. The capacity is an identity-sealed compiler configuration
  field and the occupancy harness now refuses a report unless the supplied
  sealed identity tuple is embedded in the selected ELF; this prevents a
  source-header fallback of 240 from being attributed to an overridden
  artifact. The 20,100-frame canonical idle-boot remeasurement held the
  138-slot peak with zero allocation failures, while intentionally retaining
  the pickup/hold and action-particle coverage gap as a later gate. An unset
  build remains byte-identical to a forced explicit-240 rebuild.

### Fixed

- Bound the goal native-math audit to the sealed flags-on ELF without
  altering the historical v2 fixture or digest.  The first audit of the
  integrated `id-735756402029c2f4` artifact found that stale callback owners
  included unreachable cutscene tables and omitted the geo-walk render-time
  callbacks; the legacy bootstrap graph also under-approximated literal-JSR
  candidates.  The corrected source-derived v1 route declarations now seed
  every declared dynamic-edge endpoint for analysis, while new audit-contract
  v3 pins the exact ELF SHA-256 and fails before any SH tool runs on a wrong
  artifact.  This preserves v2's historical 582-helper baseline and makes
  the 700-helper goal result meaningful only for its approved artifact.

- Sticky SH-2 DIVU overflow flag silently corrupted `atan2`/the shared 64/32
  divide primitive after the first divide overflow, target-confirmed as the
  mechanism behind BOB's permanent display blackout. The SH-2's on-chip DIVU
  latches its overflow bit in DVCR (`0xFFFFFF08` bit 0) and does NOT
  auto-clear it before the next division -- this branch's own proven-correct
  reference pattern (`src/port/saturn/gpl/slavedriver_projection.h:44-47`, a
  documented close-port of SlaveDriver Engine's DIVU launch) explicitly
  clears DVCR immediately before every `cpu_divu_64_32_set()` call for
  exactly this reason. Two of this branch's native-math primitives were
  missing that clear: `sm64_saturn_atan2_q16_index()`
  (`src/port/saturn/runtime/saturn_engine_math_q16.h`, the code path
  `atan2s()`/`atan2_lookup()` take under `SATURN_ATAN2_VARIANT=2`, the
  Makefile default) and `sm64_saturn_div_s64_s32()`
  (`src/port/saturn/gfx/saturn_render_native_math.h`, the shared divide used
  pervasively across the render/transform pipeline -- matrix constructors,
  vec3 normalize, frustum/terrain clip, demo render). Full causal chain,
  target-confirmed: an overflowing divide anywhere sets the sticky DVCR bit
  -> the next `atan2_q16_index` call reads that stale bit via
  `cpu_divu_status_get()` and force-returns the lookup table's saturated max
  index (1024, i.e. 45 degrees) instead of the real quotient -> BOB's
  radial-hill camera yaw (`sAreaYaw`) froze at that saturated sentinel
  starting post-BIOS frame ~9580-9600 in the pre-fix headless capture and
  never recovered through 30000+ frames -> the presented frame permanently
  stopped updating while game state stayed alive underneath (Mario still
  walking, VDP generations still climbing), matching the owner's manual-test
  artifact's deterministic, unrecoverable display blackout.
  `sm64_saturn_div_s64_s32`'s identical gap is a second exposure on the same
  mechanism, code-inferred rather than separately target-isolated this
  session: `sm64_saturn_vec3_normalize_q16()` maps any of its divide
  failures to `(0,0,0)`, a plausible collapse path if it ever hits camera
  basis vectors. Fix: `*SM64_SATURN_DIVU_DVCR &= ~1u;` immediately before
  each `cpu_divu_64_32_set()` call in both files, matching
  `slavedriver_projection.h`'s exact pattern/comment; the register pointer
  is defined locally in each header (matching address/citation, not a
  shared include) so neither file's existing dependency surface changes.
  Audited every `cpu_divu_64_32_set(` call site in the repo outside
  `third_party/libyaul` (read-only dependency): only these two lacked the
  clear. libyaul's own `cpu_divu_fix16_set()` wrapper has the same gap but
  is third-party/unmodified; this branch's one caller of it
  (`saturn_ir_transform.c`) is a separate, unaffected code path (Gouraud IR
  transform, not on the atan2/render-native-math seam). New regression
  coverage: `tools/saturn/test_divu_overflow_clear_contract.py` -- DVCR is
  real hardware state a host (non-`__sh__`) build cannot exercise, so this
  is a source-text contract (asserts the clear immediately precedes each
  launch) rather than a compiled test; mutation-verified to fail against
  the pre-fix source and to fail again if the clear is later deleted.
  Existing host mutation gates (`verify-render-native-math-mutation`,
  `verify-engine-atan2-q16-mutation`) still pass unchanged, as expected --
  both only exercise the non-`__sh__` software-overflow branch, which this
  fix does not touch. Target re-verified post-fix: rebuilt sourceboot
  (`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
  SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_SEMANTIC_AUDIO=0
  SATURN_RENDERER_PIPELINE=4 SATURN_DIAGNOSTIC_MODE=0
  SATURN_SOURCEBOOT_LIVE_INPUT=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1`, fresh
  identity `id-b8cf2d2e52b7c85f`) and re-ran the exact headless repro (Ymir
  headless, chunked `exec.run_for`, `sAreaYaw` resolved fresh at
  `0x0609DEFA` -- the old `0x0609F5DA` had shifted, as expected after a
  rebuild) sampling every 20 frames across the historical freeze window
  (post-BIOS frames 9500-10000, 26 screenshots) plus every 1000 frames
  through frame 32000 (exceeding the original 30000+-frame proof depth, 37
  screenshots total). Result: `sAreaYaw` moved through 8 distinct values
  across the run (-7135 -> -7141 -> -7147 -> -7055 -> -7166 -> -7555 ->
  -7651 -> -7214), including movement inside the historically-frozen 9500-
  10000 window itself, and never landed on the pre-fix -8192/0x2000
  sentinel; the value does hold flat for stretches (matching the scripted
  replay route's own held/released rotation-input segments, consistent
  with normal deterministic-route behavior, not a re-manifestation of the
  bug) but always resumes moving afterward through frame 32000. No
  screenshot collapsed toward the ~3178-byte near-black pattern (all 37
  ranged 9589-12761 bytes); spot-checked visually at frames 9580, 15000,
  19000, and 31000 -- all show distinct, real rendered content (terrain,
  HUD, Mario at different poses/framing). This also protects the
  in-progress demo-path build (`SATURN_DEMO_PATH=1 SATURN_CAMERA_VARIANT=3`):
  that config does not set `SATURN_ATAN2_VARIANT`, so it resolves to the
  same Makefile default of 2 and shares the identical atan2 exposure this
  fix closes.

### Added

- Instrumented `gObjectPool` occupancy with a fail-closed probe
  (memory-residency-campaign Task 2, feeds OWNER GATE G1):
  `sm64_saturn_object_pool_probe_t` / `g_sm64_saturn_object_pool_probe`
  (`src/port/saturn/runtime/saturn_object_pool_probe.h`, magic/current/
  peak/alloc_failures/frames_sampled, all `volatile uint32_t`, 20 bytes,
  always compiled under `TARGET_SATURN`) is wired at the real allocate/free
  sites in `src/game/spawn_object.c` -- `try_allocate_object()`'s single
  successful-return point (current/peak), `deallocate_object()`'s free-list
  push (current, underflow-guarded), and `allocate_object()`'s true
  pool-exhaustion path (the `find_unimportant_object() == NULL` branch that
  otherwise just hangs forever) -- plus a per-game-loop-tick counter in
  `sourceboot_run_source_tick()` (`src/port/saturn/sourceboot/main.c`).
  New source-text contract test `tools/saturn/test_object_pool_probe_contract.py`
  (11 tests, RED before implementation, GREEN after, including three
  "guard is not a tautology" mutation-detection tests) plus a new
  `verify-saturn-object-pool-probe-contract` Makefile target (wired into
  `verify-all`). A real 21,600-emulated-frame headless Ymir capture
  (`tools/saturn/capture_object_pool_occupancy.py`, 20,100 of them
  post-BIOS-handoff, sampled every 300 frames) against the canonical
  flags-on geo-walk build found **real peak occupancy of 138 objects
  (57.5% of the 240-slot pool) with zero alloc_failures** -- more than
  double the plan's previously-cited 64-live-rendered-actor bound, proving
  the plan's own caution that pool occupancy includes invisible logic
  objects (spawners, triggers, helpers), not just rendered actors. The
  capture is an idle-boot measurement (no live input/route replay in this
  config), so it captures BOB's full static/macro object roster (which
  SM64 spawns at area load, not proximity) but not Mario-movement-
  triggered transients or the pickup/hold interaction itself; reported
  honestly as a measured floor, not a worst-case ceiling, in
  `docs/saturn/evidence/reports/memcamp-object-pool-occupancy-2026-08-09.md`.
  This real number (138) invalidates the plan's own worked capacity
  examples (96, 128 -- both below the measured peak); Task 3's owner gate
  should compute candidates from 138.

- Offline SeamAwareDecimater terrain-decimation prototype (memory-residency-
  campaign Task 7, parallel lane -- no build-system integration, no identity
  wiring, not wired into any `verify-*` target): `tools/saturn/mesh_ir_obj_shim.py`
  converts `sm64-saturn-mesh-ir` v2 <-> Wavefront OBJ (v/vt share an index by
  construction; `texture_tile` and `source` round-trip via material-id/face-
  order side channels, since OBJ has no field for either), driving a pinned
  MIT clone of SeamAwareDecimater
  (`github.com/songrun/SeamAwareDecimater@c69934356ecdb0dd91070a6fc0520cdb0cc4d983`,
  libigl@`4ce917d4` and Eigen@`3.3.7` pinned alongside it, all under
  `work/upstream/seam-aware-decimater/`, untracked per the existing `/work/`
  convention) built via a hand-rolled MSYS2 MinGW Makefile
  (`decimater.exe` SHA-256 `0d04b80a5a98d103501c2b0a7439946febdb06765909f0a1f2eae01848759bd9`).
  IR->OBJ->IR round trip is byte-identical with decimation disabled (proven
  against the real 1,625-vertex/1,101-triangle/18-material BOB mesh, not just
  a fixture). Real 50% and 25% decimation runs (`--strict 2`) are GREEN-twice
  deterministic (four runs, one SHA-256:
  `97e1e94b6be4a308d33128ab22b603fd5fa6b2a1e1422b6120cf0eb5032c53bf`) and,
  surprisingly, identical to each other -- this terrain is 65% boundary edges
  once split into per-material submeshes, so `--strict 2` hits the same
  natural collapse floor well below either target (1,625->1,532 positions,
  1,101->932 triangles). Two real blockers were found and fixed inside the
  shim (not routed around, and the downstream compilers were never modified):
  bowtie (multi-fan) vertices crashing libigl's `circulation.cpp` assertion
  (fixed via geometry-preserving vertex splitting), and two post-decimation
  triangles that became zero-area only after integer requantization (fixed
  via a degenerate-triangle filter with full per-triangle drop records).
  Running the real, unmodified `saturn_mesh_ir.py`/`compile_bob_bsp.py` on the
  decimated IR against real baseline numbers re-verified from the current
  generated files (867 primitives/1,183 BSP nodes, not trusted from the plan
  summary) gives real primitives 867->727 (-16.2%) and BSP nodes 1,183->945
  (-20.1%). Full evidence, hashes, and two isometric wireframe SVGs (before/
  after) at `docs/saturn/evidence/reports/memcamp-decimation-prototype-2026-08-09.md`
  (+ companion `.json`). Identity-wired integration remains explicitly
  deferred (Task 8, separate plan, pending the owner's G3 fidelity verdict and
  renderer-route decision).

- Closure-derived resident audio bundles (task12-completion Task 3):
  `tools/saturn/saturn_audio_package.py` and `compile_saturn_audio.py` accept
  `--closure <scene closure JSON>` and derive that scene's resident bundle
  from `collect_scene_closure.py`'s authoritative declarations instead of the
  hardcoded music-only selection. Join chain: closure `sfx_ids` ->
  `include/sounds.h` `SOUND_ARG_LOAD` declarations (reusing the closure
  generator's one-declaration-per-ID and `SOUND_BANK_*`->lowercase bank-name
  conventions, extended with the soundID operand) -> the preprocessed
  `sound/sequences/00_sound_player.s` channel dyntables (positional
  chan_setbank/chan_setinstr pairing, chan_jump/branch/layer following,
  `>=0x80` = synthesized waveform, `0x7F` = percussion) -> `sound_banks/*.json`
  instruments -> sample records; music comes from the closure's
  `music_sequence_ids` via `include/seq_ids.h`. Every unresolvable link fails
  packaging closed naming the SFX ID; closure-driven bundles record
  `selection`/`sfx_resolution`/closure provenance, and scenes without a
  closure (currently WF) keep the hardcoded music-only fallback, recorded as
  `closure_selection` in the manifest. `Makefile.saturn.mk` wires the flag
  through `compile-saturn-audio` and the `verify-audio-residency` GREEN-twice
  gate via opt-in `SATURN_AUDIO_SCENE_CLOSURE` -- opt-in because the honest
  real-BOB bundle (all 54 closure SFX IDs resolved across 9 instrument banks
  plus music bank 22, 48 samples) needs 679,936 resident bytes against the
  491,520-byte `SM64_SATURN_AUDIO_RESIDENT_LIMIT` hardware contract, so real
  end-to-end packaging fails closed by design (deterministically) until a
  sample-fidelity/residency policy closes the 188,416-byte gap; SFX-only
  (~426 KiB) or music-only (~246 KiB) each fit, their union does not.
  Packager suite grows 8 -> 11 (exact-union selection, fail-closed
  unresolvable IDs/music/bank-mismatch, resident overflow on the closure
  path); batched Task 2 review cleanups landed alongside (unused `Iterable`
  import removed, `pin_m64_size` now emits the real assets.json entry shape,
  deliberate-shadowing comment on the legacy `_load_sequences` guards).

- Closed the real BOB closure's 188,416-byte resident overflow
  (task12-completion Task 3 follow-up, owner-approved this session: "halve
  the sample rate - thats fine"): `saturn_audio_package.py` now 2:1-decimates
  closure-mode SFX sample PCM (drop every other frame, post-AIFF-decode,
  before packaging) and halves the affected samples' `rate` field --
  eligibility is exact (reachable only via a resolved SFX chain, never via
  any scene's whole-bank music inclusion), so music bank 22 and WF's
  hardcoded fallback are provably untouched (spot-checked byte-identical
  against independent AIFF parses). Corrected sub-figures from the prior
  entry's estimate, independently re-derived against the real 660,864-byte
  raw PCM total: SFX-only was 416,112 B (406.4 KiB) at full rate, not
  ~426 KiB/418.0 KiB; music-only is confirmed at 253,952 B (248.0 KiB,
  includes its metadata share) as estimated. Halving drops SFX PCM to
  208,056 B, so the real closure now packages at 473,088 resident bytes
  (462.0 KiB) against the 491,520-byte (480.0 KiB) limit -- 18,432 bytes
  (18.0 KiB) of real margin, confirmed via `compile-saturn-audio
  verify-audio-residency` in closure mode (`--closure` against the real
  generated BOB closure) and two independent compiles hashing byte-identical
  (`AUDIO.DAT`, `audio_manifest.json`, both `*_audio_closure.json`).
  `SATURN_AUDIO_SCENE_CLOSURE` no longer needs to stay opt-in for a
  resident-budget reason (a fidelity/residency policy still gates whether it
  ships by default). Packager suite grows 11 -> 12
  (`test_closure_real_bob_sfx_halving_fits_resident_budget`, generated from
  the real repo via `collect_scene_closure`); the existing
  `test_closure_resident_overflow_fails_closed` synthetic-fixture fail-closed
  coverage is unchanged.

### Docs

- Hedged two overclaiming docstrings in `tools/saturn/m64_decode_walk.py`
  per its accepted quality review (comment-only, no behavior change): the
  module docstring now states the call-delay propagation rule's one-way
  optimism (a callee delaying only on SOME reachable path counts as
  delay-bearing -- a contrived shape no real sequence has; dynamic budget
  behavior is Task 15's domain), and `_delay_free_cycle`'s docstring now
  accurately describes its reported offset as a near-cycle diagnostic
  anchor (Kahn-peel survivors can include between-cycle nodes) rather
  than claiming it always sits on the cycle. Walker suite re-run: 30/30.

- Re-ran the Task 14 headless boot capture against a fresh canonical
  acceptance build now that the `.cart_rodata` orphan-input-section fix
  (`4bd66637`) is landed, confirming the cart-load gate the prior capture
  found is now passed: `g_sm64_saturn_source_cart_probe` reads
  `status=ok`/`stage=ready`/`copied_size==expected_size==2,940,880` at every
  post-identity checkpoint across 38,062 total frames (36,000 past identity
  confirmation, ~5x the prior capture's depth); no SH-2 exception fires;
  VDP1/VDP2 presentation generation climbs continuously (10 -> 1,379);
  `gMarioState` resolves to real, evolving action state; and the final frame
  is a real rendered scene with Mario visible, not black. Independently
  cross-checked with the project's own already-reviewed
  `tools/saturn/capture_sourceboot_boot_trace.py` against a second,
  separately-built ISO, confirming the same result. Also found and
  documented (not fixed -- evidence-only task) a real, previously-unknown
  build-identity nondeterminism defect: `tools/saturn/gen_build_identity.py`'s
  output is not stable across separate `make` process invocations, so the
  project's own `pre-build-iso` hook (run via a freshly re-parsed recursive
  `make`) sometimes stages `SOURCE.DAT`/patches IP.BIN into a *different*
  identity's directory than the outer build is packaging, silently shipping
  an `.iso` missing `SOURCE.DAT` entirely. One affected build's artifacts
  were repaired by hand (tool-only, no source changes) for this capture; a
  second full build converged cleanly on its own. Still open, unchanged from
  the prior capture: the specific "Mario holding an object" scenario cannot
  be exercised by this build (`SATURN_SOURCEBOOT_LIVE_INPUT=0` and
  `SATURN_SOURCEBOOT_ROUTE_REPLAY=0` mean nothing drives Mario), so that
  piece still needs either a scripted-input capture route or a human at a
  live desktop Ymir window. See
  `docs/saturn/evidence/reports/task14-headless-boot-capture-post-cart-rodata-fix-2026-08-09.md`.

- Independently re-verified the `.cart_rodata` orphan-input-section fix
  (`4bd66637`, `src/port/saturn/sourceboot/sourceboot-cart.x`) from a
  from-scratch clean build the review round ran itself -- not the
  implementer's report -- after a prior review round was force-concluded
  before its own independent build finished (only past the identity-assets
  stage). Real numbers read from this round's own build (identity
  `e2-bob-identity-id-58b5304a51e463e9`, same canonical acceptance
  configuration): `sh-elf-readelf -S` reports `.cart_rodata` `SIZEOF` =
  2,940,880 B; `sh-elf-nm`'s `___sourceboot_cart_rodata_start`/`_end` span is
  also 2,940,880 B; a direct `sh-elf-objcopy -O binary
  --only-section=.cart_rodata` reproduction of `SOURCE.DAT` is 2,940,880 B
  on disk -- all three agree exactly, and exactly match the byte count
  `4bd66637` claimed. The link-time `ASSERT` added by that commit therefore
  holds. Budget margins also independently confirmed from the same ELF:
  HWRAM 19,524 B free against the 6,912 B floor, LWRAM 16,384 B free against
  the 16,384 B floor (both `>=`, both PASS) -- matching `4bd66637`'s claimed
  HWRAM margin exactly. The soft-fp/fp-bit substitution gate and undefined-
  symbol gate (`sh-elf-nm -u`) also pass on this ELF. No source change was
  needed; the original fix was already correct. Recorded two sandbox/tooling
  gotchas hit while reproducing this build, with their confirmed workarounds,
  in `docs/saturn/BUILDING.md`, for future review/build rounds run from an
  AI-agent shell sandbox.

- Reconciled `docs/superpowers/plans/2026-08-07-task14-completion.md`'s
  Task 1/4/5/6 status against this session's real, verified outcomes: Task 1
  (2026-08-07 baseline) marked superseded by the real green link below;
  Tasks 4-5 (LWRAM/HWRAM closure) marked moot for the plan's own canonical
  acceptance configuration (`e2-bob-identity-id-fdc1ac9ba25a4779`) -- real
  surplus of 12,612 B HWRAM / 526,128 B LWRAM there, no deficit to close --
  while flagging that a different, heavier `SATURN_DEMO_*` configuration's
  earlier-measured 3,992-byte HWRAM shortfall (predating this session's own
  linker-script fix) remains real, unretested against current HEAD, and out
  of scope for this pass; Task 6 marked real for the green link and
  headless-capture evidence, still open for the manual/visual desktop
  confirmation gate (needs the owner's own eyes). Appended a matching,
  bounded-claims entry to the SDD ledger
  (`.superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md`).
  No source changes. `STATE.md:34-35`'s stability-lane exit condition
  ("the geo source-policy gate reaches zero direct recursive calls") is
  flagged as stale against the real accepted exit state (an allowlist of 2
  structurally-necessary permanent call sites, not a literal zero) for the
  session controller to reconcile separately -- not corrected here, per this
  task's own scope boundary (STATE.md/ROADMAP.md are human-maintained
  narrative docs, out of scope for this pass).

### Fixed

- Moved the VDP2 HUD's lives/coins/stars/timer/power-meter cells from the
  bottom of the frame to the top, where the source game actually draws
  them. The owner's manual screenshot review of the first working HUD
  render (commit `a6c2032a`) caught the defect directly: lives, coins,
  stars, and the timer rendered at the BOTTOM of the screen, when real
  SM64 draws them near the TOP. Root cause was already diagnosed, but not
  fixed, by a 2026-08-07 code-review pass documented in
  `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md` (~line 1149): that
  pass correctly traced `src/game/print.c:391`'s `render_textrect()`,
  which applies an unconditional Y flip (`rectBaseY = 224 - y`) to
  everything routed through `print_text()`/`print_text_fmt_int()` --
  covering lives/coins/stars (source `HUD_TOP_Y=209`, flips to real screen
  y=15) and the timer (source y=185, flips to y=39), both near the TOP of
  a 224-line-tall frame -- but that pass only corrected the explanatory
  comment in `saturn_hud_layout.c`, leaving the actual `HUD_ROW_*` tile
  constants unmoved at the bottom (rows 10-12 of the 20x14 grid).
  Re-derived every row fresh from the real source for this fix, not from
  that prior summary: `HUD_ROW_COUNTERS` moves 12 -> 0 and `HUD_ROW_TIMER`
  moves 11 -> 2, matching the flipped pixel math above. A third group,
  the power meter (health wheel), was flagged by the 2026-08-07 pass as
  "intentionally not pixel-derived, just a coarse-grid choice" and left at
  the bottom (row 10) -- re-deriving it independently found that was
  wrong too: `render_dl_power_meter()` positions it via a real vertex
  transform (`guTranslate` into the same shared HUD projection
  `create_dl_ortho_matrix()` sets up, `guOrtho(..., 0, SCREEN_HEIGHT, ...)`
  with `SCREEN_HEIGHT=240`, a Y-up world convention distinct from
  `print.c`'s own "224" flip constant), so its real screen row at its
  typical resting/visible Y (200) is `240-200=40` -> tile row 2 -- the
  SAME row as the timer, whose columns never overlap it (power meter is a
  single cell at col 8; the timer spans cols 13-18), so
  `HUD_ROW_POWER_METER` moves 10 -> 2 to share `HUD_ROW_TIMER`'s row
  rather than getting its own. `HUD_ROW_CANNON_CAMERA` (row 13, camera
  status icon + cannon reticle) is unchanged: `render_hud_camera_status()`
  draws the camera icon via the *unflipped* `render_hud_tex_lut()` path
  directly at y=205, which genuinely is near the bottom -- that placement
  was already correct. Re-verified zero (col,row) collisions across every
  group that can co-occur with a new, permanent, systematic test,
  `test_layout_no_collisions_across_realistic_snapshots`
  (`tools/saturn/saturn_hud_layout_test.c`, 3,072 snapshot combinations
  covering all `HUD_FLAG_*` bit combinations, cannon/camera/power-meter
  state, and both branches of the stars<100 column-width switch) --
  replacing the 2026-08-07 pass's own 1,638,400-combination brute-force
  spec review, which was run by hand and never committed as a test.
  `tools/saturn/capture_sourceboot_hud_state.py`'s `DEFAULT_EXPECT_ROW`
  updated in lockstep (12 -> 0) to keep the automated in-emulator capture
  checking the cell the layout actually writes now, instead of silently
  false-passing against the old row. All four HUD host suites green
  (`verify-saturn-hud-snapshot/-layout/-layout-mutation/-no-vdp1`) and a
  real headless-Ymir `verify-sourceboot-hud-target` capture (`build-agent2`
  `ymir-headless`) against the canonical HUD build config confirms the
  corrected layout on target.

- Made the VDP2 gameplay HUD actually visible for the first time, closing
  two target-proven defects from the completed Task 9 blank-HUD
  investigation (pixel-exact PND-injection proof in headless Ymir -- the
  mechanisms were confirmed on target before any code changed, not
  hypothesized). Defect 1, wrong pattern-name encoding
  (`src/port/saturn/gfx/saturn_hud_atlas.c`,
  `sm64_saturn_hud_atlas_write_cell()`): the cell write used
  `VDP2_SCRN_PND_CONFIG_1` -- the CHAR_SIZE_1X1 packing (char# bits 11-0,
  `cpd_addr >> 5`) -- but this screen is CHAR_SIZE_2X2 + AUX_MODE_1, whose
  1-word PND holds char# bits 13-2 (`(cpd_addr >> 7) & 0x0FFF`,
  `VDP2_SCRN_PND_CONFIG_3`; verified against the vendored
  `scrn_macros.h:159-161` and `vdp2_scrn_cell.c:347-351` supplement logic).
  Under 2x2 decode the hardware re-scales the CONFIG_1 value by 4, so every
  HUD cell resolved into VRAM bank A0 (the NBG1 sky bitmap) instead of the
  glyph atlas at bank B0 -- the injection experiment rendered the glyph
  pixel-exactly the moment the CONFIG_3 word was poked. Defect 2,
  sprite/NBG0 priority tie (`src/port/saturn/sourceboot/main.c`,
  `sourceboot_vdp2_layers_set()`): all 8 VDP1 sprite groups sat at
  priority 7, tying NBG0's 7; VDP2 resolves the tie sprite-over-NBG0 and
  VDP1 covers the whole frame, so even corrected cells lost everywhere
  except sprite-free regions (also target-proven). Sprite groups are now
  capped at 6 (caller-requested priority honored up to the cap); NBG0 alone
  owns 7. Companion fixes: (a)
  `tools/saturn/capture_sourceboot_hud_state.py` reproduced the CONFIG_1
  packing in `expected_character_number()`, so it false-PASSED the broken
  encoding -- it now derives the CONFIG_3 relationship
  (`(cpd_addr >> 7) & 0x0FFF`) and passes only against the fix; (b)
  NBG0's bank-B0 CHPNDR cycle slots moved t1-t4 -> t1,t2,t4,t5 with
  explicit `NO_ACCESS` in t3/t6/t7 (`sourceboot_init_sky_bitmap()`),
  because T3 is an illegal CPD slot when PND reads at T0 (VDP2 timing rule;
  Ymir's `kLoResPatterns` mirrors the exclusion but renders leniently --
  real hardware does not), and because an unset designated-initializer slot
  is 0x0 = `PNDR_NBG0`, not no-access, which in NBG0's own PND bank could
  move the PND fetch off T0. Verification -- the FIRST real completion of
  the HUD plan Task 9's verification intent
  (`docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`): all four HUD
  host suites green (`verify-saturn-hud-snapshot/-layout/
  -layout-mutation/-no-vdp1`; none encoded the CONFIG_1 expectation -- the
  layout tests stub the atlas writer); canonical acceptance
  `verify-sourceboot` build (identity `id-fb7fe58dc19689a1`) green; and the
  corrected capture tool against that build in headless Ymir
  (build-agent2 `ymir-headless`, 3,600 startup frames, 5,100 total) PASSES:
  the lives cell PND word reads 0x0830 = the CONFIG_3-encoded Mario-head
  character number, and the final screenshot shows the HUD genuinely
  rendered OVER the VDP1 scene for the first time -- Mario-head x04 lives,
  coin x000, star x00, and the power-meter glyph, all pixel-correct over
  Bob-omb Battlefield terrain. Out of scope, tracked separately: NBG1 sky
  priority 0 and the NBG3/dbgio RGB555 restriction. Session gotcha worth
  recording: the stale `YMIR_HEADLESS_EXE` default in `Makefile.saturn.mk`
  points at the `build-agent` ymir-headless (2026-07-18, predates
  `--dram-cart` support), which silently boots with no DRAM cart and parks
  every build -- including a byte-identical known-good one -- in the
  cart-load failure spin; the task14 evidence report's `build-agent2`
  binary is the working one.

- Recovered 5,952 B of HWRAM `.data` by const-qualifying write-once cold
  tables so the sourceboot linker's existing `*sm64-port?*(.rodata.*)`
  rule relocates them to the cart bank, clearing the demo-path build's
  HWRAM TLSF-floor link assert (margin was 5,688 B below the 0x1B00
  floor; it is now 264 B above it, `___end = 0x060FE3F8`). Moves, each
  preceded by a whole-repo write-site trace per the `sSkyboxTextures`
  discipline (commit `0ebd5b05`): `MacroObjectPresets`
  (`include/macro_presets.h`, 2,928 B; read only by
  `spawn_macro_objects()` at area load, no address taken, no writes) and
  the entire `struct CameraTrigger` table family in `src/game/camera.c`
  -- approved candidates `sCamBBH` (1,464 B) and `sCamCastle` (840 B)
  plus, as a same-file, same-mechanism scope extension flagged for owner
  review, the trace-identical siblings `sCamRR`/`sCamHMC` (168 B each),
  `sCamSSL` (120 B), `sCamSL`/`sCamTHI`/`sCamCCM` (72 B each),
  `sCamCotMC` (48 B), and the dead-in-this-port `sCamBOB` (gc'd before
  and after; 0 B). The sibling extension was needed because the fourth
  approved candidate, `gArctanTable` (2,050 B), was skipped as genuinely
  warm -- `atan2_lookup_q16()` passes it to
  `sm64_saturn_atan2_lookup_q16()` on the live TARGET_SATURN per-frame
  `atan2s` path (`src/engine/math_util.c:719`), the same hot-trig class
  as the explicitly excluded `gSineTable` -- leaving the approved three
  440 B short of the floor. Every trigger table is referenced only by
  its definition and one `levels/level_defines.h` row consumed solely by
  `camera.c`'s `sCameraTriggers` spine initializer, and every spine
  access in `camera_course_processing()` is a read (field loads and the
  `event()` call), so the spine's element type became
  `const struct CameraTrigger *` (the 160-B spine itself deliberately
  stays non-const `.data`, outside the approved scope). `gSineTable`
  untouched per explicit exclusion. Also intentionally NOT "fixed":
  `verify-sourceboot`'s SH-2 native-math census fails afterward with
  `bounded route closure has no linked owner: _demo_actor_queue_transform`
  -- a pre-existing defect this unblocking merely exposes (the symbol is
  already absent from the owner's blocked-attempt map
  `e2-bob-identity-id-c272f4c6d93451ef`, fully inlined into
  `demo_actor_admit_compat_wrapper` at -O2 since the feature-off compat
  wrappers landed), which belongs to the native-math census lane, not
  this memory-budget task.

- Right-sized the LWRAM `.lwram_geo_traversal` arena by changing
  `tools/saturn/geo_depth_manifest.py`'s capacity rounding policy from
  next-power-of-two to 16-frame alignment above the requirement
  (max_proven_depth + safety_margin). At real full-game scale the old
  rounding turned requirement 188 (172 proven + 16 margin) into 256
  frames = 4,096 B, which crossed the reserved LWRAM slave-stack floor
  at 0x002FC000 by 784 B and blocked the demo-path manual-test link
  (arena 0x002FB310-0x002FC310, fresh `.map` evidence); the new policy
  yields 192 frames = 3,072 B, clearing the floor with 240 B to spare
  and saving 1,024 B of LWRAM. Owner-approved this session via explicit
  AskUserQuestion, backed by two independent safety nets: the real
  measured end-to-end runtime peak for the complete converted traversal
  is 19 frames (docs/saturn/evidence/reports/
  task14-closure-mario-body-chain-real-depth-2026-08-09.md), two orders
  below the static bound, and the runtime latches
  `SM64_SATURN_GEO_WALK_RUNTIME_OVERFLOW` fail-closed if the static
  bound is ever wrong on hardware. RED-first: extended
  `tools/saturn/test_geo_depth_manifest.py` with the alignment-policy
  case (requirement 34 -> capacity 48, where power-of-two would give 64;
  exact-policy assertion at repository scale; aligned-but-undercutting
  capacity mutations fail closed with a recomputed identity digest), and
  re-based the wave-3 slack check on capacity minus proven depth since
  the alignment policy deliberately removes the old accidental
  power-of-two remainder the check previously consumed. No linker edit:
  `sourceboot-cart.x`'s size assert binds to the generated
  `__sourceboot_geo_traversal_expected_size`, and
  `sourceboot_geo_walk_frames[]` is sized by the generated
  `SM64_SATURN_GEO_TRAVERSAL_CAPACITY`, so both track the manifest
  automatically. `verify-saturn-geo-depth-manifest` and
  `verify-saturn-geo-walk-runtime` both PASS.

- Fixed the sourceboot build-identity nondeterminism first documented in
  `docs/saturn/evidence/reports/task14-headless-boot-capture-post-cart-rodata-fix-2026-08-09.md`
  section 4: one logical `make -f Makefile.saturn.mk verify-sourceboot`
  invocation could split its outputs across several
  `build/saturn/sourceboot/e2-bob-identity-id-*` directories, with the
  worst case a shipped `.iso` silently missing `SOURCE.DAT` (boot-time
  `SM64_SATURN_SOURCE_CART_IMAGE_NOT_FOUND`). Root cause was two
  compounding defects, both traced live this session. (1) Content
  instability inside the hash closure: two different rules write
  `build/saturn/sourceboot/generated/bob_area1_bsp_report.json` -- the
  sourceboot Makefile's report/header rule runs `compile_bob_bsp.py` with
  `--manifest`/`--header` (report carries the fragment-analysis keys),
  while `Makefile.saturn.mk`'s `compile-bob-bsp` (re-run on every assets
  pass through `compile-bob-scene`'s phony dependency chain) ran it bare,
  so the report flipped between two byte-variants on every pass and the
  Make graph never reached a fixed point. (2) Per-parse resealing: the
  identity bootstrap re-hashed the live tree in every fresh parse of the
  sourceboot Makefile -- the main build, Yaul's recursive
  `pre-build-iso`/`post-build-iso` hook sub-makes spawned from the `.iso`
  recipe, and `verify` -- so any closure change between parses (the
  report flip above, or a concurrently-active agent editing `tools/saturn`,
  observed live twice this session) made the `pre-build-iso` hook stage
  `SOURCE.DAT` into a different identity's directory than the `.iso` the
  outer parse was packaging. Fix: `compile-bob-bsp` now passes the same
  `--manifest`/`--header` arguments as the sourceboot rule (one canonical
  report byte-content); the identity is sealed exactly once per logical
  build -- `Makefile.saturn.mk` captures the tag via the sourceboot
  Makefile's new `print-identity-tag` target right after `identity-assets`
  and passes it as `SOURCEBOOT_SEALED_IDENTITY` to the build and (via
  `SOURCEBOOT_IDENTITY_FROZEN=1` read-back of the frozen spec, no reseal)
  to `verify`; a seal-stage parse exports the tag so the hook sub-makes it
  spawns inherit it; any provided tag is asserted against the frozen
  spec's own derivation, so identity divergence inside one build is now a
  loud parse-time error instead of silent mis-staging; and
  `bootstrap_sourceboot_identity_spec.py` no longer hashes
  `__pycache__`/`.pyc` bytecode caches (214 were in the closure --
  derived from `.py` files the closure already hashes byte-for-byte, they
  embed source mtimes and are rewritten by any interpreter import, so
  they added drift, not coverage; what the hash covers is otherwise
  unchanged). Proof, canonical acceptance flags, clean identity-dir state
  each time: runs 1+2 sealed and verified the same identity
  `id-86d0871248d8a89b` end-to-end with byte-identical ELF/`SOURCE.DAT`/
  `.iso`/`.cue`; a concurrent audio-lane commit then legitimately resealed
  the tree (delta attributed to `tools/saturn/m64_decode_walk.py` by
  manifest diff) and runs 3+4 both sealed `id-03363ebec1b504f4`, again
  byte-identical to each other; every run produced exactly one identity
  directory with `SOURCE.DAT` present inside the `.iso` (direct ISO9660
  root-directory parse) at exactly the ELF's `.cart_rodata` size
  (2,940,880 B), so the adjacent cart-size fix also still holds. A
  live-input variant (`SATURN_SOURCEBOOT_LIVE_INPUT=1
  SATURN_SOURCEBOOT_ROUTE_REPLAY=1` added to the canonical flags) built
  the same way into `id-62d0e851516512df`, complete and self-consistent,
  for the owner's manual desktop-Ymir acceptance run.

- Fixed three review findings on the m64 decode-walk packaging commit
  (`82841ccb`), the largest a reviewer-proven residual truncation exposure:
  the walker validates only the sequence-level prefix of an m64 (~17% of
  real sequence bytes; channel/layer script bodies are opaque by design,
  and that scope boundary stays), so a truncation landing entirely in the
  opaque region -- reviewer-proven by cutting `03_level_grass.m64` from
  5,122 to 2,000 bytes -- still sealed into `AUDIO.DAT` with exit 0.
  `tools/saturn/saturn_audio_package.py` now cross-checks every on-disk US
  m64 it consumes against the exact byte size `assets.json` pins for it
  (`sound/sequences/us/NAME.m64 -> [size, ...]`), failing packaging closed
  with the sequence name, actual size, and pinned size on mismatch. Policy,
  from real data: all 34 extracted US m64s carry pins that match disk
  exactly (verified, zero mismatches), so a missing per-sequence pin -- or
  a missing `assets.json` -- also fails closed. New RED test reproduces the
  reviewer's exact cut (temp-copied real file mutated at test time; nothing
  Nintendo-derived committed) and asserts the walker alone still passes the
  truncated bytes, pinning why the size gate exists. Second finding, a
  counting error: the walker docstring, test docstring, and the `82841ccb`
  CHANGELOG entry all said "20 of 34" US sequences never reach a
  sequence-level 0xFF; the real measurement is 19 of 34 (the 20 folded
  generated seq00 into a count of the 34 extracted files that also mentions
  seq00 separately) -- corrected in all three places; the commit message
  itself is immutable history, so the correction is recorded here. Third
  finding: `test_m64_decode_walk.py` enshrined `fb 00 00` (a delay-free
  unconditional self-loop) as valid with a false rationale ("exactly how
  looping music terminates" -- in fact all 19 real loopers carry an `0xfd`
  delay inside the loop, and a delay-free closed cycle exhausts
  `vm_tick_sequence`'s 64-instruction per-tick budget and returns false, a
  fault, on the first tick: `sequence_vm.c:231` loop bound). Took the
  review's preferred fix, not the comment-only fallback: the walker now
  records the control-flow graph it decodes and reports a `delay-free-loop`
  finding for any reachable cycle carrying no delay opcode (0xfd/0xfe),
  counting a `0xfc` call as delay-bearing when its callee's reachable code
  delays (the cycle through the call's return point dynamically executes
  the callee each iteration); all 34 real US m64s plus generated seq00
  still pass, and companion tests lock the with-delay loop shapes (plain,
  `0xfe`, delay-in-subroutine) as valid. Also documented the walker's one
  deliberate strictness deviation from `vm_flow`: out-of-range EU/SH
  relative-branch displacements are rejected unconditionally, while the
  real VM (`sequence_vm.c:203-207`) checks the not-taken condition before
  bounds-checking, making a never-taken branch with a bad displacement
  dynamically legal -- conservative, US-irrelevant (the US set never uses
  these opcodes), now stated in the module docstring. Suites: 30 walker
  tests (was 27), 8 packager tests (was 7), 9 sequence-bank tests, all
  green under `.venv-saturn-tools`; real `compile-saturn-audio` GREEN-twice
  with byte-identical artifacts, all 35 sequences passing.

- Fixed a real cart-load packaging defect that caused the sourceboot cart-load
  safety gate to reject a correct, fully-built `SOURCE.DAT` at boot
  (`src/port/saturn/sourceboot/sourceboot-cart.x`). Root cause: the
  `mario_actor_bank.o` generated by `tools/saturn/emit_actor_bank_c.py`
  places its payload in an input section literally named `.cart_rodata`
  (`__attribute__((section(".cart_rodata")))`) -- the same name as this
  linker script's own OUTPUT section -- but the `.cart_rodata` output
  block's glob rules (`*sm64-port?*(.rodata)` etc.) never matched that
  literal name. GNU ld's orphan-section-placement rule still appended the
  input section to the OUTPUT section of the identical name, but only
  *after* `___sourceboot_cart_rodata_end` had already been assigned,
  silently excluding 596,896 bytes (`0x91ba0`) from the `[start,end)` span
  `source_cart.c`'s `sm64_saturn_source_cart_load()` uses as
  `expected_size`. `SOURCE.DAT` (a straight `objcopy --only-section` of the
  real, complete output section) was always correct at 2,940,880 bytes; the
  runtime probe undercounted it as 2,343,984 bytes and rejected the real CD
  directory entry as `SM64_SATURN_SOURCE_CART_SIZE_MISMATCH`, halting boot
  in the safety-gate spin loop. Fix: added an explicit `*(.cart_rodata)`
  rule to the `.cart_rodata` output block so any input section using this
  naming convention is placed as ordinary script content before the end
  symbol, and added a link-time
  `ASSERT (SIZEOF (.cart_rodata) == (___sourceboot_cart_rodata_end - ___sourceboot_cart_rodata_start))`
  immediately after `} > cart` so any future recurrence of an orphan-placed
  `.cart_rodata` input section fails the link instead of silently passing
  through to a runtime halt only discoverable via a multi-thousand-frame
  headless capture. Verified with two from-scratch clean builds
  (`e2-bob-identity-id-58b5304a51e463e9`, `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
  SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_SEMANTIC_AUDIO=0
  SATURN_RENDERER_PIPELINE=4 SATURN_DIAGNOSTIC_MODE=0`): both builds pass
  `verify-sourceboot`, produce byte-identical ELF/ISO/CUE/SOURCE.DAT
  (SHA-256 verified), and `SIZEOF(.cart_rodata)` (2,940,880 B) now equals
  `___sourceboot_cart_rodata_end - ___sourceboot_cart_rodata_start`
  (2,940,880 B) exactly, matching `SOURCE.DAT`'s real on-disk size.
  HWRAM/LWRAM budget asserts still pass with real, unaffected margins
  (HWRAM: 19,524 B free against a required 6,912 B floor). Pre-existing:
  the mismatch has been present in every canonical build using this feature
  combination since `tools/saturn/emit_actor_bank_c.py` was added
  (2026-08-05), not introduced by this session's other geo-walk/link-order
  work.

- Fixed the real root cause of sourceboot's persistent
  `ld: cannot open linker script file saturn_geo_depth_manifest.ld` link
  failure (`src/port/saturn/sourceboot/sourceboot.specs`,
  `src/port/saturn/sourceboot/Makefile`) -- the defect a prior session
  diagnosed but only worked around diagnostically (copying the generated
  `.ld` fragment into the link's CWD, never committed). `sourceboot.specs`'s
  `*link:` spec entirely replaces GCC's default link spec (no
  `%(old_link)`), so none of the driver's usual `%{L*}` handling applies to
  it: a real `-v` capture of the actual `collect2`/`ld` command line showed
  GCC's built-in `LINK_COMMAND_SPEC` always places this spec's expansion
  (`-T sourceboot-cart.x`) first, immediately after the LTO plugin options,
  while the `%{L*}` cluster it assembles from ordinary `-L` flags --
  including the Makefile's own `-L build/saturn/sourceboot/generated`,
  meant to resolve `sourceboot-cart.x`'s `INCLUDE
  saturn_geo_depth_manifest.ld` -- lands far later, after the startfiles.
  `ld` resolves `INCLUDE` immediately as it parses `-T`, in a single
  left-to-right scan of the command line, so the search path is still empty
  at that point; reordering the flag within the Makefile's `SH_LDFLAGS`
  cannot change this, since GCC's spec engine collects flags into fixed
  template slots, not literal argv position (verified empirically: moving
  the `-L` earlier in `SH_LDFLAGS` never moved it earlier in the real
  `collect2` invocation). Also verified and ruled out: the project's own
  prior-art fix for an identical-shaped problem --
  `sourceboot-cart.x:14`'s `SEARCH_DIR ("$YAUL_INSTALL_ROOT/...")`, which
  makes its own `INCLUDE ldscripts/yaul-c++.x` resolve -- turned out to be
  a red herring, not a working mechanism: `ld 2.44`'s `SEARCH_DIR` does not
  actually expand arbitrary `$VARNAME` environment references (confirmed
  by testing a control variable pointing at a real, existing directory,
  which still failed to resolve); `ldscripts/yaul-c++.x` was always
  resolving via `ld`'s own relocatable-prefix default script directory
  (visible via `sh-elf-ld --verbose`), unrelated to that `SEARCH_DIR` line
  or to anything -L-based.
  The real fix: `sourceboot.specs`'s `*link:` spec now uses GCC's
  `%:getenv(NAME SUFFIX)` spec function to build `-L<generated-dir>`
  directly inside the same spec string, immediately ahead of
  `-T sourceboot-cart.x`, guaranteeing the real command-line order
  regardless of anything the Makefile does downstream. The Makefile now
  `export`s `SOURCEBOOT_GENERATED_LDDIR` (the same
  `build/saturn/sourceboot/generated` path) for the spec's `%:getenv()` to
  read, and drops the now-redundant `-L$(SOURCEBOOT_GENERATED)` from
  `SH_LDFLAGS` (superseded by the spec-level `-L`, keeping only one place
  that owns this search path). Confirmed with a real `-v` capture in an
  isolated repro against the actual pinned toolchain
  (`work/yaul-install/bin/sh-elf-gcc` 14.3.0, `sh-elf-ld` (GNU Binutils)
  2.44) before touching the real specs file: the fix moves `-L` to
  immediately precede `-T` on the real `collect2` command line, and the
  `INCLUDE` resolves.
  - Verified: two independent real `make -f Makefile.saturn.mk sourceboot`
    builds against the actual pinned SH-2 toolchain (MSYS2 runtime at
    `C:\msys64`, not Git Bash's own bundled `/mingw64`/`/usr`, which
    resolves to a different, incompatible MSYS runtime that silently drops
    exported environment variables before they reach `make`/`gcc`/`ld` --
    a second, unrelated environment hazard hit and worked around during
    this verification). Attempt 1: default target params, `-j1`, from a
    build tree with pre-existing partial object directories from many
    prior failed link attempts. Attempt 2: a genuinely clean rebuild after
    `rm -rf`-ing the target's identity-tagged build directory entirely,
    `-j8`. Both attempts compiled all ~230 translation units, linked with
    zero `ld` errors of any kind, and completed the full pipeline through
    `.elf`/`.sym`/`.asm`/`SOURCE.DAT`/`.iso`/`.cue` -- the
    `sourceboot-cart.x` HWRAM/LWRAM budget `ASSERT()`s (separately tracked
    under Task 14 completion Tasks 4-6) both evaluated and passed for this
    default build configuration. No diagnostic workaround (no `.ld`
    fragment copied into the link CWD) was used in either attempt.

### Changed

- Removed `saturn_geo_walk_process_children`'s `sSaturnGeoWalkActive`
  reentrancy guard (`src/game/rendering_graph_node.c`) as dead code, closing
  Task 14 Task 2's real final gap. The guard's fallback (plain recursion
  when a walk was already active) existed to protect against exactly one
  path: `saturn_geo_walk_enter`'s `default:` case
  (`saturn_geo_walk_dispatch_legacy`) detouring into real recursion for a
  still-unconverted node type reached from inside an already-active walk.
  The prior commit (`24b156fe`, same day) converted the last two node types
  that could reach that default case for any real content
  (`GRAPH_NODE_TYPE_START`/`GRAPH_NODE_TYPE_CULLING_RADIUS`) but explicitly
  kept the guard "out of caution" rather than resolving whether it was now
  dead. This closure traces that concretely: `saturn_geo_walk_enter`'s
  switch now has a real case for all 20 node types that can legitimately
  appear as a walk token; the only type left uncased is
  `GRAPH_NODE_TYPE_ROOT`, which by construction never appears as one
  (`geo_process_root` always drives its own children via real recursion
  directly, never through `saturn_geo_walk_process_children`). Every
  `geo_process_*` top-level wrapper function that calls
  `saturn_geo_walk_process_children` (`geo_process_master_list`,
  `geo_process_object`, `geo_process_held_object`, etc.) has exactly one
  call site each, confirmed by grep, all inside
  `geo_process_node_and_siblings`'s own switch -- never reachable from
  `saturn_geo_walk_enter`'s converted cases, which call the
  `saturn_geo_enter_*` helpers directly and never recurse. The runtime
  itself (`sm64_saturn_geo_walk_runtime_run`,
  `src/port/saturn/runtime/saturn_geo_walk_runtime.c`) is a pure iterative
  loop that never re-enters itself. With the only trigger path proven
  unreachable, removed the guard variable, its `if`/fallback branch, and
  the two state toggles; updated the now-stale present-tense documentation
  in `saturn_geo_walk_dispatch_legacy`'s own comment and the wave 1-4
  historical doc block that had (accurately, at the time) argued for
  keeping it.
- Updated `tools/saturn/geo_walk_source_policy_test.py`'s
  `ALLOWED_ENCLOSING_CALL_SITES` from 3 to 2 entries (dropped
  `saturn_geo_walk_process_children`), matching the guard removal above.
  Rewrote the module docstring and the failure-message text to explain the
  new lowest-achievable count and flag a future regression (a new direct
  recursive call site inside `saturn_geo_walk_process_children`) as a
  guard-removal regression, not just a generic policy violation.
  - Verified: `python tools/saturn/geo_walk_source_policy_test.py` reports
    `PASS (2 allowlisted permanent call sites, 0 unaccounted)`.
  - Verified: a real cross-compile of the full `sourceboot` target
    (`make -C src/port/saturn/sourceboot`, MSYS2 toolchain at
    `work/yaul-install/bin`, MSYS2 runtime at `C:\msys64` -- not Git
    Bash's own bundled `/mingw64`/`/usr`, which resolves to a different,
    cygwin-flavored toolchain that silently breaks `make`'s
    environment-variable inheritance) compiles this file (and all ~230
    other translation units) with zero errors, warnings only,
    pre-existing/unrelated to this change. Caught and fixed one
    comment-only bug during that process before it reached this state: a
    literal `*/` inside a new comment's prose prematurely closed the
    surrounding block comment, corrupting subsequent real code into a
    parse error -- reworded the comment; no logic was ever affected.

### Added

- Added `tools/saturn/m64_decode_walk.py` (+ `test_m64_decode_walk.py`, 27
  unittest cases, synthetic fixtures only) and wired it into
  `saturn_audio_package.py`'s `_load_sequences` as the packaging authority
  (Task 2 of `docs/superpowers/plans/2026-08-07-task12-completion.md`): a
  validation-only static reachability walker over m64 sequence-level
  scripts, ported from the 68k sequence VM's traversal structure
  (`src/port/saturn/audio68k/sequence_vm.c` `vm_flow`/`vm_tick_sequence`
  dispatch tables, including the US vs EU/SH operand splits -- US 0xf2
  reserve-notes+u8/0xf1 bare/0xf0 invalid; EU/SH 0xf1+u8/0xf0 bare,
  0xf2-0xf4 relative branches, 0xda/0xdc rejected). It decodes from offset
  0, follows both sides of every conditional plus calls/loops with a
  visited set and a bounded instruction budget, and fails packaging closed
  -- naming the sequence and offset -- on out-of-range or mid-instruction
  branch/call targets, channel-pointer table entries past EOF, unknown
  opcodes, operands truncated mid-opcode, overlapping decode, and control
  flow that falls off EOF. Root cause of the gap: the old byte-scan
  heuristic only rejected literal `FB/FC FF FF` patterns, so Task 1's
  quality review confirmed a sequence truncated mid-opcode sailed through
  packaging; the scan survives only as a cheap prefilter. Finding M-A's
  closure is split (correction recorded in the follow-up fix entry above):
  the walker closes *sequence-level* truncation with a RED fixture, while
  truncation landing entirely in the opaque channel-script region is
  closed by the `assets.json` exact-size pin added in the follow-up
  commit, not by the walker. Judgment call, recorded for
  reviewers: the plan's literal "0xFF reachable on every path" check was
  implemented as "every reachable path terminates at 0xFF, a validated
  jump, or a merge into already-decoded code", because 19 of the repo's 34
  real US sequences -- every looping level-music script -- plus generated
  seq00 end in an intentional 0xfb jump-back loop and never reach a
  sequence-level 0xFF, so the literal check would reject all real music.
  (This entry originally said "20 of the repo's 34", double-counting seq00
  into the extracted-file count; the commit message carries the same
  error.)
  Channel-script bodies stay opaque (pointers range-validated only): the
  VM emits CHANNEL_START events and has no channel interpreter to port,
  and interpretation is Task 15's scope, not this validator's. All 35 real
  repo sequences pass the walker; `compile-saturn-audio` output is
  byte-identical before/after and GREEN-twice, so the walker adds no
  artifact or determinism impact. Also deduplicated the generated-bank
  index parse per Task 1's accepted review: `_extract_generated_seq00` now
  reuses `gen_sequence_bank.parse_sequence_bank` instead of
  re-implementing the TYPE_SEQ layout, with packager-only policy checks
  (35-entry US count, non-empty sequence 00) layered on top.

- Added `tools/saturn/gen_sequence_bank.py` (+ `test_gen_sequence_bank.py`,
  9 unittest cases, synthetic fixtures only): standalone generation of the
  expanded sequence bank (`build/saturn/audio/generated/sequences.bin`, raw
  binary, plus a JSON manifest with per-sequence offsets/sizes/SHA-256)
  without requiring a PC game build (Task 1 of
  `docs/superpowers/plans/2026-08-07-task12-completion.md`). Root cause of
  the audio packager's fail-closed inventory block: `sound/sequences.bin.inc.c`
  is a PC-build product that never lands in `sound/`, while every raw input
  (34 US `.m64`s, committed `sound/sequences/00_sound_player.s`,
  `sequences.json`) already exists on disk -- a missing generation step, not
  a source-data gap. The generator reproduces the decomp's exact assembly
  path: cpp+as+objcopy of `00_sound_player.s` (the decomp `Makefile`'s
  :901-903 assemble rule and :771-773 `.m64` objcopy rule; pinned sh-elf
  toolchain discovered via `--toolchain-bin`, then `$YAUL_INSTALL_ROOT/bin`
  as check-sdk does, then PATH, MSYS-style paths converted), then
  serialization through the repo's own canonical `tools/assemble_sound.py
  --sequences` as a subprocess (reuse mode: dependency -- index table,
  garbage alignment and padding are the reference implementation's bytes),
  pinned to big-endian/32-bit words: the Saturn SH-2/68k consumer's layout,
  resolving the plan's documented text/binary seam in favor of raw bytes.
  `saturn_audio_package.py` (and the `compile_saturn_audio.py` CLI) gained
  `--sequences-bin`: the inventory accepts the generated bank in place of
  the `.inc.c` (old acceptance kept as fallback; both guard messages now
  name the generator as the fix), and sequence 00's catalog payload is now
  the real sound-player script bytes extracted via the bank's index table
  (recorded as `source_range`), no longer the whole generated file as a
  stand-in. New `compile-audio-sequences` make target wired as a
  prerequisite of `compile-saturn-audio`; the `verify-audio-residency`
  determinism gate passes `--sequences-bin` through. Real results:
  `sequences.bin` 114,112 B, 35 entries, seq00 13,456 B at offset 288
  (packager's 1,024 B guard now passes on real repo inputs);
  `compile-saturn-audio` ran GREEN twice with all seven generated artifacts
  byte-identical across runs, and `verify-audio-residency` (C residency
  test + determinism gate) PASS via native `mingw32-make`. Packager suite
  extended to 6/6 (`test_compile_saturn_audio.py`).

- Added `docs/saturn/evidence/reports/task14-closure-mario-body-chain-real-depth-2026-08-09.md`:
  real, run-verified closure evidence for Task 14 Task 2, superseding the
  prior wave-4 report's pre-closure "headline finding" (that Mario's real
  render path detours into real recursion at `START`) now that
  `24b156fe` and this session's guard removal (above) have closed that
  gap. Modeled fully-equipped Mario (moving -- so
  `geo_switch_mario_stand_run` selects the `GEO_NODE_START`-gated
  `mario_geo_render_body` branch, the overwhelming majority of real play
  time -- near LOD range, normal non-metal/non-vanish body, right hand in
  the closed-grip `GEO_HELD_OBJECT` case) by transcribing the real node
  sequence 1:1 against the actual `actors/mario/geo.inc.c` (41 synthetic
  nodes including every real sibling at every level, not simplified away)
  and driving it through the actual, unmodified
  `saturn_geo_walk_runtime.c` (compiled and run, not hand-derived): real
  measured peak **19 frames**, `final_depth=0` (clean, no stranded
  frames), against the manifest's current `capacity=256`/`safety_margin=16`
  (240-frame usable budget) -- **221 frames of real margin**. The real run
  caught and corrected a hand-derivation mistake mid-analysis (expected
  the held-object hand to be strictly deeper than the non-holding one;
  both hands actually peak identically at their `SCALE -> DISPLAY_LIST`
  leaf, with `HELD_OBJECT` one level shallower as `SCALE`'s sibling) --
  the exact reason this report drives the real runtime instead of
  hand-counting. Also attempted a real target link
  (`make -C src/port/saturn/sourceboot`, same flags as the prior sandbox's
  own `SATURN_DEMO_PATH=1`/`SATURN_RENDERER_PIPELINE=4` script): compiled
  clean; link failed at the same previously-recorded
  `ld: cannot open linker script file saturn_geo_depth_manifest.ld`
  defect, now diagnosed to its precise root cause (a real `-v` re-run of
  the captured `collect2` invocation shows `sourceboot.specs`'s `*link:`
  spec places `-T sourceboot-cart.x` at command-line offset 108, while the
  `-L build/saturn/sourceboot/generated` flag that would resolve
  `sourceboot-cart.x`'s own `INCLUDE saturn_geo_depth_manifest.ld` lands
  at offset 525 -- after `-T`, so `ld`'s immediate INCLUDE resolution
  can't see it yet; controlled by GCC's own spec-template placement, not
  reorderable from the Makefile's own flag order). A diagnostic-only
  workaround (copying the generated `.ld` fragment into the link's CWD,
  not committed) pushed past the defect to reveal the real,
  separately-tracked HWRAM budget gap for this specific build
  configuration: `___end=0x060ff498`, `ram` top `0x06100000`, actual
  margin 2,920 bytes vs. required 6,912 bytes -- **3,992-byte deficit**
  (Task 14 completion plan Tasks 4-6 territory, not fixed here).
- Updated `docs/superpowers/plans/2026-08-07-task14-completion.md`'s
  Task 2 real-completion-status note to reflect the real final closure
  (guard removed, real depth measurement, real link attempt) and appended
  an honest summary to the SDD ledger
  (`.superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md`).

- Converted `GRAPH_NODE_TYPE_START` and `GRAPH_NODE_TYPE_CULLING_RADIUS` in
  `src/game/rendering_graph_node.c`'s `saturn_geo_walk_enter` switch onto
  the bounded iterative geo-walk runtime, closing Task 14's real remaining
  gap: both types previously fell to that switch's default case
  (`saturn_geo_walk_dispatch_legacy`), which detours into real, unbounded
  C recursion via `geo_try_process_children` ->
  `geo_process_node_and_siblings`. `GRAPH_NODE_TYPE_START` is the one that
  actually matters -- it is the literal first command of `mario_geo_
  render_body` (`actors/mario/geo.inc.c:1788`) and of every other actor's
  geo layout (`GEO_NODE_START()` confirmed as the entry command of 30+
  `actors/*/geo.inc.c` files and 8 level files via grep), and
  `process_geo_layout()`'s return value for each actor is stored as that
  actor's `Object.header.gfx.sharedChild` (`src/engine/level_script.c`,
  `src/engine/behavior_script.c`), so `GRAPH_NODE_TYPE_OBJECT` (converted
  wave 3) has, until this commit, descended straight into an unconverted
  START and back into real recursion on every object render -- including
  Mario's own body/limb/held-object subtree, 13 real `GraphNode` levels
  deep, during essentially all non-stationary gameplay. This is exactly
  the master-stack-overrun scenario the prior closure report
  (`docs/saturn/evidence/reports/task14-wave4-full-traversal-capacity-
  margin-2026-08-09.md`, cited in this file's most recent `### Added`
  entry below) flagged as still real-recursion-reachable despite all 18
  other node types already being converted. `GRAPH_NODE_TYPE_CULLING_
  RADIUS` carries the identical detour risk (34 `GEO_CULLING_RADIUS`
  occurrences across ~24 actor files, essentially always wrapping real
  `GEO_OPEN_NODE()`/.../`GEO_CLOSE_NODE()` content per a 20+-file sample,
  e.g. `actors/toad/geo.inc.c`'s full body chain) and converts in this
  same commit. Verified against the real struct definitions
  (`src/engine/graph_node.h:139-142`, `:342-347`): both types are purely
  structural -- no function pointer, no per-visit side effects -- so their
  new `saturn_geo_enter_start`/`saturn_geo_enter_culling_radius` helpers
  just hand back `node->children`, with no leave action needed (same
  no-leave shape as `ORTHO_PROJECTION`/`BACKGROUND`/`DISPLAY_LIST`, not
  the matrix-stack-push types). Neither type ever had a dedicated
  `geo_process_*()` wrapper to extract from (confirmed by grep:
  `geo_process_node_and_siblings`'s own switch has never had a case for
  either, and still doesn't -- that switch and its `geo_try_process_
  children` default fallback are unrelated to and unchanged by this
  commit). With both added as real cases, `saturn_geo_walk_enter`'s
  default case is now unreachable for any node type that can legitimately
  appear as a walk token; the only type it still has no case for is
  `GRAPH_NODE_TYPE_ROOT`, which by construction never appears as one
  (`geo_process_root` always drives its own children via real recursion
  directly, never through `saturn_geo_walk_process_children`). The default
  case and `sSaturnGeoWalkActive`'s own real-recursion reentrancy-guard
  fallback are both deliberately kept, not removed -- the former as a
  defensive fallback (matching how the eleven-type sub-wave before this
  one already treated it), the latter because it remains the correct,
  safe behavior for its one call site (nested reentry from within an
  already-active walk), unrelated to the default-case detour this commit
  closes.
  - Verified: `mingw32-make -f Makefile.saturn.mk -j1
    verify-saturn-geo-walk-runtime verify-saturn-geo-depth-manifest` and
    `python tools/saturn/geo_walk_source_policy_test.py` all PASS (policy
    script: `3 allowlisted permanent call sites, 0 unaccounted`, unchanged
    from before this commit -- this closure removes a *second*,
    previously-unaccounted-for recursion path reachable only through
    `saturn_geo_walk_enter`'s own default case, not one of the three
    call sites that script tracks inside `geo_process_node_and_siblings`
    itself). Also ran a real `sh-elf-gcc -fsyntax-only` check against
    `rendering_graph_node.c` using the actual sourceboot `SH_CFLAGS`
    (extracted via `mingw32-make -p -q` in
    `src/port/saturn/sourceboot/`, toolchain at
    `work/yaul-install/bin`): 0 errors, only 4 pre-existing-style
    `-Wcomment` warnings from intentionally-escaped `*\/` sequences
    elsewhere in this file's doc comments (one of the four newly added by
    this commit's own doc comment, in the same pre-existing style).
- Added `docs/saturn/evidence/reports/task14-headless-boot-capture-cart-load-blocker-2026-08-09.md`:
  the first real automated headless-Ymir boot capture against the
  genuinely green-linked Task 14 canonical acceptance build
  (`e2-bob-identity-id-fdc1ac9ba25a4779`). Pushed depth to 7,462 real
  emulated frames since BIOS handoff (5,400 past target-identity
  confirmation), in bounded ≤600-frame `exec.run_for` chunks. Found: no
  SH-2 exception ever fired (`sourceboot_exception_record.magic` stayed
  `0x00000000` throughout), but also no VDP1/VDP2 presentation activity
  at all -- `main()` halts permanently in a deliberate `for (;;) {}` at
  the pre-cart-load safety gate. Root-caused via
  `g_sm64_saturn_source_cart_probe` (`SM64_SATURN_SOURCE_CART_SIZE_MISMATCH`)
  and independently cross-verified by parsing the built ISO's own
  ISO9660 directory record directly: this build's packaged `SOURCE.DAT`
  is 2,940,880 bytes, but its own linked ELF `.cart_rodata` section
  expects exactly 2,343,984 bytes -- a real, previously-undiscovered
  596,896-byte build-packaging mismatch, unrelated to the geo-walk
  recursion work, that blocks this identity from ever reaching the game
  loop under headless emulation. Cross-checked with the project's own
  already-vetted `capture_sourceboot_boot_trace.py` (unmodified) to rule
  out a harness-specific bug: identical stuck-at-`main-entry` result.
  Honest conclusion: this capture is inconclusive about whether the
  original Mario-holding-object master-stack-overrun crash is fixed,
  because execution never reaches that code path; it does prove no SH-2
  exception occurs before the cart-load gate, and it surfaces a new,
  real blocker for whoever picks up Task 14's next increment.

### Fixed

- Fixed `tools/saturn/geo_walk_source_policy_test.py` (the
  `verify-saturn-geo-walk-source-policy` gate), left permanently red by
  the wave 4 sub-wave 1 commit (`ac37a009`) that finished the per-node-type
  handler cutover: the script still hard-asserted **zero** direct recursive
  `geo_process_node_and_siblings()` calls in `rendering_graph_node.c`, but
  that commit's own CHANGELOG documents 3 call sites as permanently
  out of scope by design (`saturn_geo_walk_process_children`'s
  `sSaturnGeoWalkActive` reentrancy-guard fallback, `geo_try_process_children`'s
  generic children-only bridge, and `geo_process_root`'s top-level kickoff) --
  running the script raised `AssertionError` and exited 1 on that HEAD, found
  via direct execution while re-reviewing that commit. Rewrote the check to
  allowlist exactly those 3 call sites by enclosing function name (resolved
  via each call's nearest preceding column-0 function-definition line, not
  just a raw count) and still fail on any direct recursive call found
  outside that allowlist, or on a missing/duplicated allowlisted site --
  both remain real regressions. Verified the new check both accepts the
  current HEAD (which has exactly the 3 allowlisted sites and nothing else)
  and rejects a locally re-injected stray direct call in an unrelated
  handler, confirming it still does its job.
  - While re-running this gate for real to confirm the fix (not just
    reading the script), found a second, older, unrelated defect in the
    same file: its trailing `assert "saturn_geo_walk_runtime_frame_t" in
    text` sanity check has been dead/unsatisfiable since the script's
    original commit (`d49c8799`, predates the wave 1-4 handler conversion
    entirely) -- that exact identifier never existed anywhere in
    `rendering_graph_node.c` (missing the `sm64_` prefix, and a spurious
    `_frame` that doesn't belong; the real, instantiated type is
    `sm64_saturn_geo_walk_runtime_t`, declared at the local `walk` variable
    in `saturn_geo_walk_process_children()`). Corrected the string to match
    so this sanity check actually verifies what it claims to. This is
    reported separately from the reviewer's confirmed 3-call-site defect
    above because it predates and is independent of the commit under
    review.
  - Verified: `python tools/saturn/geo_walk_source_policy_test.py` now
    prints `geo walk source policy: PASS (3 allowlisted permanent call
    sites, 0 unaccounted)` and exits 0 (both via the repo's
    `.venv-saturn-tools` interpreter, matching how
    `make -f Makefile.saturn.mk verify-saturn-geo-walk-source-policy`
    invokes it, and via the system `python`); `test_geo_walk_contract.py`
    and `test_geo_depth_manifest.py` still PASS, confirming no regression
    to the other geo-walk verification gates. This gate remains
    deliberately excluded from `verify-all` (per its own prior
    documentation), so this fix does not change the aggregate test target.

- Corrected a stale accounting comment in `rendering_graph_node.c` (above
  the Task 14 wave 3 action-code enum) that claimed converting
  `geo_process_object`/`_parent`/`geo_process_held_object` drops
  `geo_walk_source_policy_test.py`'s remaining-call count from 19 to 13
  -- it omits the `sSaturnGeoWalkActive` reentrancy guard's own real
  fallback recursion call (added by the same wave), so the real,
  script-verified count is 14, not 13. Caught while fixing this: a
  first attempt at the correction spelled the dispatcher's name out
  contiguously in the new prose, which the policy script's raw
  (non-comment-aware) text scan counted as a phantom 15th call site --
  the original comment had dodged this by accident, via a line-wrap
  that splits the identifier across two lines. Reworded to avoid the
  literal pattern instead of relying on incidental line-wrapping, and
  left a note for future editors of that comment block.

### Added

- Added `docs/saturn/evidence/reports/task14-wave4-full-traversal-capacity-margin-2026-08-09.md`:
  closure-verification for the geo-walk recursive-to-bounded cutover
  (waves 1-4), independently re-deriving (not re-trusting) two decisions
  the wave 3/4 commits had already made. **Guard disposition**: confirmed
  `sSaturnGeoWalkActive`'s real-recursion fallback (kept, not removed) is
  not dead code by reading `saturn_geo_walk_enter`'s full switch (no case
  for `GRAPH_NODE_TYPE_ROOT`/`START`/`CULLING_RADIUS`, all three fall to
  the legacy bridge) and finding both types genuinely, commonly reachable
  in shipped content: `GEO_CULLING_RADIUS` is the first command in
  `actors/whomp/geo.inc.c` and 24 other actor files, and `GEO_NODE_START()`
  is the first command of `mario_geo_render_body` -- the branch
  `geo_switch_mario_stand_run` (`src/game/mario_misc.c:343-352`) selects
  during every non-stationary gameplay frame, i.e. essentially all real
  play time. **Capacity re-measurement**: drove the real
  `saturn_geo_walk_runtime.c` (extending wave 3's own empirical-probe
  method, per that report's own explicit "should re-run this same method"
  note) through the actual push shapes for all 18 now-converted node
  types plus the 3 permanent detour types, across four real/representative
  scenarios; found real measured/hypothetical peaks of 5-14 frames against
  manifest capacity 256/margin 16 (226-235 frames of slack -- capacity-safe
  by a wide margin), but also found the headline result: **Mario's actual
  live in-game body/limb/held-object chain still runs via real C recursion
  today during normal (non-stationary) gameplay**, because it detours off
  the bounded array one level below `OBJECT`, before ever reaching the
  `ANIMATED_PART`/`HELD_OBJECT` chain the original "Mario holding
  something" master-stack-overrun scenario was about -- the bounded
  array's margin is real but is not the safety proof for that scenario,
  which remains governed by the pre-existing, unrelated
  `geo_depth_manifest.py` native-stack mechanism, unchanged by this whole
  effort. Also attempted a real target link per Task 1's baseline command;
  could not reach the previously-recorded `ld: cannot open linker script
  file` step this session to confirm or deny it, blocked earlier by a
  different, this-sandbox-specific toolchain issue (this MSYS2 install has
  no `mingw32-make.exe` of its own; the only available one, Qt's bundled
  `mingw32-make`, resolves an `awk` during recipe execution that cannot
  parse a path-normalization one-liner in Yaul's shared
  `build.post.bin.mk`) -- not fixed (`yaul-install` is a read-only
  dependency directory); link status remains unconfirmed, for a reason
  distinct from the previously-recorded defect.
- Updated `docs/superpowers/plans/2026-08-07-task14-completion.md`'s
  Task 2 checkboxes to reflect real completion status (all four wave
  steps done, with a real-completion-status note explaining the 3
  allowlisted call sites and pointing at the new capacity-margin report)
  and appended an honest summary of waves 1-4 to the SDD ledger
  (`.superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md`).

- Added `docs/saturn/evidence/reports/task14-budget-baseline-2026-08-07.md`
  (Task 1 of `docs/superpowers/plans/2026-08-07-task14-completion.md`): a
  fresh from-source link attempt at current HEAD (`dd31e2ad`) compiled
  cleanly but failed *before* reaching the `sourceboot-cart.x` memory-budget
  asserts, on `ld: cannot open linker script file
  saturn_geo_depth_manifest.ld` -- confirmed the file exists on disk with a
  timestamp preceding the failing link step, and confirmed the link command
  does carry `-L` to its generated-headers directory, so this is a real,
  reproducible linker-script/build-config defect (most likely
  `sourceboot.specs`'s `*link:` override replacing rather than chaining
  `%(old_link)`, though that was not isolated further since this task makes
  no source changes), not a fabricated result and not the budget assertion
  itself. Because no fresh `.map` exists at HEAD, the report instead uses
  the most recent `.map` in the worktree that *did* reach assert evaluation
  (07:37 local today, 3 commits stale -- predates the
  `465fb8b0`/`fce3f4b5`/`0ebd5b05` HWRAM-reduction commits) to record real
  measured margins: HWRAM short by exactly 3,128 bytes and LWRAM over by
  exactly 784 bytes, both matching the plan doc's cited pre-reduction
  baseline exactly. This confirms the pre-reduction baseline but does
  *not* confirm the plan's reconstructed post-reduction estimate
  (~2,512 B HWRAM), which remains unconfirmed by any fresh link. The report
  also ranks the top 25 HWRAM `.bss`/`.data` occupants from that same map
  by size and owning object file, categorized by mobility (SCU/DMA-fixed,
  CPU-only mutable, write-once/cart candidate, libyaul-owned) for Tasks 4-5
  to consume once a fresh link is available to re-confirm the current gap.

### Changed

- Converted the first 4 of 24 direct recursive `geo_process_node_and_siblings()`
  call sites in `src/game/rendering_graph_node.c` off the SH-2 C call stack
  and onto the bounded LWRAM-backed iterative runtime
  (`saturn_geo_walk_runtime.h/.c`, `sourceboot_geo_walk_frames`), wave 1 of
  Task 2 in `docs/superpowers/plans/2026-08-07-task14-completion.md`:
  `geo_process_master_list`, `geo_process_ortho_projection`,
  `geo_process_perspective`, and `geo_process_camera` now call a new shared
  `saturn_geo_walk_process_children()` engine instead of recursing. Each
  handler was split into an `_enter`/`_leave` pair (setup vs. post-child
  restoration) bound to the runtime's enter/dispatch/leave phases per the
  design-correction constraint in `progress.md:436-441` (16-byte SH-2
  continuation frame, no node-filtering shortcut); the new engine's
  `ops.enter` dispatch mirrors `geo_process_node_and_siblings`'s full
  node-type switch and bridges every not-yet-converted type to its existing,
  unmodified handler function by name (never through the tracked function
  name), so any node type reachable beneath a converted handler's children
  is still handled correctly. A new `saturn_geo_walk_sibling_of()` helper
  replicates the original's circular-ring `iterateChildren`/wraparound
  semantics (switch-case selected-child termination, self-looped single
  nodes, and `node->parent->children` as a time-invariant ring-head
  reference) without needing to thread the chain's head pointer through the
  runtime's frame fields.
  - **Wave scoped to 4, not ~6, for a documented reentrancy-safety reason**:
    `GRAPH_NODE_TYPE_SWITCH_CASE` and `GRAPH_NODE_TYPE_LEVEL_OF_DETAIL` were
    deliberately excluded this wave. Each `saturn_geo_walk_process_children()`
    call takes a **fresh** `sm64_saturn_geo_walk_runtime_init()` over the one
    shared `sourceboot_geo_walk_frames` span and drains to completion before
    returning; this is safe only because this wave's four types are the
    level_geo.c-authored top-level scene skeleton (root-only placement,
    `gCurGraphNodeMasterList`'s pre-existing re-entrancy guard, at most one
    camera per branch) and can never appear nested inside a still-unconverted
    handler's subtree. `SWITCH_CASE` and `LEVEL_OF_DETAIL` are, by contrast,
    pervasively authored *inside* actor/object geo layouts (e.g. cap-state
    and eye-blink switches) alongside still-unconverted types
    (`OBJECT`, `TRANSLATION_ROTATION`, `ANIMATED_PART`, ...); converting
    either before every type that can contain them is also converted would
    let a nested instance reinitialize the shared LWRAM frame span while an
    outer instance is still mid-drain, silently corrupting its in-progress
    continuation frames. Both remain correctly handled via the legacy bridge
    (unchanged `geo_process_switch`/`geo_process_level_of_detail`, real
    C recursion, unaffected by this walk instance) and are deferred to the
    wave that also converts every type that can nest beneath them.
  - Verified: `tools/saturn/geo_walk_source_policy_test.py` now reports 20
    remaining direct recursive calls (24 - 4); `verify-saturn-geo-walk-runtime`
    and `verify-saturn-geo-depth-manifest` PASS; a real SH-2 cross-compile
    (`sh-elf-gcc -fsyntax-only` with the exact sourceboot `SH_CFLAGS`/
    `sourceboot.specs`, `-Wall -Wextra -Wshadow -Wunused` etc.) of
    `rendering_graph_node.c` is clean with zero warnings/errors. No target
    link, Ymir, or manual evidence is claimed by this wave; the memory-budget
    link (Tasks 1/4/5/6 of the same plan) remains open and unrelated to this
    change.
- Converted 1 more direct recursive `geo_process_node_and_siblings()` call
  site (19 of the original 24 now remain), wave 2 of Task 2 in
  `docs/superpowers/plans/2026-08-07-task14-completion.md`:
  `geo_process_background` now calls `saturn_geo_walk_process_children()`
  instead of recursing, split into a `saturn_geo_enter_background()` helper
  (no leave phase needed -- the handler never touches the matrix stack or
  any global requiring post-child restoration) and registered as a real
  `GRAPH_NODE_TYPE_BACKGROUND` case in the shared `ops.enter` dispatch
  (removed from the legacy bridge switch it used to fall through to).
  `GEO_BACKGROUND` is authored only in `levels/*/areas/*/geo.inc.c` (zero
  occurrences under `actors/`, confirmed by grep across the full tree) and
  always as `ORTHO_PROJECTION`'s child (converted wave 1), so it is
  level-authored top-level scene skeleton in the same sense as wave 1's
  four types and carries no reentrancy risk.
  - **Wave scoped to 1, not ~6, after finding a real architectural blocker
    for every other remaining call site.** `saturn_geo_walk_runtime.c`'s
    `push()`/`sm64_saturn_geo_walk_runtime_run()` (read in full this wave)
    supports exactly one child subtree, one optional dispatch, and one
    optional leave per node ENTER event -- there is no mechanism to resume
    a node for a *second*, independently-restored child subtree.
    `geo_process_object`, `geo_process_object_parent`, and
    `geo_process_held_object` each process TWO sequential child subtrees
    per node (a `sharedChild` subtree under a temporary `->parent` alias
    that must be cleared strictly before an unrelated `->node.children`
    subtree begins) -- this does not fit the established single-child
    enter/dispatch/leave pattern used by every handler converted so far.
    Every other remaining type -- `TRANSLATION_ROTATION`, `TRANSLATION`,
    `ROTATION`, `SCALE`, `BILLBOARD`, `ANIMATED_PART`, `SHADOW`,
    `DISPLAY_LIST`, `GENERATED_LIST`, `SWITCH_CASE`, `LEVEL_OF_DETAIL` --
    is authored pervasively inside actor geo layouts (verified per-macro
    against `actors/*/geo.inc.c` vs. `levels/*/geo.inc.c`, e.g.
    `GEO_DISPLAY_LIST`: 89 actor files vs. 341 level files; `GEO_SCALE`:
    67 vs. 8; `GEO_ANIMATED_PART`: 56 vs. 1) and is therefore reachable
    only beneath `OBJECT`'s `sharedChild`; none of them can be safely
    converted before `OBJECT`/`OBJECT_PARENT`/`HELD_OBJ` are, for the same
    reentrancy reason wave 1 excluded `SWITCH_CASE`/`LEVEL_OF_DETAIL`
    (`progress.md:436-441`). Converting any of them now would let a nested
    occurrence, reached via the still-real-recursion `OBJECT`/
    `OBJECT_PARENT` bridge from within an already-active outer drain (e.g.
    `CAMERA`'s), reinitialize the shared `sourceboot_geo_walk_frames` span
    mid-drain. Resolving this needs either a runtime extension (a second
    child-subtree slot per frame) or an owner-approved design
    accommodation -- recorded as a blocker rather than forced through with
    a shortcut.
  - Verified: `tools/saturn/geo_walk_source_policy_test.py` now reports 19
    remaining direct recursive calls (24 - 5 across waves 1-2);
    `verify-saturn-geo-walk-runtime` and `verify-saturn-geo-depth-manifest`
    PASS. No target link, Ymir, or manual evidence is claimed by this wave.
- Extended `saturn_geo_walk_runtime.h`/`.c` to support two-subtree nodes,
  resolving the wave 2 blocker above (`geo_process_object`,
  `geo_process_object_parent`, `geo_process_held_object` each walk a
  `sharedChild` subtree, run a boundary side effect, then walk a second,
  unrelated child subtree -- a shape the single-child `enter()` result
  could not express). This is a runtime-only extension: no handler is
  converted by this change, so `geo_walk_source_policy_test.py` still
  reports exactly 19 remaining direct recursive calls.
  - `sm64_saturn_geo_walk_runtime_enter_t` gained `second_child`
    (`uintptr_t`), `boundary_action` (`uint16_t`), and `boundary_required`
    (`bool`). `second_child` is walked strictly after `child`'s entire
    subtree (and deferred dispatch, if any) drains; `boundary_required`
    optionally fires a leave callback (with `boundary_action`) between the
    two subtrees; `leave_required`'s existing leave callback (with
    `leave_action`) now fires after everything -- child, dispatch,
    boundary, and second_child -- drains, not just after child, when
    `second_child != 0`. `second_child` is ignored whenever `child == 0`
    (no meaningful "second" subtree without a first); a dedicated
    regression test (`test_two_child_ignored_when_child_zero`) pins this
    down since it is an easy invariant to get backwards.
  - `sm64_saturn_geo_walk_runtime_run()`'s admitted-node push sequence now
    branches on `child != 0 && second_child != 0`: the existing
    single-child push order is unchanged (moved into an `else`, byte-for-
    byte identical -- confirmed by diff) and a new branch pushes
    (bottom-to-top, so LIFO pop/fire order is child subtree, dispatch,
    boundary leave, second_child subtree, final leave, sibling): sibling
    continuation (shared, pushed before the branch), final leave, second_
    child ENTER, boundary leave, dispatch, child ENTER.
  - The real (and only) wave 1/2 construction site,
    `rendering_graph_node.c`'s `saturn_geo_walk_enter()`, builds this
    struct by field assignment through a pointer (`result->child = ...`)
    on a caller-owned `{ 0 }`-initialized local, not by designated-
    initializer struct literal as originally assumed going into this
    task -- confirmed by reading the real call site rather than the
    assumption. Because C field access by name is layout-order-
    independent and `run()`'s `sm64_saturn_geo_walk_runtime_enter_t result
    = { 0 };` zero-initializes every member including the three new ones,
    that call site needed no changes; verified both by inspection and by
    an isolated host-side proxy compile of the identical field-assignment
    pattern against the extended header (`-std=c11 -Wall -Wextra -Werror`,
    zero warnings, and the new fields observed zero at runtime).
  - Verified: extended `geo_walk_runtime_contract_test.c` (3 pre-existing
    cases unchanged + 6 new second_child cases) PASSES; a 4-mutation sweep
    of the new push-order logic (dropping the `child != 0` guard,
    unconditionally pushing the boundary leave, swapping the final-leave/
    second_child push order, and dropping the deferred-dispatch push) was
    caught by the corresponding new test in all 4 cases (0% survival) then
    reverted. `geo_walk_source_policy_test.py` (19, unchanged),
    `verify-saturn-geo-walk-runtime`, and `verify-saturn-geo-depth-manifest`
    all PASS with no change to their own outcomes.
- Fixed 4 code-quality findings from the review of the two-subtree runtime
  extension above, before building on it (`saturn_geo_walk_runtime.h`/`.c`,
  `tools/saturn/geo_walk_runtime_contract_test.c`):
  - Added `test_two_child_sibling_fires_after_second_child_and_leave`: none
    of the 6 existing two-subtree tests combined a node's own sibling
    continuation with `second_child`, so nothing pinned down that the
    sibling fires only after `second_child`'s entire subtree (and any
    final leave) resolves, not right after `child`'s subtree the way it
    would for a single-child node.
  - Strengthened `test_two_child_overflow_reports_capacity` with an exact
    `walk.depth == 2` assertion for its capacity-2 scenario (traced by
    hand against `push()`'s exact call order: the final-leave and
    second_child pushes succeed, the boundary-leave push is the one that
    overflows, stranding depth at 2) -- previously it only checked that
    `overflowed`/`fail_reason` were set, which would also pass if the
    stranding mechanism silently changed to a different (still "detected
    somehow") shape.
  - Documented `sm64_saturn_geo_walk_runtime_leave_fn`'s declaration and
    the `ops.leave` field (`saturn_geo_walk_runtime.h`): for a two-subtree
    node this one callback now fires with two conceptually distinct
    meanings (a BOUNDARY call between the subtrees, a FINAL call after
    everything), distinguished only by which action code the caller
    chose -- callers must keep those codes numerically distinct.
  - Hoisted the duplicated `if (result.leave_required) push(...)` block in
    `sm64_saturn_geo_walk_runtime_run()` above the two-subtree/single-
    subtree branch split (it only used values already available before
    the split); a pure refactor, push order unchanged.
  - Verified: `verify-saturn-geo-walk-runtime` PASSES (9 cases, up from 8).
- Converted the last 3 of the original 24 direct recursive
  `geo_process_node_and_siblings()` call sites in
  `src/game/rendering_graph_node.c` -- `geo_process_object`,
  `geo_process_object_parent`, `geo_process_held_object` -- to the bounded
  runtime, using the two-subtree extension above, wave 3 of Task 2 in
  `docs/superpowers/plans/2026-08-07-task14-completion.md`. Each handler
  was split into a small `saturn_geo_enter_*` helper (returning the node's
  two child pointers, or a `saturn_geo_object_children`/`_object_parent_
  children`/`_held_object_children` struct with a couple of extra flags
  for `OBJECT`) shared between the handler's own top-level function (still
  reachable via real recursion or `geo_process_node_and_siblings`'s
  switch, driving `saturn_geo_walk_process_children()` directly on each
  child list -- never on `node` itself, which would double-process this
  node's own sibling since that continuation is already owned by whichever
  caller reached it) and a new case in `saturn_geo_walk_enter`'s switch
  (for when the node is reached as a child within an already-active walk).
  5 new action codes (`SATURN_GEO_BOUNDARY_OBJECT_PARENT`,
  `SATURN_GEO_BOUNDARY_OBJECT`, `SATURN_GEO_LEAVE_OBJECT_FINAL`,
  `SATURN_GEO_LEAVE_OBJECT_COMBINED`, `SATURN_GEO_BOUNDARY_HELD_OBJECT`) in
  the existing shared enum distinguish `saturn_geo_walk_leave`'s boundary
  vs. final calls; `_COMBINED` folds both jobs into one callback for the
  (real, always-taken) case where a node has only one subtree at a given
  visit, since the runtime's single-subtree path fires only one leave call
  then. `geo_process_object`'s admission is gated on `areaIndex ==
  gCurGraphNodeRoot->areaIndex` exactly as before; its final leave (matrix
  pop, actor-observation end, anim-state reset, `throwMatrix = NULL`) now
  fires whenever admitted, even when neither child ends up walked (not
  visible) -- matching the pre-conversion code exactly, where that cleanup
  ran unconditionally once inside the outer `if`.
  `geo_process_held_object`'s matrix-stack push/pop is asymmetric versus
  the other two: the push happens in `enter()` strictly before child A is
  walked, but the matching pop happens in the boundary/leave callback
  strictly after child A's subtree drains -- re-verified against the real
  source rather than assumed, since the task description flagged this as
  the one place a wrong guess would silently corrupt matrix state.
  - **Found and fixed a real reentrancy hazard beyond what the two-subtree
    extension alone addressed** (not merely a frame-cost question):
    converting `geo_process_held_object` makes it reachable, in every real
    actor, via a real-recursion detour through still-unconverted
    "skeleton" types (`GEO_ANIMATED_PART`, `GEO_SCALE`, `GEO_SWITCH_CASE`)
    nested beneath a now-converted `Object`'s own `sharedChild` --
    verified against the real shipped `actors/mario/geo.inc.c`,
    `GEO_HELD_OBJECT` is reached only via exactly those types, 13 levels
    deep from the layout root. Absent a guard, that detour would let
    `geo_process_held_object()` call `saturn_geo_walk_process_children()`
    again while an outer walk (e.g. `CAMERA`'s) is still active many real
    C stack frames up, silently reinitializing the shared
    `sourceboot_geo_walk_frames` array mid-drain and corrupting the outer
    walk's still-pending frames -- genuine memory corruption, not a
    capacity problem. Independently confirmed via a second-opinion consult
    (Codex) before implementing the fix. Fixed with a new
    `sSaturnGeoWalkActive` non-reentrancy guard on
    `saturn_geo_walk_process_children()`: any call arriving while it is
    already `true` is, by construction, reached via exactly this detour,
    and falls back to plain `geo_process_node_and_siblings()` recursion
    instead of touching the array -- this subtree's unconditionally-
    correct pre-conversion behavior, and strictly non-regressing (a
    subtree reached this way already used real recursion before this
    wave). This means the realistic "Mario holding something" case does
    **not** yet get the bounded-stack benefit (its held-object subtree
    still runs via real recursion, identical depth/behavior to before this
    wave) -- that requires a future wave converting the skeleton types
    too, at which point this guard's fallback stops firing for that path
    automatically, with no further changes needed in `geo_process_object`/
    `_parent`/`geo_process_held_object`.
  - **Real call-site count is 14, not the 16 (19 - 3) originally
    estimated going into this wave**: `geo_process_object`,
    `geo_process_object_parent`, and `geo_process_held_object` each
    contained *two* literal `geo_process_node_and_siblings(` call sites
    (`sharedChild` and `children`), not one like every other handler type
    -- removing all three handlers' direct calls (6 sites, not 3) would
    give 19 - 6 = 13, and the `sSaturnGeoWalkActive` guard's fallback
    above (`geo_process_node_and_siblings(children)`, a new, deliberate,
    narrowly-scoped call) adds exactly 1 back: 19 - 6 + 1 = 14. Verified
    directly (`geo_walk_source_policy_test.py` reports "14 direct
    recursive dispatcher calls"), not assumed from the arithmetic.
  - Verified: `verify-saturn-geo-walk-runtime` and
    `verify-saturn-geo-depth-manifest` PASS (see also the new manifest
    test below); a real SH-2 cross-compile
    (`sh-elf-gcc -fsyntax-only -std=c11` with the exact sourceboot
    `SH_CFLAGS`/`sourceboot.specs`/`YAUL_CFLAGS_shared` include set,
    `-Wall -Wextra -Wshadow -Wunused -Wduplicated-branches` etc.) of
    `rendering_graph_node.c` is clean with zero new warnings/errors (2
    pre-existing, unrelated `-Wcomment` warnings on wave 2's own doc
    comment lines 1779/1808 are unchanged). No target link, Ymir, or
    manual evidence is claimed by this wave; the memory-budget link
    remains open and unrelated to this change.
- Added `test_wave3_two_subtree_object_chain_has_real_capacity_margin` to
  `tools/saturn/test_geo_depth_manifest.py`, closing the real capacity-
  margin gap flagged against the two-subtree runtime extension: the
  manifest generator has no model of the iterative runtime's own frame
  cost, so its "PASS" alone proved nothing about capacity safety once
  `geo_process_object`/`_parent`/`geo_process_held_object` actually started
  using the two-subtree path. Investigating that gap is what surfaced the
  reentrancy hazard above; the hazard, not the frame-cost multiplier, was
  the dominant risk, and the guard that closes it also bounds the real
  frame cost to a small, scene-complexity-independent constant (every
  not-yet-converted type is confined to real recursion, off the array,
  regardless of depth). Two throwaway host-side C harnesses (not
  committed) empirically drove the real `saturn_geo_walk_runtime.c`
  through the exact push shapes `saturn_geo_walk_enter` now produces for
  an `OBJECT_PARENT` -> 3 live `Object`s chain, reading `walk.high_water`
  directly: **5 frames** peak for the shape this codebase's real, verified
  behavior actually produces (`OBJECT_PARENT.node.children` and every live
  `Object.node.children` are provably always `NULL` -- traced to
  `geo_add_child`, the only writer of `.children` anywhere in the
  codebase, which is never called with a `GraphNodeObject`'s `.node` as
  the *parent* argument), **8 frames** padded for the theoretical case
  `node.children` were ever non-`NULL` (never observed, but not assumed
  away). Both numbers are now asserted against the manifest's real,
  freshly-computed capacity/margin (`256 - 16 - 172 = 68` frames of
  existing slack today), not hardcoded, so the check stays meaningful if
  the actor/level asset set changes. Full derivation, the reentrancy chain,
  and the empirical probe transcripts are recorded in
  `docs/saturn/evidence/reports/task14-wave3-geo-walk-capacity-margin-2026-08-07.md`.
  Also corrects the originating review's "up to 3x per level" estimate:
  measured directly from `run()`'s push order, the real worst-case factor
  for a two-subtree node's `child`-direction cost is up to 2x the old
  single-child path's, not 3x.
- Converted the remaining 11 unconverted node-type handlers in
  `src/game/rendering_graph_node.c` -- the "skeleton" types wave 2
  deliberately deferred -- off real recursion and onto the bounded
  runtime, the final sub-wave of Task 2 in
  `docs/superpowers/plans/2026-08-07-task14-completion.md`:
  `geo_process_level_of_detail`, `geo_process_switch`,
  `geo_process_translation_rotation`, `geo_process_translation`,
  `geo_process_rotation`, `geo_process_scale`, `geo_process_billboard`,
  `geo_process_animated_part`, `geo_process_display_list`,
  `geo_process_generated_list`, and `geo_process_shadow`. Each was split
  into a `saturn_geo_enter_*` helper (returning the node's one child
  pointer to descend into, matching each pre-conversion handler's exact
  side-effect ordering) and a matching case in `saturn_geo_walk_enter`'s
  switch; the six types that unconditionally push a matrix-stack slot in
  enter (`TRANSLATION_ROTATION`, `TRANSLATION`, `ROTATION`, `SCALE`,
  `BILLBOARD`, `ANIMATED_PART`) also gained a `saturn_geo_leave_*` helper
  and a new action code in the existing shared enum
  (`SATURN_GEO_LEAVE_TRANSLATION_ROTATION` through
  `SATURN_GEO_LEAVE_ANIMATED_PART`) so the pop always balances the push
  regardless of whether the node had children, exactly as the
  pre-conversion code did. `geo_process_animated_part`'s animation-state
  globals (`gCurAnimType`, `gCurrAnimAttribute`, ...) are deliberately
  left unrestored in the new leave helper, matching pre-conversion
  behavior: that state is meant to flow downward into the child subtree
  and persist. `geo_process_switch`'s selection callback and
  `geo_process_level_of_detail`'s distance gate now run inside their
  `saturn_geo_enter_*` helper exactly as before, unconditionally ahead of
  the child-presence check; `saturn_geo_walk_sibling_of`'s existing
  switch-case special case (no sibling chaining for a selected child,
  added in wave 1) needed no changes. Re-reading every one of these
  eleven handlers in full (not relying on a prior research pass alone)
  confirmed all are true single-child shapes with no two-subtree case
  among them, and no ordering hazard: none of them call
  `saturn_geo_walk_process_children` themselves, so converting this whole
  group at once could not recreate the wave-1/2/3 class of "fresh walk
  starter reached before its containing type converts" hazard -- all
  eleven now funnel through the same shared `sSaturnGeoWalkActive`
  type-agnostic reentrancy guard every previously-converted type already
  used.
  - Because this was the last group of node types the legacy bridge
    (`saturn_geo_walk_dispatch_legacy`) dispatched real cases for, that
    switch is now collapsed to its `geo_try_process_children` default
    fallback (still needed for `ROOT`/`START`/`CULLING_RADIUS`, which
    neither switch ever gave a named case); its own comment was rewritten
    to describe the two remaining, deliberately out-of-scope, non-per-
    node-type call sites instead of an exhaustive per-type absence list
    that no longer applied.
  - **Real remaining call-site count is 3, not 0**:
    `geo_walk_source_policy_test.py` reports "3 direct recursive
    dispatcher calls" after this change (down from 14), and all three are
    the ones flagged out of scope before this wave started -- the
    `sSaturnGeoWalkActive` guard's own deliberate real-recursion fallback
    (unchanged, one call site), plus the two non-per-node-type sites wave
    2's own accounting paragraph already named and never converted:
    `geo_try_process_children`'s generic children-only bridge and
    `geo_process_root`'s top-level kickoff call. Converting either of
    those is a materially different, larger change (turning
    `geo_process_node_and_siblings` itself into a bounded-walk entry
    point) than converting a per-node-type handler, and was out of scope
    for this sub-wave.
  - Verified: `verify-saturn-geo-walk-runtime` and
    `verify-saturn-geo-depth-manifest` PASS (both independent of this
    file's per-node-type dispatch, unaffected by this change); a real
    SH-2 cross-compile (`sh-elf-gcc -fsyntax-only -std=c11` with the exact
    sourceboot `SH_CFLAGS`/`sourceboot.specs`/`YAUL_CFLAGS_shared` include
    set, `-Wall -Wextra -Wshadow -Wunused -Wduplicated-branches` etc.,
    resolved via `.yaul.env` and a prior build's generated headers) of
    `rendering_graph_node.c` is clean with zero new warnings/errors (the
    same 2 pre-existing, unrelated `-Wcomment` warnings on wave 2's own
    doc comment lines are unchanged, unmoved by this wave's insertions
    above them).

### Fixed

- Fixed code-quality issues a reviewer found in the CLUT16 baking-mode
  commit (bc08fb27, Task 1 of `docs/superpowers/plans/2026-08-07-saturn-vdp2-clut.md`):
  - `bake_bob_sky.py`'s `bake()` and `bake_clut16()` shared an
    identical dimension-check/offset/edge-clamp block by copy-paste, while
    `bake_clut16()`'s docstring claimed the two paths "cannot desync on
    canvas geometry" -- untrue, since nothing forced a fix to one function
    into the other. Factored the block into a new `_replicate_canvas()`
    helper both functions call, making the claim actually true.
  - `bake_clut16()` mapped colors to CLUT indices with
    `index_by_color.get(value, 0) if (value & 0x8000) else 0`, a silent
    fallback, instead of the strict `mapping[value]` subscript this
    codebase's other `quantize_clut16` call sites use
    (`bake_bob_tiles.py`, `bake_bob_bsp_fragments.py`), which intentionally
    raise `KeyError` on an unmapped color. Every sample this canvas
    produces is unconditionally tagged opaque (bit 15 set, alpha is
    discarded -- pre-existing in `bake()`), so the fallback was always
    dead; switched to the strict subscript so a future refactor that
    desyncs the histogram from the indexed pixels fails loudly instead of
    silently mis-mapping.
  - `test_bake_bob_sky.py`'s two CLUT16 tests fed a solid single-color
    4x4 image through `bake_clut16()` and asserted only lengths -- a
    solid-color source produces one CLUT index for every pixel, so
    `pack_clut16`'s nibble order can be flipped without changing the
    output byte, and `quantize_clut16`'s median-cut box-splitting loop
    never ran beyond its first iteration. Added a four-color-stripe test
    that independently derives the expected packed bytes and palette
    (calling `quantize_clut16` directly as an oracle, not `pack_clut16`)
    and asserts full equality -- verified this catches a nibble-order
    flip in `pack_clut16` by mutation-testing it directly (test fails,
    then reverted). Also added tests for `bake_clut16()`'s two error
    branches (odd total texel count, source larger than output) and a
    new `BobSkyBakeTests.test_bob_sky_bake_clut16_is_vdp2_sized_and_deterministic`
    in `test_tools.py` that bakes the real multi-color `water.png` sky
    asset through the CLUT16 path (mirroring the existing RGB1555 test),
    so quantization on realistic gradient input is exercised, not just
    small synthetic fixtures.

  No behavior change to the default `rgb1555` path or to `bake_clut16()`'s
  output on real input (`test_tools.BobSkyBakeTests` still passes
  unchanged); `tools/saturn/test_bake_bob_sky.py` now has 6 tests (up
  from 3), all passing.

- Repaired a corrupted `CHANGELOG.md` entry from the CLUT16 baking-mode
  commit (bc08fb27): the edit inserting the new "Added a CLUT16 baking
  mode to the BOB sky tool" bullet had deleted the summary line of the
  adjacent pre-existing "Added project-owned SH-2 exception trampolines"
  bullet while leaving that entry's continuation paragraph attached
  directly beneath the new entry with no bullet separator, producing one
  garbled item that switched topics mid-paragraph. Restored the missing
  summary line and the blank-line list-item boundary between the two
  entries by diffing against parent commit 0ebd5b05's intact copy; no
  other changelog content altered.

- Shrank `SM64_SATURN_HUD_LAYOUT_MAX_CELLS` from 40 to 24
  (`src/port/saturn/gfx/saturn_hud_layout.h`), recovering 128 bytes of
  HWRAM `.bss` (`sourceboot_hud_publish_state.last_cells[40]` -> `[24]`,
  8 bytes/cell). Part of a memory-budget audit (2026-08-07) that
  exhaustively traced every branch of `sm64_saturn_hud_layout_build()`:
  the real worst case across every simultaneously-reachable HUD group
  (lives 4 + coins 5 + stars 4 + timer 6 + camera/power 4 + cannon 1) is
  24 cells, not 40 -- the original 40 was an unjustified round number (no
  measured-data comment like this codebase's other capacity constants
  have). `push_cell()`'s existing bounds check means this fails safe (a
  glyph silently not drawn) rather than corrupting memory if the trace
  is ever wrong; host test suite re-run and passes unchanged (11/11).

- Baked sourceboot's boot-time sky gradient to a compile-time constant
  (`tools/saturn/gen_sourceboot_sky_gradient.py`, generating
  `saturn_sky_gradient_generated.h`; `src/port/saturn/sourceboot/main.c`,
  `Makefile`), recovering 448 bytes of HWRAM `.bss`
  (`sourceboot_sky_gradient[224]` deleted). Second finding from the same
  2026-08-07 memory-budget audit: `sourceboot_init_sky_gradient()` filled
  this array with pure integer arithmetic once at boot, DMA'd it to VDP2
  VRAM, and never touched it again, so it never needed to be a mutable
  runtime buffer. The generator reproduces the original fill loop exactly
  (verified line-for-line against a real host-compiled copy of the
  original C loop, all 224 lines match) and packs RGB1555 words using
  this codebase's existing hardware-verified bit order
  (`extract_mario_textures.saturn_rgb1555`), not a re-derivation from the
  union's compiler-defined bitfield layout. The array is now
  `static const`, which the linker's existing automatic
  `*sm64-port*(.rodata)` rule (`sourceboot-cart.x`) already routes onto
  the 4 MiB DRAM cartridge instead of HWRAM -- no new linker wiring
  needed. A `_Static_assert` in `main.c` guards against the generated
  header's line count drifting from `SOURCEBOOT_BACKSCREEN_LINES`. Could
  not get real `sh-elf-gcc` verification (no cross toolchain present in
  this worktree, unlike a prior session's environment); verified instead
  via the standalone Python test suite (8/8 passing) and a host-gcc
  syntax/round-trip check of the generated header in isolation.

- Qualified `sSkyboxTextures` as `SkyboxTexture *const` in the
  `SATURN_SOURCEBOOT` branch of `src/game/skybox.c`, recovering 40 bytes
  of HWRAM `.data` (linker auto-routes it to the cart via the existing
  `*sm64-port*(.rodata)` rule). Fourth finding from the 2026-08-07
  memory-budget audit: this Saturn-specific 10-entry pointer spine
  (all ten entries alias the same water skybox while the multi-level
  package is pending, per the existing comment) was fully compile-time
  literal with exactly one read site (`skybox.c:278`, value-read only,
  never reassigned) and no other reference anywhere in the tree. The
  pointee type was already `const`; only the outer array needed the
  qualifier.

### Added

- Added a CLUT16 baking mode to the BOB sky tool
  (`tools/saturn/bake_bob_sky.py` gains `bake_clut16()` plus
  `--texture-format {rgb1555,clut16}` and `--palette-output` CLI options;
  new `tools/saturn/test_bake_bob_sky.py`, 3 tests). Task 1 of the VDP2
  CLUT plan (`docs/superpowers/plans/2026-08-07-saturn-vdp2-clut.md`):
  storing the 512x256 sky as 4bpp palette indices instead of 16bpp
  RGB1555 will recover VRAM/cart budget in later tasks. Rather than
  writing a new quantizer, the mode reuses the already-hardware-proven
  median-cut CLUT pipeline from `bake_castle_uv.py`
  (`quantize_clut16`/`pack_clut16`, the same code behind 97.8% of BOB's
  VDP1 terrain tiles), and duplicates `bake()`'s exact PNG decode +
  centered edge-replication so the two paths cannot desync on canvas
  geometry. The default `rgb1555` path is byte-identical to before (the
  existing Makefile `compile-bob-sky` target passes no new flags and is
  unaffected; `test_tools.BobSkyBakeTests` still passes), so nothing
  consumes CLUT16 output yet -- later plan tasks wire it into the VDP2
  init code.

- Added project-owned SH-2 exception trampolines
  (`src/port/saturn/sourceboot/source_exception_trampolines.sx`,
  `source_exception_record.c`) installed for both master and slave interrupt
  vector tables at the top of `main()`, for illegal instruction, illegal
  slot, CPU address error, and DMA address error. Yaul's own trampoline
  passes the original register frame to `__exception_assert()`, but
  `__reset()` and the VDP2 diagnostics screen then reuse the faulting
  stack -- these trampolines save the full register frame first and copy it
  into a fixed, pointer-free, HWRAM-resident record
  (`sourceboot_exception_record`) before delegating to Yaul's normal
  handler, so the original fault state survives past the reset screen for a
  debugger to read. Adapted from Yaul's own `cpu_exceptions.sx` pattern (API
  use, no source copied).

- Relocated ~20 previously-HWRAM (`.bss`) sourceboot statics (frame
  pipeline, Mario actor snapshot/pose, render-snapshot bank, VDP1
  backend/frame-bank-set/transfer-targets, Gouraud banks, per-frame
  telemetry counters, the DMA queue's own metadata, and two small
  diagnostic probes in `source_cart.c`/`source_q16_kernel_probe.c`) into
  `.lwram_bss` via a new `SOURCEBOOT_LWRAM_STATE`/
  `SATURN_DMA_QUEUE_LWRAM_STATE` attribute macro, to relieve chronic HWRAM
  pressure documented elsewhere in this file and in `STATE.md`. Since
  `.lwram_bss` is NOLOAD (not crt0-zeroed), added an explicit
  `sourceboot_reset_lwram_state()` that zeroes every relocated field, called
  once in `main()` immediately before `sourceboot_init_sky_bitmap()`.
  Shrank `SOURCEBOOT_MAIN_POOL_BYTES` from `0x60000` to `0x5EC00` (freeing
  5 KiB) to make room for the relocated state within the existing LWRAM
  budget. Also reworked `SATURN_DEMO_HOT_PROMOTION`'s BOB geometry path
  (`saturn_demo_render.c`) to read the generated immutable bank directly
  and promote it into one LWRAM work-area, instead of first copying it into
  a separate LWRAM-resident array -- avoiding paying the 43,776-byte BOB
  geometry bank's cost twice.

  **This is real relief, not a full fix, and does not by itself produce a
  passing link.** Per the measured, before/after linker `.map` numbers
  recorded in this file's entries for Task 9 of
  `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`: with this work
  included, the accepted-rollback configuration (this work + the
  unrelated, separately-committed `39008658`/`6dbaea8d`/`a48e5aac` +
  the VDP2 HUD) still fails `sourceboot-cart.x`'s memory-budget asserts,
  short by 3,128 HWRAM bytes and over the LWRAM slave-stack boundary by
  784 bytes -- smaller shortfalls than the ~5,880/~2,176-byte gaps measured
  with this work stashed out, but not closed. Closing the remaining gap
  needs either further HWRAM/LWRAM relief beyond this commit or shrinking
  one of the two unrelated fixed-size consumers named above; this commit
  does not attempt that on its own.

### Fixed

- **Correction to the "Fixed three real, target-only compile failures..."
  entry immediately below in this section, and to the "Added
  `tools/saturn/capture_sourceboot_hud_state.py`..." entry in the "Added"
  section further down this same file** (same-day follow-up review; both
  of those entries left exactly as originally written, this entry appended
  rather than editing them in place). Two problems with the original
  root-cause narrative in those two entries:

  1. **Misattribution.** Both of those entries blame the `.lwram_actor_runtime`
     (`0x10000` = 65,536-byte) section on the geo-walk commits (`6dbaea8d`/
     `a48e5aac`). Re-verified with `git show 39008658 -- src/port/saturn/
     sourceboot/sourceboot-cart.x`: that section, including its own
     `ASSERT (SIZEOF(.lwram_actor_runtime) == 0x10000, ...)`, was actually
     added ten hours earlier the same day by `39008658` ("feat(saturn):
     reserve sourceboot actor runtime arena"), an unrelated actor-system
     feature. `git show 6dbaea8d -- src/port/saturn/sourceboot/
     sourceboot-cart.x` confirms that commit only adds `.lwram_geo_traversal`
     and reads the pre-existing `__lwram_actor_runtime_end` symbol; it does
     not define or resize `.lwram_actor_runtime`. Reverting just
     `6dbaea8d`/`a48e5aac` would not reclaim this 64 KiB.

  2. **An uncommitted confound that was never isolated.** This worktree
     carries uncommitted changes (`git diff --numstat`, re-verified while
     writing this correction) to `src/port/saturn/sourceboot/main.c`
     (+167/-44), `src/port/saturn/gfx/saturn_demo_render.c` (+29/-16),
     `src/port/saturn/gpl/slavedriver_dma_queue.c` (+18/-9),
     `src/port/saturn/sourceboot/source_cart.c` (+5/-1), and `src/port/
     saturn/sourceboot/source_q16_kernel_probe.c` (+4/-1) -- 223
     insertions/71 deletions total across those five tracked files -- plus
     two new untracked files (`source_exception_record.c`,
     `source_exception_trampolines.sx`, 96 lines together) that the
     *committed* sourceboot Makefile already lists in `SH_SRCS`. All of
     this was therefore compiled into every one of the seven build attempts
     behind the entries below, and was never isolated as a variable before
     writing the original narrative.
     `saturn_demo_render.c`'s own uncommitted diff documents moving a
     43,776-byte BOB geometry bank from HWRAM into `.lwram_bss`
     (new `__attribute__((section(".lwram_bss")))` annotations, with a
     comment citing the exact byte count), and `main.c`'s uncommitted diff
     applies the same pattern to several other statics via a new
     `SOURCEBOOT_LWRAM_STATE` macro -- consistent with this file's own
     `STATE.md` describing "the uncommitted/current work" as "repairing
     memory/exception behavior." `.lwram_bss` has no fixed-size `ASSERT` in
     `sourceboot-cart.x` (unlike `.lwram_actor_runtime`/
     `.lwram_geo_traversal`), so this uncommitted growth is invisible to a
     diff of the linker script alone. Geo-walk's own genuine addition is
     `0x1000` (4,096) bytes per the real generated
     `saturn_geo_depth_manifest.ld`; this uncommitted refactor is roughly an
     order of magnitude larger by itself and is at least as likely to be the
     dominant contributor to the LWRAM assertions. It also cannot be
     responsible for the fourth, separate HWRAM-margin assertion either way,
     since geo-walk touches zero HWRAM bytes -- neither can this LWRAM-side
     refactor explain an HWRAM assertion, so that specific failure's cause
     remains unidentified by either explanation.

  **Net effect: the real dominant cause of the link failure is not yet
  conclusively identified.** The committed-only delta (this plan's Tasks
  1-9 plus the separately-committed, unrelated `39008658`/`6dbaea8d`/
  `a48e5aac`) was never build-tested in isolation from the ~390 lines of
  uncommitted/untracked change described above. Whoever resumes this should
  not assume reverting `6dbaea8d`/`a48e5aac` alone restores a passing
  link -- it may not touch the actual dominant consumer at all. The
  uncommitted work belongs to someone else's in-progress task in this
  shared worktree and was deliberately left untouched (not stashed, not
  reverted) per this task's own instructions; re-testing with it stashed
  by its owner, against the same accepted-rollback flags, would be the
  fastest way to separate the two variables.

- Fixed three real, target-only compile failures found by Task 9's first
  full SH-2 cross-build of the sourceboot image (`docs/superpowers/plans/
  2026-08-06-saturn-hud-vdp2.md`) -- every one of these files had only ever
  been compiled by a host `gcc` (whose headers pulled `NULL` in
  transitively as a side effect) or never actually linked into a full
  target image before. `src/port/saturn/gfx/saturn_hud_layout.c` (Task 5)
  and `src/port/saturn/gfx/saturn_hud_publish.c` (Task 6) both use `NULL`
  but only reach `<stdint.h>` through their own header chain, which the C
  standard does not require to define it; the real SH-2 newlib headers
  correctly rejected it (`'NULL' undeclared`). Both now `#include
  <stddef.h>` directly. `src/port/saturn/runtime/saturn_geo_walk_runtime.c`
  (unrelated to the HUD; part of the concurrent "iterative geo walk"
  work, commit `a48e5aac`) had the identical gap and got the identical
  fix. All three fixes are additive-only (one standard include each);
  every host test that covers these files
  (`verify-saturn-hud-snapshot`, `verify-saturn-hud-layout`,
  `verify-saturn-hud-layout-mutation`, `verify-saturn-hud-no-vdp1`,
  `verify-saturn-geo-walk-runtime`) was re-run afterward and still passes.

### Added

- Added `tools/saturn/capture_sourceboot_hud_state.py` and the
  `verify-sourceboot-hud-target` composed build+capture+assert Make target
  (Task 9 of `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`): boots
  the "accepted rollback" sourceboot configuration headless in Ymir, peeks
  the VDP2 HUD pattern-name table at the real, current
  `saturn_hud_atlas.c`/`saturn_hud_layout.c` VRAM address and tile
  coordinates (bank 2, `HUD_PND_BASE=0x25E48000`; `(col=1,row=12)` for the
  lives cluster's Mario-head glyph), and asserts the observed VDP2
  character-number field against the value `sm64_saturn_hud_atlas_init()`
  would actually have written there. Reuses `YmirClient` from
  `capture_route_views.py` rather than reimplementing the JSON-RPC
  transport. **Status: the build+link now reaches a real SH-2 ELF link for
  the first time (every compile error, including the three fixed above, is
  resolved), but that link itself currently fails** on a real, pre-existing
  memory-budget conflict introduced by the same concurrent "iterative geo
  walk" work (commit `6dbaea8d`, "feat(saturn): add generated geo traversal
  storage", 2026-08-06): `src/port/saturn/sourceboot/sourceboot-cart.x`'s
  HWRAM/LWRAM budget assertions (lines 142, 225-227, 232) fail once the new
  `.lwram_geo_traversal`/`.lwram_actor_runtime` sections are linked in
  alongside the accepted-rollback configuration. This is not part of the
  HUD (Tasks 1-8) and not something this task's scope covers fixing; the
  capture script and Make target are ready to produce real pass/fail
  evidence as soon as that budget conflict is resolved by whoever owns the
  geo-walk work -- no further HUD-side changes are expected to be needed.

- Wired the VDP2 gameplay HUD into sourceboot's real frame bank and
  presentation path (Task 8 of `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`)
  -- the task where Tasks 3-7's independently-built, independently-tested
  pieces first connect inside the real game loop. Added `hud` to
  `sm64_saturn_vdp1_frame_bank_t` alongside `camera_snapshot`
  (`saturn_vdp1_frame_bank.h`/`.c`), with a new
  `sm64_saturn_vdp1_frame_bank_set_hud_snapshot()` that mirrors
  `_set_camera_snapshot()`'s exact guard (`bank->state !=
  SM64_SATURN_VDP1_FRAME_BANK_BUILDING` -> reject, otherwise a plain field
  copy) after reading its real implementation first, per the plan's
  instruction. Also mirrored the field's full lifecycle, not just the
  setter: `sm64_saturn_vdp1_frame_bank_begin_build()` now resets
  `bank->hud` to `{0}` on FREE->BUILDING transition, exactly like
  `camera_snapshot` already does -- the plan's Step 2 text only asked for
  setter-shape parity, but leaving a fresh `BUILDING` bank holding a stale
  `hud` snapshot from two generations back (in the same struct where
  `camera_snapshot` is deliberately zeroed) would have been an inconsistent,
  latent bug in the same file, so this task closed it as part of the
  mirroring the field's own name implies. `main.c` now copies `hud` off
  `sourceboot_active_render_snapshot` into the frame bank right after the
  existing `camera_snapshot` copy (same acquired-and-generation-checked
  pointer, no new gate), publishes it every presented generation via
  `sm64_saturn_hud_publish(&sourceboot_hud_publish_state, &bank->hud)`
  alongside the existing camera-snapshot extraction in
  `sourceboot_present_generation()`, initializes the atlas and publish
  state once after cart load, and raises NBG0 to priority 7 (topmost,
  above VDP1 sprites and NBG3 dbgio text) in `sourceboot_vdp2_layers_set()`.
  `SM64_SATURN_VDP2_FRAME_DISPLAY_MASK` now includes NBG0.

  **Found and fixed a real VRAM base-address collision, not just a
  bandwidth question.** The plan's design decision #7 and Task 4's
  committed `saturn_hud_atlas.c` placed the HUD character-pattern/pattern-
  name data at `VDP2_VRAM_ADDR(1, 0x00000)`, describing it as "bank B0,
  disjoint from NBG1's sky bitmap in bank A0." Re-deriving the real address
  math (`VDP2_VRAM_ADDR(bank, offset) = base + (bank << 17) + offset`,
  third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2/vram.h) shows bank
  argument `1` is actually quarter-bank **A1**, not B0 -- and sourceboot's
  sky bitmap (`sourceboot_init_sky_bitmap()`, 512x256 @ RGB1555 =
  0x40000 bytes) is exactly two quarter-banks, so it physically occupies
  bank A0 *and* bank A1 in full (`0x25E00000`-`0x25E3FFFF`). The HUD atlas
  was about to be written directly into the sky bitmap's own bottom 128
  rows. This was dormant since Task 4 (nothing called
  `sm64_saturn_hud_atlas_init()` yet), and no cycle-pattern slot allocation
  can fix a base-address collision -- moving VRAM bytes is the only fix.
  Relocated `HUD_CPD_BASE`/`HUD_PND_BASE` in `saturn_hud_atlas.c` to bank B0
  (`VDP2_VRAM_ADDR(2, ...)`), confirmed unused by grepping every
  `VDP2_VRAM_ADDR` call site in `src/port/saturn` (sky bitmap: banks 0-1;
  dbgio's NBG3 console + the backscreen gradient table: both bank 3). The
  relative CPD/PND offset (`0x08000`) is unchanged, so the existing
  `_Static_assert` guarding atlas overflow into the PND region is
  unaffected (confirmed by successful compile, not just inspection).

  This also **simplified** the plan's Step 5 VRAM cycle-pattern carve-out.
  The literal plan text reduced NBG1 from 8 slots to 6 (stealing one from
  each of bank A0/A1) to give NBG0 2 slots in those same banks, on the
  (now-corrected) assumption the HUD atlas shared a bank with the sky. With
  the atlas relocated to bank B0 -- previously completely idle -- NBG0
  instead gets its own slots there (`.pt[2]`), and NBG1's original 8-slot
  sky provision is left **untouched**, which is strictly lower-risk than
  the plan's literal text (no reduction to the sky's own read bandwidth at
  all). Provisioned NBG0 with 1 PNDR slot (always sufficient regardless of
  color depth, per the general guideline in `vdp2_vram_cycp_bank_t`'s own
  doc comment) plus 4 CHPNDR slots, matching the same per-bank ratio the
  sky bitmap already uses for its own RGB_32768 data (NBG0 is also
  RGB_32768) -- bank B0 has 8 slots total and nothing else competes for it,
  so this is free headroom, not a tradeoff.

  Verified `VDP2_VRAM_CYCP_PNDR_NBG0`/`CHPNDR_NBG0` and the real
  `vdp2_vram_cycp_t` shape (`pt[4]`, each a `t0..t7` bitfield -- not the
  `pt[2]`/`t0..t3` shape the plan's snippet implied) directly against the
  vendored header rather than trusting the plan's snippet.

  Added `saturn_hud_atlas.c`/`saturn_hud_layout.c`/`saturn_hud_publish.c`
  to sourceboot's `SH_SRCS`, and added `$(SOURCEBOOT_HUD_GLYPH_HEADER)` to
  the `$(SH_OBJS_UNIQ): |` order-only prerequisite list (deferred here from
  Task 2, which created the `source-hud-glyphs` generator target but not
  this ordering) -- confirmed via `make -p` that every object target now
  correctly lists the generated glyph header as an order-only prerequisite,
  matching the existing quad-map/mario-texture-header guard immediately
  above it.

  **Compiler verification:** `main.c` carries real, uncommitted, unrelated
  in-flight work in this worktree (an LWRAM frame-state relocation and new
  exception trampolines), so this task's own hunks were isolated with
  `git apply --cached` against a clean `HEAD` copy (replaying the same
  edits onto a `git show HEAD:main.c` copy, diffing, and staging only that
  diff) rather than staging the whole working tree, leaving the unrelated
  work uncommitted exactly as found. Got real `sh-elf-gcc` verification
  beyond `-fsyntax-only`: full object-file compiles (not just syntax
  checks) of `saturn_vdp1_frame_bank.c`, `saturn_hud_atlas.c`, and the
  complete `main.c` against the real vendored Yaul headers and this
  worktree's real generated headers (quad map, HUD glyphs, build identity,
  Mario textures) -- all three produced real SH-2 object files with exit
  code 0, zero new warnings (the two `-Wunused-variable` warnings `main.c`
  produces are both pre-existing and orthogonal to this task: `pose_ok`
  under `SATURN_DIAGNOSTIC_MODE=0` and `sourceboot_render_overlap_event_ok`
  under `SATURN_DEMO_PATH=0`, neither touched by this task's edits). Hit
  the same MSYS2-`make`-doesn't-propagate-env and native-`sh-elf-gcc`-
  resolves-MinGW's-`as.exe`-over-the-real-one quirks prior tasks in this
  plan documented; worked around the first with PowerShell + explicit
  `$env:` vars instead of sourcing `.yaul.env` through bash, and the second
  with an explicit `-B<yaul-bin>/` search-path flag. Confirmed via `make -p`
  that `SH_SRCS` includes all three new `.c` files in the right place. Not
  verified here (needs Task 9's automated Ymir capture and Task 10's manual
  acceptance): the full sourceboot link, real VDP2 visual composition, and
  whether the corrected cycle-pattern provision is genuinely sufficient for
  glitch-free NBG0 rendering under real scanout timing.

- Added a build-time mutation gate and a static structural gate for the VDP2
  gameplay HUD (Task 7 of `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`),
  plus closed three test-coverage gaps Task 5's review had flagged but
  deferred.

  **Why a mutation gate, not just a passing test:** Task 6's
  `test_publish_only_rewrites_changed_cells` passing proves the dirty-cell
  diff *can* produce zero writes on an unchanged snapshot; it does not by
  itself prove that behavior is load-bearing rather than accidental (e.g. a
  refactor that quietly always-writes but happens not to break any other
  assertion would slip through unnoticed). `saturn_hud_publish.c`'s pass-2
  writer-selection `if` is now wrapped in
  `#ifdef SM64_SATURN_HUD_TEST_MUTATE_DIRTY_GATE`, which forces every cell
  to be treated as changed regardless of `prior->glyph`. The plan's given
  snippet leaves `prior` computed-but-unread on that branch, which fails
  `-Wunused-variable` under this project's `-Werror`; added `(void)prior;`
  to keep the mutation build compiling without weakening it (the real gate
  in the `#else` arm is untouched). New Make target
  `verify-saturn-hud-layout-mutation` builds that mutant and requires it
  exit nonzero via the existing `tools/saturn/expect_failure.py` convention
  (14+ other call sites already use this pattern in this Makefile) --
  confirmed the mutant fails with "unchanged snapshot triggered 4
  rewrites", caught by the fixture.

  **Why a grep gate for VDP1:** this HUD is VDP2-only by design (character
  cells on NBG0), and Task 8 is about to introduce a VDP1 frame-bank type
  that legitimately carries the HUD snapshot through a `sm64_saturn_vdp1_*`
  name. A static, structural proof that the HUD's own rendering logic
  (`saturn_hud_layout.c`, `saturn_hud_publish.c`, `saturn_hud_atlas.c`)
  never references `vdp1_cmdt`/`VDP1_CMDT`/`sm64_saturn_vdp1_` closes the
  door on that boundary eroding silently in a future edit, without needing
  a compiler-level dependency check (these files already don't include any
  VDP1 header). `saturn_hud.h`/`saturn_render_snapshot.h` are deliberately
  excluded from the grep for exactly that Task 8 reason. New Make target
  `verify-saturn-hud-no-vdp1` passes against the real files; also verified
  it actually catches a violation, not just that it passes today, by
  temporarily appending a fake `sm64_saturn_vdp1_frame_bank_fake_reference`
  comment to `saturn_hud_layout.c`, confirming the gate correctly failed,
  then reverting via `git checkout --` before committing.

  Both new targets added to `.PHONY` alongside every other `verify-*`
  target in this file.

  **Coverage-gap closure (beyond the plan's literal text):** Task 5's
  review had left three `TODO(Task 7)` comments in `saturn_hud_layout.c`,
  each recording a real mutation-testing finding that the original 4 tests
  in `tools/saturn/saturn_hud_layout_test.c` never exercised: the
  `cannon_active` gate (no test ever set it), both `camera_status` switch
  statements (MARIO/LAKITU/FIXED and C_DOWN/C_UP -- no test ever set
  `camera_status` to a case-matching value, so only `default:` ever ran),
  and the `stars < 100` branch (only the `>=100` sibling had coverage, via
  the existing capacity test's `stars=9999`). Added 5 new tests closing all
  three: `test_layout_places_cannon_reticle_when_active` /
  `test_layout_omits_cannon_reticle_when_inactive` (both directions of the
  boolean gate -- a single inversion mutation breaks both, but each also
  independently catches an always-true or always-false variant alone),
  `test_layout_camera_mode_switch_selects_correct_glyph` /
  `test_layout_camera_cbutton_switch_selects_correct_glyph` (every case
  label of both switches, table-driven, asserting the *sibling* glyphs are
  absent so a swapped-glyph mutation is caught rather than just "some"
  camera-mode glyph appearing), and
  `test_layout_star_count_below_100_uses_two_digit_field` (stars=42; the
  `<100` and `>=100` branches coincidentally produce the same total cell
  count -- 4 either way -- so the assertion targets the two signals only
  `<100` can produce: the multiply glyph's presence and an exact 2-digit
  field instead of 3). All 5 wired into `main()`.

  Verified each new test against a real mutation of its own target code
  path, not just that it passes today (project standing policy): inverted
  the `cannon_active` gate (both new cannon tests failed, plus incidentally
  broke an existing Task 6 publish test since `cannon_active` defaults to 0
  almost everywhere); swapped `CAM_MARIO_HEAD`/`CAM_LAKITU_HEAD` in the mode
  switch (`test_layout_camera_mode_switch_selects_correct_glyph` failed,
  naming `CAM_STATUS_MARIO`); swapped `CAM_ARROW_DOWN`/`CAM_ARROW_UP` in the
  C-button switch (`test_layout_camera_cbutton_switch_selects_correct_glyph`
  failed, naming `CAM_STATUS_C_DOWN`); forced the stars branch to always
  take the `>=100` path (`test_layout_star_count_below_100_uses_two_digit_field`
  failed: "did not render the multiply glyph"). Each mutation was applied
  with `Edit`, built, run, observed red, then reverted with
  `git checkout --` and confirmed byte-identical to `HEAD` before moving to
  the next. Replaced the three now-resolved `TODO(Task 7)` comments with
  notes naming the covering test, so they stop claiming the gap is still
  open.

  Verified by direct execution from a from-scratch `build/saturn/host-tests`
  directory: normal build (11 tests, exit 0), mutation build (exit 1,
  caught by `expect_failure.py`, exit 0), no-VDP1 grep gate (exit 0, both
  via real `make` and standalone). Hit the same MSYS2 `make`
  recipe-shell-strips-`TMP`/`TEMP` quirk Task 6 already documented (`Cannot
  create temporary file in C:\WINDOWS\: Permission denied`); worked around
  it identically -- reproduced the exact `make -n` command lines directly
  in a shell with a correctly populated environment. `verify-saturn-hud-no-vdp1`
  itself has no such issue (grep needs no temp files) and was additionally
  confirmed to run clean through real `make` directly.

- Added `src/port/saturn/gfx/saturn_hud_publish.{h,c}` (Task 6 of the VDP2
  gameplay HUD plan, `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`):
  `sm64_saturn_hud_publish()` diffs the layout Task 5's
  `sm64_saturn_hud_layout_build()` computes for the current frame against
  the layout actually published last time, and calls Task 4's
  `sm64_saturn_hud_atlas_write_cell()` only for cells whose `(col,row,glyph)`
  changed since then -- including writing `SM64_SATURN_HUD_GLYPH_BLANK` for
  any cell that was occupied last publish and is not occupied this publish.
  Pure host-testable logic with no VRAM access of its own; a test double
  stands in for the real atlas writer in the host test, exactly as the plan
  specified.

  Verified the real, current Task 4/5 public interfaces
  (`saturn_hud_atlas.h`, `saturn_hud_layout.h`) before writing any code
  against them, per this plan's established practice -- both matched what
  the plan's Step 3 snippet assumed exactly (field names/types on
  `sm64_saturn_hud_cell_t`, `SM64_SATURN_HUD_LAYOUT_MAX_CELLS` = 40, and
  `sm64_saturn_hud_layout_build()`'s signature), so the plan's given
  implementation needed no interface-mismatch fixes this time.

  Added `test_publish_writes_blank_for_vacated_cells` to
  `tools/saturn/saturn_hud_layout_test.c`, beyond the plan's own given test.
  The plan's `test_publish_only_rewrites_changed_cells` never clears
  `HUD_FLAG_LIVES`, so it only ever changes a digit glyph within a group
  that stays on screen -- it never exercises `sm64_saturn_hud_publish()`'s
  first diff pass (the one that blanks cells no longer occupied) at all;
  deleting that whole pass would still pass the plan's test. The new test
  drives the lives group from shown to fully hidden and checks that every
  previously-occupied cell is rewritten exactly once, that every one of
  those rewrites specifically carries `SM64_SATURN_HUD_GLYPH_BLANK` (not
  just "some glyph"), and that republishing the same now-empty snapshot a
  second time costs zero further writes -- proving `state->last_count`
  actually shrinks to 0 rather than leaving stale occupied bookkeeping
  behind.

  Traced the specific correctness questions this task's brief called out
  before trusting the algorithm: a cell whose glyph changes while a
  different cell's glyph also changes in the same publish is handled
  independently per cell (no shared state between diff decisions); the
  first-ever publish (`state->primed == 0`) writes every cell because
  `last_count` is already 0 from `sm64_saturn_hud_publish_init()`, so
  `find_cell()` would return `NULL` regardless of the explicit `primed`
  guard (the guard is belt-and-suspenders, not load-bearing); and
  `find_cell()`'s O(count) per-call / O(count^2) total linear scan is a
  performance-only characteristic, not a correctness gap, given
  `SM64_SATURN_HUD_LAYOUT_MAX_CELLS` is a fixed 40 and Task 5's layout
  builder is documented never to emit duplicate `(col,row)` keys for
  simultaneously-visible glyphs. Also confirmed by construction that the
  "clear vacated cells" pass and the "write occupied cells" pass can never
  both target the same `(col,row)` in one call (they partition on
  membership in the new layout's cell list), so a glyph that changes while
  staying occupied always gets exactly one write, never a spurious
  blank-then-rewrite pair.

  Verified by direct execution: compiled and ran the full host test
  (`gcc -std=c11 -Wall -Wextra -Werror`, zero diagnostics, exit 0, all 6
  tests including the 2 new ones), then ran a real mutation-testing pass
  (project standing policy) rather than trusting the given tests --
  disabling the vacate pass, forcing pass 2 to always/never write, weakening
  `find_cell`'s `&&` to `||`, and writing the wrong glyph on vacate were all
  5/5 caught (test suite goes red for each); restored the clean
  implementation afterward and confirmed a final clean build.

  Hit the same `Makefile.saturn.mk`/Cygwin-`make` `OS` quirk Task 5
  documented, worked around identically (`OS=Windows_NT` on the command
  line). Also hit a related but distinct quirk this time: this sandbox's
  MSYS2 `make` (`/c/msys64/usr/bin/make`, Cygwin-built) strips `TMP`/`TEMP`/
  `TMPDIR` entirely from its recipe shell's environment (confirmed with a
  minimal diagnostic Makefile target), which breaks the native
  (non-MSYS-linked) MinGW `gcc.exe`'s ability to create its intermediate
  temp files ("Cannot create temporary file in C:\WINDOWS\: Permission
  denied") -- unrelated to the already-documented `OS`-variable quirk, and
  reproducing consistently even after this task's files existed. Worked
  around the same way the plan's own guidance anticipated for the Python-
  wrapper quirk: reproduced the exact compiler and run commands `make -n`
  would have issued and ran them directly in a shell with a correctly
  populated environment, which is what actually proves RED and then GREEN.

  Added `saturn_hud_publish.c` to the `verify-saturn-hud-layout` Makefile
  target's compile line, per the plan's Step 2.

  **Provenance correction:** the plan's Step 4 text asserted, as the
  rationale for an "honest negative finding," that "Z-Treme's own HUD
  counters redraw unconditionally every frame." Both pinned references
  (`Lobotomy-Software/SlaveDriver-Engine` and `Maxime-XL2/SONIC-Z-TREME`)
  are vendored locally, so this was checked directly rather than copied
  into permanent provenance docs unverified. SlaveDriver's own text/HUD
  module, `PRINT.C`/`PRINT.H`, has no dirty-tracking of any kind and
  renders text as VDP1 sprites (`sega_spr.h`, `EZ_setLookupTbl`), not VDP2
  character-pattern cells at all -- not even the same rendering mechanism
  this task's atlas uses. Z-Treme's only candidate HUD-counter call site,
  `slPrint("RINGS : ", slLocate(0,4))` in `SRC/game.c:30`, is commented out
  in the pinned snapshot, and `slPrint`/`slLocate` are proprietary SGL
  primitives with no available source in this repository. The more precise,
  defensible finding recorded in `docs/saturn/UPSTREAM_CODE_LEDGER.md`
  ("Task 23A Task 6") and `docs/saturn/PROVENANCE.md` instead: neither
  pinned reference offers inspectable per-cell VDP2 dirty-diffing logic to
  adopt or contrast against, because neither has a live, readable call site
  to inspect at all -- not that either one demonstrably redraws
  unconditionally. Added alongside, not overwriting, Task 4's existing
  `ztFont2NBG3` citation in both files.

  **Provenance correction, corrected (2026-08-07):** a spec review caught
  that the "Provenance correction" above is itself wrong. It cited an
  unrelated, commented-out `slPrint("RINGS : "...)` label inside
  `ztReset()` (`SRC/game.c:30`, a one-time player-death/respawn path) and
  concluded no live per-frame HUD-counter call site existed in either
  reference -- but never checked `draw_stats()` (`ZT_RENDERING.c:146-152`),
  which is exactly what the plan's own text names, at the plan's line 20.
  Re-read the pinned Z-Treme tree directly and traced the real call chain:
  `draw_stats()` issues unconditional `slPrintHex`/`slLocate` calls for
  `LIVES`, `OWNED` ("Rings or weapons", `ZTE_DEF.H:282`), and a
  `TIMER`-derived clock, gated by nothing (its own early-return at line 148
  is commented out, and the only active gate, line 165, guards later
  debug-only readouts, not these three); it is called unconditionally for
  the local player from `ztRender()` (`ZT_RENDERING.c:792-793`, gated only
  on `currentPlayer->PLAYER_ID == 0`, true for `PLAYER_1`, `main.c:119`),
  which `main_loop()` calls unconditionally every invocation
  (`SRC/game.c:772`), inside `ztGameLoop()`'s `while(1)` loop paced by
  `slSynch()` (`ZT_GAME.c:70-107`). The plan's original claim was correct
  all along: Z-Treme's own HUD counters do redraw unconditionally every
  frame. Fixed `docs/saturn/UPSTREAM_CODE_LEDGER.md` and
  `docs/saturn/PROVENANCE.md` to cite `draw_stats()` instead of the
  unrelated dead line, restoring the plan's original framing, while leaving
  the SlaveDriver `PRINT.C` finding (VDP1-sprite-based text, not VDP2
  cells) unchanged, since that part was independently confirmed accurate
  and not in question. The underlying negative finding these citations
  support -- neither reference implements *per-cell* VDP2 dirty diffing, so
  `saturn_hud_publish.c` is original engineering -- is unaffected; only the
  supporting evidence for Z-Treme's half of it was wrong and is now
  corrected. No code or test changes; this is a documentation-only fix.

- Added `src/port/saturn/gfx/saturn_hud_layout.{h,c}`: a pure, host-testable
  function (`sm64_saturn_hud_layout_build()`) that decides which glyph goes in
  which VDP2 tile cell for a given `sm64_saturn_hud_snapshot_t` (Task 5 of the
  VDP2 gameplay HUD plan, `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`).
  No VRAM access, no Yaul dependency -- takes a snapshot, writes up to
  `SM64_SATURN_HUD_LAYOUT_MAX_CELLS` (40) `(col,row,glyph)` cells, and returns
  the count actually used.

  Resolved an intentional test/gate contradiction flagged in the plan itself:
  the plan's power-meter test sets `snapshot.wedges = 3` but leaves
  `power_meter_animation` at its `memset`-zeroed default while still expecting
  the meter glyph to appear, apparently conflicting with a `!= 0` gate. Read
  the real `src/game/hud.h` (`enum PowerMeterAnimation`: `POWER_METER_HIDDEN`
  is the first enumerator, value 0) and the real `hud.c`
  (`render_hud_power_meter()`, `hud.c:229-257`) rather than assuming: the
  source's own gate is `if (sPowerMeterHUD.animation == POWER_METER_HIDDEN)
  return;`, i.e. it renders for all four non-hidden phases (EMPHASIZED,
  DEEMPHASIZING, HIDING, VISIBLE), not just the resting VISIBLE state. So
  `power_meter_animation != 0` is exactly correct and was kept unchanged; the
  bug was in the test, which was missing
  `snapshot.power_meter_animation = 1;` (`POWER_METER_EMPHASIZED`, matching
  the numeric-literal convention `saturn_hud_snapshot_test.c` already
  established for this field, since neither host test can include
  `src/game/hud.h` without pulling in the full N64 `PR/ultratypes.h` chain).
  Fixed the test, not the gate, and documented why inline.

  Also found and fixed a placement bug the plan's own Step 3 snippet did not
  flag: most of its literal `(col,row)` values are outside the atlas's real
  visible grid. `sm64_saturn_hud_atlas_write_cell()` (Task 4) silently
  no-ops any write with `col >= 20` or `row >= 14`
  (`saturn_hud_atlas.c`'s `HUD_TILE_COLS`/`HUD_TILE_ROWS`, confirmed by
  reading the real, current file rather than the plan's summary of it) --
  and the plan's snippet placed lives/coins at `row=26`, stars at
  `col=24-27`, the timer at `col=20-25`, and the camera glyphs at
  `col=26-27`, all past those bounds. None of Task 5's or Task 6's own tests
  would have caught this: they only check glyph presence and total count,
  never `col`/`row`, and Task 6's dirty-cell publisher (already drafted in
  the plan) forwards this layout's `(col,row)` straight into
  `sm64_saturn_hud_atlas_write_cell()` with no remapping. Left uncorrected,
  this would have made lives, coins, stars, the timer, and the camera status
  indicator permanently invisible on real hardware and in emulation --
  passing every test in this 10-task plan while silently defeating its
  stated goal. Re-derived every placement from `hud.c`'s real pixel
  coordinates (`SCREEN_WIDTH`/`SCREEN_HEIGHT` = 320x240,
  `include/config.h:38-39`) divided down to this port's 16px/20x14 grid,
  clustering everything into the bottom four tile rows -- and checked that
  no two glyphs able to be visible in the same frame ever target the same
  cell, including the realistic simultaneous case (`HUD_DISPLAY_DEFAULT`'s
  LIVES | COIN_COUNT | CAMERA_AND_POWER bits plus STAR_COUNT/TIMER,
  `level_update.h:106-116`), not just the tests' synthetic worst case.
  Documented the full derivation and the final grid assignment in a comment
  in `saturn_hud_layout.c`.

  **Code-review correction (2026-08-07):** the first version of that
  derivation comment (and this entry) claimed lives/coins/stars/camera "all
  sit at y=205-209" in the source, as if the bottom-row clustering were a
  pixel-derived transcription for all four groups. A code-quality review
  traced the actual rendering paths and found this conflates two different
  Y-axis conventions that coexist in `hud.c`: lives/coins/stars
  (`HUD_TOP_Y=209`) and the timer (`y=185`) all go through
  `print_text()`/`print_text_fmt_int()` -> `render_text_labels()` ->
  `render_textrect()`, which applies an unconditional Y flip,
  `s32 rectBaseY = 224 - y;` (`src/game/print.c:391`, confirmed by reading
  the real file) -- putting them at actual screen y≈15/y≈39, near the
  **top**, not the bottom. Only the camera status icon
  (`render_hud_camera_status()`, `y=205`) uses the unflipped
  `render_hud_tex_lut()` path directly, so it genuinely is near the bottom.
  So in the real game, lives/coins/stars/timer cluster near the top and
  only the camera icon is near the bottom -- the opposite of what the
  original comment claimed for 4 of the 5 groups. This was a documentation
  defect only: the shipped layout was already safe (in-bounds) and
  internally consistent (collision-free) either way, confirmed by the spec
  reviewer's exhaustive brute-force check of all 1,638,400 possible input
  combinations. Fixed by correcting the derivation comment in
  `saturn_hud_layout.c` to state plainly that only the camera icon's bottom
  placement is a literal match to the source's screen semantics, and that
  the rest of the bottom-clustering is a deliberate choice driven by the
  tile grid's coarseness, not a pixel-derived one -- comment-only, no logic
  changes (mechanically confirmed: built the pre-fix and post-fix
  `saturn_hud_layout.c` side by side against five representative snapshots,
  including the pathological worst case, and diffed the emitted
  `(col,row,glyph)` cells -- byte-identical). Also named the per-group
  `HUD_ROW_*`/`HUD_COL_*` constants that were previously bare numeric
  literals scattered across roughly 15 call sites, so a future edit to one
  group's position is grep-auditable against every other group instead of
  relying solely on this prose comment, and added `TODO(Task 7):` markers
  at the three branches the reviewer's mutation testing found completely
  unexercised by the current 4 tests (the cannon reticle path, the camera
  mode/C-button switch cases, and the star-count `<100` branch) so Task 7
  -- the plan's dedicated mutation-test task -- picks them up.

  Verified by direct execution, not just reading: compiled and ran the host
  test (`gcc -std=c11 -Wall -Wextra -Werror`, zero diagnostics, exit 0), then
  ran a real mutation-testing pass (project standing policy) rather than
  trusting the given tests -- flipping the power-meter gate and inverting
  the lives-flag check were both caught (test suite goes red); an
  off-by-one in `push_cell`'s capacity guard (`>` for `>=`) survives
  uncaught, because the tests' "9999 lives/coins/stars" pathological input
  only reaches ~20-24 cells against the 40-cell buffer, never the actual
  boundary -- a real, pre-existing test-coverage gap (inherited from the
  plan's Step 1 test, not introduced here) worth knowing about, though the
  shipped code uses the correct `>=` guard. Also confirmed `push_clamped_int`
  truncates to the low-order N decimal digits rather than saturating at the
  field's max value (e.g. a 2-digit field showing 105 would render "05", not
  "99") -- the pathological test's all-9s values (9999) happen to read
  identically either way, which masks the distinction; documented inline as
  a known, pre-existing display-fidelity limitation of the fixed-width
  tile HUD, not a capacity or memory-safety issue.

  Hit two known host-tooling environment quirks getting a real run: this
  sandbox's Cygwin `make` does not see the `OS` environment variable at all
  (confirmed with a minimal repro Makefile), so `Makefile.saturn.mk`'s
  `ifeq ($(OS),Windows_NT)` branch silently picked the wrong
  `SATURN_TOOLS_PYTHON`/`HOST_EXEEXT` values; fixed by passing
  `OS=Windows_NT` on the `make` command line rather than editing the
  Makefile. Separately, the venv Python's `subprocess.run()` test-runner
  wrapper can't launch an MSYS-style `/d/...` path via native Windows
  `CreateProcess` (`WinError 2`) -- pre-existing and orthogonal to this
  task, would affect any `verify-*` target's Python-wrapped run step
  equally. Both are environment issues, not defects in the Makefile target
  or the code; worked around by running the make-built binary directly,
  which is what actually proves the test passes.

  Added `verify-saturn-hud-layout` (Makefile target + `.PHONY` entry),
  following the exact convention already established by
  `verify-saturn-hud-snapshot`.

- Closed two code-review gaps in `saturn_hud_atlas.c` (Task 4, above) found by
  a stricter-warning-level review pass (`-Wconversion -Wsign-conversion
  -Wshadow -Wcast-align -Wcast-qual -Wdouble-promotion -Wundef
  -Wstrict-prototypes`, still zero diagnostics -- these are both missing
  guards, not compiler-catchable bugs):

  First, `sm64_saturn_hud_atlas_write_cell()` bounds-checked `col`/`row` but
  not `glyph`: a future caller computing an out-of-range
  `sm64_saturn_hud_glyph_t` (e.g. Task 5's not-yet-written unclamped digit
  arithmetic) would have `cpd_addr` land past the last real character
  pattern and write a garbage glyph into a valid, visible PND cell --
  discoverable only at Task 9/10's visual check, far from where the bug
  would actually be introduced. Added `|| glyph >= SM64_SATURN_HUD_GLYPH_COUNT`
  to the existing guard clause, matching the established index-plus-enum-
  sentinel shape already used by
  `saturn_demo_render.c:471-472`'s `demo_terrain_template_valid_set()`
  (confirmed by reading it: same two-condition-plus-`_COUNT` pattern).

  Second, nothing enforced that the atlas's character-pattern data
  (`SM64_SATURN_HUD_GLYPH_COUNT * HUD_CHAR_BYTES`, currently 32 * 512 =
  16384 bytes) stays under the 32768-byte gap to `HUD_PND_BASE`; a future
  glyph addition could silently grow past that boundary and corrupt the
  pattern-name table's own VRAM region, a cross-structure corruption that
  would be very hard to trace back from a Ymir visual glitch. Added a
  `_Static_assert` at file scope, matching the invariant-enforcement shape
  already used by `saturn_pcm_protocol.h:164`
  (`SM64_SATURN_PCM_BANK_OFFSET < SM64_SATURN_PCM_SOUND_RAM_BYTES`).
  Verified the assert actually fires, not just that it compiles: built a
  scratch copy with `HUD_PND_BASE`'s offset shrunk from `0x08000` to
  `0x2000` (8192 bytes, below the real 16384-byte requirement) and
  confirmed `sh-elf-gcc -fsyntax-only` fails with exactly the written
  message ("HUD character-pattern data overflows into the PND region"),
  then discarded the scratch copy -- the real file was never edited to an
  invalid state.

  Also folded the reviewer's optional minor suggestion: added
  `HUD_CHAR_DIM` (16) so the eight power-meter crop calls reference a named
  constant instead of a bare `16U, 16U` pair. Left the second optional
  suggestion (extracting the quadrant-reorder arithmetic into a pure,
  host-testable function with a unit test) as a note for a later task
  rather than doing it here: that would mean building a new host-testable
  module and Makefile `verify-*` target ahead of Task 5, which already
  plans a pure, host-testable `saturn_hud_layout.c` of its own -- doing it
  now risks scaffolding that Task 5 would then have to reconcile with
  rather than build.

- Added `src/port/saturn/gfx/saturn_hud_atlas.{h,c}`: a one-time VDP2 NBG0
  character/cell-mode glyph atlas (Task 4 of the VDP2 gameplay HUD plan,
  `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`). `sm64_saturn_hud_atlas_init()`
  uploads every HUD glyph (digits, camera-status icons, power-meter wedges,
  a procedural cannon reticle, a transparent blank) into VDP2 character-
  pattern VRAM and configures a dedicated NBG0 plane for them, separate from
  the existing NBG1 sky bitmap and NBG3 dbgio text; `sm64_saturn_hud_atlas_write_cell()`
  writes one pattern-name-data cell. This is the first character/cell-mode
  VDP2 usage in this port -- the only prior usage (the NBG1 sky bitmap,
  `introface`'s title screen, dbgio's own NBG3 console) is bitmap mode or an
  already-existing library device, a structurally different Yaul API path,
  so there was no in-repo precedent to reuse for the pixel-upload shape.

  Re-verifying the plan's own code snippet against the real vendored Yaul
  source (rather than trusting it, per this project's standing rule to
  check permissive reference code before writing to it) found and fixed
  three mismatches between the plan's assumptions and reality:

  1. **VDP2 `CHAR_SIZE_2X2` character-pattern data is four separately-
     addressed, individually-contiguous 8x8 pixel cells** (top-left/top-
     right/bottom-left/bottom-right, 64 words each), not one flat 16-wide
     raster. Confirmed two independent ways: `vdp2_scrn_pnd_set()`'s aux-
     mode character-number bit-packing
     (`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2_scrn_cell.c:320-356`)
     supplements the pattern-name table's character number with implicit
     low bits that select one of the four sub-cells; and, independently,
     Yaul's own `satconv` texture converter's `TILE_16x16` case reads
     exactly those four 8x8 quadrants, in that order, into one contiguous
     buffer (`third_party/libyaul/tools/satconv/tile.c:177-208`). The
     plan's snippet copied source pixels into a character-pattern slot
     with a flat `dest[index] = pixels[index]` loop, which would have
     interleaved rows from different quadrants and produced a scrambled
     glyph on real hardware (and in cycle-accurate emulation) for every
     glyph wider than 8px -- i.e. everything except the two 8x8 camera
     arrows, which happened to work by coincidence since their source
     width equals one cell's width. Fixed by replacing the single-pixel-
     count `hud_atlas_upload_one()` helper the plan specified with
     `hud_atlas_upload_pattern()`, which performs the same quadrant
     reordering as `satconv`'s `TILE_16x16` case, generalized with a
     source-stride parameter so one helper also serves the crop case
     below and the 8x8 arrows (whose unwritten quadrants are now
     explicitly blanked to solid transparent, rather than left at
     whatever the VRAM bank previously held -- Saturn VRAM is not
     guaranteed zeroed at power-on).
  2. **The real generated `sm64_saturn_hud_power_meter_1..8` arrays are
     `[1024]` (32x32 source pixels), not `[256]`** as the plan's snippet
     assumed (confirmed by reading the real Task-2-generated
     `build/saturn/sourceboot/generated/saturn_hud_glyphs_generated.h`,
     which the plan itself expected this task to check rather than trust).
     This matches the plan's own prose ("Power meter source art is
     32x32") but not its code, which called the upload helper with a
     literal `256U` pixel count against a 1024-element array -- not an
     out-of-bounds read (256 < 1024), but the wrong 256 pixels: the first
     8 full 32-wide source rows, not a 16x16 top-left square. Fixed by
     giving `hud_atlas_upload_pattern()` a source-stride parameter so the
     power-meter calls can correctly crop the top-left 16x16 region
     (stride 32, width/height 16), matching the plan's own stated
     "one representative 16x16 cell... via the top-left quadrant" intent.
  3. **This port's real VDP2 TV mode is 320x224** (`VDP2_TVMD_HORZ_NORMAL_A`
     / `VDP2_TVMD_VERT_224`, set in `user_init()`,
     `src/port/saturn/sourceboot/main.c:1512-1514`), giving a 20x14 visible
     grid of 16x16 cells (320/16, 224/16) -- not the 32x28 the plan's
     `HUD_TILE_COLS`/`HUD_TILE_ROWS` and header doc comment assumed "at
     this screen resolution". 32x32 is real too, but it is the raw
     `CHAR_SIZE_2X2` page's hardware capacity (`VDP2_SCRN_PAGE_WIDTH_CALCULATE`/
     `PAGE_HEIGHT_CALCULATE` in `scrn_macros.h`, fixed regardless of TV
     resolution), not what is actually on screen; conflating the two
     wouldn't have corrupted memory (32x32 cells are all validly
     addressable within the allocated PND page) but would have let a
     later layout task silently place HUD elements in the invisible
     16 columns / 18 rows outside the real 320x224 raster, a bug that
     would only have surfaced at Task 9/10's visual verification stage,
     much later. Fixed by splitting the single constant into
     `HUD_PAGE_STRIDE_COLS` (32, used only internally for the real PND
     address stride) and corrected `HUD_TILE_COLS`/`HUD_TILE_ROWS` (20/14,
     the public bounds `sm64_saturn_hud_atlas_write_cell()` checks against).

  All three were resolvable by adjusting the code to match verified
  reality rather than requiring an escalation. Verification performed:
  no target link is possible yet (needs Task 8's Makefile wiring and the
  full SH-2 game-tree link), but the real `sh-elf-gcc` 14.3.0 cross-
  compiler (found already installed at `work/yaul-install/bin/`) was used
  to both `-fsyntax-only` check and fully compile-to-object-file this
  source against the real vendored Yaul headers and the real generated
  glyph header, with `-Wall -Wextra -Wpedantic` and zero diagnostics. The
  resulting object's symbol table confirms every `sm64_saturn_hud_*` glyph
  array resolved, both public functions are correctly exported (`T`), the
  internal helper is correctly local (`t`), and the only unresolved
  externs are the two genuinely-external Yaul calls (`cpu_cache_purge`,
  `vdp2_scrn_cell_format_set`) plus compiler-generated helpers -- stronger
  verification than a plain read-through, though still short of an actual
  target boot. Added the Z-Treme `ztFont2NBG3()` pattern-only citation
  (dedicated character-mode plane, own VRAM region, single static page,
  topmost priority -- priority itself is set later, in Task 8) plus the
  Yaul-dependency verification notes to `docs/saturn/UPSTREAM_CODE_LEDGER.md`
  and `docs/saturn/PROVENANCE.md`.

- Added two read-only accessors to `hud.c`/`hud.h` (`get_hud_camera_status`,
  `get_hud_power_meter_state`) and a new `sm64_saturn_hud_snapshot_t` type
  (`src/port/saturn/gfx/saturn_hud.h`), then wired a `hud` field of that type
  onto the existing `sm64_saturn_render_snapshot_t` and filled it from
  `sourceboot_capture_render_snapshot()` (Task 3 of the VDP2 gameplay HUD
  plan, `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`). This rides
  the project's existing double-buffered, generation-tracked render-snapshot
  rail instead of building a second capture mechanism: `hud` inherits that
  struct's generation coherence for free. `render_hud()` itself is
  unmodified -- the two new accessors are pure reads of state it already
  computes every tick (`sPowerMeterHUD`/`sCameraHUD`), at the same
  external-visibility level `gHudDisplay` already has as an `extern` global.
  `saturn_hud.h` stays header-only (struct definition only, no capture
  function), per the task's explicit design constraint.
  One placement decision not spelled out in the task's snippet: the new
  HUD-capture block was inserted immediately before
  `sm64_saturn_render_snapshot_publish()`, not after it and not at the
  function's closing brace. `sourceboot_capture_render_snapshot()`'s actual
  body (`main.c:348-474` pre-change) continues past the `publish` call with
  quarantine-on-failure cleanup, so "the end of the function" and "before
  publish" are different places; writing `snapshot->hud.*` after `publish`
  would race a reader that may have already claimed the buffer via
  `acquire_ready`. Everything must be written into the snapshot before the
  single `publish` call that hands it off.
  Added `tools/saturn/saturn_hud_snapshot_test.c` (a host-only, pointer-free
  struct-shape test, no Yaul dependency) and a matching
  `verify-saturn-hud-snapshot` Makefile target/`.PHONY` entry, following the
  exact convention already established by `verify-render-snapshot-bank` and
  `verify-actor-instance-snapshot`. Verified RED (missing `saturn_hud.h`)
  then GREEN (compiles clean under `-Wall -Wextra -Werror`, runs, exit 0)
  directly with `gcc`, and confirmed the unmodified `verify-saturn-hud-snapshot`
  target itself also passes end-to-end. Also re-ran the pre-existing
  `verify-render-snapshot-bank` host test and `test_render_snapshot_source.py`
  (which regex-asserts no `sm64_saturn_render_snapshot_t` field contains
  `*`) after adding the `hud` field -- both still pass, no regression.
  Environment note for whoever runs this next: in this sandbox, MSYS
  `make` (`/c/msys64/usr/bin/make`) does not see the `OS` environment
  variable when invoked from the plain Bash tool, so `Makefile.saturn.mk`'s
  `ifeq ($(OS),Windows_NT)` silently takes the POSIX branch and points
  `SATURN_TOOLS_PYTHON` at a nonexistent `.venv-saturn-tools/bin/python`.
  Running the same target through PowerShell (with `.venv-saturn-tools`'s
  native-Windows `Scripts/python.exe`) avoids that, but `$(SATURN_REPO_ROOT)`
  is computed via GNU Make's own `$(realpath ...)`, which this MSYS build
  always renders MSYS-style (`/d/Code/...`); that path form is fatal to
  `subprocess.run()` under a native-Windows Python (`_winapi.CreateProcess`
  has no notion of `/d/...`), so the existing python-subprocess-wrapper
  convention (used by `verify-pcm-protocol`, `verify-render-snapshot-bank`,
  `verify-actor-instance-snapshot`, and now this target) only completes
  end-to-end in this sandbox with `SATURN_REPO_ROOT` pinned to a native
  Windows-style path on the command line. This is a pre-existing sandbox/
  toolchain friction affecting every target using that convention, not
  something introduced here, and out of this task's scope to fix.

- Closed two code-review gaps in the Task 3 HUD-snapshot capture above:

  First, `sourceboot_capture_render_snapshot()`'s HUD-capture block had no
  comment explaining why it must precede `sm64_saturn_render_snapshot_publish()`
  -- this function otherwise consistently explains ordering rationale inline
  (e.g. the observer-frame-timing comment at the top of the same function),
  and a future refactor (e.g. hoisting HUD capture into a helper called at
  the end of the function) could silently reintroduce the exact publish-
  ordering race Task 3 was careful to avoid, since nothing at the call site
  itself said not to move it. Added a comment directly above the block.

  Second, `saturn_hud.h`'s and the host test's own comments both claimed
  `sm64_saturn_hud_snapshot_t` is pointer-free and fixed-width, but nothing
  actually enforced either claim: `tools/saturn/test_render_snapshot_source.py`'s
  pre-existing regex sweep (`test_snapshot_types_have_no_pointer_fields`,
  which already protects `sm64_saturn_render_snapshot_t` and the actor
  bridge types from exactly this class of regression) never opened
  `saturn_hud.h`, and `saturn_hud_snapshot_test.c`'s `sizeof(...) == 0U`
  check only proves the struct isn't literally empty. Confirmed live: a
  `char *debug_label;` injected into the struct compiled clean under the
  same `-Wall -Wextra -Werror` the Makefile target uses and the host test
  still exited 0. Fixed by adding
  `_Static_assert(sizeof(sm64_saturn_hud_snapshot_t) == 22U, ...)` to
  `saturn_hud.h` (22 bytes independently verified via a host `offsetof`
  probe before trusting it: field layout packs 8 `int16_t`/`uint16_t`
  members through offset 16, one padding byte between the `int8_t`
  `power_meter_animation` at offset 16 and the `int16_t` `power_meter_y`
  at offset 18 to satisfy 2-byte alignment, then two trailing `uint8_t`
  fields through offset 21 -- no trailing struct padding needed since 22
  is already even) and adding `sm64_saturn_hud_snapshot` (pointing at the
  new `HUD` path constant) to `test_render_snapshot_source.py`'s existing
  `names` tuple. Re-ran the same `char *debug_label;` mutation after both
  fixes: the `_Static_assert` now fails the build
  (`static assertion failed: "hud snapshot ABI must remain fixed-width"`)
  and the Python sweep now fails independently
  (`AssertionError: sm64_saturn_hud_snapshot must not carry live game,
  graph, VDP1, or VRAM pointers`) -- both gates catch it, not just one.
  Restored the clean file and re-confirmed the host test, the Python
  sweep, and the pre-existing `verify-render-snapshot-bank` host test all
  pass clean afterward.

- Added `tools/saturn/extract_hud_glyphs.py`, a local-ROM-derived extractor
  for the real SM64 gameplay-HUD glyphs (digits, multiply/coin/Mario-head/star
  icons, apostrophe/double-quote, camera-status icons, power-meter wedge
  textures), following the same MIO0-decode + assets.json-offset-lookup +
  RGB1555-conversion pipeline established by `extract_mario_textures.py`.
  This is Task 1 of the planned VDP2 gameplay HUD (`docs/superpowers/plans/
  2026-08-06-saturn-hud-vdp2.md`); the HUD itself has no visible on-screen
  presentation yet, only diagnostic dbgio text. Several offsets in the
  original task plan were wrong and were corrected against `bin/segment2.c`'s
  `main_hud_lut`/`main_hud_camera_lut` arrays and the real `assets.json`: the
  digit-range formula only covered digits 0-4, `glyph_multiply`/`glyph_coin`/
  `glyph_mario_head`/`glyph_star` and the four non-`cam_camera` camera icons
  pointed at the wrong glyph slots (JP-only glyphs shift the US MIO0 layout),
  `cam_mario_head` is not a separate texture (the game reuses
  `texture_hud_char_mario_head`, so the manifest aliases it to
  `glyph_mario_head`'s key), and the power-meter filenames were wrong
  (`power_meter_one_segment` is singular, and the 8-wedge state is
  `power_meter_full`, not `power_meter_eight_segments`). As with all
  Nintendo-derived extractors in this repo, output is generated only under
  `build/` (gitignored) from the user's own ROM and is never committed.

- Closed a code-review gap in the HUD glyph extractor: the test suite had
  zero coverage of the actual pixel-extraction logic in `build_header`,
  confirmed by live mutation testing (a swapped `width, height, size,
  regions = entry` unpack and a `"big"` -> `"little"` endianness change in
  the RGB1555 conversion both passed the suite unnoticed). Added
  `test_build_header_extracts_correct_pixels_and_metadata` to
  `test_extract_hud_glyphs.py`, which feeds `build_header` a tiny hand-built
  synthetic MIO0 blob and asserts concrete pixel words, width/height,
  offset, and sha256; re-verified to catch both mutations before fixing
  them back out. Also fixed `extract_hud_glyphs.py`'s
  `decoded_bases.setdefault(base, mio0_decode(rom, base))`, which looked
  like a decode-once-per-base cache but wasn't: Python evaluates
  `setdefault`'s second argument eagerly on every call regardless of
  whether the key already exists, so `mio0_decode` ran once per glyph (30x)
  instead of once per distinct MIO0 segment (2x). This was wasted work, not
  a correctness bug -- re-running the real extraction against the real ROM
  after the fix produced byte-identical output. Removed the now-dead
  `json`/`Path` imports and the stale `power_meter_eight_segments` fixture
  key from the test file.

- Wired `tools/saturn/extract_hud_glyphs.py` into the sourceboot Makefile
  (Task 2 of the VDP2 gameplay HUD plan): a new grouped target generates
  `saturn_hud_glyphs_generated.h` and `saturn_hud_glyphs_manifest.json`,
  mirroring `SOURCEBOOT_MARIO_TEXTURE_HEADER`/`source-mario-textures`
  exactly (same `&:` grouped-output shape, same `--rom`/`--assets`/
  `--output`/`--manifest` invocation style). `source-hud-glyphs` is now a
  prerequisite of `source-assets`, so a full sourceboot asset build
  regenerates the HUD glyph data automatically, the same as every other
  ROM-derived Saturn asset. One deliberate deviation from the original task
  plan text: the plan's snippet introduced a new `SOURCEBOOT_HUD_GLYPH_DIR`
  variable hardcoded to `$(ROOT)/build/saturn/sourceboot/generated`, but
  that path is already the existing `SOURCEBOOT_GENERATED` variable (used
  directly, with no dedicated dir variable, by all the BOB-asset targets) --
  defining a second variable with the same value would have been dead
  duplication with no precedent elsewhere in the file, so the header/
  manifest paths are defined directly off `$(SOURCEBOOT_GENERATED)` instead.
  Scope note for whichever later task adds the C-side HUD renderer: this
  change does not yet add the generated header to `$(SH_OBJS_UNIQ)`'s
  order-only prerequisite list (the mechanism that blocks every compile
  until generated headers exist) because no `.c` file includes it yet --
  that wiring should land alongside the first consumer, mirroring how
  `SOURCEBOOT_MARIO_TEXTURE_HEADER` is listed there. Verified standalone
  (`make -f src/port/saturn/sourceboot/Makefile source-hud-glyphs`) against
  the real ROM: produces all 30 glyphs from `GLYPH_MANIFEST`, is a no-op on
  re-run, and `source-hud-glyphs` shows up in `source-assets`'s expanded
  prerequisite list per `make -p`.

### Changed

- Extended the iterative geo runtime seam with depth-first child/sibling
  scheduling, deferred children-first dispatch, explicit leave actions, and a
  callback-driven host trace. The new contract proves event order and state
  tokens before source handlers are moved; the production recursive policy
  remains red until that conversion is complete.

- Added the production-oriented geo-walk runtime seam with explicit node and
  sibling cursors, bounded overflow latching, and a host C contract. It is
  linked into sourceboot alongside the LWRAM owner but is not yet selected by
  `rendering_graph_node.c`; the recursive source-policy gate intentionally
  remains red until every handler has an enter/leave conversion.

- Refined the generated traversal owner to use a dedicated 16-byte SH-2
  continuation frame with both node and sibling cursors. The original 12-byte
  host scheduler frame remains a contract fixture; production storage now has
  the state required for the upcoming enter/leave dispatcher without changing
  the recursive source path yet.

- Added the deterministic full-game geo-depth manifest and linker-owned
  `.lwram_geo_traversal` arena. The generator scans all actor/level GeoLayout
  sources (518 current inputs), accounts for structural nesting plus shared,
  held-object, and callback edges, emits a 256-frame capacity with a recorded
  SHA-256 identity, and rejects missing, duplicate, undercounted, reordered,
  or undersized manifests. The linker now asserts generated byte size,
  alignment, actor-arena ordering, and slave-stack non-overlap. This is storage
  and map infrastructure only: production traversal still uses the recursive
  source dispatcher and no target/manual/FPS claim is made.

- Added the bounded Saturn geo-walk scheduler contract and host gate that will
  carry source scene-graph continuation state outside the SH-2 call stack. The
  contract records enter/leave phases, matrix/context tokens, high-water usage,
  and fail-closed capacity overflow; production source traversal is not yet
  switched over, so this change does not claim target stability or FPS.

- Reconciled the current no-texture manual image with its build identity:
  `SATURN_DEMO_PATH=0` intentionally uses the source Fast3D RGB-only VDP1
  emitter, while `SATURN_DEMO_MARIO_TEXTURES=1` only covers the Mario demo
  assets. A fresh `SATURN_DEMO_PATH=1` dual-SH2 BOB image is kept as a
  textured visual diagnostic; the full-game source route still requires
  texture residency and texture-aware VDP1 lowering. This preserves the
  DRAM/profile contract and avoids hiding the gap by changing the default.

- Added source-owned SH-2 exception capture to the dual-SH2 sourceboot path:
  both master and slave vector tables now preserve a register frame in a
  linked HWRAM record before delegating to Yaul's normal green-reset/debug
  handler. This corrects the earlier desktop evidence that over-attributed the
  blank-green screen to master-stack exhaustion; the runtime gate remains open
  until the current `.ymir-profile` desktop launch either stays stable or yields
  a decoded frame. No single-SH2 fallback, texture bypass, or linker-margin
  relaxation is introduced.

- The dual-SH2 sourceboot image now places the downward-growing slave stack in
  an explicitly reserved 16 KiB tail of LWRAM instead of the small HWRAM
  window at `0x06001E00`. The old placement could exhaust during nested render
  callbacks and let an exception frame descend below HWRAM, producing the
  observed desktop black-screen crash. Linker assertions keep static LWRAM
  arenas below the reservation; VDP1/Gouraud transport ownership and the
  dual-SH2 requirement are unchanged. Host/link evidence and a bounded Ymir
  stack probe pass, while target stability, manual visuals, and FPS remain
  open.

- The production sourceboot link now preserves the dual-SH2 configuration
  (`SATURN_SLAVE_RENDER=1`) while making CPU-only renderer scratch explicitly
  LWRAM-owned. Scene-admission traversal borrows caller-supplied scratch from
  the phase-owned terrain command bank, and CD staging/file-list storage borrows
  the prefix of the pre-initialization LWRAM main pool before `main_pool_init()`
  resets it. VDP1 command banks, Gouraud/SCU-visible staging, and other uncached
  transport storage remain in their existing HWRAM owners. The fresh serialized
  BOB link (`e2-bob-identity-id-bd57c0a81635606c`) passes with HWRAM margin
  `0x1BC8` and full-section LWRAM margin `0x4780`; this is source/link evidence only and
  does not claim target/P2, Ymir/manual, texture, audio, or FPS closure. A
  single-SH2 build is not a production substitute.

- Sourceboot now keeps the immutable build-identity tuple in HWRAM `.bootdata`
  instead of cart-resident `.rodata`. The pre-cart guard therefore validates
  before `source_cart_load()` can safely read the DRAM cart; the previous
  placement dereferenced a non-resident cart VMA and spun at `main.c:1423`,
  presenting as a black screen. The new identity-residency test and a fresh
  Pipe-4 BOB image preserve the existing profile-managed DRAM launch contract.

- Sourceboot now places the CPU-only Fast3D frontend state in the NOLOAD
  LWRAM work arena and initializes it before the bootstrap VDP2 profile read.
  This reclaims 44,616 bytes of HWRAM for the remaining link-capacity gate
  without moving VDP1 command/Gouraud staging or any SCU-DMA-visible buffer;
  the explicit init ordering preserves startup semantics for the un-zeroed
  LWRAM section.

- Reconciled sourceboot's HWRAM VDP1 command-bank placement with the deferred
  frame-bank runtime contract: command sources are now admitted only when they
  lie in the bounded HWRAM/LWRAM ranges legal for CPU-DMAC, while SCU Gouraud
  staging remains HWRAM-only. The host gate covers both HWRAM initialization and
  cart rejection. Host compiler recipes now inherit `HOST_CC_ENV`, and the
  MSYS2 launcher preflights the transitive GCC/binutils DLL closure and puts
  both runtime directories first on `PATH`, preventing bare helper launches
  from producing missing-DLL dialogs.

- Sourceboot now bootstraps its generated build-identity spec from canonical
  route, input, camera, cart, scene, actor, animation, and feature-selected
  audio provenance before selecting an output directory. Each manifest is
  hash-validated by the existing identity generator, so a clean tree no longer
  fails on a missing spec and a failed bootstrap cannot silently reuse a stale
  identity or handwritten label. The descriptive identity label remains an
  emitted artifact, while the Yaul object directory now uses a validated short
  configuration-hash tag to stay within Windows path limits.
  The bootstrap now seals a conservative full source/config/linker/tool closure
  rather than a three-file whitelist. Its scene, dependency, actor, and
  animation fields hash the exact generated feature-off comparator payloads;
  semantic audio deliberately fails closed until a staged S64A/AUDIO.DAT and
  sound-CPU image are integrated. This prevents provisional recipes from being
  represented as final package bytes; Task 22 remains the final-package owner.
  Clean sourceboot builds now run a distinct `identity-assets` stage before
  seal-stage Make parsing, so exact generated bytes are produced rather than
  silently borrowed from a dirty workspace. The sealed closure includes the
  source sky/texture roots and exact generated compile/`incbin` inputs,
  including `water.png` and its baked sky output.
  The closure now also seals generated BOB scene/BSP/fragment and quad-map
  headers used by compiled C sources. The asset-stage escape is restricted to
  the sole `identity-assets` goal; invalid stage values and attempts to use
  the asset stage for normal build/verify goals fail during Make parsing.
  Identity sealing now derives the complete sourceboot host-generated include
  closure through the same `prepare_sourceboot_assets.py` traversal and source
  roots as the asset producer, validates every selected `build/us_pc` target,
  and includes text strings. Feature-on Mario animation additionally requires
  and seals the generated actor-bank C source. Multiword stage values now fail
  before Make can treat them as a bypass request.
  Generated host headers are now followed transitively from the discovered
  asset targets, so `text_menu_strings.h` and any future quoted generated
  header include are sealed or cause a clean fail-closed diagnostic. Feature-on
  actor-bank-C absence and byte mutation are covered explicitly.

- Added a dedicated 16-byte-aligned, NOLOAD `0x10000` LWRAM actor-runtime
  owner in sourceboot, replacing standalone actor observer/bank storage and
  explicitly clearing it through the cache-through alias before binding the
  existing lifecycle. Linker symbols and exact-size/alignment assertions make
  the reservation visible. This source-only step leaves linked route, target,
  Ymir/manual, and FPS evidence open.

- Relocated the complete 32-byte-aligned sourceboot VDP1 command double-buffer
  from LWRAM to ordinary HWRAM `.bss`, preserving its explicit backend
  initialization and frame-bank lifetime while reclaiming `0x20000` LWRAM
  bytes for the separately planned actor arena. The linker now rejects any
  future `.lwram_cmdts` input. This source/link preparation does not claim
  target transfer, P2, Ymir/manual, or FPS evidence.

- Added an exact producer-owned pre-acquire actor-bank recycle path for
  stranded WRITING and unacquired READY generations. It validates bank index,
  nonzero generation, and expected phase; clears the full cache-through
  snapshot payload and metadata before publishing FREE; preserves the other
  bank and monotonic last-published generation; and rejects quarantine,
  rendering, complete, free, stale, or double dispositions. Capture and the
  immediate sourceboot acquire-refusal path now use this narrow recovery path,
  while post-acquire handoff quarantine behavior remains unchanged.

- Repaired sourceboot's actor-observer generation handoff so a source tick
  computes the frame pipeline's nonzero successor once, before its geo walk,
  and uses that value for observer opening, source-tick publication, actor
  capture, profiling, camera bypass, and idle-probe publication. This prevents
  the `UINT32_MAX -> 0` observer/capture mismatch while preserving scheduler
  cadence, the existing action-generation validation, and all public ABIs.
  Host source-contract mutations now reject raw increment, capture, and camera
  consumers that bypass the named successor. Target visibility, sourceboot
  image/BIOS handoff, Ymir/manual, and FPS evidence remain separate gates.

- Separated Task 14's source object-pool identity domain from its compact
  drawable snapshot domain. The observer now accepts and tracks all 240
  source-attested `OBJECT_POOL_CAPACITY` slots (including parent identities
  and incarnation reuse) while its immutable 188-byte snapshots and 64-byte
  queue descriptors retain the existing 64-observation ceiling. This grows
  the fixed observer sidecars from 12,320 to 13,024 bytes; the fixed 65,536
  byte actor arena remains unchanged by reducing the derived output-record
  ceiling from 2,806 to 2,718 records. Slots outside the 240-entry source
  pool still fail closed, and a 65th compact observation still latches
  overflow. This host-only repair does not alter sourceboot timing, package
  reservation, renderer cutover, target/Ymir behavior, or FPS claims.

- Added a bounded, host-only Task 16 lifecycle handoff from the authoritative
  actor snapshot bank to the existing actor queue and stable batch builder.
  It acquires one exact ready bank generation, regenerates each descriptor's
  snapshot-derived identity while preserving caller-owned material/output
  fields, quarantines post-acquire mismatches, requires terminal queue state
  before batch/complete, and requires an explicit output-consumer
  acknowledgement before queue reset then bank retirement. Zero snapshots are
  a production-safe no-op. This deliberately does not attach the handoff to
  sourceboot, change the feature-off Mario wrapper, introduce meshlet work, or
  claim target/Ymir/manual/FPS evidence.
  Review hardening makes zero-count publish and own an explicit terminal queue
  generation, binds descriptor and source ordinals to their captured snapshot
  order, rejects insufficient worst-case batch storage before acquisition, and
  records queue-reset completion so a transient bank-retire failure can be
  retried without a destructive second reset. Symbolic boundary fixtures cover
  the exact actor, output, and batch ceilings.

- Added the Task 16 feature-off actor compatibility boundary. The existing
  ACTOR_ADMIT/ACTOR_LOWER world-graph descriptors now route through explicit
  Mario callback wrappers when `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=0`, while
  an accidental feature-on build fails closed until the source-derived generic
  actor cutover is separately reviewed. This preserves the current Mario
  renderer and does not claim generic actor meshes, target/Ymir output, or FPS
  improvement. The wrapper contract gate now parses each preprocessor branch
  separately and mutation-proves both feature polarity and each callback's
  exact delegation, preventing an inverted branch from appearing green.

- Added the host-only Task 21 completion/ack ABI slice without changing any
  existing 16-byte command word meaning. The v2 mailbox now reserves a
  pointer-free 32-entry completion ring, full 32-bit active/prepared package
  status, explicit accepted/rejected/stale/fault/dropped/finished/prepared/
  committed results, and ticket-returning enqueue APIs. Completion-aware
  callers require an advertised capability; pending two-lap cursor tickets
  cannot be reused before a terminal acknowledgment, malformed or duplicate
  completions fail closed, status reads are bounded by a stable publication
  flag, and the final eight completion slots are reserved for required
  acknowledgments. A typed PLAY_REFRESH enqueue rejects zero or greater-than-
  16-bit package generations before any sound-RAM write because its existing
  word 3 remains a non-wrapping per-boot epoch. This does not add an MC68000
  producer, source service, package commit, target/Ymir, or audible behavior;
  asynchronous FINISHED identity remains deferred to a versioned tagged-event
  extension and may not yet mutate source policy.
  Rereview repairs bind each pending cursor to its exact opcode. Legacy
  enqueue remains an explicit no-ack path only while completion capability is
  absent; capability mode rejects it before any wire write and requires every
  command to reserve a ticket. An unmatched/corrupt completion latches the
  transport fault so a delayed old acknowledgment cannot retire a later
  same-cursor/opcode ticket. The repair also replaces the reusable writing
  flag with an advancing even/odd publication
  sequence, saturate the live command-ring counters at `0xffff`, and use one
  closed status/opcode matrix. Wrong same-class acknowledgments, FINISHED as a
  command acknowledgment, dropped stop commands, and generic acceptance of
  package prepare/commit now fail without retiring the original ticket.
- Added the bounded Task 21 project-owned sound-CPU boot contract. Cold boot
  and explicit recovery now have a host-proven order from staged-byte
  validation through generic SMPC stop, bounded stopped-state proof, then
  512-KiB selection,
  owner-validated clear/copy/mailbox publication, restart, and bounded READY
  plus heartbeat advance. Warned Yaul sound convenience calls are prohibited;
  the generic call's undocumented OREG31 byte is telemetry rather than a
  boolean shortcut, and every modeled failure returns a named fault without an
  unbounded wait. This does not enable sourceboot audio or claim package,
  target, Ymir, hardware, manual, audible, or performance completion.
  Soundtest now invokes and verifies that memory-mode callback and retains a
  persistent command count, last command, and raw OREG31 diagnostic. Host
  tests prove both `0x00` and `0xFF` are retained after typed completion rather
  than interpreted as false/true.
- Routed the soundtest and PCM protocol host executables through Python
  subprocess launch so MSYS quoted-path/DLL resolution cannot create GUI
  missing-DLL failures; protocol and proof semantics are unchanged.

- Added bounded BOB actor-effect infrastructure derived from the generated
  closure rather than runtime behavior-name branches. An exact pinned oracle
  covers billboard, alpha-cutout, translucent, shadow, particle, decal,
  projectile, reward, and effect records; identity, inventory, role, unknown
  feature, stale generation, and fixed-capacity mutations fail closed. The
  pointer-free descriptor path preserves source lifetime, uses the existing
  fixed-point billboard basis, distinguishes cutout replacement from true
  VDP1 half-transparency, and produces stable far-to-near order. The nine
  `GEO_CULLING_RADIUS`/`GEO_BRANCH_AND_LINK` records and all production
  observer effect fields remain explicitly unresolved, so this host-only slice
  does not claim target/Ymir/manual output, complete BOB effects, or FPS gain.
  The effect ABI requires the canonical nonzero actor-bank hash at admission
  and carries a compact trusted bank token through descriptor and lowering
  output. One shared validator protects direct lowering and master ordering
  from stale bank identity, crafted unknown bits, and unresolved source state
  instead of assuming every public descriptor came from admission.
  Lowering also requires the caller's current frame and scene-package
  generations, closing a self-referential check that could otherwise accept an
  old descriptor after ordering or across a direct-lower call.

- Added the bounded Task 19 actor-capability admission infrastructure. The
  generated BOB closure now has an independent exact oracle for ANIMATED,
  SWITCH, PARENTED, HELD, MODEL_MUTATION, and LOD, including behavior-spawned
  children and boss rewards. Generic selection admits ANIMATED, SWITCH, and
  MODEL_MUTATION only; PARENTED, HELD, and LOD fail closed until typed immutable
  snapshot evidence exists. A resealed S64F stored-mask mutation verifies that
  unknown serialized bits fail after content-hash verification. This does not
  claim runtime pose/queue/lane/parent/held/model/despawn/reward integration,
  enemy completion, target/Ymir output, manual playability, or an FPS change.

- Added closure-derived generic actor capability records for the bounded Task
  18 slice. Rigid/opaque/static-transform/platform/collectible class bits and
  transform/scale/material/surface/lifecycle runtime masks now travel in the
  pointer-free S64F v2 family bank; the family record grows from 52 to 56
  bytes, and the C validator rejects unknown or unsupported masks. The pinned
  BOB Area 1 header-content digest is externally asserted, while the pre-change
  v1 digest is rejected; the mutation test reseals its payload before checking
  the unknown-runtime-bit branch. Platform/collectible hints are analyzer
  vocabulary only and are marked unverified; schema v1 has no typed evidence
  field, so those classes and runtime surface remain unavailable/fail-closed.
  The serial family-bank gate and independent RED oracle distinguish the eight
  opaque closure records as seven representatives (the checkerboard behaviors
  share one family), five additional rigid/static-transform representatives,
  and one non-rigid `bhvBreakBoxTriangle` representative: 13 unsupported
  representatives/reasons covering 14 closure records. No family allow-list
  or fabricated source field is used. This is host evidence only and does not
  claim target/Ymir/manual/FPS completion.

- Hardened the bounded SCSP scheduler after rereview. Stale package correction
  is now transactional for the caller: an emitted key-off returns applied
  success, while zero command capacity records a fault and retains the keyed
  shadow for retry instead of forgetting hardware state. Software total-level
  attenuation is the sole simplified envelope owner; SCSP EG remains at its
  neutral immediate/full setting, and no source-faithful Project12x ADSR claim
  is made without full envelope-table traces. The MC68000 gate now discovers
  the pinned PoneSound bundle, force-rebuilds every Task 17 object, and attests
  the PoneSound commit, exact GCC executable hash/version, fresh `elf32-m68k`
  artifact, and empty undefined-symbol set, preventing cached host state from
  masquerading as freestanding evidence.

- Added a bounded MC68000-local audio scheduling infrastructure slice that
  consumes Task 15's pointer-free note events without exposing native voice
  state to either SH-2. A 20-note allocator protects music from ordinary SFX,
  steals the lowest-priority/oldest eligible SFX deterministically, preserves
  source note-duration, tuning, pan, and release inputs, and reports drops and
  malformed or duplicate generations. A simplified timer-driven linear
  attack/decay/sustain/release model feeds software attenuation and is not a
  trace-backed close-port of Project12x's arbitrary envelope tables. A
  32-slot desired-voice shadow emits only changed scalar register commands,
  with key-off before reassignment and key-execute last. Raw SCSP writes remain
  confined to an explicit `scsp_pcm8` command executor. Serialized host gates
  and a freestanding MC68000 relocatable-module/undefined-symbol gate pass;
  the modules are deliberately not linked into the heartbeat image or runtime
  loop because Task 12's real package/sequence assets and residency bindings
  are absent. Production timer IRQ wiring, complete source envelope tables,
  full PCM68K image/package, target, Ymir, hardware, manual, and tempo evidence
  remain open rather than inferred from this infrastructure result.

- Added a bounded, pointer-free M64 sequence VM scaffold for the MC68000
  audio lane. It preserves Project12x compressed-delay, tempo-accumulator,
  call/return, loop, branch, and note timing semantics while emitting scalar
  control/note events only; it performs no SCSP writes and is not linked into
  sourceboot. An eight-frame stack, 64-instruction tick budget, bounded event
  output, and strict operand/target validation fail closed on malformed,
  truncated, unknown, or non-progressing streams. The serialized
  `verify-sequence-vm` gate launches through Python subprocess to avoid the
  inherited MSYS quoted-executable/DLL failure. No expanded seq00 bytes are
  present, so full sequence/catalog, S64P, target, Ymir, and manual audio
  claims remain open.

- Tightened the sequence VM's source parity at the layer boundary: portamento
  uses the source's special one-byte timing form when its mode bit is set, and
  layer transpose consumes the source unsigned byte representation. The
  focused VM gate covers the special portamento operand shape.

- Hardened the bounded sequence VM's source contract after review. Layer
  velocity and short-note duration now persist across delayed ticks (including
  the source `0x80` initial duration); an explicit US versus EU/SH format
  selector disambiguates the reserve/unreserve opcodes and EU/SH relative
  branch predicates; channel init/disable/test commands maintain bounded
  active/finished masks; and RED tests cover stack overflow/underflow,
  zero-count 256 loops, output overflow, invalid complete targets, branch
  polarity, layer call/loop, format-specific reserve operands, and persistent
  short-note state. This remains a host-only VM slice: no target compiler,
  SCSP/PCM driver, expanded seq00 payload, full sequence catalog/S64P closure,
  Ymir, hardware, or manual audio evidence is claimed.

- Narrowed the format-specific VM boundary after rereview. Large-layer note1
  now stores and emits its source-mandated zero duration, EU/SH layer relative
  jumps use the bounded flow engine, and `seq_initchannels` accumulates selected
  channel bits instead of discarding earlier selections. EU/SH sequence `0xda`
  fade-state and `0xdc` tempo-add forms are rejected before operand consumption
  until their distinct state machines are represented; the RED fixture covers
  these fail-closed cases and the large-note transition. This keeps the claim
  limited to the bounded host VM and does not imply full EU/SH package support.

- Added a dedicated, bounded actor-instance queue and master-only batch merge
  without expanding the proven eight-entry world graph. Each pointer-free job
  owns one Task 14 snapshot identity and a disjoint output span, either SH-2
  may claim it exactly once through the existing P2/TAS publication pattern,
  and stale generation/package/bank/incarnation or claimant failures
  quarantine only that instance. Stable batches preserve published painter
  order while grouping adjacent compatible family/material work. The queue's
  fixed memory report derives from the shared snapshot ceiling; BOB capacity
  remains an explicit package-evidence gate rather than an assumed claim. The
  DLL-safe inherited gate also now checks the worker context's current inline
  references instead of requiring primitive/material pointers that were
  deliberately removed from that context. Rereview accounting now places the
  Task 14 two-generation bank container, observer, queue, 64 master batches,
  and 2,806 eight-byte output records in one 16-byte-aligned 65,536-byte actor
  arena; record 2,807 fails closed. Batch construction reads its count through
  the queue's P2 alias. This remains infrastructure source-incomplete until
  generic actor-meshlet preparation and a production drain/cutover exist; the
  concurrent retirement race and all target evidence remain open. The runtime
  memory report exposes every component, output byte, and alignment byte, and
  executable boundary mutations pin overflow quarantine, the valid 64th
  instance, and exact-fit publication.

- Added the scene-neutral admission boundary for validated render packages.
  Generic cluster/node/portal views now reject malformed metadata before
  traversal, perform conservative Z-Treme-derived frustum tests before
  transform/classify/lower, preserve mandatory clusters, and emit ordered
  bounded references with cycle/capacity telemetry. BOB adapts through a
  deterministic package-view helper while the full runtime remains
  scene-independent; target/Ymir activation and FPS evidence remain open.
  The serial DLL-preflight gate also uses Python subprocess launches for the
  host executables, avoiding the inherited MSYS quoted-path EOF failure.
  The production BOB render-prep now constructs and consumes this package
  view; frame-owned orientation overrides static metadata, portal endpoints
  must agree in both adjacency lists, and queued nodes are marked before
  enqueue to keep bounded traversal deterministic.
  The sourceboot link list now includes the admission implementation, and the
  generated BOB header emits the generic admission node/ref section consumed
  by render prep; the 1,183-node source BSP remains available to the fallback
  painter without forcing the generic bounded worklist to truncate it.
  Generated scene headers now include the admission ABI directly, so emitted
  node/ref records are compile-time type checked at the consumer boundary.
  Zero lateral view rows now force a depth-only conservative fallback instead
  of inheriting stale package axes; package validation requires global cluster
  coverage, node containment, endpoint-only portal ownership, and zeroed
  reserved fields, while mandatory clusters are retained even when their node
  is outside the current frustum.
  Generated render-cluster records now explicitly initialize the optional scene
  identity fields, keeping the generated-header Werror smoke gate aligned with
  the generalized ABI.

- Added the generic source-resolved actor-instance seam. The geo observer records
  only authoritative scalar decisions, while two bounded snapshot banks publish
  pointer-free actor state with pool-slot/incarnation identity, package/bank
  hashes, parent/held offsets, switch/render/shadow/effect fields, and
  fail-closed stale-family, malformed-source, and capacity handling. The
  sourceboot capture is ordered after the authoritative tick while the
  observation window opens before that tick; host gates cover typed-field
  mutations, model-less controllers, despawn/reuse, and immutable bank
  lifecycle. Production geo hooks resolve bounded pool/model scalars, but
  unresolved generated family/scene/bank identity is deliberately rejected
  rather than fabricated. The render-snapshot gate now launches its Windows
  host test through Python subprocess to avoid the inherited MSYS quoted-path
  EOF error.

- Hardened actor snapshot publication after lifecycle review: observer
  overflow now latches for the frame, capture bounds-checks every source pool
  slot before reading incarnation state, and separately reports legal source
  pool slots that exceed the bounded observer capacity instead of silently
  dropping them. Telemetry resets per source frame so despawn/reuse and
  capacity failures remain attributable. Actor banks reject stale or duplicate
  generations with the shared wrap-safe helper, retain independent generation
  tickets per physical bank, and publish/acquire lifecycle state through the
  SH-2 cache-through alias with compiler fences. The physical bank/observer
  assertion now binds to Yaul's target-declared `LWRAM_SIZE`; the Task 16
  fixed actor-arena cap remains a separate queue contract. Production source
  fields without an authoritative generated registry (family, scene package,
  bank, visibility/range/switch/opacity/held-parent/effect state) remain
  explicitly zero/default and are rejected by capture rather than fabricated.
  The linked sourceboot gate remains open until the Yaul/MSYS make wrapper is
  repaired and the generated actor-family registry is bound at the production
  geo seam.

- Connected actor capture to the real two-bank sourceboot handoff. A published
  bank is claimed for the render overlap window and is retired only at the
  terminal boundary; failed publication or rendering quarantines that exact
  generation. This keeps actor state immutable while preserving the bounded
  64-instance package ceiling.

- Hardened the Saturn audio package boundary after ABI review: chunk and
  package SHA-256 values are recomputed by the C residency validator, malformed
  replacement generations fail closed without mutating the active plan, and
  the MC68000 token retains the complete source digest. The catalog binds
  BOB/WF closures through content-addressed S64P dependency records, preserves
  signed PCM polarity and source metadata, rejects empty sequence inputs, and
  emits checkout-portable sample paths. These checks keep generated audio
  artifacts deterministic while leaving playback/transport integration open.
  The MC68000 header and implementation now share the same full 32-byte source
  digest field, so target compilation cannot silently validate a stale CRC ABI.
  Scene dependency digests now hash framed sequence, bank, and PCM bytes and
  publish an explicit `audio/bob` or `audio/wf` root with its selected chunk
  hashes.
  Sequence 00 now requires the expanded generated payload; a wrapper-only
  `sound_data.c` input fails closed and blocks the complete package gate until
  the real source asset is supplied.
  Residency plans now model a bounded scratch/work span, reject all overlaps
  through a shared public validator, and require validated active/replacement
  plans at commit and MC68000 acceptance rather than trusting caller spans.
  AIFF MARK/INST loop markers and bank-side tuning/envelope/pan bindings are
  retained in the manifest, invalid all-zero/short `.m64` control streams fail
  closed, and C package validation rejects overlapping descriptor payloads.
  MC68000 acceptance now receives both active and replacement plans and rejects
  cross-generation span overlap before acknowledging a replacement.
  The disjointness helper is null-safe before inspecting either plan, ignores
  zero-length spans, and MC68000 admission validates both active and
  replacement plans.

- Added the source-authoritative S64A audio catalog compiler.  It consumes the
  35 sequence mappings, 38 banks, and 219 user-extracted AIFF samples directly,
  records source/package hashes, emits aligned big-endian AUDIO.DAT chunks and
  BOB/WF closure manifests, and converts PCM16 to deterministic Saturn PCM8.
  The bounded residency contract retains an active generation until the
  MC68000 acknowledgement and rejects post-boot whole-RAM clears; generated
  catalog data stays untracked and target playback/transport integration remains
  a later gate.

- Closed the remaining S64F admission-integrity gaps: runtime capability
  selection now ranks total family capability bits, the host validator rejects
  empty/unknown-flag banks, and the C validator recomputes and optionally
  binds the payload SHA-256 before exposing records.  The executable family
  test now checks the unequal-capability selection invariant and payload
  tamper rejection.

- Corrected generic family selection to exclude model-less/controller records
  from drawable runtime admission while retaining them in the closure report;
  both the Python proof and C selector now require the immutable geometry flag.

- Added a generic, content-addressed S64F actor-family bank compiler for every
  BOB closure record.  Source geo vocabulary, material flags, animation,
  multiplicity, effects, and model variants become stable capability records;
  unsupported geo nodes and stale closure hashes remain explicit so supported
  families can still be inspected without pretending the closure is complete.
  Family payloads use bounded offsets and source provenance, and the C ABI
  selects the smallest supported capability/capacity record without a
  Goomba- or scene-specific runtime branch.  Target/Ymir activation and final
  scene-package linkage remain intentionally open.

- Hardened complete-animation promotion against extreme Q16.16 translation
  sums and matrix overflow by failing closed before narrowing; sourceboot now
  hands each selected pose through a two-slot immutable render buffer so frame
  overlap cannot overwrite a worker's vertices.  The diagnostic sweep hashes
  that selected pose without invoking a second evaluator, while the legacy
  walking-bank compatibility fields remain feature-off only.

- Added the feature-selectable compact Mario pose evaluator.  The enabled
  sourceboot path consumes the source-selected animation ID/frame after the
  authoritative geo tick, evaluates one bounded 20-joint pose from the
  validated 209-ID S64B bank, and feeds the existing meshlet admission path;
  it does not introduce a Saturn animation clock or duplicate material/switch
  ownership.  Feature-off retains the legacy generated pose selector.  The
  serialized variant builder seals matching ELF/CUE/ISO hashes, while the
  non-promotable animation-sweep validator rejects missing/duplicate IDs,
  diagnostic-only evaluator symbols, fallback/corrupt telemetry, and identity
  drift.  Target/Ymir sweep evidence remains outstanding until a linked
  variant reports all 209 IDs.

- Replaced sourceboot's optional silent-audio path with a feature-selected,
  source-authoritative SH-2 policy adapter while preserving every public
  `src/audio/external.h` signature and retaining the silent translation unit
  as the feature-off rollback.  The adapter keeps the six-entry background
  queue, priority/duplicate handling, secondary music, jingles, fades,
  lower/unlower constraints, bank masks, one published SFX per bank,
  continuous freshness, stops, getters, and moving-source spatial updates on
  the SH-2.  Admission uses the inherited requested-priority plus exact
  distance/front weighting, and volume/pitch retain per-level acoustic reach,
  bank range, moving-speed, constant-frequency, and vibrato rules before
  quantization; distance uses the repository's existing target `sqrtf`
  service instead of a duplicate 24-iteration divider loop.  Active source
  positions are reevaluated each game-audio tick; waiting discrete requests
  enter a bounded 256-record request queue before one per-bank selection, so
  same-frame bank masks/stops/getters observe the inherited pre-admission
  state; queued pointer-token positions are reevaluated at admission rather
  than frozen at `play_sound()`, and requests expire after the inherited
  countdown.  Pending requests also retain their token lease across
  same-frame stop calls until they are admitted or rejected,
  and spatial refresh is keyed by the full sound handle as well as the source
  token so simultaneous cross-bank sounds sharing one position keep their
  own bank- and flag-specific volume, pitch, and priority,
  published requests retire through generation-matched completion feedback,
  and invalid per-bank sound IDs fail before consuming an identity slot.
  Jingle/secondary completion, published-SFX lowering, and global fades now
  produce bounded aggregate semantic actions; the latter uses one non-menu
  bank-mask record instead of nine indistinguishable channel records.  The
  Saturn/SH two-tick jingle guard, secondary `0xFF` no-op, normal-volume
  sentinel, and stop-bank lowering restoration remain source-compatible;
  early generation-matched ENV completion is retained until the guard drains
  rather than being lost, while a completion made stale by a newer ENV
  generation is discarded so it cannot block later feedback.
  Protocol-v2 `PLAY_REFRESH` records carry only fixed-width
  `soundBits`, generation-tagged source tokens, package/freshness generations,
  and quantized volume/pan/pitch; raw `f32 *pos` identities stay in a bounded
  SH-2 table, and exhausted token slots retire instead of wrapping into an
  ABA collision.  The task deliberately does not add a 68000 sequence VM, sample
  packages, SCSP voice integration, or claim target/Ymir audio.

- Added a static source audit and an illustrative digest model for the proposed
  state-only source-geo optimization, while leaving it deliberately disabled.
  The audit identifies animation, painting, water/moving-texture,
  camera/matrix-derived object, lifecycle, and visibility work interleaved
  with display construction, but it does not execute the real graph and is not
  differential evidence.  A reserved digest API therefore fails closed without
  touching caller output.  The optimization remains blocked, its true
  normal-versus-suppressed graph differential remains undone, and the normal
  full geo walk stays authoritative.

- Hardened the compact Mario actor bank after independent review.  The target
  decoder now proves every packed GEO1 table boundary and count, joint/branch
  ownership and node ordering, RGB555 material, meshlet bounds/tier spans,
  globally gap-free source-ordinal ownership, exact tier primitive/vertex relationships,
  primitive material/vertex ownership, and the compiler-derived minimum
  scratch requirement before exposing a bank.  Callers may bind validation to
  the expected eight-word source identity, so a nonzero but wrong source digest
  also fails closed.  Host validation pins the repository's actual 193-path
  animation inventory at commit `68f9dd10` via canonical path-set SHA-256
  `2d7c66e9…c67e1`, in addition to unique provenance paths, lowercase hashes,
  per-animation membership, and payload digest binding.  This rejects both
  in-range internally inconsistent GEO1 payloads, coordinated duplicate/gap
  meshlet partitions, degenerate primitive shapes, and self-consistent resealed
  filename repartitions; the JSON schema is advisory while executable
  validation owns semantic authority.

- Added a deterministic, content-addressed, big-endian `S64B` Mario actor
  bank that retains all 209 source animation IDs from all 193 animation files
  as deduplicated, length-prefixed index/value channels instead of expanding
  8,140 frames into roughly 24.16 MiB of posed vertices and light inputs.  The
  596-KiB payload carries checkout-stable source hashes, the 20-joint source
  hierarchy, joint-local vertex ownership, branch/node ordinals, materials,
  primitives, meshlets, a 3,928-byte scratch bound, and an S64P dependency
  descriptor.  A bounded C decoder rejects malformed stream spans, hashes,
  skeletons, and ownership; differential generation proves every frame of the
  legacy idle and walking fixtures produces exact vertices and light inputs.
  The old Mario animation-object converter and 1.32-MiB compatibility mesh
  remain byte-identical.  That compatibility mesh still represents only the
  normal-cap/front-eye/open-hand selection; source switch-variant geometry and
  production frame/action cutover remain explicitly owned by Task 10.

- Replaced the single PCM proof command ring with pointer-free, big-endian
  semantic-audio protocol v2 rings: eight protected control records at
  `0x04040` and twenty-four SFX records at `0x040C0`.  Two-lap cursors retain
  every physical slot, validate corrupt producer/consumer distances, and
  publish producer/consumer cursors only after record/telemetry bytes.  The
  MC68000 validates both rings and the protocol version before consumption,
  always spends its bounded poll budget on control first, and reports separate
  control/SFX saturation and consumption counters.  This prevents an SFX
  burst from dropping future music/package control while keeping rendering
  and simulation independent of audio service.  The exact v1 ABI remains a
  tested historical contract, and the audible proof soundtest now emits the
  equivalent reset/master/proof-tone commands through v2 only after the new
  host gates pass.

- Bound every S64P residency reference to a bounded, nonzero lease token
  instead of trusting an aggregate consumer count. Duplicate acquisition,
  duplicate/stale release, and token-table exhaustion now fail closed without
  changing another snapshot, VDP1 frame-bank, actor-bank, or audio-voice
  lease, preventing an old generation from being unloaded while a legitimate
  consumer still owns it.

- Hardened S64P residency after independent review.  Malicious resealed roots
  can no longer drive an unsigned offset underflow and out-of-bounds scan.
  Residency now copies roots and feature-active payloads into explicit,
  non-overlapping caller-owned spans and rehashes those owned bytes at atomic
  commit, so later mutation of CD/cart staging input cannot alter a published
  generation.  Absolute aligned placement retains old and new generations
  without overlap; refcounted render-snapshot, VDP1-frame-bank,
  actor/animation-bank, and audio-voice hooks prevent early reuse.  Sourceboot
  now exposes only the aligned CART range above `SOURCE.DAT` and strictly
  rejects an optional linked provisional root before entering the game loop.
  Runtime stable-ID, zero-generation, and zero-byte rules now match the Python
  S64P validator, and the target cart boundary explicitly declares its
  freestanding memory primitive rather than relying on an implicit prototype.

- Added bytewise big-endian target validation and exact-generation residency
  for version-one `S64P` roots.  Root, section, canonical dependency-set, and
  external payload hashes now fail closed before placement; feature-inactive
  payloads remain validated without consuming residency.  Root plus active
  payloads commit atomically, old generations cannot be evicted until their
  render, bank, and voice consumers retire, and immutable render snapshots
  carry only scalar package/bank identities.  Available CART capacity is
  injected explicitly so the existing native-pointer `SOURCE.DAT` prefix is
  never mistaken for free memory, and sourceboot rejects provisional roots.

- Added the generic, versioned, big-endian `S64P` scene-root compiler,
  validator, and C ABI emitter.  Roots now bind all eight closed section kinds,
  sorted content-addressed actor/animation/audio descriptors, package and
  dependency-set hashes, lifetimes, destinations, dependency masks, alignment,
  and scratch/budget claims.  The first BOB area-1 artifact is deliberately
  marked provisional and rejected by normal validation, so it can exercise
  deterministic world/collision/sky/BSP packing without inventing unfinished
  feature payload hashes or being mistaken for Task 22's final root.  External
  dependency edges are expressed as stable-ID references and normalized to
  canonical descriptor ordinals, preventing shuffled compiler inputs from
  silently changing masks; generated scenes include one shared guarded ABI
  header rather than redeclaring package types per root.

- Closed the last Task 3 fail-open scanner paths after final rereview.  Every
  indexed symbol reached through native functions, data, action tables, or
  function-pointer tables must now resolve uniquely, rather than applying the
  ambiguity check only to direct-call syntax.  Behavior audio now follows a
  bounded, sink-directed value flow through direct sound APIs, local aliases,
  forwarding-wrapper parameters, and reached sound tables; passing a
  `SOUND_*` value to an unrelated call no longer invents an SFX dependency.
  This preserves the 86-record / 133-source BOB closure while narrowing its
  audio union to the 54 source-proven IDs actually capable of reaching a sink.

- Closed the final Task 3 scene-closure provenance bypasses.  The bounded
  native index now resolves callbacks and reachable helpers across canonical
  repository `src` definitions (while excluding mutually exclusive port
  overlay stubs), and missing callback definitions fail before output.
  Behavior records retain every concrete model/geo variant with exact
  model-to-geo binding provenance, every spawned child has one complete type,
  and schema validation resolves the cited behavior/model/geo/animation
  symbols rather than trusting path membership.  Audio IDs now come from
  concrete call arguments, local value flow, and reached data definitions, so
  generic-helper comparison constants cannot leak into a behavior.  Per-site
  BehaviorScript recurrence replaces block-wide capacity inference, BOB's
  Goomba triplet bound is backed by explicit state/child-deletion attestations,
  and entry `JUMP_LINK`s are expanded with comments removed before area/music
  selection.  Consumers now receive both White Puff bubble and mist variants;
  the authoritative BOB host closure remains 86 behaviors and grows from 127
  to 133 hash-covered sources because the additional binding evidence is
  explicit.

- Repaired the scene-closure collector's repository boundary: native callback,
  helper, respawner, particle, sound-spawner, and loot/triangle-effect routes
  are now walked across bounded `src/game` sources, with every reached source
  hashed and every unresolved dynamic creation rejected.  Recursive
  `JUMP_LINK` evaluation now separates area-local objects/music from global
  model loads, resolves both geo and display-list model roots plus animation
  tables, and records schema-checked root provenance.  Live counts no longer
  treat a recurrent spawn burst as total capacity: tighter source-proven
  active-set/one-shot bounds are retained (including BOB's 11 Goombas), while
  recurrent paths without a provable cadence/lifetime use the explicit
  `OBJECT_POOL_CAPACITY` ceiling and fail if that source cap is unavailable.
  This closes omitted BOB respawner and mist/white-puff paths and expands the
  authoritative host closure from 76/78 to 86 records / 127 source hashes.

- Closed the remaining scene-closure rereview gaps by making behavior-spawn
  rules repository-relative, hash-covered source attestations of their native
  owners, exact model/behavior sites, and bounded capacity expressions.  Rule
  routes now traverse local helpers, action/data tables, and audited generic
  dispatch bridges, rejecting duplicate owners/edges, unrelated sources,
  nonexistent sites, and invented counts; this corrected stale water-bomb,
  explosion, Koopa-shell, coin-helper, default-star, and wooden-post edges.
  Audio dependency collection now follows only each behavior's reachable
  native functions/data and resolves every reached `SOUND_*` identifier to one
  declared `SOUND_ARG_LOAD(SOUND_BANK_*)` entry, hashing `include/sounds.h` and
  failing on missing or ambiguous declarations.  The stricter BOB result is 76
  records / 78 source hashes, preventing unrelated sounds from leaking out of
  a shared source file while preserving fail-closed helper/area/geo/schema and
  canonical-byte guarantees.

- Scene closure now walks bounded source-defined native helper chains from
  BehaviorScript callbacks, rejecting unknown computed spawn arguments before
  output. Recognized grill-table expansion remains explicit, while newly
  discovered concrete coin/star helper effects must be reviewed as normal
  source-attested edges.

- Scope LevelScript inline object discovery to the requested `AREA`, while
  retaining only that area's linked local scripts, so objects from another
  area cannot inflate a scene package's dependency or capacity closure.

- Extended BOB's reviewed native closure to include Koopa-shell wave, droplet,
  flame, and sparkle chains plus exclamation-box computed cap/star/marker
  contents. This prevents those visible effects and rewards from being omitted
  merely because their native spawn calls are reached through helpers or a
  source-owned contents table.

- Hardened scene-closure derivation after review: every reachable native
  `spawn_object*` edge now requires a source-attested, unique reviewed rule or
  generation fails. The BOB closure consequently includes model-less
  controller products (checkerboard platforms, grill halves, cannon opening,
  hidden pole 1-Up triggers) and transitive explosion/sparkle effects that the
  initial inventory omitted. Capacity is now evaluated per compatible act,
  reachable spawn cycles fail explicitly, and SFX banks derive from source
  identifiers rather than a blanket `general` label.

- Added a deterministic, generic scene-closure generator and its versioned
  schema. It follows LevelScript declarations, macro presets, model/geo
  bindings, BehaviorScript children, and explicitly reviewed native computed
  spawn rules, with source hashes and fail-closed validation. BOB's former
  hand-maintained Goomba count is now one generated capacity fact (11), so
  model-less controllers, rewards, effects, and cyclic behavior graphs cannot
  be silently excluded from later actor, animation, or audio package work.

- Closed the first independent-review gaps in the sourceboot build identity and
  historical A9A archive. The effective-config digest and identity-derived
  output label now bind every remaining compiler-affecting wrapper control,
  including atan2, demo/camera/slave-render, flat-fragment, trace, and
  diagnostic geo-walk switches, preventing distinct binaries from sharing one
  claimed identity. Baseline archival now requires the exact accepted cadence
  and target records, the hash and parsed 32-Mbit DRAM content of the actual
  Ymir profile, and the exact successful matching launch report and logs. It
  also rejects a conflicting manifest before creating or copying archive
  files, so a failed archive attempt cannot leave a partial accepted trio.

- Bound sourceboot diagnostic artifacts to a fixed-width, versioned target
  identity containing the complete feature tuple, behavior configuration, and
  hash-verified source/route/input/camera/cart/scene/actor/animation/audio
  inputs. Capture now resolves the identity from the exact ELF, checks the
  loaded target bytes before telemetry, and derives labels from those compiled
  bytes, closing the prior gap where a directory label could describe
  behavior or packages the ELF did not contain. The accepted 5.294 FPS A9A
  ELF/ISO/CUE is separately archived after exact hash, CUE-reference, profile,
  config, capture, and preserved-commit ancestry checks so later feature-off
  descendants cannot silently replace the historical rollback baseline.

- Reconciled the SH-2 native-math verifier with the live descriptor-queue
  renderer route. The pinned oracle now follows the lifecycle and master/slave
  callback tables instead of removed frame/terrain-worker symbols, and every
  oracle identity must own an unambiguous linked disassembly block. Sourceboot
  may suppress only the two exact BOB camera-trigger calls proven unreachable
  through the null camera-table guard; route, table, control-flow, third-call,
  or owned-block drift remains fail-closed. The proof is bound to the reviewed
  source-file identities and exact linked-ELF SHA-256, rejects mutation of the
  BOB level value before current-level publication, derives callbacks only from
  the table returned into runtime activation, and validates start/poll ownership
  inside those root function bodies rather than accepting unrelated decoys.

- Hardened the Task 10 sourceboot pipeline selector: `SATURN_RENDERER_PIPELINE`
  now accepts only the reviewed `2`, `3`, and `4` variants and is passed into
  the SH-2 preprocessor flags. Previously `-pipeN` changed only the output
  directory, allowing an artifact label to claim a pipeline that the compiler
  never selected; the new source contract catches that drift before a target
  build.

- Corrected the sourceboot memory-map verifier to require `.uncached` to be
  initialized `PROGBITS`, matching the pinned Yaul ELF contract. The section
  contains the slave SH-2 entry and executable cache-through helpers, so the
  previous `NOBITS` requirement would approve an image that omitted required
  bytes and reject the real linked artifact. The P2 address, physical HWRAM
  end, margin, and LWRAM checks remain fail-closed and unchanged.

- Repaired the A9A target's HWRAM boot boundary after retries against the
  unchanged reviewed ELF failed target identity at both 600 and 4,096 startup
  VBlanks. Its exact map placed `___end` at `0x061040D0`, `0x40D0` bytes past
  physical HWRAM, because the bulk primitive-tier and cluster-LOD arrays had
  been moved into P2 `.uncached`; the linker margin subtraction wrapped and
  did not reject the image. Those arrays now share one CPU-only LWRAM object
  reached through one canonical P2 alias on both SH-2s, while the small
  exact-generation lifetime record remains uncached. Linker and ELF gates now
  reject HWRAM/LWRAM upper-bound overflow before subtracting their required
  margins, including the route-0 LWRAM floor. This is a source repair only:
  independent review, a fresh serialized target build, repaired-image boot,
  P2/map evidence, capture, and FPS remain open.

- Repaired the A9A Step 11 throughput observer after the sole target build
  exposed an intentional runtime-layout evolution. The capture now recognizes
  exactly two source-validated SH-2 layouts: the reachable 92-byte legacy
  runtime with telemetry at byte 28 and the reviewed 104-byte marker-enabled
  runtime with telemetry at byte 40. It reads the resolved symbol size and
  decodes every sequence/counter relative to that layout; nearby or unknown
  sizes still fail closed. Git history retains the initial pre-Ymir observer-
  contract failure; the canonical report path was later updated by the
  authorized unchanged-target identity retry documented above. No target
  rebuild, Ymir launch, capture retry, or FPS claim accompanied the observer
  repair itself.

- Closed the second A9A review-fix source round by moving every LOD lifetime
  object read by either SH-2 into the linker-owned P2 `.uncached` partition.
  Runtime marker clocks now stamp the real notify and positive-retirement
  release sites, and publish the phase record before waking the slave or
  exposing retirement so neither CPU can observe a half-published boundary.
  The production-linked integration gate combines a deferred scene reset with
  terminal quarantine, proves reset happens only after exact-generation
  finish, asserts nonzero `QQ`, and catches late-marker and ignored-generation
  mutations. Target/Ymir evidence remains open pending two-stage rereview.

- Hardened the A9A frame-lifetime split after independent review. A renderer-
  owned generation gate now defers source scene/LOD resets until the active
  slave generation retires, lifecycle observers timestamp the actual notify
  and retirement publications, and sourceboot attributes complete master
  construction from first service through final lowering while retaining
  master finalization as a reported subset. Terminal queue telemetry is
  refreshed before reset so failed generations publish their real quarantine
  count. A production-linked host harness covers the N/N+1 transition and
  catches all four regressions. Historical cadence v1 compatibility is
  explicitly limited to direct or saved 60-byte buffers; live target capture
  continues to require the current v2 76-byte symbol.

- Split sourceboot's accepted demo renderer into exact-generation start and
  poll/finalize phases so slave construction for frame `N` can remain active
  while the master executes the single queued source tick for `N+1`. The
  retained snapshot and BUILDING bank now survive PENDING; positive slave
  retirement permits one master drain/merge/Gouraud/VDP1 lowering pass, while
  failure quarantines without serial replay. A8 remains the sole transfer and
  resident-list owner, so this changes CPU construction lifetime without
  moving presentation or VRAM ownership.
- Extended the cache-through cadence record from version 1/60 bytes to version
  2/76 bytes with separate slave-work overlap and master-finalization counters.
  Historical v1 direct/saved buffers remain decodable; live target observation
  requires v2. Version 2 reports the slave interval as a non-additive overlap
  window so phase attribution cannot double-count source work that ran
  concurrently.

- VDP2 composition now consumes the immutable camera carried by the displayed
  VDP1 bank together with explicit displayed/rendered/simulation generation
  metadata. The HUD labels that tuple, and a mismatched camera or render bank
  is rejected before VDP2 side effects; a tuple change also refreshes the HUD
  immediately instead of waiting for the normal metric interval. Bounded
  simulation lead therefore cannot silently mix sky or telemetry with an
  older framebuffer.

- Added the hardware-free A9 frame scheduler model with a presentation-scoped
  two-tick simulation budget, explicit generation-matched render/transfer
  completion, wrap-safe generation validity, bounded once-per-field
  service/poll actions, two-phase target publication acknowledgement,
  previous-frame reuse, and dropped-credit telemetry. Mutation gates reject
  four-tick catch-up, repeated-observation credit, and incomplete publication.
- Replaced sourceboot's per-outer-loop simulation catch-up with the reviewed
  six-action frame adapter. Authoritative game logic remains 30 Hz, useful
  render/transfer/presentation service remains field-rate, successful hardware
  publication is acknowledged before VDP2/cadence evidence, and generation
  zero stays reserved across scheduler, snapshots, and VDP1 banks. Existing
  `vblank_credit` telemetry names remain stable, but now report discarded whole
  30 Hz tick credits rather than raw fields.
- Corrected throughput reporting to derive FPS from the cadence trace's real
  ISR VBlank clock after simulation and presentation generations were
  decoupled. This prevents a false 60-FPS report; the exact A9 adapter capture
  measures 4.463 FPS mean, a 2.752x improvement over its pinned baseline.

- Added a 60-byte cache-through A9 cadence trace and exact-capture decoding for
  wrap-safe VBlank crossings in simulation, synchronous frame construction,
  and transport/presentation. This replaces misleading absolute claims from the
  16-bit FRT accumulators while leaving scheduler behavior unchanged.

- Extended the exact-identity sourceboot throughput capture with a configurable
  presentation-event depth and bounded final diagnostics on cadence failure.
  This replaces one-interval A8 guesses with a repeatable multi-frame sample
  while retaining queue/runtime evidence when a slow target misses the bound.

- Fixed A8's first live publication failure when Mario is fully culled by
  meshlet admission. Zero admitted actor positions now publish a terrain-only
  two-job graph instead of manufacturing invalid zero-length actor jobs and
  permanently quarantining both VDP1 source banks. Visible actors retain the
  four-job terrain-plus-actor graph and the same dependency checks. Actor
  preparation now reports success separately from its admitted count so an
  invalid pose or meshlet failure still fails closed rather than masquerading
  as successful culling.

- Fixed A8's target-only VDP1 transfer-descriptor initialization by explicitly
  converting Yaul's integer VRAM address to the descriptor pointer type. Host
  mocks exposed the address as a pointer and therefore missed the SH-2 compile
  failure; a source contract now guards the target-safe conversion.

- Replaced sourceboot's per-emitter blocking Gouraud transfer and CPU command
  upload with an A8 two-phase frame-bank transport. After the prior VDP1 list
  is overwrite-safe, command and Gouraud descriptors commit atomically to one
  serial queue, use completion-interrupt-owned CPU-DMAC channel 0 and guarded
  SCU-DMA level 0, and retire before one master-owned resident-list
  arm/publication. Stale iterations service both serial stages without one
  stage per VBlank; published banks carry immutable VDP2 camera state; and a
  partial resident-VRAM failure disables plotting instead of reusing old
  metadata. Full declared destination ranges and Gouraud alignment are checked.
  Exact per-ticket failures drain their accepted sibling before quarantine,
  preserving the prior publication. This removes the immediate transport wait
  from the accepted frame path and reports zero at nonexistent wait sites;
  rereview and target/Ymir/FPS validation remain pending.

- Hardened A7 publication after review: wrap-safe ordering now quarantines a
  late completed bank instead of regressing the current publication; manager
  initialization rejects aliased, overlapping, or misaligned command/Gouraud
  storage; and both renderer paths return failure when Gouraud queue submission
  remains unavailable after one bounded drain/retry. Sourceboot therefore
  retains the prior complete frame instead of publishing commands whose
  Gouraud dependency was never submitted.

- Replaced sourceboot's ad-hoc VDP1 bank XOR with an explicit two-bank
  lifecycle covering construction, transfer obligations, publication,
  quarantine, and retirement. Only a renderer-confirmed complete frame may
  publish; failure retains the previous complete bank, and build, published,
  and displayed generations are tracked separately. This closes the stale or
  overwritten source-bank hazard required before deferred DMA. Transfers still
  complete synchronously in this slice, so it intentionally claims no FPS
  improvement; A8 will introduce asynchronous submission and polling.

- Added a bounded sourceboot throughput capture for the remaining A5.9 queue
  observation gate. It binds an explicit CUE/ELF/Ymir triple by hash, verifies
  a linked immutable ELF code window in the running target before sampling,
  reads runtime and queue records through P2, and fails closed unless terminal
  queue telemetry and two VDP2 presentation edges are coherent. This replaces
  unreliable manual HUD transcription without changing target code, queue
  policy, or the already measured 3--4 VDP1 FPS result.

- Added bounded automatic desktop-Ymir performance capture. The helper reads
  Ymir's native one-second window-title counters for VDP1 framebuffer swaps,
  VDP1 completed draw calls, VDP2 frames, GUI rate, and emulation speed,
  records every sample with exact CUE/ISO identity, and leaves the visible
  emulator open for manual testing. This removes OCR and manual title-bar
  transcription from FPS comparisons.

- Recorded the first desktop-Ymir result for the atomic shared-SH-2 renderer:
  it remains roughly 3–4 FPS, matching the prior A3+A4 candidate. The cutover
  is retained as a correctness/ownership foundation, but no performance gain
  is claimed; per-CPU phase claims and terminal waits are now the required
  evidence before further scheduler conclusions.

- Cut the accepted Saturn frame atomically from three fixed terrain/Mario
  joins to one four-phase dependency graph shared by both SH-2s. The renderer
  publishes self-contained terrain and live-pose Mario contexts before the
  first claim, lets master and slave steal eligible admit/lower work, waits
  for both terminal descriptors and positive slave callback retirement, then
  performs deterministic terrain/Mario assembly before the master alone
  lowers final VDP1 commands. Incomplete publication or execution preserves
  the prior complete command list without a serial full-frame replay. This is
  source-complete pending independent review; no new target build, CUE, Ymir,
  cache-behavior, or FPS evidence is claimed.

### Fixed

- Hardened A5.9 sourceboot queue capture against false-positive evidence. A
  terminal sequence reused across two presentation edges now fails the capture
  instead of being silently omitted; ELF identity bytes must come from an
  allocated `SHT_PROGBITS` section contained in `PT_LOAD`; and JSON-RPC
  notifications are independently count- and byte-bounded in reports. These
  changes preserve the fail-closed observation contract without touching the
  target or scheduler.

- Tightened that A5.9 ELF identity proof to require the section's virtual and
  file offsets to share the same affine mapping inside the selected `PT_LOAD`.
  Separate range containment could otherwise hash bytes at one file offset
  while probing a different loaded address; malformed inputs now fail closed.

- Added a separate bounded A5.9 sourceboot-load window before queue telemetry
  observation. After BIOS handoff the collector advances exactly one VBlank
  per identity retry and records its wait/attempt count, so a real CUE whose
  disc payload has not yet loaded does not consume the cadence budget or get
  mistaken for a wrong ELF. A never-matching image remains a failed
  target-identity report and no telemetry is read first.

- Restored the desktop launcher to the proven `ymir-agent/build-agent`
  executable and explicit `--profile`/`--disc` arguments. The prior default
  had drifted to `build-agent2`, which could launch without the intended disc
  or 32-Mbit RAM profile and made test sessions unreliable.

- Fixed A5.9 retirement telemetry publication so the slave writes its retired
  generation/sequence before releasing the positive retirement marker. The
  earlier order allowed the master to leave its wait and snapshot stale zero
  telemetry even though the callback had returned; a source-order mutation
  test now pins the SH-2 and host paths to release-marker-last ordering.

- Moved the two master-only terrain merge streams from HWRAM into the existing
  LWRAM work arena. Activating the reviewed four-phase queue made its callback
  graph reachable and exposed a 10,032-byte HWRAM link overflow; retaining
  these 27,744 bytes in scarce HWRAM provided no cross-CPU or VDP ownership
  benefit. Final sorting and VDP1 lowering remain master-owned.

- Fixed two target-blocking terrain handoff defects found in the independent
  A5.8 cutover review. The single WORLD_ADMIT producer no longer inherits the
  legacy two-lane rendezvous, and WORLD_LOWER rebuilds its local owner map from
  the exact DONE admit claimant before choosing cached versus P2 position
  payloads. The sole admit producer transforms the complete visible set
  directly, so a slave claimant never rereads its freshly cached owner bytes
  through the obsolete producer-0 P2 alias. An executable two-generation
  callback fixture poisons prior owner state and proves both slave-to-master
  and master-to-slave handoffs.

- Fixed the A5.8 render-job queue's SH-2 include boundary. The first guarded
  serial sourceboot target build exposed that `CPU_CACHE_THROUGH` was used
  without importing Yaul's cache definition; the queue now includes the
  narrow target cache header while host builds remain independent of Yaul.
  This restores target compilation without activating the dormant CPU-DUAL
  queue or changing the accepted renderer path.

### Added

- Added bounded A5.9 dual-SH-2 scheduling telemetry after the atomic queue
  produced no visible FPS uplift. The VDP2 HUD and append-only profile now
  expose master/slave claims for each world/actor admit/lower phase, exact
  notified/retired generation, master retirement-wait iterations, failures,
  and quarantines. A delayed-slave host schedule proves the current coarse
  graph permits the master to consume all four jobs before the slave runs;
  this is diagnostic evidence only and does not change scheduling policy.

- Added the dormant A5.8 ordered terrain-command and callback-context
  contracts. Final terrain sorting now retains each descriptor-local command
  image alongside its result without growing the eight-byte SH-2 reference;
  pointer-free P2 release records bind terrain and Mario callback snapshots to
  exact generation, phase, byte bound, producer lane, and claimant identity.
  Every phase-specific API rejects corrupt, stale, incomplete, wrong-phase,
  wrong-claim, out-of-range, and cross-lane host cases. Queue snapshots are
  self-contained: Mario copies dynamic compact
  refs inline and terrain copies the transform job/work order rather than
  following cached nested or stack pointers. This does not activate CPU-DUAL
  or change the accepted live renderer.

- Added the isolated standalone Saturn PCM68K audibility candidate. The
  source-built 68K now programs four bounded SCSP PCM8 slots using attributed
  PoneSound register/pitch patterns; a deterministic 4,408-byte CC0 proof bank
  and Yaul soundtest exercise SNDOFF/copy/SNDON, heartbeat validation, bounded
  enqueue, controller-triggered play/stop/volume, and visible telemetry. This
  remains outside sourceboot and does not change renderer scheduling or the
  accepted FPS comparison image. The owner manually confirmed audible A/B/C
  playback and X stop behavior in desktop Ymir; automated telemetry/cost
  evidence and sourceboot promotion remain separate open gates.

- Added payload-kind-aware output-span validation to the dormant A5.8 render
  queue. WORLD_ADMIT positions, WORLD_LOWER records/commands, ACTOR_ADMIT
  projected vertices, and ACTOR_LOWER primitive references may reuse their
  own bounded bank-local offsets, while overlap within one physical payload
  kind and unknown or mismatched type/callback pairs fail before publication.
  The descriptor remains pointer-free and 16 bytes; this removes a combined
  graph activation blocker without activating CPU-DUAL or changing the live
  renderer.

- Added the dormant Mario half of the A5.8 descriptor-owned render queue.
  ACTOR_ADMIT now has a claimant-selected transform payload and ACTOR_LOWER
  requires its exact completed predecessor before classification; terminal
  metadata and ordered master assembly validate the complete pose/ref payload
  before restoring the Castle-proven animation emission banks. This remains
  source-only: no CPU-DUAL callback or default renderer path changed, and
  combined activation still requires output-namespace review, ordered terrain
  command lookup, callback-context publication, and target/cache evidence.

- Added an isolated bounded SH-2-to-68K PCM command path. The pointer-free,
  big-endian ring rejects corrupt indices, invalid commands, and full queues
  without spinning; the 68K consumes at most eight commands per heartbeat,
  maintains four deterministic round-robin voice states, and publishes command
  telemetry. Three generated-proof sample records are fixed and bounded, but
  no PCM bytes or SCSP register writes exist yet, so this is not an audibility
  claim and does not alter sourceboot or renderer scheduling.

- Added the isolated, source-built fixed-address MC68000 heartbeat image for the
  Saturn audio prototype. Its byte-addressed mailbox now publishes protocol
  identity, BOOTING/READY state, and a wrapping heartbeat; host tests execute
  those transitions and an ELF/map verifier rejects bad entry/reset vectors,
  nonzero images, reserved-stack, 16 KiB, mailbox/bank overlap, and unresolved
  symbols. The minimal vector/linker
  shape is an attributed MIT close-port from pinned PoneSound. No SCSP slot,
  sourceboot, renderer, target build, or Ymir image changes in this increment;
  the approved exact-path `m68k-elf` GCC 11.1.0 bundle now produces a verified
  deterministic 1,190-byte BIN. Its stripped target headers are replaced only
  inside the freestanding audio68K build by GCC target-width typedefs; generated
  ELF/BIN/MAP files and tool binaries remain uncommitted.

- Added a dormant descriptor-indexed terrain merge assembler. It accepts only
  completed WORLD_LOWER outputs whose P2 metadata, claimant lane, generation,
  count, and sequence agree with the exact descriptor; it validates every
  result identity before the master performs its existing stable depth order.
  Queue streams are not coerced back into the legacy master/slave arenas, so
  later work stealing cannot silently select a fixed range. The live renderer
  remains legacy until Mario reaches the same contract, leaving the 3–4 FPS
  rollback candidate unchanged. A generation-current descriptor accessor now
  makes that assembler fail closed if any published WORLD_LOWER is READY or
  claimed rather than silently omitting it. The executable graph contract also
  preserves the valid all-culled case: once every lower job is DONE, zero
  result records form a successful empty stable merge.

- Added a source-provenanced animated-actor generalization spike selecting
  Goomba as the first non-Mario proof. It records the real BOB instance budget,
  source model/geo/animation/behavior inputs, required billboard/alpha/shadow
  treatment, and the descriptor-owned queue contract so future enemy support
  can reuse the Mario actor path without silently inventing a BOB-only or
  fixed-split renderer. A host-only inventory fixture keeps those assumptions
  explicit; this does not activate enemy rendering or change the 3–4 FPS
  rollback baseline.

- Hardened dormant A5.8 terrain lowering with a callback-side graph proof.
  WORLD_LOWER now requires exactly one immutable, completed WORLD_ADMIT
  predecessor and validates that predecessor's P2 publication before it reads
  transformed positions. This closes the malformed/unready dependency gap
  found in review without activating the queue or changing target behavior.

- Added the next dormant A5.8 terrain queue foundation: WORLD_ADMIT now
  publishes transformed-position completion through a descriptor-indexed,
  P2-visible release record, while WORLD_LOWER records its exact result count,
  sequence, claimant state, and writer lane before runtime may mark it DONE.
  Terminal merge now derives those values from the completed descriptor rather
  than accepting a caller-supplied count. The legacy worker remains the active
  renderer, so this intentionally changes no target behavior or FPS result.

- Added the A5.8 dormant terrain queue producer/reader seam. A WORLD_LOWER
  callback now passes its exact descriptor span and actual claimant lane into
  the common transform/classify/compact producer, seals its descriptor-owned
  result arena before graph runtime may publish `DONE`, and exposes a terminal
  reader that derives record and command aliases from that exact job identity.
  The fixed-split legacy wrapper remains the default path because Mario and
  persistent per-job merge counts are not yet migrated; this intentionally
  changes no target behavior or FPS result.

- Added a maintained roadmap and reconciled state/plan status with the
  reviewed A5 ownership bridge and graph-aware runtime. The next accepted
  milestone is now explicitly the atomic terrain/Mario renderer conversion;
  the 3–4 FPS A3+A4 Ymir build remains its rollback baseline until a reviewed
  target replacement exists.

- Clarified the full-game renderer roadmap: the legacy Castle demo already
  proved source-driven full Mario animation, so the Saturn path preserves and
  optimizes that bridge rather than rebuilding animation. Future enemies use
  the same animated-actor renderer contract, with additional per-family asset
  and feature coverage. The plan now distinguishes its committed coarse
  BSP/frustum/portal-window admission from a future, evidence-driven general
  occlusion/PVS extension.

- Added the first A5.8 terrain descriptor-binding seam and an executable C
  live-cutover contract. A future WORLD callback now proves its exact claimed
  queue descriptor before deriving terrain record/command addresses from the
  recorded output span and claimant lane; it cannot choose storage from a
  range begin or fixed split. The default renderer still uses the accepted
  legacy worker until terrain and Mario callbacks can be converted together,
  so this changes no target behavior or FPS result. The old Python-only
  source check is replaced by a host-compiled test because the configured
  Windows Python launcher is unavailable in this workspace.

- Added A5.8.1 graph-aware render-job runtime drains. The sole eventual
  CPU-DUAL owner now has an activation path that claims only graph-eligible
  descriptors, so a published consumer cannot run ahead of its required
  producer merely because it appears earlier in queue storage. The focused
  host contract deliberately publishes `WORLD_LOWER` before its `WORLD_ADMIT`
  producer and proves the producer still runs first. Renderer payload routing
  and live activation remain pending; no target/FPS claim changes.

- Added the A5.7 render-job graph foundation: P2-visible dependency masks stop
  consumers claiming before every producer is `DONE`, failed producers
  quarantine only ready dependents, and terrain multi-result consumers use
  exact `(job_index, output_index)` identities. This supplies the missing
  phase boundary discovered in A5.6 preflight without activating a second
  CPU-DUAL callback or changing the accepted A3+A4 renderer path.

- Added descriptor-indexed physical payload-bank helpers for A5.6. A claimed
  job selects master or slave storage from its recorded queue claimant and
  exact output span, while a reader refuses non-terminal/mismatched metadata
  and uses the bridge's P2 policy. This is the payload migration primitive for
  terrain and actor banks; the live fixed-split renderer is not yet switched.

- Added the source-only A5.6 render-job runtime lifecycle. It holds one
  queue/callback-table/context owner and, on SH-2 only, installs the sole
  polling CPU-DUAL entry; publication remains separate and host coverage
  proves the slave drains an exact claimed descriptor to terminal state. The
  live renderer is intentionally not bound yet because its fixed physical
  payload partitions still need descriptor-owned migration, preserving the
  accepted A3/A4 candidate as rollback baseline.

- Added the A5.5 descriptor-to-result bridge for the future opportunistic
  renderer. It routes result reads and writes using the exact queue descriptor
  index and actual master/slave claim, and rejects readers before that job is
  `DONE`, so a work-stealing master cannot accidentally select the slave cache
  alias. The queue now exposes source-side arming only—not a polling callback
  attachment or notification—and cannot activate a slave until the atomic
  renderer cutover replaces the legacy worker.

- Added descriptor-owned terrain and actor output-bank publication for the A5
  SH-2 work queue. The CPU that actually claims a descriptor now publishes its
  output lane through an atomic P2-visible release record, so an opportunistic
  master steal cannot read its own cached work through the slave alias. This is
  a source-only prerequisite: the accepted renderer still uses its existing
  fixed worker while live queue integration, master VDP1 ordering validation,
  and target evidence remain pending.

- Extended the source-only A5 queue contract with master/slave polling drains
  and a local callback table. Descriptors still carry only callback
  IDs; resolution happens after an exact-once claim and exposes the claiming
  CPU to the callback, which is required before a future work-stealing render
  path can publish cache-correct output ownership. The host fixture now proves
  the slave drains all callback IDs without per-job function pointers; target
  scheduling and FPS behavior remain unchanged until the existing fixed-lane
  renderer is converted to descriptor-owned output banks.

- Added the source-only A5 immutable render-job queue contract: fixed-width,
  pointer-free terrain/actor descriptor records publish through cache-through
  release words; master and slave claims are exact-once and terminal work alone
  can retire a generation. This establishes an auditable queue boundary before
  the active A3/A4 renderer candidate is rewired, avoiding a scheduler change
  that would obscure its pending review and target evidence.

- Added generated, bounded Mario actor meshlets (at most 32 primitives each)
  with material/opacity partitions, source ordinals, tight bounds, and compact
  near/mid/far primitive and position remaps. The serial master path now
  rejects behind meshlets before actor transform dispatch, transforms each
  admitted position once, preserves opaque source order, and
  puts textured/translucent work into stable fixed far-to-near depth bins;
  this replaces the quadratic actor insertion sort without moving camera,
  material, Gouraud, texture-slot, terrain-relative insertion, or VDP1
  ownership away from the master. Target visual/counter evidence and
  independent reviews remain required.

- Added an isolated, pointer-free SH-2/68K PCM wire-protocol foundation for
  the future standalone soundtest. The fixed big-endian mailbox/ring map and
  host contract prevent separately built CPUs from exchanging compiler-layout
  dependent structures, while leaving sourceboot, SCSP, the render queue, and
  the accepted FPS candidate unchanged.

### Fixed

- Corrected the dormant A5.8 terrain queue route so classification receives
  the actual claimant/execution lane explicitly. A legal slave claim whose
  descriptor begins at input offset zero can no longer be mistaken for master
  work by a `begin == 0` rule; that legacy-only inference remains confined to
  the fixed worker adapter. This is source-only and does not activate the
  queue or alter target behavior.

- Hardened A5.8.1 graph-runtime descriptor access after review. A claimed job
  is now fetched through a P2/cache-through accessor that revalidates the
  exact claimant state; the runtime no longer raw-dereferences its cached
  queue owner after a graph claim. A static mutation guard protects this
  boundary. This remains source-only and does not activate the renderer.

- Hardened the A5.7 job graph after review: independent READY work is no
  longer mistaken for a blocked dependent, cyclic/self dependency masks fail
  before queue publication, and failed-producer quarantine reaches every
  reverse-chain ready dependent before a generation can merge or reset.

- Corrected the A5.6 queue runtime to read its shared generation through the
  queue's SH-2 cache-through accessor. The slave poll can no longer observe a
  stale P1 queue header before deciding whether to drain work; a source gate
  rejects direct runtime generation dereferences while preserving host tests.

- Removed A5.5's premature Yaul CPU-DUAL callback registration and notify.
  The source-only ownership bridge now reserves and validates its one-owner
  lifecycle without compiling a second callback beside the legacy fixed-split
  worker. The later atomic renderer cutover must remove that worker before it
  binds the queue to CPU-DUAL.

- Renamed A5.5's misleading slave attach/notify API to explicit source arming.
  This prevents callers from treating the unbound bridge as an active worker;
  the static source gate now scans both queue and bridge sources for CPU-DUAL
  activation.

- Hardened A5 output-bank publication to bind each published cache lane to the
  actual queue release record, rather than trusting a callback-supplied claim
  value. Forged publication before a queue claim now fails closed, while an
  actual master or slave claim remains the sole source of output ownership;
  this preserves P2 peer reads before live queue wiring reaches the renderer.

- Corrected Mario meshlet admission to project each meshlet's live animation
  pose after Mario yaw, rather than using its neutral-pose AABB centre. Whole
  meshlets now cull only when their furthest live extent is behind the view
  plane, choose LOD from their nearest live extent, and bin translucent work by
  its furthest live extent. The generated compact position streams now provide
  the globally deduplicated transform references directly, so telemetry matches
  actual transform work and walking/animated poses cannot be rejected from
  stale neutral bounds.

- Added the first A3 scene-neutral render-cluster contract and a focused host
  gate. It chooses a hysteretic near/mid/far compact position span from a
  cluster AABB before transforms, rejects empty/behind optional spans, and
  carries only generated offsets and snapshot generation. The BOB generator
  now also emits deterministic compact unique position-reference streams for
  each LOD tier, so the remaining fragment-bank integration can replace its
  full-position marking without changing ownership or presentation logic.

- Extended A3's generated compact position streams to the active BSP-fragment
  bank and the Mario actor bank. The terrain renderer now admits its selected
  build tier before transform, filters the matching far primitives while
  retaining the mandatory route prefix, and marks only that immutable tier
  span instead of expanding every accepted primitive's four corners. This
  preserves the existing post-transform projected/near tests while exposing
  cluster and position admission/transform counters; target performance and
  visual evidence are still outstanding.

- Tightened Mario's generated far-tier policy to retain one deterministic
  source-primitive residue, rather than merely omitting one. The checked-in
  neutral pose now carries 228 far references versus 424 near/mid references,
  so the actor stream is a real compact future-workload input rather than a
  nominal tier with the same unique positions.

- Wired Mario's selected compact tier references into the actor transform
  dispatch. Worker ranges now index the immutable reference span and retain an
  explicit original-vertex ownership map for cache-through reads, so FAR
  transforms 228 selected vertices without classifying untransformed vertices
  as valid. Added deterministic BOB and BSP-fragment cluster metadata plus
  host gates for tight bounds, single material identity, mandatory retention,
  in-range/unique tier references, and a strictly smaller FAR stream.

- Routed accepted terrain work through the scene-neutral render-cluster
  contract before transforms. Generated BOB and fragment banks now provide
  Q16 tight bounds and exact per-cluster tier spans; the runtime derives a
  camera view, maintains per-cluster hysteresis (reset at scene changes),
  admits only validated clusters, and marks exactly their returned references.
  This replaces the former global tier span while retaining the existing
  post-transform projected, near, material, and capacity tests.

- Added Saturn-only immutable two-slot render snapshot banks with fixed-width
  camera, scene, Mario, pose-selector, and generated-bank-ID records. The
  master publishes a generation only after camera and actor records agree;
  illegal lifecycle transitions, stale acquires, double acquires, and timed-out
  quarantined slots fail closed so later pre-transform admission cannot mix
  live game state or reuse an unsafe bank.

### Fixed

- Moved A3's bulk per-cluster LOD and admitted-result scratch from HWRAM BSS
  into the linker-owned CPU-only LWRAM section. The initial target A3 link
  exceeded HWRAM by 29,680 bytes; the route-0 sourceboot candidate now keeps
  its required 4 KiB libyaul heap floor while retaining the same admission
  behavior. This is a build-budget repair, not a measured FPS claim.

- Corrected A3 render-cluster header integration for sourceboot's declared
  include paths. The scene-neutral gfx header now reaches its isolated GPL
  promotion dependency relatively, and its host gate no longer supplies a
  hidden `gpl` include directory. This prevents target compilation from
  depending on a host-only include-path accident; target/Ymir evidence remains
  pending.

- Corrected A3 admission/transform generation agreement at 32-bit wrap. The
  renderer now derives one nonzero frame generation before either consumer,
  reuses it for admission and publication, and fails closed on a mismatched
  admission result. Focused host coverage includes the exact
  `UINT32_MAX -> 1` transition and hysteresis reset; target visual/counter
  evidence remains pending.

- Corrected A3 generic compact-cluster admission to derive conservative depth
  from the immutable Q16 camera-forward vector instead of world Z. This keeps
  yawed and pitched optional terrain from being rejected or assigned the wrong
  LOD tier, while preserving mandatory work and the exact selected compact
  position span before transform. Focused host fixtures now cover both rotated
  views; target visual and counter evidence remain pending.

- Corrected the Saturn terrain runtime-contract fixture to distinguish optional
  worker-owned post-light RGB1555 shades from immutable VDP1 material words.
  It now exercises both no-shades and live-shades publication through sorted
  master/slave spans. The compact writer now drops supplied shade values unless
  `POST_LIGHT_SHADES` is set, keeping ignored bytes zero while preserving the
  live four-word shade payload; this prevents material-like values leaking
  across the master/worker boundary under a clear flag.

- Serialized every A2 snapshot terminal transition through its release claim
  lock. A timeout quarantine can no longer be overwritten by a stale
  `READY → RENDERING` claimant; completion and positive retirement likewise
  revalidate their owned state before publishing, preserving fail-closed bank
  ownership when the two SH-2s contend.

- Hardened snapshot-bank recovery so public initialization touches only
  already-free slots: quarantined and in-flight generations remain terminal or
  owned until explicit lifecycle retirement. Release state and peer payload are
  now accessed through SH-2 P2 cache-through helpers (host identity aliases
  preserve fixture coverage), and callers can use one exported generation
  validator instead of duplicating mixed-frame checks.

- Made `READY → RENDERING` exclusive across both SH-2s. An uncached release
  lock now uses the SH-2 `tas.b` bus-atomic transition (with a host atomic
  equivalent), so only one renderer can claim a snapshot generation; the
  losing contender must observe no acquired payload.

- Corrected snapshot publication so producers clear, initialize, and return
  the bulk payload through its SH-2 P2 cache-through address before releasing
  `READY`. This prevents a clean release word from racing ahead of dirty P1
  cache lines; host identity aliases cover lifecycle behavior, while target
  cache visibility remains a separate evidence gate.

- Added an opt-in desktop Ymir launch helper that records the exact SDL3
  command, working directory, project profile, staged CUE, and CUE-referenced
  ISO identities before manual testing. It keeps the 32-Mbit DRAM cart
  profile-managed and writes durable stdout/stderr logs beside its timestamped
  JSON report. This prevents the short-lived launcher from closing the GUI's
  pipe handles after monitoring, while preserving later crash diagnostics
  without silently launching a different emulator mode or disc.

- The bounded sourceboot boot-trace reader now verifies a caller-selectable
  linked text probe (default `main`) at every BIOS/checkpoint and final
  sample through both P1 and P2, alongside raw master-SH-2 register evidence.
  It records the ELF-derived expected bytes and SHA-256 plus observed PC/SP,
  so a bad trace word cannot be interpreted before the loaded code identity is
  established.
- The sourceboot boot-trace reader now samples every checkpoint and final
  record through both the resolved P1 address and its SH-2 P2 cache-through
  alias, retaining each address, raw byte vector, and decoded words
  independently. This separates a cache/alias disagreement from a real target
  memory overwrite without changing the bounded capture cadence or legacy P1
  fields.
- The sourceboot boot-trace reader now accepts an optional positive
  `--post-bios-checkpoint-interval`. When set, it samples the raw trace after
  every bounded post-BIOS execution chunk, including the final remainder, so
  a single headless capture can locate the first sentinel mutation without
  changing the default one-sample capture behavior.
- The sourceboot boot-trace reader now records raw 32-byte samples at
  protocol-ready and each existing BIOS-handoff boundary, with accumulated
  emulated frames plus any stopped SH-2 PCs. This makes the first loss of the
  trace sentinel observable, instead of attributing an end-of-run bad word to
  the entire boot sequence.
- The sourceboot post-BIOS trace reader now binds each diagnostic launch to
  the CUE, its referenced ISO, and its CUE-local ELF, recording SHA-256,
  size, and timestamp identities for all three. It rejects stale ISO/ELF
  pairs and generic same-byte CUE wrappers before Ymir starts, so symbols from
  an unrelated ELF cannot be attributed to the loaded disc.
- Sourceboot's boot trace now seeds its magic and version in ELF `.data`, so
  a bounded debugger read can distinguish a wrong RAM address or mapping
  (all zeroes) from execution that never reached `user_init()` (valid header,
  stage 0) without changing runtime cache-through publication.
- Sourceboot's cache-through boot trace now publishes at `user_init()` entry
  and after VBlank callback registration, making an all-zero post-BIOS record
  distinguish a pre-main handoff failure from later scheduler or VDP work.
- Sourceboot debug builds now publish a low-cost, symbol-resolvable RAM trace
  across bootstrap, scheduler, VDP1, and VDP2 boundaries. The bounded headless
  Ymir reader reports its last stage and raw words after BIOS handoff, so the
  repeated post-BIOS freeze can be diagnosed without a GUI launch or a timing
  measurement.
- Default-off `diag-skip-geo` measures a risky duplicate geo-walk upper bound;
  it requires demo/replay and is not a full-game mode or promotion path.
- Declared Saturn-only build support so obsolete PC/N64 guidance cannot imply a
  supported configuration.

### Fixed

- Repaired sourceboot's fragment-mode compatibility defaults. Two deferred
  `?=` aliases could recurse when neither spelling was supplied, preventing
  route-0 Make parsing before compilation. The canonical fragment value is now
  resolved eagerly only when it is absent, while the legacy spelling remains a
  compatible default and existing invalid/mismatched-value checks remain in
  force. An isolated parser matrix covers default, both one-sided aliases,
  matching/mismatched inputs, and mutation back to the recursive form. This
  fixes a host build invocation boundary only; a linked map, target, Ymir/manual,
  and FPS evidence remain separate.

- Corrected Task 14 actor snapshot publication so capture writes the bulk
  payload through the existing SH-2 cache-through bank alias before the
  unchanged publish transition. Previously capture addressed the cached bank
  parameter while publish/acquire used the uncached alias, allowing READY to
  become observable before dirty payload bytes reached shared memory. The
  188-byte ABI, two-bank lifecycle, observer capacity/generation validation,
  quarantine behavior, and fixed actor-arena accounting are unchanged. The
  new exact acquire fixture and cached-destination mutation gate are host-only
  evidence; target cache-race, sourceboot, Ymir/manual, and FPS evidence remain
  open.

- Failed post-BIOS trace captures now still write a bounded JSON evidence
  report containing any raw `mem.peek` bytes/words, Ymir protocol
  notifications, and capped stderr before returning failure. This preserves
  the observable target state when a bad trace header or missing byte payload
  would previously discard the only crash-boundary evidence.
- The post-BIOS trace reader now resolves the target ELF through the audited
  DLL-safe MSYS wrapper and accepts the SH-ELF leading-underscore symbol ABI,
  preventing an otherwise valid trace capture from stopping before emulation.
- Post-BIOS trace writes now use the SH-2 cache-through alias and pin their
  eight-word ABI at 32 bytes, so Ymir and hardware debuggers read current
  backing-WRAM telemetry instead of dirty cached data.
- The post-BIOS trace reader now rejects zero frame requests before issuing a
  Ymir `exec.run_for` RPC, preventing an invalid diagnostic capture request.
- Restored the sourceboot startup VDP2 begin/commit retirement barrier before
  frontend and scheduler initialization. This drains the sky-DMA work queued
  by `user_init()` before the first paired VDP1/VDP2 presentation, avoiding a
  new post-BIOS hang path while retaining the one-VBlank presentation cadence.
- Fenced sourceboot presentation to one observed VBlank generation: elapsed
  credit is sampled only at outer-loop entry, recovery is capped at one extra
  simulation tick, and excess eligible credit is counted and dropped. This
  prevents slow rendering from refilling catch-up work and submitting multiple
  VDP1 plots for one displayed VDP2 field; VDP1 and geometry-free VDP2 now
  commit together at one terminal boundary.
- The `diag-skip-geo` host policy gate now executes the real Make validation
  matrix and inspects the actual normal compile-time branch, preventing inert
  comments or the wrong source region from satisfying diagnostic containment.
- `diag-skip-geo` now rejects observable whitespace-padded and malformed flag
  values before any prerequisite, compiler-flag, or output-tag decision, closing
  a spelling that could activate the unsafe diagnostic without demo/replay.
