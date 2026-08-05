# Task 9A Fix Round 4 — HWRAM Boot Repair Report

Date: 2026-08-05

Base: `8e13fd01200bfce1c7dad0f386e2cf8fe664e249`

Implementation/docs commit: pending closeout commit

Status: **source-repaired and focused GREEN; independent specification and
code-quality review required.** This report does not claim a repaired target
build, boot, P2/map placement, capture, FPS, manual Ymir acceptance, broad
verify, or native-math closure.

## Trigger and exact failed evidence

Fix Round 3 made the throughput observer understand the reviewed 104-byte
runtime without rebuilding the target. Retries against the unchanged exact
ELF/CUE failed target identity at both bounds:

- `docs/saturn/evidence/reports/a9a-step11-overlap-throughput-2026-08-05.json`:
  failed at `target-identity` after 600 startup VBlanks.
- `docs/saturn/evidence/reports/a9a-step11-overlap-throughput-startup4096-2026-08-05.json`:
  failed at `target-identity` after 4,096 startup VBlanks.

Both bind ELF SHA-256
`5afbc7527bf470e9c9b099d5874f13030f4a4406dc93d9a751b057584c3065f0`,
ISO `1d5f55f2...ab5411`, and CUE `cdbf0bfa...f46dba7`. Both logs authenticate
the disc and load `A.BIN`, then produce no target identity. They provide no FPS
or runtime-phase result.

## Root cause

Read-only inspection of the exact failed ELF gives:

- `___bss_end=0x060FD7D0`;
- `.uncached=0x260FD7D0+0x6900`;
- `___end=0x061040D0`;
- physical HWRAM overflow: `0x40D0` bytes.

Fix Round 2 had moved the bulk primitive tier and cluster LOD arrays into P2
`.uncached`. The linker evaluated `HWRAM_TOP - ___end >= 0x1B00` without first
proving `___end <= HWRAM_TOP`; unsigned wrap accepted the invalid image. The
Python verifier did reject a negative final margin, but did not explicitly pin
the `.uncached` physical end to `___end` or enforce the route-0 LWRAM floor.

This exact overflow is the best root-cause explanation for the identity
failures, but remains an inference until a reviewed repaired image boots.

## Reference-code-first record

Project research, plan, architecture, and prior-art records were inspected
before production work. The repair reuses existing project patterns:

- `saturn_dual_frame_cache_through()` for SH P2 cache-through conversion;
- canonical-P2 producer/consumer ownership from render snapshots;
- linker-owned `.lwram_bss` for CPU-only bulk state;
- the existing `saturn_lod_lifetime` exact-generation/deferred-scene laws.

Pinned upstream records remain SlaveDriver
`a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later), Z-Treme
`cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0), Yaul
`6012f79f237773378c8014e70d8998ad95a38d98` (MIT), Jo Engine
`556d081146211b6a1cfa6591d70f9487d406758b` (MIT/BSD-style file notices), and
sm64-psx `3073845688ea273da78d539b20c45110d8a868c3` (no repository-wide
license). Reuse remains dependency/API or pattern-only. No upstream source was
copied or closely ported.

## Watched RED

Before production edits:

1. `.venv-saturn-tools\Scripts\python.exe tools\saturn\test_a9_overlap_target_coherency.py`
   ran 5 tests: 3 failed, 2 passed. Missing behavior was combined LWRAM LOD
   ownership, one shared P2 accessor with no cached-P1 access, and ordered WRAM
   upper-bound linker assertions.
2. `.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sourceboot_memory_map.py`
   ran 13 tests: 3 failed, 10 passed. It accepted route-0 LWRAM with only
   `0x3000` free, did not report the exact HWRAM overflow before margin logic,
   and accepted `.uncached` ending somewhere other than `___end`.

## Implementation

- `saturn_demo_render.c` combines `primitive_tiers` and `cluster_lod` in one
  `demo_lod_storage_t` placed in `.lwram_bss`. The sole
  `demo_lod_storage_cache_through()` accessor supplies the canonical P2 alias
  used for master admission, lifetime setup, and either CPU's WORLD_LOWER
  claim. The small lifetime publication record remains `.uncached`; no purge
  protocol or ordinary cached P1 access was introduced.
- `sourceboot-cart.x` asserts each physical WRAM upper bound before subtracting
  its margin. The final `0x4000` LWRAM floor is unconditional, including route
  0 where no SCC1 capture exists.
- `verify_sourceboot_memory_map.py` rejects HWRAM overflow first, requires P2
  NOBITS `.uncached` to end physically at `___end`, validates `.lwram_bss`, and
  reports/enforces the final LWRAM end and margin.
- Source/layout/unit and real production-linked integration tests pin storage
  ownership and retain exact-generation, deferred-scene, quarantine, and
  mutation coverage.

## GREEN evidence actually run

- `test_a9_overlap_target_coherency.py`: 5/5 PASS.
- `test_verify_sourceboot_memory_map.py`: 13/13 PASS.
- `mingw32-make -f Makefile.saturn.mk verify-render-overlap-integration`:
  production integration PASS; six mutations rejected (active reset, omitted
  start, late notify, late retire, ignored generation, skipped quarantine
  refresh).
- `test_render_cluster_generation.py
  RenderClusterGenerationTest.test_renderer_keeps_bulk_cluster_state_out_of_hwram_bss`:
  PASS.
- Read-only `inspect_elf()` plus `validate_layout()` against exact failed ELF:
  prints `0x61040d0 0x260fd7d0 0x6900`, then rejects it with
  `ValueError: ELF end is past HWRAM top`.

The complete seven-test cluster-generation file was also run. Six tests pass;
one unrelated pre-existing test expects absent
`sm64_mario_render_cluster_lod_vertex_offsets` reference-stream source. That
failure was not broadened into unrelated production/verifier changes.

No target build, Ymir, capture, broad verifier, or native-math command ran.

## Projected budget and risks

Removing the exact old-map bulk symbols projects `___end` near `0x060FDE98`,
about `0x2168` HWRAM free. That is only `0x668` above the `0x1B00` floor.
Projected LWRAM free is about `0x74E0`, above its `0x4000` floor. These are
planning estimates, not evidence; the reviewed rebuilt map is authoritative.

Remaining concerns/gates:

- independent specification review and code-quality review of the exact diff;
- one serialized DLL-safe target build only after GO;
- exact rebuilt `.uncached`, `___end`, `.lwram_bss`, and both margin evidence;
- repaired-image target identity/boot;
- bounded overlap/phase/FPS capture and remeasurement of canonical-P2 cluster
  admission cost;
- manual Ymir, broad verify, and native-math publication gates remain open.

Task 9A Step 11, target evidence, and Step 12 therefore remain unchecked.
