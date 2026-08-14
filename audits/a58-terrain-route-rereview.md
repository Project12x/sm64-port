# A5.8 terrain route re-review — 2026-08-04

Scope: `8096c44c` and `f4a39e16`, reviewed against their A5.8 ancestry. This
is a source-scope review only; it does not authorize CPU-DUAL activation, a
Saturn build, or an FPS claim.

## Verdict: GO (source scope)

The repaired dormant `WORLD_LOWER` route no longer derives a queue-reachable
cache/output lane from logical work bounds. `demo_terrain_queue_bind_output()`
first proves that the supplied descriptor is an in-queue, exact, P2-revalidated
claim, then the output bridge publishes and records the actual claim state.
`demo_terrain_compact_exact()` and `demo_classify_exact()` receive that lane
explicitly. This covers the important legal case where a slave claim has
`input_offset == 0`: it remains slave-owned rather than being treated as a
master range.

The sole remaining `begin == 0U` lane selection is in
`demo_terrain_compact_range()`, which is documented and structurally confined
to the current legacy fixed-split worker adapter. The queue callback does not
refer to `s_slave_begin`, and terminal reads first require the exact DONE
descriptor before payload-bank/owner-lane alias selection. Queue publication
still verifies the live claimed state under its claim lock; the callback cannot
fabricate a lane merely by passing an enum.

The callback remains marked unused and is absent from a live CPU-DUAL callback
registration. No graph generation is published by this increment, so the plan
and evidence correctly describe it as dormant/source-only.

## Evidence run

- `gcc -std=c11 -Wall -Wextra -Werror` on
  `render_job_terrain_route_source_test.c`: PASS.
- Direct C11/Werror payload-bank fixture: PASS.
- Direct C11/Werror graph fixture: PASS.
- `git diff --check 8ebfe10a^ f4a39e16` and `git show --check`: PASS.

The configured Make/MSYS wrapper was not used for this re-review because its
host execution environment remains unreliable; the direct Qt MinGW fixtures
are accurately limited to host-source evidence. No target compilation, CUE,
desktop Ymir run, target capture, or performance measurement was performed.

## Remaining gates

- Descriptor-indexed WORLD_ADMIT transformed-position publication.
- Persistent exact per-job terrain count/sequence and terminal merge assembly.
- Equivalent Mario producer/reader migration.
- Fresh integration review, then a single atomic replacement of the sole
  CPU-DUAL callback, target build, and manual desktop-Ymir validation.
