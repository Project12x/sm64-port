# Peer Audit — A5.8 atomic live cutover (2026-08-04)

## Context

- Prior agent: Codex subagent
- Task claimed: Atomically replace the accepted fixed terrain/Mario worker path with one dependency-aware, descriptor-owned dual-SH-2 render graph.
- Commit reviewed: `a12236a9ecd90b37b87a7fffcb825943f69c877f`
- Files examined: the commit diff; `src/port/saturn/gfx/saturn_demo_render.c`; render-job queue, graph, runtime, bridge, output-bank, payload-bank, and callback-context sources and headers; the pinned libyaul CPU-DUAL implementation and API comments; all requested host fixtures; the active plan, design, evidence report, state, roadmap, changelog, and prerequisite A5.6/A5.7/A5.8 audits.
- Commands run: `git status --short`; `git show`/`git diff`/`rg` source inspection; nine direct Qt MinGW C11 `-Wall -Wextra -Werror` fixture builds and executions listed below; `git diff a12236a9^ a12236a9 --check`.

## Verdict

**NO-GO for target build or Ymir.** The registration, publication, terminal assembly, retirement, and master-only VDP1 boundaries are directionally correct, and every requested host fixture passes. However, the live terrain admit callback necessarily waits for a peer transform publication that the new four-job graph can never produce. After that is repaired, a stolen slave admit followed by a master lower can still read stale cached position-owner metadata. These are target-relevant correctness failures hidden by structural host fixtures.

## Findings

### Critical

#### 1. The sole WORLD_ADMIT descriptor waits for a nonexistent second transform producer

The frame constructs `classify` with `.dual_phase = true` (`src/port/saturn/gfx/saturn_demo_render.c:3678-3687`) and snapshots that flag unchanged into the terrain callback context (`:1801-1818`, `:3736-3738`). The graph publishes exactly one `WORLD_ADMIT` descriptor (`:3702-3731`). Its callback deliberately assigns every visible position to the actual claimant (`:2686-2694`) and calls `demo_transform_owned_positions()` once.

That transform helper publishes only the claimant lane, then—because `dual_phase` is true—waits for the opposite lane's release (`:1544-1567`). No other graph descriptor performs terrain transformation, so the peer release cannot appear. The callback spins up to 4,000,000 iterations, sets `s_transform_phase_failed`, returns false, and the runtime marks WORLD_ADMIT failed and quarantines WORLD_LOWER. Therefore the advertised live route cannot produce a complete terrain graph on Saturn.

Repair the queue contract so this single descriptor is explicitly a single-producer transform phase (`dual_phase=false` for the queue snapshot), or publish genuinely independent transform descriptors with a valid join. Add an executable callback-level fixture that proves one coarse admit returns successfully without a peer publication; the current live-cutover fixture only searches source tokens.

#### 2. A slave-owned WORLD_ADMIT followed by a master-owned WORLD_LOWER can select stale position payload lanes

The new callback allows either SH-2 to own all transformed positions and rewrites `s_position_owner` from that claimant (`src/port/saturn/gfx/saturn_demo_render.c:2686-2694`). But `s_position_owner` remains a cached LWRAM array (`:141-142`), and `demo_position_owner_read()` hard-codes producer lane 0 (`:465-472`). Consequently a master lower reads its own cached alias when `lane == 0`; it is not forced through P2 even when the slave wrote the owner map. The master can retain the prior frame's owner values and then choose the wrong cached/P2 aliases for `s_view`, `s_projected`, and `s_position_valid` (`:475-497`).

This violates the required actual-claimant ownership contract for the legal schedule slave WORLD_ADMIT -> master WORLD_LOWER. The admit release metadata correctly records its writer lane, but WORLD_LOWER merely validates that metadata (`:2742-2747`) and does not use it to select the transformed-position bank.

Make owner metadata unconditionally P2-visible, or derive all position reads in the coarse lower job directly from the exact DONE admit claimant (all positions have one owner in this graph). Add a cross-lane callback test for slave admit -> master lower across two generations, poisoning the master's cached owner map so the stale-read case is executable.

