# Task 11 — VDP2 frame transaction

## Result and ownership

- Added `saturn_vdp2_frame.{h,c}`: a host-testable transaction which consumes
  only a copied camera yaw/pitch snapshot, source-tick number, and profile
  counters. It owns sky scroll calculation, compact HUD text preparation,
  NBG1/NBG3 display mask, and a single backend VBlank commit.
- Sourceboot adapts that transaction to Yaul. The one adapter commit calls
  `vdp2_sync()`; Yaul commits the prepared shadow state at VBlank-IN. The
  per-present path invokes the transaction once, after VDP1 sync, instead of
  independently scrolling sky and separately arming VDP2 sync.
- NBG1 remains priority 0 (opaque sky behind VDP1); NBG3 remains priority 7
  (dbgio HUD); every VDP1 sprite priority is 7. The active/display mask is
  exactly NBG1 | NBG3.
- The frame API has no terrain, actor, VDP1 command, texture, VRAM, or live
  game-state input. VDP1 remains the sole terrain/Mario geometry renderer.
- HUD writes are dirty only at initial presentation, source-tick rewind, or
  every 30 source ticks. Each presented frame still commits sky, layers, and
  the existing async dbgio state once. HUD text is integer-only and reports
  `FPS`, master/slave transform-result counters, final order count, DMA wait,
  and VDP1 wait; it contains no float formatting or policy gate.

## Review correction — telemetry semantics

- `FPS` is now a genuine presentation/source-cadence measurement: the frame
  transaction counts presented frames and divides by elapsed 30 Hz source
  ticks at each HUD update. It does not use render-construction time.
- `MT` and `ST` accumulate the actual master/slave classify-transform owner
  lanes before compact-result joining; `ORD` reads the final emitted command
  count. `DMAW` reads `dma_wait_ticks_last`, sampled from actual time
  spinning at an outstanding DMA queue fence; it no longer displays the
  cumulative late-bank diagnostic. `VDP1W` reads `vdp1_wait_ticks_last`,
  measured immediately around `vdp1_sync_render(); vdp1_sync();`.
- The corresponding last/accumulated fields are an append-only renderer
  profile suffix, dynamically discovered by `fast3d_profile_decode.py`. They
  are diagnostics only and never select scheduling or promotion policy.

## Test-first record and verification

1. Before the module existed, `runtime_contract_test.c` included
   `saturn_vdp2_frame.h`; a direct native GCC compile failed as expected with
   `fatal error: saturn_vdp2_frame.h: No such file or directory`.
2. Added the desired host contract to `runtime_contract_test.c`, then added
   the focused native fixture `vdp2_frame_contract_test.c` so the new
   transaction can be checked independently of the large existing runtime
   binary.
3. The focused native Windows Qt MinGW GCC build and executable pass:

   ```powershell
   gcc -std=c11 -Wall -Wextra -Werror -Iinclude -Isrc -Isrc/port/saturn/gfx \
     tools/saturn/vdp2_frame_contract_test.c \
     src/port/saturn/gfx/saturn_vdp2_frame.c \
     -o build/saturn/host-tests/vdp2-frame-contract-task11.exe
   build/saturn/host-tests/vdp2-frame-contract-task11.exe
   ```

   It proves NBG1+NBG3, visible VDP1 priority 7, copied-snapshot scroll of
   `(128,128)` at yaw `0x4000`, one VBlank commit per frame, integer HUD
   output, no HUD rewrite at tick 15, and three presented frames over 30
   source ticks reporting exactly `FPS 3` at the next HUD update.
4. Static ownership checks pass: sourceboot contains exactly one direct
   `vdp2_sync()` call (the frame adapter); the frame module contains no VDP2
   terrain/Mario/polygon interface and no `printf`/`sprintf`/`snprintf` or
   float conversion. `git diff --check` passes.

The complete pre-existing `runtime_contract_test` native build was started
with its full source closure but made no progress/output within the bounded
host window and was terminated rather than left running. Its normal Make
recipe also uses POSIX `env -u`, which the Windows Qt MinGW make environment
cannot parse. The focused test is therefore the authoritative Task 11 host
execution for this change; target compilation and Ymir are intentionally
deferred to Task 12.

No MSYS, bash, `sh-elf-*`, target build, or Ymir command was used.

## Reference and reuse record

- Plan/spec reviewed: Task 11 and the ownership contract in
  `docs/superpowers/plans/2026-08-02-saturn-dual-sh2-vdp-pipeline-sprint.md`.
- Upstream dependency inspected: `yaul-org/libyaul`
  `6012f79f237773378c8014e70d8998ad95a38d98`, MIT —
  `libyaul/scu/bus/b/vdp/vdp2.h`, `vdp_sync.c` (`vdp2_sync`),
  `vdp2_scrn_scroll.c`, and `vdp2_sprite.c`.
- Reuse mode: dependency/API use only. The VDP2 frame transaction, integer
  formatter, callback interface, and sourceboot composition policy are
  original project code; no Yaul renderer implementation was copied. Existing
  project provenance already records the pinned Yaul dependency and license.
- The DMA timing accessor extends the existing isolated GPL-3.0-or-later
  SlaveDriver queue close-port (`a8986591557b6e680550d3c23970284d3b38ff8f`,
  `DMA.C`/`DMA.H`, recorded in `UPSTREAM_CODE_LEDGER.md`). It is an original
  diagnostic measurement around the project’s existing bounded wait loop; no
  additional upstream implementation was copied and the existing GPL notice
  remains in the queue files.
