# A5.5 bridge re-review

**Commits reviewed:** `962dfde304f9d87c54edf9bf09c08430986747b1`, `54f53dfcd4b054d4140c4bb1415f444b0e3d30d3`  
**Verdict:** **NO-GO**

## Scope and method

Read the repair diff and traced the source-only queue/bridge, the compiled
legacy SlaveDriver owner, output-bank claim binding, and the focused fixtures.
No source or build configuration was edited.

## Blocking finding

### P1 — the lifecycle fixture still falsely reports a real CPU-DUAL attach/notify

`sm64_saturn_render_job_queue_slave_attach()` only writes the private
`s_slave_attachment` record and `sm64_saturn_render_job_queue_slave_notify()`
only returns `true` when that record is populated; neither function registers a
Yaul callback, wakes CPU-DUAL, or drains a descriptor
(`src/port/saturn/gfx/saturn_render_job_queue.c:87-113`).  Nevertheless, the
fixture calls those APIs and treats success as a persistent polling attachment
and an explicit slave notification (`tools/saturn/render_job_bridge_test.c:42-53`).
This makes a green host test look like target attachment proof, even though a
future renderer could call `notify()` and silently perform no work.

The repair documentation is clear that target registration must wait for the
atomic cutover, but the API names, fixture assertions, and plan Step 2 still
present an attach/notify lifecycle (`docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md:107-114, 909-918`).
That is an executable-contract mismatch, not merely wording.

**Required repair:** replace this source-only API with explicitly named
reservation/introspection semantics (for example `reserve_consumer` and
`consumer_reserved`) or remove it until the atomic cutover.  The fixture must
prove only duplicate-reservation rejection and must assert that no dispatch
occurred; it must not call a function named `notify` or claim a polling
attachment.  The plan/evidence should then distinguish source-only reservation
from target callback binding and notification.

## Verified repair properties

- The previous premature Yaul calls are absent from the source-only queue and
  bridge.  A repository source search finds `cpu_dual_slave_set` and
  `cpu_dual_slave_notify` only in the intentionally still-linked legacy
  `src/port/saturn/gpl/slavedriver_dual_worker.c` (and the separate
  `dualtransform` sample), not in `saturn_render_job_queue.c` or
  `saturn_render_job_bridge.c`.
- The legacy worker remains the only renderer-side CPU-DUAL owner:
  `slavedriver_dual_worker.c:89-90` registers `dual_slave_entry`, and line 126
  notifies it.  A5.5 no longer introduces a competing compiled owner.
- The descriptor-to-output bridge still selects bank kind from the immutable
  job type and requires `done_job()` before a consumer can read
  (`src/port/saturn/gfx/saturn_render_job_bridge.c:4-61`).
- Output publication still locks and revalidates the actual queue release,
  derives writer lane from its claimed state, and rejects preclaim/invalid
  descriptor state (`src/port/saturn/gfx/saturn_render_output_bank.c:85-130`).
  P2/cache-through selection remains descriptor-owner based; no fixed
  `begin == 0` split was reintroduced.

## Test evidence

- PASS: direct Qt MinGW host compile with `-std=c11 -Wall -Wextra -Werror` of
  `render_job_bridge_test.c` plus queue, output-bank, and bridge sources;
  executable output: `render job bridge fixture: PASS`.
- BLOCKED: `tools/saturn/test_render_job_bridge_source.py` could not be run in
  this environment because neither `python` nor the Windows `py -3` launcher
  has an installed interpreter.  Static inspection confirms its only check is
  that the two Yaul token strings are absent from **queue.c**.  It does not
  scan the bridge, does not prove no indirect registration route, and does not
  detect the misleading no-op `notify` contract.  It is therefore insufficient
  as the claimed complete source-only coexistence gate.
- Not run: Saturn target build, target callback ownership/retirement proof,
  cache-coherency, Ymir, and performance gates.  The documents correctly leave
  these target gates open.

## Recommendation

Repair the source-only API/test/document contract above, expand the source gate
to scan both queue and bridge (and reject registration/notify aliases or an
indirect registration hook), then repeat this independent review.  Keep the
actual Yaul registration and `cpu_dual_slave_notify()` exclusively in the later
atomic renderer cutover that removes all legacy dispatches.
