# Sprint 2 Task T2.10 — the three generic `spatial_admit` fixes, landed and measured

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `c7c89eae`.
- Task: implement the top three generic fixes T2.9 identified in
  `demo_spatial_admit()`. Specification:
  `docs/saturn/evidence/reports/sprint2-t2_9-spatial-admit-audit.md`.
  Equivalence-oracle pattern: T2.6 (`sprint2-t2_6-meshlet-arithmetic.md` §3, §5).
  Cadence discipline: T2.7 §1.1.
- **This task changes product code.** Items 4-8 of T2.9's ranked list are out
  of scope.

## Headline

**`demo_spatial_admit()` fell from 16,588.1 to 9,871.8 FRT ticks — −40.5%,
−6,716.3 ticks, −1.914 VBlank-equivalent per frame — and not one admitted
cluster moved.** The pre-notification window fell from 21,330.3 to 14,658.6
ticks on the same instrument, same route, same emulator.

Per item, measured rather than estimated:

| Item | T2.9 estimate | **Measured** | Verdict |
| --- | ---: | ---: | --- |
| 1 — O(1) dedup (`cluster_dedup` + `mandatory`) | −1.55 VB | **−1.511 VB** (−5,301.2 ticks) | as predicted |
| 2 — memoise `metadata_valid()` (`validate`) | −0.485 VB | **−0.484 VB** (−1,697.3 ticks) | exact |
| 3 — cross-multiply the divides (`cluster_test`) | −0.55 VB | **+0.075 VB** (+264.6 ticks) | **did not pay off** |
| everything else | — | −0.004 VB | noise |
| **total** | −2.6 VB | **−1.914 VB** | |

- **Item 3 is bit-identical and slightly slower.** 516,090 classifier cases
  and 1,024 admission poses produce zero divergences against the pinned
  pre-T2.10 body, so there is **no visual-risk item for the owner to
  adjudicate** — but `cluster_test` moved from T2.9's 8,978-9,154 tick band to
  a **disjoint** 9,286-9,454 band. Removing four SH-2 hardware divisions cost
  ~39 cycles per test rather than saving ~250. Section 6 says why; section 9
  says what to do about it.
- **`dedup_compares` reads 0 in all 75 valid samples**, against T2.9's
  26,335-62,128. That is the on-target witness item 1 promised, and
  `clusters_duplicate` is still 0, so nothing was admitted that was not
  admitted before.
- **`validate` reads 2-3 ticks**, against T2.9's flat 1,700-1,701 in every one
  of 1,330 frames.
- **No product FPS figure is available for this build.**
  `capture_sourceboot_throughput.py` **aborted** — twice, at 30 and at 10
  presentation events — with
  `ObservationError: phase VBlank crossings exceed the observed interval`.
  Per T2.7 §1.1 nothing is hand-computed from those diagnostics. A control run
  of the same tool on the previous product build, in the same session on the
  same host, **completed** and reproduced T2.7's figures exactly, so the tool
  and the host are healthy and the abort belongs to this build. Section 7
  characterises it and shows the summarizer's own margin was already zero.

---

## 1. What changed, per item

Five commits on `saturn/recovery`, base `c7c89eae`:

| SHA | Subject |
| --- | --- |
| `23d16e55` | `fix(build)`: run three host gates directly instead of through native python |
| `dfe7dec5` | `test(frustum)`: pin the divided AABB classification before de-dividing it |
| `1caa2f47` | `perf(admission)`: make the duplicate test O(1) with the array already allocated |
| `1f12f2b5` | `perf(admission)`: validate package metadata once per binding, not once per frame |
| `cd121aab` | `perf(frustum)`: cross-multiply the lateral limits instead of dividing |

Source diff against base: **+347 / −46** across five files
(`saturn_scene_admission.c` +146/−46, `saturn_scene_admission.h` +15,
`ztreme_frustum.c` +168, `ztreme_frustum.h` +12, `saturn_demo_render.c` +6).
Tests and build: `frustum_cross_multiply_test.c` +666 (new),
`scene_admission_test.c` +147, `Makefile.saturn.mk` +19/−3.

### Item 1 — the duplicate test is O(1)

`output_has_cluster()` (was `saturn_scene_admission.c:131-138`, called at
`:426` and `:495`) is **deleted**, along with T2.9's diagnostic twin
`output_cluster_scan()`. Membership is now a byte in
`s_admission_cluster_seen[]`: tested at
`src/port/saturn/gfx/saturn_scene_admission.c:408-409` and `:487`, set at
`:428` and `:495`.

**One correction to T2.9's Finding D, and it is load-bearing.** That finding
says the array "is already in the scratch struct and is **already cleared**
every frame". The `memset` at what is now `:232` is real, but the three lines
after it mark the array `1` for every referenced cluster
(`saturn_scene_admission.c:233-238`), so it arrives at the traversal
**all-ones**, not clear. Item 1 therefore adds an explicit clear of
`scene->cluster_count` bytes in the admission path
(`saturn_scene_admission.c:352`) — 867 bytes for BOB, and the `scratch_clear`
bucket rose by 3.4 ticks to pay for it, against ~5,300 removed. Putting the
clear there rather than inside `metadata_valid()` is also exactly what keeps
the invariant true once item 2 stops calling that function every frame; item 1
landing first is what made item 2 safe.

