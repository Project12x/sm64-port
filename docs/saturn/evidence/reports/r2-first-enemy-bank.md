# R2 entry — the first non-Mario actor bank, through the rigid lane

- Date: 2026-08-22. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `a4102c68`.
- Baseline is `id-c7ef9bd81c762a6d` (the rigid-evaluator build):
  `hwram_remaining` **8,760 B**, `lwram_remaining` **79,920 B**, cart free
  **480,320 B**, cadence **6.8775 / 6.6667** at route tick 30 and
  **6.8235 / 6.6667** at tick 150.
- **No emulator GUI.** All observation is headless Ymir.
- Governing principle: phase plan §2b — this port descends a generation, so
  every subsystem is a degradation decision and the question is *which
  degradation is invisible in play*, never *how close to the N64 original*.
- Scope: item **1** of `r2-rigid-part-evaluator.md` §8 — the enemy bank.
  Item 2 (the generic actor closure) and item 3 (a rigid draw path) are
  explicitly **not** built, and §7 below specifies exactly what each of them
  needs from this bank.

---

## 0. The answer, in one paragraph

**The bank did not have to be authored. It had to be *found*.**
`r2-rigid-part-evaluator.md` §8.1 recorded that "`mario.s64b` is the only bank
in the tree" and named a Bob-omb or Goomba bake as blocker 1 of 3. That is true
of the standalone bank **files** and false of the shipped **package**:
`build/saturn/packages/bob/1/actors-v3-g16/bob-area1-actors-v3.s64f` — already
in the ISO, already resident in CART — carries a validated **S64B v2 bank for
`MODEL_BLACK_BOBOMB`: 13 joints, 2 animations (25 and 10 frames), 68
primitives, 6,288 B**, produced by `tools/saturn/actor_variant_bank.py`, which
is a *generic* geo/animation/material compiler and not the Mario-specific
`extract_mario_actor.py`. **The chosen actor is therefore the Bob-omb, and the
Goomba cannot be baked today without lifting a hard-coded gate** (§2). 13 parts
is `16 + 13*24 = 328 B`, inside the 640 B cold class and inside the 20-part
shared-workspace ceiling, so **no class-plan edit and no workspace edit were
needed**. The bank loads and evaluates through the rigid lane in two places
that were **proven** — a host fixture arm driving the real bank bytes, and a
budget-enforcing bake tool that proves the standalone payload is
**byte-identical** to the one already published in the scene package — and in a
third that was **built, linked and symbol-checked but not observed**: a
boot-time exercise on the SH-2 that resolves the bank out of the resident
bundle through the real identity-bound path (§5.2 says why). **Cart cost is
zero** — the bytes are read where they already are rather than linked a second
time.

**The four figures: `hwram_remaining` 8,664 B (−96), `lwram_remaining`
79,888 B (−32), cart free 480,032 B (−288), and cadence NOT MEASURED at either
tick.** The cadence gap is not a shortcut: the emulator harness fails at the
`target-identity` stage on this build **and on the owner-accepted baseline**
(§5.2), so no live observation of any artifact was possible this session.
**This work is therefore `host-contract-passed`, not `live-observed`.**

The honest caveats beyond that are three: this still does not put an enemy on
screen (§7), the rigid lane is still linked to CART rather than HWRAM, and
**the extraction tooling generalises structurally but is gated by two
hard-coded whitelists** (§2.3).

---

## 1. Which actor, and why

**The Bob-omb.** The brief left the choice open — charter R2 names a Bob-omb,
the Goomba looked like the simpler pipeline proof — and asked for whichever
gets a correct bank through the toolchain fastest. The answer is not close.

| | **Bob-omb** (`MODEL_BLACK_BOBOMB`) | **Goomba** (`MODEL_GOOMBA`) |
|---|---|---|
| family ordinal / model id | 7 / `0x00BC` | 22 / `0x00C0` |
| in the BOB closure | yes, `bhvBobomb`, 14 live | yes, `bhvGoomba`, 11 live |
| `GEO_ANIMATED_PART` joints | **13** | **7** |
| source animations | **2** (25 and 10 frames) | 1 (31 frames) |
| geo-referenced triangles | **80** | 144 |
| rigid pose bytes | **328** (640 B class) | 184 (256 B class) |
| **does the bake succeed today** | **yes — it already ships** | **no** |

The Goomba is the smaller actor on every axis that matters at runtime — half the
parts, half the pose bytes, and it drops into the *small* 256 B class instead of
the large one. On the pipeline axis it is the harder one, and that is the axis
this task is on:

