# Sprint 2 Task T2.12 — hierarchical scene admission, landed and measured

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `3bf5f590`.
- Task: T2.9's ranked item 4 — "give admission a hierarchy again, and restore
  the INSIDE short-circuit". Specification:
  `sprint2-t2_9-spatial-admit-audit.md` §10 item 4 and Findings A/B.
- Equivalence-oracle pattern: T2.10 (`sprint2-t2_10-spatial-admit-fixes.md` §2),
  itself following T2.6. Reference technique: Z-Treme's `ztCheckBoxInFrustum`
  short-circuit, `sprint2-t2_0-reference-sweep.md` §4.3.
- Measurement basis: `summarize_cadence` only, repaired by T2.11.
- **Both halves were in scope and both landed.** Sections 1 and 2 say why the
  offline half turned out to be small.

## Headline

The product build runs at **4.9432 FPS mean, 12.1379 VBlanks per
frame** - against T2.10's `id-b46f60d0a6d129dd` at 4.3176 / 13.8966 on the
same tool, same emulator, same route. **-1.7587 VBlanks per frame, +14.5%
FPS**, and **not one admitted cluster moved.**

- **`SM64_SATURN_BOB_ADMISSION_NODE_COUNT` goes from 1 to 255** — 128 leaves,
  depth 7, over the same 867 cluster refs. The production spatial index is a
  tree now, not a flat list wearing a tree's type signature.
- **The admitted cluster index array is byte-identical.** 48,200 pose
  comparisons against the pinned flat traversal, 13,835,970 cluster
  admissions, **0 divergences** — not a set match, an index-for-index match of
  the emitted array.
- **Total frustum tests per pose fall to 31.6% of the flat pass** on the host
  corpus (147.9 cluster tests + 106.0 node visits, against 802.2), and
  `clusters_tested` now **varies with the view** — 0 to 548 on the corpus,
  against a flat pass that reads 867 in every frame without exception.

---

## 1. Which half was scoped, and why both fit

The brief allowed scoping to one half if the offline side was large. It is not
large, and the reason is worth recording: **`emit_bob_scene.py` already owned
the whole admission section**, in one function
(`build_scene_admission_metadata()`, 38 lines), whose docstring said in so many
words that it emitted "one conservative root node" as a placeholder until the
S64P portal section grew node bounds. The generated header is a build artifact
(`build/saturn/sourceboot/generated/bob_scene.h`, untracked, rebuilt by
`compile-bob-scene` whenever the tool changes), so there was no committed data
blob to migrate and no package-format version to negotiate. The offline change
is **+95 / −20 lines of Python**.

What was *not* obvious before reading the code, and what changed the plan:

- **T2.9's suggestion to reuse the existing 1,183-node BSP was not taken.**
  That tree is a *splitting-plane* BSP built for painter ordering
  (`bob_bsp.h`: `_planes`, `_distances`, `_children`,
  `_octant_child_order`), and its nodes index *primitive spans*, not admission
  cluster refs. Adapting it would have meant a cross-index mapping and a second
  containment proof over data shaped for a different job. A median split
  straight over the 867 cluster AABBs is 60 lines, produces bounds that are the
  exact union of what they own — which is precisely the invariant the runtime
  needs — and leaves the BSP alone for the fallback painter that still uses it.
- **The runtime had no descent to restore.** T2.9 Finding A says the BFS "can
  never enqueue anything else, because the only enqueue site is inside the
  portal loop". That is exact: `sm64_saturn_scene_admission_node_t` had no
  child link at all. The traversal is a portal graph walk, not a tree walk. So
  half (b) is not "thread the node result into the cluster tests" — it is that
  **plus** a child edge in the node record and the validation that makes the
  edge safe.

## 2. The data — 255 nodes over 867 clusters

`build_admission_hierarchy()` in `tools/saturn/emit_bob_scene.py`:

| Property | Value | Why the runtime needs it |
| --- | --- | --- |
| split rule | median on the widest axis of the node's own bounds | — |
| leaf capacity | **8 clusters** | measured, §5 |
| node bounds | **exact union** of the cluster bounds the node owns | makes a node's bounds a bound on its whole subtree, which is what licenses the prune and the short-circuit |
| child numbering | breadth-first, so `child_first > parent index` | the descent terminates, the graph is acyclic, and `visited[]` stops being load-bearing |
| leaf ref spans | contiguous, partitioning `cluster_refs` | every cluster is still referenced exactly once, so `metadata_valid()`'s coverage sweep is unchanged |

Result on BOB: **255 nodes, 128 leaves, maximum depth 7**, 867 cluster refs
unchanged. The node array grows from 36 bytes to **9,180 bytes** — in
`.cart_rodata`, on the A-bus cartridge, not in work RAM.

**None of those five properties is trusted.** `metadata_valid()` re-proves all
of them at bind time; §4 lists the checks and the mutations that kill them.

## 3. The runtime — descent, inheritance, prune

`src/port/saturn/gfx/saturn_scene_admission.c`. Three changes, in the order
they matter.

### 3.1 The descent and the short-circuit

The node record's `reserved` word becomes `child_first`, and a `child_count`
takes the two bytes the struct was already padding to 36 — **`sizeof` is
unchanged**. Queue entries now carry the parent's classification in the two
spare high bits of the existing `uint16` queue word (`MAX_NODES` is 2,048, so
bits 14-15 are free), which is why the descent needed **no new scratch**.

```
inherited == INSIDE   -> node_state = INSIDE, no test at all
otherwise             -> test the node
node_state == OUTSIDE -> prune: no clusters, no children
node_state == INSIDE  -> admit every cluster ref without testing it
node_state == INTERSECTS -> test each cluster; enqueue children with INTERSECTS
```

That is `demo_spatial_admit_node()`'s shape
(`saturn_demo_render.c:772-785`), which is this repository's own close-port of
`ZT_RENDERING.c:425, 486-492`. It was adapted rather than reinvented, as
T2.9 Finding B pointed out it should be.

### 3.2 The correctness hole the oracle found, and the margin that closes it

**Pruning an OUTSIDE subtree is not automatically equivalent to testing every
cluster in it, and the first implementation was wrong.**

Containment of the *true* boxes is validated. But
`sm64_saturn_ztreme_frustum_aabb()` does not test true boxes: it reduces each
AABB to an integer centre/half-extent pair (`ztreme_frustum.c:85-91`), then
floors the projected centre and ceils the projected support radius. Every one
of those steps is individually conservative — each can only *widen* a box — but
they widen the node and the cluster **independently**, so a small cluster's
widened projection can poke past its large parent's widened projection. The
oracle found exactly that: **102 poses** where the flat pass admitted a cluster
whose node the hierarchy had pruned, every one of them within a couple of world
units of the near plane, and every one of them in the corpus's widest frustum
template where the near plane is the only active limit.

The fix is a margin on node tests and nothing else
(`ADMISSION_NODE_MARGIN`, 8 world units), derived rather than tuned:

- the centre/extent quantisation places the reconstructed box inside
  `[min − 1, max + 1]` per axis, whose projection onto a Q16 unit basis is at
  most `sum|basis| <= sqrt(3) < 1.74` wider per side;
- the floor/ceil pair adds at most 1 more;
- so a contained cluster's computed projected extent exceeds its node's by
  **less than 3.74 world units**, and widening the node by *d* raises its own
  projected extent by at least *d*, because `sum|basis| >= 1` for a unit basis.

Eight is that bound with better than a factor of two in hand, against a scene
spanning ±8,192. **Widening is conservative in both directions**: a wider node
is harder to classify OUTSIDE (fewer prunes) and harder to classify INSIDE
(fewer short-circuits). It can never admit less. Removing it is killed by the
gate (§6, M4).

### 3.3 Emission had to stop depending on traversal shape

The flat pass emitted during the traversal and then ran a trailing mandatory
sweep. On BOB that produced a **strictly ascending** index array: the single
node's refs are ascending, and T2.9 measured that the trailing sweep "admits
nothing" because a mandatory cluster classified OUTSIDE was already admitted
inline.

A hierarchy breaks that. Mandatory clusters in a pruned subtree are no longer
seen by the traversal, so the trailing sweep picks them up and **appends them
to the tail** — 96 of them per frame, by T2.9's count table. The set would
still be equal; the sequence would not. That matters because admission output
order is an input to the downstream depth-bin scatter's tie order.

