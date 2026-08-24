# Quad-pairing headroom on BOB terrain — measured, and it is not the lever

**Date:** 2026-08-23 · Measured through the real bake, not a reconstruction.

## Why it looked promising

Saturn draws quads natively; every unpaired triangle ships as a degenerate quad
and wastes half a VDP1 primitive. BOB bakes to **867 primitives from 1,101
source triangles — only 234 quads against 633 triangle fallbacks**, i.e. 42% of
triangles paired. `pairing_forbidden_triangles` is **0**, so nothing is being
refused by policy; they simply fail the pairing tests.

Primitive count is the right currency: VDP1 *fill* is not the frame limiter
(measured), command emission is, and command cost is per-primitive. Cutting
primitives also attacks **both** master-only spans at once — fewer clusters to
admit in the pre-notification window and fewer commands to emit in finalize —
which no amount of further slave offloading can do.

## Where the rejections actually are

From `bob_area1_report.json` at the shipped tolerance (256):

| reason | count |
|---|---:|
| `uv_affine_error` | **532** |
| `planar_quad_not_convex` | 60 |
| `normal_divergence` | 27 |
| `uv_cycle_degenerate` | 2 |

84% of failures are the textured-affine test. VDP1 maps a texture affinely
across a quad's four corners while the N64 source is perspective-correct
per-triangle, so the bake refuses pairs whose UVs would warp. The test is a UV
parallelogram residual, `(u0+u2-u1-u3)² + (v0+v2-v1-v3)²`, rejected above
`limit²` (`quad_pairing.py:194`), and the limit is a plain default —
`DEFAULT_TEXTURED_AFFINE_ERROR_LIMIT = 256`, which BOB does not override.

That framing suggested a large, purely bake-side win available from one
constant: accept some texture warping, halve the primitives.

## What it is actually worth

Re-baked at **1024**, a 4x looser tolerance:

| tolerance | quads | triangles | primitives |
|---|---:|---:|---:|
| 256 (shipped) | 234 | 633 | **867** |
| 1024 | 285 | 531 | **816** |

**5.9%.** Not 30%.

And the rejection profile moves rather than shrinking:

| reason | at 256 | at 1024 |
|---|---:|---:|
| `uv_affine_error` | 532 | 321 |
| `planar_quad_not_convex` | 60 | **187** |
| `normal_divergence` | 27 | 48 |

Relaxing the UV test only exposes triangles that then fail the **geometric**
tests. The binding constraint is not the texture mapping — it is that BOB's
triangle soup largely is not pairs of coplanar, convex, similarly-facing
triangles in the first place. Loosening the UV tolerance further buys
progressively less while paying in visible texture warp on everything it does
pair.

## Conclusion

**Quad-pairing tolerance is not the FPS lever.** Roughly 6% of primitives for a
4x tolerance increase and a real visual cost is a bad trade, and the ceiling is
geometric, not tunable.

This does **not** weaken the wider "reduce primitives" thesis — it narrows it to
the technique that does not depend on the source geometry being quad-friendly:

- **Terrain decimation (S7.1)** removes triangles outright instead of merging
  them, so the convexity and normal-divergence ceilings do not apply. It is
  owner-approved at 75% ("75% looks nearly identical; 50% does not") and
  unlanded. On these numbers it is now the strongest remaining bake-side lever.
- The N64 geo walk (S1.10) remains the runtime-side counterpart.

## Reproducing

Set `DEFAULT_TEXTURED_AFFINE_ERROR_LIMIT` in `tools/saturn/quad_pairing.py`,
run a full sourceboot build, and read `quad_count` / `standalone_triangle_count`
from `build/saturn/sourceboot/generated/bob_area1_report.json`. Note the
targeted `source-bob-mesh` target alone did **not** regenerate the report in
testing; the full build did. Restore the constant afterwards — the shipped value
is 256.
