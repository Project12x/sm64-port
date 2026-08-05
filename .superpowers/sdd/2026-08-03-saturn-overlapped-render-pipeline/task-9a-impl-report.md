# Task 9A Steps 1--9 Implementation Report

Date: 2026-08-05

Base: `0350a473`

RED checkpoint: `ec81ddc6`

Implementation: `0f5ccd65`

Status: Steps 1--9 source-implemented and focused-host-green; Steps 10--12 not
started. This is not a review, target-build, runtime-capture, or FPS claim.

## Outcome

- Replaced the accepted monolithic demo-render call with exact-generation
  `start_frame(N)` and `poll_frame(N)` operations.
- Added scene-neutral `saturn_render_lifecycle.{h,c}` state. It permits one
  active nonzero generation, returns after immutable publication/notify,
  reports PENDING until positive slave retirement, and owns one terminal
  drain/finalize or quarantine transition.
- Kept renderer-specific snapshot, pose, backend, Gouraud, descriptor, and
  merge state in the demo-render transaction rather than generic scheduler
  state.
- Sourceboot retains the active render snapshot and explicit BUILDING VDP1
  bank across PENDING. COMPLETE alone marks READY and acknowledges render
  completion; FAILED quarantines and records a no-retry tombstone.
- Preserved A8 transfer/poll/publish ownership. No transfer, resident-list,
  VDP2, input, source-simulation, or live-game-state ownership moved.
- Proved the existing frame scheduler already expresses a long-pending `N`
  with one queued immutable `N+1`; no scheduler production changes were
  necessary.
- Extended the cadence trace to version 2 (76 bytes/19 words) and added
  separate slave-work overlap and master-finalization counters. Explicit
  version-1/60-byte decoding remains supported.

## Failure policy

Wrong-generation polls and concurrent starts fail without side effects. A
prepare or terminal failure invokes the one lifecycle quarantine path and
never replays terrain, actor, or full-frame work. Sourceboot additionally
quarantines its exact BUILDING bank and render snapshot and retains the prior
published frame. Late completion cannot reach A8 transfer or presentation.

## Prior art and reuse

Pinned sources were re-inspected before production work:

- SlaveDriver `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later:
  `WALLS.C:1240-1408,1803-1950`, `DMA.C`, `DMA.H`, `V_BLANK.C:94-145`.
- Sonic Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0:
  `ZT_RENDERING.c:406-505,718-786`, `ZT_FRUSTUM.c:126-161`,
  `ZT_LOADING.c:118-176,299-355`, `workarea.c:12-25`.
- Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, MIT: public DMA/VDP1
  APIs and `libmic3d/render.c`.
- Jo Engine `556d081146211b6a1cfa6591d70f9487d406758b`, MIT plus file-level
  BSD-style notices: `vdp1_command_pipeline.c` and `3d.c`.
- sm64-psx `3073845688ea273da78d539b20c45110d8a868c3`, no repository-wide
  license: source-loop/compact-render behavior study only.

Reuse mode is retained dependency/API use for Yaul and pattern-only,
project-owned lifecycle code for the other compatible references. No new
upstream source was copied or closely ported.

## Watched RED

- Integration contract: missing start/poll and pending retention.
- Cadence source contract: version 1/60 bytes and absent overlap counters.
- `verify-demo-render-overlap`: missing production lifecycle module.
- Capture decoder: absent v2/76-byte record and overlap-window phase.

The RED checkpoint is `ec81ddc6`; the implementation is `0f5ccd65`. Plain
`python` and `make` were unavailable;
reruns used the repository venv and native `mingw32-make`. No MSYS/SH command
was invoked.

## GREEN evidence

- `mingw32-make -f Makefile.saturn.mk verify-demo-render-overlap`: PASS;
  nominal plus finalize-before-retirement, double-lowering, and serial-replay
  mutations caught.
- `mingw32-make -f Makefile.saturn.mk verify-frame-pipeline`: PASS; nominal
  plus four-tick, repeated-credit, and incomplete-publication mutations caught.
- `test_a9_frame_pipeline_integration_contract.py`: 9/9 PASS.
- `test_a9_sourceboot_cadence_trace_contract.py`: 3/3 PASS.
- `verify-render-job-runtime`: PASS (C fixture and 5/5 Python).
- `verify-render-job-live-cutover`: PASS.
- `verify-vdp1-frame-bank`: PASS (C fixture and 2/2 Python).
- `verify-vdp1-transfer-pipeline`: PASS, including DMA queue, C transfer
  fixture, 3/3 transfer-source, and 7/7 A8 contracts.
- `test_capture_sourceboot_throughput.py`: 31/31 PASS.
- Additional migrated consumers: live-cutover Python 2/2 PASS,
  terrain-route PASS, and the scoped cluster-generation assertion PASS.

The optional full `verify-dual-actor-worker` executable compiles but stops at
its pre-existing `worker_context_has_no_live_game_pointers()` failure before
the migrated renderer source assertion. The full cluster suite also retains a
pre-existing stale assertion expecting generated Mario LOD identifiers in
`saturn_demo_render.c` although they are declared in
`saturn_mario_actor_mesh.h`. Neither is substituted for a required Task 9A
green gate.

## Remaining gates

- [ ] Step 10 specification review.
- [ ] Step 10 quality review.
- [ ] Step 11 process audit and one serialized target build.
- [ ] Step 11 exact-identity automatic cadence capture.
- [ ] Step 12 final reconciliation and rereview.
- [ ] Manual Ymir acceptance.
- [ ] Broad verify/native-math publication census.

No target build, Ymir process, broad make verification, or native-math census
was run during Steps 1--9.
