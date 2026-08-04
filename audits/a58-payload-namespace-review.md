# A5.8 payload namespace independent review

Date: 2026-08-04  
Reviewed commit: `db28fd872dc21094f2be7615b2076d31911ef923`  
Verdict: **GO (source scope)**

## Findings

No critical, important, or minor source-scope defects found.

The queue derives one of four physical payload kinds from the exact immutable
type/callback pair. Unknown job types and mismatched callbacks fail descriptor
validation before publication. Output spans may reuse bank-local offsets only
when their derived payload kinds differ; overlap between spans of the same
kind remains rejected. Because all fields used in the addition are `uint16_t`,
the overlap endpoints are representable in `uint32_t` without wraparound.

The public descriptor contains no pointer and its existing 16-byte static
assert remains intact. The commit does not alter the graph, bridge, runtime,
payload-bank, renderer, or CPU-DUAL registration paths, so the existing
P2/cache and dependency behavior is unchanged. No target, CUE, Ymir, cache, or
FPS evidence is claimed.

## Verification

Using `C:\Qt\Tools\mingw1310_64\bin\gcc.exe` with
`-std=c11 -Wall -Wextra -Werror`, the following independently rebuilt fixtures
pass:

- `render_job_queue_test.c`
- `render_job_graph_test.c`
- `render_job_bridge_test.c`
- `render_job_runtime_test.c`
- `render_job_payload_bank_test.c`

`git diff db28fd87^ db28fd87 --check` passes. The Python source fixtures remain
uncredited because the installed WindowsApps Python launcher cannot execute in
this environment; this is consistent with the implementation evidence and is
not represented as a green gate.

## Remaining gates

This GO is limited to the payload-namespace source increment. Ordered terrain
command lookup, explicit P2/cache-through callback-context publication,
direct callback corruption and cross-lane tests, the atomic CPU-DUAL cutover,
target/cache validation, and manual Ymir evidence remain open.
