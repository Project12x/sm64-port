# R2 entry — the generic actor closure: the census that actually gates props, and coins through it

- Date: 2026-08-22. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `a04e2cf7`.
- Briefed baseline `id-c7ef9bd81c762a6d`, **re-measured this session and
  confirmed to the byte**: `hwram_remaining` **8,760** (`0x2238`),
  `lwram_remaining` **79,920** (`0x13830`).
- **No emulator GUI.** All observation is headless.
- Governing principle: phase plan §2a/§2b — this port descends a generation,
  every subsystem is a degradation decision, and the question is **which
  degradation is invisible in play**.
- Scope held: no enemy-bank work, no draw path, no pose-pool changes.

---

## 0. The answer, in one paragraph

**The census the task was briefed against is not the census that gates props.**
The "13 unsupported required capabilities, twelve of them `GEO_CULLING_RADIUS`"
figure comes from `compile_actor_bank`'s **family-metadata** census, and that
census decides a *label*, not drawability. The census that decides whether a
prop can be drawn lives one layer down, in `actor_variant_bank`, and it is
published as the bundle's `unsupported_drawable` list: **33 of 48 BOB model
variants could not be compiled, and the single largest blocker was
`GEO_SHADOW` (17), not `GEO_CULLING_RADIUS` (13).** Both were already
answerable — `GEO_SHADOW` and `GEO_SCALE` had full, validated Saturn reduction
handlers sitting behind `allow_saturn_reductions`, which was hardcoded to
`(family_ordinal, model_id) == (7, 0x00BC)`: **the Bob-omb, and nothing else.**
Making that a policy rather than a one-actor exception, and giving
`GEO_CULLING_RADIUS` the same treatment, clears **all 30 of those rejections**
while leaving every one of the 15 already-shipping actor banks
**byte-identical**. On top of that, the three BOB coin variants are admitted to
the direct-textured material path with pinned capture digests, and the bundle
goes from **15 to 18 drawable variants**: red, blue and yellow coins now reach
the closure, compile to S64B v2 banks, and are published in the S64F bundle.
**Trees do not, and the reason is not `GEO_CULLING_RADIUS`: BOB's trees are
`SPECIAL_OBJECT` entries in `collision.inc.c`, and the closure collector never
reads `SPECIAL_OBJECT` at all**, so no tree record has ever existed in the BOB
closure.

---

## 1. Current capability census, established first-hand

Everything below was measured at HEAD `a04e2cf7`, not taken from the prior
report.

### 1.1 Census A — family metadata (`compile_actor_bank` / `actor_source`)

`build/saturn/packages/bob/1/actors/actor-families.json`:

| field | value |
|---|---|
| `closure_record_count` | 86 |
| `family_count` | 47 |
| `unsupported_required_capability_count` | **13** |
| `unsupported_family_representative_count` | **13** |
| `unsupported_closure_record_count` | **14** |
| `complete_closure` | `false` |

Breakdown of the 13: **12 × `UNSUPPORTED_GEO_NODE:GEO_CULLING_RADIUS`**
(seesaw platform, cartoon star/dirt, purple switch, chain-chomp gate,
bars/grills, wooden post, water bomb, breakable box, water-bomb shadow,
exclamation box, small breakable box, checkerboard platform) and
**1 × `UNSUPPORTED_GEO_NODE:GEO_BRANCH_AND_LINK`** (`bhvKoopaShellFlame`).

The brief's numbers reproduce exactly. **They are also not load-bearing**, for
two reasons established in §1.3 and §2.6.

### 1.2 Census B — drawability (`actor_variant_bank` / `compile_actor_family_bundle`)

This is the census that decides whether an actor gets an S64B bank and
therefore whether anything can draw it. Measured by compiling every model
variant of every family: **48 variants, 15 compile, 33 do not.**

| blocker | variants |
|---|---|
| `GEO_SHADOW` | **17** |
| `GEO_CULLING_RADIUS` (reported as *unknown* node — not even in the vocabulary) | **13** |
| `GEO_SCALE` | 1 |
| `GEO_ASM` | 1 |
| `textured` (material-approval whitelist, not a capability) | 1 |

Note the shape: **`GEO_SHADOW` outnumbers `GEO_CULLING_RADIUS`**, and coins,
Goomba, Koopa, star, 1-Up, heart, chain chomp, King Bob-omb, Bob-omb Buddy,
wing cap and the signpost are all in the `GEO_SHADOW` group.

### 1.3 Why census A does not gate props

