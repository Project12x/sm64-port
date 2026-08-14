# A5.6 runtime re-review — 2026-08-04

## Verdict

**GO for the A5.6 source-only foundations at `0aa0e122`, `225df00e`, and
`6eb532e2`.** This is not approval to bind the live renderer, build a Saturn
image, or claim a target/FPS result. The explicit live-cutover guard remains
red until the legacy producer/read paths are atomically replaced.

## Scope reviewed

- `0aa0e12244eb4df47eb4bf3d69068908d2f93b88` — queue runtime foundation.
- `225df00eafcf232dd869a26b1dcd6d0298aa3198` — descriptor-owned physical
  payload-bank helper.
- `6eb532e26c3d516cbc8154f6ad0cf7afcf4f7952` — P2/cache-through lifecycle
  read repair.
- Current queue/bridge/output-bank implementations, the legacy
  `slavedriver_dual_worker`, runtime/payload fixtures, active A5.6 plan,
  evidence report, and shared SDD progress ledger.

## Findings

### Cache-through runtime state: resolved

The prior P2 defect is fixed. The only runtime generation read now goes through
`sm64_saturn_render_job_queue_generation()`, which first converts the queue to
its `CPU_CACHE_THROUGH` alias. The subsequent slave drain independently
normalizes the same queue pointer before every queue/release state access.
`s_runtime` itself is `__uncached` on SH-2, so its active/queue/callback/context
reads do not reintroduce a cached runtime-control read. No direct
`s_runtime.queue->generation` dereference remains.

The payload helper also does not infer a physical lane from a legacy range: it
uses the exact DONE descriptor and output-bank claimant record, with queue and
metadata accesses normalized through their cache-through helpers. Its focused
fixture proves a master claim of an old slave-side input range writes and reads
the master physical payload only.

### CPU-DUAL ownership: correctly non-competing in accepted path

`saturn_render_job_runtime.c` contains one Yaul polling registration when and
only when its explicit activation API is called. The legacy generic worker still
contains its own registration, but the accepted renderer currently calls neither
the queue runtime nor its activation API; it therefore cannot produce two live
registrations. This is the correct source-only prerequisite state. The live
cutover guard is intentionally red because it confirms that the renderer has
not yet atomically removed the legacy fixed dispatches.

### Documentation and claim scope: adequate

The dirty shared SDD ledger records the original NO-GO, repair commit
`6eb532e2`, source gates, and the absence of renderer binding, target build,
Ymir, and FPS claims. The active plan/evidence report retain the payload/runtime
work as an active prerequisite and state that terrain/actor callers are not yet
migrated. No source document promotes the earlier 3–4 FPS A3/A4 manual result
to A5.6 evidence.

## Evidence run by this review

- Direct Qt MinGW `-std=c11 -Wall -Wextra -Werror` runtime fixture: **PASS**
  (`render job runtime fixture: PASS`).
- Direct Qt MinGW `-std=c11 -Wall -Wextra -Werror` payload-bank fixture:
  **PASS** (`render job payload bank fixture: PASS`).
- `git diff --check 0aa0e122^ 6eb532e2`: **PASS**.
- Manual source inspection verified the cache-through accessor and the single
  accepted-path activation condition above.

The Python source guards were feasible but could not be rerun in this review
environment: `python.exe` is an inaccessible WindowsApps alias and `py -3`
reports no installed Python. This is an environment limitation, not a green
gate. Their recorded prior status remains source-guard PASS for the runtime
repair and expected RED for the absent live cutover; no target evidence was
substituted.

## Remaining gates

1. Migrate terrain and actor arrays/readers to descriptor-indexed payload
   banks.
2. Atomically replace the accepted legacy CPU-DUAL callback with the queue
   runtime only after all old producer/reader dispatches are removed.
3. Rerun the Python source gates in the project toolchain, obtain the required
   integration reviews, then run the serialized Saturn build and desktop-Ymir
   evidence sequence.
