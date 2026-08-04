# Peer Audit — A5.8 atomic live cutover re-review (2026-08-04)

## Context

- Prior agent: Codex subagent
- Task claimed: Repair both critical A5.8 terrain handoff failures from audit `8e64b482`, including the slave-admit self-read edge, without changing the accepted queue/VDP1 lifecycle.
- Commits reviewed: cutover `a12236a9`, NO-GO audit `8e64b482`, repair `1667958e61e729efdedc9dbdf6c9759d39240594`.
- Files examined: the repair diff and full renderer terrain/frame route; terrain handoff policy and fixture; queue, graph, runtime, bridge, output, payload, callback-context sources; active plan/design/evidence/state/roadmap/changelog; prior audit.
- Commands run: commit/status/diff inspection, focused ownership/registration searches, the new handoff fixture plus all nine prior strict host fixtures, and `git diff 1667958e^ 1667958e --check`.

## Verdict

**GO for the repaired A5.8 source scope.** Both prior Critical findings are repaired in the live renderer, including the slave self-read edge. No Critical, Important, or Minor source-scope finding remains. This is not target cache/runtime approval: one serialized Saturn rebuild and desktop-Ymir run remain mandatory before any CUE or FPS claim.

## Findings

### Critical

None.

### Important

None.

### Minor

None.

### Discrepancies between summary and code

None. The documentation accurately records the first-review NO-GO, the exact repair, passing host gates, and the absence of post-cutover target/Ymir/FPS evidence.

## Critical-finding resolution

### Single WORLD_ADMIT no longer waits for a nonexistent peer

- The live frame snapshots terrain with `.dual_phase = false` (`src/port/saturn/gfx/saturn_demo_render.c:3690-3699`).
- WORLD_ADMIT derives one `sm64_saturn_terrain_queue_handoff_t` from its actual claimant and requires `peer_transform_required == 0` (`:2661-2684`).
- It calls `demo_transform_owned_positions(..., true)` (`:2693-2695`). In single-producer mode the loop deliberately does not call `demo_position_owner_read()` (`:1525-1531`), so a slave producer never rereads its just-written cached owner bytes through the obsolete hard-coded producer-0 alias.
- The producer transforms every position in the current visible-position set, publishes its exact lane/count release, and returns immediately because the live context has `dual_phase == false` (`:1532-1551`). The legacy diagnostic adapter alone calls the helper with `single_producer=false` and retains its two-lane rendezvous (`:2637-2655`).

### Lower reconstructs ownership from the exact DONE admit claimant

- WORLD_LOWER first proves its exact graph predecessor and obtains that exact descriptor through `done_job()` (`src/port/saturn/gfx/saturn_demo_render.c:2736-2746`).
- `demo_terrain_queue_admit_metadata()` validates generation, job index, nonzero sequence, claimant state, and output-bank claimant lane before returning metadata. Lower derives the handoff only from `admit_metadata->writer_lane` (`:2747-2752`).
- Before any classification/projected/view/valid read, lower calls `demo_prepare_position_owners()` locally with the exact admit claimant (`:2753-2759`). This overwrites a stale prior-generation cached owner map on whichever SH-2 claimed lower. Master-admit -> slave-lower and slave-admit -> master-lower therefore both select the producer's cached or P2 payload correctly.
- The new executable fixture runs two queue generations in those opposite directions, poisons the lower-local prior owner each time, and verifies that the exact admit claimant replaces it. It also proves that a single producer never requests a peer transform and rejects invalid lanes/null output.

## Retained cutover contract

- Process lifetime still has one graph-runtime activation and one Yaul CPU-DUAL registration. The accepted frame contains no legacy terrain worker or Mario transform dispatch.
- Four immutable descriptors and both self-contained terrain/Mario callback contexts publish before notify and either claimant drain.
- Master and slave share graph-aware dependency eligibility. All terminal state and positive notified-slave retirement precede queue reset and payload-bank reuse.
- Terrain and Mario terminal assembly validate exact DONE identity and claimant-owned payloads before `sm64_saturn_vdp1_backend_begin()`. VDP1 ordering/lowering remains master-only.
- Publication or execution failure returns before backend begin, performs no serial replay, and leaves the prior complete VDP1 list presentable. A terminal failed generation is retired before the next frame can reuse banks.

## Verification

All commands used `C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror` and passed:

```text
terrain_queue_handoff_test.c + graph/queue: PASS
render_job_live_cutover_source_test.c: PASS (exit 0; no success text)
render_job_terrain_route_source_test.c: PASS
render_job_actor_route_source_test.c: PASS
render_job_runtime_test.c + runtime/graph/queue: PASS
render_job_queue_test.c + queue: PASS
render_job_graph_test.c + graph/queue: PASS
render_job_bridge_test.c + bridge/output-bank/queue: PASS
render_job_payload_bank_test.c + payload/bridge/output-bank/queue: PASS
render_callback_context_test.c + callback-context/queue: PASS
git diff 1667958e^ 1667958e --check: PASS
```

No Saturn target build or Ymir run was performed or credited.

## What was done well

- The repair is narrow and preserves the legacy dual-phase helper solely for an isolated diagnostic adapter.
- Claimant ownership is carried through exact DONE metadata rather than inferred from job order, input offset, or the lower claimant.
- The two-generation poisoned-owner fixture covers both cross-lane directions and the slave producer's no-owner-lookup rule is also pinned structurally in the live callback.
- Documentation records the root cause and remaining target gates without overstating source evidence.

## Recommended next actions

1. Mark the repaired A5.8 source re-review GO in the active plan, evidence report, and SDD ledger.
2. Run exactly one serialized target rebuild and inspect link/section placement and coherency gates.
3. If target-green, launch the fresh CUE in desktop Ymir and compare behavior/FPS against the accepted 3–4 FPS rollback candidate.

