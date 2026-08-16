# Sprint 2 Task T2.9 — does `spatial_admit` need to cost as much as it does?

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `1ff45354`.
- Owner's question, verbatim: **"does spatial_admit need to cost as much as
  it does?"**
- Instrument extends T2.8's (`sprint2-t2_8-vdp1-fence-attribution.md`), record
  ABI v3 -> v4. Method follows T2.5
  (`sprint2-t2_5-prepare-mario-audit.md`): an FRT sub-probe paired with a code
  audit and an arithmetic sanity check.
- **This is a measurement + audit task. Nothing was optimised.**

## Headline

**No. It needs to cost roughly a sixth to an eighth of what it costs, and
the reason it does not is structural rather than arithmetic.**

- `demo_spatial_admit()` measures **16,588.1 FRT ticks = 2,123,278 SH-2
  cycles = 4.727 VBlank-equivalent = 77.77% of the pre-notification window**,
  reproducing T2.6's 16,020.6 / 4.5654 and T2.8's 16,024.5 once this task's
  own +3.52% instrument cost is subtracted.
- **`SM64_SATURN_BOB_ADMISSION_NODE_COUNT` is 1.** The production admission
  path's spatial index is a single node carrying all 867 cluster references.
  It is a flat list wearing a tree's type signature, and the 1,183-node BSP
  the brief's arithmetic assumed is **never touched on a production frame** —
  it belongs to the fail-closed fallback, which ran **0 times in 1,330
  frames** (`admit_fallback_frames = 0`).
- **867 clusters are frustum-tested every frame, in every sample, without
  exception** — `clusters_tested` reads 867 with min = max = 867 across all 77
  valid samples — while only **230-353 (mean 283, 32.6%)** are admitted. The
  frustum-test bucket measures **8,978-9,154 ticks regardless**, a ±1% band.
  That is the owner's clue confirmed at instrument resolution: **the cost does
  not fall when less is visible, because nothing in the traversal can skip.**
- The decomposition, on the unperturbed basis:

  | | share of `spatial_admit` | behaviour with visibility |
  | --- | ---: | --- |
  | `cluster_test` (867 flat frustum tests) | **54.6%** | **flat** |
  | `cluster_dedup` (a linear scan of the output list) | **27.7%** | **rises as K²** |
  | `validate` (revalidating `static const` metadata) | **10.6%** | **exactly constant — 1,700-1,701 ticks every frame** |
  | `mandatory` (a trailing 867-cluster sweep that admits nothing) | **6.6%** | flat |
  | everything else | 0.5% | — |

  **Nothing in this stage gets cheaper when less is visible. 71.8% of it is
  literally constant, and the remaining 27.7% gets more expensive the more you
  can see.**
- `output_has_cluster()` costs **39,903 comparisons per frame**, and the
  measured count equals the closed form `K(K-1)/2` for K = 283 **exactly**.
  The O(1) array that removes it — `s_admission_cluster_seen[4096]` — is
  **already in the scratch struct and already cleared every frame**.
- No soft-float, no `___divdi3`, no `___divsi3`, no `sinf`/`cosf` anywhere on
  this path, confirmed in the linked image. T2.5's Finding-A pathology does
  not apply. What *is* there is **four SH-2 hardware 64/32 divisions per AABB
  test — 3,472 per frame** — to compute a projected limit that a
  cross-multiply would give for two `dmuls.l`.

**Defensible cost: 0.5-0.8 VBlanks/frame against a measured 4.565**
(product basis). **Recoverable: ~3.8-4.0 VBlanks, ~24-26% of the whole
15.483-VBlank frame**, with no fidelity cost and no BOB bypass. Section 8
justifies the number; section 10 ranks the work.

---

## 1. The sub-stage enumeration, established before instrumenting

Reading `demo_spatial_admit()` (`src/port/saturn/gfx/saturn_demo_render.c:844`)
and everything it reaches produced this list **before** any probe was written.
The first thing the reading established is that the function's own body is a
thin adapter and that all of the work is in one callee,
`sm64_saturn_scene_admit_with_scratch()`
(`src/port/saturn/gfx/saturn_scene_admission.c:271`).

| # | Sub-stage | What runs | Where (HEAD line numbers) |
| --- | --- | --- | --- |
| 1 | `view_setup` | `sm64_saturn_render_view_t` and `sm64_saturn_scene_admission_view_t` assembly: camera position to Q16, forward/right/up basis copy, scene table pointers, frustum limits, output binding, scratch cast | `saturn_demo_render.c:849-895` |
| 2 | `validate` | entry guards **plus** `metadata_valid()` — a full revalidation of the package's static admission metadata: every cluster's bounds/reserved/LOD fields, every node's bounds, every node-to-cluster containment relation, every cluster ref's range, and a coverage sweep proving every cluster is referenced | `saturn_scene_admission.c:289-298` -> `:136-269` |
| 3 | `scratch_clear` | `memset` of `visited[2048]` and `queued[2048]` — 4,096 bytes, sized by `SM64_SATURN_SCENE_ADMISSION_MAX_NODES`, not by the scene | `saturn_scene_admission.c:299-300` |
| 4 | `frustum_derive` | `admission_frustum()`: camera position `floor_q16` per axis, forward/right/up selection, near/far/focal clamping, lateral-basis fallback | `saturn_scene_admission.c:301` -> `:54-107` |
| 5 | `node_test` | the BFS loop's per-node `test_bounds()` — `world_bounds()` de-Q16 of the node AABB followed by `sm64_saturn_ztreme_frustum_aabb()` | `saturn_scene_admission.c:304-317` -> `:109-116` |
| 6 | `cluster_test` | the per-cluster `test_bounds()` inside the admitted node's cluster-ref loop, plus the loop overhead of every iteration that rejects | `saturn_scene_admission.c:319-327` |
| 7 | `cluster_dedup` | `output_has_cluster()` — a linear scan of the output list already built, once per cluster that survived the frustum test | `saturn_scene_admission.c:333` -> `:118-125` |
| 8 | `cluster_emit` | output capacity check and the append to `s_render_work_order`, plus the mandatory bookkeeping | `saturn_scene_admission.c:337-346` |
| 9 | `portal` | the per-node portal-ref loop: portal open test, portal `test_bounds()`, `output_has_portal()`, neighbour enqueue | `saturn_scene_admission.c:348-387` |
| 10 | `mandatory` | the trailing sweep that admits any `mandatory` cluster the traversal missed | `saturn_scene_admission.c:391-403` |
| — | residue | the epilogue after the last probe | — |

Candidates the brief listed that **do not exist on this path**, recorded as
absent rather than reported as zero:

- **BSP descent.** The production path performs none. The 1,183-node BSP
  (`sm64_saturn_bob_bsp_bounds_min/_max`, `_planes`, `_octant_child_order`) is
  referenced only from `demo_spatial_admit_node()`
  (`saturn_demo_render.c:762-841`), the **fail-closed fallback** reached only
  if `sm64_saturn_scene_admit_with_scratch()` returns false. Confirmed by
  reference: those four arrays appear at `saturn_demo_render.c:777-778`,
  `:800-802`, `:810-818`, `:825-827` and nowhere else in `src/`. Confirmed on
  target: `admit_fallback_frames = 0` over 1,330 frames.
- **LOD tier selection.** Not in admission. `saturn_lod_select()` runs in
  `demo_prepare_render_work_order()` (`saturn_demo_render.c:935`), the separate
  `work_order` node.
- **Depth-bin / sort-key derivation.** Not in admission. Draw order is produced
  later by `sm64_saturn_terrain_depth_bins_*` and
  `sm64_saturn_vdp1_backend_link_depth_bins()` (`saturn_demo_render.c:3422`,
  `:3446`, `:4766`). **Admission output order is not draw order** — which
  materially lowers the risk of every remediation in section 10.
- **Position-set / dedup bookkeeping.** The visible-position bitset is
  `demo_build_visible_position_set()`, the separate `position_set` node.
- **Per-primitive admission.** Admission works in *clusters*. For BOB the
  cluster-to-primitive map is 1:1 (867 = 867), so "per cluster" and "per
  primitive" coincide numerically here, but the code has no per-primitive loop.

## 2. The scene, confirmed from the generated headers

Counted from `build/saturn/sourceboot/generated/bob_scene.h` and `bob_bsp.h`,
not from the brief:

| Constant | Value | Source |
| --- | ---: | --- |
| `SM64_SATURN_BOB_POSITION_COUNT` | 1,625 | `bob_scene.h:6` |
| `SM64_SATURN_BOB_PRIMITIVE_COUNT` | 867 | `bob_scene.h:7` |
| `SM64_SATURN_BOB_CLUSTER_COUNT` | 867 | `bob_scene.h:10` |
| `SM64_SATURN_BOB_BSP_NODE_COUNT` | 1,183 | `bob_bsp.h:5` |
| `SM64_SATURN_BOB_SCENE_NODE_SPAN_COUNT` | 1,183 | `bob_scene.h:13` |
| **`SM64_SATURN_BOB_ADMISSION_NODE_COUNT`** | **1** | **`bob_scene.h:15`** |
| **`SM64_SATURN_BOB_ADMISSION_CLUSTER_REF_COUNT`** | **867** | **`bob_scene.h:16`** |

The brief's ~1,183 nodes / ~867 primitives / ~1,625 positions is confirmed —
but **the denominator the brief's per-unit arithmetic used is the wrong one**,
and the correction makes the finding worse, not better.

The production admission path never touches a BSP node. Its spatial index is a
**single** node (`sm64_saturn_bob_scene_admission_nodes`, 36 bytes in the
linked image at `0x22761644` — one 34-byte record padded) carrying **all 867
cluster references** (`sm64_saturn_bob_scene_admission_cluster_refs`, `0x6c6` =
1,734 bytes = 867 × `uint16` at `0x22760f7c`).

The per-unit cost is therefore **2,050,406 / 867 = 2,365 cycles per cluster**
against the brief's ~100 cycles for a six-plane AABB frustum test — **~24x** —
on a structure that has no early-out available to it at any granularity.

## 3. What was added

### The instrument

`src/port/saturn/runtime/saturn_prenotify_profile.h`: record ABI 716 -> 860
bytes (179 -> 215 words), version 3 -> 4. **The node table is unchanged at 24
entries and ids 0-23 keep their T2.4/T2.5/T2.6/T2.7 meaning**, so every earlier
ranked table remains directly comparable and the `spatial_admit` node continues
to measure exactly what T2.6 and T2.8 measured.

The 36 new words are **flat sub-spans, not node-tree children**, and that
choice is the whole design:

- The cluster loop runs 867 times per frame. A `push`/`pop` pair charges
  through the `__uncached` working state and costs ~100 SH-2 cycles; three
  pairs per iteration would have added ~260k cycles to a ~2.0M-cycle stage —
  **13% perturbation at exactly the granularity where the answer lives**. That
  is T2.5's per-vertex mistake, and T2.5 avoided it by deriving rather than
  probing. Here the answer genuinely needs per-loop resolution, so the probe
  was made cheap instead of being removed.
- A flat span is two on-chip FRT byte reads plus a subtract and an add into a
  caller-held local. Accumulators and the cursor stay in locals for the life of
  the call and are published once, at the end, in ~35 uncached writes.
- One `uint16_t` cursor threads the entire call, so each span charges "time
  since the previous probe" to the bucket it names — the same discipline
  `charge()` uses for the node tree. **The named buckets therefore sum to the
  whole admission call by construction**; the only unattributed part is the
  epilogue after the last probe, and it measured **−0.0 ticks**.

Probe sites: `saturn_scene_admission.c` (nine spans) and `saturn_demo_render.c`
(one span for the caller-side view assembly, plus a fallback-frames counter).

### Counts, which are the half that answers the question

Timing alone cannot distinguish "each test is too expensive" from "we run far
too many tests". The record therefore publishes, per frame: `nodes_tested`,
`nodes_admitted`, `clusters_tested`, `clusters_admitted`,
`clusters_rejected_frustum`, `clusters_inside`, `clusters_intersect`,
`clusters_duplicate`, `portals_tested`, `output_count`, `dedup_calls` and
`dedup_compares`.

`dedup_compares` is measured with **zero inner-loop perturbation**. A counter
incremented inside `output_has_cluster()`'s body would have added ~15% to the
very loop under test. Instead a diagnostic-only twin, `output_cluster_scan()`,
returns the loop's own induction variable at exit; the loop header and body are
the same shape, and only the exit paths differ.

### Where the probes deliberately are NOT

- **Not inside `metadata_valid()`.** Its ~3,470 iterations get one bracket, not
  per-loop brackets. Its iteration counts are compile-time properties of the
  generated header (867 clusters, 1 node, 867 refs) and are stated in Finding C
  by derivation, exactly as T2.5 derived its per-vertex divisor.
- **Not inside `sm64_saturn_ztreme_frustum_aabb()`.** The four `scaled_limit`
  divisions are counted from the linked image (Finding E), not probed; a probe
  there would cost more than the thing measured.
- **Not on the fallback path.** `demo_spatial_admit_node()` gets a frame
  counter, not spans, because the point of instrumenting it at all is to prove
  on target that it never runs. It does not: **0 of 1,330**.

### Perturbation — predicted, then measured, and the prediction was wrong

Predicted before the build: ~2,610 spans/frame at ~14 cycles each ≈ 36,500
cycles ≈ 1.8% of the stage.

**Measured: 1,440 spans/frame and +563.6 ticks = +72,141 cycles = +3.52% of the
stage, i.e. ~50 cycles per span, not ~14.** The estimate was wrong by ~3.5x per
span. A `volatile` byte pair read from the on-chip FRT block is not a register
op, and at `-Os` the accumulators do not all stay in registers. Recorded as a
correction rather than quietly adjusted — T2.8 made the same class of error at
the frame level and said so.

Three things make the perturbation tractable rather than disqualifying:

1. **99% of the added cost lands where the probes are.** The window grew by
   568.6 ticks (20,761.7 -> 21,330.3) and `spatial_admit` grew by 563.6
   (16,024.5 -> 16,588.1).
2. **The unprobed controls reproduce.**

   | Node (no new probes) | T2.6 | T2.8 | **T2.9** | Δ vs T2.8 |
   | --- | ---: | ---: | ---: | ---: |
   | `work_order` | 1,943.6 | 1,944.3 | **1,946.6** | +0.12% |
   | `position_set` | 458.2 | 458.3 | **460.3** | +0.44% |
   | `meshlet_depth_admit` | 1,132.3 | 1,132.8 | **1,132.6** | −0.02% |
   | `prepare_mario` (self) | 5.7 | 5.6 | **5.6** | 0 |
   | `window_residue` | 30.2 | 30.0 | **30.0** | 0 |

3. **The per-span cost is uniform, so it subtracts cleanly.** Distributing
   563.6 ticks over the 1,440 spans (0.3914 ticks each) and subtracting per
   bucket reconstructs T2.8's unprobed `spatial_admit` to **16,018.8 against
   16,024.5 — −0.036%**. The de-perturbed column in section 7 is therefore a
   derivation with a measured residual, not a guess.

**The instrument's bias is against the dedup finding, not for it.**
`cluster_test` opens 867 spans and `cluster_dedup` opens 283, so `cluster_test`
absorbs 3x more probe overhead in absolute terms while being the bucket the
dedup finding competes with.

### Wrap headroom

Every span is one probe gap inside a stage whose entire measured total is
~16,600 ticks at φ/128, four times below the 65,535 ceiling. Witnesses:
`admit_max_raw = 1,702` (headroom **63,833**), `admit_total_ticks_max = 19,207`,
node-tree `max_raw_interval = 19,216` (headroom **46,319**). No interval
aliased, and `max_raw_interval` again equals `spatial_admit`'s own maximum to
within a tick, so the stage still sets the ceiling.

### Product cleanliness — proven at OBJECT level, with one fully characterised exception

Every addition is inside `#if SM64_SATURN_ADMIT_DIAG` (which is
`SATURN_DIAGNOSTIC_MODE != 0 && defined(__sh__)`), and the product arm of every
`#else` is the pre-T2.9 statement verbatim. Diff shape across source:
**+323 / −4** (`saturn_demo_render.c` +26/−0, `saturn_scene_admission.c`
+117/−1, `saturn_prenotify_profile.h` +180/−3).

Method: T2.8's, verbatim. Each translation unit the change can reach is
compiled at `SATURN_DIAGNOSTIC_MODE=0` with `-g0` on the product build's own
command line, twice — once from the working tree and once from a
**path-identical mirror of `HEAD`** extracted with `git show` and given the same
`-ffile-prefix-map` target — and the objects compared. The worktree was never
stashed or modified.

| Object | Bytes | Verdict |
| --- | ---: | --- |
| `saturn_scene_admission.o` | 4,048 | **IDENTICAL** — `c8528ea9bb8bb0062e1c467e7a4f9dfe2c4ebb0f0676a4dc5e22e42a37770817` |
| `main.o` | 660,904 | **IDENTICAL** — `c79c819e1201ac61e45e68d36b470420176760b2b49d7d91e0123576f4f4f179` |
| `saturn_actor_meshlets.o` | 16,500 | **IDENTICAL** — `f423caa89b4dbd14cd84eaa99e961e67c0870dea36df0bdff7bcfe38ada50cdd` |
| `saturn_render_job_runtime.o` | 5,356 | **IDENTICAL** — `49cad063edc38e336b7e6a99ec31d31db347c54f63ff7b606931eb23a7ff0be6` |
| `saturn_demo_render.o` | 723,168 | **5 bytes differ — characterised below** |

