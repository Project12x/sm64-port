# Changelog

## [Unreleased]

### Added

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
