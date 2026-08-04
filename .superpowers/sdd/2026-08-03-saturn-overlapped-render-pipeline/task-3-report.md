# Task 3 / A3 — render-cluster host-contract report

## Status

**Active; accepted terrain compact-span substitution is source-complete for
the generated tier streams, but A3 as a whole remains open.** This report does
not claim an FPS or VDP1-work reduction. The live renderer now marks only the
selected generated fragment span before transform; target evidence remains
unrun.

## Delivered scope

- Added `src/port/saturn/gfx/saturn_render_cluster.h`, a scene-neutral C11
  value contract for tight bounds, primitive span, compact per-tier position
  spans, material partition, stable ordinal, and mandatory admission.
- Added `sm64_saturn_render_cluster_admit()`. It returns an exact chosen-tier
  span tagged by the immutable view generation, rejects malformed/empty
  optional work before transform, and uses existing hysteretic LOD thresholds.
- Extended BOB scene, BSP-fragment, and Mario actor emitters to deterministically
  emit compact near/mid/far position-reference streams. Far preserves the
  terrain mandatory route prefix. Mario's neutral pose has 424 near/mid and
  228 far references.
- Replaced `demo_build_visible_position_set()`'s accepted primitive-corner
  loop with the validated selected-tier reference span, and filtered optional
  far primitives before ownership/worker scheduling. Existing post-transform
  projected rejection remains unchanged.
- Appended counters for coarse clusters tested/admitted and positions
  admitted/transformed; direct compact-reference validation is host-tested.
- Review remediation: Mario's worker dispatch now uses its selected compact
  vertex-reference span and an original-vertex ownership map; BOB/fragment
  generated metadata is property-checked for tight bounds, material identity,
  mandatory FAR retention, sorted unique references, and FAR reduction.
- Generic-runtime remediation: BOB and fragment emitters now generate the
  scene-neutral cluster contract itself (Q16 bounds, material, ordinal,
  mandatory flag, exact tier spans). Terrain admission calls that contract
  before transforms and visible-position marking consumes only returned spans.
- Final-review critical remediation: the immutable render view carries a Q16
  camera-forward vector and generic admission conservatively projects the
  entire cluster AABB onto that view-space axis. Optional behind rejection and
  hysteretic LOD can no longer use world Z; terrain still marks only the exact
  compact reference span returned by the helper.
- Final-review generation remediation: the render frame derives one normalized
  nonzero transform generation before admission, uses it for the immutable
  view and later publication, and rejects any admitted result whose generation
  disagrees before marking its compact references.
- Added `tools/saturn/render_cluster_test.c`,
  `tools/saturn/test_render_cluster_generation.py`, and
  `verify-render-clusters`.

## Provenance

The policy is an original scene-neutral adaptation informed by Sonic Z-Treme
`cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0,
`Projects/SONIC Z-TREME/ZTE/ZT_RENDERING.c:439-480`; no upstream source was
copied. SlaveDriver `a8986591557b6e680550d3c23970284d3b38ff8f`,
GPL-3.0-or-later, `WALLS.C:1240-1408`, was re-inspected for the fixed-capacity
fail-before-write discipline. Reuse mode: pattern-only. Both references remain
recorded in `docs/saturn/UPSTREAM_CODE_LEDGER.md` and the active plan.

## TDD evidence

1. RED: direct C compilation failed with the expected missing
   `saturn_render_cluster.h` error.
2. GREEN: `verify-render-clusters` passed the C contract fixture. It covers
   inside/mid, far with fewer refs, mandatory behind-view admission,
   optional behind-view rejection, empty tier rejection, and null inputs.
3. RED: the generator test failed because generated compact LOD stream fields
   were absent.
4. GREEN: the deterministic generator test and C fixture passed through the
   explicit worktree-root/host-compiler Make command recorded in the evidence
   ledger.
5. RED: yaw/pitch fixture compilation failed before the render-view contract
   exposed `view_forward_q16`; the old world-Z helper could not satisfy those
   cases.
6. GREEN: `verify-render-clusters` passes front/behind non-axis-aligned yaw
   and pitch cases, mandatory behind work, exact spans, and yawed MID
   hysteresis through the immutable Q16 forward vector.
7. RED: the A3 source gate required a normalized render-frame generation, and
   the C fixture could not compile before the shared nonzero increment helper
   existed. GREEN: the focused gate proves `0 -> 1`, `UINT32_MAX - 1 ->
   UINT32_MAX`, and `UINT32_MAX -> 1`; it also proves the wrapped admission
   result is exactly `1` after reset drops a prior MID hysteresis tier to NEAR.

## Remaining gates

- Obtain independent specification and quality rereview, then target visual
  and counter evidence.

## Execution ledger (2026-08-04)

- RED recorded: fragment stream declaration was absent; actor synthetic stream
  metadata was absent; compact reference marking was absent; and the full
  Mario reproducibility fixture differed until the derived header was
  regenerated.
- GREEN: `verify-visible-position-set`, `verify-render-clusters`, the renderer
  compact-span source check, the actor synthetic/reproducibility checks, and
  the append-only profile decoder check passed using the explicit MSYS host
  compiler command. `git diff --check` passed.
- Not run by authorization: target build, CUE packaging, Ymir, and target
  visual/counter capture. Independent review remains unrequested.
- Source sub-slice commit: `feat(saturn): admit compact terrain position spans
  before transform`; overall A3 remains active.
- No target build or Ymir run is authorized or performed by this A3 slice.
- Final-review Critical source repair: `fix(saturn): use view depth for
  compact cluster admission` (this task commit). Focused host gate passed; the
  generation-wrap Minor, fresh independent rereview, and target evidence are
  explicitly unclosed.
- Final-review generation repair: derives `transform_generation` once with
  `sm64_saturn_render_generation_next()` before both admission and transform
  publication; results are generation-checked before compact spans are used.
  `verify-render-clusters` is green (six Python checks plus the C fixture) and
  `git diff --check` passed. No target build,
  CUE, Ymir, visual, or counter gate was run; fresh independent rereview and
  target evidence remain open. In-tree reference inspected:
  `saturn_fast3d_frontend.c`'s nonzero wrap discipline; reuse mode:
  pattern-only, no source copied.
