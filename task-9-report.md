# Task 9 — bounded Mario dual-SH-2 worker

## Scope and ownership

- Worktree: `sm64-port/.worktrees/sh2-native-math-purge`
- Starting HEAD: `5a5e850b97f4e27ec58ebe75e683136bbe5faa2a`
- The terrain range job returns before `demo_dispatch_mario_transform()` can
  dispatch the actor range job. `sm64_saturn_dual_worker_is_idle()` asserts
  that the one polling worker is not live; it is an ownership condition, not a
  timing/performance gate.
- The actor context copies the transform job, Mario snapshot, all 424 pose
  vertices, light intensities, and pose metadata. Its only pointers designate
  immutable generated mesh/material tables. It contains no game/graph state,
  VDP1 state, texture residency, or allocator pointer.
- Master and slave first write disjoint vertex-result spans `[0, 212)` and
  `[212, 424)`, then—after that job retires—classify disjoint primitive ranges
  `[0, 322)` and `[322, 644)` into fixed four-byte compact actor references.
  Each phase publishes its count through its own uncached frame-bank sequence;
  master reads peer results through `sm64_saturn_dual_frame_read_range()` /
  the cache-through alias.
- A timeout latches cancellation but does not return or clear `active` until
  the slave has positively written `done`. A serial fallback therefore begins
  only after the prior slave owns no result span. The timeout counter remains
  diagnostic; it does not select a performance policy.
- If a target callback violates the required bounded cancellation polling
  contract, positive retirement intentionally waits without a second arbitrary
  deadline. This trades liveness for the non-negotiable safety property that a
  serial fallback never races a live slave write; all Task 9 callbacks poll
  cancellation every 16 bounded items.
- Gouraud reservation, texture binding/slots, VDP1 command allocation, and
  final Mario actor insertion remain master-only.

## Provenance

Reuse mode is a narrowly scoped **GPL-3.0-or-later close-port extension** of
the project’s existing bounded worker boundary, not a world-renderer copy.

- Upstream: `Lobotomy-Software/SlaveDriver-Engine`
- Pinned commit: `a8986591557b6e680550d3c23970284d3b38ff8f`
- License: GPL-3.0-or-later
- Reference records/files inspected: `docs/saturn/UPSTREAM_CODE_LEDGER.md`
  records `WALLS.C:1240-1408,1803-1950`; `docs/saturn/PROVENANCE.md` Task 5b
  entry; existing project close-port
  `src/port/saturn/gpl/slavedriver_dual_worker.{c,h}` and
  `slavedriver_terrain_worker.{c,h}`.
- Adapted pattern: bounded master/slave range hand-offs, disjoint output
  ownership, positive cancellation retirement, then master joins. Yaul
  polling integration, actor snapshot/pose layout, frame-bank publication,
  and all renderer code are project-specific adaptations. No upstream polygon,
  portal, sector, or VDP command source was copied.

## Test-first record and verification

1. Added `tools/saturn/dual_actor_worker_test.c` before its idle/serialization
   API existed. Native GCC failed as expected with an implicit declaration of
   `sm64_saturn_dual_worker_is_idle`.
2. The cancellation regression test was written against the former serial
   host adapter and failed as expected (`host worker split completion contract
   failed`). The real host adapter now launches a Windows worker thread. An
   explicit entered-worker event plus a fixture-only forced-timeout hook makes
   cancellation deterministic; the test proves positive retirement/no write
   can occur after fallback begins without relying on wall-clock scheduling.
3. The fixture uses every generated Mario vertex and all 644 primitives; it
   compares serial/split vertex coordinates and primitive order, colors, and
   corner coordinates byte-for-byte. It also source-checks the worker context
   to reject live Mario/game/graph/VDP pointers. It executes the same pure
   split-owner helper used by `demo_actor_ref_read()` after inducing an
   all-master primitive fallback, requiring every reference to remain cached
   owner 0. An always-peer mutation is compiled and must fail the fixture.
4. `verify_dual_cpu_coherency.py --self-test` passed its source gate and all
   five mutation cases. `git diff --check` passed.

`verify-dual-actor-worker` was added to `Makefile.saturn.mk`; it runs the
native fixture including the real delayed-callback retirement case. I did not invoke Make, MSYS, bash, sh-elf tools, a
target build, or Ymir under the explicit safety restriction. Therefore target
compilation/disassembly and hardware/emulator verification remain pending.
The target enables `SM64_SATURN_DUAL_WORKER_TEST_HOOK` for both host fixture
builds only; it is excluded from `__sh__` builds and cannot affect the Saturn
binary.
The exact Windows-native command
`mingw32-make -f Makefile.saturn.mk verify-dual-actor-worker` passes. The
host compiler environment is empty on Windows (rather than using POSIX
`env -u`), and the expected-failure mutation is evaluated by the shell-free
`tools/saturn/expect_failure.py` runner rather than a POSIX `if` recipe.
