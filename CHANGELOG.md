# Changelog

## [Unreleased]

### Changed

- Added the scene-neutral admission boundary for validated render packages.
  Generic cluster/node/portal views now reject malformed metadata before
  traversal, perform conservative Z-Treme-derived frustum tests before
  transform/classify/lower, preserve mandatory clusters, and emit ordered
  bounded references with cycle/capacity telemetry. BOB adapts through a
  deterministic package-view helper while the full runtime remains
  scene-independent; target/Ymir activation and FPS evidence remain open.
  The serial DLL-preflight gate also uses Python subprocess launches for the
  host executables, avoiding the inherited MSYS quoted-path EOF failure.

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