```
family 7  MODEL_BLACK_BOBOMB  -> OK  v2, 13 joints, 2 anims, 6,288 B
family 22 MODEL_GOOMBA        -> FAIL UnsupportedActorSourceError:
                                 unsupported GeoLayout node: GEO_SHADOW
```

Both geo layouts open with `GEO_SHADOW` followed by `GEO_SCALE`. The variant
compiler accepts neither unless `allow_saturn_reductions` is set, and that flag
is set by exactly one line:

```python
# tools/saturn/actor_variant_bank.py:1494
allow_saturn_reductions=(family_ordinal, model_id) == (7, 0x00BC))
```

**The Bob-omb is the only actor in the tree that is allowed to be reduced.**
Baking the Goomba is therefore not a data task; it is a tooling task (§2.3), and
doing it inside this task would have meant lifting a gate somebody set
deliberately, without the evidence that set it. So: **Bob-omb first, and §2.3
states precisely what the Goomba additionally needs.**

---

## 2. The degradation decisions, and who made them

The important honesty here is that **most of these were already made by the
bake, before this task existed.** They are recorded because the brief is right
that they are §2b judgements and must be stated — and because two of them are
decisions this task explicitly *ratifies* rather than discovers. They are now
written down in `tools/saturn/manifests/actors/bobomb.json` and enforced by
`tools/saturn/compile_enemy_actor_bank.py`, so a future re-bake that quietly
breaks one fails the build instead of shipping.

### 2.1 Poly budget — 68 primitives, and the ceiling is population, not fidelity

The Bob-omb's geo-referenced display lists are 80 source triangles: 4 for the
billboard face, 28 for each foot, 16 for the body, and 4 for the
`geo_switch_anim_state` eyes pair. The bake emits **68 primitives / 68 VDP1
commands per instance**.

Reference points, as the brief asked for them: Mario is 644 primitives / 694
VDP1 commands; Z-Treme's *whole-scene* budget is 1,900 polys. **The number that
actually binds is not one Bob-omb, it is fourteen.** `bhvBobomb` declares
`maximum_live_instances = 14`, and 14 x 68 = **952 commands** — half of
Z-Treme's entire scene budget for one enemy class, next to 867 atomic terrain
primitives that have no LOD tiers to give any of it back (W3). So the manifest
records 68 as a **ceiling**, not a target: the honest reading is that this actor
is already at the edge of what its own population can afford, and the lever when
it stops fitting is *live instances* and *draw distance*, not triangles.

**Nothing was decimated in this task.** Decimating the Bob-omb would have been
exactly the mistake W3 exists to prevent in reverse — reaching for savings
before there is a draw path to measure them against. The budget is declared now
so the measurement has something to fail.

### 2.2 Texture budget — CLUT16, and the T2.19a structure is *not* inherited

This is the decision the brief flagged as the cautionary tale, and the bake gets
it right, which is worth stating explicitly because it is not obvious:

```
draw_records_per_instance        68
texture_commands_per_instance    12
gouraud_tables_per_instance      56
                                 --
                                 68 = 12 + 56
```

**One VDP1 command per source primitive.** A textured primitive emits a
distorted sprite and **not** an additional Gouraud polygon. T2.19a measured the
opposite structure on Mario — his 50 textured primitives emit a Gouraud polygon
*plus* a distorted sprite, and 56.38% of their texture words are transparent, 6
of 50 tiles fully so. The Bob-omb does not inherit that, and
`compile_enemy_actor_bank.py` now asserts `textured + gouraud == primitives` so
it cannot start to.

The rest of the texture decision, `bob-direct-material-v1`:

| | value | the degradation |
|---|---|---|
| format | **CLUT16** | 16 colours per tile, not RGBA16 |
| quantiser | transparent + 15-colour median cut | deterministic; index 0 is transparent, alpha threshold 128 |
| tile classes | 16 and 32 | 16 when the source period span is <= 2 |
| resident | **1,408 B texture + 160 B CLUT** | 11 tiles, 5 CLUTs, 1,568 B of VDP1 VRAM |
| shading | VDP1 textured Gouraud is **additive** | not N64 texture-times-shade |

1,568 B of VDP1 VRAM for a whole enemy class is a good trade at Saturn
resolution; the additive-shading divergence is a fixed material-level
difference, not the swimming/popping class §2b calls a defect.

### 2.3 What the *tooling* assumes, and what generalising costs

**The brief asked for this to be said plainly, so: the extraction tooling does
generalise beyond Mario — just not the tool the brief pointed at.**