The invariant is written into the source at `:131-152`:
`s_admission_cluster_seen[c]` is non-zero exactly when `c` is already in
`output->cluster_indices[0 .. cluster_count)`. It is established where
`output->cluster_count` is known to be zero and maintained at the two append
sites; every index reaching it is below `scene->cluster_count`, which
`metadata_valid()` bounds by `SM64_SATURN_SCENE_ADMISSION_MAX_REFS` — the
array's own size.

**T2.9's two safety facts, confirmed in the code rather than taken on trust.**
`output_has_cluster()` was a linear scan over a list that grows by one per
admission, so K admissions cost K(K−1)/2 comparisons — the closed form T2.9
measured exactly. And no duplicate can exist for BOB:
`metadata_valid()` proves at `:233-238` that every cluster is referenced, and
`SM64_SATURN_BOB_ADMISSION_CLUSTER_REF_COUNT == SM64_SATURN_BOB_CLUSTER_COUNT
== 867` (`bob_scene.h:10,16`) forces a bijection over the single node's ref
list. On target, `clusters_duplicate` remains 0 across all 75 valid samples,
as it was in T2.9.

### Item 2 — `metadata_valid()` runs once per binding

`sm64_saturn_scene_admit_with_scratch()` now calls it behind a memo. The key is
**every scalar and pointer the validator reads**: version, the valid byte, both
reserved fields, the new opt-in and its padding, all five array pointers, all
five counts, and the root node.

**The key I chose, and why it is sufficient — including where it is not.** It
covers every field `metadata_valid()` reads, but it cannot see *through* a
pointer. A content change behind an unchanged pointer with unchanged counts is
exactly what it would miss, and that is not a theoretical hole: the standing
fixture `tools/saturn/scene_admission_test.c:145` mutates
`nodes[0].cluster_ref_count` behind an unchanged binding and requires
rejection. So rather than serve a stale verdict, the memo is gated on a new
**opt-in**, `sm64_saturn_scene_admission_view_t::metadata_immutable`
(`saturn_scene_admission.h:23-33`), which is **fail-closed at zero** — a
caller that does not set it revalidates on every call, byte for byte as
before. `demo_spatial_admit()` sets it (`saturn_demo_render.c:882-888`)
because every array it binds is `static const` in `.cart_rodata`, which the
sourceboot link places at `0x22400000` in the A-bus cartridge window:
read-only *hardware*, not merely `const`-qualified.

Coverage of the key is **mechanical, not a promise**. A `_Static_assert`
requires `sizeof(metadata_memo_t) == offsetof(view_t, frustum)`, so a field
added to the view's prefix — the region the validator reads — fails the build
unless it is added to the key too. Verified to hold on both ABIs in play
(4-byte pointers on SH-2, 8-byte on the host test).

**Rejection is never cached.** Only successful validations are recorded, so a
malformed package is re-validated, and re-reported through `stats`, on every
attempt.

### Item 3 — cross-multiplication instead of division

`sm64_saturn_ztreme_frustum_aabb()`
(`src/port/saturn/gpl/ztreme_frustum.c:52-206`) derives its lateral limits by
multiplication when a guard holds, and falls back to the original
`scaled_limit()` division path when it does not.

The identity, stated in the source: for integer `a`, integer `N >= 0` and
integer `F > 0`,

```
a >  trunc(N/F)  <=>  a*F >  N        a <  -trunc(N/F) <=>  a*F <  -N
a <= trunc(N/F)  <=>  a*F <= N        a >= -trunc(N/F) <=>  a*F >= -N
```

`N >= 0` is what makes `trunc` equal `floor`, and it is load-bearing rather
than decorative: for `N = -5`, `F = 2`, `a = -2` the divided form says false
and the multiplied form says true. The guard is the domain on which the
identity holds **and** no intermediate leaves `int64`:

| Guard clause | What it buys |
| --- | --- |
| `focal > 0`, `half_width >= 0`, `half_height >= 0`, `near_z >= 0`, `far_z >= 0` | `N >= 0`, so `trunc == floor` |
| `near_z`, `far_z` and the four lateral sums fit `int32` | every product is at most `2^31 * 2^31 = 2^62` |
| each numerator `<= INT32_MAX * focal` | the divided form **clamps** a limit that will not fit `int32` to `INT32_MAX`, and where that clamp is active the two forms genuinely differ |

**Outside the guard the divided form runs unchanged**, which is what makes the
change bit-identical for *every* input rather than for the inputs BOB happens
to produce.

## 2. The equivalence oracle (`dfe7dec5`), landed before the change

Following T2.6: three independent statements per case, plus a set-level check.

| Statement | What it is |
| --- | --- |
| `reference` | `sm64_saturn_ztreme_frustum_aabb_reference()` — a **verbatim copy** of the pre-T2.10 divided body, compiled into `ztreme_frustum.c` only under `SM64_SATURN_ZTREME_FRUSTUM_REFERENCE`, which only the new `verify-frustum-equivalence` target defines |
| `shipped` | whatever `sm64_saturn_ztreme_frustum_aabb()` currently is |
| `model` | an exact, unclamped restatement in `__int128`, written from the algebra in `tools/saturn/frustum_cross_multiply_test.c`. It **divides** where the cross-multiplied form multiplies, in a wider type, and never clamps — so it is not a paraphrase of either implementation |

