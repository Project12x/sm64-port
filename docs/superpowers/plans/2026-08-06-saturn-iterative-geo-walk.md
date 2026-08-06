# Saturn Iterative Geo-Walk Implementation Plan

> **For agentic workers:** Execute this plan inline in bounded task waves. Every checkbox is reconciled in the live sprint plan and SDD ledger before the next task begins.

**Goal:** Remove recursive source scene-graph traversal from the production Saturn path while preserving source visitation/callback/matrix semantics for the full linked game.

**Architecture:** A Saturn-only enter/leave dispatcher will replace recursive `geo_process_node_and_siblings()` descent. Its compact continuation frames live in a linker-owned LWRAM arena whose capacity is generated from all linked level and actor geo layouts; the master SH-2 owns traversal and the slave SH-2 consumes immutable jobs after publication. The recursive walk remains only in a separate baseline/oracle build.

**Tech Stack:** C11 freestanding Saturn runtime, GNU SH-ELF/Yaul linker script, Python source/map contract tests, existing native C contract harness, Ymir headless/desktop evidence.

## Global Constraints

- Preserve exact source visitation order, callback timing, switch/children-first rules, matrix push/pop boundaries, global restoration, animation state, and shared-child parenting.
- The iterative candidate remains dual-SH2; no single-SH2 fallback or disabled geo walk is permitted.
- The traversal arena is CPU-only LWRAM; it must not overlap VDP1 commands, SCU-visible staging, camera capture, actor runtime, or the reserved slave stack.
- Capacity is generated from the full linked level/actor manifest, not hardcoded for BOB.
- Overflow is fail-closed with a named diagnostic record; no silent truncation or partially restored state is allowed.
- Every behavior commit updates `CHANGELOG.md`; every task updates the active plan and SDD ledger before the next task.
- Do not claim target, Ymir, manual, audio, texture, or FPS gates from host-only tests.

---

### Task 1: Define and RED-test the iterative traversal contract

**Files:**
- Create: `src/port/saturn/runtime/saturn_geo_walk.h`
- Create: `src/port/saturn/runtime/saturn_geo_walk.c`
- Create: `tools/saturn/geo_walk_contract_test.c`
- Create: `tools/saturn/test_geo_walk_contract.py`
- Modify: `Makefile.saturn.mk` to expose `verify-saturn-geo-walk-contract`
- Modify: `CHANGELOG.md` only with the behavior/design rationale once the first behavior commit lands

**Interfaces:**
- `sm64_saturn_geo_walk_frame_t` stores opaque node/sibling tokens, phase, leave action, saved matrix depth, and saved context token.
- `sm64_saturn_geo_walk_t` stores the caller-provided frame span, depth/high-water, overflow latch, and generation/scene diagnostics.
- `sm64_saturn_geo_walk_init(sm64_saturn_geo_walk_t *, sm64_saturn_geo_walk_frame_t *, uint16_t capacity)` initializes a bounded context without allocating.
- `sm64_saturn_geo_walk_push_enter(...)`, `sm64_saturn_geo_walk_push_leave(...)`, `sm64_saturn_geo_walk_next(...)`, and `sm64_saturn_geo_walk_fail_reason(...)` provide the scheduler seam used by the source dispatcher. Initialization is explicit through `sm64_saturn_geo_walk_init(...)` so the caller owns the LWRAM frame span.
- The host seam uses opaque `uintptr_t` node tokens and an operation table; it does not include Yaul or Saturn MMIO headers.

- [x] Write RED tests for enter/leave order, sibling continuation, children-first scheduling, matrix-depth/context tokens, leave restoration, and exact-capacity overflow.
- [x] Run the focused contract test and record the expected missing-header failure in the ledger.
- [x] Implement the minimal pointer-free/opaque scheduler and its explicit overflow latch.
- [x] Re-run the focused C/Python tests; the overflow latch prevents stale frame consumption after capacity failure and the ordering fixture covers dropped/retained continuation events.
- [x] Record the host ABI sizes (`frame=16`, `walk=32`) and test output in the SDD ledger before starting Task 2. SH-2 linked sizes remain a Task 2/sourceboot map gate.

**Task 1 status:** source-complete for the bounded scheduler contract. This is
not a production traversal cutover: full-game capacity, LWRAM placement, source
handler conversion, linked target stability, and manual/FPS evidence remain
open under Tasks 2--5.

### Task 2: Add full-game capacity generation and LWRAM ownership

**Files:**
- Create: `tools/saturn/geo_depth_manifest.py`
- Create: `tools/saturn/test_geo_depth_manifest.py`
- Create: `src/port/saturn/runtime/saturn_geo_walk_storage.c`
- Create: `src/port/saturn/runtime/saturn_geo_walk_storage.h`
- Modify: `src/port/saturn/sourceboot/sourceboot-cart.x`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `Makefile.saturn.mk`

**Interfaces:**
- `tools/saturn/geo_depth_manifest.py` emits a deterministic generated header with `SM64_SATURN_GEO_TRAVERSAL_CAPACITY`, the source identity, and the maximum proven depth across all linked level/actor geo inputs.
- `sourceboot_geo_walk_frames[]` is aligned, NOLOAD, and placed in `.lwram_geo_traversal`.
- Linker symbols `__lwram_geo_traversal_start/end` and assertions prove alignment, exact generated size, and non-overlap with `.lwram_actor_runtime`, camera capture, and the final 16-KiB slave stack.