So the traversal now only **marks** `s_admission_cluster_seen[]`, and one pass
at the end emits in ascending cluster index, folding in the mandatory
obligation:

```c
for (index = 0U; index < scene->cluster_count; index++) {
    if (s_admission_cluster_seen[index] == 0U) {
        if (cluster->mandatory == 0U) continue;
        stats->mandatory_clusters_admitted++;
        s_admission_cluster_seen[index] = 1U;
    }
    ... emit index ...
}
```

This replaces the old trailing sweep rather than adding to it — the same 867
iterations with the same one cartridge read of `mandatory`, plus an LWRAM byte
read. **On a flat scene it is a no-op**, which is the point: it makes the two
forms byte-identical instead of merely set-equal.

Independent corroboration that it changed nothing on the flat path:
`verify-frustum-equivalence`, which drives the real `sm64_saturn_scene_admit()`
over 1,024 poses on its own synthetic scene, still prints **admitted-set digest
`0f70643abf026a13`** — the same value T2.10 recorded.

## 4. Fail-closed validation of the new data

Added to `metadata_valid()`:

| Check | What it prevents |
| --- | --- |
| `child_first + child_count <= node_count` | out-of-range descent |
| `child_count == 0` implies `child_first == 0` | a stale word read as a child range; this replaces the old `reserved != 0` check |
| `child_first > index` | a cycle, or a descent that does not terminate |
| child bounds contained in parent bounds | **the property the prune and the short-circuit rest on** |
| forest check: no node claimed by two parents, root claimed by none | an admitted set that depends on which edge the queue reached first |

The forest check borrows the `visited[]` bank, which the admission path clears
for itself immediately afterwards. All of this runs **once per binding**, not
once per frame — T2.10's memo already made `metadata_valid()` bind-scoped, so
the new checks cost nothing per frame.

## 5. Leaf size 8 is measured, not chosen

The gate prints the sweep. Total frustum tests per pose, over 964 poses × 5
frustum templates, against the flat pass's 802.2:

| Leaf cap | Nodes | Leaves | Depth | Cluster tests | Node visits | **Total vs flat** | Divergences |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 1,733 | 867 | 10 | 55.8 | 488.5 | 67.8% | 0 |
| 2 | 1,023 | 512 | 9 | 77.0 | 315.3 | 48.9% | 0 |
| 4 | 511 | 256 | 8 | 107.6 | 181.0 | 36.0% | 0 |
| **8** | **255** | **128** | **7** | **147.9** | **106.0** | **31.6%** | **0** |
| 16 | 127 | 64 | 6 | 203.5 | 62.9 | 33.2% | 0 |
| 32 | 63 | 32 | 5 | 280.9 | 37.2 | 39.7% | 0 |
| 64 | 31 | 16 | 4 | 366.5 | 21.8 | 45.7% | 0 |
| 128 | 15 | 8 | 3 | 493.7 | 12.1 | 63.0% | 0 |
| (flat, 1 node) | 1 | 1 | 0 | 806.6 | 1.0 | 100.7% | 0 |

The curve is exactly the shape a hierarchy should have: deep trees trade
cluster tests for node tests one-for-one and lose, shallow trees do not prune
enough, and the minimum sits in the middle. **8 is the minimum**, and 4 and 16
bracket it within 4.4 points, so the choice is not delicate.

The `bob-generated` sweep — the tree the baker actually emits, traversed
exactly as the target traverses it — reproduces the leaf-8 row to the digit:
147.9 + 106.0, 31.6%, 0 divergences.

## 6. The equivalence oracle and the mutations

`tools/saturn/admission_hierarchy_test.c`, gate
`verify-admission-hierarchy` (new; `SM64_SATURN_SCENE_ADMISSION_REFERENCE` is
defined by that recipe and nowhere else).

Two statements per pose:

| Statement | What it is |
| --- | --- |
| `reference` | `sm64_saturn_scene_admit_reference_with_scratch()` — the pre-T2.12 traversal copied **verbatim** from HEAD `3bf5f590`, driven with the flat scene (one root node, all 867 refs ascending: exactly what the baker published before this task) |
| `shipped` | whatever `sm64_saturn_scene_admit_with_scratch()` currently is, driven with a hierarchy |

The cluster bank is the **real generated BOB bank**, 867 clusters straight out
of `bob_scene.h`, not a synthetic stand-in. Corpus: 964 camera poses × 5
frustum limit templates = 4,820 comparisons per sweep, 10 sweeps. Poses cover a
6 × 4 × 5 position grid over the scene AABB × 8 yaws × 3 pitches, plus two
cameras a million units outside looking away, plus cameras sitting on the scene
origin (inside the root bounds). Templates are the production BOB frustum
(`saturn_demo_render.c:134-139`), one that admits everything, one that admits
almost nothing, one whose near plane sits deep inside the scene so cluster
bounds straddle it, and one with a degenerate focal length.

The corner cases the brief named are all present and all pass: **camera fully
inside a node** (origin poses, and every grid pose is inside the root); **node
straddling the near plane** (the `near-deep` template, which is where the 102
divergences of §3.2 lived); **empty nodes** (every internal node of a
median-split tree owns zero cluster refs); **single-cluster nodes** (the leaf-1
sweep, 867 of them); **views admitting everything** (867 admitted, the maximum
observed) **and nothing** (125 admitted — the mandatory floor, which is what
"nothing" means for this package).

### Result

```
admission hierarchy: 48200 poses, 13835970 cluster admissions, 0 divergences
admission hierarchy: admitted per pose min 125 max 867
admission hierarchy: clusters_tested reference 38668200, shipped 12953180 (min 0 max 548)
admission hierarchy: admitted-set digest a99b76b99477a4ff
admission hierarchy fixture: PASS
```

**Byte-identical, not set-identical**: the comparison is `memcmp` over
`output->cluster_indices[0 .. cluster_count)`, plus the return value and
`stats.clusters_admitted`, and the first mismatching slot is printed when it
fails.

### Mutations

Applied to the working tree, run, reverted. **None is committed.**

| # | Mutation | Result | Signal |
| --- | --- | --- | --- |
| M1a | node INSIDE short-circuit removed (test the node anyway) | **SURVIVED** | 0 divergences |
| M1b | cluster INSIDE short-circuit removed (test every cluster anyway) | **SURVIVED** | 0 divergences |
| M2 | OUTSIDE prune inverted (`==` to `!=`) | **KILLED** | 65,740 divergences |
| M3 | INTERSECTS descent dropped (children enqueued only from INSIDE nodes) | **KILLED** | 58,878 divergences |
| M4 | node safety margin removed (`ADMISSION_NODE_MARGIN` 8 to 0) | **KILLED** | 120 divergences |
| MH | pinned reference's mandatory sweep disabled | **KILLED** | admitted floor falls 125 to 0 |

**M1a and M1b survive by construction, and that is the finding, not a gap.** A
correct INSIDE short-circuit is *unobservable in the output* — it changes which
tests run, and the tests it skips would all have returned INSIDE. Nothing any
fixture can assert about the admitted set can distinguish "short-circuited" from
"tested and admitted". The speed path is witnessed by the test *counts* the
gate prints (M1b would move `bob-generated` from 147.9 cluster tests per pose
back toward the flat 800), and the correctness guards are M2, M3 and M4 —
which is why those three are the ones the brief asked for and the ones that
kill.

MH is the harness proof, run before any hierarchy existed: it perturbs the
**reference**, so the comparison cannot be the shipped body being compared
against itself.

## 7. Build and identity

Same 27-variable invocation as `sprint1-stage1-link-smoke.md` and T2.2-T2.11
(pool 208), `SATURN_DIAGNOSTIC_MODE=0`, via
`tools/saturn/with-msys-toolchain.ps1` to MSYS `sh --noprofile --norc -l`,
sourcing `.yaul.env`, then `unset COMPILER_PATH`. **All tracked source was
committed before the build ran.** One build attempt, **exit 0**, ~12 minutes
of compile inside a ~35-minute wall clock (the g15 package pipeline and the
source-closure verification dominate). The g15 staleness cascade did not fire
and no repair was needed.

