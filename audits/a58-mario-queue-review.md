# Peer Audit — 2026-08-04 16:49

## Context

- Prior agent: Codex subagent
- Task claimed: Add dormant A5.8 Mario queue parity without changing the live renderer or Castle-proven animation path.
- Commit reviewed: `1d1137f17f3ccfaf7f33ba7ecb59e9fc41230f0c`
- Files examined: `src/port/saturn/gfx/saturn_demo_render.c`, `saturn_render_job_graph.{c,h}`, `saturn_render_job_queue.{c,h}`, `saturn_render_payload_bank.c`, `saturn_render_output_bank.c`, `tools/saturn/render_job_actor_route_source_test.c`, `tools/saturn/render_job_graph_test.c`, `Makefile.saturn.mk`, `ARCHITECTURE.md`, `STATE.md`, `CHANGELOG.md`, the active A5 plan, and its evidence report.
- Commands run: commit/status/diff inspection, focused `rg` mutation searches, `git diff --check`, and seven direct Qt MinGW C11 fixtures listed under Verification.

## Verdict

**GO for the dormant A5.8 Mario queue-parity source increment.** No Critical or Important source-scope defect was found. This verdict does not approve queue activation, a target build, a CUE, Ymir behavior, cache behavior on hardware, or an FPS claim.

## Findings

### Critical

None.

### Important

None.

### Minor

#### The actor route fixture is structural, not an executable callback-behavior harness

`tools/saturn/render_job_actor_route_source_test.c` proves that the intended calls and forbidden fixed-split tokens are present/absent, but it does not execute the static actor transform, classify, or terminal assembly functions. The graph, queue, bridge, runtime, and payload fixtures execute their reusable contracts, and manual inspection found the dormant renderer route coherent; nevertheless, activation should add a direct behavioral fixture or target proof for corrupt vertex identities, incomplete primitive coverage, stale sequence metadata, and cross-lane reads.

### Discrepancies between summary and code

None. The documentation accurately calls this source-only and dormant. It also accurately leaves the live-cutover fixture RED and defers the output-offset namespace: queue publication enforces globally disjoint descriptor spans, while the typed actor/terrain payload banks currently interpret offsets within separate bounded arrays. That must be resolved before publishing a combined production graph.

## What was done well

- `demo_snapshot_mario_transform_context()` copies the existing live posed vertex bank, light intensities, actor snapshot/yaw, animation frame/count/bank metadata, and immutable generated primitive/material references. It does not replace or simplify Mario animation.
- `demo_actor_queue_transform()` accepts only the complete `ACTOR_ADMIT` compact-reference span, binds output through the claimant-derived bridge, transforms the copied live pose, records explicit vertex identities, and publishes generation/job/count/sequence/claimant/writer-lane metadata before the runtime can mark the descriptor DONE.
- `demo_actor_queue_classify()` rejects malformed primitive spans, binds its own descriptor lane, proves exactly one DONE `ACTOR_ADMIT` dependency through the graph, reads that predecessor through the terminal payload bridge using the consumer's actual lane, and publishes its own terminal metadata. It contains no `begin == 0`, `s_actor_slave_begin`, `s_actor_primitive_slave_begin`, or `owner_for_split` inference.
- `demo_actor_queue_assemble_done()` is master-only. It first collects every exact DONE `ACTOR_LOWER`, requires contiguous complete primitive coverage, one common exact admit predecessor, consistent nonzero sequence, ordered descriptor/local identities, valid vertex identities, and valid primitive identities. Only after all checks succeed does it clear and rewrite the legacy projected/ref banks. This satisfies validate-before-mutate for renderer-visible state.
- The queue payload reads derive writer ownership from the P2-visible output bank and choose cached versus peer/cache-through aliases from the recorded claimant. Metadata arrays are in `.uncached`; bulk payload arrays remain bounded LWRAM banks.
- The legacy default path remains active. The new transform/classify/assemble functions are explicitly unused, there is no production `sm64_saturn_render_job_runtime_activate_graph()` call, and commit `1d1137f1` adds no `cpu_dual_slave_set()` or notification. Legacy actor-ref reads now use an explicit owner array which is initialized for the normal split and reset to master ownership on serial/failure recovery.
- The changelog, state, architecture, active plan, and evidence report describe why the increment exists, preserve the accepted 3–4 FPS rollback candidate, and do not overclaim a target or performance result.

## Verification

All commands ran from the exact `sh2-native-math-purge` worktree with `C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror` and passed:

```text
tools/saturn/render_job_actor_route_source_test.c
render job actor route source fixture: PASS

tools/saturn/render_job_terrain_route_source_test.c
render job terrain route source fixture: PASS

tools/saturn/render_job_graph_test.c + saturn_render_job_graph.c + saturn_render_job_queue.c
render job graph fixture: PASS

tools/saturn/render_job_queue_test.c + saturn_render_job_queue.c
render job queue fixture: PASS

tools/saturn/render_job_bridge_test.c + bridge/output-bank/queue sources
render job bridge fixture: PASS

tools/saturn/render_job_runtime_test.c + runtime/graph/queue sources
render job runtime fixture: PASS

tools/saturn/render_job_payload_bank_test.c + payload/bridge/output-bank/queue sources
render job payload bank fixture: PASS

git diff 1d1137f1^ 1d1137f1 --check
clean
```

The worktree contains pre-existing unrelated verifier, SDD-ledger, audio-prototype, temporary, and audit changes. This review did not modify them.

## Recommended next actions

1. Mark the A5.8 Mario parity increment source-reviewed GO in the active plan/evidence ledger.
2. Resolve the combined graph's output-offset namespace before activation; do not rely on type-local payload offsets while queue publication enforces one global disjoint namespace.
3. At live integration, publish the immutable callback context through an explicit P2/cache-through contract for both SH-2 consumers, then add behavioral corruption/cross-lane coverage for the actor callbacks.
4. Connect ordered terrain command lookup, perform the single atomic CPU-DUAL owner replacement, and keep the old worker only as an isolated diagnostic path.
5. Run the serialized target link/cache gates and desktop Ymir manual test before claiming runtime or performance improvement.
