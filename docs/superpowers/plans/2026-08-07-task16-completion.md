# Task 16 Completion Plan (actor-queue production drain and cutover)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish governing-plan Task 16 (`docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md:832-879`): replace the production renderer's hardcoded Mario-only actor jobs with a real drain of the generic actor-instance queue — one descriptor-owned job per admitted instance — through meshlet preparation, batch publication, painter-ordered master merge, and queue-owned retirement, without destabilizing the proven eight-entry world graph and with the feature-off Mario rollback preserved byte-for-byte.

**Architecture:** All the hard infrastructure already exists and passed independent review (queue/batch/arena ABI at `d4efe0e9`; lifecycle handoff EMPTY→…→RETIRED at `e82759ce`, final rereview PASS C0/I0/M0 — note the governing plan's status bullet at :190 is stale, the ledger at :261 is authoritative). What's missing is the production path that *uses* it: (a) a bank-generalized meshlet preparer, (b) real ACTOR_ADMIT/ACTOR_LOWER workers draining the queue at the marked "one auditable replacement point," (c) an actor-batch consumer in the master merge, (d) retirement routed through the queue instead of around it. The design contract is plan:836/:858: preserve the 8-descriptor world graph; either SH-2 claims descriptors; master final merge preserves source/painter order; capacity comes from the validated scene manifest max + compile-time ceiling.

**Tech Stack:** SH-2 C (dual-CPU, generation-keyed lock-free claim patterns already established in `saturn_actor_instance_queue.c` / `saturn_render_job_runtime.c`), host C test harnesses via `Makefile.saturn.mk` `verify-*` targets, MSYS2 toolchain wrapper.

**Hard dependency:** `2026-08-07-task14-completion.md` Task 3 (identity registry). Until it lands, `valid_observation()` rejects every real object (`family_id==0`), so this plan's drain would run against a permanently empty queue. **Task 1 below is registry-independent and may start immediately; Tasks 2-5 require Task 14's registry merged.** Target (Ymir) evidence additionally requires Task 14's green link.

**Execution status (2026-08-11):** `active` as the load-bearing prerequisite
for hermetic release Task 10. Exact manifest-bound smoke at commit `5a72a3aa`
proved the target loads correctly, completes its cart copy, and receives
VBlank callbacks, but render generation 1 terminates
`DONE,DONE,FAILED,QUARANTINED`: the sealed feature-on actor-admission wrapper
still intentionally fails closed. Disabling dynamic actor closure is rejected
because the same production actor path must scale to the total game. Task 1 is
source-complete after review repair on its isolated branch with independent
rereview still open;
Tasks 2–5, target proof, Task 9 reseal, and Task 10 smoke/visual/manual
acceptance remain open.

**Task 1 interface correction (2026-08-11):** the governing-plan prototype's
`bank` parameter means the complete validated
`const sm64_saturn_actor_bank_view_t *`, not its header-only
`sm64_saturn_actor_bank_t` member. The latter cannot expose pose streams or
geometry spans. The six-argument shape remains binding; the Task-1-owned
meshlet output binding therefore carries draw capacity and an established
quarantine reason for Task 2/3 to adapt from each queue descriptor. Bank
family/model/source-hash mismatches fail before output writes. Numeric
`actor_bank_id` is not encoded in S64B; Task 2 must map that ID plus exact
scene-package generation to the immutable validated view. Scene-package
generation freshness remains a Task 2 handoff precondition because it is not
represented by the S64B bank view. S64F v2's 56-byte records establish family
registration/capability metadata upstream; they are not geometry records. The
generic output binding is one descriptor-owned contiguous draw-record span,
partitioned deterministically as opaque records followed by translucent
records; it also owns an explicit uniqueness bitmap so evaluated pose lights
and joint matrices remain immutable through preparation. The legacy Mario
entry point and output struct retain their separate arrays and frozen ABI. The
generalized entry point is type-safe: its output parameter is
`sm64_saturn_actor_meshlet_bank_output_t *`, and the shared core receives that
binding's unchanged legacy `output` member internally.

**Task 1 review repair (2026-08-11):** independent review of `3d5e6ff8` /
`863faa8d` returned `CHANGES REQUIRED C0/I4/M2`, so Task 2 remains blocked.
The repair centralizes the eight-byte output record and quarantine values in a
renderer-neutral ABI header: queue-arena records and legacy meshlet draw refs
are the same typedef, permitting direct binding without casts or copies. The
fixed 65,536-byte actor runtime arena and its 2,718 output records do not absorb
pose scratch. Instead, the registry-selected S64B dependency's already-reserved
`maximum_scratch` owns two aligned, non-overlapping claimant lanes because
either SH-2 may process a descriptor concurrently. Each package-derived lane
contains posed vertices (`6V`), lights (`V`), 4x4 Q16 matrices (`64J`),
positions (`2V`), and uniqueness bytes (`V`), with four-byte alignment; the
Mario bank therefore declares 5,520 bytes per lane / 11,040 total. Task 2 must
expose this dependency scratch at the exact scene generation and map numeric
bank ID to its validated view; Task 3 must gate live lane ownership; Task 5
retains target map/capacity proof. `prepare_bank` trusts that immutable
validated view and performs only cheap identity/bounded selected-record checks,
never a full-bank validation pass. The combined Mario S64B currently advertises
its dependency as `ANIMATION_DEPENDENCIES`; the binder is payload-kind-neutral
and Task 2 must expose the scratch belonging to the registry-selected S64B
rather than changing package identity in Task 1.

**Current ground truth (2026-08-07 research pass):**
- Arena reality (post-`c1e8e73e`, supersedes older ledger numbers): bank 24,088 + observer 13,024 (240-slot identity sidecars) + queue 5,644 + batches 1,024 + alignment + outputs = 65,536 B; output-record ceiling **2,718**; 64 live instances.
- The replacement point: `demo_render_prepare_publish()` (`src/port/saturn/gfx/saturn_demo_render.c:3834-3974`) publishes a fixed 4-job graph — WORLD_ADMIT, WORLD_LOWER, ACTOR_ADMIT (:3939-3943), ACTOR_LOWER (:3945-3949), deps `{0, 1<<0, 0, 1<<2}` (:3952-3954), job count 4 if Mario visible else 2 (:3955) — via `sm64_saturn_render_job_graph_publish` (:3958). The ACTOR slots route through `demo_actor_admit_compat_wrapper` / `demo_actor_lower_compat_wrapper` (:2936-2962; wired :2964-2974): feature-off → exact Mario `demo_actor_queue_transform`/`demo_actor_queue_classify`; feature-on → `return false` (quarantine). The comment at :2931-2935 marks this as the "one auditable replacement point."
- Mario's inputs come from `demo_prepare_mario()` (:3648-3711) calling the Mario-scoped `sm64_saturn_actor_meshlets_prepare` (:3691); the generalized `sm64_saturn_actor_meshlets_prepare_bank(bank, instance, view, pose_work, output, stats)` specified at plan:848-856 exists nowhere.
- Master merge today: `demo_render_finalize()` (:3994) merges terrain then appends Mario via `demo_finalize_mario_draws()` (:4085) / `demo_emit_mario` (:4124). No consumer of `sm64_saturn_actor_batch_t` exists outside the infra files and tests; `acknowledge_consumed` is never called in production.
- Bank lifecycle IS production-wired per frame (observer open `sourceboot/main.c:492`; end/capture/acquire/recycle :368-389) but retirement bypasses the queue (:1238-1256 success path, :1274-1284 quarantine path); the handoff module (`saturn_actor_runtime_handoff.h:37-53`) has zero production callers.
- Feature flags: `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE ?= 0` (`sourceboot/Makefile:53`, validated :61-63, `-D` at :495, in build identity :78/:408); `build_sourceboot_variant.py --actors` maps to it. `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION` gates the compiled-in bank. Task 22 runs animation=1, actors=1, audio=0.
- Deferred-from-Task-14 I3 half (plan:1366-1372): binding the `.lwram_actor_runtime` owner's members through the production handoff with identity-bound capacity gates (accept 64 live / 2,718 records; reject 0, 65, 2,719, stale generation). **This plan's Task 4 owns it.**

---

### Task 1: Generalize meshlet preparation to bank instances (registry-independent — start immediately)

**Status:** `source-complete-review-repair` from base `ec8ef844` in the repair
behavior commit containing this status; round-1 RED/GREEN and all focused host
gates are complete. Task 2 remains blocked until independent rereview passes.
No target-complete claim is made.

**Files:**
- Modify: `src/port/saturn/gfx/saturn_actor_meshlets.h/.c`
- Test: extend the existing meshlet host gate (locate via `Makefile.saturn.mk`'s `verify-actor-pose-bank` / meshlet targets — read the current test harness before writing)

- [x] **Step 1: Read the real Mario-scoped path first**

Read `saturn_actor_meshlets.h:27-35` (current `sm64_saturn_actor_meshlets_prepare` contract) and its full `.c` implementation, plus `saturn_actor_instance_queue.h:57-72` (descriptor fields the generalized form must populate: `material_id`, `output_offset`, `output_capacity`, `output_class`) and the S64F family-bank record layout from Task 11's generated bank (the 56-byte v2 records). The generalization target signature is fixed by plan:848-856: `sm64_saturn_actor_meshlets_prepare_bank(bank, instance, view, pose_work, output, stats)`.

- [x] **Step 2: RED tests**

Extend the meshlet host gate with bank-driven cases before implementing: a registered rigid/opaque family instance produces exactly the meshlet set its bank record declares (counts and output-span bounds from the bank, not from Mario's hardcoded shape); an instance whose output span would exceed `output_capacity` fails closed with the established quarantine disposition (never a partial write); a stale bank generation is rejected before any output write. Run; confirm RED.

- [x] **Step 3: Implement `prepare_bank` as a wrapper-plus-generalization, not a fork**

Factor the Mario path's per-meshlet loop so both entry points share one core (Mario's existing `prepare` becomes a thin call through the same core with his fixed inputs — the feature-off path must remain byte-identical, which the existing feature-off wrapper tests already gate). No new global state; all working storage comes from the caller-supplied `pose_work`/`output` spans per the plan contract.

- [x] **Step 4: Verify + commit**

Run the meshlet gate + `verify-actor-pose-bank` + the feature-off wrapper gate (all must stay green). Commit:

```bash
git add src/port/saturn/gfx/saturn_actor_meshlets.h src/port/saturn/gfx/saturn_actor_meshlets.c <test files> CHANGELOG.md
git commit -m "feat(saturn): generalize actor meshlet preparation to bank instances"
```
Independent two-stage review before Task 2.

---

### Task 2: Production handoff wiring — populate and claim (requires Task 14 registry)

**Files:**
- Modify: `src/port/saturn/sourceboot/main.c` (the per-frame bank lifecycle at :368-389/:492 and retirement at :1238-1284)
- Modify: `src/port/saturn/gfx/saturn_demo_render.c` (feature-on prepare path)
- Reference: `src/port/saturn/gfx/saturn_actor_runtime_handoff.h/.c` (the reviewed state machine — use it, do not reimplement)

- [ ] **Step 1: RED — a production-shaped host test**

Extend the handoff/queue host gates with a production-sequence case: observer capture (with registry-resolved nonzero identities) → handoff begin → queue population (one descriptor per admitted instance, fields per `saturn_actor_instance_queue.h:57-72`) → claim by a worker → terminal publication → `acknowledge_consumed` → retirement THROUGH the handoff (not around it). Assert the current production bypass shape (direct bank retire without queue involvement) is structurally absent from the new path. Run; RED.

- [ ] **Step 2: Wire the feature-on population path**

Behind `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE`, insert the handoff calls into the real frame loop in `main.c` where the bank lifecycle already runs, populating the queue from the bank's admitted instances (using Task 1's `prepare_bank` outputs for descriptor spans). Feature-off compiles the exact current code — guard with the same `#if` discipline the existing feature-off wrapper commits established, and keep the existing bypass retirement as the feature-off path only.

- [ ] **Step 3: Verify + commit + review**

Host gates green (`verify-actor-instance-snapshot verify-render-snapshot-bank verify-actor-instance-queue` + the extended handoff gate). Commit `feat(saturn): wire actor runtime handoff into production frame loop`; two-stage review.

---

### Task 3: Worker cutover at the compat wrappers

**Files:**
- Modify: `src/port/saturn/gfx/saturn_demo_render.c` (:2936-2974 wrappers; :3834-3974 job publication)

- [ ] **Step 1: RED**

Extend the render-job host gate: feature-on ACTOR_ADMIT must drain queue descriptors (bounds → selected-pose eval → meshlet admission per plan:858) instead of returning false; ACTOR_LOWER lowers each admitted instance's meshlets into its descriptor-owned output span; a queue with zero admitted instances publishes the world-only graph exactly as today's Mario-invisible path does (:3955's count-2 shape); either-SH2 claiming exercised by the existing dual-lane test pattern. RED first.

- [ ] **Step 2: Replace the wrapper quarantine bodies**

`demo_actor_admit_compat_wrapper`/`demo_actor_lower_compat_wrapper` feature-on branches change from `return false` to the real drain calls. The 4-job graph shape and deps `{0, 1<<0, 0, 1<<2}` stay untouched — the ACTOR jobs' *content* changes from single-Mario to queue-drain; the world graph is not modified (plan:836's design correction is binding).

- [ ] **Step 3: Verify + commit + review**

Full actor regression wave (`verify-actor-effects verify-actor-batches verify-actor-capability-bank verify-actor-family-bank verify-actor-instance-queue` — the serialized wave shape from `progress.md:230`). Feature-off wrapper gate still green. Commit `feat(saturn): drain generic actor queue in production admit/lower workers`; review.

---

### Task 4: Master merge, retirement, and the deferred I3 capacity gates

**Files:**
- Modify: `src/port/saturn/gfx/saturn_demo_render.c` (`demo_render_finalize` :3994, `demo_finalize_mario_draws` :4085, `demo_emit_mario` :4124)
- Modify: `src/port/saturn/gfx/saturn_actor_instance.c` / the arena owner (capacity gates per plan:1366-1372)

- [ ] **Step 1: RED**

Merge-order test: with world jobs + N actor instances, the final master command bank preserves source/painter order (plan:858) — actor draws interleave by the same depth/priority contract Mario's appended draws use today, not appended as an unordered block. Capacity gates: accept 64 live/2,718 records; reject 0, 65, 2,719, and stale-generation — the exact cases plan:1366-1372 deferred from Task 14's I3. Retirement: after merge + `acknowledge_consumed`, the bank retires through the queue-reset path; a mid-merge failure retains queue ownership for retry (the `e82759ce` I3 semantics, now exercised from production).

- [ ] **Step 2: Implement**

Add the actor-batch consumer to `demo_render_finalize()` alongside (not replacing) the Mario finalize path — feature-on drains batches, feature-off keeps `demo_emit_mario` exactly. Bind the `.lwram_actor_runtime` owner's members through the handoff with the identity-bound capacity checks.

- [ ] **Step 3: Verify + commit + review**

Full wave + merge-order + capacity mutations green. Commit `feat(saturn): actor batch master merge, queue-owned retirement, capacity gates`; two-stage review. This commit closes Task 14's I3 residual — note that cross-reference in the CHANGELOG entry and ledger.

---

### Task 5: Rollback proof, evidence, reconciliation

- [ ] **Step 1: Feature-off byte-identity proof** — build feature-off, confirm the produced object code for the wrapper/finalize/frame-loop sites is unchanged from pre-plan HEAD (objdump diff of the affected objects, or the established source-policy test pattern if one exists for this). The accepted 4-6 FPS BOB rollback must be provably untouched.
- [ ] **Step 2: Target evidence (gated on Task 14's green link)** — feature-on serialized build via `build_sourceboot_variant.py --animation 1 --actors 1 --audio 0 --pipeline 4`; map inspection (arena spans, P2 queue records, output spans per Task 22:1608's checklist); headless capture showing admitted-instance jobs reaching terminal results or named quarantines. If Task 14's link isn't green yet, record host-complete status in the ledger with the target gate explicitly open — do not fake it.
- [ ] **Step 3: Reconcile** — governing-plan Task 16 status bullet (:190 is stale — fix it), ledger entry, CHANGELOG, final whole-diff review.

```bash
git add docs/ .superpowers/ CHANGELOG.md
git commit -m "docs(saturn): reconcile task 16 completion evidence and ledger"
```