One tracked file was dirty during the build and is **not mine**: `STATE.md`,
edited by the concurrent desktop owner-gate session to record that
`id-b46f60d0a6d129dd` was look-and-listened and the gate closed. It is not a
sealed input, the build passed `verify-sealed-inputs`, and it was left
untouched and uncommitted for its author.

Sealed identity **`id-05046d9d5d8a5593`**
(`effective_config_sha256`
`05046d9d5d8a559357aa2089698d0870f25f5d70728bda1ab2447f51f517daba`), label
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfg05046d9d5d8a`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 9,991,332 | `bda133fc5de43baa8b4b7b3556b38c4a1be26bde35518786447a21e76b171b4e` |
| `sm64-saturn-sourceboot-e2.iso` | 5,179,392 | `1cb1220a3cb67b566bcce5b6034458db070e9955dbf1945af6d8c47a595cee4c` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,270 | `c9eb7353b9b6ba93884a13897bc33bec7454209817f1b34717b7d478ec06cbd1` |
| `identity_sha256` (target build record) | 500 | `80576b161c842fde326d8eb38c0964daf009df21d6a6085aff6bc64cfdfe8daf` |

**The tree is in the linked image, verified by symbol rather than by
assertion.** `sh-elf-nm -S`:

```
22760f7c 000006c6 t _sm64_saturn_bob_scene_admission_cluster_refs
22761644 000023dc t _sm64_saturn_bob_scene_admission_nodes
```

`0x23dc` = **9,180 bytes = 255 x 36**, at the same `.cart_rodata` address
(`0x22761644`) where T2.9 measured **36 bytes = one node**. The cluster-ref
array is unchanged at `0x6c6` = 1,734 = 867 x `uint16`. Both sit at
`0x224xxxxx`, the A-bus cartridge window - **no work RAM was spent on the
hierarchy.**

Preservation (mandatory rule): the accepted and current candidates
(`id-6b7c7e5d5f71e809`, `id-6eca5970628d581d`, `id-b46f60d0a6d129dd`,
`id-d378c3e178e5dec3` - ELF/ISO/CUE/manifest each) copied to
`releases/2026-08-16_t2_12-pre-build/` **before** the build ran; this build's
four artifacts to `releases/2026-08-16_t2_12-product/id-05046d9d5d8a5593/`.
No diagnostic build was made, so no profile-JSON `diagnostic_mode` flip was
needed and none was made: `git diff` on
`tools/saturn/profiles/sourceboot-bob-demo-v1.json` is empty and it still
reads `"diagnostic_mode":0`.

## 8. Gates

### `verify-memory-map` on the product build (verbatim)

```
verify-memory-map: checking .../e2-bob-identity-id-05046d9d5d8a5593/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FB548
  hwram_remaining = 0x4AB8 bytes (required >= 0x1F00)
  lwram_end       = 0x002E89E0
  lwram_remaining = 0x17620 bytes (floor >= 0x4000)
  RESULT          = OK
```

**RESULT OK.** True slack over the `0x1F00` floor is **11,192 B**. HWRAM fell
by **352 B** against T2.10's `0x4C18` - that is the traversal's own `.text`,
not data. LWRAM is unchanged: the descent reuses the existing queue word and
allocated no scratch.

### Host contracts

| Suite | Result |
| --- | --- |
| `verify-admission-hierarchy` (new) | **PASS** - 48,200 comparisons, 0 divergences |
| `verify-scene-admission` | **PASS** - with the new negative hierarchy cases |
| `verify-portal-windows` | **PASS** |
| `verify-ztreme-frustum` | **PASS** |
| `verify-frustum-equivalence` | **PASS** - 516,090 cases, 0 divergences, digest `0f70643abf026a13` unchanged from T2.10 |
| `verify-terrain-depth-bins` | **PASS** |
| `verify-actor-meshlets` | **PASS** - 685,456 cases, 0 divergences |
| `verify-vdp1-painter-chain` | **PASS** |
| `verify-vdp1-frame-bank` | **OK - 4 tests** |
| `verify-render-job-runtime` | **OK - 5 tests** |
| `verify-demo-render-overlap` | **PASS** |
| `verify-render-overlap-integration` | **PASS** |
| `verify-audio-loop-contracts` | **OK - 24 tests** |
| `verify-pcm68k-model` | **OK** |
| `verify-visible-position-set` | **PASS** |
| `verify-render-clusters` | **RED - pre-existing, not caused by T2.12** |

`verify-render-clusters` fails to *compile* its fixture:

```
src/port/saturn/gpl/ztreme_hot_promotion.c:3:10: fatal error:
  port/saturn/platform/saturn_cart_code.h: No such file or directory