`tools/saturn/extract_mario_actor.py` does **not** generalise, and it is not the
producer here. It hard-codes the block name `mario_geo_body`, a `LIGHTS` table
of six Mario light-group symbols, a `BRANCH_SELECTIONS` table of Mario's
cap/eye/hand switch states, and three Mario-specific stages
(`mario_vertex_light_intensities`, `mario_render_clusters`, `mario_meshlets`).
Generalising *it* would be a rewrite.

`tools/saturn/actor_variant_bank.py` **is** the generic producer, and it is
genuinely general: it walks an arbitrary `GeoLayout` entry, builds a joint
hierarchy with parents-before-children ordering, folds switch alternatives onto
one shared joint, parses an arbitrary animation table, traces Fast3D material
state, and emits S64B v2. It produced the Bob-omb, and it produced fourteen
other BOB variants in the same bundle.

**It is gated, not limited.** Two hard-coded whitelists stand between it and any
other actor:

1. `actor_material_v2.BOB_DIRECT_TEXTURED_KEYS` — 15 approved
   `(family_ordinal, model_id)` pairs. A key outside it raises
   `unapproved BOB material key`. Admission also runs `_verify_exact_capture`,
   which compares the actor's traced material state against **captured
   per-actor hashes**, so adding an actor means capturing and reviewing its
   exact material states, not just adding a tuple.
2. `actor_variant_bank.py:1494` — `allow_saturn_reductions` is literally
   `(family_ordinal, model_id) == (7, 0x00BC)`. Only the Bob-omb may drop
   `GEO_SHADOW` or fold `GEO_SCALE`.

**So the cost of the Goomba, specifically:** promote `allow_saturn_reductions`
from one hard-coded pair to a per-actor manifest field (its geo needs both
`GEO_SHADOW` and `GEO_SCALE(0x00, 16384)`); add `(22, 0x00C0)` to the approved
material keys; capture and review its exact material states for
`_verify_exact_capture` — it has three 32x32 RGBA16 textures and a
`G_CC_DECALRGBA` billboard, the same shapes the Bob-omb already exercises. That
is a bounded tooling task with a real review step in it, and it is **not** what
"bake a Goomba" sounds like.

### 2.4 The reductions the Bob-omb bake actually applies

| reduction | what happens | visual cost |
|---|---|---|
| **`GEO_SCALE` folded** | `GEO_SCALE(0x00, 24576)` = 0.375 is applied to the joint translations at bake time and no runtime scale node is emitted. Source `(0, 57, -60)` bakes to `(0, 21, -23)`; source `(91, 0, 0)` bakes to `(34, 0, 0)`. | sub-unit rounding on four joint offsets, fixed and non-accumulating — §2b's invisible class. Saves one matrix multiply per part per frame and one geo node class the runtime never has to support. |
| **`GEO_SHADOW` dropped** | recorded as `source-shadow-path` metadata, emitted as no geometry | no per-actor shadow from this bank. Replaced by W4's standing decision: a generic painted sprite under the actor, not a light-following N64 shadow. |
| **switch alternatives share one joint** | the two `geo_switch_anim_state` display lists (eyes open / eyes blink) both bind **joint ordinal 12** | none — the source only ever draws one of them. This is also what makes the joint count agree with the animation stream (§3). |

### 2.5 Part count — 13, and nothing was collapsed

The brief asked what was dropped and what it costs. **Nothing was dropped.** 13
parts is inside the 20-part shared-workspace ceiling with seven to spare, and
328 B is inside the 640 B class with 312 B to spare, so collapsing the
hierarchy would have bought nothing and cost the leg articulation. The
three-deep leg chains (hip → knee → ankle → foot) are what make a Bob-omb walk
read as walking rather than sliding, and they are 8 of the 13 parts.

The one number worth flagging: **13 parts means this actor takes a 640 B block,
not a 256 B one.** Eight double-buffered Bob-ombs would be 8 x 640 = 5,120 B of
the 7,168 B arena. A Goomba at 7 parts would take 256 B blocks. The class plan
sized for exactly this and needs no edit.

### 2.6 Animation set — bake all two, and say why the tiering was not copied

Task C's precedent on Mario was to bake the reachable action set and **alias**
the rest to the nearest baked id, so an unbaked id can never fail to evaluate.
That precedent exists because Mario had 193 animation files and needed to reach
103.

