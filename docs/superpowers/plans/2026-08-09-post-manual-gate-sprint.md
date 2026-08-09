# Post-Manual-Gate Sprint: Visible Enemies + Audible BOB

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This is an ORCHESTRATION plan: several tasks delegate to already-written, fully-detailed sub-plans (cited per task) — those sub-plans' own task text is authoritative for their internal steps; this document owns sequencing, gating, and the cross-lane dependencies only.

**Goal:** Starting from the first owner-accepted manual boot of the canonical build (animation=1, actors=1), deliver the two things that most change what the game visibly and audibly IS: real enemies rendering with textures and their existing interactions on screen (governing-plan Task 22 closure), and real in-game music/SFX (governing-plan Task 23 closure) — run as two parallel lanes that converge at the end.

**Architecture:** Lane A (enemies) finishes the stashed actor-identity registry, executes the already-written Task 16 completion plan (production actor-queue drain replacing Mario's hardcoded jobs), closes the unowned actor-bank capability gap (13 unsupported records), and lands governing-plan Task 22. Lane B (audio) executes the already-written Task 12 completion plan (real audio payload data), then Task 21's remaining sourceboot integration waves, and lands governing-plan Task 23. The lanes are independent except for one deliberate join: **Task 22's linker step requires Task 12's real payload hashes** (governing plan :1604), so B1 feeds A4 even though Task 22 keeps semantic audio off.

**Tech Stack:** Established project conventions throughout — SH-2 C via Yaul SDK, Python 3 host tooling, MSYS2 toolchain (invoke via `/c/msys64/usr/bin/bash.exe` explicitly; pass vars as `make VAR=value`, not exports — both hazards documented in CHANGELOG this session), subagent-driven-development with two-stage review per task, SDD ledger updates at every transition.

---

## Entry gate (nothing in this sprint starts until ALL of these hold)

1. The in-flight cart-size-fix workflow completes: spec + quality reviews PASS on commit `4bd66637`, and the headless re-capture confirms execution proceeds past the cart-load gate (VDP1/VDP2 presentation generation advances past 0, no exception record).
2. **Owner manual acceptance** of the canonical build (identity rebuilt post-fix) in desktop Ymir with the profile-managed 32-Mbit DRAM cart: BOB boots, controls/camera normal, the Task 23A HUD is visible and live, stability holds through real play, FPS in the accepted 4–6 band or explained if not. This is the owner's own eyes per project convention — no automated capture substitutes.
3. If manual acceptance FAILS: stop, root-cause the failure as its own task, re-gate. Do not start sprint lanes against an unaccepted baseline.

**Rides along with the gate (not a sprint task):** HUD plan Tasks 9–10 (`docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`) — Task 9's automated headless HUD verification is unblocked by the green link and should run as part of gathering the entry-gate evidence; Task 10's manual HUD acceptance IS the same manual session as gate item 2.

## Standing constraints (owner-set, unchanged)

- No single-SH2 substitution, texture workaround, capacity shrink, or link-margin weakening. Feature-off Mario rollback stays byte-identical. The accepted 4–6 FPS BOB rollback baseline is immutable.
- Intermediate feature regressions are measured, not merge-blocking (governing plan policy) — but every task records real FPS impact when target evidence is captured.
- CHANGELOG.md entry in every behavior-changing commit; SDD ledger (`.superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md`) appended at every task transition; the 4 pre-existing dirty ledger files from other tracks stay untouched.

## Dependency map

```
Entry gate (manual acceptance)
  ├─ Lane A: A1 registry ─┬─> A2 Task-16 drain ─┬─> A4 Task 22 (enemies visible)
  │           A2.1 meshlets (parallel w/ A1) ────┘        ▲            │
  │           A3 capability gap (parallel w/ A1/A2) ──────┘            ▼
  └─ Lane B: B1 Task-12 audio data ──────────────────────┘   B3 Task 23 (audible BOB)
              └─> B2 Task-21 sourceboot integration ─────────────▲
```

---

## Lane A — Visible enemies with texture and interaction

**Discovered constraint (2026-08-09):** A2's and A4's target-build steps
will not link until the memory-residency campaign
(`docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`) closes
a measured 12,408-byte HWRAM link deficit on the flags-on demo-path build
(`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1`). Evidence map:
`build/saturn/sourceboot/e2-bob-identity-id-f60aaf60fe7b5d06/obj/sm64-saturn-sourceboot-e2.map`.
Host-side/source-only steps (A1 registry, A3 capability-gap closure) are
unaffected and may proceed now; only the actual link/target-build steps in
A2 and A4 are gated on the campaign.

### A1: Finish the actor-identity registry (unstashes + completes Task 14's Task 3)

**Authoritative spec:** `docs/superpowers/plans/2026-08-07-task14-completion.md` Task 3 (read it in full first — RED tests, generator determinism, fail-closed absent-not-zero rows, seam wiring into `saturn_source_observe_object_begin()`).

**Files:** per that spec, plus the two stashes holding the half-built prior attempt.

- [ ] **Step 1: Recover the stashed work-in-progress.** `git stash list` — expect two entries labeled "Task14 Task3 actor identity registry" (part 1: `rendering_graph_node.c` + generator + generator test; part 2: Makefiles + C test + snapshot test). Pop BOTH (`git stash pop` twice, part 2 first since it was pushed last), resolve any conflicts against the file's post-wave-4 state (`rendering_graph_node.c` changed substantially since the stash — the observer seam at `saturn_source_observe_object_begin()` itself should be intact, but verify line positions fresh; treat the stashed edit as a draft to re-apply against current reality, not a clean merge).
- [ ] **Step 2: Audit the draft against the Task 3 spec before completing it.** The stashed work was interrupted mid-verification and never reviewed — do not trust it. Check each spec requirement (deterministic output, absent-not-zero unsupported rows, big-endian 8×u32 hash words, registry lookup on hit / zeros on miss) against what the draft actually does; finish or fix what's missing.
- [ ] **Step 3: Verify per the spec's own commands** (`verify-actor-identity-registry` target from the stashed Makefile wiring, `verify-actor-instance-snapshot`, `verify-render-snapshot-bank`, the extended `test_actor_snapshot_source.py`). GREEN required. Confirm nonzero admission: the snapshot tests must show a registered-family fixture object passing `valid_observation()`.
- [ ] **Step 4: Commit + two-stage review** (spec, then quality — the seam is the single gate every downstream actor feature admits through; review rigor per `feedback_subagent_review_rigor` memory).

### A2: Execute the Task 16 completion plan (production actor-queue drain)

**Authoritative plan:** `docs/superpowers/plans/2026-08-07-task16-completion.md` (5 tasks, fully specified). Execute via subagent-driven-development, task by task, with its own internal reviews.

- [ ] **Step 1 (may start in parallel with A1):** Task 16 plan's Task 1 — `sm64_saturn_actor_meshlets_prepare_bank()` generalization. Explicitly registry-independent per that plan.
- [ ] **Step 2 (requires A1 merged):** Task 16 plan's Tasks 2–4 — handoff wiring, worker cutover at the compat wrappers, painter-ordered master merge + queue-owned retirement + the deferred I3 capacity gates.
- [ ] **Step 3:** Task 16 plan's Task 5 — feature-off byte-identity rollback proof + target evidence against a fresh canonical build.

### A3: Close the actor-bank capability gap (the 13 unsupported records — currently UNOWNED)

**Context:** Task 11's generic actor-family bank reports `complete_closure=false` with 13 unsupported representatives across 14 BOB closure records — 12× `GEO_CULLING_RADIUS`, 1× `GEO_BRANCH_AND_LINK` (ledger, Tasks 11/18/20 histories). Task 22's acceptance requires `unsupported_required_capability_count == 0`, so this MUST close before A4. No existing task owns it.

**Head start from this session's geo-walk research (verified, cite in the implementation):** `GEO_CULLING_RADIUS` is structurally a pure single-child pass-through wrapper — `GraphNodeCullingRadius` is `node + s16 cullingRadius + padding`, no function pointer, no per-visit dispatch; its only runtime effect is `obj_is_in_view()`'s field-peek for the cull-test radius (`rendering_graph_node.c` ~:1449). All 34 real occurrences wrap real children. The bank compiler can therefore almost certainly treat it as a transparent wrapper: record the radius as family metadata, descend into children. `GEO_BRANCH_AND_LINK` (1 occurrence) is a call-with-return-style layout branch — needs its own real read.

- [ ] **Step 1: Research-first (mandatory).** Read Task 11's compiler (`compile-actor-banks` chain in `Makefile.saturn.mk`, the family-bank generator under `tools/saturn/`) to find exactly where unsupported geo commands are rejected and what "supporting" a command requires (S64F record fields, capability bits, validation). Read the 13 real closure records to see which actors they gate — these are the actual enemies this sprint exists to render.
- [ ] **Step 2: RED tests** in the compiler's existing test suite: a CULLING_RADIUS-wrapped fixture layout must compile to a family with the radius captured and children fully processed; a BRANCH_AND_LINK fixture must compile with correct return semantics; the real BOB closure must reach `unsupported_required_capability_count == 0` and `complete_closure=true`.
- [ ] **Step 3: Implement, GREEN, regenerate the real bank,** confirm the bank's byte size/hash changes flow correctly through the identity system (the seal-stage identity hash will change — expected, note it in the CHANGELOG entry).
- [ ] **Step 4: Commit + two-stage review.**

### A4: Land governing-plan Task 22 (the convergence — enemies on screen)

**Authoritative spec:** `docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md` Task 22 (:1587–1623) — final BOB S64P reseal, generated instance list replacing the single-Mario path, closure-derived replay manifest, zero unsupported capabilities, target + headless + manual evidence.

**Depends on:** A1 + A2 + A3 + **B1** (payload hashes). Do not start its linker/reseal step before B1's outputs exist.

- [ ] Execute Task 22 per its own acceptance text, via subagent-driven-development. Its manual visual check (enemies visible, interacting, no ordinary quarantines) is an owner-eyes gate — schedule it with the owner, same convention as the entry gate.

---

## Lane B — Audible BOB (runs in parallel from sprint start)

### B1: Execute the Task 12 completion plan (real audio payload data)

**Authoritative plan:** `docs/superpowers/plans/2026-08-07-task12-completion.md` (5 tasks, fully specified: standalone seq00 generation, m64 decode-walk validator, closure-derived resident bundles, S64P AUDIO_DEPENDENCIES emission, fresh deterministic artifacts). Fully independent — start at sprint entry, or even before the manual gate if idle capacity exists (it touches only host tooling, zero risk to the gate build).

- [ ] Execute all 5 tasks via subagent-driven-development with their internal reviews. Sprint-level acceptance: GREEN-twice determinism, real payload hashes/byte counts/scratch limits existing for Task 22's consumption (this unblocks A4's reseal step).

