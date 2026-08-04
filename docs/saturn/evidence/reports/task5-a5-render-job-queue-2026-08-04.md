# Task 5 / A5 source-only queue report — 2026-08-04

## Scope and status

**Active, source-contract only.** This change creates the isolated shared
render-job queue ABI and its host/coherency gates. It deliberately does not
touch `saturn_demo_render.c`, so the concurrent A3 compact-cluster and A4
meshlet candidate remains byte-for-byte outside this task's scope. No target
build, CUE, Ymir launch, replay, counter, visual, or FPS claim was made.

## Contract

- Eight bounded 16-byte descriptors carry only type, static callback ID,
  snapshot generation, immutable input span, and disjoint output span/capacity.
  They contain no function pointer, live-game pointer, VDP pointer, or
  allocator state.
- A queue generation and each release record's generation/state/claim are
  32-bit. On SH-2 all queue access selects the P2 cache-through alias; claim
  acquisition is `tas.b`, with a host CAS equivalent.
- Descriptors are copied and validated (including non-overlapping output spans)
  before count, then queue generation, then `READY` release states publish.
  Stale/full/invalid/overlapping work fails closed.
- A claim revalidates under its lock and can become terminal only through its
  original master or slave claim state. Reset first requires the full
  generation to be terminal and then claims every release word; it cannot
  reuse an in-flight result span.

## Test evidence

| Gate | Result | Notes |
|---|---|---|
| Direct host C fixture | PASS | `gcc -std=c11 -Wall -Wextra -Werror -I src/port/saturn/gfx tools/saturn/render_job_queue_test.c src/port/saturn/gfx/saturn_render_job_queue.c` then `render-job-queue-test.exe`: racing host master/slave claimers give each job exactly one owner; stale/full/reset-before-terminal paths fail closed; master claims work while a slave claim remains occupied. |
| Queue coherency structural + mutation gate | PASS | `tools/saturn/verify_dual_cpu_coherency.py --queue-source ... --queue-header ... --self-test`: rejects cached state, a function-pointer descriptor, missing descriptor completion, non-final generation publication, reset before terminal retirement, and missing `tas.b`. |
| `make verify-render-job-queue` | UNEXECUTED / infrastructure-blocked | The audited MSYS wrapper reached its known `\\d\\Code...` Windows-Python root translation/access error while creating `build/saturn/host-tests`; it failed before compiling this target. Direct equivalent gates above are green, but this broader wrapper gate is not credited. |

## References and reuse

| Upstream | Pin | License | Files inspected | Reuse |
|---|---|---|---|---|
| SlaveDriver Engine | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later | `WALLS.C:1803-1950` | Pattern-only: persistent slave and disjoint result ownership. |
| SONIC Z-TREME | `cff75451c1616aac1236fc2b44223902b55c706b` | GPL-3.0 | `ZT_RENDERING.c:718-786`, `workarea.c:12-25` | Pattern-only: early dispatch and fixed non-overlapping work areas. |

No upstream scheduler code, ABI, data structure, or notice was copied. The
new queue is project-owned code; the existing GPL boundary is not expanded.

## Remaining gates

1. SH-2 polling consumer and static callback-ID table.
2. One-frame terrain/actor integration after the current A3/A4 candidate is
   reviewed/handed off; old fixed split stays diagnostic-only.
3. Queue/coherency/actor/cluster/runtime aggregate gates, two independent
   reviews, target cache/coherency evidence, deterministic replay, and manual
   target visual/counter/FPS evidence.
