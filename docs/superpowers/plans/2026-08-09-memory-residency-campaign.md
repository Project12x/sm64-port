# Memory Residency Campaign: Textures + Actors in One Build

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Tasks 3 and 6 are OWNER GATES — they require the project owner, not an agent, and the plan pauses there.

**Goal:** Make the textured demo-path build link and run with `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1` and `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1`, so the owner can manually exercise the original object-holding crash scenario in a visually testable build.

**Architecture:** Close a measured 12,408-byte HWRAM deficit with one large, evidence-gated lever (object-pool residency cut, SlaveDriver/Z-Treme pattern) instead of many small scrapes; instrument first, cut to a measured number, keep a fail-closed overflow latch. A parallel fidelity-scaling lane (SeamAwareDecimater) attacks LWRAM/cart/VDP1 for the future but is NOT on this plan's critical path.

**Tech Stack:** SH-2 GCC (Yaul), GNU ld map analysis, Ymir headless (build-agent2), Python host tools under `.venv-saturn-tools`, MSYS2 login-shell builds.

---

## Measured ground truth (2026-08-09, all from real .map/emulator evidence — do not re-derive unless HEAD moves)

- Flags-on demo-path config fails the link **HWRAM-only**: `___end = 0x06101578`, hard ceiling `0x06100000`, required safe floor `0x1B00` above `___end`'s ceiling → **deficit 12,408 B** past the safe floor. LWRAM adds zero bytes (surplus stays +240 B). Evidence map: `build/saturn/sourceboot/e2-bob-identity-id-f60aaf60fe7b5d06/obj/sm64-saturn-sourceboot-e2.map`.
- Per-flag HWRAM cost +12,632 B: `complete_actor_pose_slots` (`src/port/saturn/gfx/saturn_actor_bridge.c:43-44`) = **+8,560 B** (68%); new actor-bank `.text` ≈ +4.0 KB net (not movable — only `ram` is executable).
- `gObjectPool` (`src/game/object_list_processor.c:70`, capacity 240 at `src/game/object_list_processor.h:26`, 608 B/slot via `struct Object`, `include/types.h:149-208`) = **145,920 B of always-resident HWRAM `.bss`** in every config. The port's own snapshot domain already attests at most **64 live rendered actors** (`SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE`, `src/port/saturn/gfx/saturn_actor_instance.h`). Reference engines: SlaveDriver runs 350×144 B objects with run/idle/free lists (`OBJECT.C:9-89`); Z-Treme keeps pickups as 24 B in-level records with no pool at all (`ZTE_DEF.H:187-195`).
- CAUTION that motivates Task 2: pool occupancy ≠ rendered actors. The pool also holds invisible logic objects (spawners, triggers, Mario/camera helpers) and transient particles. The 64-live bound is attested for RENDERING only. Cut to a MEASURED number, never a borrowed one.
- Working reference build for occupancy measurement: the canonical geo-walk config (`SATURN_FEATURE_*=1`, no demo flags) links fine with HWRAM surplus and runs 38k+ frames headless with zero exceptions.
- Reference-code ledger (reference-code-first rule): SlaveDriver Engine `work/upstream/slavedriver-engine` @ `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0; Sonic Z-Treme `work/upstream/sonic-z-treme` @ `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0. Close-porting with attribution into `src/port/saturn/gpl/` is established practice. SeamAwareDecimater (MIT, `github.com/songrun/SeamAwareDecimater`) not yet cloned — Task 7 records its pin.

## Environment hazards (all bit us this session — non-negotiable)