### B2: Task 21 remaining sourceboot integration

**Authoritative spec:** governing plan Task 21 + its preflight report (ledger :233, :447–448, :482–484). Waves 0/1/2 (SMPC wrapper, boot contract, completion-ring ABI) are source-complete; what remains is the production side: sourceboot service/loader wiring, real 68K image integration (Task 17's modules into the production heartbeat), package/residency binding for the B1 payloads, and the completion/acknowledgment path.

**Depends on:** B1 (real AUDIO.DAT/bundles to load). Uses Tasks 15/17's already-source-complete modules — do not reopen their internals.

- [ ] **Step 1:** Read the Task 21 preflight + Waves 0–2 ledger entries in full; draft the remaining-waves brief per the preflight's own staged-wave structure (it defines the wave boundaries — follow them, don't invent new ones).
- [ ] **Step 2:** Execute the remaining waves via subagent-driven-development, each wave with two-stage review, host gates green before any target claim.

### B3: Land governing-plan Task 23 (audible proof)

**Authoritative spec:** governing plan Task 23 (:1625+) — full BOB music/SFX from original call sites, independent 68K timing, capture + manual audible confirmation.

**Depends on:** B2 + **A4** (Task 23 depends on Task 22 per the governing plan's own dependency list). This is the sprint's final convergence.

- [ ] Execute per its acceptance text. Its audible confirmation (music correct, SFX firing on real gameplay events, X-stop behavior, no drops) is an owner-ears gate — schedule with the owner.

---

## Sprint exit criteria

1. Task 22 accepted: BOB runs with the full generated actor closure — every ordinary enemy/object visible, textured, animating, interacting, with exact-generation identity and zero ordinary quarantines — owner-confirmed visually.
2. Task 23 accepted: original BOB music and SFX audible from real gameplay, owner-confirmed.
3. Real FPS recorded for the full build (measured, not gated — the ≥4.0 recovery gate belongs to Tasks 28/29 after this sprint).
4. Ledger, CHANGELOG, and the governing plan's Task 16/12/21/22/23 statuses reconciled to match reality.

## Explicitly NOT in this sprint

The 12–15 FPS optimization sprint (downstream, Tasks 28/29 first), Whomp's Fortress (Task 27), the CLUT plan's remaining Tasks 2–5, scene-residency wiring beyond what Task 22 itself requires, and any STATE.md/ROADMAP.md narrative rewrite (one stale exit-condition claim is already flagged for the owner separately).