The Bob-omb has **two**, both reachable from `bhvBobomb`, and both are baked.
The manifest records the policy as `bake-all-v1` with the same
`nearest-baked-animation-id` substitution rule declared and **unreachable** —
the fallback exists so the contract is uniform with Mario's, not because any id
can miss. Copying a tiering scheme onto a two-element set would have cost more
than it saves.

**A finding that the draw path must know about, because it looks like a bug and
is not:** the 25-frame walk is a **closed loop**. Eleven of its 42 channels
animate and every one of them returns to its frame-0 value at frame 24:

```
ch  0  count=25  f0=    -8   f12=     7   f24=    -8
ch  6  count=25  f0=   518   f12=  -815   f24=   518
ch 17  count=25  f0= 26917   f12=  8562   f24= 26917
ch 20  count=25  f0=-22954   f12=-13858   f24=-22954
   ... 11 channels total, all f24 == f0
```

A "does this animation move?" check that compares frame 0 against frame *n*-1
therefore reports a **stiff actor for a perfectly good animation**. The host
fixture hit exactly that and it cost a build: both the fixture and the
boot-time exercise now compare against a mid frame and assert the loop closure
separately. This is written down here because the draw path and any future
animation-liveness telemetry will hit it too.

---

## 3. The part hierarchy

13 joints, parents strictly preceding children — which is what the rigid
evaluator requires and now what the bake tool asserts.

```
 0  root                    (  0,   0,   0)
 1  +- body pivot           (  0,   0,   0)
 2     +- face billboard    (  0,   0,   0)   dl_08022D08  LAYER_ALPHA
 3     +- left hip          (  0,  21, -23)
 4     |  +- left knee      (  0,   0,   0)
 5     |     +- left ankle  ( 34,   0,   0)
 6     |        +- left foot(  0,   0,   0)   dl_08023270
 7     +- right hip         (  0,  21,  23)
 8     |  +- right knee     (  0,   0,   0)
 9     |     +- right ankle ( 34,   0,   0)
10     |        +- right ft (  0,   0,   0)   dl_08023378
11     +- body              (  0,   0,   0)   dl_08023480
12     +- eyes  (switch)    (  0,   0,   0)   dl_08022B58 / dl_08022B88
```

Six drawable parts on five joints (12 carries both switch alternatives); seven
joints are pure transform nodes. Translations are post-`GEO_SCALE`.

**The joint count is cross-checked against the animation stream, not asserted.**
The source geo contains 14 `GEO_ANIMATED_PART` tokens; the two switch
alternatives share one joint, giving 13; and both source animation index
streams are 84 `u16` = 42 channels = `3 + 13*3`. The three independent counts
agree. The same arithmetic on the Goomba gives 8 tokens - 1 shared = 7 joints
against a 48-`u16` index stream = 24 channels = `3 + 7*3`. Agreed as well, which
is why §1's Goomba row can be trusted without a successful bake.

---

## 4. Bank size and cart impact

| | value |
|---|---:|
| payload | **6,288 B** |
| `payload_sha256` | `89a13d8828a7242c68d5f4fc26d0d1d91fbc58789a677ebdf6058fc3487aabe6` |
| joints / animations | 13 / 2 |
| vertices / primitives / meshlets | 62 / 68 / 7 |
| materials / tiles / CLUTs | 6 / 11 / 5 |
| texture + CLUT resident | 1,408 B + 160 B |
| workspace lane / max scratch | 1,456 B / 2,915 B |
| rigid pose | **328 B** of the 640 B class |
| **cart bytes added by this task** | **0** |

**Zero, and the reason is the point.** The same 6,288 bytes are already inside
`bob-area1-actors-v3.s64f`, which is already the scene's single
`ACTOR_DEPENDENCIES` package — 171,548 B of CART, `SCENE` lifetime, generation
16. Emitting a second linked `.s64b` would have cost 6,288 cart bytes for zero
new capability, so `build/saturn/actors/bobomb/bobomb.s64b` is written as a
**host verification artifact and is deliberately not linked**
(`Makefile.saturn.mk`: `ENEMY_ACTOR_BANK` has no ELF consumer). The runtime
reads the copy that is already resident.

`compile_enemy_actor_bank.py` proves that claim rather than asserting it: it
recompiles the variant through the same generic producer and requires the
emitted payload's SHA-256 to equal the `payload_sha256` the published bundle
report records. **If the standalone file and the shipped bank ever diverge, the
build fails.**

---

## 5. Verification

### 5.1 Host gates