`sm64_saturn_ztreme_frustum_force_reference` routes the shipped entry point
through the pinned body, which lets the fixture drive the **real**
`sm64_saturn_scene_admit()` twice per camera pose with the classifier switched
underneath it and require the emitted cluster index arrays to be byte-equal.
That is the "admitted set, not individual comparisons" requirement.

**What the oracle deliberately does not do:** it does not model the prologue
(centre/extent/dot/support radius) independently, because T2.10 does not touch
it. The verbatim `reference` copy is what covers that half. Stated here rather
than left for a reader to discover.

Corpus: 16 frustum limit templates x 8 orientation bases x 4 camera positions,
each against 16 fixed boxes and 880 pseudo-random boxes drawn from four
magnitude bands (+-512, +-8,192, +-2^20, +-2^30). The fixed boxes cover
degenerate volumes, odd-sized edges (which exercise the `ceil` in the
half-extent), signed mirrors on every axis, boxes straddling the near and far
planes, and the `int32` rails — including two constructed so the lateral low
edge lands exactly on `INT32_MIN`, the only place where the divided form's
clamp is observable inside the rest of the domain. The templates cover the
production BOB frustum (near 128, far 8192, half 160/112, focal 256 —
`saturn_demo_render.c:134-139`), zero and negative focal lengths, zero and
negative half extents, an inverted depth range, a near plane behind the camera,
and three frustums sitting on or just past the `int32` clamp boundary.

### Result — bit-identical

```
frustum equivalence: 516090 cases, 269968 in cross-multiply domain, 246122 outside it, 6 skipped (reference int64 overflow)
frustum verdicts: outside 458235, intersects 21859, inside 35996
frustum divergences: shipped vs reference 0, shipped vs model 0
frustum cross-multiply path taken: 269968 of 269968 domain cases
admitted-set equivalence: 1024 poses, 29109 cluster admissions, 0 divergences
admitted-set digest: 0f70643abf026a13
frustum cross-multiply fixture: PASS
```

- **Zero divergences**, not "small differences". No admitted cluster moves, so
  no LOD tier and no painter bin moves. There is **no visual-risk item**.
- **`crossed_cases == domain_cases`, both 269,968.** The shipped guard and the
  domain the fixture computes independently agree in *both* directions. A
  shipped guard that narrowed would fall back to the divided form and still
  show zero divergences while delivering none of the intended change; a guard
  that widened would leave cases un-modelled. Neither can pass silently.
- **6 cases skipped**, not silently compared: those overflow `int64` inside the
  *pinned reference*, which is pre-existing undefined behaviour in the code
  under test. The fixture excludes them rather than comparing two undefined
  results and calling the agreement evidence.
- The **admitted-set digest** is an FNV-1a over every admitted index sequence
  in pose order. It is printed, not pinned: a golden constant would have to be
  re-blessed whenever the corpus moves, which is how a pinned hash stops
  meaning anything. Building the fixture against base `c7c89eae`'s admission
  unit gives `0f70643abf026a13`; against this tree it gives
  `0f70643abf026a13`. **Items 1 and 2 do not move the admitted set either.**

### Mutation proofs

Applied to the working tree, run, reverted. **None is committed.**

Oracle, at `dfe7dec5` (before item 3):

| # | Mutation | Result | Signal |
| --- | --- | --- | --- |
| MO1 | model outside-comparison inverted | **KILLED** | 1,231 model divergences |
| MO2 | whole model clamp guard dropped | **KILLED** | 17 |
| MO2b | *one* of the four clamp checks dropped | **SURVIVED** | the four are mutually redundant on this corpus; the guard is load-bearing as a unit, not clause by clause |
| MO3 | model lateral operands swapped | **KILLED** | 4,708 |
| MO4 | model near limit replaced by far | **KILLED** | 285 |
| MO5 | pinned reference perturbed (`>> 16` to `>> 15`) | **KILLED** | shipped-vs-reference, first divergence at box `[160 112 256]..[161 113 257]` |

MO5 is the important one: it perturbs the *reference*, proving the
force-reference hook and the set-level comparison are wired to something real
rather than comparing the shipped body against itself.

Item 1 (`1caa2f47`), against `verify-scene-admission`, `verify-portal-windows`
and `verify-frustum-equivalence`:

| # | Mutation | Result |
| --- | --- | --- |
| I1 | traversal never marks the emitted cluster | **KILLED** |
| I2 | mandatory sweep never marks its emission | **SURVIVED** |
| I3 | admission-path clear removed | **KILLED** |
| I4 | duplicate test inverted | **KILLED** |
| I5 | mandatory duplicate test inverted | **KILLED** |

I2 cannot be killed and that is a property of the code, not a gap in the test:
the mandatory sweep visits each index once and nothing reads the array
afterwards. The line is annotated in the source as invariant maintenance so a
reviewer does not read it as load-bearing.

Item 2 (`1f12f2b5`):