### Important

#### The source gates cannot substantiate the main live-renderer claim

`render_job_live_cutover_source_test.c`, the terrain route fixture, and the actor route fixture are structural token tests. They do not invoke the static renderer callbacks or frame lifecycle. The reusable queue/graph/runtime/bridge/payload/context fixtures are valuable, but they cannot catch either failure above because they use synthetic callbacks and host cache aliases are identity. A callback-level harness or the planned target runtime evidence is required before the plan can call Step 5 green.

### Minor

#### Several comments still call now-live routes dormant or future work

Examples include `saturn_demo_render.c:2088-2092`, `:2436-2439`, `:260-263`, `:2662-2665`, `:2702-2705`, and `:2807-2810`. This does not cause the runtime failure, but it makes ownership review materially harder after the atomic cutover. Update them in the repair commit.

### Discrepancies between summary and code

- The changelog says the renderer now "lets master and slave steal eligible admit/lower work" and completes terminal assembly. The scheduling API permits that, but the only terrain admit cannot complete because it retains the removed two-producer phase wait.
- The changelog says descriptor-owned results are assembled before final VDP1 lowering. That boundary is present, but the valid terrain graph needed to reach it is not.
- The evidence report says "a stolen coarse admit cannot inherit half of the removed logical split." It does assign the whole position set to the claimant, but it neither removes the obsolete peer wait nor makes the rewritten owner map safely readable by the other SH-2.
- The documentation correctly makes no post-cutover target, cache, Ymir, or FPS claim.

## What was done well

- `sm64_saturn_demo_render_init()` activates the graph runtime once, and repository search finds the sourceboot renderer's Yaul registration/notify only in `saturn_render_job_runtime.c`. The accepted frame no longer calls the legacy terrain worker or Mario transform dispatcher.
- The frame publishes all four immutable descriptors and both self-contained callback contexts before notify or master drain. The contexts contain inline dynamic terrain work order and Mario pose/reference data; peer consumers receive cache-through aliases.
- Queue claims use TAS-protected release records; output lanes are derived from the actual accepted claim. Terrain result and Mario payload banks validate exact DONE identity and claimant-derived writer lane.
- Master and slave use the same graph-aware drain and only claim dependency-eligible jobs. Failure propagation quarantines dependents.
- The master waits for a positive notified-slave return before reset/reuse. Libyaul's pinned polling trampoline blocks on MINIT, invokes the registered entry once per notification, and returns to its polling loop, matching the retirement token design.
- Terrain command-stream and Mario terminal assembly validate complete DONE coverage before mutating renderer-visible legacy banks. `sm64_saturn_vdp1_backend_begin()` remains after terminal validation and queue retirement, and no failed-generation serial replay exists.

## Verification

All requested host gates independently compiled with `C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror` and passed:

```text
render_job_live_cutover_source_test.c: PASS (exit 0; no success text)
render_job_terrain_route_source_test.c: PASS
render_job_actor_route_source_test.c: PASS
render_job_runtime_test.c + runtime/graph/queue: PASS
render_job_queue_test.c + queue: PASS
render_job_graph_test.c + graph/queue: PASS
render_job_bridge_test.c + bridge/output-bank/queue: PASS
render_job_payload_bank_test.c + payload/bridge/output-bank/queue: PASS
render_callback_context_test.c + callback-context/queue: PASS
git diff a12236a9^ a12236a9 --check: PASS
```

No Saturn target build or Ymir run was performed or credited.

## Recommended next actions

1. Remove the obsolete two-lane transform rendezvous from the single-descriptor queue admit path while preserving it only for the isolated legacy diagnostic adapter.
2. Make transformed-position owner selection safe for the legal slave-admit/master-lower schedule, using the exact DONE admit claimant or unconditional P2 owner metadata.
3. Add executable callback-level regressions for single-admit completion and poisoned-cache slave-admit/master-lower handoff; keep the structural cutover gate as a separate ownership guard.
4. Rerun all nine strict host gates and obtain fresh independent re-review. Only then run the serialized Saturn target build and desktop-Ymir test.