Coins were **already `supported: true`** in census A with **zero** unsupported
entries, and were still undrawable. Conversely a census-A-unsupported family
is simply never *attempted* by the bundle compiler. Census A gates *attempt*;
census B gates *success*. The prior report's conclusion — "props are gated on
the generic actor closure … twelve of them `GEO_CULLING_RADIUS`, which is
exactly what trees use" — is wrong on both halves: coins were never gated by a
capability at all, and no tree record exists to be gated.

---

## 2. Disposition of every unsupported capability

### 2.1 `GEO_SHADOW` (17 variants) — **degrade, deliberately**

SM64's `GEO_SHADOW` creates a scene-graph shadow node. This port already
**recreated** shadows (phase plan §2a names it as the canonical recreation
precedent: "a generic painted sprite that does not follow light cues is *not* a
degraded SM64 shadow; it is the Saturn answer to the same problem"). Carrying
the N64 node buys nothing, because the thing it would drive does not exist.

Handled as a recorded reduction `{"node": "GEO_SHADOW", "mode":
"source-shadow-path", type/solidity/size}`. The subtree is walked normally, so
the actor's own geometry is untouched. The handler already existed and was
already argument-validated; only the gate changed.

### 2.2 `GEO_CULLING_RADIUS` (13 variants) — **degrade, deliberately**

A per-object frustum cull sphere. **Scene admission already culls at cluster
granularity** (867 clusters, 230–353 admitted per frame, T2.9/T2.12) before an
actor is ever submitted, so the per-object sphere is redundant with work that
has already happened. Treating the object as always visible and letting
admission cull is the degradation.

Two edits were needed, because the node was unknown to two separate
vocabularies:

- `actor_variant_bank._GeoCompiler`: new branch recording
  `{"node": "GEO_CULLING_RADIUS", "mode": "scene-admission-culls", radius}`.
  The radius is validated (`0 < r <= 32767`) and preserved as evidence, then
  the node is treated as a pass-through like `GEO_NODE_START`.
- `dl_rigid_groups._GEO_NEUTRAL_NODES`: added alongside `GEO_SHADOW`. Sound
  because the node carries no transform and hides no child at rest — it only
  decides whether the subtree is *considered*, which is not what a rigid-group
  walk asserts about.

**Measured cost of the degradation: zero frames and zero bytes.** It admits no
object by itself — every one of the 13 is still held out by a *material*
decision (§2.5), so nothing extra reaches the draw path from this change
alone. The claim "a per-object radius may be redundant with cluster admission"
is therefore not merely plausible here; it currently costs nothing to act on,
and the cost of the objects it would eventually admit is the per-family cost
measured in §4, not a cost of the culling decision.

### 2.3 `GEO_SCALE` (1 variant, `bhvBowlingBall`) — **already degraded; still rejects loudly**

A constant scale on a rigid subtree folds into baked vertex positions
(`mode: "static-bake"`). The bowling ball's parameters fall outside the
representable range and it now fails with `unsupported GEO_SCALE parameters`
instead of `unsupported GeoLayout node: GEO_SCALE` — a **more specific**
rejection, still closed.

### 2.4 `GEO_ASM` (3 variants: `MODEL_MIST`, `MODEL_KING_BOBOMB`, `MODEL_MARIOS_WING_CAP`) — **reject, loudly**

An arbitrary runtime callback. It can hide its subtree or return extra
geometry, so there is no static reduction that is honest. It stays in
`_GeoCompiler._UNSUPPORTED` and in `dl_rigid_groups._GEO_POISON_NODES`, and
the family is published with the reason string
`unsupported GeoLayout node: GEO_ASM`. **This is the fail-closed arm and it is
deliberately not degraded.**

### 2.5 `textured` — **not a capability; a material approval**

After §2.1–§2.2, the dominant remaining reason is
`unsupported rigid-group source: textured`, which means
`(family_ordinal, model_id) ∉ BOB_DIRECT_TEXTURED_KEYS`. That is a pinned
attestation table, not a missing feature: admission requires the key **and** a
`_EXPECTED_CAPTURE_HASHES` digest over the measured Fast3D material capture,
so a source or state change cannot gain admission by staying syntactically
valid. Coins were admitted this way (§3). Everything else was left out
deliberately — see §6.

### 2.6 `GEO_BRANCH_AND_LINK` (census A only) — **a false positive, not a capability gap**

`actor_source.analyze_actor_capabilities` scans the **whole geo source file**,
not the bound geo root. `red_flame_geo` — the root actually bound by
`bhvKoopaShellFlame` — contains no `GEO_BRANCH_AND_LINK`; its *sibling*
`red_flame_shadow_geo`, in the same file, does. `actor_variant_bank` handles
the node correctly (it is a normal branch target in `_branch_arg`), and
`MODEL_RED_FLAME` compiles cleanly once its material key is approved.

Verified across all 47 families: root-scoping the node scan would change the
verdict for **1** family (this one) and would narrow the reported node set for
7 more. It was **not** changed here — see §6 — because narrowing the node set
also narrows `capability_mask`, which is S64F ABI surface, and that blast
radius is not worth a label fix in this task.

---

## 3. Coins

`actors/coin/geo.inc.c` is `GEO_SHADOW` + `GEO_SWITCH_CASE(8,
geo_switch_anim_state)` over flat display lists. Per-frame state is one
integer; **no pose slot is involved**, exactly as `r2-pose-slot-pooling.md`
predicted.

Three closure families carry it, and all three now compile end to end:

| family ordinal | model | id | behaviour | capture digest (pinned) |
|---|---|---|---|---|
| 5 | `MODEL_RED_COIN` | `0x00D7` | `bhvRedCoin` | `698ac125a783301e…` |
| 9 | `MODEL_BLUE_COIN` | `0x0076` | `bhvBlueCoinJumping` | `809f4b989a7151d3…` |
| 45 | `MODEL_YELLOW_COIN` | `0x0074` | `bhvCoinFormationSpawn` | `896d859e2628e0c9…` |

Published bundle
`build/saturn/packages/bob/1/actors-v3-g17/bob-area1-actors-v3.s64f`:
**47 families, 18 variants, 180,584 bytes**, all 18 embedded banks parse as
S64B **v2**.

`supported_variant_count`: **15 → 18**. `unsupported_drawable_count`: 19 → 16.

---

## 4. What it costs — measured, not estimated

Bundle generation 16 (before) against generation 17 (after):

| figure | before | after | delta |
|---|---|---|---|
| CART bundle payload | 171,548 | **180,584** | **+9,036** |
| CART margin | 1,682,320 | **1,673,284** | −9,036 |
| embedded S64B bytes | 47,208 | 55,824 | +8,616 |
| VDP1 actor texture | 18,048 | **21,120** | +3,072 |
| VDP1 actor CLUT | 2,976 | **3,744** | +768 |
| VDP1 `future_repartition_margin_bytes` | 31,648 | **27,808** | −3,840 |
| workspace used / capacity | 2,915 / 3,072 | **2,915 / 3,072** | **0** |
| actor shares (output/commands/gouraud) | 2718 / 1351 / 892 | unchanged | 0 |
| service floors | 39 / 56 / 15 / 64 | unchanged | 0 |
| `guaranteed_any_mix_count` | 15 | 15 | 0 |

Per coin instance: **16 draw records, 16 texture commands, 0 Gouraud tables**,
1,024 B texture, 256 B CLUT, 384 B workspace lane (below the 1,456 B stride,
which is why workspace does not move).

The two authoritative budget constants `actor_texture_bytes` and
`actor_clut_bytes` in `inventory_actor_family_bundles.ResourceLimits` were
raised from 18,048/2,976 to **21,120/3,744**. That is affordable because the
raise is drawn from `existing_vdp1_remaining_bytes` = 52,672 B of VDP1 that no
current binder owns; after the raise, actor residency is 24,864 B and
**27,808 B remain unclaimed**.

### The four briefed figures

| figure | value | source |
|---|---|---|
| `hwram_remaining` | **8,760** (`0x2238`) baseline `id-c7ef9bd81c762a6d`; **8,664** (`0x21D8`) at current HEAD `id-c06c2183c9d74e0d` | `verify-memory-map`, run this session |
| `lwram_remaining` | **79,920** (`0x13830`) baseline; **79,888** (`0x13810`) at HEAD | `verify-memory-map`, run this session |
| cart free | **1,673,284 B bundle-level margin** after the change (was 1,682,320) | bundle resource inventory |
| cadence | **not measured for this change** | see below |

**Honest statement on the last two.** This change is entirely in the asset
pipeline; its target-visible effect is a **CART** delta
(`destination_class: CART`, `lifetime: SCENE`) of +9,036 B, and HWRAM/LWRAM are
untouched by construction — the only HWRAM-adjacent figure, the actor
workspace, is unchanged at 2,915/3,072 B. **No sourceboot ELF was built for
this change**, because publishing a new bundle requires a
`SCENE_PACKAGE_GENERATION` bump (the publication path is no-clobber by design)
and that pulls a full scene-package plus sourceboot rebuild, which fell outside
the two-hour stop rule. The briefed `hwram_remaining` 8,760 / `lwram_remaining`
79,920 / cart free 480,320 figures are therefore **baseline figures, re-verified
but not re-measured against this change**, and the cadence arm is untaken.
Whoever picks this up should bump the generation, rebuild, and re-measure all
four; the expected movement is cart-only.

---

## 5. Proof that nothing existing regressed

**All 15 pre-existing actor banks are byte-identical.** Each was recompiled
under the new reduction policy and its payload SHA-256 compared against the
generation-16 published report:

```
SAME ord=  4 0x00cd bhvExplosion                 0aaad92af894e84b
SAME ord=  6 0x00db bhvOrangeNumber              65b7e21156a2ac4b
SAME ord=  7 0x00bc bhvBobomb                    89a13d8828a7242c
SAME ord=  8 0x008f bhvSparkle                   0080bcb862250410
SAME ord= 13 0x00a3 bhvObjectWaveTrail           b36c8bd5667f55fb
SAME ord= 18 0x00a4 bhvBobombExplosionBubble     8c21b3faece317a3
SAME ord= 19 0x00c9 bhvCannonClosed              7c7dfd5dfd1852e2
SAME ord= 21 0x00a8 bhvWhitePuffExplosion        e7c3cbbd2a9bc4b7
SAME ord= 24 0x007f bhvCannonBarrel              59da404d3c74137f
SAME ord= 28 0x0084 bhvRotatingExclamationMark   4339fb1a8f097ee6
SAME ord= 29 0x0080 bhvCannon                    2c8eee36768f0a42
SAME ord= 32 0x0096 bhvBobombBullyDeathSmoke     2a3863158d3ae158
SAME ord= 39 0x00a5 bhvBubbleSplash              855ffa68141ab110
SAME ord= 40 0x0095 bhvCoinSparkles              b514be12ae1ee408
SAME ord= 46 0x00a6 bhvObjectWaterWave           8a28445ab98e1434
DIFFERING: 0
```

This is a strong result and it is not luck: an existing bank compiles today
precisely *because* it contains none of `GEO_SHADOW`, `GEO_SCALE` or
`GEO_CULLING_RADIUS`, so enabling their reductions cannot reach it.

Also unchanged: the family-metadata census is still exactly **13 / 13 / 14**
across 47 families, so `verify-actor-family-bank`'s literal assertion needed no
edit and `Makefile.saturn.mk` was **not touched**. Mario, terrain and the scene
package are untouched — no closure record, no terrain cluster and no scene
package input changed, so the admitted-cluster digests and the W2 route
timeline have nothing to compare against.

---

## 6. What was deliberately not done, and why

- **Trees.** Not blocked by `GEO_CULLING_RADIUS`. **BOB has no tree in its
  closure at all.** Its 17 trees are `SPECIAL_OBJECT(special_bubble_tree, …)`
  entries in `levels/bob/areas/1/collision.inc.c`; `collect_scene_closure`
  reads `OBJECT`, `OBJECT_WITH_ACTS`, `MACRO_OBJECT` and
  `MACRO_OBJECT_WITH_BEH_PARAM` and **never reads `SPECIAL_OBJECT`**. See §7
  for exactly what is needed.
- **Root-scoping the census-A node scan** (§2.6). Correct, but it narrows
  `capability_mask`, which is S64F ABI surface, for 8 families. Out of
  proportion to a label fix.
- **Flipping the 12 `GEO_CULLING_RADIUS` families to census-A `supported`.**
  Now that their variants compile, this is a one-line change to
  `actor_source.SUPPORTED_GEO_NODES` — but on its own it only moves them from
  "not attempted" to "attempted and rejected on material approval", while
  invalidating `verify-actor-family-bank`'s 13/13/14 literals and two oracle
  fixtures. It should land **together with** their material keys, not before.
- **Admitting the other 16 compilable variants.** The sweep shows 19 of the 33
  blocked variants compile once reductions are generic and a capture digest is
  pinned — including Koopa, Bob-omb Buddy, 1-Up, heart, both breakable boxes,
  exclamation box, wooden post, purple switch, checkerboard platform, signpost,
  red flame, transparent star, metallic ball, cartoon star and dirt. Each costs
  VDP1 residency out of the same 27,808 B remaining pool, so they should be
  admitted against a stated budget, not in bulk.

---

## 7. Precisely what is still needed

### 7.1 For a prop to actually render (blocker 3, the draw path)

The bundle now hands the runtime everything a coin needs:

1. **A family record with `FAMILY_FLAG_SUPPORTED | FAMILY_FLAG_GEOMETRY`** and
   at least one variant — `sm64_saturn_actor_family_bank_select` refuses any
   family missing either flag (`saturn_actor_bank.c:1204`). Coins now have
   both.
2. **An embedded S64B v2 bank** reachable via `embedded_bank_payloads`, with
   `bank_offset`/`bank_size`/`payload_sha256`. Present for all 18 variants.
3. **VDP1 texture + CLUT residency**, 1,024 B + 256 B per coin, already
   accounted in the published dependency manifest.
4. **A per-instance transform** from the actor instance queue, plus **one
   integer**: the `GEO_SWITCH_CASE` selector (`animInfo.animFrame`), which the
   variant bank folds into per-switch draw records. **This is the one thing the
   draw path must supply that Mario's path does not.** `demo_generic_actor_emit`
   (`saturn_demo_render.c:4234`, behind `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE`)
   already walks the instance queue and resolves bundles and textures; what it
   needs is the switch selector wired from object state to the draw-record
   choice. No pose slot, no rigid pose, no animation table.
5. **Command budget**: 16 output records and 16 texture commands per live coin,
   against 2,718 / 1,351 shares. A screenful of coins is affordable; the
   existing near-tail command-arena selection already handles oversubscription.

### 7.2 For trees to reach the closure

1. `collect_scene_closure._object_roots` must accept `SPECIAL_OBJECT`,
   `SPECIAL_OBJECT_WITH_YAW` and `SPECIAL_OBJECT_WITH_YAW_AND_PARAM` from
   `levels/{level}/areas/{area}/collision.inc.c`.
2. Preset resolution: evaluate the `enum SpecialPresets` in
   `include/special_preset_names.h` (it has explicit values and one alias —
   `special_level_geo_03 = 0x65`, `special_haunted_door = special_wooden_door`
   — so ordinal position is **not** the preset id), then look the value up by
   the `preset_id` field in `SpecialObjectPresets` in
   `include/special_presets.h`, exactly as `macro_special_objects.c:272` does.
   Verified: `special_bubble_tree` evaluates to `0x79` →
   `{MODEL_BOB_BUBBLY_TREE, bhvTree}`. Both headers and the collision file must
   join `scene_sources`/`source_hashes`.
3. `bubbly_tree_geo` is `GEO_CULLING_RADIUS(800)` + one `GEO_DISPLAY_LIST` —
   which §2.2 now handles — so once the record exists the tree is a single
   static display list with a transform.
4. **The ordinal hazard.** `BOB_DIRECT_TEXTURED_KEYS`,
   `_EXPECTED_CAPTURE_HASHES`, `_EXPECTED_CATEGORY_HASHES` and
   `_EXPECTED_TRANSFER_PREFIXES` are all keyed by **1-based family ordinal in
   the sorted 47-family report**, and `compile_actor_family_bundle` hard-fails
   unless the report has **exactly 47** families. Adding a tree family makes it
   48 and renumbers roughly half the ordinals, invalidating every pinned key at
   once. Whoever adds trees must re-derive the whole table in the same change.
   This is now flagged in a comment above `BOB_DIRECT_TEXTURED_KEYS`.

---

## 8. Tests and mutations

| test | result |
|---|---|
| `verify-actor-family-bundle` (C parser, real mutation arm) | **PASS (54 mutations)** |
| `validate_bundle` on the published g17 artifact | **PASS** — 47 families, 18 variants, 180,584 B |
| all 18 embedded banks re-parsed as S64B v2 | **PASS** |
| 15 pre-existing bank payload digests | **PASS** — 15/15 byte-identical |
| family-metadata census 13 / 13 / 14 across 47 families | **PASS** — unchanged |
| `tools.saturn.test_inventory_actor_family_bundles` | **PASS** (3 tests) after updating the pinned residency figures |
| `…test_compile_actor_family_bundle.test_real_bob_bundle_is_v2_only_complete_and_host_neutral` | **PASS** after updating the pinned figures: 15→18 variants, 19→16 unsupported drawables, 47,208→55,824 B embedded, 18,048→21,120 texture, 2,976→3,744 CLUT, 31,648→27,808 VDP1 margin, 101,832→105,872 source-ceiling records, `GEO_SHADOW` reasons 17→**0**, `GEO_CULLING_RADIUS` reasons asserted **0**, `GEO_ASM` reasons 1→3, and the three coin keys asserted present in `BOB_DIRECT_TEXTURED_KEYS` |

### Mutations exercised

1. **Make a supported capability silently no-op.** `test_inventory_actor_family_bundles`
   injects `actor_texture_bytes = 21119` and `actor_clut_bytes = 3743` (one byte
   under the true residency) and requires `texture budget exceeded` /
   `CLUT budget exceeded`. Both **fail as required**. Previously pinned at
   18,047/2,975; updated with the budget.
2. **Make the fail-closed path pass.** `GEO_ASM` remains in `_UNSUPPORTED`;
   `MODEL_MIST`, `MODEL_KING_BOBOMB` and `MODEL_MARIOS_WING_CAP` still reject
   with `unsupported GeoLayout node: GEO_ASM` **after** the reduction policy is
   generic — proving the policy is a *named* reduction set and not a blanket
   accept. `bhvBowlingBall` likewise still rejects, on
   `unsupported GEO_SCALE parameters`.
3. **Make a degraded capability claim full support.** The reductions are
   recorded, not silent: each emits a `saturn_reductions` entry naming the node
   and the mode (`source-shadow-path`, `static-bake`,
   `scene-admission-culls`) plus its arguments, and each still validates those
   arguments — a malformed `GEO_CULLING_RADIUS` or an out-of-range radius
   raises `MalformedActorSourceError`, and a non-constant `GEO_SCALE` raises
   `UnsupportedActorSourceError`. Construct `_GeoCompiler` with
   `allow_saturn_reductions=False` and every one of the 30 reverts to a hard
   rejection; that arm is what `SATURN_REDUCTIONS_ENABLED` exists to keep
   reachable.
4. **Byte-identity as a mutation detector.** The 15-bank digest comparison is
   itself the strongest arm: any behavioural change to the shared geo walk —
   node ordering, matrix composition, switch folding — would move at least one
   of the 15 digests. None moved.

### Known failures, not chased

- `tools.saturn.test_compile_actor_family_bundle`:
  `test_residency_gate_builds_and_verifies_real_bundle_from_absent_output` and
  `test_make_tool_inputs_cover_repository_local_import_closure`. Both are
  **path-form artifacts of driving GNU Make through the MSYS wrapper from this
  shell**, not content failures: the first asserts a string in `make -n`
  output, the second reports `tools/saturn/write_if_changed.py` as
  "nonexistent" when the file **is present** — because the declared input
  arrives as an MSYS `/d/Code/…` path that Windows Python resolves to
  `\d\Code\…`. The same root cause makes `verify-actor-family-bank` raise
  `FileNotFoundError: '\d\Code\…\actor-families.json'` in this shell while the
  identical assertion passes when evaluated directly in Python (verified:
  13 / 13 / 14, 47 families).
- The pre-existing failure list from the brief was not re-run and not chased.

---

## 9. Honest weak spots

- **No target build.** Everything here is host-side and artifact-level. The
  cart delta is exact, but it has not been through a linker, and the
  `--gc-sections` hazard the project has been bitten by three times does not
  apply to a CART data dependency the way it applies to code — which is a
  reason to be *less* worried, not a reason to skip the build.
- **No cadence measurement for this change.** The capture harness was reported
  down for most of this session and fixed only at the end (`e8b3c6f1`, binary
  selection: `build-agent2`, `--startup-vblanks >= 1024`). Baseline
  `id-c7ef9bd81c762a6d` is 6.9048 mean / 6.6667 median / 6.0 1%-low at tick 30
  per that fix's verification; **no post-change number exists.** Coins add 16
  output records and 16 texture commands per live instance and zero Gouraud
  tables, so the expected frame cost is small but is genuinely unmeasured.
- **The pinned capture digests are self-measured.** That is the intended
  workflow — the rejection message prints the measured capture so it can be
  approved — but it means the digests attest *stability*, not *correctness*.
  Whether a Saturn coin looks right is an owner observation, not a hash.
- **The ordinal-keyed material tables are fragile** (§7.2.4) and this change
  did not fix that, only documented it.
- **`unsupported_drawable_count` dropped 19 → 16 rather than to 0** because the
  count only covers variants of census-A-supported families; the 12
  `GEO_CULLING_RADIUS` families are still not attempted at all.
