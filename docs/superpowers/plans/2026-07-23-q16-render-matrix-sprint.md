# Q16.16 Render-Matrix Sprint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove SH-2 soft-float from the render-graph matrix pipeline entirely — every matrix the rendering path composes (camera lookat, per-node rotate/translate/billboard/scale, the modelview stack, and the display-list handoff) becomes Q16.16 integer math, eliminating the toolchain soft-float miscompilation that is the last blocker to visible pixels.

**Architecture:** SM64's render graph funnels all matrix work through one uniform choke point: every `geo_process_*` builds a local matrix with an `mtxf_*` constructor, composes it into `gMatStack[]` with `mtxf_mul`, then converts to the display-list `Mtx` via `mtxf_to_mtx`. This sprint adds a parallel Q16.16 stack (`gMatStackQ[]`) maintained under `TARGET_SATURN` at those exact call sites, using the port's already-proven `sm64_saturn_mtx_t`/`sm64_saturn_matrix_mul` (Q16.16, 64-bit accumulate, overflow-flagged — `saturn_matrix.h`). New Q16.16 constructor kernels (lookat, rotate, billboard, scale, translate) are host-differential-tested against the float originals before integration. The display-list `Mtx` wire format switches to native Q16.16 (`s32[4][4]`, same 64 bytes) on Saturn, so the port's Fast3D frontend reads it with zero float conversion; the few non-render-graph float `Mtx` producers (perspective, skybox, paintings, HUD) route through an exact float→Q16 converter that uses no floating-point arithmetic (exponent-manipulation + int conversion only). Float `gMatStack` entries are refreshed *from* the Q16 truth for the handful of engine consumers that read them (shadows, culling, held objects), so no engine logic changes meaning.

This is platform adaptation, not gameplay change: the same computation, executed with the arithmetic the CPU was designed for. SH-2 has no FPU but has native 32×32→64 signed multiply (`dmuls.l`) and MAC — exactly what Q16.16 needs. This matches Sega's own SGL (whose `FIXED` is the identical 16.16 format and whose `slLookAt(FIXED*, FIXED*, ANGLE)` is precisely a fixed-point camera lookat — `work/upstream/sonic-z-treme/Documentation/DOC/210A_US/MATH.TXT:313`, `SL_DEF.H:909`), Sonic Z-Treme (built entirely on that fixed-point API), and SlaveDriver (integer/fixed math with a dedicated sqrt table, `work/upstream/slavedriver-engine/FLASH/SQRTTAB.H`). Kernels here are clean-room implementations grounded in those references' *behavior* (no code copied — SGL is proprietary doc-study-only; SlaveDriver/Z-Treme reuse is authorized but keeping the kernel header GPL-free avoids license-mixing next to SM64-adjacent code).

**Tech Stack:** Host-native C tests (`tools/saturn/runtime_contract_test.c` + the proven `mtxf_lookat_host_diff_test.c` differential-harness pattern), Python codegen in `.venv-saturn-tools`, Yaul SH-2 cross-compile (pinned GCC 14.3.0 — unchanged by this sprint; the whole point is to stop depending on its soft-float), `ymir-headless` + `capture_hwtest.py` live verification.

