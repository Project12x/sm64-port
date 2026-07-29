# SH-2 Native Math Purge — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development.
> This plan is the successor to `2026-07-26-soft-float-replacement-design.md`
> and supersedes its Stage 2+. Owner policy, verbatim intent: **soft-fp is not
> the answer — SlaveDriver and Z-Treme ship zero runtime float.** Where float
> remains, the plan is conversion or bake, never faster emulation.

**Goal:** Zero soft-float, zero 64-bit division, zero libm calls in any
per-frame or per-tick hot path. The SH-2 executes only what it does natively:
32-bit integer add/sub/shift, `muls/dmuls.l/mac.l` multiply, scheduled DIVU
division, table lookups. Cold paths (boot, level transition, rare branches)
may keep soft-float, per the sm64-psx precedent — hot paths may not.

**The enemy list (what the SH-2 handles badly):**
| Operation | Today | Native replacement |
| --- | --- | --- |
| `float` +,−,× | soft-fp calls | Q16.16 (render) / Q-format (sim) via `dmuls.l`+`xtrct` |
| `float` ÷, `1/w` | `__divsf3` | scheduled DIVU (start/work/collect — kernels landed) |
| `double` anything | promotion chains | ban outright; no legitimate hot use exists |
| `sinf/cosf/sqrtf/atan2` | libm soft-float | `saturn_trig_q16` tables + integer sqrt (both exist) |
| 64-bit ÷ / % | `__divdi3` | restructure to 32-bit or shift; DIVU 64/32 where it fits |
| float↔int churn | `__floatsisf`/`__fixsfsi` | keep data in Q-format end-to-end; conversions only at bake |

## Task 0: Census gate — measure, then make regression impossible
- Extend `verify_q16_sh2_disassembly.py` into `verify_sh2_native_math.py`:
  objdump the linked demo ELF, attribute every `jsr` to a soft-float/libm/
  64-div symbol back to its calling function (the literal-pool + addr2line
  method from the call-site map, which found 3,099 refs: 2,886 `src/game`,
  141 `src/engine`, 19 port-owned).
- Output: per-function count, tagged HOT (reachable per-frame/per-tick on the
  route) vs COLD. The HOT total is the plan's single success metric.
- Wire into demo-path `make verify` with an explicit allowlist file; every
  task below shrinks the allowlist and the gate enforces the shrink. Same
  failed-first discipline as the coherency gate.

## Task 1: Render residue → Q16 (port-owned, no engine edits)
The demo renderer's remaining float (perspective divide already DIVU'd;
sweep whatever the census tags in `saturn_demo_render.c`, `saturn_ir_*`,
actor bridge). Acceptance: route checkpoint unchanged, pixel-diff vs current
frame within stated bbox tolerance, census HOT count for `src/port/` = **0**.

## Task 2: `math_util.c` + `sqrtf/sinf/cosf` call sites (engine seam #1; resliced)

> **Owner reslice — 2026-07-29:** Task 2 closes at the measured
> `atan2s`/`atan2_lookup` seam. Its implementation and technical review are
> complete through `3f249d8`. This deliberately does **not** claim completion
> for the broader original `math_util`/trig scope.

- Delivered scope: `TARGET_SATURN`-guarded Q16 `atan2s` and
  `atan2_lookup`, with host differential/mutation gates and two independent,
  role-bound 2,000-tick target A/B captures.
- Explicitly deferred follow-on scope: hot vec3 operations, `approach_*`, and
  `sqrtf`/`sinf`/`cosf` call sites. Re-estimate and schedule these after the
  camera seam rather than silently treating them as complete.
- The verified 1-ulp `sinf/cosf` from scratchpad `rejected-fix2/` becomes
  live only if a float path survives a later stage; otherwise trig goes
  straight to `saturn_trig_q16` tables.
- Methodology for every future function remains host-differential versus the
  float original on real captured inputs (mtxq pattern), tolerance derived
  rather than picked, plus route checkpoint and 2,000-tick
  positional-divergence gate (< 1.0 world unit).

## Task 3: Camera (`camera.c`, 382 refs — the sim's biggest single pocket)
Convert the per-tick camera math to Q-format behind the seam. The sm64-psx
artifact to test against explicitly: neutral-stick drift (their deadzone
paper-over). Our gate: bit-stable idle camera over 600 ticks.

## Task 4: Behaviors by heat (`behaviors/*`, 1,218 refs across 130 files)
Census-ranked, top files first (`obj_behaviors`, `mario_actions_moving`...);
convert only what the route actually executes per tick; leave cold behaviors
on soft-float with a COLD tag. This is where sm64-psx spent most of its ~112
files — expect the same shape, but measured file-by-file instead of wholesale.

## Task 5: Format decision + 16-bit narrowing (needs Task 0 data first)
- **Open question the census answers:** one Q16.16 everywhere vs Q20.12 for
  sim (sm64-psx's choice — world coords up to ±524k at 1/4096 precision).
  Decide from BOB's actual coordinate ranges + overflow-guard telemetry, not
  by inheritance. Record as an owner-visible decision either way.
- Range-analysis-driven 16-bit vertex/record narrowing in banks (the
  shipping engines' habit) — bake-time, behind the existing schema versioning.

## Standing rules
- Every conversion: differential fixture + mutation pass + route checkpoint.
  No expected-value edits to make a test fit.
- Bake beats convert: any hot math whose inputs are static moves offline
  before it gets a Q-format twin.
- References: Jo `jo_fixed_mult` (landed), SlaveDriver DIVU (landed),
  Z-Treme fixed-point patterns (adoptable), sm64-psx behaviour-only file map.
  AW rules from `2026-07-27-demo-path-first.md` apply, including per-task
  reference-consumption tables.
- Sequencing with the rendering-stability work: Tasks 0–1 can start now;
  Tasks 2–4 should not land while the swap-timing/coherency masking bug is
  open, or their visual effect is unjudgeable. The census (Task 0) is
  useful immediately regardless.

**Exit:** census HOT count = 0 outside the allowlist; route FPS re-measured
(absolute, with emulation-speed ratio); sim tick ms re-measured; the
soft-float library remains linked only for COLD paths, exactly as sm64-psx
proved is acceptable.
