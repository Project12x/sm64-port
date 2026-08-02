# Post-manifest Wave 1 gate delta

## Scope and preserved evidence

This is a host-only diagnosis of the fresh gate failure after `9907ec8`. No
target build, cross tool, CUE/ISO generation, emulator, or tracked-code edit
was run. Evidence inspected:

- fresh linked camv3/stage8 ELF and its already-generated `.asm`/`.sym`
  (2026-08-02 13:19);
- `9907ec8` and its checked route-oracle additions;
- `audit-dispatcher-manifest-report.md`;
- the verifier, immutable v2 contract, and relevant C sources.

## 1425 versus the fixed 582 contract

`9907ec8` changes only the verifier/oracle/tests/report, not target program
source. Its 37 checked dispatcher/target pairs recursively add the five active
BOB GraphNode callbacks, five level callbacks (one shared), and 27 GeoLayout
command handlers to the audit closure. Those targets then add their own direct
descendants. The increase to 1425 is therefore a legitimate static-closure
expansion caused by making previously opaque dispatch edges visible. The 12
new unresolved transfers are the next frontier exposed by that expansion.

This does **not** justify repinning `EXPECTED_TOTAL 582`. The count is an exact
native-math contract, and the larger closure demonstrates that the current
route still exposes considerably more helper-shaped math than the contract
allows. It is also not a runtime weighting: one-time graph construction and
cold cutscene paths count statically, so 1425 must not be presented as an FPS
ratio or proof that every newly reached helper executes in the BOB replay.

## Classification of the 12 transfers

| Transfer | Classification | Evidence / required disposition |
|---|---|---|
| `_camera_course_processing +286` | Genuine dynamic dispatcher | Calls `sCameraTriggers[level][b].event(c)` for a matching-area trigger. Derive the BOB trigger-event set from `sCameraTriggers[LEVEL_BOB]`. |
| `_camera_course_processing +316` | Genuine dynamic dispatcher | Calls the same trigger event for the default-area case. It should share the same checked BOB event set; caller-granular Phase-A declarations will cover both sites. |
| `_init_graph_node_perspective +84` | Genuine dynamic dispatcher | Invokes the non-null `GraphNodeFunc nodeFunc` with `GEO_CONTEXT_CREATE`; derive perspective callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_switch_case +74` | Genuine dynamic dispatcher | Invokes its non-null `GraphNodeFunc nodeFunc`; derive switch-case callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_camera +90` | Genuine dynamic dispatcher | Invokes its non-null `GraphNodeFunc func`; derive camera-node callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_generated +60` | Genuine dynamic dispatcher | Invokes its non-null `GraphNodeFunc gfxFunc`; derive generated-node callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_background +70` | Genuine dynamic dispatcher | Invokes its non-null background `GraphNodeFunc`; derive background callbacks from reached BOB GeoLayouts. |
| `_init_graph_node_held_object +78` | Genuine dynamic dispatcher | Invokes its non-null held-object `GraphNodeFunc`; derive held-object callbacks from reached BOB GeoLayouts. |
| `_next_lakitu_state` (single reported site) | Static-recovery gap | The C function contains only named direct calls and compiler soft-float/integer helpers; it has no function-pointer call. Preserve the real instruction shape as a regression and recover the concrete target. |
| `_play_cutscene` (single reported site; preserved ASM `+186`) | Genuine dynamic dispatcher | The `CUTSCENE` macro invokes `cutscene[sCutsceneShot].shot(c)`. A manifest must be derived from the compiled cutscene tables/shot entries, with any route narrowing stated explicitly. |
| `_play_mode_change_level` (single reported site; preserved ASM `+14`) | Genuine dynamic dispatcher | Invokes non-null `sTransitionUpdate(&sTransitionTimer)`. Derive targets from all reachable assignments to `sTransitionUpdate`. |
| `_update_lakitu` (single reported site) | Static-recovery gap | The C function contains only named direct calls and compiler arithmetic helpers; it has no callback invocation. Preserve the real instruction shape and recover the concrete target. |

The six `init_graph_node_*` cases are not false positives merely because their
callbacks are stored in the node. Each initializer also immediately invokes a
non-null callback in `GEO_CONTEXT_CREATE`, so they require checked dispatch
manifests rather than parser suppression.

## Smallest fail-closed next step

1. Repair only the two static-recovery gaps (`next_lakitu_state` and
   `update_lakitu`) with exact linked-instruction regressions. Require a real
   direct-call fact and zero unresolved transfer at each site; do not add
   declarations for them.
2. In a separate task, extend the existing source derivation to the ten real
   dynamic sites: six initializer callback sets, the BOB camera-trigger event
   set, cutscene shot callbacks, and transition-update callbacks. Compare the
   complete derived mapping against both oracle edge sets, with omission,
   underived-addition, stale, and exact-site tests. State the existing
   dispatcher-granular imprecision.
3. Run one serial target audit after both host changes. Keep 582 fixed and keep
   every still-unlisted transfer visible. If the transfer gate clears while
   the helper total remains above 582, treat that remaining delta as the next
   native-math optimization inventory, not as permission to generate or test a
   CUE.

This ordering is intentionally fail-closed: static analyzer defects cannot be
hidden behind declarations, and genuine callback closure is admitted only
from checked source data.

## Static-recovery implementation

The two static sites are now represented by exact host regressions at their
linked caller offsets. `_next_lakitu_state +108` must emit the direct fact
`___subsf3`; `_update_lakitu +464` must emit the direct fact `___addsf3`.
Neither site may retain an unresolved transfer. A near-match in which the
`find_floor` output argument is an unknown frame-derived alias remains
unresolved and emits no direct fact.

Both positive fixtures failed before the analyzer change. The
`_next_lakitu_state` loop initially resolved `r2`, then collapsed its advancing
`r9`/`r14` frame pointers to an address-anywhere state on the backedge. Their
non-overlapping output stores consequently poisoned the earlier `r2` spill.
The `_update_lakitu` fixture treated the `find_floor` output pointer at the
exclusive end of the four-byte helper spill as overlapping that spill.

The analyzer now retains monotone frame-relative pointer ranges through joins
and immediate increments. Backedge widening makes an expanding endpoint
unbounded while preserving the opposite bound, so a forward-only store range
cannot overwrite an earlier spill, but a range that may overlap a slot still
poisons it. Exact escaped-pointer invalidation now uses the correct half-open
longword interval. `MaybeStackPtr` and other unknown frame aliases retain the
existing whole-frame fail-closed behavior.

Focused verification covered both exact sites, the unknown-alias control, the
adjacent-slot rule, and a truly overlapping escaped pointer:

```text
Ran 5 tests in 0.019s
OK
```

The complete host verifier suite then passed:

```text
Ran 172 tests in 0.275s
OK
```

An observation-only run against the preserved camv3 ELF was stopped by the
90-second bound after producing no stdout and no JSON result. It is recorded
as incomplete, not passed or failed. No target build, CUE/ISO generation, or
Ymir session was run. `EXPECTED_TOTAL 582` and both route-oracle manifest edge
sets remain unchanged.
