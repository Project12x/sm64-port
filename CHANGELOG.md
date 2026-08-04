# Changelog

## [Unreleased]

### Added

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

- Added Saturn-only immutable two-slot render snapshot banks with fixed-width
  camera, scene, Mario, pose-selector, and generated-bank-ID records. The
  master publishes a generation only after camera and actor records agree;
  illegal lifecycle transitions, stale acquires, double acquires, and timed-out
  quarantined slots fail closed so later pre-transform admission cannot mix
  live game state or reuse an unsafe bank.

### Fixed

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