| # | Mutation | Result |
| --- | --- | --- |
| V1 | early-out on the opt-in removed | **SURVIVED** |
| V1b | opt-in removed from gate, key **and** store | **KILLED** |
| V2 | cluster pointer dropped from the key | **KILLED** |
| V3 | cluster count dropped from the key | **KILLED** |
| V4 | `metadata_valid` byte dropped from the key | **KILLED** |
| V5 | rejection cached too | **KILLED** |
| V6 | memo forced never to hit | **KILLED** |
| V7 | opt-in range check dropped | **KILLED** |

V1 survives because `metadata_immutable` is *also* a key member, so a view that
has not opted in cannot match a stored key even without the guard line. V1b
removes both and dies. The line is annotated as an early-out rather than the
safety property.

Item 3 (`cd121aab`): **eight of eight killed** — int64 overflow guard dropped,
int32 clamp guard dropped, outside-test operands swapped, inside test using the
far limit, `focal > 0` relaxed to `>= 0`, non-negative depth guard dropped,
negative half extent admitted, strict comparison relaxed to non-strict. The
corpus gained a zero-focal zero-extent frustum specifically because without it
the `focal > 0` guard was only subsumed by the clamp checks rather than
independently killable.

New coverage added to the standing gate `verify-scene-admission`: a scene where
one cluster is reachable from two nodes (neither the old fixture nor BOB itself
ever produced a duplicate, so the duplicate branch was untested), and eight
memoisation cases — the same immutable binding twice; second bindings differing
only in the cluster pointer, only in a count, and only in the `metadata_valid`
byte, each of which must be validated rather than served; a rejected binding
rejected again; an out-of-range opt-in value treated as malformed; and one case
that mutates metadata behind an immutable binding to pin the contract's
consequence, so the memo cannot be silently dead.

## 3. The Makefile defect (`23d16e55`)

T2.9 §5 recorded that `verify-scene-admission` and `verify-portal-windows` are
red at HEAD. Reproduced exactly, at `Makefile.saturn.mk:753`:

```
FileNotFoundError: [WinError 2] The system cannot find the file specified
make: *** [Makefile.saturn.mk:753: verify-scene-admission] Error 1
```

Both recipes compiled their fixture cleanly and then launched it through the
Saturn venv Python as `subprocess.run([r'$(SATURN_REPO_ROOT)/...'])`. Under the
repository's own MSYS2 GNU Make, `$(SATURN_REPO_ROOT)` derives from
`$(realpath ...)` and is an MSYS path (`/d/Code/...`) that a **native Windows**
`python.exe` cannot resolve. The fixtures were always passing; only the launch
died.

Fixed by running the built fixture directly through the recipe shell, which is
the pattern the working sibling targets in the same file already use
(`verify-terrain-depth-bins`, `verify-actor-meshlets`, `verify-terrain-clip`).
**A third target, `verify-ztreme-frustum` (`:717`), has the same defect and
covers `ztreme_frustum.c` — the file item 3 edits — so it is fixed in the same
commit.** All three now exit 0.

**Not fixed here: 22 other recipes in `Makefile.saturn.mk` still launch their
fixture through the same `python -c subprocess.run` indirection** and carry the
same latent defect. Out of scope for T2.10 and flagged for a dedicated pass.

## 4. Build and identity

Same 27-variable invocation as `sprint1-stage1-link-smoke.md` and T2.2-T2.9
(pool 208), `SATURN_DIAGNOSTIC_MODE=0`, via
`tools/saturn/with-msys-toolchain.ps1` to MSYS `sh --noprofile --norc -l`,
sourcing `.yaul.env`, then `unset COMPILER_PATH`. **All tracked source was
committed before the build ran**; `git status` showed only untracked
`.msys-home/` and `releases/`.

**One build attempt, exit 0, ~14 minutes.** The g15 package-staleness cascade
did not fire and no repair was needed.

Sealed identity **`id-b46f60d0a6d129dd`**
(`effective_config_sha256 b46f60d0a6d129dd01b41fb1b6600ed67a076e9a38fadb4382a228c822b6c686`),
label
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfgb46f60d0a6d1`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 9,979,884 | `68fbdeb05f5da3ae6f8da32b73f67c7304c37503ccec32f1221f724d256288a6` |
| `sm64-saturn-sourceboot-e2.iso` | 5,171,200 | `147c1306cc63fec51033539fe43ced6298f88571f386b2f8888c5e1e9db55c3c` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,270 | `6907a5e4d864e2671fdac9e20863955938ce5de6f394238455ee6c3cac0f6d6b` |
| `identity_sha256` (target build record) | 500 | `a45db827e57baaac9e270a4fc2d3298241f4f5be45c0c9f7e753c569f6fb113c` |

A second, diagnostic build (`SATURN_DIAGNOSTIC_MODE=2`, the only variable
different) produced **`id-84ab1b4094175582`** for section 6's measurement:
ELF `694641349063ac18e907565adcc550b0274649b0be600b031cd5ac3e451cdad7`,
ISO `07db31185af82b2992bf651a2b3f9e1bd1eee4e3dcd609cf8d97f5d5085cc610`,
manifest `7a00274016d62fef82e6b426a500391531c034a78724e77f9b56a152f8ec9352`.
`tools/saturn/profiles/sourceboot-bob-demo-v1.json` had `diagnostic_mode`
flipped 0 to 2 as an uncommitted byte-canonical edit — T2.1's precedent — and
**has been reverted**: `git diff` on that file is empty and it reads
`"diagnostic_mode":0`.

Preservation (mandatory rule): the accepted and current candidates
(`id-6b7c7e5d5f71e809`, `id-6eca5970628d581d`, `id-d378c3e178e5dec3`,
`id-6e1f4ef7ebdb275e` — ELF/ISO/CUE/manifest each) copied to
`releases/2026-08-16_t2_10-pre-build/` **before** the build ran; this build's
four artifacts to `releases/2026-08-16_t2_10-product/` and the diagnostic
build's to `releases/2026-08-16_t2_10-diag/`.

### Gate — `verify-memory-map` on the product build (verbatim)

```
verify-memory-map: checking .../e2-bob-identity-id-b46f60d0a6d129dd/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FB3E8
  hwram_remaining = 0x4C18 bytes (required >= 0x1F00)
  lwram_end       = 0x002E89E0
  lwram_remaining = 0x17620 bytes (floor >= 0x4000)
  RESULT          = OK
