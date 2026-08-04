# A5.9 claim telemetry review — 2026-08-04

## Scope and verdict

**Verdict: NO-GO.** Independently reviewed commit `9655a838` against
`4f0b997c` in the exact `sh2/native-math-purge` worktree. The per-phase claim
mapping, uncached ownership, bounded wait counter, append-only profile fields,
and bounds-safe HUD assembly are structurally sound, but the slave publishes
its retirement release marker before publishing the retirement telemetry that
the marker is intended to make safe to read. A target build or manual HUD
observation must not start until that ordering is repaired and re-reviewed.

## Findings

### Critical

None.

### Important

1. **The slave retirement release marker is stored before its telemetry
   payload, so the master may snapshot a false `QR` value.** In
   `render_job_slave_entry()`, `s_runtime.retired_sequence = notified` occurs
   before `telemetry_retire(generation, notified)`. The master spins only on
   `sm64_saturn_render_job_runtime_slave_retired()`, which observes that first
   store. It can therefore leave the loop, record its wait, and copy telemetry
   after the release marker becomes visible but before the slave executes the
   later retirement-telemetry stores. The compiler-only fences do not prevent
   this source-level interleaving. This violates the stated exact
   notified/retired-generation contract and can display `QN != QR` even though
   positive slave retirement was observed. Publish the telemetry payload
   first, fence, and publish `s_runtime.retired_sequence` last as the release
   marker; mirror the order in the host-only retirement path so the executable
   contract represents the target ordering. Add a fixture/source assertion
   that enforces payload-before-release ordering.

### Minor

None.

## Verified contract

- The entire runtime record, including telemetry, remains in the existing
  `__uncached` placement on SH-2. Master and slave claims use disjoint arrays,
  so no claim counter has two CPU writers. The master reads the snapshot only
  after the intended positive-retirement join.
- Callback IDs are contiguous in exact phase order (`WORLD_ADMIT`,
  `WORLD_LOWER`, `ACTOR_ADMIT`, `ACTOR_LOWER`), and queue publication already
  validates the matching job type/callback pair. The four telemetry slots
  therefore map to the advertised phases rather than to descriptor position.
- Notify resets the generation-local counters before `cpu_dual_slave_notify()`.
  Claims increment only after a successful graph claim and claimed-descriptor
  lookup. Callback failures are CPU-specific, and terminal quarantine is
  counted through the queue's P2-selecting state accessor after the join.
- The master retirement loop adds only a saturating local scalar increment;
  shared telemetry publication and the bounded eight-slot quarantine scan are
  outside the hot wait loop.
- The frontend profile additions are appended after the previous final field,
  preserving all prior offsets. HUD append helpers retain one byte for the NUL
  terminator and therefore cannot overrun the expanded 160-byte buffer. The
  runtime's semantic claim/failure/quarantine bounds keep the diagnostic useful
  for the intended four-job/eight-slot generation, though arbitrary maximum
  test values may be safely truncated.
- The delayed-slave host schedule honestly demonstrates the legal ordering
  `notify -> master drains four jobs -> delayed slave claims zero`. It does not
  model concurrent instruction-level publication: the host poll runs
  synchronously and completes both retirement stores before returning, which
  is why it cannot catch the important finding above.

## Evidence run

- `git diff --check 4f0b997c..9655a838`: PASS.
- Direct MinGW C11 `-std=c11 -Wall -Wextra -Werror` render-job runtime fixture:
  PASS (`render job runtime fixture: PASS`).
- Direct MinGW C11 `-std=c11 -Wall -Wextra -Werror` VDP2 frame fixture: PASS.
- Focused Python runtime-source, live-cutover-source, and profile-decode suites:
  PASS, 19 tests run, 1 skipped.
- No target build, CUE, Ymir run, cache observation, or FPS claim was performed
  or authorized by this review.

## Required repair and remaining target gates

1. Publish `telemetry.retired_generation` and
   `telemetry.retired_sequence` before the runtime `retired_sequence` release
   marker on both SH-2 and host paths, with the fence at the publication
   boundary; add a regression guard for that ordering.
2. Obtain a fresh focused source re-review.
3. Run one serialized target build and inspect link/section/CPU-DUAL ownership.
4. Boot the fresh CUE in desktop Ymir and read
   `QM/QS/QN/QR/QW/QF/QQ`; only that target observation may select the next
   scheduling repair or support a performance claim.