```

The recipe passes `-I src/port/saturn/gfx` only, while commit `1ec76248`
("make A3 GPL include self-contained") gave that file a `src`-relative
include. Neither the recipe nor the file is touched by any T2.12 commit, and
its Python half - `test_render_cluster_generation.py`, 7 tests - runs first
and passes. This is the same class of latent recipe defect T2.10 section 3
flagged 22 more of. **Filed, not fixed here.**

### Validator coverage and its mutations

The five new `metadata_valid()` checks were, as first landed, exercised only
by valid trees. `verify-scene-admission` now carries a negative case for each,
plus a positive case that binds the fixture's three nodes as a hierarchy and
requires the admitted count not to move.

| # | Mutation | Result |
| --- | --- | --- |
| V1 | `child_first > index` ordering check dropped | **KILLED** |
| V2 | `child_first + child_count <= node_count` range check dropped | **SURVIVED** |
| V3 | leaf `child_first != 0` check dropped | **KILLED** |
| V4 | child-bounds-in-parent containment dropped | **KILLED** |
| V5 | forest / parent-uniqueness check dropped | **KILLED** |

**V2 cannot be killed from C and that is a property of the check, not a gap in
the test.** Removing a bounds guard makes the fixture read one node past the
array; what then rejects the case is undefined-behaviour garbage rather than
the guard, so no in-language assertion can distinguish the two. Stated rather
than smoothed over. V1 and V5 initially survived too, for a real reason worth
recording: as first written their cases were rejected by *containment* before
the check under test ran. Both were reshaped so the check under test is the
only thing that rejects them.

## 9. Cadence

From `summarize_cadence` only, per T2.11 and T2.7 section 1.1. Nothing below
is hand-computed from failure diagnostics. ymir-headless `build-agent2`
(`fcc88d82b2ea7afd...3943` - the same byte-identical binary A9A, T2.7, T2.10
and T2.11 used), BIOS `Sega Saturn BIOS (USA).bin`, absolute `--cue`,
`--startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30
--timeout 1800`, release-manifest bound. **`status: complete`, exit 0, one
attempt.** On-target identity **MATCH** (`observed_sha256 ==
expected_sha256`, `d88b17a9...23b5`), `diagnostic_mode: 0` in the target
build record.

Evidence: `sprint2-t2_12-throughput-30events.json`.

| Build | Events | Intervals | Window (VB) | **VB/frame** | **FPS mean** | Median | 1% low |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A9A archived | 10 | 9 | 102 | 11.333 | 5.294 | 5.000 | 5.000 |
| `id-6eca5970628d581d` (pre-T2.10) | 30 | 29 | 449 | 15.4828 | 3.8753 | 3.75 | 3.75 |
| `id-b46f60d0a6d129dd` (T2.10) | 30 | 29 | 403 | 13.8966 | 4.3176 | 4.2857 | 4.0 |
| **`id-05046d9d5d8a5593` (T2.12)** | 30 | 29 | **352** | **12.1379** | **4.9432** | **5.0** | **4.6154** |

- **-1.7587 VBlanks per frame, +14.5% FPS** against T2.10 on the same
  30-event basis.
- **The median is now 5.0 - equal to A9A's median** - and the **1% low is
  4.6154**, comfortably above the 4 FPS floor that blocks retention. T2.10's
  1% low sat exactly on 4.0.
- **The gap to A9A is 1.071x** (12.1379 against 11.333 VB/frame), from 1.23x
  at T2.10 and 1.37x before it. The remaining gap is **0.805 VBlanks per
  frame.**

Per-frame phase profile, 29 intervals each:

| | T2.10 `b46f60d0` | **T2.12 `05046d9d`** | Delta |
| --- | ---: | ---: | ---: |
| frame (`vblank_delta`) | 13.8966 | **12.1379** | **-1.7587** |
| construction | 7.8966 | **6.6552** | **-1.2414** |
| - master finalization | 4.8966 | 4.6552 | -0.2414 |
| - slave work overlap | 3.0000 | 2.4828 | -0.5172 |
| simulation / source tick | 6.0345 | 5.7931 | -0.2414 |
| transport + presentation | 0.0000 | 0.0345 | +0.0345 |
| attributed | 13.9310 | 12.4828 | -1.4482 |
| allowance available | 4.8966 | 4.6552 | -0.2414 |
| **allowance actually needed** | 1, in 4 of 29 | **1, in 14 of 29** | |

**The saving lands in `construction`, where `demo_spatial_admit()` lives** -
-1.2414 VB - which is exactly where T2.9 item 4 predicted **1.0-1.2 VB**. The
estimate is met at its upper end by the phase that owns the change. The
whole-frame delta is larger (-1.7587) than the construction delta; the
remaining -0.24 in `simulation` and -0.31 of unattributed movement are **not
claimed for this change** and sit inside the basis variation T2.11 section 6
already documented for this route.

**One thing for the owner that is not good news.** T2.11's concurrency
allowance was needed on 4 of 29 intervals at T2.10 and is needed on **14 of
29** here - still 1 crossing each, still well inside a 4.66-crossing
allowance, but the trend T2.11 section 7 predicted is now visible: as frames
shorten, the unstamped master/slave boundary consumes a larger fraction of the
budget. It is not blocking at 12 VB/frame. It will be at 8.

## 10. Honesty — what is wrong with these numbers

1. **The 31.6% figure is a test *count*, not a time.** Node tests and cluster
   tests are the same call with the same cartridge-resident operand shape, but
   a node test additionally applies the six-add margin, and an
   inherited-INSIDE node visit performs no test at all while still counting in
   `nodes_tested`. The count is a good proxy and it is the quantity the audit
   was written against; it is not a cycle measurement.
2. **`clusters_tested` on target was not measured by this task.** The
   view-variance evidence in section 5 is host-side, over the real generated
   cluster bank and the real baked tree, but under a synthetic pose corpus.
   Confirming it on the route needs a `SATURN_DIAGNOSTIC_MODE=2` build and a
   sub-stage capture, which was out of this task's budget. The product FPS
   figure is the one that decides whether the change was worth making, and it
   is measured.
3. **The pose corpus is not the route.** 964 poses on a grid with eight yaws
   is broader than one camera path in coverage and narrower in realism. The
   equivalence claim is strong because it is structural; the *saving* figure is
   a property of that corpus and the route may differ, which is exactly why the
   cadence measurement is quoted as the headline rather than the test-count
   ratio.
4. **The margin is derived, not proven mechanically.** Section 3.2's bound
   assumes the frustum basis vectors are unit-length Q16, which is what
   `saturn_demo_render.c` publishes but not something `admission_frustum()`
   enforces. A caller publishing a basis longer than unit scales the projection
   error with it. The gate covers the bases the corpus uses; a much longer
   basis is untested.
5. **Two mutations of six survive, by construction.** M1a and M1b are the
   INSIDE short-circuit itself, and a correct short-circuit is unobservable in
   the output. That is stated in section 6 rather than hidden; the guards that
   matter are the three that kill.
6. **The ordered emission changes a degenerate behaviour.** When the output
   capacity is exhausted, the clusters that get dropped are now the
   highest-indexed rather than the last-traversed. BOB cannot reach that path
   -- capacity is `SM64_SATURN_BOB_PRIMITIVE_COUNT` = 867 = the cluster count
   -- but a future package with a smaller output bank would drop a different
   set than it used to.
7. **`stats.clusters_tested` no longer means what it meant.** It counts
   frustum tests performed, so it is no longer comparable, as a raw number,
   with T2.9's and T2.10's 867. Consumers reading it as "scene size" are now
   wrong; nothing in tree does.
8. **This is not an owner observation.** It is a headless capture. The product
   gate is still a CUE the owner sees and hears.

## 11. What this leaves for the owner

1. **T2.12 should be kept.** 4.9432 FPS / 12.1379 VB/frame against 4.3176 /
   13.8966, visuals proven byte-identical by 48,200 pose comparisons over the
   real cluster bank, `verify-memory-map` OK with 11,192 B of HWRAM slack, and
   every standing suite green except one pre-existing red recipe.
2. **`id-05046d9d5d8a5593` is a new candidate for the owner gate.** The
   previous candidate `id-b46f60d0a6d129dd` was look-and-listened and closed
   on 2026-08-16 (per `STATE.md`, written by that session). This build is
   byte-identical in admitted content to that one, so a look-and-listen here
   is confirmation rather than adjudication - but it is the first build whose
   *median* frame rate equals A9A's.
3. **The remaining gap to A9A is 0.805 VBlanks per frame**, from 2.56 after
   T2.10. Whatever is left is no longer mostly `spatial_admit`.
4. **T2.9's ranked list still has items 5, 6 and 7 unspent** - staging the
   cluster AABBs out of the cartridge (~0.20-0.32 VB), folding `work_order`
   into admission (up to 0.554 VB), and the free `scratch_clear` tidy-up.
   Item 6 in particular is now more attractive than when T2.9 ranked it: it
   said "do only after 1-3", and 1-4 are done.
5. **Two defects filed, neither fixed here.** `verify-render-clusters`'s
   include path (section 8), and T2.11's cadence-rail margin, which this build
   spends 3.5x more of than T2.10 did (section 9).
6. **The commits.** `9fff16d0` oracle, `031a575e` runtime, `0d3450f4`
   generator, `3b4964cb` validator coverage. The measured build is
   `0d3450f4`; `3b4964cb` is test-only and touches no product code
   (`git diff --stat` shows `tools/saturn/scene_admission_test.c` and
   `CHANGELOG.md` and nothing else).

## 12. Reproduction

```
# Host gates (no build, no emulator):
#   make -f Makefile.saturn.mk verify-admission-hierarchy   <- new
#   make -f Makefile.saturn.mk verify-scene-admission verify-portal-windows #        verify-ztreme-frustum verify-frustum-equivalence
# verify-admission-hierarchy depends on compile-bob-scene, so it always
# measures the tree the baker currently emits.