```

**RESULT OK.** True slack over the `0x1F00` floor is **11,544 B**
(`0x4C18 - 0x1F00`). The memo record costs 52 B (`_s_metadata_memo`,
`0x060F0B40`); no new scratch was allocated, because item 1 reuses
`s_admission_cluster_seen[]`.

### Gate — host contracts

Every standing suite named in the brief, run at HEAD after all five commits:

| Suite | Result |
| --- | --- |
| `verify-audio-loop-contracts` | **OK — 24 tests** |
| `verify-pcm68k-model` | **OK** |
| `verify-actor-meshlets` | **PASS** (invalid-span mutation caught) |
| `verify-vdp1-painter-chain` | **PASS** |
| `verify-vdp1-frame-bank` | **OK — 4 tests** |
| `verify-render-job-runtime` | **OK — 5 tests** |
| `verify-terrain-depth-bins` | **PASS** |
| `verify-demo-render-overlap` | **PASS** (two mutations caught) |
| `verify-render-overlap-integration` | **PASS** (two mutations caught) |
| `verify-scene-admission` | **PASS** — and the Make target runs now |
| `verify-portal-windows` | **PASS** — and the Make target runs now |
| `verify-ztreme-frustum` | **PASS** — and the Make target runs now |
| `verify-frustum-equivalence` | **PASS** (new) |
| `test_dual_sh2_work_storage_contract.py` | **OK** |
| `test_vdp1_staging_relocation.py` | **OK** |
| `verify-memory-map` | **RESULT OK** |

## 5. Capture

- Instrument: headless Ymir **build-agent2**
  (`ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`,
  `fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943`), BIOS
  `sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`, absolute
  `--cue`, release-manifest bound.
- Sub-stage harness: `tools/saturn/capture_prenotification_profile.py`,
  unchanged from T2.9 (record ABI v4), on the diagnostic tuple.
- Run shape: 24,000 post-BIOS frames in 300-frame chunks. **26,182 emulated
  frames, 303 s wall, 89 samples (77 with a valid record), 1,494 completed
  pre-notification windows.**
- **Route: `ROUTE_REPLAY=1`, movement positively witnessed**
  (`route_movement_observed true`).
- On-target identity **MATCH**; the target build record reports
  `diagnostic_mode: 2` for the profiled tuple.
- **Zero SH-2 exceptions** — `exception_record_clear` true in every sample.
- **`admit_windows = 1,494 = windows`**: still exactly one admission call per
  pre-notification window.
- **No desktop launch. The owner holds the observation gate.**

Acceptance, all checks pass:

```
capture_completed        true    profile_seen             true
exception_record_clear   true    profile_stable_sample    true
frt_wrap_headroom_ok     true    profile_version_ok       true
no_profiler_faults       true    profiler_stack_balanced  true
route_movement_observed  true    vdp_generations_climbing true
windows_accumulated      true    pass                     TRUE
```

`faults = 0`, `end_depth_max = 1`, `admit_max_raw = 1,675` (headroom 63,860).

## 6. Results — the sub-stage table, before and after

Mean per admission call. **T2.9** is that task's measured column over 1,330
calls; **T2.10** is this task's over 1,494 calls. Both builds carry the *same*
probe rig at the same sites, so the comparison is like for like and no
de-perturbation is applied to either side.

| Stage | T2.9 ticks | **T2.10 ticks** | Delta ticks | Delta VB-eq | % of stage now |
| --- | ---: | ---: | ---: | ---: | ---: |
| `cluster_test` | 9,087.1 | **9,351.7** | **+264.6** | **+0.0754** | 94.79% |
| `cluster_dedup` | 4,542.0 | **130.6** | **−4,411.4** | **−1.2573** | 1.32% |
| `validate` | 1,700.5 | **3.2** | **−1,697.3** | **−0.4838** | 0.03% |
| `mandatory` | 1,060.7 | **170.9** | **−889.8** | **−0.2536** | 1.73% |
| `cluster_emit` | 157.8 | 172.2 | +14.4 | +0.0041 | 1.75% |
| `scratch_clear` | 13.7 | 17.1 | +3.4 | +0.0010 | 0.17% |
| `node_test` | 11.8 | 11.7 | −0.1 | −0.0000 | 0.12% |
| `frustum_derive` | 4.6 | 4.6 | 0.0 | 0.0000 | 0.05% |
| `view_setup` | 3.6 | 3.6 | 0.0 | 0.0000 | 0.04% |
| `portal` | 0.6 | 0.6 | 0.0 | 0.0000 | 0.01% |
| **stage total** | **16,582.4** | **9,866.2** | **−6,716.2** | **−1.9142** | 100% |
| `spatial_admit` node (outside bracket) | 16,588.1 | **9,871.8** | **−6,716.3** | **−1.9143** | — |

Unattributed remainder: **0.0 ticks**. Node-tree vs span-sum ratio **0.9994**,
so the two independent brackets still agree.

The window as a whole:

| | T2.9 | **T2.10** |
| --- | ---: | ---: |
| pre-notification window, ticks | 21,330.3 | **14,658.6** |
| window VBlank-equivalent | 6.077 | **4.177** |
| `spatial_admit` share of window | 77.77% | **67.34%** |
| `work_order` (unchanged control) | 1,946.6 | 1,975.0 |
| `meshlet_depth_admit` (unchanged control) | 1,132.6 | 1,133.5 |
| `position_set` (unchanged control) | 460.3 | 463.3 |

**The unchanged controls reproduce to within 1.5%**, which is what licenses
reading the changed rows as signal.

### Route stability — the per-sample spread, 75 valid samples

| Quantity | T2.9 range | **T2.10 range** |
| --- | --- | --- |
| `clusters_tested` | 867 (min = max) | **867 (min = max)** |
| `clusters_admitted` | 230-353 | **230-353** |
| `clusters_duplicate` | 0 | **0** |
| **`dedup_compares`** | **26,335-62,128** | **0-0** |
| **`validate` ticks** | **1,700-1,701** | **2-3** |
| **`cluster_dedup` ticks** | **3,010-5,304** | **98-159** |
| **`cluster_test` ticks** | **8,978-9,154** | **9,286-9,454** |
| `admit` total ticks | ~16,000-16,600 | **9,740-10,032** |

`clusters_admitted` spans the identical 230-353 route range, which is the
on-target counterpart to the host oracle's byte-equal admitted sets.

### Item 3, honestly: it made `cluster_test` more expensive

The two `cluster_test` ranges are **disjoint** — 8,978-9,154 against
9,286-9,454 — so this is not sampling noise. Per cluster tested the stage went
from 1,341.6 to 1,380.6 measured cycles: **+39 cycles, +2.9%**.

Why the estimate inverted:

1. **T2.9's Finding E priced the SH-2 DIVU at its datasheet latency (39 cycles
   plus the DVCR read-modify-write and the call frame) and treated that as
   serial cost.** The SH-2 divider runs concurrently with the pipeline until
   the quotient is read, so a large part of that latency was already hidden.
   Finding E labelled its own saving "datasheet arithmetic, not measured in
   isolation"; this is that caveat coming true.
2. **The guard is not free.** It is roughly sixteen 64-bit comparisons — each
   several SH-2 instructions — plus one extra widening multiply for the clamp
   bound, evaluated on every test whether or not the fast path is taken.
3. **The widening-multiply intent only partly landed.** Disassembling
   `_sm64_saturn_ztreme_frustum_aabb` (`0x0607A69C`, `0x84C` bytes; it was
   `0x528` before) shows the cross-multiplied block at `0x0607AA40` onward
   contains **no `jsr` and no DIVU access** — the four `jsr _scaled_limit` that
   remain in the image belong to the fallback tail. But only **one** of the
   block's products became a single `dmuls.l`; the rest compiled to the generic
   three-instruction `mul.l`/`mul.l`/`dmulu.l` 64x64 expansion, because the
   operands arrive as `int64` values GCC does not narrow before multiplying. A
   version that kept genuine `int32` locals across the guard might still win;
   that is a follow-up, not a claim.

**What did not change:** correctness. The saving is negative, the equivalence
is exact, and section 9 records the recommendation.

## 7. Cadence — the summarizer aborted, twice

`tools/saturn/capture_sourceboot_throughput.py` on the product build
`id-b46f60d0a6d129dd`, release-manifest bound, `--startup-vblanks 4096
--max-vblanks 3600 --timeout 1800`:

| Attempt | `--presentation-events` | Result |
| --- | ---: | --- |
| 1 | 30 | **`status: failed`** — `ObservationError: phase VBlank crossings exceed the observed interval` |
| 2 | 10 | **`status: failed`** — same error |

Both runs reached their full `presentation_events_observed` count (all events
were seen); the abort is in `summarize_cadence`, not in the observation.

**Per T2.7 §1.1, no FPS figure is computed by hand from those diagnostics, and
none is quoted here.** T2.6's contaminated 1.4634 came from exactly that
mistake.

**Control, same session, same host, same tool, same parameters, on the previous
product build `id-6eca5970628d581d`: `status: complete`, exit 0.**

| | Control `id-6eca5970628d581d` (this session) | T2.7's archived figure |
| --- | ---: | ---: |
| FPS mean | **3.8753** | 3.875 |
| FPS median | **3.75** | 3.750 |
| FPS 1% low | **3.75** | 3.750 |
| intervals | 29 | 29 |
| VBlanks | 449 | 449 |
| **VBlanks/frame** | **15.4828** | 15.483 |

It reproduces T2.7 to four significant figures, so the emulator, the BIOS, the
harness and this host are all healthy. **The abort belongs to the new build.**

### Why it aborts, and why this is a tool margin rather than a defect T2.10 introduced

The guard is one line
(`tools/saturn/capture_sourceboot_throughput.py:535-540`): for each interval it
sums `simulation + construction + transport_presentation` VBlank crossings and
raises if that exceeds the interval's own `vblank_delta`.

The control run's own per-interval data says the margin was **already zero**:

| Control phase profile (29 intervals, mean) | VBlanks/frame |
| --- | ---: |
| frame (`vblank_delta`) | 15.4828 |
| construction | 9.5172 |
| — master finalization | 4.5172 |
| — slave work overlap | 2.9310 |
| simulation / source tick | 5.8276 |
| transport + presentation | 0.0000 |
| attributed | 15.3448 |
| unattributed | 0.1379 (0.89%) |
| (dropped VBlank credit — a scheduler counter, not a time phase) | 6.7586 |

**In 25 of those 29 intervals `attributed` already equals `vblank_delta`
exactly.** The check has no headroom on this build family. Two phases that
straddle the same VBlank boundary each count it, so on a frame that is one to
two VBlanks shorter — which is what section 6 predicts — a single interval tips
`attributed` one past `vblank_delta` and the entire summary is discarded.

Nothing in this task's diff touches the cadence rail: the phase counters are
incremented by the runtime scheduler, in code T2.10 does not modify. What
changed is the frame duration they are divided against.

This is the same class of issue T2.7 §1.1 recorded, and it now **blocks
measuring any build materially faster than ~15.5 VBlanks per frame**. Section 9
escalates it.

## 8. Honesty — what is wrong with these numbers

1. **There is no product FPS for this build.** The sub-stage measurement in
   section 6 is from the *diagnostic* tuple and cannot be converted into a
   frame rate: T2.9 measured the whole diagnostic rig at +0.276 VB against the
   product build, and the crossing rig and the FRT rig disagreed about that
   figure by 4.6x. A stage saving of 1.914 VBlank-equivalent is a stage saving,
   not a promise about cadence.
2. **The T2.9 to T2.10 sub-stage comparison assumes the probe cost per span is
   unchanged.** The span *sites* are identical, but `cluster_dedup` now
   brackets almost no work, so its 130.6 ticks are mostly probe. Read the
   changed rows as "collapsed to near-nothing", not as precise residuals.
3. **`cluster_test` is "cluster test **plus** loop overhead", by construction**
   (T2.9 §9.1). The +264.6 ticks charged to item 3 is an upper bound on the
   frustum test's own regression.
4. **Item 3's regression is measured on one route, one level, one camera path.**
   The *structural* claims — bit-identical classification, no divider on the
   fast path — do not depend on the route; the +39 cycles per test does.
5. **The equivalence corpus is large but finite.** 516,090 classifier cases and
   1,024 admission poses are not a proof over all inputs. The guard-plus-
   fallback structure is what makes the *argument* total; the corpus is what
   makes it credible.
6. **MO2b, I2 and V1 survive**, and each is explained above rather than quietly
   omitted. Two are provable redundancies; one is a code line no mutation can
   reach.
7. **The `_Static_assert` tying the memo key to the view prefix is an ABI
   assumption.** It holds on the two ABIs in play. A third ABI that padded the
   two structures differently would fail the build — deliberately — and the fix
   would be to revisit the key, not to delete the assertion. Recorded so a
   future reader does not delete it.
8. **`metadata_immutable` moves a safety obligation to the caller.** The memo
   cannot see through a pointer, and the fixture pins that consequence by
   mutating metadata behind an immutable binding and asserting it is still
   admitted. That test documents the contract; it does not endorse doing it.
9. **The Makefile fix is partial by choice.** 22 recipes still carry the defect.
10. **Item 3's codegen was not tuned.** Section 6 identifies a plausible
    improvement (keep `int32` locals so every product becomes `dmuls.l`) and
    explicitly does not claim it would flip the sign.
11. **Two implementation attempts were spent on the cadence capture and both
    aborted.** Under the two-attempt rule no third was made, and no figure was
    manufactured to fill the gap.

## 9. What this leaves for the owner

1. **The cadence summarizer now blocks measurement of fast builds, and this is
   the highest-priority item.** `attributed > vblank_delta` already held with
   exactly zero margin in 25 of 29 control intervals. Until it accounts for two
   phases sharing a VBlank boundary, no build materially faster than ~15.5
   VBlanks/frame can be measured by the accepted metric — which is a problem
   for exactly the work Sprint 2 is doing. **No FPS claim can be made for
   `id-b46f60d0a6d129dd` until this is resolved.**
2. **Item 3 (`cd121aab`) costs +0.075 VBlank-equivalent and should probably be
   reverted.** It is bit-identical and fully gated, so it is safe to keep, and
   it is roughly 0.5% of a frame — but it is a measured regression and it adds
   168 lines to a GPL close-port. It was left in place rather than reverted at
   the end of the session because reverting it would have left HEAD unbuilt and
   unmeasured, which is a worse evidential position than a measured HEAD with a
   known small regression. **The owner's call.** The oracle stays useful either
   way: it now pins the divided form against an independent model, so a second
   attempt at de-dividing starts from a green gate.
3. **Items 1 and 2 are unambiguous wins and should stay.** −1.995 VBlank-
   equivalent between them, both provably unable to move the admitted set, both
   matching T2.9's estimates to within 3%.
4. **T2.9's remaining items are now the whole remaining cost.** `cluster_test`
   is **94.79%** of what is left of `spatial_admit`, and it is still flat with
   visibility — 867 clusters tested to admit 230-353, every frame. That is
   T2.9's item 4 (a real hierarchy plus the INSIDE short-circuit), which the
   brief holds out of scope for its own task. After T2.10 it is not one lever
   among several; it is the only one left in this stage.
5. **22 Makefile recipes still launch their fixture through a native Python
   with an MSYS path.** Any of them can go red the moment someone runs them
   under MSYS2 Make.

## 10. Reproduction

```
# Host gates (no build required):
make -f Makefile.saturn.mk verify-frustum-equivalence
make -f Makefile.saturn.mk verify-scene-admission verify-portal-windows
make -f Makefile.saturn.mk verify-ztreme-frustum verify-actor-meshlets
make -f Makefile.saturn.mk verify-vdp1-painter-chain verify-audio-loop-contracts
make -f Makefile.saturn.mk verify-pcm68k-model verify-terrain-depth-bins
make -f Makefile.saturn.mk verify-vdp1-frame-bank verify-demo-render-overlap
make -f Makefile.saturn.mk verify-render-overlap-integration verify-render-job-runtime
python tools/saturn/test_dual_sh2_work_storage_contract.py
python tools/saturn/test_vdp1_staging_relocation.py