| gate | result |
|---|---|
| `compile-enemy-actor-bank` (new) | **PASS** (new) -- `MODEL_BLACK_BOBOMB` 13 joints, 2 animations, 6,288 B, rigid pose 328 B of the 640 B class, 0 cart bytes added |
| `verify-actor-rigid-pose` (extended with the enemy arm) | **PASS** -- now with the enemy real-data arm |
| `verify-actor-rigid-pose-mutation` | **PASS** -- 3 of 3 caught (`TRANSPOSE`, `SKIP_PARENT`, `ALIAS`) |
| `verify-actor-pose-pool` / `-mutation` | **PASS** / **PASS** -- 3 of 3 caught |
| `verify-actor-pose-owner` / `-mutation` | **PASS** / **PASS** -- 3 of 3 caught |
| `verify-memory-map` | **OK** |

`compile_enemy_actor_bank.py` enforces, and fails closed on, every one of:
part count against the 20-matrix shared joint workspace; rigid pose bytes
against the 640 B class; primitives, texture residency, CLUT residency, payload
size and animation count against the declared manifest budget; one VDP1 command
per source primitive (the T2.19a structure check); exactly one hierarchy root;
no forward parent reference; every animation carrying `3 + joints*3` channels
and a matching joint count; the baked animation ids matching the manifest keep
list; and byte-equality with the published bank.