# Leaf-size sweep: the gate prints it. To retune, change
# SCENE_ADMISSION_LEAF_MAX in tools/saturn/emit_bob_scene.py and re-run;
# the k_leaf table in tools/saturn/admission_hierarchy_test.c is the search.

# Mutations: applied to the working tree, run, reverted, none committed.
#   M1a  drop the `inherited == INSIDE` arm of the node test        -> SURVIVES
#   M1b  drop the `node_state == INSIDE` arm of the cluster test    -> SURVIVES
#   M2   `node_state == OUTSIDE` -> `!=` at the prune               -> KILLED
#   M3   enqueue children only from INSIDE nodes                    -> KILLED
#   M4   ADMISSION_NODE_MARGIN 8 -> 0                               -> KILLED

# Product build: the 27-variable invocation from sprint1-stage1-link-smoke.md
# with SATURN_OBJECT_POOL_CAPACITY=208 and SATURN_DIAGNOSTIC_MODE=0, via
#   tools/saturn/with-msys-toolchain.ps1 sh --noprofile --norc -l -c
#     'source ../../.yaul.env; unset COMPILER_PATH;
#      make -f Makefile.saturn.mk -j1 sourceboot <27 vars>'
# FREEZE ALL TRACKED SOURCE FIRST -- the sealed identity is derived from a
# source-hash spec frozen at build start.

# Cadence: summarize_cadence only (T2.11). Never hand-computed.
python tools/saturn/capture_sourceboot_throughput.py   --ymir <ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe>   --ipl <sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin>   --game <absolute .cue> --elf <absolute .elf>   --release-manifest <saturn-release-manifest-v1.json>   --output docs/saturn/evidence/reports/sprint2-t2_12-throughput-30events.json   --startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30   --timeout 1800
```