# Admitted-set differential for items 1 and 2 (no emulator):
#   build tools/saturn/frustum_cross_multiply_test.c twice, once against
#   `git show c7c89eae:src/port/saturn/gfx/saturn_scene_admission.c` and once
#   against the working tree, with -DSM64_SATURN_ZTREME_FRUSTUM_REFERENCE=1.
#   Both print `admitted-set digest: 0f70643abf026a13`.

# Product build: the 27-variable invocation from sprint1-stage1-link-smoke.md
# with SATURN_OBJECT_POOL_CAPACITY=208 and SATURN_DIAGNOSTIC_MODE=0, via
#   tools/saturn/with-msys-toolchain.ps1 sh --noprofile --norc -l -c
#     'source ../../.yaul.env; unset COMPILER_PATH;
#      make -f Makefile.saturn.mk -j1 sourceboot <27 vars>'
# FREEZE ALL TRACKED SOURCE FIRST -- the sealed identity is derived from a
# source-hash spec frozen at build start.
make -f Makefile.saturn.mk verify-memory-map

# Sub-stage measurement: rebuild with SATURN_DIAGNOSTIC_MODE=2 and
# tools/saturn/profiles/sourceboot-bob-demo-v1.json's diagnostic_mode
# temporarily 0 -> 2 (canonical LF, uncommitted, reverted after), then
python tools/saturn/capture_prenotification_profile.py \
  --ymir  <abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl   "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game  <abs>/build/saturn/sourceboot/e2-bob-identity-id-84ab1b4094175582/sm64-saturn-sourceboot-e2.cue \
  --elf   <abs>/.../obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest <abs>/.../saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_10-spatial-admit-fixes.json \
  --post-bios-frames 24000 --sample-interval 300

# Cadence: ABORTS on this build at both 30 and 10 presentation events.
python tools/saturn/capture_sourceboot_throughput.py \
  --ymir <...> --ipl <...> --game <...> --elf <...> --release-manifest <...> \
  --output docs/saturn/evidence/reports/sprint2-t2_10-throughput.json \
  --startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30 --timeout 1800
# The control on id-6eca5970628d581d with identical arguments completes.
```

Artifacts of record:
`docs/saturn/evidence/reports/sprint2-t2_10-spatial-admit-fixes.json` (the
sub-stage capture), `sprint2-t2_10-throughput.json` and
`sprint2-t2_10-throughput-10events.json` (both `status: failed`, retained
because a recorded abort is evidence).

Source claims can be checked directly:

```
grep -n "s_admission_cluster_seen\|metadata_memo\|_Static_assert" src/port/saturn/gfx/saturn_scene_admission.c
sed -n '19,40p'    src/port/saturn/gfx/saturn_scene_admission.h
sed -n '43,206p'   src/port/saturn/gpl/ztreme_frustum.c
sed -n '880,890p'  src/port/saturn/gfx/saturn_demo_render.c
sh-elf-objdump -d --start-address=0x0607A69C --stop-address=0x0607AEE8 <elf>
```