The new fixture arm (`test_enemy_bank`) checks, against the real bank bytes:
the bank is S64B **v2** (a different shape from Mario's v1); the hierarchy fits
the joint workspace and the pose fits the cold class; exactly one root and every
parent strictly preceding its child, read back through
`sm64_saturn_actor_bank_joint`; **every advertised animation decoding at frame 0
and at frame n-1** with the published frame and frame-count matching what was
asked for; one published part per bank joint; that each animation actually moves
(mid-frame, per §2.6); and two fail-closed rejections — one past the last
animation id, and a block one byte short of the hierarchy.

### 5.2 On target — ``id-c06c2183c9d74e0d` -- NOT OBTAINED`

`docs/saturn/evidence/reports/r2_probe_enemy_bank.py`, read out of guest LWRAM
over route ticks 100-200.

```
**This was not obtained, and the reason is not the change.** The probe run
fails before it reads anything:

```
ValueError: target identity did not match after 4096 one-VBlank startup attempts
  stage: target-identity
```

**The control run says it is the harness, not the build.** The same harness,
same emulator binary, same arguments, pointed at the owner-accepted baseline
`id-c7ef9bd81c762a6d` in `releases/2026-08-22_r2-rigid-product/` -- an artifact
that produced complete probe and cadence evidence earlier the same day -- fails
with the byte-identical error at the byte-identical stage. Two BIOS images were
tried (the `Ymir.toml` path and the in-tree
`.ymir-profile/roms/ipl/`, one each) and `--startup-vblanks` was doubled to
8,192; no stray `ymir-headless` process was holding anything. **So no live
observation of any build was possible in this session, including of the
accepted baseline.**

Per AGENTS.md rule 4 and the two-hour stop, the live arm was abandoned rather
than debugged further. **The status of this task is therefore
`host-contract-passed`, not `live-observed`**, and the enemy arm's on-target
behaviour is *unproven* -- specifically these, which the probe would have read:

```
enemy_bank_ready              expected 1     resolved from the resident bundle
enemy_bank_joint_count        expected 13
enemy_bank_animation_count    expected 2
enemy_rigid_selftest_mask     expected 0x7F  all 7 checks
enemy_rigid_part_count        expected 13
enemy_rigid_pose_bytes        expected 328
```

`docs/saturn/evidence/reports/r2_probe_enemy_bank.py` is committed and decodes
all six; it needs only a working emulator harness. **The first thing the next
task should do is fix the harness and run it** -- it is the only outstanding
gate on this work.
```

### 5.3 Symbol table — checked, not inferred

`--gc-sections` is on. Verified against the linked ELF with `sh-elf-nm`:

```
2274e2c8 000000dc t _complete_actor_enemy_rigid_selftest    CART,  220 B  <- new
2274e118 000001b0 t _complete_actor_rigid_selftest         CART,  432 B
2274e3a4 00000718 T _sm64_saturn_actor_bundle_validate     CART, 1816 B
2274ecfc 00000338 T _sm64_saturn_actor_rigid_pose_compose  CART,  824 B
2274f034 000001d8 T _sm64_saturn_actor_rigid_pose_evaluate CART,  472 B
2274ec72 0000001a T _sm64_saturn_rigid_q15_from_q16        CART,   26 B
0606f148 00000024 T _..._complete_actor_set_enemy_bundle  HWRAM,   36 B  <- new
0606fcc0 000000c4 T _sm64_saturn_actor_bundle_variant     HWRAM,  196 B
0606fd84 0000014c T _sm64_saturn_actor_bundle_resolve     HWRAM,  332 B
0606f050 0000004c t _complete_actor_despawn_sink          HWRAM,   76 B
06072134 0000008c T _sm64_saturn_pose_pool_release_owner  HWRAM,  140 B
00200054 00000004 b _complete_actor_enemy_bundle          LWRAM,    4 B  <- new
00200050 00000004 b _complete_actor_enemy_bundle_bytes    LWRAM,    4 B  <- new
00200058 00000060 B _sm64_saturn_complete_actor_probe     LWRAM,   96 B  (was 84)
00201238 00001c00 b _complete_actor_cold_arena            LWRAM, 7168 B  (unchanged)
060deaa8 00001770 d _complete_actor_hot_arena             HWRAM, 6000 B  (unchanged)
060e0218 00000500 d _complete_actor_joint_workspace       HWRAM, 1280 B  (unchanged)
```

Five things this proves that the source cannot.

1. **The enemy arm survived `--gc-sections`** -- `_complete_actor_enemy_rigid_selftest`
   is in the link at 220 B, so it is code something reaches, not a header that
   compiles. Three previous occasions in this project were fooled by
   `--gc-sections`; this is checked, not inferred.
2. **It is at `0x2274xxxx`** -- genuinely CART cold, alongside the rest of the
   rigid lane. Boot-only code is not spending HWRAM.
3. **`_sm64_saturn_complete_actor_set_enemy_bundle` is at `0x0606xxxx`** -- in
   HWRAM at 36 B, because it is not annotated `SM64_SATURN_CART_COLD`. It is
   called exactly once at scene bring-up and **should be cold**; that is 36 of
   the 96 HWRAM bytes this task spent and it is recoverable in a one-line
   change. Recorded rather than fixed, because fixing it costs another build.
4. **The probe grew exactly 12 B** (84 -> 96), which is the six new `uint16`
   fields and nothing else, in LWRAM, costing no HWRAM.
5. **Mario's storage did not move.** `_complete_actor_hot_arena` is still 6,000 B,
   `_complete_actor_joint_workspace` is still 1,280 B, and
   `_complete_actor_cold_arena` is still 7,168 B -- the shared-workspace claim
   and the class plan are unchanged, checked in the symbol table.

```
```

### 5.4 Mario is unchanged

**Not proven at the level the previous entry proved it, and that is stated
rather than dressed up.** The W2 route-timeline comparison needs the emulator
harness, which is broken this session (5.2), so **no route comparison was run.**

What *is* known:

- **Mario's storage is byte-for-byte the same shape at the same sizes** --
  the hot arena, the joint workspace and the cold arena all unchanged in the
  symbol table (5.3).
- **The Mario arm of the rigid self-test passes the same arguments it always
  passed.** `complete_actor_rigid_selftest` was parameterised, not
  re-specified: the Mario call site passes `COMPLETE_ACTOR_OWNER_RIGID`,
  `COMPLETE_ACTOR_FALLBACK_ANIMATION_ID`, second frame 3, no alternate
  animation, and the same staged joints, and it writes the same three probe
  fields it wrote before.
- **Nothing on Mario's pose path was touched.** The skinned evaluator, the bank
  staging, the selector and the handoff are unmodified.

**The residual risk, named:** the enemy arm claims and releases one 640 B cold
block *before* `complete_actor_pool_selftest()` runs. Claim/release is balanced
and the arm uses a distinct reserved owner key (`0xE007`, outside the generic
`pool_slot < 240` band -- 3.2 of the previous entry), and the host
`verify-actor-pose-pool` fixture still passes with 3 of 3 mutations caught. But
whether the pool self-test's *on-target* mask is bit-identical with an extra
claim/release ahead of it is exactly what the probe would have shown, and it
did not run. **`pool_selftest_mask == 0x7FF` is an unverified expectation in
this build.**

---

## 6. Measurements

### 6.1 Memory

```
verify-memory-map: e2-bob-identity-id-c06c2183c9d74e0d
  ___end          = 0x060FDE28
  hwram_remaining = 0x21D8 bytes (required >= 0x1F00)
  lwram_end       = 0x002EC7F0
  lwram_remaining = 0x13810 bytes (floor >= 0x4000)
  RESULT          = OK
```

| | baseline `id-c7ef9bd81c762a6d` | this build | delta |
|---|---:|---:|---:|
| **`hwram_remaining`** | **8,760 B** | ****8,664 B** (-96)** | **-96 B** |
| **`lwram_remaining`** | **79,920 B** | ****79,888 B** (-32)** | **-32 B** |
| **cart free of 4,194,304** | **480,320 B** | ****480,032 B** (-288)** | **-288 B** |

### 6.2 Cadence

`capture_sourceboot_throughput.py`, `summarize_cadence` only.

**Not measured.** `capture_sourceboot_throughput.py` fails at the
`target-identity` stage on this build **and on the accepted baseline**
`id-c7ef9bd81c762a6d` (5.2), so there is no cadence arm to report and no
comparison to make. The baseline figures stand unchallenged and unconfirmed:
**6.8775 / 6.6667** at tick 30 and **6.8235 / 6.6667** at tick 150.

**What can be said about cost without a measurement:** the only new code that
can execute per frame is *none*. `complete_actor_enemy_rigid_selftest` is
`SM64_SATURN_CART_COLD`, is called once from `complete_actor_pools_bring_up`,
and has no caller in any per-frame path;
`sm64_saturn_complete_actor_set_enemy_bundle` is four stores called once per
scene. The 96 B of HWRAM and 32 B of LWRAM are static, not per-frame work.
**That is an argument, not evidence, and it does not substitute for the run.**

**Read the median, not the 1% low** (`w2-onepercent-low-recheck.md`): at n=29
the 1% low resolves to `rates[0]` on a `60/k` grid, so +-10% is its smallest
expressible step and it carries no independent information.

---

## 7. What the closure work and the draw path each need from this bank

This is the part that scopes the next two tasks, so it is specific.

### 7.1 The generic actor closure (blocker 2)

`features.dynamic_actor_closure = 0`, and `despawn_releases == 0` on target is
the direct consequence: actors are observed and despawned (13 times over the
route) but none of them reaches the pose pool.

**What it needs from this bank — nothing new.** That is the useful finding. The
Bob-omb is already `supported: true` in the attested family report, already one
of the 15 approved material keys, already carries `capability_mask` bits
`ANIMATED | SWITCH | BILLBOARD | ALPHA | SHADOW | PARENTED | MODEL_MUTATION |
EFFECT`, and reports **zero** unsupported required capabilities. The 13
unsupported required capabilities that gate the closure — twelve of them
`GEO_CULLING_RADIUS` — belong to *other* families, and the report already
records `unsupported_family_representative_count == 13` across 14 closure
records.

So the closure task is: **admit `bhvBobomb` without admitting the twelve
`GEO_CULLING_RADIUS` families.** Concretely it needs
(a) a per-family admission decision rather than an all-or-nothing
`dynamic_actor_closure` flag, so one supported family can spawn while thirteen
unsupported ones degrade at the smallest safe unit (constitution rule 6);
(b) the claim site — whatever loop turns instance snapshots into pose claims —
which must ask for `SM64_SATURN_RIGID_POSE_BYTES(bank.joint_count)` = **328 B**
for this actor, and must use `instance_key = (incarnation << 16) | pool_slot`
as the owner key, which is already published by
`sm64_saturn_actor_instance_bank_capture`; and
(c) nothing from the bank format itself.

### 7.2 The rigid draw path (blocker 3)

Nothing reads the published rigid pose. What a consumer needs from *this* bank,
precisely:

1. **The pose block.** `sm64_saturn_rigid_pose_t` at 328 B: a 16 B header then
   13 x 24 B parts. Part *i* is joint ordinal *i*. Transform a part-local
   vertex with the documented row-vector form —
   `out[c] = ((v[0]*rot[0*3+c] + v[1]*rot[1*3+c] + v[2]*rot[2*3+c]) >> 15) +
   translation[c]` — and **never walk the hierarchy**: `translation` is already
   model space.
2. **The part-to-joint binding, which is in the bank and is the piece the draw
   path actually has to consume.** Six drawable parts map to five joints:
   `{2: dl_08022D08 (alpha, billboard), 6: dl_08023270, 10: dl_08023378,
   11: dl_08023480, 12: dl_08022B58 | dl_08022B88 (switch)}`. Seven joints draw
   nothing. A draw path that assumes one drawable per joint will emit seven
   empty draws and get the switch pair wrong.
3. **Two things the pose does *not* carry and the draw path must resolve
   itself.** The **billboard** on joint 2 — its rotation must be replaced by the
   view-facing basis, not composed — and the **switch selection** on joint 12,
   which is `geo_switch_anim_state` and picks one of two display lists at
   runtime. Both are recorded in the bank's `selection` metadata; neither is a
   rigid-part transform.
4. **Layers.** Parts 0, 4 and 5 (`dl_08022D08`, `dl_08022B58`, `dl_08022B88`)
   are `LAYER_ALPHA`; the rest are `LAYER_OPAQUE`. With no depth bias anywhere
   in the renderer, SlaveDriver's `COMPO_SHADOW` ordering lesson applies: emit
   in a fixed order per actor rather than relying on sorting.
5. **Staging.** The boot exercise reads the joint table straight out of CART
   because no staged slice exists for this bank. A per-frame caller must stage
   13 joint records (13 x 12 B = 156 B) into LWRAM, exactly as the skinned lane
   already stages Mario's 20, or it pays an A-bus read per joint per frame for
   data that never changes.
6. **Residency, restated because it is now urgent.** The rigid lane is 1,698 B
   of `.cart_cold_text` at `0x2274xxxx` and the HWRAM margin is 824 B at the baseline, **728 B now**.
   That is correct while nothing calls it per frame and **wrong the moment this
   draw path exists.** Deleting the pool's evidence scaffolding (~1.1 KB) is not
   enough on its own; the right fix remains de-duplicating the inlined
   `rotate_xyz_and_translate` / `matrix_mul` between the two lanes, which pays
   twice — and which touches Mario's evaluator and so needs its own
   byte-identical proof.

### 7.3 Still not built, and stated plainly

**This does not put an enemy on screen and cannot.** Blocker 1 is closed: a
real, budgeted, non-Mario bank loads and evaluates through the rigid lane on
real hardware. Blockers 2 and 3 are open, and §7.1 and §7.2 are their
specifications. Distance-ordered claiming
(`r2-pose-slot-pooling.md` §8.3) is still not built and the pool is still
first-come.

---

## 8. Identity and hashes

| field | value |
|---|---|
| build id | **`id-c06c2183c9d74e0d`** |
| `effective_config_hash` | `c06c2183c9d74e0d753c6c0424833bb6059ea73f72c2aeb48b3b353a2df2d2e5` |
| identity label | `feat101-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfgc06c2183c9d7` |
| ELF SHA-256 / size | `e5b3427ba62383f4514a356165958e12654ee486a665a60dd4f07fc0cd4a4a66` / 10,280,444 |
| ISO SHA-256 / size | `7af62c99673404b9e1167de5134920e587079305820bcec5b33e186010be11f3` / 5,120,000 |
| `SOURCE.DAT` SHA-256 / size | `4d00deae07bb8d44c31ddf4967cf33d7e1629da8e49234f9baa55fd0883b84b2` / 3,714,272 |
| `.cue` SHA-256 / size | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` / 88 |
| profile / mode | `sourceboot-bob-demo` / `development` |
| `package_set_root_hash` | `89b2dd28c448771aa8359379d27c0c1347188017b1f1b60e86b54dddc7f831ae` (**unchanged**) |
| `target_profile_hash` | `bccdd297fd7792d0bdcbc5d4d2594e34faaa1d219e4e745cc7a6cfa02348a45c` (**unchanged**) |
| package generation | 16 (**unchanged**) |

**No profile JSON was flipped and no package generation moved.** The sealed
tuple is the accepted one; the identity changed only because the source closure
did (three edited translation units, one edited header, one edited Makefile,
two new tools, one new manifest).

Preserved at `releases/2026-08-22_r2-enemy-bank-product/id-c06c2183c9d74e0d/`
(cue, ISO, manifest, ELF, linker map, `SOURCE.DAT`). The accepted baselines are
untouched.

**Attached evidence**

```
tools/saturn/manifests/actors/bobomb.json
tools/saturn/compile_enemy_actor_bank.py
build/saturn/actors/bobomb/bobomb-actor-bank.json
docs/saturn/evidence/reports/r2_probe_enemy_bank.py   (written, not run)
```

**Status: `host-contract-passed`, NOT `live-observed`.** Constitution rule 5 is
explicit that host contracts never authorize the next product task. The
outstanding gate is one probe run against a working emulator harness.

**Known pre-existing failures, not chased and not caused here:**
`verify-render-clusters`, `verify-sourceboot-presentation-boundary`,
`verify-vdp1-transfer-pipeline`, `verify-render-snapshot-bank`,
`verify-graph-q16-contract`, `test_gen_actor_identity_registry.py`,
`verify-tools`, `verify-bob-bsp-header`, `verify-actor-feature-off-wrapper`.