**Evidence this plan rests on** (all from this project's own committed docs/captures):
- `docs/saturn/evidence/e2-sourceboot-gmatstack-corruption-2026-07-22.md`: corruption originates in `gMatStack[1]` (camera lookat output), inputs proven non-degenerate, algorithm exonerated on host across all 65,536 rolls, GCC 14.3.0 `fp-bit.c` soft-float implicated; GCC 15.2.0 has a *different* miscompile of the same function at `-Os` — neither toolchain's float path is trustworthy here.
- Free-roam state is live: `mario_action == ACT_IDLE`, correct level identity, input works (`1f76b66` chain). Only rendering is blocked.
- `docs/saturn/SGL_REFERENCE_NOTES.md:95`: "Q16.16 fixed point — SGL `FIXED` is the identical 16.16 format (`MATH.TXT`)".

---

### Task 1: Q16.16 trig table codegen

SM64's trig is table-driven: `sins(x) = gSineTable[(u16)(x) >> 4]` with `gCosineTable = gSineTable + 0x400` (`src/engine/math_util.h:20-27`), tables in `include/trig_tables.inc.c` (`f32 gSineTable[]` then `f32 gCosineTable[0x1000]`, laid out adjacently so cosine reads overflow into the second table). A Q16.16 mirror table must hold the *converted values of the exact f32 table*, not freshly-computed ideal sines — semantic preservation against the source engine, same determinism rule as everything else in this port.

**Files:**
- Create: `tools/saturn/gen_trig_q16.py`
- Create (generated, committed): `src/port/saturn/gfx/saturn_trig_q16.inc.c`
- Modify: `Makefile.saturn.mk`
- Test: `tools/saturn/test_gen_trig_q16.py`

- [x] **Step 1: Write the failing test**

Create `tools/saturn/test_gen_trig_q16.py`:

```python
import re
import unittest
from pathlib import Path

import gen_trig_q16

REPO = Path(__file__).resolve().parents[2]


class GenTrigQ16Test(unittest.TestCase):
    def test_parses_all_f32_entries(self):
        floats = gen_trig_q16.parse_trig_tables(
            REPO / "include" / "trig_tables.inc.c")
        # gSineTable[0x400] + gCosineTable[0x1000] = 0x1400 entries total,
        # matching math_util.h's overlapped-adjacent layout.
        self.assertEqual(len(floats), 0x1400)
        self.assertEqual(floats[0], 0.0)          # sin(0)
        self.assertEqual(floats[0x400], 1.0)      # cos(0)

    def test_q16_conversion_is_round_to_nearest(self):
        self.assertEqual(gen_trig_q16.to_q16(0.0), 0)
        self.assertEqual(gen_trig_q16.to_q16(1.0), 65536)
        self.assertEqual(gen_trig_q16.to_q16(-1.0), -65536)
        self.assertEqual(gen_trig_q16.to_q16(0.5), 32768)
        # round-half-away-from-zero, symmetric
        self.assertEqual(gen_trig_q16.to_q16(1.52587890625e-05), 1)

    def test_generated_file_matches_source_table(self):
        generated = REPO / "src" / "port" / "saturn" / "gfx" / \
            "saturn_trig_q16.inc.c"
        self.assertTrue(generated.exists(),
                        "run gen_trig_q16.py before this test")
        values = [int(v) for v in re.findall(
            r"(-?\d+),", generated.read_text())]
        floats = gen_trig_q16.parse_trig_tables(
            REPO / "include" / "trig_tables.inc.c")
        self.assertEqual(len(values), 0x1400)
        for i, f in enumerate(floats):
            self.assertEqual(values[i], gen_trig_q16.to_q16(f),
                             f"entry {i} stale -- regenerate")


if __name__ == "__main__":
    unittest.main(verbosity=2)
```

- [x] **Step 2: Run it to confirm it fails**

Run: `./.venv-saturn-tools/Scripts/python.exe tools/saturn/test_gen_trig_q16.py` (from `sm64-port/`)
Expected: FAIL — `ModuleNotFoundError: No module named 'gen_trig_q16'`. (This
project's Saturn tool tests are plain scripts, not pytest — see
`tools/saturn/test_tools.py`'s `if __name__ == "__main__": unittest.main(verbosity=2)`
and `Makefile.saturn.mk`'s `verify-tools` target, which runs it directly.
There is no pytest in `tools/saturn/requirements.txt` and no pytest anywhere
in this repo — do not add it.)

- [x] **Step 3: Write the generator**

Create `tools/saturn/gen_trig_q16.py`:

```python
"""Generate a Q16.16 mirror of SM64's f32 sine/cosine tables.

The engine's trig is table-driven (math_util.h: sins(x) =
gSineTable[(u16)(x) >> 4], gCosineTable = gSineTable + 0x400). The Saturn
render-matrix kernels need the SAME values in Q16.16 -- converted from the
exact f32 table entries, not recomputed ideal sines, so fixed-point results
stay semantically anchored to the source engine's own data.

Output is a committed generated file; re-run this tool only if
include/trig_tables.inc.c ever changes (it is original game data and
should not).
"""
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "include" / "trig_tables.inc.c"
OUTPUT = REPO / "src" / "port" / "saturn" / "gfx" / "saturn_trig_q16.inc.c"

FLOAT_RE = re.compile(r"(-?(?:\d+\.\d*(?:e[+-]?\d+)?f?|0x[0-9a-fA-F.p+-]+f?|\d+f?))\s*,")


def parse_trig_tables(path):
    # include/trig_tables.inc.c holds gSineTable then gCosineTable back to
    # back (0x400 + 0x1000 = 0x1400 f32 entries) -- but immediately after
    # that comes a THIRD array, gArctanTable (s16, bare hex integers like
    # 0x000A). That table must never enter this scan: a hex literal with
    # no A-F letters (e.g. 0x0014) fails FLOAT_RE's "0x" branch as a whole,
    # but the scanner then resumes right after the failed "0x" and matches
    # the plain-decimal branch against the trailing digits alone ("0014"
    # -> 14.0), silently inflating the count. Bounding the scan to end at
    # gArctanTable's declaration keeps this correct regardless of what any
    # regex alternative could match on data that was never meant to be read.
    text = path.read_text()
    region = text[:text.index("gArctanTable")]
    values = []
    for raw in FLOAT_RE.findall(region):
        raw = raw.rstrip("f")
        values.append(float.fromhex(raw) if raw.startswith(("0x", "-0x"))
                      else float(raw))
    return values


def to_q16(f):
    scaled = f * 65536.0
    # round half away from zero, symmetric for negative values
    q = int(scaled + 0.5) if scaled >= 0 else -int(-scaled + 0.5)
    assert -(1 << 31) <= q < (1 << 31), f"{f} out of Q16.16 range"
    return q


def main():
    floats = parse_trig_tables(SOURCE)
    if len(floats) != 0x1400:
        print(f"ERROR: expected 0x1400 table entries, parsed {len(floats)}",
              file=sys.stderr)
        return 1
    lines = [
        "/* GENERATED by tools/saturn/gen_trig_q16.py -- do not edit.",
        " * Q16.16 conversion of include/trig_tables.inc.c's exact f32",
        " * values (round half away from zero). Layout mirrors",
        " * math_util.h: entries [0x000..0x3FF] are the sine table's",
        " * first quadrant span, [0x400..0x13FF] the cosine table, with",
        " * sine reads at index >= 0x400 intentionally overflowing into",
        " * the cosine data exactly as the f32 original does. */",
        "#include <stdint.h>",
        "",
        "const int32_t gSaturnSineTableQ16[0x1400] = {",
    ]
    for i in range(0, 0x1400, 8):
        chunk = ", ".join(str(to_q16(f)) for f in floats[i:i + 8])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    OUTPUT.write_text("\n".join(lines))
    print(f"wrote {OUTPUT} ({len(floats)} entries)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [x] **Step 4: Generate and re-run the test**

Run: `./.venv-saturn-tools/Scripts/python.exe tools/saturn/gen_trig_q16.py && ./.venv-saturn-tools/Scripts/python.exe tools/saturn/test_gen_trig_q16.py`
Expected: `wrote .../saturn_trig_q16.inc.c (5120 entries)` then unittest's
verbosity=2 output (each test name + `ok`, ending in `Ran 3 tests in ...s`
/ `OK`).
If the parse count is not 0x1400, STOP and inspect `trig_tables.inc.c`'s real
layout rather than forcing the regex — the count assertion exists to catch
exactly that.

- [x] **Step 5: Wire into the tool test suite**

This repo's real convention (confirmed in `Makefile.saturn.mk`) is NOT pytest
discovery — `verify-tools` runs `tools/saturn/test_tools.py` directly as a
plain script:

```makefile
verify-tools: check-host-tools
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_tools.py"
```

Add a second line for the new test file, in the same style (same quoting,
same variable usage):

```makefile
verify-tools: check-host-tools
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_tools.py"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_gen_trig_q16.py"
```

Do not merge `test_gen_trig_q16.py`'s test classes into `test_tools.py` —
keep it a separate file, per this task's own file list.

Run: `make -f Makefile.saturn.mk verify-tools SATURN_TOOLS_PYTHON=./.venv-saturn-tools/Scripts/python.exe` (msys2 bash, `cd` into `sm64-port` first)
Expected: exit 0, output from both scripts, `test_gen_trig_q16.py`'s 3 tests
included and passing.

- [x] **Step 6: Commit**

```bash
git add tools/saturn/gen_trig_q16.py tools/saturn/test_gen_trig_q16.py \
        src/port/saturn/gfx/saturn_trig_q16.inc.c Makefile.saturn.mk
git commit -m "feat(saturn): generate Q16.16 mirror of the engine trig tables"
```

---

### Task 2: Q16.16 math kernel primitives

**Files:**
- Create: `src/port/saturn/gfx/saturn_matrix_kernels.h`
- Test: `tools/saturn/runtime_contract_test.c` (append)

- [ ] **Step 1: Write the failing tests**

Append to `tools/saturn/runtime_contract_test.c`, after the last existing
test function and before `main()` (include `"saturn_matrix_kernels.h"` next
to the existing `"saturn_matrix.h"` include at the top of the file):

```c
static void test_kernels_isqrt64(void)
{
    assert(sm64_saturn_isqrt64(0) == 0);
    assert(sm64_saturn_isqrt64(1) == 1);
    assert(sm64_saturn_isqrt64(4) == 2);
    assert(sm64_saturn_isqrt64(15) == 3);   /* floor */
    assert(sm64_saturn_isqrt64(16) == 4);
    assert(sm64_saturn_isqrt64(65536) == 256);
    /* Q16.16 usage shape: sqrt of a Q32 value yields Q16.
     * (2.25 in Q32) = 9663676416; sqrt = 98304 = 1.5 in Q16.16. */
    assert(sm64_saturn_isqrt64(9663676416LL) == 98304);
    /* large world-scale magnitudes stay exact */
    assert(sm64_saturn_isqrt64((int64_t) 60000 * 60000) == 60000);
}

static void test_kernels_q16_mul(void)
{
    assert(sm64_saturn_q16_mul(1 << 16, 1 << 16) == (1 << 16));
    assert(sm64_saturn_q16_mul(3 << 16, 1 << 15) == (3 << 15)); /* 3*0.5 */
    assert(sm64_saturn_q16_mul(-(1 << 16), 1 << 16) == -(1 << 16));
    assert(sm64_saturn_q16_mul(0, 12345678) == 0);
}

static void test_kernels_float_q16_roundtrip(void)
{
    /* the exact-conversion helpers must use no float multiply/divide --
     * verified by reading the implementation; these tests pin values. */
    assert(sm64_saturn_float_to_q16(1.0f) == 65536);
    assert(sm64_saturn_float_to_q16(-1.0f) == -65536);
    assert(sm64_saturn_float_to_q16(0.0f) == 0);
    assert(sm64_saturn_float_to_q16(0.5f) == 32768);
    assert(sm64_saturn_float_to_q16(4864.0f) == 4864 << 16);
    assert(sm64_saturn_q16_to_float(65536) == 1.0f);
    assert(sm64_saturn_q16_to_float(-65536) == -1.0f);
    assert(sm64_saturn_q16_to_float(0) == 0.0f);
    assert(sm64_saturn_q16_to_float(4864 << 16) == 4864.0f);
    /* saturation instead of UB at the format ceiling */
    assert(sm64_saturn_float_to_q16(40000.0f) == INT32_MAX);
    assert(sm64_saturn_float_to_q16(-40000.0f) == INT32_MIN);
}

static void test_kernels_trig_lookup(void)
{
    /* sins(0)=0, coss(0)=1; sins(0x4000)=1 (90 deg).
     * Table layout mirrors math_util.h exactly: index = (u16)angle >> 4,
     * cosine = sine index + 0x400. */
    assert(sm64_saturn_sins_q16(0) == 0);
    assert(sm64_saturn_coss_q16(0) == 65536);
    assert(sm64_saturn_sins_q16(0x4000) == 65536);
    assert(sm64_saturn_coss_q16(0x4000) == 0);
    /* negative angle wraps through u16 exactly like the engine macro */
    assert(sm64_saturn_sins_q16(-0x4000) == -65536);
}
```

Register in `main()` after the last existing frontend test call:

```c
    test_kernels_isqrt64();
    test_kernels_q16_mul();
    test_kernels_float_q16_roundtrip();
    test_kernels_trig_lookup();
```

- [ ] **Step 2: Run to confirm failure**

Run (msys2 bash): `cd /d/Code/RetroDev/sm64-saturn-port/sm64-port && export OS=Windows_NT && make -f Makefile.saturn.mk verify-runtime-contracts SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe`
Expected: FAIL to compile — `saturn_matrix_kernels.h: No such file or directory`.

- [ ] **Step 3: Implement the kernel header**

Create `src/port/saturn/gfx/saturn_matrix_kernels.h`:

```c
#ifndef SM64_SATURN_MATRIX_KERNELS_H
#define SM64_SATURN_MATRIX_KERNELS_H

#include <stdint.h>
#include <string.h>

#include "saturn_matrix.h"

/* Q16.16 integer math primitives for the Saturn render-matrix pipeline.
 *
 * WHY THIS EXISTS: live evidence (2026-07-22, see
 * docs/saturn/evidence/e2-sourceboot-gmatstack-corruption-2026-07-22.md)
 * proved the pinned SH-2 toolchain (GCC 14.3.0) miscompiles/corrupts the
 * engine's soft-float matrix math (mtxf_lookat/mtxf_mul feeding
 * gMatStack) for real, non-degenerate camera inputs, and GCC 15.2.0 has
 * a different miscompilation of the same function at -Os. SH-2 has no
 * FPU; every float op is a libgcc soft-float call. These kernels remove
 * that dependency entirely for the rendering matrix path: 32-bit integer
 * and Q16.16 fixed-point only, using SH-2's native 32x32->64 multiply.
 *
 * PRECEDENT: Sega's own SGL uses the identical 16.16 FIXED format for
 * this exact job -- slLookAt(FIXED*, FIXED*, ANGLE) is a fixed-point
 * camera lookat (SGL MATH.TXT, SL_DEF.H:909; docs/saturn/
 * SGL_REFERENCE_NOTES.md), Sonic Z-Treme builds on that API, and
 * SlaveDriver ships integer sqrt tables (FLASH/SQRTTAB.H). These
 * kernels are clean-room implementations of the standard algorithms --
 * behavior-grounded in those references, no code copied (SGL is
 * proprietary doc-study-only; keeping this header GPL-free avoids
 * mixing GPL next to SM64-adjacent code).
 *
 * Host-testable by design (no Yaul dependency), same as saturn_matrix.h. */

extern const int32_t gSaturnSineTableQ16[0x1400];

/* Mirrors math_util.h's macros exactly: index = (u16)angle >> 4,
 * cosine = sine + 0x400, sine reads at high indices intentionally
 * overflowing into the cosine data. */
static inline int32_t sm64_saturn_sins_q16(int32_t angle)
{
    return gSaturnSineTableQ16[(uint16_t) angle >> 4];
}

static inline int32_t sm64_saturn_coss_q16(int32_t angle)
{
    return gSaturnSineTableQ16[((uint16_t) angle >> 4) + 0x400];
}

/* Q16.16 * Q16.16 -> Q16.16 via 64-bit intermediate (SH-2 dmuls.l). */
static inline int32_t sm64_saturn_q16_mul(int32_t a, int32_t b)
{
    return (int32_t) (((int64_t) a * (int64_t) b) >> 16);
}

/* Floor integer square root of a non-negative 64-bit value.
 * Bit-by-bit method: pure shifts/compares, no multiply, no float.
 * Q-format usage: isqrt64(Q32 value) yields a Q16 result (sqrt halves
 * the scale exponent); isqrt64(integer) yields integer. */
static inline int64_t sm64_saturn_isqrt64(int64_t v)
{
    int64_t rem = v;
    int64_t root = 0;
    int64_t bit = (int64_t) 1 << 62;

    if (v <= 0) {
        return 0;
    }
    while (bit > rem) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (rem >= root + bit) {
            rem -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* Exact float <-> Q16.16 conversion WITHOUT floating-point arithmetic.
 *
 * Rationale: the entire reason these kernels exist is that this
 * toolchain's soft-float *arithmetic* (multiply/add) is proven broken.
 * Pure int<->float *conversions* (_fixsfsi/_floatsisf) are separate,
 * far simpler libgcc routines with no evidence of defect -- but scaling
 * by 65536 must NOT introduce a float multiply. Both helpers therefore
 * scale by adjusting the IEEE-754 exponent field directly (+/-16), a
 * pure integer bit operation, then use only the bare conversion.
 *
 * float_to_q16: out-of-range magnitudes saturate to INT32_MAX/MIN
 * (positions beyond +-32767 world units are outside this port's
 * documented range assumption -- see saturn_matrix.h's decode note --
 * and saturating beats the UB of a raw out-of-range _fixsfsi). */
static inline int32_t sm64_saturn_float_to_q16(float f)
{
    union { float f; uint32_t u; } bits;
    int32_t exponent;

    bits.f = f;
    if ((bits.u & 0x7FFFFFFFu) == 0) {
        return 0;
    }
    exponent = (int32_t) ((bits.u >> 23) & 0xFF);
    /* magnitude >= 2^15 would scale past Q16.16's ceiling: saturate.
     * (biased exponent 127+15 = 142) */
    if (exponent >= 142) {
        return (bits.u & 0x80000000u) ? INT32_MIN : INT32_MAX;
    }
    /* subnormals and tiny values (< 2^-16 after scaling would still be
     * fractional): exponent + 16 <= 0 means |f| < 2^-127+... -- after
     * the +16 scale these are < 1 ulp of Q16.16 only when the scaled
     * exponent underflows; the bare conversion truncates them to 0,
     * which is the desired behavior. Guard only against exponent-field
     * underflow while rebiasing. */
    if (exponent + 16 > 254) {
        return (bits.u & 0x80000000u) ? INT32_MIN : INT32_MAX;
    }
    bits.u = (bits.u & 0x807FFFFFu) | ((uint32_t) (exponent + 16) << 23);
    return (int32_t) bits.f; /* single _fixsfsi, truncates toward zero */
}

static inline float sm64_saturn_q16_to_float(int32_t q)
{
    union { float f; uint32_t u; } bits;
    int32_t exponent;

    if (q == 0) {
        return 0.0f;
    }
    bits.f = (float) q; /* single _floatsisf; |q| < 2^31 always valid */
    exponent = (int32_t) ((bits.u >> 23) & 0xFF);
    bits.u = (bits.u & 0x807FFFFFu) | ((uint32_t) (exponent - 16) << 23);
    return bits.f;
}

#endif
```

- [ ] **Step 4: Add the table object to the host test build**

`saturn_trig_q16.inc.c` defines `gSaturnSineTableQ16`. The host test link
needs it: in `Makefile.saturn.mk`'s `verify-runtime-contracts` recipe, add
`src/port/saturn/gfx/saturn_trig_q16.inc.c` to the compile line (it already
compiles `saturn_fast3d_frontend.c` alongside the test — add the new file
the same way).

- [ ] **Step 5: Run the tests**

Same command as Step 2. Expected: exit 0, silent.

- [ ] **Step 6: Commit**

```bash
git add src/port/saturn/gfx/saturn_matrix_kernels.h \
        tools/saturn/runtime_contract_test.c Makefile.saturn.mk
git commit -m "feat(saturn): Q16.16 math kernel primitives (isqrt, trig, exact float bridge)"
```

---

### Task 3: Q16.16 matrix constructors with host differential tests

The render graph uses exactly these constructors (inventory from
`rendering_graph_node.c`, verified by grep this session): `mtxf_identity`
(:1076), `mtxf_lookat` (:327), `mtxf_rotate_zxy_and_translate` (:353, :378,
:401, :821), `mtxf_rotate_xyz_and_translate` (:592), `mtxf_billboard`
(:451, :818), `mtxf_scale_vec3f` (:426, :454, :457, :825, :903),
`mtxf_translate` (:698, :897), `mtxf_mul` (compose, many sites),
`mtxf_rotate_xy` (:323, HUD ortho, writes an `Mtx` directly). Identity and
mul already exist in `saturn_matrix.h`. This task implements the rest, each
differential-tested on host against the real float original.

**Files:**
- Create: `src/port/saturn/gfx/saturn_matrix_ctors.h`
- Create: `tools/saturn/mtxq_ctor_diff_test.c`
- Modify: `Makefile.saturn.mk`

- [ ] **Step 1: Write the differential test harness (failing)**

Create `tools/saturn/mtxq_ctor_diff_test.c` — same structure as the proven
`mtxf_lookat_host_diff_test.c` (compiles the real `src/engine/math_util.c`
on host as ground truth):

```c
/* Host differential test: Q16.16 render-matrix constructors vs. the real
 * float originals in src/engine/math_util.c, compiled natively.
 *
 * Tolerance model: constructors' rotation entries are unit-range; the
 * Q16.16 kernels use the trig table converted from the same f32 data, so
 * agreement should be within a few Q16 ulps for trig-driven entries.
 * Translation entries are exact conversions. Lookat involves
 * normalization (integer sqrt + 64-bit divide vs. float 1/sqrtf), so its
 * unit-vector entries get a wider documented tolerance. Every tolerance
 * is asserted, not just printed -- a regression fails the suite. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "engine/math_util.h"

#include "saturn_matrix.h"
#include "saturn_matrix_kernels.h"
#include "saturn_matrix_ctors.h"

#define Q16_TOL_TRIG   4        /* ulps, table-identical trig */
#define Q16_TOL_NORM   64       /* ulps (~0.001), sqrt/divide chains */
#define Q16_TOL_TRANS  1        /* ulps, exact conversions */

static int32_t f_to_q(float f) { return sm64_saturn_float_to_q16(f); }

static void assert_close(int32_t got, float want_f, int32_t tol,
                         const char *what, int i, int j)
{
    int64_t want = f_to_q(want_f);
    int64_t diff = (int64_t) got - want;
    if (diff < 0) diff = -diff;
    if (diff > tol) {
        fprintf(stderr, "%s [%d][%d]: got %d want %lld (diff %lld > %d)\n",
                what, i, j, got, (long long) want, (long long) diff, tol);
        exit(1);
    }
}

static void diff_lookat(Vec3f from, Vec3f to, s16 roll)
{
    Mat4 want;
    sm64_saturn_mtx_t got;
    int32_t fq[3] = { f_to_q(from[0]), f_to_q(from[1]), f_to_q(from[2]) };
    int32_t tq[3] = { f_to_q(to[0]), f_to_q(to[1]), f_to_q(to[2]) };

    mtxf_lookat(want, from, to, roll);
    sm64_saturn_mtxq_lookat(&got, fq, tq, roll);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            /* row 3 (translation-ish dot products) scales with position
             * magnitude; normalized rows use the norm tolerance */
            assert_close(got.m[i][j], want[i][j],
                         (i == 3) ? Q16_TOL_NORM * 16 : Q16_TOL_NORM,
                         "lookat", i, j);
        }
    }
    assert(got.m[0][3] == 0 && got.m[1][3] == 0 && got.m[2][3] == 0);
    assert(got.m[3][3] == (1 << 16));
}

int main(void)
{
    /* Fixture 1: the exact real captured camera state from the live
     * corruption evidence (e2-sourceboot-gmatstack-corruption doc). */
    {
        Vec3f from = { -7208.26318359375f, 264.13934326171875f, 7050.0f };
        Vec3f to = { -6566.8955078125f, 124.66311645507812f, 6454.2001953125f };
        diff_lookat(from, to, 0);
    }
    /* Fixture 2: roll sweep at the same position (64 evenly spaced) */
    {
        Vec3f from = { -7208.26318359375f, 264.13934326171875f, 7050.0f };
        Vec3f to = { -6566.8955078125f, 124.66311645507812f, 6454.2001953125f };
        for (int r = 0; r < 65536; r += 1024) {
            diff_lookat(from, to, (s16) r);
        }
    }
    /* Fixture 3: BOB spawn-scale positions, varied axes */
    {
        Vec3f from = { -6558.0f, 850.0f, 6464.0f };
        Vec3f to = { -6558.0f, 0.0f, 5000.0f };
        diff_lookat(from, to, 0);
    }

    /* rotate_zxy / rotate_xyz: full-turn sweeps on each axis */
    for (int a = 0; a < 65536; a += 4096) {
        Mat4 want;
        sm64_saturn_mtx_t got;
        Vec3f t = { 123.0f, -456.0f, 789.0f };
        Vec3s r3 = { (s16) a, (s16) (a * 3), (s16) (a * 7) };
        int32_t tq[3] = { f_to_q(t[0]), f_to_q(t[1]), f_to_q(t[2]) };

        mtxf_rotate_zxy_and_translate(want, t, r3);
        sm64_saturn_mtxq_rotate_zxy_and_translate(&got, tq, r3[0], r3[1], r3[2]);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                assert_close(got.m[i][j], want[i][j], Q16_TOL_TRIG * 4,
                             "rot_zxy", i, j);
        for (int j = 0; j < 3; j++)
            assert_close(got.m[3][j], want[3][j], Q16_TOL_TRANS,
                         "rot_zxy_t", 3, j);

        mtxf_rotate_xyz_and_translate(want, t, r3);
        sm64_saturn_mtxq_rotate_xyz_and_translate(&got, tq, r3[0], r3[1], r3[2]);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                assert_close(got.m[i][j], want[i][j], Q16_TOL_TRIG * 4,
                             "rot_xyz", i, j);
    }

    /* billboard: uses a camera matrix -- feed the real lookat output */
    {
        Mat4 cam_f;
        sm64_saturn_mtx_t cam_q, got;
        Mat4 want;
        Vec3f from = { -7208.26f, 264.14f, 7050.0f };
        Vec3f to = { -6566.90f, 124.66f, 6454.20f };
        Vec3f pos = { -6558.0f, 100.0f, 6464.0f };
        int32_t pq[3] = { f_to_q(pos[0]), f_to_q(pos[1]), f_to_q(pos[2]) };
        int32_t fq[3] = { f_to_q(from[0]), f_to_q(from[1]), f_to_q(from[2]) };
        int32_t tq[3] = { f_to_q(to[0]), f_to_q(to[1]), f_to_q(to[2]) };

        mtxf_lookat(cam_f, from, to, 0);
        sm64_saturn_mtxq_lookat(&cam_q, fq, tq, 0);
        for (int a = 0; a < 65536; a += 8192) {
            mtxf_billboard(want, cam_f, pos, (s16) a);
            sm64_saturn_mtxq_billboard(&got, &cam_q, pq, (s16) a);
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    assert_close(got.m[i][j], want[i][j], Q16_TOL_TRIG,
                                 "billboard_rot", i, j);
            for (int j = 0; j < 3; j++)
                assert_close(got.m[3][j], want[3][j], Q16_TOL_NORM * 16,
                             "billboard_pos", 3, j);
        }
    }

    /* scale + translate: exact-ish */
    {
        Mat4 base_f, want;
        sm64_saturn_mtx_t base_q, got;
        Vec3f s = { 0.5f, 2.0f, 1.25f };
        Vec3f t = { 10.0f, -20.0f, 30.0f };
        int32_t sq[3] = { f_to_q(s[0]), f_to_q(s[1]), f_to_q(s[2]) };
        int32_t tq[3] = { f_to_q(t[0]), f_to_q(t[1]), f_to_q(t[2]) };

        mtxf_identity(base_f);
        sm64_saturn_matrix_identity(&base_q);
        mtxf_scale_vec3f(want, base_f, s);
        sm64_saturn_mtxq_scale_vec3f(&got, &base_q, sq);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                assert_close(got.m[i][j], want[i][j], Q16_TOL_TRIG,
                             "scale", i, j);

        mtxf_translate(want, t);
        sm64_saturn_mtxq_translate(&got, tq);
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                assert_close(got.m[i][j], want[i][j], Q16_TOL_TRANS,
                             "translate", i, j);
    }

    printf("mtxq ctor differential: all fixtures within tolerance\n");
    return 0;
}
```

- [ ] **Step 2: Add the Makefile target and confirm compile failure**

In `Makefile.saturn.mk`, add `verify-mtxq-ctors` mirroring the existing
`verify-mtxf-lookat-host-diff` recipe (same flags/includes — it already
compiles `math_util.c` with host stubs for `find_floor`/`guMtxF2L`/
`gVec3fZero`; reuse that exact stub approach), compiling
`tools/saturn/mtxq_ctor_diff_test.c src/engine/math_util.c
src/port/saturn/gfx/saturn_trig_q16.inc.c` and add it to whatever aggregate
target `verify-mtxf-lookat-host-diff` is (or is not) part of — keep it a
standalone target exactly like that precedent, for the same reason
(documented in that recipe's comment: it tests engine math, not port
contracts).

Run it. Expected: FAIL — `saturn_matrix_ctors.h: No such file or directory`.

- [ ] **Step 3: Implement the constructors**

Create `src/port/saturn/gfx/saturn_matrix_ctors.h`:

```c
#ifndef SM64_SATURN_MATRIX_CTORS_H
#define SM64_SATURN_MATRIX_CTORS_H

#include "saturn_matrix.h"
#include "saturn_matrix_kernels.h"

/* Q16.16 constructors mirroring src/engine/math_util.c's mtxf_* matrix
 * builders, formula-for-formula (same row/column conventions, same sign
 * choices, same output layout), in pure integer arithmetic. See
 * saturn_matrix_kernels.h's header comment for why (toolchain soft-float
 * proven broken) and the SGL/Z-Treme/SlaveDriver precedent for this
 * being the architecturally correct Saturn approach.
 *
 * UNIT ANALYSIS (load-bearing -- do not change casually):
 * - Positions/translations arrive as Q16.16 (+-32767 world-unit range,
 *   same documented assumption as saturn_matrix.h's decode).
 * - mtxq_lookat reduces position deltas to INTEGER world units (>>16)
 *   before squaring: BOB-scale deltas (up to ~60000 units) square to
 *   ~3.6e9 and sum x3 within int64 trivially, whereas squaring raw
 *   Q16.16 deltas ((60000<<16)^2 * 3 = 1.16e19) would overflow int64
 *   (max 9.2e18). Direction = (Q16.16 delta) / (integer magnitude)
 *   yields a Q16.16 unit component directly, one 64/32 divide per
 *   component (libgcc __divdi3 -- exact integer, not soft-float).
 * - Sub-unit truncation from the >>16 delta reduction shifts direction
 *   by at most 1 part in the magnitude (irrelevant at world scale;
 *   documented, accepted for bring-up). */

/* Normalize a Q16.16 3-vector in place. Squares of unit-range Q16
 * components are Q32 (fit int64 with huge margin summed x3);
 * isqrt64(Q32) = Q16 magnitude; component/(magnitude) rescaled by <<16
 * keeps Q16.16. Zero vector: left unchanged (caller guards). */
static inline void sm64_saturn_q16_vec3_normalize(int32_t v[3])
{
    int64_t mag2 = (int64_t) v[0] * v[0] + (int64_t) v[1] * v[1]
                 + (int64_t) v[2] * v[2];
    int64_t mag = sm64_saturn_isqrt64(mag2); /* Q16 */
    if (mag == 0) {
        return;
    }
    v[0] = (int32_t) (((int64_t) v[0] << 16) / mag);
    v[1] = (int32_t) (((int64_t) v[1] << 16) / mag);
    v[2] = (int32_t) (((int64_t) v[2] << 16) / mag);
}

/* Mirrors mtxf_lookat (math_util.c:194-266) including its exact
 * negative-reciprocal convention (colZ points from->to NEGATED) and
 * roll handling. from/to are Q16.16 world positions; roll is the same
 * s16 angle the engine uses. */
static inline void
sm64_saturn_mtxq_lookat(sm64_saturn_mtx_t *mtx, const int32_t from[3],
                        const int32_t to[3], int16_t roll)
{
    int32_t colX[3], colY[3], colZ[3];
    int64_t dxi, dzi, mag;
    int32_t dx_q = to[0] - from[0];
    int32_t dz_q = to[2] - from[2];

    /* horizontal direction, integer-unit magnitude (see unit analysis) */
    dxi = (int64_t) (dx_q >> 16);
    dzi = (int64_t) (dz_q >> 16);
    mag = sm64_saturn_isqrt64(dxi * dxi + dzi * dzi);
    if (mag == 0) {
        mag = 1;
    }
    /* float code: d *= -1/len. Negated Q16.16 unit components: */
    dx_q = (int32_t) (-((int64_t) dx_q) / mag);
    dz_q = (int32_t) (-((int64_t) dz_q) / mag);

    colY[0] = sm64_saturn_q16_mul(sm64_saturn_sins_q16(roll), dz_q);
    colY[1] = sm64_saturn_coss_q16(roll);
    colY[2] = -sm64_saturn_q16_mul(sm64_saturn_sins_q16(roll), dx_q);

    /* full look direction, same integer-unit reduction, negated */
    {
        int32_t vx = to[0] - from[0];
        int32_t vy = to[1] - from[1];
        int32_t vz = to[2] - from[2];
        int64_t xi = vx >> 16, yi = vy >> 16, zi = vz >> 16;
        mag = sm64_saturn_isqrt64(xi * xi + yi * yi + zi * zi);
        if (mag == 0) {
            mag = 1;
        }
        colZ[0] = (int32_t) (-((int64_t) vx) / mag);
        colZ[1] = (int32_t) (-((int64_t) vy) / mag);
        colZ[2] = (int32_t) (-((int64_t) vz) / mag);
    }

    /* colX = colY x colZ; renormalize (float code divides by +len) */
    colX[0] = sm64_saturn_q16_mul(colY[1], colZ[2])
            - sm64_saturn_q16_mul(colY[2], colZ[1]);
    colX[1] = sm64_saturn_q16_mul(colY[2], colZ[0])
            - sm64_saturn_q16_mul(colY[0], colZ[2]);
    colX[2] = sm64_saturn_q16_mul(colY[0], colZ[1])
            - sm64_saturn_q16_mul(colY[1], colZ[0]);
    sm64_saturn_q16_vec3_normalize(colX);

    /* colY = colZ x colX; renormalize (matches float code's final
     * recompute -- order matters, keep it) */
    colY[0] = sm64_saturn_q16_mul(colZ[1], colX[2])
            - sm64_saturn_q16_mul(colZ[2], colX[1]);
    colY[1] = sm64_saturn_q16_mul(colZ[2], colX[0])
            - sm64_saturn_q16_mul(colZ[0], colX[2]);
    colY[2] = sm64_saturn_q16_mul(colZ[0], colX[1])
            - sm64_saturn_q16_mul(colZ[1], colX[0]);
    sm64_saturn_q16_vec3_normalize(colY);

    for (int c = 0; c < 3; c++) {
        mtx->m[0][c] = (c == 0) ? colX[0] : (c == 1) ? colY[0] : colZ[0];
    }
    /* write in the float code's exact layout: column c of the rotation
     * lives at m[row][c] with rows = x/y/z components */
    mtx->m[0][0] = colX[0]; mtx->m[1][0] = colX[1]; mtx->m[2][0] = colX[2];
    mtx->m[0][1] = colY[0]; mtx->m[1][1] = colY[1]; mtx->m[2][1] = colY[2];
    mtx->m[0][2] = colZ[0]; mtx->m[1][2] = colZ[1]; mtx->m[2][2] = colZ[2];

    /* translation row: -(from . col) per column, 64-bit dot products */
    for (int c = 0; c < 3; c++) {
        int64_t dot = (int64_t) from[0] * mtx->m[0][c]
                    + (int64_t) from[1] * mtx->m[1][c]
                    + (int64_t) from[2] * mtx->m[2][c];
        mtx->m[3][c] = (int32_t) (-(dot >> 16));
    }
    mtx->m[0][3] = 0;
    mtx->m[1][3] = 0;
    mtx->m[2][3] = 0;
    mtx->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_rotate_zxy_and_translate (math_util.c:272-299). */
static inline void
sm64_saturn_mtxq_rotate_zxy_and_translate(sm64_saturn_mtx_t *dest,
                                          const int32_t translate[3],
                                          int16_t rx, int16_t ry, int16_t rz)
{
    const int32_t sx = sm64_saturn_sins_q16(rx), cx = sm64_saturn_coss_q16(rx);
    const int32_t sy = sm64_saturn_sins_q16(ry), cy = sm64_saturn_coss_q16(ry);
    const int32_t sz = sm64_saturn_sins_q16(rz), cz = sm64_saturn_coss_q16(rz);
    const int32_t sxsy = sm64_saturn_q16_mul(sx, sy);
    const int32_t sxcy = sm64_saturn_q16_mul(sx, cy);

    dest->m[0][0] = sm64_saturn_q16_mul(cy, cz)
                  + sm64_saturn_q16_mul(sxsy, sz);
    dest->m[1][0] = -sm64_saturn_q16_mul(cy, sz)
                  + sm64_saturn_q16_mul(sxsy, cz);
    dest->m[2][0] = sm64_saturn_q16_mul(cx, sy);
    dest->m[3][0] = translate[0];

    dest->m[0][1] = sm64_saturn_q16_mul(cx, sz);
    dest->m[1][1] = sm64_saturn_q16_mul(cx, cz);
    dest->m[2][1] = -sx;
    dest->m[3][1] = translate[1];

    dest->m[0][2] = -sm64_saturn_q16_mul(sy, cz)
                  + sm64_saturn_q16_mul(sxcy, sz);
    dest->m[1][2] = sm64_saturn_q16_mul(sy, sz)
                  + sm64_saturn_q16_mul(sxcy, cz);
    dest->m[2][2] = sm64_saturn_q16_mul(cx, cy);
    dest->m[3][2] = translate[2];

    dest->m[0][3] = dest->m[1][3] = dest->m[2][3] = 0;
    dest->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_rotate_xyz_and_translate (math_util.c:305-334). */
static inline void
sm64_saturn_mtxq_rotate_xyz_and_translate(sm64_saturn_mtx_t *dest,
                                          const int32_t b[3],
                                          int16_t rx, int16_t ry, int16_t rz)
{
    const int32_t sx = sm64_saturn_sins_q16(rx), cx = sm64_saturn_coss_q16(rx);
    const int32_t sy = sm64_saturn_sins_q16(ry), cy = sm64_saturn_coss_q16(ry);
    const int32_t sz = sm64_saturn_sins_q16(rz), cz = sm64_saturn_coss_q16(rz);
    const int32_t sxsy = sm64_saturn_q16_mul(sx, sy);
    const int32_t cxsy = sm64_saturn_q16_mul(cx, sy);

    dest->m[0][0] = sm64_saturn_q16_mul(cy, cz);
    dest->m[0][1] = sm64_saturn_q16_mul(cy, sz);
    dest->m[0][2] = -sy;
    dest->m[0][3] = 0;

    dest->m[1][0] = sm64_saturn_q16_mul(sxsy, cz)
                  - sm64_saturn_q16_mul(cx, sz);
    dest->m[1][1] = sm64_saturn_q16_mul(sxsy, sz)
                  + sm64_saturn_q16_mul(cx, cz);
    dest->m[1][2] = sm64_saturn_q16_mul(sx, cy);
    dest->m[1][3] = 0;

    dest->m[2][0] = sm64_saturn_q16_mul(cxsy, cz)
                  + sm64_saturn_q16_mul(sx, sz);
    dest->m[2][1] = sm64_saturn_q16_mul(cxsy, sz)
                  - sm64_saturn_q16_mul(sx, cz);
    dest->m[2][2] = sm64_saturn_q16_mul(cx, cy);
    dest->m[2][3] = 0;

    dest->m[3][0] = b[0];
    dest->m[3][1] = b[1];
    dest->m[3][2] = b[2];
    dest->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_billboard (math_util.c:342-365): camera-facing rotation
 * plus the object position transformed through the camera matrix. */
static inline void
sm64_saturn_mtxq_billboard(sm64_saturn_mtx_t *dest,
                           const sm64_saturn_mtx_t *mtx,
                           const int32_t position[3], int16_t angle)
{
    dest->m[0][0] = sm64_saturn_coss_q16(angle);
    dest->m[0][1] = sm64_saturn_sins_q16(angle);
    dest->m[0][2] = 0;
    dest->m[0][3] = 0;

    dest->m[1][0] = -dest->m[0][1];
    dest->m[1][1] = dest->m[0][0];
    dest->m[1][2] = 0;
    dest->m[1][3] = 0;

    dest->m[2][0] = 0;
    dest->m[2][1] = 0;
    dest->m[2][2] = 1 << 16;
    dest->m[2][3] = 0;

    for (int c = 0; c < 3; c++) {
        int64_t dot = (int64_t) mtx->m[0][c] * position[0]
                    + (int64_t) mtx->m[1][c] * position[1]
                    + (int64_t) mtx->m[2][c] * position[2];
        dest->m[3][c] = (int32_t) (dot >> 16) + mtx->m[3][c];
    }
    dest->m[3][3] = 1 << 16;
}

/* Mirrors mtxf_scale_vec3f (math_util.c:538-547). */
static inline void
sm64_saturn_mtxq_scale_vec3f(sm64_saturn_mtx_t *dest,
                             const sm64_saturn_mtx_t *mtx,
                             const int32_t s[3])
{
    for (int i = 0; i < 4; i++) {
        dest->m[0][i] = sm64_saturn_q16_mul(mtx->m[0][i], s[0]);
        dest->m[1][i] = sm64_saturn_q16_mul(mtx->m[1][i], s[1]);
        dest->m[2][i] = sm64_saturn_q16_mul(mtx->m[2][i], s[2]);
        dest->m[3][i] = mtx->m[3][i];
    }
}

/* Mirrors mtxf_translate (math_util.c:181-192): identity + position. */
static inline void
sm64_saturn_mtxq_translate(sm64_saturn_mtx_t *dest, const int32_t b[3])
{
    sm64_saturn_matrix_identity(dest);
    dest->m[3][0] = b[0];
    dest->m[3][1] = b[1];
    dest->m[3][2] = b[2];
}

/* Mirrors mtxf_rotate_xy (math_util.c:596+): Z-axis screen roll used by
 * the HUD/ortho camera node. Writes a full matrix; the caller converts
 * to the wire Mtx exactly like every other constructor. */
static inline void
sm64_saturn_mtxq_rotate_xy(sm64_saturn_mtx_t *dest, int16_t angle)
{
    sm64_saturn_matrix_identity(dest);
    dest->m[0][0] = sm64_saturn_coss_q16(angle);
    dest->m[0][1] = sm64_saturn_sins_q16(angle);
    dest->m[1][0] = -dest->m[0][1];
    dest->m[1][1] = dest->m[0][0];
}

#endif
```

Note the stray first `for` loop in `mtxq_lookat` above (`for (int c = 0; c < 3; c++) { mtx->m[0][c] = ... }`) is redundant with the explicit writes that follow — **do not include it**; implement only the explicit column writes. (Called out so the implementer deletes it rather than copying the redundancy.)

- [ ] **Step 4: Run the differential test**

Run the new `verify-mtxq-ctors` target. Expected: exit 0,
`mtxq ctor differential: all fixtures within tolerance`.
If a tolerance fails: investigate before loosening — the tolerances encode
real precision claims. A lookat unit-vector off by more than ~0.001 means a
unit-analysis bug (likely the `>>16` reduction or a missed negation), not
an acceptable rounding difference.

- [ ] **Step 5: Run the full existing suites (no regressions)**

`verify-runtime-contracts` and `verify-mtxf-lookat-host-diff` both still
exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/port/saturn/gfx/saturn_matrix_ctors.h \
        tools/saturn/mtxq_ctor_diff_test.c Makefile.saturn.mk
git commit -m "feat(saturn): Q16.16 render-matrix constructors, host-differential-tested"
```

---

### Task 4: Render-graph integration under TARGET_SATURN

**Files:**
- Modify: `src/game/rendering_graph_node.c`
- Modify: `src/game/game_init.c` (guMtxF2L替 for non-graph producers — see Step 5)

This is the engine-boundary task. The change is mechanical and uniform, but
touches protected engine code — the rule making it legitimate: **every
`#ifdef TARGET_SATURN` branch computes the same mathematical result as the
float code it shadows, and the float code remains compiled and authoritative
for every other platform.** No gameplay-visible decision changes.

- [ ] **Step 1: Verify the call-site inventory is still exact**

```
grep -n "mtxf_lookat\|mtxf_mul(\|mtxf_rotate\|mtxf_billboard\|mtxf_scale\|mtxf_translate\|mtxf_identity\|mtxf_to_mtx\|throwMatrix" src/game/rendering_graph_node.c
```
Expected sites (from this plan's grounding pass): camera :323-331,
translate_rotate :353-357, translate :378-382, rotation :401-405,
scale :426-429, billboard :451-462, animated_part :592-596,
shadow :698-701, object+throwMatrix :815-840, held_object :897-911,
root :1076-1077, plus the `Mtx *mtx = gMatStackFixed[...]` reads at
:186/:271/:276. If any NEW site exists that this plan doesn't cover, STOP
and report before editing — the plan's edits must cover every site.

- [ ] **Step 2: Add the parallel Q16 stack and bridge helpers**

In `rendering_graph_node.c`, directly below the existing
`Mtx *gMatStackFixed[32];` (line ~41):

```c
#ifdef TARGET_SATURN
#include "port/saturn/gfx/saturn_matrix.h"
#include "port/saturn/gfx/saturn_matrix_ctors.h"

/* Q16.16 shadow of gMatStack, maintained in lockstep at every
 * composition site below. On Saturn this is the AUTHORITATIVE matrix
 * state: the toolchain's soft-float is proven to corrupt the float
 * stack's math (docs/saturn/evidence/
 * e2-sourceboot-gmatstack-corruption-2026-07-22.md), so the float
 * gMatStack entries are REFRESHED FROM these Q16 results (exact
 * conversion, no float arithmetic) for the engine consumers that read
 * them (shadow positioning, culling, held-object math). The wire Mtx
 * (gMatStackFixed) is written from Q16 directly -- see
 * saturn_mtxq_write_wire below and the frontend's matching native-Q16
 * decode (SATURN_MTX_IS_Q16). */
static sm64_saturn_mtx_t gMatStackQ[32];

static void saturn_mtxq_refresh_float_mirror(s16 index) {
    for (s32 i = 0; i < 4; i++) {
        for (s32 j = 0; j < 4; j++) {
            gMatStack[index][i][j] =
                sm64_saturn_q16_to_float(gMatStackQ[index].m[i][j]);
        }
    }
}

/* Write the Q16 matrix into the display-list Mtx slot. Mtx under
 * GBI_FLOATS is struct { float m[4][4] } -- 64 bytes, same size/shape
 * as s32[4][4]. On Saturn the wire format IS Q16.16 raw (the port's
 * own frontend is the only consumer; it decodes natively under
 * SATURN_MTX_IS_Q16, no float ever touched). memcpy avoids the
 * type-pun UB. */
static void saturn_mtxq_write_wire(Mtx *dest, const sm64_saturn_mtx_t *src) {
    memcpy(dest, src->m, sizeof(src->m));
}

static void saturn_vec3f_to_q16(int32_t out[3], Vec3f in) {
    out[0] = sm64_saturn_float_to_q16(in[0]);
    out[1] = sm64_saturn_float_to_q16(in[1]);
    out[2] = sm64_saturn_float_to_q16(in[2]);
}

static void saturn_mat4_to_q16(sm64_saturn_mtx_t *out, Mat4 in) {
    for (s32 i = 0; i < 4; i++) {
        for (s32 j = 0; j < 4; j++) {
            out->m[i][j] = sm64_saturn_float_to_q16(in[i][j]);
        }
    }
}
#endif
```

(`memcpy` needs `<string.h>` — `rendering_graph_node.c` includes it via the
engine headers; verify with the build, add the include if not.)

- [ ] **Step 3: Shadow every composition site**

The pattern is identical at every site: after (or instead of) the float
composition, perform the Q16 composition into `gMatStackQ`, refresh the
float mirror, and write the wire Mtx from Q16. The float `mtxf_*` calls are
LEFT IN PLACE on Saturn only where their outputs feed gameplay-visible
reads this sprint doesn't convert (none do at these sites — the float
stack's only remaining consumers are the mirror reads, which the refresh
covers), so each site's float math is replaced under `#ifdef TARGET_SATURN`
with `#else` keeping the original. Exact edits, one per site:

**Camera (:323-331)** — original:
```c
    mtxf_rotate_xy(rollMtx, node->rollScreen);
    ...
    mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);
    mtxf_mul(gMatStack[gMatStackIndex + 1], cameraTransform, gMatStack[gMatStackIndex]);
    ...
    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
    gMatStackFixed[gMatStackIndex] = mtx;
```
becomes (the `rollMtx` write at :323 is a direct `Mtx` producer for the
ortho HUD node — handle it with the Q16 ctor + wire write):
```c
#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t rollQ;
        sm64_saturn_mtxq_rotate_xy(&rollQ, node->rollScreen);
        saturn_mtxq_write_wire(rollMtx, &rollQ);
    }
#else
    mtxf_rotate_xy(rollMtx, node->rollScreen);
#endif
    ...
#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t camQ;
        int32_t posQ[3], focusQ[3];
        saturn_vec3f_to_q16(posQ, node->pos);
        saturn_vec3f_to_q16(focusQ, node->focus);
        sm64_saturn_mtxq_lookat(&camQ, posQ, focusQ, node->roll);
        (void)sm64_saturn_matrix_mul(&camQ, &gMatStackQ[gMatStackIndex],
                                     &gMatStackQ[gMatStackIndex + 1]);
    }
#else
    mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);
    mtxf_mul(gMatStack[gMatStackIndex + 1], cameraTransform, gMatStack[gMatStackIndex]);
#endif
```
and after the `gMatStackIndex++` that follows (keep reading the original
control flow — the increment sits between the mul and the mtxf_to_mtx):
```c
#ifdef TARGET_SATURN
    saturn_mtxq_refresh_float_mirror(gMatStackIndex);
    saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
    gMatStackFixed[gMatStackIndex] = mtx;
```

**IMPORTANT ordering note for every site:** `sm64_saturn_matrix_mul(a, b, out)`
computes `a * b` in row-major — matching `mtxf_mul(dest, a, b)`'s
`dest = a * b`. The argument order in the shadow calls above preserves the
original's operand order exactly. Do not swap.

**Translate+rotate (:353-357), translate (:378-382), rotate (:401-405)** —
same shape; each becomes:
```c
#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t nodeQ;
        int32_t tQ[3];
        saturn_vec3f_to_q16(tQ, translation);
        sm64_saturn_mtxq_rotate_zxy_and_translate(&nodeQ, tQ,
            node->rotation[0], node->rotation[1], node->rotation[2]);
        (void)sm64_saturn_matrix_mul(&nodeQ, &gMatStackQ[gMatStackIndex],
                                     &gMatStackQ[gMatStackIndex + 1]);
    }
#else
    mtxf_rotate_zxy_and_translate(mtxf, translation, node->rotation);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);
#endif
```
(for the translate-only site pass the zero rotation exactly as the float
code does with `gVec3sZero` — literal zeros; for the rotate-only site pass
`gVec3fZero`-equivalent zero translation `{0,0,0}`), followed at each
site's `mtxf_to_mtx` by the same refresh+wire-write pattern as the camera
site.

**Scale (:426-429)**:
```c
#ifdef TARGET_SATURN
    {
        int32_t sQ[3];
        saturn_vec3f_to_q16(sQ, scaleVec);
        sm64_saturn_mtxq_scale_vec3f(&gMatStackQ[gMatStackIndex + 1],
                                     &gMatStackQ[gMatStackIndex], sQ);
    }
#else
    mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], scaleVec);
#endif
```
plus refresh+wire-write at its mtxf_to_mtx.

**Billboard (:451-462)** — note the float original writes IN PLACE at
`gMatStackIndex` (no +1), reading `gMatStackIndex - 1`; mirror that exactly:
```c
#ifdef TARGET_SATURN
    {
        int32_t tQ[3];
        saturn_vec3f_to_q16(tQ, translation);
        sm64_saturn_mtxq_billboard(&gMatStackQ[gMatStackIndex],
                                   &gMatStackQ[gMatStackIndex - 1], tQ,
                                   gCurGraphNodeCamera->roll);
        if (gCurGraphNodeHeldObject != NULL) {
            int32_t sQ[3];
            saturn_vec3f_to_q16(sQ, gCurGraphNodeHeldObject->objNode->header.gfx.scale);
            sm64_saturn_mtxq_scale_vec3f(&gMatStackQ[gMatStackIndex],
                                         &gMatStackQ[gMatStackIndex], sQ);
        } else if (gCurGraphNodeObject != NULL) {
            int32_t sQ[3];
            saturn_vec3f_to_q16(sQ, gCurGraphNodeObject->scale);
            sm64_saturn_mtxq_scale_vec3f(&gMatStackQ[gMatStackIndex],
                                         &gMatStackQ[gMatStackIndex], sQ);
        }
    }
#else
    /* original three calls */
#endif
```
(match the float original's exact conditional structure when writing this —
read the real code at the site rather than trusting this sketch's guess at
the two scale branches' conditions).

**Animated part (:592-596)**: same as translate+rotate but with
`mtxf_rotate_xyz_and_translate` → `sm64_saturn_mtxq_rotate_xyz_and_translate`.

**Shadow (:698-701)** — original composes `mtxf_translate(mtxf, shadowPos)`
then `mtxf_mul(gMatStack[gMatStackIndex], mtxf, *gCurGraphNodeCamera->matrixPtr)`.
`matrixPtr` points at a float `gMatStack` entry — its Q16 twin is
`gMatStackQ[camera's index]`. The camera node stores
`node->matrixPtr = &gMatStack[gMatStackIndex]` when processed; ADD alongside
it (in the camera site edit) a parallel
`static sm64_saturn_mtx_t *sSaturnCameraMatrixQ;` assignment
(`sSaturnCameraMatrixQ = &gMatStackQ[gMatStackIndex];`), then here:
```c
#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t tQm;
        int32_t tQ[3];
        saturn_vec3f_to_q16(tQ, shadowPos);
        sm64_saturn_mtxq_translate(&tQm, tQ);
        (void)sm64_saturn_matrix_mul(&tQm, sSaturnCameraMatrixQ,
                                     &gMatStackQ[gMatStackIndex]);
    }
#else
    mtxf_translate(mtxf, shadowPos);
    mtxf_mul(gMatStack[gMatStackIndex], mtxf, *gCurGraphNodeCamera->matrixPtr);
#endif
```
plus refresh+wire-write.

**Object (:815-840)** — three branches: throwMatrix (float, produced by
gameplay — convert at the boundary), billboard, and rotate. throwMatrix
branch:
```c
#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t throwQ;
        saturn_mat4_to_q16(&throwQ, *node->header.gfx.throwMatrix);
        (void)sm64_saturn_matrix_mul(&throwQ, &gMatStackQ[gMatStackIndex],
                                     &gMatStackQ[gMatStackIndex + 1]);
    }
#else
    mtxf_mul(gMatStack[gMatStackIndex + 1], *node->header.gfx.throwMatrix,
             gMatStack[gMatStackIndex]);
#endif
```
(the other two branches follow the billboard/rotate patterns above), then
the shared scale (:825) and refresh+wire-write (:839-840) as usual.

**Held object (:897-911)**: translate + mul + scale, same patterns.

**Root (:1076-1077)**:
```c
#ifdef TARGET_SATURN
        sm64_saturn_matrix_identity(&gMatStackQ[gMatStackIndex]);
        saturn_mtxq_refresh_float_mirror(gMatStackIndex);
        saturn_mtxq_write_wire(initialMatrix, &gMatStackQ[gMatStackIndex]);
#else
        mtxf_identity(gMatStack[gMatStackIndex]);
        mtxf_to_mtx(initialMatrix, gMatStack[gMatStackIndex]);
#endif
```

Every site keeps its `gMatStackFixed[...] = mtx;` line untouched.

- [ ] **Step 4: Handle the non-render-graph Mtx producers via guMtxF2L**

Producers outside the render graph still build float matrices and convert
via `guMtxF2L`/`mtxf_to_mtx` (perspective node's `guPerspective`, skybox,
paintings, HUD `create_dl_*` in `ingame_menu.c`, `intro_geo.c`). Their
*content* is simple/static enough that soft-float building has not been
implicated (and their outputs demonstrably rendered fine in the boot
banner). Only their WIRE FORMAT must match the new Q16 Mtx. Find this
port's `guMtxF2L` implementation:
```
grep -rn "guMtxF2L" src/port/saturn/ lib/src/guMtxF2L.c src/pc/ | head
```
Under `GBI_FLOATS` it is a plain float memcpy (`lib/src/guMtxF2L.c`,
confirmed this session). Add a `TARGET_SATURN` variant in the same file
(or the port's compat layer if the lib file is shared — put it wherever
this build actually links it from, verified by the grep):
```c
#ifdef TARGET_SATURN
#include "port/saturn/gfx/saturn_matrix_kernels.h"
void guMtxF2L(float mf[4][4], Mtx *m) {
    int32_t q[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            q[i][j] = sm64_saturn_float_to_q16(mf[i][j]);
        }
    }
    memcpy(m, q, sizeof(q));
}
#else
/* existing implementation */
#endif
```
This makes EVERY Mtx reaching the display list Q16.16 on Saturn, from
either producer class.

- [ ] **Step 5: Cross-compile**

Forced clean rebuild (delete `@`-mangled objects for
`rendering_graph_node.c`, the `guMtxF2L` TU, `saturn_fast3d_frontend.c`,
plus ELF/ISO/CUE, then `make -j2 && make verify` in
`src/port/saturn/sourceboot` with `.yaul.env` sourced). Expected: clean
build. The frontend still decodes Mtx as floats at this point, so a boot
now would render garbage — that flip is Task 5; do NOT capture-verify yet.

- [ ] **Step 6: Commit**

```bash
git add src/game/rendering_graph_node.c lib/src/guMtxF2L.c
git commit -m "feat(saturn): Q16.16 render-graph matrix pipeline under TARGET_SATURN"
```
(adjust the second path to wherever the guMtxF2L edit actually landed)

---

### Task 5: Frontend native-Q16 Mtx decode

**Files:**
- Modify: `src/port/saturn/gfx/saturn_matrix.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c` (decode call site)
- Modify: `tools/saturn/runtime_contract_test.c` (matrix-decode fixtures)

- [ ] **Step 1: Add the native decode, gated on one shared define**

In `saturn_matrix.h`, next to `sm64_saturn_matrix_decode`:

```c
/* SATURN_MTX_IS_Q16: when defined, the on-wire Mtx payload is raw
 * s32[4][4] Q16.16 (written by rendering_graph_node.c's
 * saturn_mtxq_write_wire and the TARGET_SATURN guMtxF2L), NOT
 * GBI_FLOATS floats. Both producer and consumer key off this ONE
 * define so they cannot desync. The float decode remains for host
 * tests that exercise the float path explicitly. */
static inline void
sm64_saturn_matrix_decode_q16(const int32_t *wire_q16, sm64_saturn_mtx_t *out)
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out->m[i][j] = wire_q16[i * 4 + j];
        }
    }
}
```

- [ ] **Step 2: Flip the frontend's G_MTX decode**

In `saturn_fast3d_frontend.c`, the G_MTX handler currently does
`sm64_saturn_matrix_decode(gbi_floats, &decoded);` (locate via
`grep -n "sm64_saturn_matrix_decode" src/port/saturn/gfx/saturn_fast3d_frontend.c`).
Replace with:
```c
#ifdef SATURN_MTX_IS_Q16
            sm64_saturn_matrix_decode_q16((const int32_t *) w1_ptr, &decoded);
#else
            sm64_saturn_matrix_decode(gbi_floats, &decoded);
#endif
```
(using whatever the real local variable naming at that site is — read it
first; `gbi_floats` was derived from the command's w1 pointer, the q16 read
uses the same pointer reinterpreted).

Define `SATURN_MTX_IS_Q16=1` in `src/port/saturn/sourceboot/Makefile`
alongside the existing `-DSM64_SATURN_VDP1_LWRAM_STAGING=1`, and in the
same place `TARGET_SATURN` builds get their defines for
`rendering_graph_node.c` (both TUs must see it — it's a global build flag
for the sourceboot target, so add it to the target-wide CFLAGS, not
per-file).

- [ ] **Step 3: Update the host contract tests' G_MTX fixtures**

`runtime_contract_test.c`'s G_MTX tests feed float matrix data. Keep the
suite testing BOTH decodes: existing float-fixture tests stay (they test
`sm64_saturn_matrix_decode`, still used off-Saturn); add one new test
compiled unconditionally that exercises `sm64_saturn_matrix_decode_q16`:

```c
static void test_matrix_decode_q16_native(void)
{
    int32_t wire[16];
    sm64_saturn_mtx_t out;

    for (int i = 0; i < 16; i++) {
        wire[i] = (i + 1) * 1000 - 8000; /* mixed signs, exact */
    }
    sm64_saturn_matrix_decode_q16(wire, &out);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            assert(out.m[i][j] == wire[i * 4 + j]);
        }
    }
}
```
Register in `main()`. The full-pipeline frontend tests (G_MTX + G_VTX +
G_TRI) run on host WITHOUT `SATURN_MTX_IS_Q16` defined, so they continue
exercising the float wire format — correct, since host tests model the
generic path; the Q16 wire path's end-to-end proof is the live capture
(Task 6).

- [ ] **Step 4: Run host suites**

`verify-runtime-contracts` exit 0.

- [ ] **Step 5: Full cross-compile + verify**

Forced clean rebuild of the frontend + sourceboot TUs (same mangled-object
deletion discipline), `make -j2 && make verify` — both exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/port/saturn/gfx/saturn_matrix.h \
        src/port/saturn/gfx/saturn_fast3d_frontend.c \
        src/port/saturn/sourceboot/Makefile \
        tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): native Q16.16 Mtx wire format end-to-end (SATURN_MTX_IS_Q16)"
```

---

### Task 6: Live verification — the payoff capture

**Files:** none (verification; produces evidence commits).

- [ ] **Step 1: Fresh address resolve + free-roam capture**

Standard discipline: `sh-elf-nm` fresh resolve of `_sourceboot_fast3d`
(address WILL have shifted), then the proven capture recipe at the
confirmed free-roam depth:
```
./.venv-saturn-tools/Scripts/python.exe tools/saturn/capture_hwtest.py \
  --ymir ".../ymir-headless.exe" --ipl ".../Sega Saturn BIOS (USA).bin" \
  --game "build/saturn/sourceboot/e2-bob/sm64-saturn-sourceboot-e2.cue" \
  --dram-cart --bios-input --frames 240 --handoff-yield \
  --post-poke-frames 25000 --probe-address <fresh> --probe-count 210 \
  --allow-invalid --timeout 400 \
  --screenshot-output docs/saturn/evidence/screenshots/e2-sourceboot-q16-matrix-freeroam-2026-07-23.png \
  --output docs/saturn/evidence/reports/e2-sourceboot-q16-matrix-freeroam-2026-07-23.json
```
Decode with the `offsetof()`-ground-truthed script pattern (see
`e2-sourceboot-bad-mtx-pointer-2026-07-22.md`'s standing lesson — never
hand-derive the struct offsets).

- [ ] **Step 2: Judge against the concrete success criteria**

| Metric | Pre-sprint (real, ppf=25000) | Success threshold |
| --- | --- | --- |
| `reject_w_nonpositive_overflow_suspect` | 392 | **≈ 0** (only genuine behind-camera rejects remain) |
| `dbg_first_w_reject_mp23` | `INT32_MIN` sentinel | sane magnitude or no w-rejects at all |
| `tri_emitted` | 18 | **hundreds** (real geometry surviving) |
| `tri_vdp1_emitted` | 0 at sampled instant | **> 0** |
| Screenshot | uniform near-white | **visible geometry** |

Read the screenshot with the Read tool. Both honest outcomes are
acceptable completions: (a) visible Bob-omb Battlefield terrain — the
project's real first rendered free-roam frame; or (b) counters improved
but screen still blank — in which case the remaining reject attribution
(the counters will say which bucket now dominates) is the next lead, and
the capture is still committed as evidence either way. Do not force (a).

- [ ] **Step 3: User visual confirmation gate**

Per this project's standing rule ("my eyes are the final arbiter"): if the
screenshot shows geometry, present it to the user for confirmation BEFORE
writing any TIMELINE.md milestone entry. The TIMELINE entry (if earned)
follows the established pattern: untextured/flat-shaded labeling, emulator-
evidence-not-retail-proof closing line, screenshot/report/frame-hash cited.

- [ ] **Step 4: Evidence + docs commit**

Update `docs/saturn/HANDOFF_2026-07-22.md`'s successor — create
`docs/saturn/HANDOFF_2026-07-23.md` (superseding format, same as prior
handoffs): the sprint's what/why (soft-float removal from the render matrix
pipeline, the toolchain evidence trail), before/after counters, outcome
honest per Step 2, and next steps (texture decode if rendering now works;
audio milestone per standing instruction). Update `docs/saturn/PROVENANCE.md`
with the SGL/SlaveDriver/Z-Treme consultation entries for this sprint
(behavior-grounded clean-room, no code copied — matching the ledger's
existing entry format).

```bash
git add docs/saturn/evidence/... docs/saturn/HANDOFF_2026-07-23.md docs/saturn/PROVENANCE.md
git commit -m "docs(saturn): Q16 render-matrix sprint outcome + 2026-07-23 handoff"
```

---

## Out of scope (explicit, so the sprint stays bounded)

- **Gameplay float math** (Mario physics, collision, camera *decisions*,
  `mtxf_align_terrain_*`, `vec3f_normalize` in behaviors): stays float.
  Empirically working (Mario reaches ACT_IDLE at the right coordinates);
  converting it would risk simulation-semantics drift for zero rendering
  benefit. The throwMatrix boundary conversion covers the render-graph
  contact point.
- **Toolchain replacement**: GCC 14.3.0 stays pinned. The GCC 15.2.0
  build/evaluation concluded separately (worse `-Os` bug, do-not-pin —
  see the evidence doc); reporting that bug upstream is tracked as its
  own follow-up, not part of this sprint.
- **Texture decode, audio, performance**: next milestones after this
  sprint, per the standing priority notes.

## Self-review (performed at write time)

- **Spec coverage**: user directive = "math appropriate for the processor,
  stop leaning on soft float for camera math" → Tasks 2-5 remove soft-float
  from the full render-matrix path (camera + all node types), not just
  lookat. Reference-liberal directive → SGL/Z-Treme/SlaveDriver grounding
  in Task headers + PROVENANCE updates in Task 6.
- **Placeholder scan**: no TBDs. Two deliberate read-the-real-code-first
  instructions remain (billboard branch conditions in Task 4 Step 3; G_MTX
  local variable naming in Task 5 Step 2) — these are verification
  instructions against drift, with the full pattern code supplied, not
  missing content.
- **Type consistency**: `sm64_saturn_mtx_t`/`sm64_saturn_matrix_mul(a,b,out)`
  argument order (`out = a*b`) checked against `saturn_matrix.h:93` and
  matched to `mtxf_mul(dest,a,b)` semantics at every integration site.
  `sm64_saturn_float_to_q16` saturation contract matches its Task 2 test.
- **Known risk, called out**: Task 3's lookat `>>16` integer-unit reduction
  trades sub-unit direction precision for overflow safety; the differential
  test's tolerances (Q16_TOL_NORM) will catch it if the real error exceeds
  ~0.001 on the captured-camera fixture. If tolerances fail there, the fix
  is a smarter pre-scale (shift by less when deltas are small), not
  loosening the tolerance.