- Build via `/c/msys64/usr/bin/bash.exe -l` (LOGIN shell), source `.yaul.env` inside it, pass `OS=Windows_NT YAUL_INSTALL_ROOT=/d/Code/RetroDev/sm64-saturn-port/work/yaul-install` as make args. Never `unset COMPILER_PATH` for SH-2 cross builds.
- Headless Ymir MUST be `D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe` (the Makefile's default binary silently boots cartless). BIOS: `.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin`. Chunk `exec.run_for` ≤600 frames.
- Host Python: `.venv-saturn-tools` (its `subprocess.Popen` absolute-path defect is a known, chipped, separate issue — run make-built test binaries directly when it bites).
- Never stage the 4 permanently-dirty `.superpowers/sdd/*` files. CHANGELOG.md edits: re-read fresh, self-contained bullet, no neighbor reflow.

---

### Task 1: Documentation reconciliation (doc drift + constraint recording)

**Files:**
- Modify: `STATE.md` (current-lane section)
- Modify: `ROADMAP.md` (current-execution-lane section)
- Modify: `docs/superpowers/plans/2026-08-09-post-manual-gate-sprint.md` (Lane A preamble)
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Update STATE.md.** Replace the stale "uncommitted/current work is repairing memory/exception behavior and replacing the production recursive geo walk" narrative: the geo-walk cutover is COMPLETE (policy gate: 0 unaccounted recursive calls, guard removed as dead code, commits `5ba8d85c`..`dd81d616`), six fix commits landed 2026-08-09 (`1eb30fee`, `2c08b009`, `b1f456a5`, `a6c2032a`, `16007c4d`, `b9679f57`), and the first owner-played manual session on the textured demo build (identity `id-1335252b7f9383a6`) confirmed textures/HUD/camera-freeze-fix at stable 2-4 FPS. State the new active lane: this memory-residency campaign, with the measured 12,408 B flags-on HWRAM deficit as its driving number.
- [ ] **Step 2: Update ROADMAP.md** current-execution-lane paragraph the same way: stability lane complete; memory-residency campaign is the prerequisite gate before Lane A (Tasks 16/22) resumes; decimation/CLUT are the parallel fidelity lane feeding the later FPS sprint.
- [ ] **Step 3: Amend the sprint plan.** In `2026-08-09-post-manual-gate-sprint.md`, add a short "Discovered constraint (2026-08-09)" block to Lane A's preamble: A2/A4 target builds will not link until this campaign closes the 12,408 B deficit; cite the evidence map path above.
- [ ] **Step 4: CHANGELOG (Docs) bullet** summarizing the reconciliation, then commit all four files: `docs(saturn): reconcile STATE/ROADMAP/sprint plan with completed stability lane and measured flags-on deficit`.

### Task 2: Object-pool occupancy probe + headless measurement

**Files:**
- Modify: `src/game/object_list_processor.c` (or the real allocation site — see Step 2)
- Create: `src/port/saturn/runtime/saturn_object_pool_probe.h`
- Create: `tools/saturn/capture_object_pool_occupancy.py`
- Test: `tools/saturn/test_object_pool_probe_contract.py`
- Modify: `Makefile.saturn.mk` (verify target), `CHANGELOG.md`

- [ ] **Step 1: Write the failing contract test.** `test_object_pool_probe_contract.py`: source-text contract (same style as `test_divu_overflow_clear_contract.py`) asserting the probe struct exists with fields `magic`, `current_allocated`, `peak_allocated`, `alloc_failures`, `frames_sampled`, that the counters are updated at the real allocate AND free sites, and that the probe is `volatile` and TARGET_SATURN-gated. Run: expect FAIL (nothing exists yet).
- [ ] **Step 2: Find the real allocate/free sites.** Grep `src/game/spawn_object.c` and `src/engine/` for the functions that take objects from / return objects to the free list (decomp names are typically `try_allocate_object` / `unload_object` or `deallocate_object` — read the real code; do NOT trust these names). The counter hooks go exactly there, one increment/decrement each, plus a failure increment on the NULL/exhausted path.
- [ ] **Step 3: Implement the probe.**

```c
/* saturn_object_pool_probe.h */
#define SM64_SATURN_OBJECT_POOL_PROBE_MAGIC 0x4F504F4Cu /* 'OPOL' */
typedef struct {
    volatile uint32_t magic;
    volatile uint32_t current_allocated;
    volatile uint32_t peak_allocated;
    volatile uint32_t alloc_failures;
    volatile uint32_t frames_sampled;
} sm64_saturn_object_pool_probe_t;
extern sm64_saturn_object_pool_probe_t g_sm64_saturn_object_pool_probe;
```

Hook the counters at the Step-2 sites (peak updated on allocate; `frames_sampled` bumped once per game-loop tick from wherever the existing per-frame probe/boot-trace update runs — find the real site). Keep it always-compiled under TARGET_SATURN (it is 20 bytes; not worth a flag).
- [ ] **Step 4: Contract test passes; host suites unaffected.** Re-run Step 1's test (PASS) plus `verify-saturn-geo-walk-runtime` as a canary.
- [ ] **Step 5: Build the canonical flags-on geo-walk config** (the one that links: `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_SEMANTIC_AUDIO=0 SATURN_RENDERER_PIPELINE=4 SATURN_DIAGNOSTIC_MODE=0`, `verify-sourceboot`), resolve `g_sm64_saturn_object_pool_probe` via `sh-elf-nm`.
- [ ] **Step 6: Measure.** `capture_object_pool_occupancy.py` (pattern-copy from the existing boot-capture harnesses): BIOS handoff, then sample the probe every 300 frames to ≥20,000 post-handoff frames on the standard BOB route. Record `peak_allocated`, `alloc_failures` (must be 0 at capacity 240), and the time series. If the route never spawns the macro-object-heavy areas, note that honestly in the report — the owner gate weighs it.
- [ ] **Step 7: Evidence report** `docs/saturn/evidence/reports/memcamp-object-pool-occupancy-2026-08-XX.md` with the real numbers, then commit code+test+harness+report+CHANGELOG: `feat(saturn): instrument object pool occupancy with fail-closed probe`.

### Task 3: OWNER GATE G1 — pool capacity decision

**Task 2 execution ledger (2026-08-09):** **review-cleared for G1**. Commit
`16ff8b4b` implements the target-visible probe and commits the 21,600-frame
headless evidence report at
`docs/saturn/evidence/reports/memcamp-object-pool-occupancy-2026-08-09.md`.
The independent spec review passed; the differential/code-quality review
accepts the current capacity-240 evidence with two required follow-ons before
Task 4 reuses the harness under an override: bind reported capacity to the
sealed compiled artifact (not the source fallback `240`), and make the
source-text contract prove that each hook remains in its named allocation/free
function. Tests actually rerun by the review: 11/11 object-pool contract tests
directly and through the Make target, `py_compile` for the capture tool,
`git diff --check`, and the geo-walk runtime canary. The remaining gate is
owner G1. The 138 peak is a real idle-boot measurement with zero allocation
failures, but not target evidence for pickup/hold or action-particle pressure;
G1 must state that coverage gap and treat 138 as a floor, not a ceiling.

**Task 3 execution ledger (2026-08-09):** **owner G1 approved.** The owner
approved the recommended **208-slot** capacity by directing the campaign to
proceed. It is 1.507× the measured 138-slot peak (the required 1.5× floor),
recovers exactly `32 × 608 = 19,456 B` from the 240-slot pool, and projects
an estimated `19,456 - 12,408 = 7,048 B` HWRAM link margin for the known
flags-on deficit. This approval is deliberately limited: the 21,600-frame
capture was idle boot only, with no pickup/hold or action-particle pressure,
so 138 remains an evidence floor rather than an occupancy ceiling. Task 4
must bind its remeasurement to the sealed 208-slot artifact and return to G1
on any allocation failure.

- [x] **Step 1: Present via AskUserQuestion.** Give the owner: measured `peak_allocated`, the time series shape, and a capacity recommendation = smallest power-of-two-free number ≥ peak × 1.5, alongside the bytes recovered per candidate (e.g. capacity 96 → 87,552 B freed; 128 → 68,096 B freed; deficit to beat: 12,408 B). Options: recommended capacity / a more conservative one / abort-and-find-other-levers.
- [x] **Step 2: Record the decision** verbatim in the plan's execution ledger and carry it into Task 4. Do not proceed without it (standing owner constraint: capacity changes require explicit sign-off — precedent: the geo-arena resize).

### Task 4: Pool capacity cut + overflow latch

**Status (2026-08-09):** complete. Task 4
additionally owns the two Task 2 review hardenings: an artifact-bound capacity
reader for the remeasurement harness and function-local source-contract checks
for the three probe hooks.

**Task 4 execution ledger (2026-08-09):** G1 chose 208. The host TDD suite
observed RED before the implementation, then passed 12 object-pool contracts,
9 build-identity contracts, 7 bootstrap contracts, and 2 capture-artifact
contracts. The flags-on 208 target build passed verify-sourceboot, with sealed
identity id-2db3d6487ae1bb4d; gObjectPool is 0x1ee00 (126,464 B), exactly
19,456 B below the 240-slot 0x23a00 baseline. The artifact-bound 21,600-frame
capture recorded 20,100 post-BIOS frames, peak 138, and zero allocation
failures; see the 208-slot capacity evidence report. The override-unset
240 build (id-e49afeb0d4f053ab) and a forced explicit-240 recompilation have
the same ELF SHA-256
eb1fc628ef2cf523c59d3789645561d044463109cde41176f4348b52a7a582d7.
The idle-boot coverage gap remains: this is not pickup/hold or
action-particle target evidence. The Task 4 specification and differential
reviews passed after correcting two stale target-header comments; remaining
gate is Task 5.
Final pre-commit verification reran all 30 focused host contracts, both
Make-level contracts, the geo-walk runtime canary, and the canonical flags-on
208 target build. That target build passed verify-sourceboot as
id-999bd5f4943c0267; its sealed generated spec records capacity 208, its map
records gObjectPool at 0x1ee00 (126,464 B), and its ELF SHA-256 is
5a4315a0205b786eca06ecbca9b9b4e13b6e7c8b44f13bb5227c6e16f84602b6.
Task 4 was committed as `2b765df6` (`feat(saturn): cut object pool residency
to owner-approved measured capacity`), including PASS specification and
differential reviews at `audits/audit-20260809-task4-object-pool-spec.md` and
`audits/sm64-port_task4_differential_review_20260809.md`. Task 2's two
post-implementation reviews are also preserved in that commit; its remaining
G1 action was resolved by the recorded 208-slot owner decision above.
TDD RED was observed before implementation: the real host preprocessor kept
an override at 240; the identity/bootstrap contracts did not seal
`object_pool_capacity`; and the capture harness had no sealed-artifact
reader. Reference inspection (pattern-only, no copied code): SlaveDriver
Engine `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0, `OBJECT.C:9-89`
(fixed pool and run/idle/free lists); Sonic Z-Treme
`cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0,
`Projects/SONIC Z-TREME/ZTE/ZTE_DEF.H:187-195` (24-byte static records).

**Files:**
- Modify: `src/game/object_list_processor.h` (capacity constant)
- Modify: `src/port/saturn/sourceboot/Makefile` (flag plumbing)
- Modify: `src/game/spawn_object.c` (or real exhaustion site — latch)
- Test: extend `tools/saturn/test_object_pool_probe_contract.py`
- Modify: `CHANGELOG.md`

- [x] **Step 1: RED.** Extend the contract test: `OBJECT_POOL_CAPACITY` must honor an override macro, and the exhaustion path must both count AND latch (a `pool_exhausted_latched` field or reuse `alloc_failures != 0`) — assert the test fails before implementation.
- [x] **Step 2: Implement the override.**

```c
/* object_list_processor.h — replace the bare constant */
#ifdef SATURN_OBJECT_POOL_CAPACITY_OVERRIDE
#define OBJECT_POOL_CAPACITY SATURN_OBJECT_POOL_CAPACITY_OVERRIDE
#else
#define OBJECT_POOL_CAPACITY 240
#endif
```

Makefile: `SATURN_OBJECT_POOL_CAPACITY ?=` empty → no define (byte-identical passthrough, feature-off-rollback convention); non-empty → `-DSATURN_OBJECT_POOL_CAPACITY_OVERRIDE=$(value)`. Wire it into the build-identity typed parameters exactly like `polygon_tier` (`gen_build_identity.py` `COMPILER_CONFIG_FIELDS` + bootstrap `--set`), so identity changes when capacity does.
- [x] **Step 3: GREEN + passthrough proof.** Contract test passes. Build the canonical config once with the override unset — the sealed identity must match a pre-change build at the same HEAD (byte-identical passthrough), same discipline as the audio closure flag.
- [x] **Step 4: Cut + re-measure.** Build canonical flags-on with `SATURN_OBJECT_POOL_CAPACITY=<G1 value>`. From the fresh map: confirm `gObjectPool` shrank by exactly 608 × (240 − N) bytes. Re-run the Task 2 harness to ≥20,000 frames: `alloc_failures == 0` REQUIRED. Any failure = STOP, report, return to G1.
- [x] **Step 5: Commit** with the G1 decision, map delta, and re-measurement numbers in the CHANGELOG: `feat(saturn): cut object pool residency to owner-approved measured capacity`.

### Task 5: Flags-on textured build — link gate + combined smoke

**Task 9 supersession note (2026-08-10): source-complete pending independent
review.** The historical exact-v3 reproduction failure below remains factual,
but it no longer defines the current candidate path. Hermetic Task 9 rebuilt
the accepted flags-on tuple twice from isolated source commit `44b78627` and
produced byte-identical identity-v2 release manifests at
`b75ba5f0...a2ddf`, identity `id-9a051d30880c78f0`. Candidate B passed the
new exclusively sealed audit-v4 contract at total 700 with `_atan2_lookup` and
`_atan2s` absent, leaves 6,856 usable HWRAM bytes and 628,608 cart bytes, and
was transactionally staged without overwrite. This closes the fresh build,
reproducibility, release sealing, package, capacity, and native-math gates only.
The 20,100-frame combined smoke, visual proof, desktop launch, and owner manual
play remain open under hermetic Task 10. The prior 138-slot idle-boot peak is
still a floor without pickup/hold/action-particle coverage.

**Target-reproduction ledger (2026-08-10): BLOCKED, not source-complete.**
The Task 3 exact-target reproduction ran the complete approved flags-on tuple
through the documented MSYS login-shell fallback (the literal wrapper command
first failed before compilation because it did not source `.yaul.env`; MSYS
`/tmp` also required a workspace-local temporary directory). Its ordinary
sourceboot work reached v3, which fail-closed on a newly generated
`id-176914bc91ea6537` ELF (`352a88bafb29208019833263d63cbf4be63c235b994fd34d163392187c490ad3`)
instead of the sealed `id-735756402029c2f4` ELF
(`562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a`).
The scalar tuple in the generated spec includes the approved capacity 208, but
its effective-config digest is
`176914bc91ea65373ce961f5ca2789f501ebb374aa1678bcfa9b6032d3cc39c1`, not the
approved `735756402029c2f43b4b9792077ca7f4393de9330a37ad12914c4c9ffb68ed59`.
The historical sealed ELF remains byte-identical to its pin, while the fresh
spec fails its binding check against it. Thus no fresh package, map margin,
cart/ISO, headless, visual, or owner-manual result exists. Leave Task 5 and
Task 6 open; resolve the identity drift with a reviewed contract/design change
rather than repinning v3 or promoting stale output.

**Combined-smoke harness ledger (2026-08-10):** Task 2's capture extension is
**complete for host scope; independent review cleared.** Its TDD RED
observed the requested absent decoder/acceptance entry points, then the
focused 3-test GREEN and prescribed 6-test capture suite passed after the
review repair. The harness
now binds every paused sample to one ELF's DLL-safe symbol listing and P2
cache-through reads for signed `sAreaYaw`, cart completion, exception magic,
boot telemetry, and stable cadence telemetry, reporting a fail-closed
nonvisual acceptance summary. It derives replay/live route information from
the sealed identity instead of assuming disabled movement, and proves the
target code plus P2-loaded build identity before post-BIOS sampling. The
independent review's artifact-binding finding is repaired and cleared; no
Critical, Important, or Minor findings remain. This is not target evidence:
the exact-artifact >=20,000 post-handoff smoke, visual proof, and owner
acceptance remain open.

**Status (2026-08-09):** active. The exact historical demo-path tuple was
recovered from the sealed id-1335252b7f9383a6 ELF and its matching build
directory; Task 5 changes only the three feature bits and the approved object
pool capacity as specified below.

**Audit correction (2026-08-09):** the flags-on link produced
`id-735756402029c2f4`, but the first full native-math audit exposed stale
callback-owner declarations: the renderer's feature-gated actor wrappers,
unreachable cutscene tables, and callback paths introduced by Task 14's
geo-walk. Source tracing corrected the ownership rule: each reached GeoLayout
callback belongs to both its `init_graph_node_*` caller at `GEO_CONTEXT_CREATE`
and its `saturn_geo_enter_*` caller at `GEO_CONTEXT_RENDER`; the sourceboot
walker separately owns its three static runtime-ops callbacks. The verifier's
legacy bootstrap scan also under-approximated literal JSR candidates, so
code-only analysis now seeds both ends of every declared indirect edge before
disassembly.

The source-derived oracle test and 11 focused declaration/negative tests pass
with the 25 newly derived callback edges. The fresh direct audit of the sealed
flags-on ELF ran for 447.1 s: it reports `total 700` with no unlisted
unresolved transfer or unresolved-effect failure, and fails only because the
fixed-v2 total remains `582`. Configuration comparison establishes that v2
pins a narrower historical baseline, whereas the goal artifact enables replay
and live input, demo-path rendering, camera variant 3, hot promotion, tier 2,
8-sector staging, and capacity 208. The 2026-07-29 procedure deliberately
permits one historical v2 re-pin; it is not a safe per-configuration total
edit. The selected resolution is a new, versioned, identity-bound goal-target
audit contract. No map margin, ISO-size, smoke, capture, or target-evidence
claim has been made.

**Owner decisions (2026-08-09):** approved the audit-contract reconciliation
with the linked BOB route as the authority for dynamic dispatcher inclusion,
then approved the exact-identity v3 design. V2 stays frozen. V3 binds total 700
to ELF SHA-256
`562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a`
and embedded identity `id-735756402029c2f4`; the detailed design is
`docs/superpowers/specs/2026-08-09-goal-target-native-math-audit-v3-design.md`.

**Task 1 audit-contract ledger (2026-08-10):** complete for the exact-artifact
audit scope. The test-first v3 contract change preserves v2 byte-for-byte and
adds a fail-closed SHA-256 binding before any SH tool invocation. Focused
v3/source-derived tests: 13 pass. The full verifier suite is explicitly
**open**, with 228/229 passing and the pre-existing unrelated pinned BOB
null-camera-trigger proof failure still present. The direct DLL-safe audit of
the sealed `id-735756402029c2f4` ELF completed in 391.7 s with exit 0 and
audit total 700; it reported no unlisted unresolved transfer/effect and no
forbidden `_atan2_lookup`/`_atan2s` caller. This is only the native-math audit
gate: Task 5's build/package, combined smoke, visual inspection, and owner
manual acceptance remain unchecked. Commit:
`fix(saturn): bind goal native math audit to sealed target` (Task 1 commit).
Independent specification/code-quality review of `418161fa..373c2640` cleared
the parser, digest, exact-ELF binding, fail-fast, v2-preservation,
source-derived oracle, and documentation requirements with no Critical issue.
Its one Important documentation-gate finding was the absent recorded verdict;
this entry records the clearing verdict. The unrelated 228/229 host-suite
result remains open and is not claimed green.

**Files:** runtime sources remain unmodified. The in-progress audit
reconciliation modifies `tools/saturn/verify_sh2_native_math.py`, its two route
oracles, and `tools/saturn/test_verify_sh2_native_math.py`; any accepted
resolution must include its tests, this ledger, and `CHANGELOG.md` in the same
commit. Task evidence report + CHANGELOG only for the actual build result.

- [ ] **Step 1: Build the goal config**: the exact 18-flag demo-path tuple from the `id-1335252b7f9383a6` build but with `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1`, plus `SATURN_OBJECT_POOL_CAPACITY=<G1 value>`. Link must pass both region asserts; record real margins from the map (expect roughly: old deficit −12,408 B + pool recovery ≥ +55 KB net HWRAM surplus).
- [ ] **Step 2: ISO completeness** (SOURCE.DAT in ISO9660 listing, size == `.cart_rodata` SIZEOF).
- [ ] **Step 3: Combined headless smoke** (≥20,000 post-handoff frames): cart gate, zero exceptions, VDP generations climbing, `sAreaYaw` non-freeze through 9,500–10,000 (fresh `sh-elf-nm` address), pool probe `alloc_failures == 0`, HUD at top, textured terrain, **and actors visibly present** — screenshot must show at least one spawned actor (coin/enemy) that the flags-off build lacked. If actors don't render because Task 16's drain stubs still quarantine them, capture what the probe says is *spawned* vs *rendered* and report honestly — spawned-but-invisible still unblocks the pickup test only if interaction works; flag for the owner either way.
- [ ] **Step 4: Evidence report + CHANGELOG commit**: `docs(saturn): flags-on textured build links and smokes with pool residency cut`.

### Task 6: OWNER GATE G2 — manual acceptance (the campaign's actual goal)

- [ ] **Step 1: Launch** desktop Ymir (`build-agent2` `ymir-sdl3.exe -p <repo>\.ymir-profile -d <Task 5 CUE>`).
- [ ] **Step 2: Owner checklist:** (a) **pick up / hold an object — THE original crash scenario, in its home config family**; (b) hold camera rotation for a sustained stretch (direct DVCR-fix confirmation); (c) textures + HUD-at-top sanity; (d) several minutes of stability; (e) rough FPS impression vs the 2-4 flags-off baseline (animation/actors will cost — measured, not blocking, per the governing plan's regression policy).
- [ ] **Step 3: Record the verdict** in the sprint plan's manual-gate section and STATE.md. PASS here closes the campaign's critical path and re-opens Lane A/Lane B execution.

### Task 7 (parallel lane, start any time after Task 1): SeamAwareDecimater offline prototype

**Files:**
- Create: `work/upstream/seam-aware-decimater/` (pinned clone — record SHA + MIT in the ledger)
- Create: `tools/saturn/mesh_ir_obj_shim.py` (IR→OBJ→IR round-trip)
- Test: `tools/saturn/test_mesh_ir_obj_shim.py`
- NO build-system integration in this task. NO identity wiring. Offline only.

- [ ] **Step 1: Clone + pin + build.** Clone SeamAwareDecimater into `work/upstream/`, pin libigl/Eigen to era-appropriate commits, build under MSYS2 MinGW — prefer a small hand-rolled Makefile in the fork over introducing CMake (repo has zero CMake today). Record binary SHA-256.
- [ ] **Step 2: RED then implement the shim.** Round-trip test first: IR→OBJ→IR with decimation disabled must be byte-identical (positions re-quantized deterministically, `texture_tile` state reattached by material, UVs 1:1 by construction). Then run real decimation at 50% and 25% (`--strict 2`), and prove GREEN-twice determinism of the decimated outputs (two runs, byte-identical) — a nondeterministic decimater is disqualifying (identity-seal precedent: the `__pycache__` drift incident).
- [ ] **Step 3: Run the existing downstream tools offline** (`saturn_mesh_ir.py`, `compile_bob_bsp.py`) on the decimated IRs; capture REAL post-pairing primitive counts, BSP node counts, and the regenerated LWRAM array bounds vs today's 867 primitives / 1,183 nodes / ~447 KB scaled LWRAM.
- [ ] **Step 4: Visual artifact for OWNER GATE G3.** Render or capture the decimated terrain (host-side viewer or an offline Ymir capture from a scratch build if cheap) at both levels; package screenshots + the Step 3 numbers into an evidence report. The owner decides acceptable fidelity level — UI/visual quality is an owner-eyes gate by project convention.
- [ ] **Step 5: Commit** shim + tests + report (upstream clone stays untracked per `work/upstream/` convention): `feat(saturn): offline seam-aware terrain decimation prototype with real downstream counts`.

### Task 8 (deferred — separate plan required): identity-wired decimation build stage

Explicitly OUT of this plan's scope. Prerequisites before planning it: G3 fidelity verdict, and the **owner routing decision** (demo/BSP Route A vs geo-walk Route B as the surviving renderer — Route B needs a new IR→Vtx/Gfx emitter, roughly a third of the integration cost; Route A gets decimation nearly free at the IR boundary). The five-point identity wiring recipe (config fields, generated inputs, tool provenance JSON, `identity-assets` ordering, bootstrap-test extension) is recorded in the 2026-08-09 decimation scoping report — carry it into that plan verbatim. Also carry: the CLUT plan's `bob_sky_*` rename will hard-fail the identity seal unless `GENERATED_IMAGE_INPUTS` and its bootstrap test are updated in the same commit.

---

## Follow-on backlog (mined, ledgered, NOT in this campaign)

**Task 10 exact-smoke correction (2026-08-11): blocked.** The flags-on target
links and loads, and its measured object-pool peak remains 138/208 with zero
allocation failures, but it does not satisfy this campaign's run/play goal.
Live queue evidence shows the intentional feature-on Task 16 ACTOR_ADMIT stub
failing generation 1 and quarantining ACTOR_LOWER, after which source ticks
remain at 2. Therefore Steps 5–6 remain unchecked; no owner pickup/hold test is
authorized. Complete the Task 16 production generic actor drain, then reopen
hermetic Task 9 sealing before retrying this campaign gate.

| Lever | Source | Benefit class | Reuse mode |
|---|---|---|---|
| Per-Object fat trim (64 B resident `Mat4`, 320 B worst-case `rawData`) | Both engines recompute transforms transiently | HWRAM, scales with capacity | behavior-lesson |
| Two-region LIFO arena allocator with spill + lock | SlaveDriver `UTIL.C:336-405` | structural — ends per-flag link wars | close-port (~60 lines) |
| Animation working-set cap (12 B cursors, decode-in-place from memory-mapped cart) | Z-Treme `ZT_ANIMATION.c:5-52` + our cart mapping | HWRAM (animation flag cost) | pattern-only |
| Run/idle/free intrusive object lists + render-visibility wake | SlaveDriver `OBJECT.C:20-89`, `WALLS.C:2520` | CPU + enables deeper pool cuts | close-port (~80 lines) |
| Dormant 24 B pickup records in level data | Z-Treme `ZTE_DEF.H:187-195` | HWRAM (coins bypass pool) | pattern-only |
| Frame-phase buffer aliasing | SlaveDriver `WALLS.C:1234-1253` | LWRAM, needs overlap-phase proof | pattern-only |
| VDP1 texture slots + LRU + upload-time mips | SlaveDriver `PIC.C` | VDP1 time (FPS sprint) | close-port (~150 lines) |
| Near-to-far degradation + AI time-slicing | Z-Treme `ZT_RENDERING.c:494-503`, SlaveDriver `AICOMMON.C:24` | VDP1 + CPU (FPS sprint) | pattern-only |

## Self-review notes

- Spec coverage: goal = flags-on textured build, linkable and owner-accepted → Tasks 2-6 are the critical path; Task 1 records the constraint; Task 7 runs the owner's requested fidelity-scaling exploration without coupling to the gate.
- The pool cut recovers ≥55 KB against a 12.4 KB deficit even at the conservative capacity-128 option — margin is not the risk; unmeasured occupancy spikes are, which is why Task 2 precedes G1 and the latch is mandatory in Task 4.
- Type/name consistency: probe struct name `g_sm64_saturn_object_pool_probe` and field names are used identically in Tasks 2, 4, and 5. Capacity macro `SATURN_OBJECT_POOL_CAPACITY_OVERRIDE` consistent across Tasks 4-5.