- [x] Write RED manifest tests for missing inputs, duplicate identities, depth undercount, capacity mutation, and nondeterministic ordering.
- [x] Run the manifest tests before implementation and record the expected missing-generator failure.
- [x] Implement deterministic depth extraction from every actor/level GeoLayout source, shared-child edge, held-object edge, and generated callback declaration; the current repository proof is 518 inputs, depth 172, capacity 256.
- [x] Add the storage section and linker assertions without changing the current master/slave stack addresses.
- [x] Run manifest, sourceboot memory-map, storage-owner, generated-linker-fragment, and host storage ABI tests; leave target-link evidence unchecked until a fresh image is built.

**Task 2 status:** source-complete for generated capacity and LWRAM ownership in
commit `6dbaea8d`. The generated header/linker fragment are build outputs, not
checked-in artifacts; a target link must regenerate and consume them. The
production renderer still does not dispatch through the arena.

Task 2 frame refinement `98d9cce2` gives the production owner a 16-byte SH-2
continuation record with node and sibling cursors; the original host scheduler
ABI remains a separate contract fixture. Task 3 is active. Its RED source
policy currently reports 24 direct recursive dispatcher calls, so no source
conversion or target claim is implied yet.

Task 3a runtime seam is source-complete in `937f0043`: the production runtime
API now owns bounded node/sibling enter/leave events and overflow latching, and
the sourceboot object list links it beside the LWRAM owner. The source-policy
gate remains intentionally red until the handlers are converted.

Runtime scheduling refinement `a48e5aac` adds depth-first child/sibling order,
deferred children-first dispatch, explicit leave callbacks, and the synthetic
differential trace. This remains infrastructure; the source policy still
reports 24 recursive handler edges.

The runtime seam exposes `sm64_saturn_geo_walk_runtime_ops_t`: enter callbacks
return child/sibling continuations and saved tokens, while dispatch and leave
callbacks run at explicit phases. The source conversion must bind these
callbacks to the existing GraphNode globals; it must not bypass them with
synthetic node filtering.

### Task 3: Convert the Saturn source graph handlers to enter/leave dispatch

**Files:**
- Modify: `src/game/rendering_graph_node.c`
- Modify: `src/game/rendering_graph_node.h` only for Saturn-private declarations if required
- Modify: `src/port/saturn/runtime/saturn_geo_walk.c/.h`
- Create: `tools/saturn/geo_walk_source_policy_test.py`
- Create: `tools/saturn/geo_walk_differential_test.c`

**Interfaces:**
- `geo_process_node_and_siblings()` remains the public source entry, but under `TARGET_SATURN` it drives the iterative context rather than calling itself recursively.
- Each node handler has an enter path and a leave action. Leave actions restore `gMatStackIndex`, Q16/float mirrors, camera/frustum/master-list/object/held-object globals, animation globals, and shared-child parent links.
- The existing callback ABI and `GEO_CONTEXT_*` values are unchanged.

- [ ] Add source-policy RED checks proving the current direct recursive call sites are present and enumerated by node kind.
- [ ] Convert the root/sibling dispatcher first, preserving selected-switch sibling termination and children-first scheduling.
- [ ] Convert matrix-bearing nodes (camera, perspective/ortho, translation/rotation/scale, billboard, animated part, shadow) with explicit leave tokens.
- [ ] Convert master-list, display-list, generated/background, LOD, and callback paths without changing callback arguments.
- [ ] Convert object, object-parent, and held-object paths with saved context/animation state and exact shared-child parent restoration.
- [ ] Add a source-policy test that rejects any production handler's direct call to `geo_process_node_and_siblings()` and rejects an iterative build with no `.lwram_geo_traversal` owner.
- [ ] Run the synthetic recursive-versus-iterative differential suite across every node kind and all mutation cases.

### Task 4: Link and qualify the candidate image

**Files:**
- Modify: active full-game plan and SDD progress ledger
- Create: `tools/saturn/test_iterative_geo_link_contract.py`
- Create: `docs/saturn/evidence/reports/iterative-geo-walk-link-2026-08-06.md`

- [ ] Build the candidate sourceboot image with dual SH-2 and the generated full-game traversal capacity.
- [ ] Verify ELF/map identity, arena placement/high-water margin, both exception vectors, and no direct recursive source call in linked candidate code.
- [ ] Run the exact sourceboot headless post-BIOS trace through the prior green transition and retain the exception record if it fails.
- [ ] Launch the exact CUE with the repository `.ymir-profile` and profile-managed 32-Mbit DRAM for the manual gate; do not launch an older artifact.
- [ ] Record whether the candidate survives, whether textures/Mario/callbacks remain visible, and the current FPS without promoting unrelated audio/texture/performance gates.

### Task 5: Full-game closure and integration review

**Files:**
- Modify: active full-game plan, SDD progress ledger, `CHANGELOG.md`
- Modify: `docs/superpowers/specs/2026-08-06-saturn-iterative-geo-walk-design.md` if review corrections are required
- Create: `docs/saturn/evidence/reports/iterative-geo-walk-full-game-closure-2026-08-06.md`

- [ ] Run the full generated level/actor depth manifest and prove every linked scene remains within capacity.
- [ ] Run the complete host contract aggregate serially, including mutation tests and the source policy gate.
- [ ] Obtain independent specification/quality review of the implementation and repair every finding before closure.
- [ ] Reconcile every individual plan step, commit, test result, design correction, and remaining target/manual gate in the live ledger.
- [ ] Mark the traversal task source-complete only after linked target/Ymir evidence is fresh; leave broader FPS, texture, audio, and full-game content gates independently unchecked when still open.
