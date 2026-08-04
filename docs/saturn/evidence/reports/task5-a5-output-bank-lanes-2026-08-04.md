# A5 descriptor-owned output lanes — source-only evidence

Date: 2026-08-04
Scope: A5 prerequisite only; no renderer integration, target build, CUE, or
Ymir execution.

## Decision

The queue descriptor kind owns the output-bank class: `WORLD_*` maps to
terrain, `ACTOR_*` maps to actor. The CPU that successfully claims the job is
the only CPU allowed to publish the output lane. Readers recover that published
owner and use P2/cache-through only for a peer's output. This replaces unsafe
logical-range ownership (`begin == 0`) without changing the master’s VDP1
lowering or stable painter order.

## Reference basis

| Upstream | Pin / license | Files inspected | Reuse mode |
| --- | --- | --- | --- |
| Lobotomy-Software/SlaveDriver-Engine | `a8986591557b6e680550d3c23970284d3b38ff8f` / GPL-3.0-or-later | `WALLS.C:1803-1950` | Pattern-only: fixed capacity, disjoint result ownership and cache-through handoff. No upstream scheduler code copied. |
| Maxime-XL2/SONIC-Z-TREME | `cff75451c1616aac1236fc2b44223902b55c706b` / GPL-3.0 | `ZT_RENDERING.c:406-505,718-786` | Pattern-only: bounded persistent SH-2 work areas. No source copied. |

## Test record

| Gate | Result | What it proves |
| --- | --- | --- |
| Watched RED host compile | PASS (expected failure) | The test failed because the output-bank header/source did not exist. |
| `render_output_bank_test.c` | PASS | Descriptor-kind bank choice; claimed-CPU lane; master steal; peer cache-through; mismatch/overwrite failure; two-thread exact-one publisher. |
| `verify_dual_cpu_coherency.py --output-bank-source ... --self-test` | PASS | Five structural mutations cannot weaken P2, `tas.b`, release order, reader aliasing, or eliminate logical-range lane inference. |

## Remaining gates

- Independent specification and code-quality review.
- Bind the bank records to live terrain/actor callbacks, deleting the accepted
  `begin == 0` lane inference rather than retaining two producer paths.
- Confirm master-only VDP1 command lowering and stable painter order during
  queue integration.
- Target SH-2 cache/coherency, visual, counter, and FPS evidence.

Commit: `0026a3a1` (`feat(saturn): publish descriptor-owned output lanes`).
