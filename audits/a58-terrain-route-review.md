# A5.8 terrain dormant-route review — NO-GO

Reviewed commit `8096c44c68d8e49e4aad52698a136db8432beef4` independently on
2026-08-04. Scope was the dormant `WORLD_LOWER` terrain producer/reader
foundation only, not target activation.

## Verdict

**NO-GO.** The output arena is bound to the accepted descriptor claim
correctly, and the route remains dormant, but the queue-reachable
classification path still derives its read lane from `begin == 0`. Therefore
a valid `WORLD_LOWER` descriptor claimed by the slave with `input_offset == 0`
would write a slave-owned output arena while classifying through the master
alias. That is exactly the logical-range-to-physical-lane coupling A5.8 is
meant to remove, and on SH-2 it can read peer-produced transform data through
the wrong cache alias.

## Findings

### P1 — queue-reachable classifier still chooses its lane from range offset

`demo_terrain_compact_exact()` correctly receives `lane` explicitly, but it
calls `demo_classify_range(context->classify, begin, end)`. The latter computes
`const uint8_t lane = begin == 0U ? 0U : 1U;` and uses that value for
`demo_position_valid_read`, `demo_view_read`, `demo_projected_read`, and the
per-lane counters. Consequently, a legal claim such as slave + descriptor
span `[0, n)` takes writer lane 1 from the claim yet reads lane 0 aliases in
classification. The current source fixture only rejects `s_slave_begin` in
the callback; it does not inspect the shared queue-reachable classifier, so
it passes despite this violation.

Required repair: make the classifier take an explicit physical lane (for
example `demo_classify_range_exact(context, begin, end, lane)`), forward the
claim-derived lane from `demo_terrain_compact_exact()`, and confine the
`begin == 0` compatibility derivation to the legacy wrapper only. Add a
negative source/host contract for a slave claim with `input_offset == 0`, or
at minimum reject range-derived lane selection anywhere reachable from the
dormant callback.

## Checks that passed

- The callback validates `WORLD_LOWER`, its callback id, the immutable input
  span, and calls `demo_terrain_queue_bind_output()` before writing.
- `demo_terrain_queue_bind_output()` revalidates the exact queue claim and
  gets a descriptor-owned output lane from the bridge. Output metadata and
  payload-bank reads use cache-through queue/output accessors.
- The terminal reader first requires the exact `DONE` descriptor and derives
  both record and command aliases by descriptor index. It does not use
  `s_slave_begin`.
- No callback table registration, queue publication, graph runtime
  activation, or `cpu_dual_slave_set` call was added to the renderer. The
  fixed worker remains live.
- The documents accurately retain outstanding WORLD_ADMIT position
  publication, persistent per-job result count/merge assembly, Mario route,
  activation, target build, and Ymir evidence. Their claim that the dormant
  queue route has no `begin == 0` rule must be corrected after the P1 repair.

## Evidence rerun

Direct Qt MinGW GCC 13.1 C11 builds with `-Wall -Wextra -Werror` passed:

- `render_job_terrain_route_source_test.c`
- `render_job_payload_bank_test.c`
- `render_job_graph_test.c`

Those are narrow host checks only. No configured Make/MSYS gate, Saturn build,
CUE, Ymir run, or FPS measurement was performed by this review.