**The file this task edits most heavily, `saturn_scene_admission.c`, is
byte-identical.** So is `main.o`, which is the strongest single check on the ABI
change: the record grew 716 -> 860 bytes and the mode-0 object did not move by a
byte, because none of it is instantiated in a product build.
`saturn_actor_meshlets.o` reproduces T2.6's and T2.8's independently recorded
hash `f423caa8…50cdd` at 16,500 B exactly, confirming this command line is the
product build's rather than an approximation of it.

**The `saturn_demo_render.o` exception, characterised to the byte.** This is
T2.4's exception recurring for T2.4's reason and not T2.5's: T2.5 was byte-clean
because its edits sat *below* every `assert()` in the file; this task's edits sit
*above* two of them.

- **5 differing bytes out of 723,168** (0.0007%).
- **Disassembly identical.** `sh-elf-objdump -d` on both objects differs only in
  the filename banner line.
- **Section sizes identical**, every section.
- All 5 bytes are ASCII digits inside two `.rodata` assert-message strings:
  `required_positions <= 1625U` **`1020` -> `1046`**, and
  `context->required_positions == sm64_saturn_visible_position_set_count(...)`
  **`1794` -> `1820`**.
- **Both deltas are exactly +26**, exactly the number of lines this task adds to
  `saturn_demo_render.c`.

So the entire change to the product object is: two `assert()` failure messages
would report their own new line numbers. No instruction, no branch target, no
data layout, no symbol.

One methodological note, because the obvious cross-check misfires here:
recompiling both sides with `-DNDEBUG` does **not** remove these strings — the
HEAD object hashes identically with and without it (`a1b8d96a…dbb71` both
times), so this tree's `assert` is not the `NDEBUG`-gated C-library macro and
that experiment is inconclusive rather than contradictory. The byte census and
the identical disassembly are the evidence; the `-DNDEBUG` result is recorded so
the next reader does not repeat it.

## 4. The audit — why a flat 867-item frustum pass costs 4.565 VBlanks

### Finding A — the "hierarchy" is one node, so no early-out is reachable

`demo_spatial_admit()` publishes `scene.node_count =
SM64_SATURN_BOB_ADMISSION_NODE_COUNT` (`saturn_demo_render.c:875`) and
`scene.portal_ref_count = 0U` (`:878`), and never assigns `scene.portals` or
`scene.portal_count` — the view is zero-initialised at `:848`. The BFS in
`sm64_saturn_scene_admit_with_scratch()` therefore seeds the queue with the root
(`saturn_scene_admission.c:302-303`) and can never enqueue anything else,
because the only enqueue site is inside the portal loop (`:384-385`). **The
`while (queue_head < queue_tail)` loop runs exactly once**, and the whole
`visited`/`queued`/cycle-edge machinery — plus the 4,096-byte per-frame `memset`
that services it (`:299-300`) — exists to manage a one-element queue.

Measured, on target: `nodes_tested = 1`, `nodes_admitted = 1`,
`portals_tested = 0`, `node_test = 11.4 ticks (0.07%)`.

Two consequences:

1. The node-level frustum test is free and useless.
2. All 867 cluster tests are children of that one node, so **nothing is ever
   skipped**. The cost is set by total scene size, not visible scene size —
   exactly the symptom the brief identified.

**Not directly actionable on its own** (it is the enabling defect for Findings
B/C). **Confidence: certain** — it is a generated constant, confirmed by a
36-byte symbol in the linked image and by `nodes_tested = 1` on target.

### Finding B — the port already implements Z-Treme's short-circuit, in the path this one replaced

