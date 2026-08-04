# Changelog

## [Unreleased]

### Added

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