The pattern T2.0 recorded as the reference technique
(`sprint2-t2_0-reference-sweep.md` §4.3: "If the parent tested
`INSIDE_FRUSTUM`, children skip the test entirely; only `INTERSECTS_FRUSTUM`
re-tests", `ZT_RENDERING.c:425, 486-492`) **is already written in this
repository** — in `demo_spatial_admit_node()`, the legacy BOB painter that A9A
ran as production and that this build keeps only as a fallback:

```c
/* saturn_demo_render.c:772-785 */
sm64_saturn_ztreme_frustum_result_t state = inherited;
if (state != SM64_SATURN_ZTREME_FRUSTUM_INSIDE) {      /* <- short-circuit */
    const sm64_saturn_ztreme_frustum_result_t tested =
        sm64_saturn_ztreme_frustum_aabb(frustum,
            sm64_saturn_bob_bsp_bounds_min[node],
            sm64_saturn_bob_bsp_bounds_max[node]);
    state = tested;
}
profile->demo_bob_nodes_visited++;
if (state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE) {     /* <- subtree prune */
    profile->demo_bob_nodes_outside++;
    return state;
}
```

`state` is then passed down to both children (`:833`, `:840`), so an INSIDE node
costs its subtree nothing and an OUTSIDE node prunes its subtree entirely.

The generic path throws both away. It *computes* `node_state`
(`saturn_scene_admission.c:315-316`), uses it only to decide whether to process
the node at all (`:317`), and then **never passes it to the cluster tests**
(`:324-326`). Even in a scene with a real node tree, a cluster inside a
fully-INSIDE node would still be tested from scratch: the inheritance parameter
does not exist in this module's signature.

Measured relevance: 91 of 867 clusters classify INSIDE and 96 INTERSECTS, so on
this scene an inheriting hierarchy would have real work to skip — if there were
nodes to inherit from.

**Estimated saving: 30-60% of `cluster_test` on a real hierarchy; ~0 on BOB's
single node until Finding A is fixed too. Confidence: high on the mechanism, low
on the BOB-specific number.**

### Finding C — `metadata_valid()` revalidates baked constants every frame

`sm64_saturn_scene_admit_with_scratch()` calls `metadata_valid(scene, stats)`
unconditionally on entry (`saturn_scene_admission.c:298`). That function is a
package **integrity** check over data that is `static const` in a generated
header and cannot change between frames:

| Loop | Iterations for BOB | Per iteration | Source |
| --- | ---: | --- | --- |
| cluster field/bounds check | **867** | out-of-line `bounds_valid()` call (6 cart reads) + `primitive_count` + 3 `reserved` + `position_ref_count[NEAR]` + `mandatory` | `:158-169` |
| node bounds + **node-to-cluster containment** | 1 node × **867** refs | ref read + 3-axis min/max containment against the cluster's own bounds — 6 cart reads on each side | `:170-198` |
| cluster-ref range check | **867** | one cart read | `:199-203` |
| coverage `memset` + mark + verify | 867 + **867** + **867** | LWRAM scratch write/read plus a cart ref read | `:204-211` |
| portal well-formedness | 0 | (BOB publishes no portals) | `:212-243` |
| **portal endpoint symmetry — `O(portals × nodes)`** | 0 | a nested scan of every node for every portal endpoint | `:247-267` |

**~3,470 loop iterations and ~19,400 uncached cartridge reads per frame** to
re-prove a property of a `static const` array.

**The measurement is unusually clean on this one.** `validate` reads **1,700 or
1,701 ticks in every single sample across the whole route** — a two-tick band
over 1,330 frames spanning 28 distinct Mario positions. It is not merely
view-independent in principle; it is constant to one part in 1,700 in practice.
Nothing else in the port measures that flat.

The portal-symmetry loop at `:247-267` is zero-cost for BOB but is
`O(portals × nodes)` and is a scaling landmine for any scene that publishes
portals; recorded here so it is not rediscovered later as a regression.

**Measured cost: 1,700.1 ticks = 217,614 cycles = 10.61% of the stage = 0.485
VBlanks/frame. Confidence: high** — it is isolated by its own bracket and the
fix (validate once per scene bind) cannot change any admitted set.

### Finding D — `output_has_cluster()` is an O(K²) linear scan, and the O(1) array it needs is already allocated

```c
/* saturn_scene_admission.c:118-125 */
static bool output_has_cluster(const sm64_saturn_scene_admission_output_t *output,
                               uint16_t cluster)
{
    uint16_t index;
    for (index = 0U; index < output->cluster_count; index++)
        if (output->cluster_indices[index] == cluster) return true;
    return false;
}
```

Called once per cluster that survives the frustum test (`:333`), against an
`output->cluster_count` that grows by one on every admission (`:342`). For K
admitted clusters the scan costs **K(K−1)/2** `uint16` comparisons.

**Measured, and the model is exact:**

| Quantity | Measured | Closed form `K(K−1)/2`, K = 283 |
| --- | ---: | ---: |
| `dedup_calls` | 283 | — |
| `dedup_compares` | **39,903** | **39,903** |
| mean scan length | 141.0 | (283−1)/2 = 141 |
| `clusters_duplicate` | **0** | — |

Not one duplicate was found in 1,330 frames. For BOB it *cannot* find one:
`metadata_valid()` proves at `:204-211` that every cluster is referenced at
least once, and `cluster_ref_count == cluster_count == 867` forces a bijection.
The scan is 567,198 cycles per frame of provably fruitless search.

It also runs a second time. The trailing mandatory sweep (`:391-403`) calls
`output_has_cluster()` for each of the **96** mandatory clusters — all of which
the traversal already admitted at `:344-346`, because the reject condition at
`:328` requires `mandatory == 0`. **That sweep admits exactly zero clusters**
(187 non-OUTSIDE + 96 OUTSIDE-but-mandatory = 283 = `output_count`) and costs
**135,720 cycles = 6.62% of the stage.** Its per-cluster cost, 156.6 cycles
against a loop body that is one uncached `mandatory` byte read, is only
explicable as the 96 embedded linear scans.

**The array that makes this O(1) is already in the scratch struct and is already
cleared every frame.** `s_admission_cluster_seen[4096]`
(`saturn_scene_admission.h:96`) exists solely for `metadata_valid()`'s coverage
sweep (`:204-211`) and is dead for the rest of the call. Reusing it as the
admission-side membership bit costs zero additional memory and zero additional
clearing.

**Measured cost: `cluster_dedup` 4,431.2 ticks = 567,198 cycles = 27.66%, plus
~125,000 cycles of `mandatory` = 33.8% of the stage combined ≈ 1.55
VBlanks/frame. Confidence: high** — the quadratic model matches the measurement
exactly, and the replacement is order-preserving.

### Finding E — four hardware 64/32 divisions per AABB test

`sm64_saturn_ztreme_frustum_aabb()` derives the projected lateral limits by
dividing:

```c
/* ztreme_frustum.c:43-50, 86-93 */
static int32_t scaled_limit(int64_t depth, int32_t extent, int32_t focal_length)
{ ... sm64_saturn_div_s64_s32(depth * extent, focal_length, &result); ... }
...
const int32_t far_width   = scaled_limit(far_z,  frustum->half_width,  focal);
const int32_t far_height  = scaled_limit(far_z,  frustum->half_height, focal);
const int32_t near_width  = scaled_limit(near_z, frustum->half_width,  focal);
const int32_t near_height = scaled_limit(near_z, frustum->half_height, focal);
```

Confirmed in the linked image, not inferred from source. `_sm64_saturn_ztreme_
frustum_aabb` is at `0x0607aa64` (`0x528` bytes) and issues exactly **seven
`jsr`**: three to `_support_radius_q16` (`0x0607a9d4`, loaded into `r14` at
`0x0607abe6`) and **four to `_scaled_limit`** (`0x0607aa1c`, loaded into `r11`
at `0x0607acaa`). `_scaled_limit` writes DVSR/DVDNTH/DVDNTL and reads
DVCR/DVDNTL at `0xFFFFFF00/10/14/08` — the SH-2 on-chip divider — so this is a
genuine 64/32 hardware division (39 cycles of unit latency plus the DVCR
read-modify-write and the call frame), **not** a libgcc `___divdi3`. T2.5's
Finding-A pathology does not apply here.

Four divisions per test at 868 tests per frame is **3,472 hardware divisions per
frame**, and they are avoidable: `|x| ≤ far_z·half_width / focal` is
`|x|·focal ≤ far_z·half_width` — two `dmuls.l`. The SH-2 does not need the
divider for this at all.

**Estimated saving: ~250,000-260,000 cycles ≈ 22% of `cluster_test` ≈ 12% of the
stage ≈ 0.55 VBlanks. Confidence: medium-high** — the call count and the divider
usage are certain from the linked image; the per-division cost is SH-2 datasheet
arithmetic, not measured in isolation.

### Finding F — every admission byte is read cache-through from the A-bus cartridge

Symbol placement in the linked image (`sh-elf-nm`):

| Symbol | Address | Bytes | Region |
| --- | --- | ---: | --- |
| `sm64_saturn_bob_render_clusters` | `0x22761668` | 45,084 | `.cart_rodata` |
| `sm64_saturn_bob_scene_admission_cluster_refs` | `0x22760f7c` | 1,734 | `.cart_rodata` |
| `sm64_saturn_bob_scene_admission_nodes` | `0x22761644` | 36 | `.cart_rodata` |
| `s_render_work_order` (admission output) | `0x060f04b4` | 1,734 | HWRAM, cached |
| `s_admission_scratch` target (`s_terrain_master_commands`) | — | 12,288 used | `.lwram_bss` |

`.cart_rodata` is linked at `0x22400000`
(`src/port/saturn/sourceboot/sourceboot-cart.x:22`), the SH-2 **cache-through**
window on the A-bus cartridge, and the port has already established that no
cached CS0 alias exists (`docs/saturn/PERFORMANCE_DIAGNOSIS_2026-07-26.md`,
hypothesis 2: "CONFIRMED and unavoidable for the cart").

`sizeof(sm64_saturn_render_cluster_t)` is 52 bytes
(`saturn_render_cluster.h:17-33`; 45,084 / 867 = 52 exactly). Counting the
accesses:

| Phase | Uncached cart accesses per frame |
| --- | ---: |
| `metadata_valid()` cluster loop (`:158-169`) | ~10,400 |
| `metadata_valid()` containment loop (`:182-197`) | ~11,300 |
| `metadata_valid()` range + coverage loops (`:199-206`) | ~1,730 |
| traversal cluster loop (`:319-331`) | ~6,900 |
| mandatory sweep `cluster->mandatory` (`:391-393`) | ~870 |
| **total** | **~31,200** |

This is unlike T2.5's Finding D, which was 5,632 reads and 1-2% of its stage.
Here it is ~5.5x the volume on a stage two-thirds the size.

**The measurement corroborates the estimate independently.** `validate` is
217,614 cycles for ~19,400 uncached reads = **11.2 cycles per access if memory
dominated it** — squarely inside the 10-20 cycle band that document quotes,
which is what one would expect of a loop whose body is little but loads. Applied
to the remaining ~7,800 traversal-side accesses that survive Finding C, the
residual cart exposure is **~87,000 cycles ≈ 4% of the stage.**

**Estimated saving after Finding C: ~90,000-150,000 cycles ≈ 4-7% of the stage ≈
0.20-0.32 VBlanks. Confidence: medium** — the region and the access counts are
certain; the per-access latency is corroborated but not directly measured.

### Finding G — the frustum basis is derived once, correctly

To close the brief's question rather than leave it open: `admission_frustum()`
is called once, before the loop (`saturn_scene_admission.c:301`), and nothing
inside the traversal recomputes a plane, a matrix or a normalisation. **There is
no per-node frustum re-derivation** — measured at **4.2 ticks, 0.03% of the
stage**.

What *is* per-box is `world_bounds()` (`:36-45`, called from `test_bounds()` at
`:114`), which applies `floor_q16`/`ceil_q16` to six `int32` fields of a
`static const` record — the same six integers, every frame, forever. That is a
bakeable per-cluster precompute of the same class T2.5 flagged as its Finding H,
and it is folded into remediation 4.

### Finding H — admission's result is immediately re-tested by `work_order`

`demo_prepare_render_work_order()` (`saturn_demo_render.c:935`) takes
admission's output, resets `s_render_work_count` to zero (`:957-958`), and
re-filters every admitted cluster through `sm64_saturn_render_cluster_admit()`
(`:972-987`), which re-reads the **same** `cluster->bounds_min_q16` /
`bounds_max_q16` from the cart and computes a view-forward depth projection to
reject anything behind the camera (`saturn_render_cluster.h:62-90, 110-112`).

So the frame contains two independent visibility computations over the same
static bounds, back to back. Not the *same* test — admission does a full
six-plane classification, `work_order` does a depth-only rejection plus LOD tier
selection — but the same inputs and the same iteration space, and they could be
one pass.

It is also the single most useful yardstick in this report, and section 8 uses
it.

### Finding I — no soft-float and no software integer division on this path

Checked in the linked image, not the source, in answer to
`sprint2-arithmetic-census.md`. Disassembling
`_sm64_saturn_scene_admit_with_scratch` (`0x06078d64`, `0x948` B), `_test_bounds`
(`0x06078cd4`, `0x90` B) and `_sm64_saturn_ztreme_frustum_aabb` (`0x0607aa64`,
`0x528` B) and scanning for any `___divsi3` / `___udivsi3` / `___modsi3` /
`___divdi3` / `___addsf3` / `___mulsf3` / `___divsf3` / `___adddf3` /
`___muldf3` / `___floatsi*` / `___fixsf*` reference returns **nothing**. No
`sinf` or `cosf` appears either. The only external calls out of the admission
body are `memset` (6), `memcpy` (2), `test_bounds` (3) and `bounds_valid` (3);
the only calls out of the frustum test are `support_radius_q16` (3) and
`scaled_limit` (4).

**The port's native-Q16 premise holds on this path.** 29 `dmuls.l` per frustum
test is honest fixed-point work. The problem is the *number of tests*, the *four
hardware divisions inside each one*, and the *quadratic scan beside them* — not
the number representation.

## 5. Build and identity

Same 27-variable invocation as `sprint1-stage1-link-smoke.md` / T2.2-T2.8 (pool
208), with **exactly one variable different: `SATURN_DIAGNOSTIC_MODE` 0 -> 2**.
Via `tools/saturn/with-msys-toolchain.ps1` -> MSYS `sh --noprofile --norc -l`,
sourcing `.yaul.env`, then `unset COMPILER_PATH`.
`tools/saturn/profiles/sourceboot-bob-demo-v1.json` had `diagnostic_mode`
flipped 0 -> 2 as an uncommitted byte-canonical (LF) edit — T2.1's precedent —
and **has been reverted**: `git diff` on that file is empty and it reads
`"diagnostic_mode":0`.

**One build attempt, exit 0.** The g15 package-staleness cascade did not fire and
no repair was needed. A pre-build syntax gate (compiling the three touched TUs
at mode 2 on the product command line, ~40 s) caught nothing, which is why there
was only one attempt.

Sealed identity **`id-6e1f4ef7ebdb275e`**
(`effective_config_hash 6e1f4ef7ebdb275e1b14559e0224b670088d38cfff4ba6b892b53958f397395a`).

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 10,011,260 | `ef109822ed6e4db7efca5c376e5c209c70081b2d62209a44a40e83914935830a` |
| `sm64-saturn-sourceboot-e2.iso` | 5,173,248 | `02e0269d046da08393b731ac692d7c9796720e4673732c7e6330c97fd4614d1d` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,272 | `b0f07b2e7419dfc1890203063c2b6eb3a387d1f7e07b57ebe79027d3e010bf59` |

Preservation (mandatory rule): the accepted and current candidates
(`id-6b7c7e5d5f71e809`, `id-6eca5970628d581d`, `id-d378c3e178e5dec3` —
ELF/ISO/CUE/manifest) copied to `releases/2026-08-15_t2_9-pre-build/` **before**
the build ran; this build's four artifacts to `releases/2026-08-15_t2_9-diag/`.

Probe symbols in the linked image (`sh-elf-nm`):
`_g_sm64_saturn_prenotify_profile` at `0x002D8958` (LWRAM, NOLOAD),
`_g_sm64_saturn_prenotify_profile_state` at `0x260FB304` (the `.uncached`
section through the P2 alias), `_sm64_saturn_prenotify_profile_publish_admit` at
`0x06078d54` and `_sm64_saturn_prenotify_profile_span` at `0x06078e62`.

### Gate — `verify-memory-map` on the diagnostic build (verbatim)

```
verify-memory-map: checking .../e2-bob-identity-id-6e1f4ef7ebdb275e/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FBD34
  hwram_remaining = 0x42CC bytes (required >= 0x1F00)
  lwram_end       = 0x002E8D60
  lwram_remaining = 0x172A0 bytes (floor >= 0x4000)
  RESULT          = OK
```

**RESULT OK.** True slack over the `0x1F00` floor is **9,164 B**
(`0x42CC − 0x1F00`), against T2.8's `0x462C − 0x1F00` = 10,028 B: the new probe
code and publication cost **864 B of HWRAM**, and only in a diagnostic build.
LWRAM remaining fell by 144 B, consistent with the record's 716 -> 860 B
growth.

### Gate — host contracts

| Suite | Result |
| --- | --- |
| `verify-memory-map` | **RESULT OK** |
| `verify-terrain-depth-bins` | **PASS** |
| `verify-actor-meshlets` | **PASS** (invalid-span mutation fixture caught) |
| `verify-vdp1-painter-chain` | **PASS** |
| `verify-demo-render-overlap` | **PASS** (mutation caught) |
| `verify-render-overlap-integration` | **PASS** (mutation caught) |
| `verify-audio-loop-contracts` | **OK — 24 tests** |
| `verify-pcm68k-model` | **OK** |
| `verify-vdp1-frame-bank` | **OK — 4 tests** |
| `verify-render-job-runtime` | **OK — 5 tests** |
| `verify-scene-admission` | **fixture PASSES; the Make target fails on a path defect — see below** |
| `verify-portal-windows` | **fixture PASSES; same path defect** |

**The two gates that exercise `saturn_scene_admission.c` deserve their own
paragraph, because their Make targets fail and the failure is not this
change's.** Both recipes compile the fixture successfully and then invoke it
through `python.exe -c "subprocess.run([r'/d/Code/...'])"` — an MSYS-style
`/d/...` path handed to a native Windows Python, which cannot resolve it:
`FileNotFoundError: [WinError 2]`
(`Makefile.saturn.mk:753` and `:764`). Running the two fixtures directly, with
the recipes' own compiler flags, on **both** the working tree and a `git show
HEAD:` copy of `saturn_scene_admission.c`:

| Fixture | HEAD | T2.9 working tree |
| --- | --- | --- |
| `tools/saturn/scene_admission_test.c` | compile 0, run **0** | compile 0, run **0** |
| `tools/saturn/portal_window_test.c` | compile 0, run **0** | compile 0, run **0** |

So the substantive gate passes on this change and passes identically on HEAD;
the Make-target failure is a pre-existing host-path defect in the recipe and is
recorded here rather than being papered over. It is a real repo defect and
belongs in someone's backlog, but it is not T2.9's.

## 6. Capture

- Instrument: headless Ymir **build-agent2**
  (`ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`), BIOS
  `sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`, absolute
  `--cue`, release-manifest bound.
- Harness: `tools/saturn/capture_prenotification_profile.py`, updated for the v4
  ABI with a new `spatial_admit` summary block.
- Run shape: 24,000 post-BIOS frames in 300-frame chunks, full 860-byte record
  read at every boundary. **26,181 emulated frames, 371 s wall, 89 samples (77
  with a valid record), 1,330 completed pre-notification windows.**
- **Route: `ROUTE_REPLAY=1`, movement positively witnessed** — **28 distinct
  sampled Mario world positions**. An idle boot would not have been
  representative and was not used.
- On-target identity **MATCH** (`startup_identity_attempts 681`); the build
  record on target reports `diagnostic_mode: 2` and
  `effective_config_hash 6e1f4ef7…395a`.
- **Zero SH-2 exceptions** — `exception_record_clear` true in every sample.
- **`admit_windows = 1,330 = windows`**: one admission call per
  pre-notification window, exactly as the source says.
- **No desktop launch. The owner holds the observation gate.**

### Acceptance — all checks pass, exit 0

```
capture_completed        true    profile_seen             true
exception_record_clear   true    profile_stable_sample    true
frt_wrap_headroom_ok     true    profile_version_ok       true
no_profiler_faults       true    profiler_stack_balanced  true
route_movement_observed  true    vdp_generations_climbing true
windows_accumulated      true    pass                     TRUE
```

`faults = 0`, `end_depth_max = 1` (the design value).

### Cadence — from `summarize_cadence`, which did not abort

A second capture, `tools/saturn/capture_sourceboot_throughput.py` on the same
build, `--startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30`,
**`status: complete`**. Every figure in this block is `summarize_cadence`
output; **none is hand-computed from failure diagnostics** (T2.7 §1.1).

| Phase | VBlanks / frame | Calls / frame |
| --- | ---: | ---: |
| **frame (`vblank_delta`)** | **15.7586** | — |
| construction | 9.7931 | 1.0 |
| — master finalization | 4.7931 | 1.0 |
| — slave work overlap | 2.9655 | 1.0 |
| simulation / source tick | 5.8621 | 1.0 |
| transport + presentation | 0.000 | 4.0 |
| attributed | 15.6552 | — |
| **unattributed** | **0.1034 (0.66%)** | — |
| (dropped VBlank credit — a scheduler counter, not a time phase) | 6.8966 | — |

**3.8074 FPS mean, 3.75 median, 29 intervals, 457 VBlanks.**

Whole-frame perturbation, measured rather than estimated:

| Build | Frame, VB | Construction, VB |
| --- | ---: | ---: |
| product (`id-6eca5970628d581d`, T2.7) | 15.483 | 9.517 |
| T2.8 diagnostic (`id-d378c3e178e5dec3`) | 15.724 | 9.793 |
| **T2.9 diagnostic (`id-6e1f4ef7ebdb275e`)** | **15.7586** | **9.7931** |

The whole diagnostic rig costs **+0.276 VB (+1.78%)** against the product build;
**T2.9's own additions cost +0.035 VB (+0.22%)** on top of T2.8's. Note that the
FRT instrument reports its own increment as +568.6 window ticks ≈ +0.162
VBlank-equivalent, i.e. ~4.6x larger than the VBlank-crossing rig sees. That gap
is within the crossing rig's integer quantisation over 29 intervals and is
stated rather than resolved in favour of whichever instrument is convenient.

Artifacts of record:
`docs/saturn/evidence/reports/sprint2-t2_9-spatial-admit-audit.json` and
`docs/saturn/evidence/reports/sprint2-t2_9-throughput.json`.

## 7. Results

### The node tree, for context (1,330 windows, mean per window)

| Rank | Node | Mean ticks | Mean cycles | % of window | Max ticks | Calls |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | **`spatial_admit`** | **16,588.1** | **2,123,278** | **77.77%** | 19,216 | 1 |
| 2 | `work_order` | 1,946.6 | 249,161 | 9.13% | 2,431 | 1 |
| 3 | `meshlet_depth_admit` | 1,132.6 | 144,975 | 5.31% | 1,141 | 31 |
| 4 | `meshlet_admit` | 526.3 | 67,372 | 2.47% | 537 | 1 |
| 5 | `position_set` | 460.3 | 58,915 | 2.16% | 566 | 1 |
| 6 | `meshlet_emit` | 262.3 | 33,571 | 1.23% | 266 | 1 |
| 7 | `mario_ctx` | 162.0 | 20,742 | 0.76% | 165 | 1 |
| 8 | `mario_draw_order` | 109.4 | 13,997 | 0.51% | 112 | 1 |
| — | `window_residue` (unattributed) | 30.0 | 3,842 | 0.14% | 32 | 1 |
| | **window total** | **21,330.3** | **2,730,280** | 100% | 24,550 | — |

### The ranked sub-stage table — inside `demo_spatial_admit()`

Mean per admission call over 1,330 calls. **Measured** is what the instrument
read; **de-perturbed** subtracts this task's own probe cost at the uniform
0.3914 ticks/span derived in section 3 and is the basis for the shares, because
it reconstructs T2.8's unprobed `spatial_admit` to −0.036%. VBlank-equivalents
are `share × 4.5654`, T2.6's and T2.7's unperturbed figure for this node, so
they carry no assumed constant of this task's own.

| Rank | Sub-stage | Measured ticks | De-perturbed ticks | Cycles | % of stage | VBlank-eq | Per-unit |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |
| 1 | **`cluster_test`** | 9,087.1 | **8,747.8** | **1,119,714** | **54.61%** | **2.4931** | **1,291 cycles / cluster tested** (867) |
| 2 | **`cluster_dedup`** | 4,542.0 | **4,431.2** | **567,198** | **27.66%** | **1.2629** | **14.22 cycles / comparison** (39,903) |
| 3 | **`validate`** | 1,700.5 | **1,700.1** | **217,614** | **10.61%** | **0.4845** | 11.2 cycles / uncached cart read (~19,400) |
| 4 | **`mandatory`** | 1,060.7 | **1,060.3** | **135,720** | **6.62%** | **0.3022** | 156.6 cycles / cluster swept (867), admits 0 |
| 5 | `cluster_emit` | 157.8 | 47.0 | 6,021 | 0.29% | 0.0134 | 21 cycles / admission (283) |
| 6 | `scratch_clear` | 13.7 | 13.3 | 1,704 | 0.08% | 0.0038 | 0.42 cycles / byte (4,096) |
| 7 | `node_test` | 11.8 | 11.4 | 1,460 | 0.07% | 0.0033 | 1,460 cycles / node (1) |
| 8 | `frustum_derive` | 4.6 | 4.2 | 539 | 0.03% | 0.0012 | once per frame |
| 9 | `view_setup` | 3.6 | 3.2 | 411 | 0.02% | 0.0009 | once per frame |
| 10 | `portal` | 0.6 | 0.2 | 27 | 0.00% | 0.0001 | 0 portals |
| | **stage total** | **16,582.4** | **16,018.8** | **2,050,406** | **100%** | **4.5654** | **2,365 cycles / cluster** |

**Unattributed remainder: −0.0 ticks (−2×10⁻¹⁶ of the stage).** The chained
cursor makes the decomposition exact by construction, so this row is a check on
the implementation rather than a residual: every tick between the first and last
probe is charged to a named bucket.

**Two instruments, cross-checked.** The node tree, which brackets the same code
from the outside and knew nothing about the spans, reads **16,588.1** ticks
against the span sum's **16,582.4** — a ratio of **0.9997**. The 5.7-tick gap is
the epilogue after the last probe plus the node tree's own pop.

### The count table — what fraction of the scene is actually visible

Per frame, from the final sample; the route range is across all 77 valid
samples.

| Quantity | Final sample | Route min | Route max | Note |
| --- | ---: | ---: | ---: | --- |
| Admission nodes in the package | **1** | 1 | 1 | `bob_scene.h:15` |
| **`nodes_tested`** | **1** | 1 | 1 | the whole "traversal" |
| `nodes_admitted` | 1 | 1 | 1 | |
| BSP nodes visited by this path | **0** | 0 | 0 | the 1,183-node BSP is fallback-only |
| `portals_tested` | 0 | 0 | 0 | the package publishes none |
| **`clusters_tested`** | **867** | **867** | **867** | **100% of the scene, every frame, invariant** |
| `clusters_rejected_frustum` | 584 | 514 | 637 | 59-73% of the scene is not visible |
| `clusters_inside` | 91 | — | — | fully inside the frustum |
| `clusters_intersect` | 96 | — | — | straddling a plane |
| OUTSIDE-but-`mandatory` | 96 | — | — | admitted regardless of visibility |
| **`clusters_admitted`** | **283** | **230** | **353** | **26.5-40.7% of the scene** |
| `output_count` | 283 | 230 | 353 | equals `clusters_admitted` |
| `clusters_duplicate` | **0** | 0 | 0 | in 1,330 frames |
| **`dedup_calls`** | **283** | — | — | once per frustum survivor |
| **`dedup_compares`** | **39,903** | **26,335** | **62,128** | `K(K−1)/2` **exactly** |
| `admit_fallback_frames` | **0** | — | — | the legacy painter never runs |

**This table is the answer.** We test 867 clusters to admit 283. We test 867
when 230 are visible and we test 867 when 353 are visible. The traversal has no
mechanism by which it could test fewer, because there is one node.

### Route stability — the finding is a property of the code, not of a moment

Per-frame values at five points spanning the run:

| Window | `clusters_tested` | admitted | `dedup_compares` | `cluster_test` ticks | `cluster_dedup` ticks | `validate` ticks |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 17 (early) | **867** | 239 | 28,441 | 9,011 | 3,238 | **1,701** |
| 36 | **867** | 230 | 26,335 | 8,978 | 3,010 | **1,700** |
| 670 (mid) | **867** | 307 | 46,971 | 9,133 | 5,304 | **1,701** |
| 1,292 | **867** | 283 | 39,903 | 9,083 | 4,517 | **1,700** |
| 1,330 (tail) | **867** | 283 | 39,903 | 9,087 | 4,518 | **1,700** |

- **`cluster_test` varies by ±1% (8,978-9,154) while what is visible varies by
  53% (230-353).** It is flat.
- **`validate` never leaves 1,700-1,701.** It is constant.
- **`cluster_dedup` varies by 76% (3,010-5,304) — and it varies the wrong way,
  rising with visibility**, tracking `dedup_compares`' 26,335 -> 62,128 range,
  which is `K(K−1)/2` at every point.

A working frustum culler's cost falls when less is visible. This one is 71.8%
constant and 27.7% anti-correlated. That is the owner's clue, measured.

## 8. The direct answer to the owner's question

**No. `spatial_admit` does not need to cost anything like this.**

The numbers that justify it:

1. **We do 3.06x more frustum tests than there is anything to admit** — 867
   tested, 283 admitted — and we do exactly 867 whether 230 or 353 are visible.
   There is no early-out anywhere in the production path because
   `ADMISSION_NODE_COUNT` is 1 (Finding A), and the short-circuit that would
   exploit a real tree is already written in the *replaced* path (Finding B).
2. **27.7% of the stage is a linear scan searching for duplicates that cannot
   exist** — 39,903 comparisons, 0 found, in 1,330 frames — and a further 6.6%
   is the same scan run again from the mandatory sweep, which admits nothing.
   **34.3% of the stage produces no output at all.** The O(1) array that
   replaces it is already allocated and already cleared (Finding D).
3. **10.6% of the stage revalidates `static const` data**, at 1,700 ticks a
   frame, forever (Finding C).
4. **Each of the 867 tests spends four SH-2 hardware divisions** computing a
   projected limit that a cross-multiply gives for two multiplies — 3,472
   divisions per frame (Finding E).
5. **Each test reads its operands cache-through from the cartridge**
   (Finding F), and the same operands are re-read minutes later, in the same
   frame, by `work_order` (Finding H).

### What the defensible cost is

The strongest yardstick is this port's own measurement, on this scene, on this
hardware, in the same frame:

**`work_order` (`demo_prepare_render_work_order`) costs 1,946.6 ticks =
249,161 cycles = 0.554 VBlanks** to walk the **283** admitted clusters, read the
**same** cart-resident Q16 bounds, run a three-axis view-forward projection on
each, and select a LOD tier. That is **880 cycles per cluster touched.**

Admission's `cluster_test` is **1,291 cycles per cluster touched** — the same
order. Admission is not slow because a frustum test is intrinsically expensive
on an SH-2. **Admission is slow because it touches 867 clusters instead of ~283,
and because it wraps that pass in a quadratic scan, a constant revalidation and
a fruitless sweep.**

So the defensible cost is roughly "one honest per-cluster pass over what is
plausibly visible":

| Basis | Cycles | VBlanks/frame |
| --- | ---: | ---: |
| 283 clusters × 880 cycles (`work_order`'s own measured rate) | 249,000 | **0.55** |
| 400 clusters × 700 cycles (a hierarchy with margin, divides removed) | 280,000 | **0.62** |
| 867 clusters × ~100 cycles (the brief's flat, no-hierarchy figure) | 87,000 | **0.19** |
| **measured today** | **2,050,406** | **4.565** |

**A defensible cost for this stage is 0.5-0.8 VBlanks per frame. It measures
4.565. The recoverable difference is ~3.8-4.0 VBlanks — 24-26% of the whole
15.483-VBlank frame — at no fidelity cost and without a BOB bypass.**

For calibration against the sprint's other target: `demo_prepare_mario()` —
the sprint's previous largest block, 11.69 VBlank-equivalent when T2.5 measured
it — now costs **2,068.5 ticks, 9.70% of the window**, after T2.6 landed.
**`spatial_admit` is the largest single node in the frame by 8.5x over the next
one** (`work_order`, 1,946.6 ticks), and this is where the remaining cadence
lives.

## 9. Honesty — what is wrong with these numbers

1. **`cluster_test` is "cluster test **plus** loop overhead", by construction.**
   One cursor threads the call, so any iteration that `continue`s before
   reaching a later probe has its tail charged to the next probe opened — the
   next iteration's `cluster_test`. The bucket is an upper bound on the frustum
   test itself. Stated in the header, in section 1 and here rather than buried.
2. **My perturbation prediction was wrong by 3.5x per span** (~14 cycles
   predicted, ~50 measured). The de-perturbed column is a derivation from the
   *measured* aggregate, and it reconstructs T2.8's independent unprobed figure
   to −0.036% — but it assumes the per-span cost is uniform across buckets, and
   that assumption is not independently verified.
3. **`cluster_emit`'s measurement is dominated by its own probe.** 157.8 ticks
   measured, ~110.8 of which is probe: the de-perturbed 47.0 is the difference
   of two similar numbers and should be read as "small", not as "47".
4. **The two instruments disagree about the size of this task's own
   perturbation** — +0.162 VBlank-equivalent by the FRT rig, +0.035 VBlanks by
   the crossing rig. That is within the crossing rig's integer quantisation over
   29 intervals. Neither is declared correct here.
5. **`validate` includes the entry guards and the `stats` memset**
   (`saturn_scene_admission.c:284-297`). Tens of cycles against a bucket in the
   hundreds of thousands; the bucket is still overwhelmingly `metadata_valid()`.
6. **Per-cluster cycle figures are means over a non-uniform population.** A
   rejected cluster never reaches dedup or emit, so "cycles per cluster tested"
   is a stage average, not any one cluster's journey.
7. **Finding F's cycle range is corroborated, not measured.** The access counts
   are certain from the source and the struct layout; the ~11 cycles/access
   figure is `validate`'s total divided by its estimated access count, which
   assumes memory dominates that loop.
8. **Finding E's saving is datasheet arithmetic.** The four `scaled_limit` call
   sites per test are read out of the linked image and are certain; the 39-cycle
   SH-2 DIVU latency is not measured in isolation by this task.
9. **`metadata_valid()`'s iteration counts are derived, not probed** — for the
   same reason T2.5 derived its per-vertex divisor. They are compile-time
   properties of `bob_scene.h`. The single `validate` bracket confirms only the
   total.
10. **One route, one level, one camera path, 28 sampled positions.** The
    "fraction visible" numbers are properties of BOB area 1 on route 0. The
    *structural* findings — one admission node, an O(K²) scan, per-frame
    revalidation of constants, four divisions per test — do not depend on the
    route.
11. **Two standing gates could not run their Make targets** (section 5). Their
    fixtures were run directly and pass on both trees, but the gates themselves
    are red in this repo and that is stated, not smoothed over.
12. **Nothing here was optimised, and no saving is a before/after.** Every
    figure in section 10 is an estimate against a measured baseline, ranked by
    confidence for exactly that reason. Item 3 is explicitly flagged as the one
    that can move the admitted set.

## 10. Ranked remediation list

Ordered by (saving × confidence) / risk. Every item is **GENERIC** unless marked
otherwise — the owner asked for fixes that help every level, not a BOB bypass,
and the ranking reflects that. Nothing here was implemented.

| # | Fix | Scope | Est. saving | Conf. | Risk | Test/gate that would prove it |
| ---: | --- | --- | ---: | --- | --- | --- |
| 1 | **Replace the O(K²) duplicate scan with the O(1) membership array that already exists.** `output_has_cluster()` (`saturn_scene_admission.c:118-125`, called at `:333` and `:393`) walks the whole output list per admission. `s_admission_cluster_seen[4096]` (`saturn_scene_admission.h:96`) is already in the scratch struct and is already cleared each frame at `:204`; set its bit at `:342` and test it at `:333` and `:393`. Zero new memory, zero new clearing. Fixes `cluster_dedup` **and** most of `mandatory`. | **GENERIC** | **~700,000 cycles, 34.3% of the stage, ~1.56 VB/frame** | High | **Low** | Bit-identical output: a host fixture asserting the admitted index *sequence* is byte-equal to HEAD's over a corpus of camera poses. Measured witness already in place — `dedup_compares` should fall from 39,903 to 0. If item 2 lands first, the `:204` clear must move into the admit path (867 bytes, ~200 cycles); the fixture catches it if it does not. |
| 2 | **Stop revalidating static package metadata every frame.** `metadata_valid()` is called unconditionally at `:298` and proves properties of a `static const` generated header that cannot change between frames — measured at a flat 1,700-1,701 ticks in every one of 1,330 frames. Memoise on the view's own identity (the `clusters`/`nodes`/`cluster_refs` pointers plus all four counts plus `metadata_version`) and revalidate only when that key changes. | **GENERIC** | **217,614 cycles, 10.6% of the stage, 0.48 VB/frame** | High | **Low** | `verify-scene-admission` must still reject every malformed fixture on first bind; add a fixture that binds the same view twice, and one that binds a *different* malformed view second and is still rejected. Fail-closed is preserved because the key covers every field the validator reads. (Repair the recipe's host-path defect first — section 5.) |
| 3 | **Delete the four hardware divisions per AABB test.** `scaled_limit()` (`ztreme_frustum.c:43-50`) is called four times per test (`:86-93`); each launches an SH-2 64/32 DIVU. `\|x\| ≤ far_z·half_width / focal` is `\|x\|·focal ≤ far_z·half_width` — two `dmuls.l`, no divider. 3,472 divisions per frame removed. | **GENERIC** | ~250,000 cycles, ~12% of the stage, ~0.55 VB/frame | Med-High | **Med** | **The only item that can move the classification boundary.** The file is a GPL close-port with an explicit conservativeness contract (`ztreme_frustum.c:24-28`: ceil so quantisation cannot turn a touching box into a false OUTSIDE). Gate: a host differential fixture over a randomised corpus of (frustum, AABB) pairs asserting the new result is **never OUTSIDE where the old was not**, plus `clusters_admitted` parity on the route (the rig already publishes it). |
| 4 | **Give admission a hierarchy again, and restore the INSIDE short-circuit.** Two halves: (a) bake more than one admission node — the 1,183-node BSP with the same content id already exists (`bob_bsp.h:5`, `bob_scene.h:17-18`) while `ADMISSION_NODE_COUNT` is 1; (b) thread the node's frustum result into the cluster tests so an INSIDE node's clusters skip testing — the pattern already written at `saturn_demo_render.c:772-785` and recorded as reference technique in `sprint2-t2_0-reference-sweep.md` §4.3 (`ZT_RENDERING.c:425, 486-492`). OUTSIDE-node pruning already works (`:317`), so only (a) and (b) are new. | **GENERIC** | ~450,000-560,000 cycles, ~22-27% of the stage, **~1.0-1.2 VB/frame** — and it is the only item that makes the cost *fall when less is visible* | Medium | **Med-High** | The only item that changes what the package contains, so it needs a baker change and a new generated section. Gate: a host fixture asserting the hierarchical admitted set is a **superset-or-equal** of the flat set for a corpus of poses (the short-circuit must stay conservative), plus `clusters_tested` on the route falling below 867 while `clusters_admitted` is unchanged, plus `verify-memory-map`. |
| 5 | **Stage the cluster AABBs out of the cartridge and pre-de-Q16 them.** Every bound is read cache-through from `.cart_rodata` at `0x22400000` (Finding F), and `world_bounds()` (`:36-45`, called at `:114`) re-derives the same six integers from the same constants every frame (Finding G). Copy the 867 bounds into work RAM once per scene bind, already floored/ceiled. | **GENERIC** | ~90,000-150,000 cycles, 4-7% of the stage, ~0.20-0.32 VB/frame (after item 2, which removes ~72% of the cart traffic) | Medium | **Med** | `verify-memory-map` is the arbiter, not this estimate: 867 × 24 B = 20,808 B in world-unit `int32`, or 10,404 B at `int16`. True HWRAM slack is **9,164 B** on this diagnostic build, so this belongs in LWRAM or must be `int16`. Plus `verify-scene-admission` for equivalence. |
| 6 | **Fold `work_order` into admission.** `demo_prepare_render_work_order()` (`saturn_demo_render.c:935-996`) immediately re-reads the same cart-resident bounds it just admitted and runs a second, depth-only visibility test plus LOD tier selection (`saturn_render_cluster.h:110-112, 121-123`). One pass could produce the admitted set, the tier and the result record together. | **GENERIC** | up to 249,161 cycles, **0.554 VB/frame** (`work_order`'s whole measured cost) | Medium | **Med** | Do only after 1-3, for the reason T2.8 gave about the slave window: restructuring a stage that is about to lose most of its cost measures the wrong thing twice. Gate: `verify-scene-admission`, `verify-terrain-depth-bins`, and admitted-primitive parity on the route. |
| 7 | **Clear the traversal scratch to the scene's node count, not to `MAX_NODES`.** `:299-300` memsets 4,096 bytes to service a queue that holds one entry; `scene->node_count` is already validated `<= MAX_NODES` at `:145`. | **GENERIC** | **1,704 cycles, 0.08%** — measurement demoted this from "worth doing" to "free tidy-up" | High | **Low** | `verify-scene-admission`; the bound is already validated. Listed because it is free and removes a `sizeof`-vs-`count` trap that will bite when a bigger scene ships — **not** because it is a cadence lever. |
| 8 | **BOB-only bypass — restore `demo_spatial_admit_node()` as production** (T2.7 item 1, T2.8 next-step 1). | **BOB-ONLY** | ~4.3 VB | High | **Med** | Listed for completeness and **not recommended as the fix**. Its only remaining value is diagnostic: one build would confirm this attribution end to end. Items 1-4 recover ~3.6 VB generically and leave the module correct for the multi-level renderer strategy, so taking the bypass spends comparable schedule and forfeits the generality. |

**Items 1 + 2 alone are worth ~918,000 cycles ≈ 2.04 VBlanks/frame — 45% of the
stage — and neither can change a single admitted cluster.** They are the two
lowest-risk items on the list and they are also the two largest after
`cluster_test`. That is the recommended first commit.

### Explicitly NOT recommended

- **Reducing the polygon budget or the cluster count.** The cost is not in the
  geometry. Halving the scene halves a number that should be ~8x smaller
  regardless, and `clusters_tested` would still equal the whole scene.
- **Caching the admitted set across frames.** `clusters_admitted` moves 230-353
  across the route and the camera moves every frame; an invalidation key on
  camera position and orientation would miss on essentially every moving frame.
  Items 1-4 need no invalidation key and cannot go stale.
- **`always_inline` on the admission helpers.** `test_bounds`, `bounds_valid`,
  `support_radius_q16` and `scaled_limit` are all out-of-line at `-Os`, but
  items 1-4 *delete* call sites rather than making them cheaper. T2.5's Finding
  E lesson, restated so the same budget is not spent twice.
- **Anything fill-rate related.** T2.8 established the frame is CPU-bound and
  the VDP1 fence measures zero. Nothing in this report changes that.

## 11. Reproduction

```
# Pre-build syntax/codegen gate (seconds, not minutes):
#   scratchpad/t29_syntax.sh 2 -- compiles saturn_scene_admission.c,
#   saturn_demo_render.c and main.c at SATURN_DIAGNOSTIC_MODE=2 on the
#   product build's own command line.

# Object-level product-cleanliness proof (no worktree modification):
#   scratchpad/objproof_t2_9.sh  -- five TUs at SATURN_DIAGNOSTIC_MODE=0 -g0,
#     working tree vs a path-identical `git show HEAD:` mirror. T2.8's method.
#   scratchpad/objproof_t2_9b.sh -- byte census + disassembly + section diff
#     that characterises the saturn_demo_render.o exception.

# Diagnostic build: the 27-variable invocation from
# sprint1-stage1-link-smoke.md with SATURN_DIAGNOSTIC_MODE=2 and
# tools/saturn/profiles/sourceboot-bob-demo-v1.json's
# release_config.diagnostic_mode temporarily 0 -> 2 (canonical LF, uncommitted,
# reverted after the build), via
#   tools/saturn/with-msys-toolchain.ps1 sh --noprofile --norc -l -c
#     'source ../../.yaul.env; unset COMPILER_PATH;
#      make -f Makefile.saturn.mk -j1 sourceboot <27 vars>'
# FREEZE ALL TRACKED SOURCE FIRST -- the sealed identity is derived from a
# source-hash spec frozen at build start (T2.8 S4 attempt 2).
make -f Makefile.saturn.mk verify-memory-map

python tools/saturn/capture_prenotification_profile.py \
  --ymir  <abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl   "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game  <abs>/build/saturn/sourceboot/e2-bob-identity-id-6e1f4ef7ebdb275e/sm64-saturn-sourceboot-e2.cue \
  --elf   <abs>/.../obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest <abs>/.../saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_9-spatial-admit-audit.json \
  --post-bios-frames 24000 --sample-interval 300
# the new "spatial_admit" block carries the ranked sub-stage table, the count
# table and the per-unit derivations.

python tools/saturn/capture_sourceboot_throughput.py \
  --ymir <...> --ipl <...> --game <...> --elf <...> --release-manifest <...> \
  --output docs/saturn/evidence/reports/sprint2-t2_9-throughput.json \
  --startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30 --timeout 1800
# cadence figures MUST come from summarize_cadence output. If it aborts with
# "phase VBlank crossings exceed the observed interval", report that it aborted
# -- do not substitute arithmetic over vblanks_advanced, which includes the
# pre-gameplay ramp (T2.7 S1.1). It did not abort here: status "complete".
```

Source and image claims in this report can be checked directly:

```
sed -n '15,16p'   build/saturn/sourceboot/generated/bob_scene.h
sed -n '118,125p;136,269p;298,347p;391,403p' src/port/saturn/gfx/saturn_scene_admission.c
sed -n '762,790p;844,900p;935,996p'          src/port/saturn/gfx/saturn_demo_render.c
sed -n '43,50p;86,93p'                       src/port/saturn/gpl/ztreme_frustum.c
sh-elf-nm -S <elf> | grep -E 'bob_render_clusters|admission_cluster_refs|admission_nodes'
sh-elf-objdump -d --start-address=0x0607aa64 --stop-address=0x0607af8c <elf>  # 7 jsr
sh-elf-objdump -d --start-address=0x0607aa1c --stop-address=0x0607aa64 <elf>  # DIVU
```
