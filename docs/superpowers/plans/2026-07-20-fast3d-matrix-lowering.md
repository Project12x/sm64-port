# Fast3D Matrix Stack and Crude VDP1 Emission Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `src/port/saturn/gfx/saturn_fast3d_frontend.c` from a pure
display-list walker/counter into a real Fast3D interpreter that decodes
`G_MTX`/`G_POPMTX`/`G_MOVEMEM`/`G_VTX`/`G_GEOMETRYMODE`/`G_TRI1`/`G_TRI2`,
transforms real source geometry through a new fixed-point matrix stack, and
emits crude, untextured, flat-shaded, depth-ordered VDP1 quads.

**Architecture:** A new pure-C Q16.16 matrix module (`saturn_matrix.h`) is
host-testable independent of any Saturn/Yaul dependency. The frontend
decodes commands and produces a small bounded buffer of resolved (already
transformed, projected, culled, depth-bucketed) triangle records — this
stays Yaul-free so it remains host-testable via synthetic display lists. A
new, separate, Yaul-dependent adapter file walks that buffer and writes
real `vdp1_cmdt_t` commands through the shared backend, which gains a
second, caller-storage init entry point backed by a new LWRAM linker
region. See "Decomposition decision" below for why this split exists.

**Tech Stack:** C11 (host tests, native `$(CC)`), SH-2 C (cross-compiled via
`sh-elf-gcc`/Yaul), GNU ld linker scripts.

---

## Revision note (post-review)

This plan was adversarially reviewed twice before being finalized: once
against the approved design spec's architecture, and once against the
*literal code* in this document, by independent agents re-deriving the
bit/float arithmetic and cross-checking every cited file directly (not
trusting either the spec's or an earlier draft's claims). That review
surfaced one finding significant enough to require calling out up front:

**The approved design spec's core premise about the G_MTX wire format was
wrong for this specific build.** The spec stated N64 source matrices
"are natively s15.16, stored as split integer/fraction 16-bit halves."
That is the *classic* N64 GBI encoding, but it is not what this project's
actual build produces: `src/port/saturn/sourceboot/Makefile:74` defines
`F3DEX_GBI_2E=1` for every compiled source file, and
`include/PR/gbi.h:90-94` shows `F3DEX_GBI_2E` unconditionally defines
`GBI_FLOATS`. Under `GBI_FLOATS`, `Mtx` becomes a plain
`struct { float m[4][4]; }` (`gbi.h:1192-1194`) and `Vtx_t.ob` becomes
`float ob[3]` (`gbi.h:1112-1121`) instead of the split-int/short forms.
Every real `G_MTX` command's data and every real vertex's position is
therefore a plain float on this target, not a split s15.16 encoding. This
plan's Task 1 and Task 5/6/8 are written against the *corrected* premise;
`docs/superpowers/specs/2026-07-20-fast3d-matrix-stack-design.md` itself
still describes the incorrect premise in prose and should be corrected
for future readers (tracked separately from this plan's own commits —
see the note at the end of this document).

The rest of this revision note summarizes every other confirmed
correction, so anyone diffing against an earlier draft of this plan can
see what changed and why, without re-running the review:

- The `verify-runtime-contracts` Makefile recipe (Task 5.5, new) was
  missing `-I include`, `-I src`, `-DF3DEX_GBI_2E=1`, and
  `saturn_fast3d_frontend.c` as a compiled/linked source — none of Tasks
  6-9's tests could have compiled or linked without this fix, and without
  the `F3DEX_GBI_2E` flag specifically, the host tests would have silently
  exercised the *wrong* GBI opcode dialect (different bit values for
  `G_MTX_PUSH`, `G_MV_VIEWPORT`, and `G_TRI2`'s very presence) than the
  real cross-compiled target uses.
- Task 6's own `G_MTX` test had a real bug: it packed a raw semantic byte
  where the real `gSPMatrix` encoder pre-XORs with `G_MTX_PUSH` before
  writing to `w0` — the test needed the same pre-XOR to actually test
  "no push," and without it the (correct) decode logic was what broke the
  test's assertion, not a bug in that logic.
- Task 9's `G_TRI1` test encoding didn't double vertex indices the way the
  real encoder does, silently collapsing the test triangle to two
  coincident vertices and making the backface-cull test pass without ever
  exercising winding-sign correctness.
- Task 9's per-triangle `z` storage multiplied by an extra `65536.0f`,
  creating a units mismatch against the near/far-depth constants (which
  are calibrated in raw, unscaled units matching `castleviewer`'s existing
  convention) — this would have rejected virtually all real geometry.
  Fixing it also required giving the affected test an actual (non-identity)
  projection matrix, since an identity `mp` pins clip-space `w` at 1.0
  regardless of a vertex's real depth.
- Task 1's `test_matrix_decode_identity` fixture had wrong values for two
  of its four rows (an artifact of the old split-int format that no
  longer applies after the GBI_FLOATS correction — resolved as part of
  that rewrite).
- Task 7's viewport decode hardcoded the Saturn's 224-line resolution as
  the reference's `SCREEN_HEIGHT` constant, which is actually 240 on the
  N64 side — real gameplay viewport data would have decoded to a
  16-line-tall vertical offset error. Fixed with an explicit letterbox
  step and a test using the real default viewport SM64 emits.
- Task 10's rewritten backend init used `malloc()` where the existing,
  already-shipping code uses `memalign()` specifically because
  `vdp1_cmdt_t` requires 32-byte alignment that this project's TLSF
  allocator does not guarantee via plain `malloc` — this would have been
  an alignment regression to `castleviewer`/`marioturntable`'s existing
  code, not just new code.
- Task 9's near/far rejection and degenerate-triangle rejection were
  ordered opposite to the spec's stated sequence (low-impact, since both
  paths still reject and count — only affects which counter a
  simultaneously-degenerate-and-out-of-range triangle gets attributed to).
- Task 11 aliased one profile counter across two structurally different
  capacity ceilings (the frontend's resolved-triangle buffer vs. the VDP1
  command arena), making the two failure modes indistinguishable from
  profiling data alone.
- Task 15's mutation 3 (negate a translation term) was unobservable by
  the tests it claimed would catch it, since neither test issues a
  `G_MTX` command — a dedicated test with a real, non-identity modelview
  translation was added.
- Task 9 had no test at all for the `G_TRI2` (two-triangles-per-command)
  decode path, despite implementing it — added.

Every one of the above is incorporated into the task text below; this
note exists so a reader who already has the pre-review version in mind
can see the delta at a glance.

---

## Decisions carried from the approved design spec

(`docs/superpowers/specs/2026-07-20-fast3d-matrix-stack-design.md`, as
amended by its own adversarial review, then narrowed further by the four
open questions the user answered before this plan was written — see also
the "Revision note" above for the one spec premise this plan corrects.)

- Coarse depth-bucket painter ordering: **included** (bucket by `max_z`,
  reusing `sm64_saturn_projected_quad_analyze`/`_is_visible`).
- Emission path: **direct-to-VDP1** after depth-bucketing — no
  `saturn_render_queue.h` reuse.
- VDP1 backend storage: `sm64_saturn_vdp1_backend_t.list` becomes an
  **embedded `vdp1_cmdt_list_t` value**, not a pointer.
- `verify-runtime-contracts` **joins** `Makefile.saturn.mk`'s `verify-all`
  chain.

## Decomposition decision (new — not in the spec)

The spec's per-triangle pipeline (step 9: "reserve one `vdp1_cmdt_t` from
the LWRAM-backed backend") implicitly assumed the whole pipeline lives in
`saturn_fast3d_frontend.c`. Building the plan surfaced a compile-boundary
problem: `saturn_fast3d_frontend.c` today includes only `<string.h>`,
`<stdbool.h>`, `PR/gbi.h`, and `types.h` — zero Yaul dependency, which is
exactly what lets `tools/saturn/runtime_contract_test.c` compile it with
the host's plain `$(CC)` (no cross-compiler, no Yaul include path). If the
emission step calls into `saturn_vdp1_backend.h` (which includes
`<yaul.h>`) directly from `saturn_fast3d_frontend.c`, that file can never
again compile under the host's native `$(CC)` — which breaks the spec's
own corrected acceptance criterion (a host-native test of
`sm64_saturn_fast3d_frontend_submit`, added specifically because the
existing headless no-cart probe never reaches the frontend at all).

Resolution: `saturn_fast3d_frontend.c` decodes and resolves triangles into
a small bounded array of `sm64_saturn_resolved_triangle_t` (screen
positions, flat color, depth bucket) — no Yaul, fully host-testable. A new
file, `saturn_fast3d_vdp1_emit.h`/`.c`, is the only place that includes
`saturn_vdp1_backend.h`; it walks the resolved-triangle buffer in
depth-bucket order and writes real VDP1 commands. This is a narrow split
(one new small file, ~40-60 lines) justified entirely by preserving
host-testability, not a general refactor.

## File Structure

- Create: `src/port/saturn/gfx/saturn_matrix.h` — Q16.16 4x4 matrix type,
  GBI_FLOATS decode, multiply (overflow-guarded), 11-deep modelview stack,
  lazy MP composition. Header-only `static inline`, matching every other
  file in `src/port/saturn/gfx/` (none of `saturn_transform.h`,
  `saturn_projected_workarea.h`, `saturn_command_arena.h`,
  `saturn_render_queue.h`, `saturn_vdp1_backend.h`, `saturn_gouraud.h` has
  a `.c` file). No Yaul dependency.
- Modify: `Makefile.saturn.mk` — fix the `verify-runtime-contracts` recipe
  (Task 5.5) so it compiles under the real `F3DEX_GBI_2E`/`GBI_FLOATS`
  profile and links `saturn_fast3d_frontend.c`; later, add
  `verify-runtime-contracts` to `verify-all`'s prerequisites (Task 13).
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h` — add matrix
  stack, viewport, vertex buffer, resolved-triangle buffer to frontend
  state; extend the profile struct with new counters.
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c` — full decode of
  the 7 in-scope opcodes; vertex transform, perspective divide, viewport
  map, backface cull (pre-viewport, Y-up), near/far reject, degenerate
  reject, depth-bucket. Produces the resolved-triangle buffer. No Yaul
  dependency (unchanged from today).
- Create: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h` — declares
  `sm64_saturn_fast3d_vdp1_emit()`.
- Create: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c` — walks the
  resolved-triangle buffer in depth-bucket order, reserves and fills real
  `vdp1_cmdt_t` commands via `saturn_vdp1_backend.h`. Yaul-dependent,
  cross-compiled only.
- Modify: `src/port/saturn/gfx/saturn_vdp1_backend.h` — embed
  `vdp1_cmdt_list_t` as a value; add
  `sm64_saturn_vdp1_backend_init_with_storage()`; explicit zero-init for
  caller-supplied storage; keep `memalign()` (not `malloc()`) for the
  heap-backed path's `vdp1_cmdt_t` allocation.
- Modify: `src/port/saturn/sourceboot/sourceboot-cart.x` — add an `lwram`
  MEMORY region and output section.
- Modify: `src/port/saturn/sourceboot/main.c` — declare the LWRAM-resident
  `vdp1_cmdt_t` array, call `sm64_saturn_vdp1_backend_init_with_storage`,
  call `sm64_saturn_fast3d_vdp1_emit()` once per frame after the source
  loop iteration.
- Modify: `tools/saturn/runtime_contract_test.c` — add `test_matrix_*` and
  `test_frontend_*` functions (all Yaul-free, all host-native).

---

### Task 1: `saturn_matrix.h` — Q16.16 type and GBI_FLOATS decode

**IMPORTANT — corrects a wrong premise inherited from the approved design
spec.** The spec assumed N64 source matrices arrive as the classic split
s15.16 GBI encoding (two 16-bit halves per entry). That is **not** what
this build actually produces: `src/port/saturn/sourceboot/Makefile:74`
defines `F3DEX_GBI_2E=1` for every compiled source file, and
`include/PR/gbi.h:90-94` shows `F3DEX_GBI_2E` unconditionally defines
`GBI_FLOATS`. Under `GBI_FLOATS`, `include/PR/gbi.h:1192-1194` shows `Mtx`
is a plain `struct { float m[4][4]; }`, not the split-int
`typedef s32 Mtx_t[4][4]`. Every real `G_MTX` command's `w1` therefore
points at 16 consecutive row-major `float`s, not a split s15.16 int32
array. (Independently verified by reading the cited lines directly.)

This is good news for this task specifically: decoding a plain float into
Q16.16 is a straightforward `(int32_t)(f * 65536.0f)` per entry — no
bit-splice, no odd/even-column index arithmetic, and no way to
transpose a high/low half by mistake.

**Files:**
- Create: `src/port/saturn/gfx/saturn_matrix.h`
- Test: `tools/saturn/runtime_contract_test.c`

- [ ] **Step 1: Write the failing test**

Add near the top of `tools/saturn/runtime_contract_test.c`, after the
existing `#include "saturn_transform.h"` line:

```c
#include "saturn_matrix.h"
```

Add this test function after `test_q16_normalization` (before
`test_frame_profile`):

```c
static void test_matrix_decode_identity(void)
{
    /* Real on-target encoding under GBI_FLOATS (F3DEX_GBI_2E=1, see
     * include/PR/gbi.h:90-94 and src/port/saturn/sourceboot/Makefile:74):
     * 16 consecutive row-major floats, matching gbi.h's `Mtx` struct
     * under that build configuration -- NOT the classic split s15.16
     * int32 GBI encoding. */
    const float gbi_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    sm64_saturn_mtx_t out;

    sm64_saturn_matrix_decode(gbi_floats, &out);

    assert(out.m[0][0] == (1 << 16) && out.m[0][1] == 0);
    assert(out.m[1][0] == 0 && out.m[1][1] == (1 << 16));
    assert(out.m[2][2] == (1 << 16));
    assert(out.m[3][3] == (1 << 16));
}

static void test_matrix_decode_translation(void)
{
    /* Row 3 = translation (16.0, -8.5, 0.25) in the reference's row-vector
     * convention (gfx_pc.c gfx_sp_vertex: translation lives in M[3][*]). */
    float gbi_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        16.0f, -8.5f, 0.25f, 1.0f
    };
    sm64_saturn_mtx_t out;

    sm64_saturn_matrix_decode(gbi_floats, &out);

    assert(out.m[3][0] == ((int32_t)16 << 16));
    assert(out.m[3][3] == (1 << 16));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```
make -f Makefile.saturn.mk verify-runtime-contracts
```
Expected: compile error — `saturn_matrix.h: No such file or directory` (or
`sm64_saturn_mtx_t`/`sm64_saturn_matrix_decode` undeclared). This task's
test has no dependency on `saturn_fast3d_frontend.c`, so the existing
unmodified recipe is sufficient here — Task 5.5 (below) is where the
recipe gains what Task 6 onward needs.

- [ ] **Step 3: Write minimal implementation**

Create `src/port/saturn/gfx/saturn_matrix.h`:

```c
#ifndef SM64_SATURN_MATRIX_H
#define SM64_SATURN_MATRIX_H

#include <stdbool.h>
#include <stdint.h>

/* Q16.16 fixed-point 4x4 matrix stack for the Fast3D-to-VDP1 lowering
 * front end. No Yaul/Saturn dependency by design: this header is
 * compiled directly by tools/saturn/runtime_contract_test.c under the
 * host's native C compiler (see Makefile.saturn.mk's
 * verify-runtime-contracts target), independent of any emulator or
 * cross-compiler.
 *
 * Q16.16 was chosen as the fixed-point TARGET format because SH-2 has no
 * FPU. The SOURCE wire format is NOT the classic N64 split-int s15.16 GBI
 * encoding: this build defines F3DEX_GBI_2E=1 for every source file
 * (src/port/saturn/sourceboot/Makefile:74), which makes
 * include/PR/gbi.h's GBI_FLOATS active unconditionally (gbi.h:90-94).
 * Under GBI_FLOATS, `Mtx` is `struct { float m[4][4]; }` (gbi.h:1192-1194).
 * Every real G_MTX command's w1 therefore points at 16 consecutive
 * row-major floats. This decode converts those floats to Q16.16 by
 * truncating multiply. Reference: src/pc/gfx/gfx_pc.c's gfx_matrix_mul
 * (~L544-555) for the row-major multiply convention this stack must
 * match, and gfx_sp_vertex (~L616-619) for the row-vector transform
 * convention. This is a distinct fixed-point implementation (SH-2 has no
 * FPU), not a copy. */

typedef struct sm64_saturn_mtx {
    int32_t m[4][4];
} sm64_saturn_mtx_t;

/* Decode one N64 Fast3D matrix from its real on-target GBI_FLOATS
 * encoding (16 consecutive row-major floats) into Q16.16. */
static inline void
sm64_saturn_matrix_decode(const float *gbi_floats, sm64_saturn_mtx_t *out)
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out->m[i][j] = (int32_t)(gbi_floats[i * 4 + j] * 65536.0f);
        }
    }
}

static inline void
sm64_saturn_matrix_identity(sm64_saturn_mtx_t *out)
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out->m[i][j] = (i == j) ? (1 << 16) : 0;
        }
    }
}

#endif
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```
make -f Makefile.saturn.mk verify-runtime-contracts
```
Expected: both tests pass. Add both new test calls to `main()`, after
`test_bounded_command_arena();`:

```c
    test_matrix_decode_identity();
    test_matrix_decode_translation();
```

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_matrix.h tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): add Q16.16 matrix decode for Fast3D lowering"
```

---

### Task 2: `saturn_matrix.h` — matrix multiply with overflow guard

**Files:**
- Modify: `src/port/saturn/gfx/saturn_matrix.h`
- Test: `tools/saturn/runtime_contract_test.c`

- [ ] **Step 1: Write the failing test**

Add to `runtime_contract_test.c`:

```c
static void test_matrix_multiply_identity(void)
{
    sm64_saturn_mtx_t identity, other, result;
    bool overflowed;

    sm64_saturn_matrix_identity(&identity);
    sm64_saturn_matrix_identity(&other);
    other.m[3][0] = 100 << 16; /* translation X = 100.0 */

    overflowed = sm64_saturn_matrix_mul(&other, &identity, &result);

    assert(!overflowed);
    assert(result.m[3][0] == (100 << 16));
    assert(result.m[0][0] == (1 << 16));
}

static void test_matrix_multiply_overflow_guard(void)
{
    sm64_saturn_mtx_t a, b, result;
    bool overflowed;

    sm64_saturn_matrix_identity(&a);
    sm64_saturn_matrix_identity(&b);
    /* Force an entry at the Q16.16 magnitude extreme so the >>16
     * narrowing store would wrap if unguarded. */
    a.m[0][0] = INT32_MAX;
    b.m[0][0] = INT32_MAX;

    overflowed = sm64_saturn_matrix_mul(&a, &b, &result);

    assert(overflowed);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL — `sm64_saturn_matrix_mul` undeclared.

- [ ] **Step 3: Write minimal implementation**

Add to `saturn_matrix.h`, after `sm64_saturn_matrix_identity`:

```c
/* res = a * b (row-major: res[i][j] = sum_k a[i][k]*b[k][j], matching
 * gfx_pc.c's gfx_matrix_mul ~L544-555). Accumulates in int64_t (SH-2 has
 * native dmuls.l, a hardware 32x32->64 signed multiply) and narrows with
 * a single >>16 per entry.
 *
 * This assumes every matrix entry reachable through SM64's object/camera
 * graph keeps its integer part well under Q16.16's +-32768 ceiling, so
 * the sum and the final narrowing stay in range in practice. That is an
 * assumption about the data, not a property this function enforces on
 * its own -- returns true if any entry's accumulated value would not
 * round-trip through the >>16 narrowing, so a violated bound is visible
 * (counted by the caller) rather than silently corrupting a matrix
 * entry. Two same-sign terms at the true Q16.16 extreme can already sum
 * past INT64_MAX (2^31*2^31=2^62 per term), so the accumulator itself is
 * not unconditionally safe at the format's edges either -- this is
 * checked by testing the narrowed result against the pre-shift value,
 * which also catches that case for any input this frontend will
 * realistically see. */
static inline bool
sm64_saturn_matrix_mul(const sm64_saturn_mtx_t *a, const sm64_saturn_mtx_t *b,
                       sm64_saturn_mtx_t *out)
{
    sm64_saturn_mtx_t tmp;
    bool overflowed = false;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int64_t sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += (int64_t)a->m[i][k] * (int64_t)b->m[k][j];
            }
            const int64_t narrowed = sum >> 16;
            if (narrowed > INT32_MAX || narrowed < INT32_MIN) {
                overflowed = true;
            }
            tmp.m[i][j] = (int32_t)narrowed;
        }
    }
    *out = tmp;
    return overflowed;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: PASS. Add both new test calls to `main()`.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_matrix.h tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): add overflow-guarded Q16.16 matrix multiply"
```

---

### Task 3: `saturn_matrix.h` — 11-deep modelview stack

**Files:**
- Modify: `src/port/saturn/gfx/saturn_matrix.h`
- Test: `tools/saturn/runtime_contract_test.c`

- [ ] **Step 1: Write the failing test**

```c
static void test_matrix_stack_push_pop(void)
{
    sm64_saturn_matrix_stack_t stack;
    sm64_saturn_mtx_t loaded;

    sm64_saturn_matrix_stack_init(&stack);
    assert(stack.depth == 1); /* starts with one identity entry, matching
                                * the reference's initial
                                * modelview_matrix_stack_size == 1 */
    assert(!stack.overflowed);

    sm64_saturn_matrix_identity(&loaded);
    loaded.m[3][0] = 5 << 16;
    sm64_saturn_matrix_stack_load(&stack, &loaded);
    assert(stack.entries[stack.depth - 1].m[3][0] == (5 << 16));

    assert(sm64_saturn_matrix_stack_push(&stack));
    assert(stack.depth == 2);
    /* push copies the current top, matching gfx_pc.c's push semantics
     * (memcpy of the previous top into the new slot before any load). */
    assert(stack.entries[1].m[3][0] == (5 << 16));

    sm64_saturn_matrix_stack_pop(&stack, 1);
    assert(stack.depth == 1);
}

static void test_matrix_stack_overflow(void)
{
    sm64_saturn_matrix_stack_t stack;

    sm64_saturn_matrix_stack_init(&stack);
    for (int i = 0; i < 10; i++) {
        assert(sm64_saturn_matrix_stack_push(&stack));
    }
    assert(stack.depth == 11);
    assert(!sm64_saturn_matrix_stack_push(&stack));
    assert(stack.overflowed);
    assert(stack.depth == 11); /* push-at-cap is a no-op, not a crash */
}

static void test_matrix_stack_pop_past_floor(void)
{
    sm64_saturn_matrix_stack_t stack;

    sm64_saturn_matrix_stack_init(&stack);
    /* Matches gfx_pc.c's gfx_sp_pop_matrix: popping past depth 0 is a
     * silent no-op in the reference (guarded by
     * `if (modelview_matrix_stack_size > 0)`), so this port mirrors that
     * exactly rather than trapping -- a balanced display list never
     * triggers it. */
    sm64_saturn_matrix_stack_pop(&stack, 5);
    assert(stack.depth == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL — stack types/functions undeclared.

- [ ] **Step 3: Write minimal implementation**

Add to `saturn_matrix.h`:

```c
#define SM64_SATURN_MATRIX_STACK_DEPTH 11U

typedef struct sm64_saturn_matrix_stack {
    sm64_saturn_mtx_t entries[SM64_SATURN_MATRIX_STACK_DEPTH];
    uint8_t depth;
    bool overflowed;
} sm64_saturn_matrix_stack_t;

static inline void
sm64_saturn_matrix_stack_init(sm64_saturn_matrix_stack_t *stack)
{
    sm64_saturn_matrix_identity(&stack->entries[0]);
    stack->depth = 1;
    stack->overflowed = false;
}

/* Matches gfx_pc.c's gfx_sp_matrix push guard:
 * `if ((parameters & G_MTX_PUSH) && modelview_matrix_stack_size < 11)`.
 * Copies the current top into the new slot, matching the reference's
 * memcpy-before-any-load push semantics. Push at depth 11 is a counted
 * no-op, not a trap. */
static inline bool
sm64_saturn_matrix_stack_push(sm64_saturn_matrix_stack_t *stack)
{
    if (stack->depth >= SM64_SATURN_MATRIX_STACK_DEPTH) {
        stack->overflowed = true;
        return false;
    }
    stack->entries[stack->depth] = stack->entries[stack->depth - 1];
    stack->depth++;
    return true;
}

/* Matches gfx_pc.c's gfx_sp_pop_matrix: pops `count` levels, silently
 * stopping at depth 0 rather than trapping (the reference guards with
 * `if (modelview_matrix_stack_size > 0)` inside its while loop). Callers
 * must pre-divide the raw G_POPMTX data word by 64 -- see
 * saturn_fast3d_frontend.c's G_POPMTX decode. */
static inline void
sm64_saturn_matrix_stack_pop(sm64_saturn_matrix_stack_t *stack,
                             uint32_t count)
{
    while (count-- > 0 && stack->depth > 0) {
        stack->depth--;
    }
}

static inline void
sm64_saturn_matrix_stack_load(sm64_saturn_matrix_stack_t *stack,
                              const sm64_saturn_mtx_t *m)
{
    stack->entries[stack->depth - 1] = *m;
}

static inline sm64_saturn_mtx_t *
sm64_saturn_matrix_stack_top(sm64_saturn_matrix_stack_t *stack)
{
    return &stack->entries[stack->depth - 1];
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: PASS. Add the three new test calls to `main()`.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_matrix.h tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): add 11-deep modelview matrix stack"
```

---

### Task 4: `saturn_matrix.h` — lazy MP composition

**Files:**
- Modify: `src/port/saturn/gfx/saturn_matrix.h`
- Test: `tools/saturn/runtime_contract_test.c`

- [ ] **Step 1: Write the failing test**

```c
static void test_matrix_mp_lazy_composition(void)
{
    sm64_saturn_matrix_stack_t stack;
    sm64_saturn_mtx_t projection, loaded;
    const sm64_saturn_mtx_t *mp1, *mp2;

    sm64_saturn_matrix_stack_init(&stack);
    sm64_saturn_matrix_identity(&projection);
    sm64_saturn_matrix_stack_set_projection(&stack, &projection);

    mp1 = sm64_saturn_matrix_stack_mp(&stack);
    assert(mp1->m[0][0] == (1 << 16));

    /* A second call with nothing dirtied must return the identical
     * composed matrix without recomputation (observable here only by
     * correctness, not by a tick count -- the dirty-flag mechanism
     * itself is exercised by the mutation test in Task 15). */
    mp2 = sm64_saturn_matrix_stack_mp(&stack);
    assert(mp2->m[0][0] == (1 << 16));

    sm64_saturn_matrix_identity(&loaded);
    loaded.m[3][1] = 7 << 16;
    sm64_saturn_matrix_stack_load(&stack, &loaded);
    mp2 = sm64_saturn_matrix_stack_mp(&stack);
    assert(mp2->m[3][1] == (7 << 16));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL — `sm64_saturn_matrix_stack_set_projection`/`_mp` undeclared.

- [ ] **Step 3: Write minimal implementation**

Add to `sm64_saturn_matrix_stack_t` (modify the struct from Task 3):

```c
typedef struct sm64_saturn_matrix_stack {
    sm64_saturn_mtx_t entries[SM64_SATURN_MATRIX_STACK_DEPTH];
    sm64_saturn_mtx_t projection;
    sm64_saturn_mtx_t mp;
    uint8_t depth;
    bool overflowed;
    bool mp_dirty;
    bool mp_overflowed;
} sm64_saturn_matrix_stack_t;
```

Update `sm64_saturn_matrix_stack_init` to also set:
```c
    sm64_saturn_matrix_identity(&stack->projection);
    stack->mp_dirty = true;
    stack->mp_overflowed = false;
```

Mark `mp_dirty = true` at the end of `sm64_saturn_matrix_stack_push`,
`sm64_saturn_matrix_stack_pop` (whenever `count > 0` was requested, even
if it clamped to 0 actual pops, to stay conservative rather than track a
"did anything actually change" flag precisely), and
`sm64_saturn_matrix_stack_load`.

Add:

```c
static inline void
sm64_saturn_matrix_stack_set_projection(sm64_saturn_matrix_stack_t *stack,
                                        const sm64_saturn_mtx_t *m)
{
    stack->projection = *m;
    stack->mp_dirty = true;
}

/* Recomputes modelview*projection only when dirtied since the last call,
 * matching the "lazy MP composition" design decision: the reference
 * (gfx_pc.c) recomputes eagerly after every G_MTX/G_POPMTX, but matrix
 * commands arrive in bursts before the next G_VTX, so this port composes
 * once, on demand. */
static inline const sm64_saturn_mtx_t *
sm64_saturn_matrix_stack_mp(sm64_saturn_matrix_stack_t *stack)
{
    if (stack->mp_dirty) {
        if (sm64_saturn_matrix_mul(sm64_saturn_matrix_stack_top(stack),
                                    &stack->projection, &stack->mp)) {
            stack->mp_overflowed = true;
        }
        stack->mp_dirty = false;
    }
    return &stack->mp;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: PASS. Add the new test call to `main()`.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_matrix.h tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): add lazy MP matrix composition"
```

---

### Task 5: Frontend state — extend the header

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h`

No test in this task — it only adds types/fields with no new behavior yet;
Task 6 onward exercises them. Note that `sm64_saturn_fast3d_vertex_t` below
stores `float` position fields, not `int16_t` — because under this build's
`GBI_FLOATS` configuration (see Task 1's note), `Vtx_t.ob` is genuinely
`float ob[3]` (`include/PR/gbi.h:1112-1121`), not the classic `short[3]`.

- [ ] **Step 1: Modify the header**

Replace the full contents of `saturn_fast3d_frontend.h` with:

```c
#ifndef SM64_SATURN_FAST3D_FRONTEND_H
#define SM64_SATURN_FAST3D_FRONTEND_H

#include <stdint.h>

#include "port/saturn/runtime/saturn_source_runtime.h"
#include "saturn_matrix.h"

/*
 * Bounded source-display-list intake for the Saturn renderer.
 *
 * This is deliberately a front end, not a Castle or Mario renderer.  It
 * consumes the SPTask produced by original game code, follows the Fast3D
 * display-list control flow, transforms and resolves real source
 * geometry, and hands a bounded buffer of resolved triangles to a
 * separate, Yaul-dependent emission stage (saturn_fast3d_vdp1_emit.h).
 * This file has zero Yaul/Saturn dependency by design, so it stays
 * host-testable via tools/saturn/runtime_contract_test.c.  Its command
 * ABI is verified against this tree's `src/pc/gfx/gfx_pc.c`, but this
 * implementation is new target code and does not import the PC renderer.
 */
#define SM64_SATURN_FAST3D_MAX_CALL_DEPTH 32U
#define SM64_SATURN_FAST3D_MAX_COMMANDS 16384U
#define SM64_SATURN_FAST3D_MAX_VERTICES 64U
#define SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES 192U
#define SM64_SATURN_FAST3D_DEPTH_BUCKETS 16U

enum sm64_saturn_fast3d_fault {
    SM64_SATURN_FAST3D_FAULT_NONE = 0U,
    SM64_SATURN_FAST3D_FAULT_NULL_TASK = 1U << 0,
    SM64_SATURN_FAST3D_FAULT_NULL_DISPLAY_LIST = 1U << 1,
    SM64_SATURN_FAST3D_FAULT_CALL_DEPTH = 1U << 2,
    SM64_SATURN_FAST3D_FAULT_COMMAND_LIMIT = 1U << 3,
};

typedef struct sm64_saturn_fast3d_profile {
    uint32_t frame_serial;
    uint32_t command_count;
    uint32_t display_list_calls;
    uint32_t display_list_branches;
    uint32_t matrix_commands;
    uint32_t vertex_commands;
    uint32_t triangle_count;
    uint32_t texture_commands;
    uint32_t rdp_commands;
    uint32_t other_commands;
    uint16_t max_call_depth;
    uint16_t fault_flags;

    /* Added for Fast3D-to-VDP1 lowering (see design spec). */
    uint32_t triangles_transformed;
    uint32_t triangles_emitted; /* resolved into the bounded intermediate
                                  * buffer -- NOT necessarily reaching a
                                  * real VDP1 command. See
                                  * triangles_vdp1_emitted below, which is
                                  * the Yaul-dependent emission stage's
                                  * own counter and can only be observed
                                  * on real/cross-compiled hardware. */
    uint32_t reject_near_far;
    uint32_t reject_backface;
    uint32_t reject_degenerate;
    uint32_t reject_vertex_range;
    uint32_t reject_command_capacity; /* the frontend's resolved-triangle
                                        * buffer (this file) filling up --
                                        * a DIFFERENT ceiling than
                                        * reject_vdp1_arena_capacity below,
                                        * which is the VDP1 command arena
                                        * in saturn_fast3d_vdp1_emit.c. */
    uint32_t modelview_stack_overflow;
    uint16_t max_modelview_depth_reached;

    /* Added for Task 11's VDP1 emission adapter (saturn_fast3d_vdp1_emit.c).
     * Both fields are Yaul-dependent counters that only advance once a
     * real vdp1_cmdt_t is reserved/written -- they cannot be observed by
     * a host-native test of sm64_saturn_fast3d_frontend_submit alone. */
    uint32_t triangles_vdp1_emitted;
    uint32_t reject_vdp1_arena_capacity;
} sm64_saturn_fast3d_profile_t;

/* Screen-space position + flat color for one already-transformed,
 * projected, culled, and depth-bucketed triangle. Populated by
 * saturn_fast3d_frontend.c (no Yaul dependency); consumed by
 * saturn_fast3d_vdp1_emit.c (Yaul-dependent) to write real VDP1
 * commands. Corner order is (i0, i1, i2, i2) -- the last vertex
 * duplicated -- ready to hand to VDP1's degenerate-quad polygon command. */
typedef struct sm64_saturn_resolved_triangle {
    int16_t x[3];
    int16_t y[3];
    uint16_t color_rgb1555;
    uint16_t depth_bucket;
} sm64_saturn_resolved_triangle_t;

typedef struct sm64_saturn_fast3d_viewport {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
} sm64_saturn_fast3d_viewport_t;

/* One decoded source vertex: model-space position and flat RGBA color
 * (Vtx_t.cn[4]). Position is `float`, matching Vtx_t.ob[3] under this
 * build's GBI_FLOATS configuration (include/PR/gbi.h:1112-1121) -- NOT
 * the classic short[3] model-space encoding. This keeps the frontend's
 * per-triangle scratch transform math (Task 9) in one consistent domain
 * without an extra, unnecessary Q16.16 round-trip for data that already
 * arrives as float on this target. */
typedef struct sm64_saturn_fast3d_vertex {
    float x, y, z;
    uint8_t r, g, b, a;
} sm64_saturn_fast3d_vertex_t;

typedef struct sm64_saturn_fast3d_frontend {
    sm64_saturn_fast3d_profile_t profile;
    sm64_saturn_matrix_stack_t matrix_stack;
    sm64_saturn_fast3d_viewport_t viewport;
    uint32_t geometry_mode;
    sm64_saturn_fast3d_vertex_t vertices[SM64_SATURN_FAST3D_MAX_VERTICES];
    sm64_saturn_resolved_triangle_t
        resolved[SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES];
    uint16_t resolved_count;
} sm64_saturn_fast3d_frontend_t;

void sm64_saturn_fast3d_frontend_init(
    sm64_saturn_fast3d_frontend_t *frontend);
void sm64_saturn_fast3d_frontend_submit(struct SPTask *task, void *context);

#endif
```

- [ ] **Step 2: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.h
git commit -m "feat(saturn): extend Fast3D frontend state for matrix lowering"
```

(No test-verify step: this task only adds declarations. The existing
`test_bounded_command_arena` etc. in `runtime_contract_test.c` must still
compile/pass unchanged -- run
`make -f Makefile.saturn.mk verify-runtime-contracts` once here as a
smoke check before moving on, expecting the same PASS as before this
task, since nothing behavioral changed yet.)

---

### Task 5.5: `Makefile.saturn.mk` — fix `verify-runtime-contracts` to build the frontend under the real GBI profile

**New task, added by the plan's own code review.** Without this, none of
Tasks 6-9's tests can compile or link, and even a naively-patched build
would silently exercise the wrong GBI opcode dialect.

**Files:**
- Modify: `Makefile.saturn.mk`

- [ ] **Step 1: Update the `verify-runtime-contracts` recipe**

The current recipe (`Makefile.saturn.mk:143-150`) compiles only
`tools/saturn/runtime_contract_test.c`, with just
`-I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx"` and
`-I"$(SATURN_REPO_ROOT)/src/port/saturn/platform"`, and no `-D` flags.
That's sufficient for every header tested so far (all pure, no external
type dependency), but Task 6 onward adds `#include "saturn_fast3d_frontend.h"`
and `#include "PR/gbi.h"` to the test file:

- `saturn_fast3d_frontend.h` includes
  `"port/saturn/runtime/saturn_source_runtime.h"` — resolvable only with
  `-I"$(SATURN_REPO_ROOT)/src"`.
- That header includes `<ultra64.h>`, which pulls in `PR/gbi.h` and
  `types.h` — resolvable only with `-I"$(SATURN_REPO_ROOT)/include"`.
- Without `-DF3DEX_GBI_2E=1` (matching
  `src/port/saturn/sourceboot/Makefile:74`), `include/PR/gbi.h`'s
  `#ifdef F3DEX_GBI_2` branches take the *opposite*, legacy values —
  `G_MTX_PUSH=0x04` instead of `0x01`, `G_MV_VIEWPORT=0x80` instead of
  `8`, and the `G_TRI2` case in `saturn_fast3d_frontend.c` (guarded by
  `#if defined(F3DEX_GBI) || defined(F3DLP_GBI) || defined(F3DEX_GBI_2)`)
  disappears entirely — so without this flag, the host tests would
  silently validate a different opcode dialect than the real
  cross-compiled target ever sees.
- `sm64_saturn_fast3d_frontend_init`/`_submit` are ordinary (non-static,
  non-inline) functions defined in `saturn_fast3d_frontend.c` — unlike
  every header this recipe has compiled so far, this needs a second
  compiled/linked source file.

Change (`Makefile.saturn.mk:143-150`):
```makefile
verify-runtime-contracts:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/runtime_contract_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/runtime-contract-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/runtime-contract-test$(HOST_EXEEXT)"
```
to:
```makefile
verify-runtime-contracts:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -std=c11 -Wall -Wextra -Werror \
	  -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/runtime_contract_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_fast3d_frontend.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/runtime-contract-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/runtime-contract-test$(HOST_EXEEXT)"
```

- [ ] **Step 2: Run to verify no regression**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: still PASS — this step only changes how the test binary is
built; no new test functions exist yet, so behavior is unchanged from
before this task. If `-Wall -Wextra -Werror` surfaces a warning inside
`saturn_fast3d_frontend.c` when compiled under the host compiler for the
first time (e.g. a signed/unsigned comparison on the existing
`opcode >= G_NOOP` check, since `G_NOOP` becomes `0` once
`F3DEX_GBI_2E` is defined), fix it at the source rather than suppressing
the warning flags, since they apply to every task from here on.

- [ ] **Step 3: Commit**

```bash
git add Makefile.saturn.mk
git commit -m "build(saturn): compile saturn_fast3d_frontend.c under the real GBI profile in verify-runtime-contracts"
```

---

### Task 6: Frontend — `G_MTX`/`G_POPMTX` decode

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Test: `tools/saturn/runtime_contract_test.c`

This task also performs the signature-change groundwork the design spec's
review flagged: `sm64_saturn_fast3d_count_command`'s opcode-only signature
cannot decode any of the new commands (they need `w0` bits beyond the
opcode byte, and/or `w1`). This task adds a **second**, separate dispatch
inside `sm64_saturn_fast3d_frontend_submit` with access to the full
`command->words.w0`/`.w1` -- it does not touch
`sm64_saturn_fast3d_count_command`, which keeps doing exactly what it does
today (pure counting).

- [ ] **Step 1: Write the failing test**

Add to `runtime_contract_test.c`. This constructs a two-command synthetic
Fast3D list (`G_MTX` load-modelview-identity, `G_ENDDL`) and submits it
directly to the frontend -- no Yaul, no emulator, matching the design
spec's corrected acceptance criterion.

```c
#include "saturn_fast3d_frontend.h"
#include "PR/gbi.h"

/* Builds one G_MTX command word pair. `mtx_ptr` must outlive the caller's
 * use of the returned Gfx (it is embedded as a raw pointer -- this port
 * treats w1 as a real address, matching the existing G_DL handling in
 * saturn_fast3d_frontend.c, since SOURCE.DAT's tables are linked at
 * final addresses rather than N64-segmented). `params` is packed AS-IS
 * into w0's low byte -- callers must pre-XOR with G_MTX_PUSH themselves
 * if they want the frontend's decode-time XOR (below) to cancel back to
 * a specific semantic value, exactly mirroring what the real gSPMatrix
 * macro does at encode time (include/PR/gbi.h's F3DEX_GBI_2 branch:
 * gDma2p(pkt, G_MTX, m, sizeof(Mtx), (p)^G_MTX_PUSH, 0)). */
static Gfx
make_g_mtx(uint8_t params, const float *mtx_floats)
{
    Gfx g;
    g.words.w0 = ((uint32_t)G_MTX << 24) | params;
    g.words.w1 = (uintptr_t)mtx_floats;
    return g;
}

static Gfx
make_g_enddl(void)
{
    Gfx g;
    g.words.w0 = (uint32_t)G_ENDDL << 24;
    g.words.w1 = 0;
    return g;
}

static void test_frontend_g_mtx_load_modelview(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const float identity_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    Gfx list[2];
    struct SPTask task;

    /* The real gSPMatrix(pkt, m, p) macro pre-XORs the parameter byte
     * with G_MTX_PUSH before writing it to w0. The frontend's decode
     * XORs it back with G_MTX_PUSH (matching gfx_pc.c:1373's
     * C0(0,8) ^ G_MTX_PUSH), so this test must apply the same pre-XOR by
     * hand to get LOAD-only (no push) semantics -- a raw
     * G_MTX_LOAD|G_MTX_MODELVIEW byte here would decode as
     * G_MTX_PUSH|G_MTX_LOAD and incorrectly push a stack level. */
    list[0] = make_g_mtx((uint8_t)((G_MTX_LOAD | G_MTX_MODELVIEW) ^ G_MTX_PUSH),
                          identity_floats);
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.fault_flags == SM64_SATURN_FAST3D_FAULT_NONE);
    assert(frontend.matrix_stack.depth == 1);
    assert(frontend.matrix_stack.entries[0].m[0][0] == (1 << 16));
}
```

Also add a `G_POPMTX` test:

```c
static void test_frontend_g_popmtx_scales_by_64(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    Gfx list[2];
    struct SPTask task;

    /* gSPPopMatrixN(pkt, n, num) encodes num*64 into w1 (include/PR/gbi.h
     * gSPPopMatrixN macro). A real gSPPopMatrix(1) therefore carries the
     * raw value 64, not 1. */
    list[0].words.w0 = (uint32_t)G_POPMTX << 24;
    list[0].words.w1 = 1U * 64U;
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    /* Push once first so there's a level to pop back from. */
    (void)sm64_saturn_matrix_stack_push(&frontend.matrix_stack);
    assert(frontend.matrix_stack.depth == 2);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    /* If the /64 scaling is missing, this would attempt to pop 64
     * levels instead of 1 and desync the stack far past its actual
     * depth of 2 -- asserting depth==1 (exactly one level popped)
     * catches that directly. */
    assert(frontend.matrix_stack.depth == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL to compile (frontend struct fields/behavior not wired) or
assertion failure (matrix stack untouched, still at init state) -- either
is an acceptable "fails for the right reason" here since Task 5's header
change already compiles; only the new decode behavior is missing.

- [ ] **Step 3: Write minimal implementation**

Modify `saturn_fast3d_frontend.c`. Update `sm64_saturn_fast3d_frontend_init`:

```c
void sm64_saturn_fast3d_frontend_init(
    sm64_saturn_fast3d_frontend_t *frontend)
{
    if (frontend != NULL) {
        (void)memset(frontend, 0, sizeof(*frontend));
        sm64_saturn_matrix_stack_init(&frontend->matrix_stack);
    }
}
```

Add local bit-field extraction macros and the new decode dispatch. Add
near the top of the file, after the existing includes:

```c
#define SM64_SATURN_C0(w0, pos, width) \
    (((w0) >> (pos)) & ((1U << (width)) - 1U))
#define SM64_SATURN_C1(w1, pos, width) \
    (((w1) >> (pos)) & ((1U << (width)) - 1U))
```

Add a new static function, placed after `sm64_saturn_fast3d_count_command`:

```c
/* Full-command decode for opcodes whose semantics need more than the
 * opcode byte (matrix/vertex/viewport/geometrymode data lives in w0's
 * lower bits and/or all of w1). This is intentionally separate from
 * sm64_saturn_fast3d_count_command, whose (profile, opcode) signature
 * cannot reach that data -- see the design spec's review finding on this
 * exact point. */
static void
sm64_saturn_fast3d_decode_command(sm64_saturn_fast3d_frontend_t *frontend,
                                  const Gfx *command)
{
    sm64_saturn_fast3d_profile_t *profile = &frontend->profile;
    /* IMPORTANT -- discovered during Task 6's implementation, applies to
     * every later task that adds a case to this same switch (7, 8, 9):
     * this repo's Gwords (include/PR/gbi.h:1728-1731) declares w0/w1 as
     * `uintptr_t`, not the classic N64 SDK's `u32` -- a fork-specific
     * accommodation so real pointers (G_DL targets, and now G_MTX's
     * float array, G_VTX's vertex array, etc.) round-trip without
     * truncation. Narrowing w1 into a local `uint32_t` here, THEN
     * reconstructing a pointer via `(T *)(uintptr_t)w1`, zero-extends a
     * truncated 32-bit value back into a 64-bit pointer on any 64-bit
     * host (this exact bug segfaulted the host test build during Task
     * 6's implementation). On the real SH-2 target `uintptr_t` is only
     * 32 bits, so this same bug would silently compile and "work" there
     * -- meaning a host-only crash is the ONLY signal that would ever
     * catch it, which is precisely why host-testability matters here.
     * Keep w0/w1 at their real `uintptr_t` width; the SM64_SATURN_C0/C1
     * bit-field macros still work correctly on a wider operand (the
     * encoded opcode/parameter bits only ever occupy the low 32 bits),
     * so this costs nothing for the existing bitfield-extraction uses. */
    const uintptr_t w0 = command->words.w0;
    const uintptr_t w1 = command->words.w1;
    const uint8_t opcode = (uint8_t)(w0 >> 24);

    switch (opcode) {
        case G_MTX: {
            /* F3DEX_GBI_2E (this build) inverts the push bit relative to
             * the raw parameter -- see gfx_pc.c:1373,
             * `gfx_sp_matrix(C0(0, 8) ^ G_MTX_PUSH, ...)`. */
            const uint8_t params =
                (uint8_t)(SM64_SATURN_C0(w0, 0, 8) ^ G_MTX_PUSH);
            /* w1 points at 16 consecutive row-major floats under this
             * build's GBI_FLOATS configuration (see Task 1's note) --
             * NOT a split s15.16 int32 array. w1 is already uintptr_t
             * (see the note above), so this cast is a no-op width-wise;
             * kept for clarity, NOT for narrowing. */
            const float *gbi_floats = (const float *)w1;
            sm64_saturn_mtx_t decoded;

            sm64_saturn_matrix_decode(gbi_floats, &decoded);

            if (params & G_MTX_PROJECTION) {
                if (params & G_MTX_LOAD) {
                    sm64_saturn_matrix_stack_set_projection(
                        &frontend->matrix_stack, &decoded);
                } else {
                    sm64_saturn_mtx_t composed;
                    (void)sm64_saturn_matrix_mul(
                        &decoded, &frontend->matrix_stack.projection,
                        &composed);
                    sm64_saturn_matrix_stack_set_projection(
                        &frontend->matrix_stack, &composed);
                }
            } else {
                if (params & G_MTX_PUSH) {
                    if (!sm64_saturn_matrix_stack_push(
                            &frontend->matrix_stack)) {
                        profile->modelview_stack_overflow++;
                    }
                }
                if (params & G_MTX_LOAD) {
                    sm64_saturn_matrix_stack_load(&frontend->matrix_stack,
                                                  &decoded);
                } else {
                    sm64_saturn_mtx_t composed;
                    (void)sm64_saturn_matrix_mul(
                        &decoded,
                        sm64_saturn_matrix_stack_top(
                            &frontend->matrix_stack),
                        &composed);
                    sm64_saturn_matrix_stack_load(&frontend->matrix_stack,
                                                  &composed);
                }
            }
            if (frontend->matrix_stack.depth >
                profile->max_modelview_depth_reached) {
                profile->max_modelview_depth_reached =
                    frontend->matrix_stack.depth;
            }
            break;
        }
        case (uint8_t)G_POPMTX: {
            /* gSPPopMatrixN encodes num*64 into w1 (include/PR/gbi.h) --
             * see the /64 recovery this decode performs, matching
             * gfx_pc.c:1380, `gfx_sp_pop_matrix(cmd->words.w1 / 64)`. */
            sm64_saturn_matrix_stack_pop(&frontend->matrix_stack, w1 / 64U);
            break;
        }
        default:
            break;
    }
}
```

Now wire this into `sm64_saturn_fast3d_frontend_submit`'s main walk loop.
Modify the existing loop body (inside the `while
(profile->command_count < SM64_SATURN_FAST3D_MAX_COMMANDS)` loop, right
after the existing `sm64_saturn_fast3d_count_command(profile, opcode);`
call and before the `if (opcode == G_DL)` check):

```c
        sm64_saturn_fast3d_decode_command(frontend, command);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: PASS. Add both new test calls to `main()`.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.c tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): decode G_MTX/G_POPMTX in Fast3D frontend"
```

---

### Task 7: Frontend — `G_MOVEMEM` (viewport) and `G_GEOMETRYMODE` decode

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Test: `tools/saturn/runtime_contract_test.c`

**Note**: `Vp_t` (the viewport record) is a separate struct from `Mtx`/
`Vtx_t` and is **not** gated by `GBI_FLOATS` — `include/PR/gbi.h:1232-1236`
declares it unconditionally as `short vscale[4]; short vtrans[4];` with 2
bits of fraction, regardless of the `GBI_FLOATS` build flag. So this
task's viewport decode is unaffected by Task 1's GBI_FLOATS correction.

- [ ] **Step 1: Write the failing test**

```c
static void test_frontend_g_movemem_viewport(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0}, /* 2-bit-fraction N64 units,
                                              * already expressed in
                                              * Saturn-native 224-line
                                              * terms -- a synthetic edge
                                              * case, not real SM64 data.
                                              * See the second test below
                                              * for real game data. */
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    Gfx list[2];
    struct SPTask task;

    /* F3DEX_GBI_2 G_MOVEMEM encoding: gfx_sp_movemem(C0(0,8), C0(8,8)*8,
     * seg_addr(w1)) per gfx_pc.c:1387. Index (w0 bits 0-7) must equal
     * G_MV_VIEWPORT (8 under F3DEX_GBI_2, include/PR/gbi.h:1255). */
    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.viewport.width == 320);
    assert(frontend.viewport.height == 224);
    assert(frontend.viewport.x == 0);
    /* Corrected during implementation: the decode always anchors on the
     * real N64 240-line space and applies a fixed 8-line letterbox crop
     * -- it has no way to know this synthetic Vp_t was chosen to already
     * "look" 224-native, so it gets the same treatment as any other
     * input. Hand-derivation: source_y = 240 - (448/4 + 224/2) = 16,
     * y = source_y - 8 = 8, not 0 as an earlier draft of this test
     * assumed. */
    assert(frontend.viewport.y == 8);
}

static void test_frontend_g_movemem_viewport_real_default(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* Matches src/game/area.c's real default viewport
     * (D_8032CF00 = {{640,480,511,0},{640,480,511,0}}), submitted every
     * frame via gSPViewport in render_game() -- this is what real
     * gameplay display lists actually send, expressed in the N64's
     * native 320x240 coordinate space, NOT the Saturn-224-native
     * synthetic value the test above uses. */
    static const Vp_t vp = {
        .vscale = {640, 480, 511, 0},
        .vtrans = {640, 480, 511, 0}
    };
    Gfx list[2];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    /* 320x240 reconstructed in the N64's native space, then letterboxed
     * (8 lines cropped off top and bottom) onto the Saturn's 224 visible
     * scanlines -- see the decode's own comment for the derivation. */
    assert(frontend.viewport.width == 320);
    assert(frontend.viewport.height == 240);
    assert(frontend.viewport.x == 0);
    assert(frontend.viewport.y == -8);
}

static void test_frontend_g_geometrymode(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    Gfx list[2];
    struct SPTask task;

    /* F3DEX_GBI_2 combined form: gfx_sp_geometry_mode(~C0(0,24), w1) per
     * gfx_pc.c:1428 -- clear-mask in w0 bits 0-23, set-mask is all of w1. */
    list[0].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0x000000U;
    list[0].words.w1 = G_CULL_BACK;
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert((frontend.geometry_mode & G_CULL_BACK) != 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL — `frontend.viewport`/`.geometry_mode` remain zeroed.

- [ ] **Step 3: Write minimal implementation**

Add two constants near the top of `saturn_fast3d_frontend.c`, alongside
the `SM64_SATURN_C0`/`SM64_SATURN_C1` macros:

```c
/* N64 Vp_t data is expressed in a 320x240 coordinate space -- see
 * gfx_pc.c's SCREEN_HEIGHT (gfx_pc.c:30, config.h:38-39) and the
 * default-viewport formula documented at include/PR/gbi.h:1226-1230,
 * which src/game/area.c's real D_8032CF00 = {{640,480,511,0},...}
 * matches exactly. The Saturn's VDP2 output is only 224 visible
 * scanlines (VDP2_TVMD_VERT_224, castleviewer/main.c:1187), 16 lines
 * short of the N64's 240 -- so real viewport data must be letterboxed,
 * not just have its Y-flip anchor swapped for 224 (an earlier draft of
 * this task did that and produced a 16-line vertical offset error for
 * every real gameplay viewport). */
#define SM64_SATURN_SOURCE_SCREEN_HEIGHT 240
#define SM64_SATURN_TARGET_SCREEN_HEIGHT 224
```

Add two more cases to the `switch (opcode)` in
`sm64_saturn_fast3d_decode_command`, alongside the existing `G_MTX`/
`G_POPMTX` cases:

```c
        case G_MOVEMEM: {
            const uint8_t index = (uint8_t)SM64_SATURN_C0(w0, 0, 8);
            if (index == G_MV_VIEWPORT) {
                const Vp_t *vp = (const Vp_t *)(uintptr_t)w1;
                /* N64 viewport fields carry 2 bits of fraction
                 * (include/PR/gbi.h ~L1222-1230). Reconstruct them
                 * bit-for-bit like gfx_pc.c's gfx_calc_and_set_viewport
                 * (gfx_pc.c:937-955) using the real
                 * SCREEN_HEIGHT=240 anchor, matching real game data
                 * (src/game/area.c's D_8032CF00), then letterbox the
                 * 240-line result down onto the Saturn's 224 visible
                 * lines by cropping 8 lines off top and bottom --
                 * preserving aspect ratio rather than distorting the
                 * image or leaving lines off-screen. */
                const int16_t width = (int16_t)(vp->vscale[0] / 2);
                const int16_t height = (int16_t)(vp->vscale[1] / 2);
                const int16_t source_x =
                    (int16_t)(vp->vtrans[0] / 4 - width / 2);
                const int16_t source_y = (int16_t)(
                    SM64_SATURN_SOURCE_SCREEN_HEIGHT -
                    (vp->vtrans[1] / 4 + height / 2));
                const int16_t letterbox_crop = (int16_t)(
                    (SM64_SATURN_SOURCE_SCREEN_HEIGHT -
                     SM64_SATURN_TARGET_SCREEN_HEIGHT) / 2); /* = 8 */

                frontend->viewport.width = width;
                frontend->viewport.height = height;
                frontend->viewport.x = source_x;
                frontend->viewport.y =
                    (int16_t)(source_y - letterbox_crop);
            }
            break;
        }
        case G_GEOMETRYMODE: {
            const uint32_t clear_mask = ~SM64_SATURN_C0(w0, 0, 24);
            frontend->geometry_mode =
                (frontend->geometry_mode & clear_mask) | w1;
            break;
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: PASS for all three tests. For the real-default-viewport test,
verify by hand: `width=640/2=320`, `height=480/2=240`,
`x=640/4-320/2=160-160=0`, `source_y=240-(480/4+240/2)=240-240=0`,
`y=0-8=-8`. Add all three new test calls to `main()`.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.c tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): decode G_MOVEMEM viewport and G_GEOMETRYMODE"
```

---

### Task 8: Frontend — `G_VTX` decode

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Test: `tools/saturn/runtime_contract_test.c`

- [ ] **Step 1: Write the failing test**

```c
static void test_frontend_g_vtx_transform(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t one_vertex = {
        /* Non-zero, pairwise-distinct position (float under this
         * build's GBI_FLOATS config) and a distinct alpha -- deliberately
         * NOT all-zero. sm64_saturn_fast3d_frontend_init memsets the
         * frontend to 0, so an all-zero fixture (an earlier draft of
         * this test used {0,0,0}) cannot distinguish a correctly
         * decoded position from a dropped assignment or a transposed
         * x/y/z axis -- both would silently read back as (0,0,0)
         * either way. Distinct values per axis close that gap. */
        .ob = {1.5f, -2.25f, 3.0f},
        .flag = 0,
        .tc = {0, 0},
        .cn = {255, 128, 64, 200}
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    Gfx list[3];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    /* gSPVertex(pkt, v, n, v0) under F3DEX_GBI_2:
     * gDma1p(pkt, G_VTX, v, (n<<10)|(sizeof(Vtx)*n-1), v0*2)
     * -> w0 = (G_VTX<<24) | ((n<<10)|(sizeof(Vtx)*n-1)), w1 = v.
     * gfx_sp_vertex's F3DEX_GBI_2 dispatch reads n=C0(12,8),
     * dest_index=C0(1,7)-n (gfx_pc.c:1408) -- NOT the length field
     * gSPVertex packs; the frontend must match the dispatch formula,
     * not the encoding macro, since dest_index is derived differently
     * on the read side. Solving for n=1, dest_index=0:
     * C0(12,8)==1 -> bit 12 set; C0(1,7)-1==0 -> C0(1,7)==1 -> bit 1 set
     * (bit 1 shifted right by 1 in C0(1,7) reads as bit 0 = 1). */
    list[1].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (1U << 1);
    list[1].words.w1 = (uintptr_t)&one_vertex;
    list[2] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.vertices[0].x == 1.5f);
    assert(frontend.vertices[0].y == -2.25f);
    assert(frontend.vertices[0].z == 3.0f);
    assert(frontend.vertices[0].r == 255);
    assert(frontend.vertices[0].g == 128);
    assert(frontend.vertices[0].b == 64);
    assert(frontend.vertices[0].a == 200);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL — `frontend.vertices[0]` remains zeroed.

- [ ] **Step 3: Write minimal implementation**

Add to `sm64_saturn_fast3d_decode_command`'s switch:

```c
        case G_VTX: {
            /* F3DEX_GBI_2 dispatch formula (gfx_pc.c:1408):
             * n_vertices = C0(12,8), dest_index = C0(1,7) - n_vertices.
             * w1 is a raw pointer (this port's display lists reference
             * final-linked addresses, not N64 segments -- consistent
             * with the existing G_DL handling in this file). */
            const uint32_t n_vertices = SM64_SATURN_C0(w0, 12, 8);
            const uint32_t dest_index =
                SM64_SATURN_C0(w0, 1, 7) - n_vertices;
            const Vtx_t *src =
                (const Vtx_t *)(uintptr_t)w1; /* Vtx_t layout, not Vtx_tn */

            for (uint32_t i = 0; i < n_vertices; i++) {
                const uint32_t dest = dest_index + i;
                if (dest >= SM64_SATURN_FAST3D_MAX_VERTICES) {
                    profile->reject_vertex_range++;
                    continue;
                }
                /* src[i].ob is float[3] under GBI_FLOATS (see Task 1's
                 * note) -- assigned directly, no conversion needed since
                 * sm64_saturn_fast3d_vertex_t's position fields are also
                 * float. */
                frontend->vertices[dest].x = src[i].ob[0];
                frontend->vertices[dest].y = src[i].ob[1];
                frontend->vertices[dest].z = src[i].ob[2];
                frontend->vertices[dest].r = src[i].cn[0];
                frontend->vertices[dest].g = src[i].cn[1];
                frontend->vertices[dest].b = src[i].cn[2];
                frontend->vertices[dest].a = src[i].cn[3];
            }
            break;
        }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: PASS. Add the new test call to `main()`.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.c tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): decode G_VTX in Fast3D frontend"
```

---

### Task 9: Frontend — `G_TRI1`/`G_TRI2`: transform, cull, reject, resolve

**Files:**
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.c`
- Test: `tools/saturn/runtime_contract_test.c`

This is the task that actually produces `resolved[]` entries. It
transforms the three indexed vertices through the current MP matrix,
perspective-divides, backface-culls in **pre-viewport, Y-up clip space**
(per the design spec's corrected step 6 — not in the post-viewport
screen-space that `saturn_projected_workarea.h` uses for its own,
separate near/far rejection), maps to viewport pixels, then feeds the
projected corners into the existing `sm64_saturn_projected_quad_analyze`/
`_is_visible` for near/far rejection and `max_z` depth-bucketing.

**Note on vertex-index encoding**: the real `__gsSP1Triangle_w1` encoder
(`include/PR/gbi.h`) packs each vertex index *doubled*
(`_SHIFTL((v0)*2,16,8)|...`), and the read side (`gfx_pc.c:1440`) divides
by 2 back out. Every test below that constructs a `G_TRI1`/`G_TRI2`
command packs indices `0, 2, 4` (i.e. `v0=0, v1=1, v2=2`, each pre-doubled)
for exactly this reason — packing raw `0, 1, 2` would decode to `(0,0,1)`,
silently collapsing the triangle to two coincident vertices.

- [ ] **Step 1: Write the failing test**

```c
#include "saturn_projected_workarea.h"

static void test_frontend_g_tri1_resolves_triangle(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t verts[3] = {
        { .ob = {-100.0f, -100.0f, 500.0f}, .cn = {255, 0, 0, 255} },
        { .ob = { 100.0f, -100.0f, 500.0f}, .cn = {0, 255, 0, 255} },
        { .ob = {   0.0f,  100.0f, 500.0f}, .cn = {0, 0, 255, 255} },
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    sm64_saturn_mtx_t projection;
    Gfx list[4];
    struct SPTask task;
    uint32_t vtx_w0;

    /* n=3, dest_index=0: C0(12,8)==3 and C0(1,7)-3==0 -> C0(1,7)==3. */
    vtx_w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = vtx_w0;
    list[1].words.w1 = (uintptr_t)verts;
    /* F3DEX_GBI_2 G_TRI1 body (gfx_pc.c:1440):
     * gfx_sp_tri1(C0(16,8)/2, C0(8,8)/2, C0(0,8)/2) -- indices are
     * vertex-buffer offsets *2 (see this task's note above). Encoding
     * v0=0, v1=1, v2=2 requires packing (0, 2, 4). */
    list[2].words.w0 = ((uint32_t)G_TRI1 << 24) |
                        (0U << 16) | (2U << 8) | (4U << 0);
    list[2].words.w1 = 0;
    list[3] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    /* Default modelview/projection are both identity (Task 4's
     * sm64_saturn_matrix_stack_init), which makes clip w == 1.0 for
     * every vertex regardless of model-space z -- identity has no
     * perspective term. This test's premise (a triangle at z=500, which
     * should land inside [NEAR_DEPTH=64, FAR_DEPTH=8192] and resolve)
     * needs a projection matrix whose w-column actually depends on z,
     * matching how a real Fast3D projection matrix's M[2][3] entry
     * works. Install one directly so cw == z == 500 for every vertex: */
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16; /* w = z (raw units) */
    projection.m[3][3] = 0;       /* no constant w term */
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.triangles_transformed == 1);
    assert(frontend.profile.reject_near_far == 0);
    assert(frontend.profile.reject_degenerate == 0);
    assert(frontend.resolved_count == 1);
    /* Distinct-corner check: catches an index-encoding regression (e.g.
     * reintroducing the raw-index-not-doubled bug) even if resolved_count
     * happens to stay 1 for some other reason. */
    assert(frontend.resolved[0].x[0] != frontend.resolved[0].x[1] ||
           frontend.resolved[0].y[0] != frontend.resolved[0].y[1]);
    assert(frontend.resolved[0].x[1] != frontend.resolved[0].x[2] ||
           frontend.resolved[0].y[1] != frontend.resolved[0].y[2]);
}

static void test_frontend_g_tri1_backface_cull(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* Same triangle as above but with winding reversed (swap v1/v2) --
     * under G_CULL_BACK this must be rejected, not resolved. */
    static const Vtx_t verts[3] = {
        { .ob = {-100.0f, -100.0f, 500.0f}, .cn = {255, 0, 0, 255} },
        { .ob = {   0.0f,  100.0f, 500.0f}, .cn = {0, 0, 255, 255} },
        { .ob = { 100.0f, -100.0f, 500.0f}, .cn = {0, 255, 0, 255} },
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    sm64_saturn_mtx_t projection;
    Gfx list[5];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24);
    list[1].words.w1 = G_CULL_BACK;
    list[2].words.w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);
    list[2].words.w1 = (uintptr_t)verts;
    list[3].words.w0 = ((uint32_t)G_TRI1 << 24) |
                        (0U << 16) | (2U << 8) | (4U << 0);
    list[3].words.w1 = 0;
    list[4] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16;
    projection.m[3][3] = 0;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    /* Whichever winding is actually front-facing under this port's
     * pre-viewport Y-up convention is an empirical fact to check once
     * this test runs, not assumed here -- if this specific
     * cross-product sign turns out to survive culling instead of being
     * rejected, swap this test's winding (not the implementation) so it
     * exercises the rejected case, then keep test_frontend_g_tri1_resolves_triangle
     * as the surviving-case control. The two tests together must show
     * exactly one winding survives and the other doesn't. */
    assert(frontend.profile.reject_backface == 1);
    assert(frontend.resolved_count == 0);
}

static void test_frontend_g_tri1_modelview_translation_shifts_screen_x(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* Q16.16 modelview matrix: identity plus a +50.0 X translation (row
     * 3, column 0 in the row-vector convention). Under this build's
     * GBI_FLOATS config the wire format is 16 plain floats (see Task
     * 1), so this is written directly as a float array. */
    static const float translate_x50_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        50.0f, 0.0f, 0.0f, 1.0f
    };
    /* All three vertices share model-space x = -100 so the resolved
     * screen x is independent of which vertex-buffer slot G_TRI1's index
     * decode actually selects for each corner -- only y differs, so the
     * triangle stays non-degenerate. */
    static const Vtx_t verts[3] = {
        { .ob = {-100.0f, -100.0f, 500.0f}, .cn = {255, 0, 0, 255} },
        { .ob = {-100.0f,  100.0f, 500.0f}, .cn = {0, 255, 0, 255} },
        { .ob = {-100.0f,    0.0f, 500.0f}, .cn = {0, 0, 255, 255} },
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    sm64_saturn_mtx_t projection;
    Gfx list[5];
    struct SPTask task;
    const uint32_t vtx_w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);

    list[0] = make_g_mtx((uint8_t)(G_MTX_LOAD | G_MTX_MODELVIEW),
                          translate_x50_floats);
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[1].words.w1 = (uintptr_t)&vp;
    list[2].words.w0 = vtx_w0;
    list[2].words.w1 = (uintptr_t)verts;
    list[3].words.w0 = ((uint32_t)G_TRI1 << 24) |
                        (0U << 16) | (2U << 8) | (4U << 0);
    list[3].words.w1 = 0;
    list[4] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    /* Projection still needs a z-dependent w column, matching the other
     * G_TRI1 tests, or every vertex's clip w stays 1.0 and the triangle
     * fails near-plane rejection regardless of this test's own concern
     * (the X translation). */
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16;
    projection.m[3][3] = 0;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.reject_near_far == 0);
    assert(frontend.profile.reject_degenerate == 0);
    assert(frontend.resolved_count == 1);
    /* viewport is {x=0,y=0,width=320,height=224}, so screen_x =
     * 160*cx + 160. With the +50.0 X translation applied, cx = -100+50 =
     * -50 for every vertex here (all three share model x=-100) ->
     * screen_x = 160*(-50)+160 = -7840 for all three corners. This is
     * the assertion Task 15's Mutation 3 (negate a translation term)
     * must break -- unlike the other two G_TRI1 tests above, whose
     * display lists never issue a G_MTX and so leave mp->m[3][0] at
     * exactly 0 (identity*identity), making that same negation an
     * unobservable no-op there. */
    assert(frontend.resolved[0].x[0] == -7840);
    assert(frontend.resolved[0].x[1] == -7840);
    assert(frontend.resolved[0].x[2] == -7840);
}

static void test_frontend_g_tri2_two_triangles(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t verts[6] = {
        /* Triangle A: logical vertex indices 0,1,2, decoded from w0's
         * C0 fields -- same layout G_TRI1 already exercises. */
        { .ob = {-100.0f, -100.0f, 500.0f}, .cn = {255, 0, 0, 255} },
        { .ob = { 100.0f, -100.0f, 500.0f}, .cn = {0, 255, 0, 255} },
        { .ob = {   0.0f,  100.0f, 500.0f}, .cn = {0, 0, 255, 255} },
        /* Triangle B: logical vertex indices 3,4,5, decoded from w1's
         * C1 fields -- this is the branch this test exists to cover.
         * Same shape as Triangle A, translated +20 in x and given
         * distinct colors, so a passing test provably means the C1
         * reads pulled a different vertex set rather than re-reading
         * Triangle A's C0 data. */
        { .ob = {-80.0f, -100.0f, 500.0f}, .cn = {255, 255, 0, 255} },
        { .ob = {120.0f, -100.0f, 500.0f}, .cn = {0, 255, 255, 255} },
        { .ob = { 20.0f,  100.0f, 500.0f}, .cn = {255, 0, 255, 255} },
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    sm64_saturn_mtx_t projection;
    Gfx list[4];
    struct SPTask task;
    uint32_t vtx_w0;

    /* n=6, dest_index=0: C0(12,8)==6 and C0(1,7)-6==0. */
    vtx_w0 = ((uint32_t)G_VTX << 24) | (6U << 12) | (6U << 1);

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = vtx_w0;
    list[1].words.w1 = (uintptr_t)verts;
    /* G_TRI2 (gfx_pc.c ~L1448-1451): first triangle from w0's C0 fields
     * (indices 0,1,2 -> *2 = 0,2,4), second triangle from w1's C1
     * fields (indices 3,4,5 -> *2 = 6,8,10). */
    list[2].words.w0 = ((uint32_t)G_TRI2 << 24) |
                        (0U << 16) | (2U << 8) | (4U << 0);
    list[2].words.w1 = (6U << 16) | (8U << 8) | (10U << 0);
    list[3] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16;
    projection.m[3][3] = 0;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.triangles_transformed == 2);
    assert(frontend.profile.reject_near_far == 0);
    assert(frontend.profile.reject_degenerate == 0);
    assert(frontend.resolved_count == 2);
    /* Triangle A resolves from w0/C0 first, Triangle B from w1/C1
     * second -- distinct colors and screen positions confirm the two
     * triangles came from different vertex slots, not the same C0
     * fields read twice. */
    assert(frontend.resolved[0].color_rgb1555 !=
           frontend.resolved[1].color_rgb1555);
    assert(frontend.resolved[0].x[0] != frontend.resolved[1].x[0] ||
           frontend.resolved[0].y[0] != frontend.resolved[1].y[0]);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL — `triangles_transformed`/`resolved_count` remain 0.

- [ ] **Step 3: Write minimal implementation**

Add a static helper above `sm64_saturn_fast3d_decode_command` for the
per-triangle pipeline:

```c
/* Calibrated in raw, unscaled world/model units -- matching
 * castleviewer's own NEAR_DEPTH=128/FAR_DEPTH=8192 convention
 * (castleviewer/main.c:36-37), where its projected z is stored raw (a
 * single >>16-reduced dot product, never re-multiplied by 65536). This
 * increment's cw (clip-space w, in the same raw units once divided by
 * 65536.0f below) must be stored the same way -- do NOT multiply by
 * 65536.0f again before pushing into the workarea, or every real
 * triangle's depth would appear to be tens of thousands of units out of
 * range and get near/far-rejected. */
#define SM64_SATURN_NEAR_DEPTH 64
#define SM64_SATURN_FAR_DEPTH 8192
#define SM64_SATURN_MAX_PROJECTED_SPAN 640

/* Physical screen width -- unlike height, this doesn't differ between
 * the N64 source (320x240) and the Saturn target (320x224), so there is
 * no source/target split the way SM64_SATURN_SOURCE_SCREEN_HEIGHT/
 * SM64_SATURN_TARGET_SCREEN_HEIGHT have (Task 7). Used below for
 * clip_viewport's true-screen bounds, alongside the existing
 * SM64_SATURN_TARGET_SCREEN_HEIGHT constant Task 7 already defined. */
#define SM64_SATURN_TARGET_SCREEN_WIDTH 320

static void
sm64_saturn_fast3d_resolve_triangle(sm64_saturn_fast3d_frontend_t *frontend,
                                    uint8_t i0, uint8_t i1, uint8_t i2)
{
    sm64_saturn_fast3d_profile_t *profile = &frontend->profile;
    const sm64_saturn_mtx_t *mp =
        sm64_saturn_matrix_stack_mp(&frontend->matrix_stack);
    const uint8_t idx[3] = {i0, i1, i2};
    float cx[3], cy[3], cw[3]; /* pre-viewport clip-space x/w, y/w, and
                                 * raw w (NOT further scaled -- see the
                                 * NEAR/FAR_DEPTH comment above) */
    sm64_saturn_projected_vertex_t projected_storage[4];
    sm64_saturn_projected_workarea_t workarea;
    uint16_t projected_indices[4];
    sm64_saturn_projected_quad_t quad;
    /* Deliberately NOT built from frontend->viewport: that struct's
     * width/height are the NDC-to-pixel scale factors (correct, and
     * needed unchanged below for screen_x/screen_y), but its y origin
     * carries Task 7's 8-line letterbox crop applied to a viewport
     * whose height/width still span the full pre-crop N64 240-line
     * rect. Reusing those fields here would make clip_viewport's bounds
     * an 8px-per-edge superset of the Saturn's true visible
     * [0,320)x[0,224) area, letting triangles that live entirely inside
     * that dead border strip pass this rejection check and consume a
     * resolved[]/VDP1-arena slot for geometry that is never actually
     * drawn. Use the true physical screen bounds instead -- found during
     * Task 7's review, fixed here since this is the first place
     * clip_viewport is constructed.
     *
     * Known, deliberately deferred limitation (also from Task 7's
     * review): real SM64 code submits non-full-height viewports outside
     * ordinary gameplay -- Peach's ending cutscene and the credits-zoom
     * (both src/game/mario_actions_cutscene.c) and the Goddard face
     * screen (src/goddard/renderer.c) all use vscale/vtrans shapes this
     * frontend's letterbox math wasn't designed around. This increment's
     * scope is gameplay-frame rendering (Bob-omb Battlefield), not those
     * presentation states, so this is left as a known, documented gap
     * rather than solved here. */
    const sm64_saturn_viewport_t clip_viewport = {
        .left = 0,
        .top = 0,
        .right = SM64_SATURN_TARGET_SCREEN_WIDTH,
        .bottom = SM64_SATURN_TARGET_SCREEN_HEIGHT
    };
    int16_t screen_x[3], screen_y[3];

    profile->triangles_transformed++;

    for (int c = 0; c < 3; c++) {
        if (idx[c] >= SM64_SATURN_FAST3D_MAX_VERTICES) {
            profile->reject_vertex_range++;
            return;
        }
        const sm64_saturn_fast3d_vertex_t *v = &frontend->vertices[idx[c]];
        /* Row-vector transform, matching gfx_pc.c's gfx_sp_vertex
         * (~L616-619): out[col] = sum_row v[row]*M[row][col] + M[3][col].
         * Uses float here for the perspective divide/cull math -- v->x/
         * y/z are already float (GBI_FLOATS, see Task 5's note), and
         * mp's Q16.16 entries are divided back to float for this scratch
         * computation. This is fine for a first, correctness-focused
         * pass; a later perf-motivated increment can revisit whether
         * this per-triangle math should move to fixed point once real
         * SH-2 profiling data exists. */
        const float mx = v->x, my = v->y, mz = v->z;
        const float x = mx * (mp->m[0][0] / 65536.0f) +
                        my * (mp->m[1][0] / 65536.0f) +
                        mz * (mp->m[2][0] / 65536.0f) +
                        (mp->m[3][0] / 65536.0f);
        const float y = mx * (mp->m[0][1] / 65536.0f) +
                        my * (mp->m[1][1] / 65536.0f) +
                        mz * (mp->m[2][1] / 65536.0f) +
                        (mp->m[3][1] / 65536.0f);
        const float w = mx * (mp->m[0][3] / 65536.0f) +
                        my * (mp->m[1][3] / 65536.0f) +
                        mz * (mp->m[2][3] / 65536.0f) +
                        (mp->m[3][3] / 65536.0f);
        if (w <= 0.0f) {
            profile->reject_near_far++;
            return;
        }
        cx[c] = x / w;
        cy[c] = y / w;
        cw[c] = w;
    }

    /* Backface cull in pre-viewport, Y-up clip space -- matching both
     * castleviewer's view_triangle_facing/view_triangle_is_culled
     * (main.c:503-518, pre-projection 3D view space) and the
     * reference's gfx_sp_tri1 (gfx_pc.c:729-751, perspective-divided
     * x/w,y/w clip space). Do NOT move this after the viewport map
     * below -- that space is Y-down (see the viewport decode's Y-flip
     * in Task 7) and would invert this sign. */
    if (frontend->geometry_mode & G_CULL_BOTH) {
        const float dx1 = cx[0] - cx[1];
        const float dy1 = cy[0] - cy[1];
        const float dx2 = cx[2] - cx[1];
        const float dy2 = cy[2] - cy[1];
        float cross = dx1 * dy2 - dy1 * dx2;

        switch (frontend->geometry_mode & G_CULL_BOTH) {
            case G_CULL_FRONT:
                if (cross <= 0.0f) { profile->reject_backface++; return; }
                break;
            case G_CULL_BACK:
                if (cross >= 0.0f) { profile->reject_backface++; return; }
                break;
            case G_CULL_BOTH:
                profile->reject_backface++;
                return;
        }
    }

    for (int c = 0; c < 3; c++) {
        screen_x[c] = (int16_t)(frontend->viewport.x +
            (cx[c] * 0.5f + 0.5f) * frontend->viewport.width);
        screen_y[c] = (int16_t)(frontend->viewport.y +
            (1.0f - (cy[c] * 0.5f + 0.5f)) * frontend->viewport.height);
    }

    sm64_saturn_projected_workarea_init(&workarea, projected_storage, 4);
    for (int c = 0; c < 3; c++) {
        (void)sm64_saturn_projected_workarea_push(
            &workarea,
            (sm64_saturn_projected_vertex_t){
                screen_x[c], screen_y[c],
                (int32_t)cw[c] /* raw units -- do not scale by 65536.0f
                                 * here, see the NEAR/FAR_DEPTH comment
                                 * above this function */
            },
            &projected_indices[c]);
    }
    /* (i0, i1, i2, i2) -- last vertex duplicated -- the degenerate-quad
     * convention this port introduces fresh (see design spec's "Crude
     * emission path"). */
    projected_indices[3] = projected_indices[2];

    /* Design spec step 5 (near/far + over-span reject) runs before step
     * 7 (degenerate reject) below -- step 7's own text is explicit that
     * it guards a triangle that "survives the above", i.e. this check. */
    if (!sm64_saturn_projected_quad_analyze(&workarea, projected_indices,
                                            &clip_viewport, &quad) ||
        !sm64_saturn_projected_quad_is_visible(&quad,
            SM64_SATURN_NEAR_DEPTH, SM64_SATURN_FAR_DEPTH,
            SM64_SATURN_MAX_PROJECTED_SPAN)) {
        profile->reject_near_far++;
        return;
    }

    if ((int32_t)screen_x[0] == screen_x[1] &&
        (int32_t)screen_x[1] == screen_x[2] &&
        (int32_t)screen_y[0] == screen_y[1] &&
        (int32_t)screen_y[1] == screen_y[2]) {
        profile->reject_degenerate++;
        return;
    }

    if (frontend->resolved_count >=
        SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES) {
        profile->reject_command_capacity++;
        return;
    }

    sm64_saturn_resolved_triangle_t *out =
        &frontend->resolved[frontend->resolved_count++];
    for (int c = 0; c < 3; c++) {
        out->x[c] = screen_x[c];
        out->y[c] = screen_y[c];
    }
    /* Flat color: vertex 0's, matching G_SHADE-off flat-shading
     * convention -- Gouraud is out of scope for this increment. */
    out->color_rgb1555 = (uint16_t)(
        ((frontend->vertices[idx[0]].r >> 3) << 10) |
        ((frontend->vertices[idx[0]].g >> 3) << 5) |
        (frontend->vertices[idx[0]].b >> 3));
    /* max_z buckets correctly for this (i0,i1,i2,i2) convention -- do
     * not switch this to quad.center_z, which reads only indices[0]/[2]
     * and would silently drop i1's depth (see design spec's "Painter
     * ordering reuse" finding). */
    out->depth_bucket = (uint16_t)(((int64_t)(quad.max_z - SM64_SATURN_NEAR_DEPTH) *
        (SM64_SATURN_FAST3D_DEPTH_BUCKETS - 1)) /
        (SM64_SATURN_FAR_DEPTH - SM64_SATURN_NEAR_DEPTH));
    profile->triangles_emitted++;
}
```

Add `G_TRI1`/`G_TRI2` cases to `sm64_saturn_fast3d_decode_command`'s switch:

```c
        case (uint8_t)G_TRI1: {
            /* F3DEX_GBI_2 body (gfx_pc.c:1440):
             * gfx_sp_tri1(C0(16,8)/2, C0(8,8)/2, C0(0,8)/2). */
            sm64_saturn_fast3d_resolve_triangle(
                frontend,
                (uint8_t)(SM64_SATURN_C0(w0, 16, 8) / 2),
                (uint8_t)(SM64_SATURN_C0(w0, 8, 8) / 2),
                (uint8_t)(SM64_SATURN_C0(w0, 0, 8) / 2));
            break;
        }
#if defined(F3DEX_GBI) || defined(F3DLP_GBI) || defined(F3DEX_GBI_2)
        case (uint8_t)G_TRI2: {
            /* Two triangles per command: first from w0's C0 fields
             * (same layout as G_TRI1), second from w1's C1 fields
             * (gfx_pc.c ~L1448-1451). */
            sm64_saturn_fast3d_resolve_triangle(
                frontend,
                (uint8_t)(SM64_SATURN_C0(w0, 16, 8) / 2),
                (uint8_t)(SM64_SATURN_C0(w0, 8, 8) / 2),
                (uint8_t)(SM64_SATURN_C0(w0, 0, 8) / 2));
            sm64_saturn_fast3d_resolve_triangle(
                frontend,
                (uint8_t)(SM64_SATURN_C1(w1, 16, 8) / 2),
                (uint8_t)(SM64_SATURN_C1(w1, 8, 8) / 2),
                (uint8_t)(SM64_SATURN_C1(w1, 0, 8) / 2));
            break;
        }
#endif
```

Add the `#include "saturn_projected_workarea.h"` at the top of
`saturn_fast3d_frontend.c`, alongside the existing includes.

- [ ] **Step 4: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: PASS. If `test_frontend_g_tri1_backface_cull` fails because the
*other* winding turns out to be the one culled, follow the test's own
note: swap that test's vertex winding (not the implementation) and rerun,
then confirm `test_frontend_g_tri1_resolves_triangle` still resolves (not
rejects) with its original winding -- the pair must land on opposite
sides. Add all new test calls to `main()`.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_frontend.c tools/saturn/runtime_contract_test.c
git commit -m "feat(saturn): decode G_TRI1/G_TRI2, transform/cull/reject/resolve"
```

---

### Task 10: `saturn_vdp1_backend.h` — embedded list, caller-storage init, zero-init

**Files:**
- Modify: `src/port/saturn/gfx/saturn_vdp1_backend.h`

No host test: this file includes `<yaul.h>` and cannot compile under the
host's native `$(CC)`. Acceptance for this task is the cross-compiled
build (Task 14's `make` step) plus the existing callers (`castleviewer`,
`marioturntable`) continuing to build unchanged, since all access to
`backend->list` is internal to this header (verified during planning —
neither caller touches `.list` directly).

- [ ] **Step 1: Add the `<stdlib.h>` include**

Add `#include <stdlib.h>` to this file's include block — needed below
for `memalign`. The file currently includes only `<string.h>` and
`<yaul.h>`; no header reachable from `<yaul.h>` declares `memalign`
transitively. Omitting this is not a style nit: the pinned sh-elf-gcc
14.3.0 toolchain treats implicit-function-declaration as a hard error by
default under `-std=c11`, which would break Task 14's cross-compiled
build entirely.

```c
#include <stdlib.h>
#include <string.h>
#include <yaul.h>

#include "saturn_command_arena.h"
```

- [ ] **Step 2: Change the struct and existing init**

Change `sm64_saturn_vdp1_backend_t`:

```c
typedef struct sm64_saturn_vdp1_backend {
    vdp1_cmdt_list_t list;
    sm64_saturn_command_arena_t commands;
} sm64_saturn_vdp1_backend_t;
```

(was `vdp1_cmdt_list_t *list;`). Update every existing `backend->list->`
access in this file to `backend->list.` instead. This is the full list of
sites in the current file, including the allocation and null-check lines:

```c
sm64_saturn_vdp1_backend_init (current lines 26-38):
    backend->list = vdp1_cmdt_list_alloc(capacity);   /* removed below --
        `list` is no longer a pointer, so vdp1_cmdt_list_alloc() can't
        populate it; replaced by memalign() + vdp1_cmdt_list_init() */
    if (backend->list == NULL)                        /* removed below --
        replaced by a NULL check on the local `cmdts` pointer instead */
        return false;
    (void)memset(backend->list.cmdts, 0, sizeof(vdp1_cmdt_t) * capacity);
    vdp1_cmdt_system_clip_coord_set(&backend->list.cmdts[0]);
    vdp1_cmdt_vtx_system_clip_coord_set(&backend->list.cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&backend->list.cmdts[1]);
    vdp1_cmdt_vtx_local_coord_set(&backend->list.cmdts[1], local);
    vdp1_cmdt_end_set(&backend->list.cmdts[2]);
    backend->list.count = 3;

sm64_saturn_vdp1_backend_begin:
    vdp1_cmdt_end_clear(&backend->list.cmdts[...]);

sm64_saturn_vdp1_backend_reserve:
    return &backend->list.cmdts[first];

sm64_saturn_vdp1_backend_finish:
    vdp1_cmdt_end_set(&backend->list.cmdts[...]);
    backend->list.count = backend->commands.live_count;

sm64_saturn_vdp1_backend_upload:
    vdp1_sync_cmdt_list_put(&backend->list, 0);
```

Replace the full body of `sm64_saturn_vdp1_backend_init` with:

```c
static inline bool
sm64_saturn_vdp1_backend_init(sm64_saturn_vdp1_backend_t *backend,
                              uint16_t capacity, int16_vec2_t clip,
                              int16_vec2_t local)
{
    if (capacity <= 2U)
        return false;

    /* memalign, not malloc: vdp1_cmdt_t is __aligned(32)
     * (yaul/vdp1/cmdt.h), and this project's malloc is TLSF-backed
     * (.yaul.env: YAUL_OPTION_MALLOC_IMPL=tlsf) -- on this 32-bit target
     * TLSF only guarantees 4-byte alignment, far short of the required
     * 32. This preserves the exact allocation behavior of the
     * vdp1_cmdt_list_alloc() call this replaces (which itself calls
     * memalign(sizeof(vdp1_cmdt_t), ...) internally); only the storage
     * of the 8-byte list header changed shape, from a second heap
     * allocation to this struct's embedded `list` field. */
    vdp1_cmdt_t *cmdts = memalign(sizeof(vdp1_cmdt_t),
                                  sizeof(vdp1_cmdt_t) * capacity);
    if (cmdts == NULL)
        return false;
    vdp1_cmdt_list_init(&backend->list, cmdts);

    (void)memset(backend->list.cmdts, 0, sizeof(vdp1_cmdt_t) * capacity);
    sm64_saturn_command_arena_init(&backend->commands, capacity, 2);
    vdp1_cmdt_system_clip_coord_set(&backend->list.cmdts[0]);
    vdp1_cmdt_vtx_system_clip_coord_set(&backend->list.cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&backend->list.cmdts[1]);
    vdp1_cmdt_vtx_local_coord_set(&backend->list.cmdts[1], local);
    vdp1_cmdt_end_set(&backend->list.cmdts[2]);
    backend->list.count = 3;
    return true;
}
```

Update the other four functions' bodies to use `backend->list.` instead
of `backend->list->` (mechanical, per the site list above).

- [ ] **Step 3: Add the caller-storage init entry point**

```c
/* Caller-supplied (e.g. LWRAM) storage variant of
 * sm64_saturn_vdp1_backend_init. `cmdts` must point to at least
 * `capacity` vdp1_cmdt_t entries and must outlive the backend. Unlike
 * the heap path above, this performs no allocation.
 *
 * Zero-init note: neither vdp1_cmdt_list_init() nor any linker-driven
 * zero-init covers caller-supplied storage outside .bss (a new LWRAM
 * output section is not .bss and crt0's _bss_clear() never visits it --
 * see the design spec's "VDP1 backend entry point" finding). This
 * function must therefore memset explicitly, exactly mirroring the
 * heap path above. */
static inline bool
sm64_saturn_vdp1_backend_init_with_storage(sm64_saturn_vdp1_backend_t *backend,
                                           vdp1_cmdt_t *cmdts,
                                           uint16_t capacity,
                                           int16_vec2_t clip,
                                           int16_vec2_t local)
{
    if (capacity <= 2U || cmdts == NULL)
        return false;

    vdp1_cmdt_list_init(&backend->list, cmdts);
    (void)memset(backend->list.cmdts, 0, sizeof(vdp1_cmdt_t) * capacity);
    sm64_saturn_command_arena_init(&backend->commands, capacity, 2);
    vdp1_cmdt_system_clip_coord_set(&backend->list.cmdts[0]);
    vdp1_cmdt_vtx_system_clip_coord_set(&backend->list.cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&backend->list.cmdts[1]);
    vdp1_cmdt_vtx_local_coord_set(&backend->list.cmdts[1], local);
    vdp1_cmdt_end_set(&backend->list.cmdts[2]);
    backend->list.count = 3;
    return true;
}
```

- [ ] **Step 4: Verify existing callers still build**

This can't be checked until Task 14's full cross-compiled build, since it
needs the Yaul toolchain. Note it here as a checkpoint to confirm at that
point: `castleviewer` and `marioturntable` (the two existing consumers of
`sm64_saturn_vdp1_backend_init`) must build with zero changes to their own
source, since neither touches `backend.list`/`backend->list` directly —
only through this header's public functions.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_vdp1_backend.h
git commit -m "feat(saturn): add caller-storage init to VDP1 backend"
```

---

### Task 11: `saturn_fast3d_vdp1_emit.{h,c}` — new emission adapter

**Files:**
- Create: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h`
- Create: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`

No host test (Yaul-dependent, cross-compiled only — see "Decomposition
decision" at the top of this plan). Acceptance is the Task 14 build.

- [ ] **Step 1: Create the header**

```c
#ifndef SM64_SATURN_FAST3D_VDP1_EMIT_H
#define SM64_SATURN_FAST3D_VDP1_EMIT_H

#include "saturn_fast3d_frontend.h"
#include "saturn_vdp1_backend.h"

/* Walks frontend->resolved[] in depth-bucket order (far-to-near) and
 * writes real VDP1 degenerate-quad polygon commands via backend. This is
 * the only Yaul-dependent half of the Fast3D lowering pipeline --
 * saturn_fast3d_frontend.c itself stays Yaul-free so it remains
 * host-testable (see the design spec's review finding on this exact
 * compile-boundary problem). */
void sm64_saturn_fast3d_vdp1_emit(sm64_saturn_fast3d_frontend_t *frontend,
                                  sm64_saturn_vdp1_backend_t *backend);

#endif
```

- [ ] **Step 2: Create the implementation**

```c
#include "saturn_fast3d_vdp1_emit.h"

void sm64_saturn_fast3d_vdp1_emit(sm64_saturn_fast3d_frontend_t *frontend,
                                  sm64_saturn_vdp1_backend_t *backend)
{
    sm64_saturn_fast3d_profile_t *profile = &frontend->profile;

    sm64_saturn_vdp1_backend_begin(backend);

    /* Far-to-near: iterate buckets from SM64_SATURN_FAST3D_DEPTH_BUCKETS-1
     * down to 0, emitting every resolved triangle whose bucket matches,
     * so nearer geometry draws last (painter's algorithm). O(buckets *
     * triangles) is acceptable at this increment's bounded
     * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES scale. */
    for (int32_t bucket = SM64_SATURN_FAST3D_DEPTH_BUCKETS - 1;
         bucket >= 0; bucket--) {
        for (uint16_t i = 0; i < frontend->resolved_count; i++) {
            const sm64_saturn_resolved_triangle_t *tri =
                &frontend->resolved[i];
            if (tri->depth_bucket != (uint16_t)bucket) {
                continue;
            }

            vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(backend, 1);
            if (cmdt == NULL) {
                /* Distinct from the frontend's own reject_command_capacity
                 * (that one is the resolved-triangle buffer, a different
                 * ceiling than this VDP1 command arena -- see
                 * saturn_fast3d_frontend.h's comment on both fields). */
                profile->reject_vdp1_arena_capacity++;
                continue;
            }

            /* (i0, i1, i2, i2) degenerate quad -- last vertex duplicated,
             * matching the convention this port introduces in the
             * frontend's triangle resolve step. */
            const int16_vec2_t quad_vertices[4] = {
                INT16_VEC2_INITIALIZER(tri->x[0], tri->y[0]),
                INT16_VEC2_INITIALIZER(tri->x[1], tri->y[1]),
                INT16_VEC2_INITIALIZER(tri->x[2], tri->y[2]),
                INT16_VEC2_INITIALIZER(tri->x[2], tri->y[2])
            };

            vdp1_cmdt_polygon_set(cmdt);
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_REPLACE
            });
            vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                .raw = tri->color_rgb1555
            });
            vdp1_cmdt_vtx_set(cmdt, quad_vertices);
            profile->triangles_vdp1_emitted++;
        }
    }

    sm64_saturn_vdp1_backend_finish(backend);
    sm64_saturn_vdp1_backend_upload(backend);
}
```

- [ ] **Step 3: Commit**

```bash
git add src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c
git commit -m "feat(saturn): add depth-ordered VDP1 emission adapter"
```

---

### Task 12: `sourceboot-cart.x` — add the `lwram` region

**Files:**
- Modify: `src/port/saturn/sourceboot/sourceboot-cart.x`

- [ ] **Step 1: Add the MEMORY region**

Modify the `MEMORY { ... }` block (currently declares only `ram` and
`cart`):

```
MEMORY {
  ram   (Wx) : ORIGIN = 0x06004000, LENGTH = 0x000FC000
  lwram (Wx) : ORIGIN = 0x00200000, LENGTH = 0x00100000
  cart  (R)  : ORIGIN = 0x22400000, LENGTH = 0x00400000
}
```

- [ ] **Step 2: Add the output section**

Add a new output section for the LWRAM-resident `vdp1_cmdt_t` array,
placed in `SECTIONS { ... }` (after the existing `.uncached` section, per
this file's existing ordering convention of "unusual/special sections
last"):

```
  .lwram_cmdts (NOLOAD) :
  {
    . = ALIGN (32); /* vdp1_cmdt_t is __aligned(32) */
    *(.lwram_cmdts)
  } > lwram
```

(`NOLOAD` is correct here: this section holds no initialized data — Task
14 declares the actual C array with `__attribute__((section(".lwram_cmdts")))`,
zeroed explicitly at runtime by `sm64_saturn_vdp1_backend_init_with_storage`'s
memset, exactly like the existing HWRAM `.bss` convention, except this
region is never crt0-zeroed since it isn't `.bss`.)

- [ ] **Step 3: Verify `make verify` still passes**

Run immediately, per the design spec's explicit instruction to verify this
specific edit before writing any decode logic on top of it:

```
C:\msys64\usr\bin\bash.exe -lc "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/src/port/saturn/sourceboot && source ../../../../.yaul.env && make -j2 && make verify"
```

Expected: `make verify` succeeds — same E2 entry-point (`0x6004000`) and
`.cart_rodata` VMA (`22400000`) checks as before this change. If either
check fails, the `lwram` region's placement is disturbing something in the
existing section layout — do not proceed to Task 14 until this passes.

- [ ] **Step 4: Commit**

```bash
git add src/port/saturn/sourceboot/sourceboot-cart.x
git commit -m "feat(saturn): add lwram linker region for VDP1 command staging"
```

---

### Task 13: `Makefile.saturn.mk` — add `verify-runtime-contracts` to `verify-all`

**Files:**
- Modify: `Makefile.saturn.mk`

Note: this task is scoped narrowly to the `verify-all` prerequisite list
only. The `verify-runtime-contracts` recipe's compile flags were already
fixed in Task 5.5 — this task does not touch them again.

- [ ] **Step 1: Modify the target**

Change:
```makefile
verify-all: verify-tools classify-source verify-hello verify-hwtest
```
to:
```makefile
verify-all: verify-tools verify-runtime-contracts classify-source verify-hello verify-hwtest
```

- [ ] **Step 2: Run to verify**

Run: `make -f Makefile.saturn.mk verify-all`
Expected: succeeds, now including the native contract-test build/run as
part of the standard chain (this closes the gate gap the design spec
flagged — confirmed by you before this plan was written).

- [ ] **Step 3: Commit**

```bash
git add Makefile.saturn.mk
git commit -m "build(saturn): add runtime-contracts to verify-all gate"
```

---

### Task 14: Wire it all together in `sourceboot/main.c`

**Files:**
- Modify: `src/port/saturn/sourceboot/main.c`

- [ ] **Step 1: Declare LWRAM storage and the backend**

Add near the top of `main.c`, alongside the existing
`sourceboot_main_pool`/`sourceboot_fast3d` statics:

```c
#include "saturn_fast3d_vdp1_emit.h"
#include "saturn_vdp1_backend.h"

#define SOURCEBOOT_VDP1_COMMAND_CAPACITY 512U

/* LWRAM-resident command staging -- see sourceboot-cart.x's new lwram
 * MEMORY region/.lwram_cmdts section. Zeroed explicitly by
 * sm64_saturn_vdp1_backend_init_with_storage below, since this section
 * is not .bss and crt0 never visits it. */
static vdp1_cmdt_t sourceboot_vdp1_cmdts[SOURCEBOOT_VDP1_COMMAND_CAPACITY]
    __attribute__((section(".lwram_cmdts")));
static sm64_saturn_vdp1_backend_t sourceboot_vdp1_backend;
```

- [ ] **Step 2: Initialize the backend after cart load succeeds**

In `main()`, after the existing
`sm64_saturn_source_runtime_configure(...)` call and before
`main_pool_init(...)`:

```c
    {
        const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
        const int16_vec2_t local = INT16_VEC2_INITIALIZER(0, 0);
        if (!sm64_saturn_vdp1_backend_init_with_storage(
                &sourceboot_vdp1_backend, sourceboot_vdp1_cmdts,
                SOURCEBOOT_VDP1_COMMAND_CAPACITY, clip, local)) {
            dbgio_puts("sourceboot: VDP1 backend init failed\n");
            dbgio_flush();
            for (;;) {}
        }
    }
```

- [ ] **Step 3: Emit once per frame**

Modify the existing loop:

```c
    thread5_game_loop(NULL);
    for (;;) {
        game_loop_one_iteration();
        sm64_saturn_fast3d_vdp1_emit(&sourceboot_fast3d,
                                     &sourceboot_vdp1_backend);
    }
```

- [ ] **Step 4: Cross-compiled build**

Run:
```powershell
C:\msys64\usr\bin\bash.exe -lc "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/src/port/saturn/sourceboot && source ../../../../.yaul.env && make source-assets -j2 && make -j2 && make verify"
```
Expected: builds clean, `make verify` still passes (E2 entry-point and
`.cart_rodata` VMA checks unaffected by this task's changes). Resolve the
new `g_sm64_saturn_source_cart_probe` symbol address from the generated
map file if you need it for a later manual capture — it is build output,
not an ABI, per the handoff's own caution.

If HWRAM `.bss` overflows the `ram` region (link error), that means the
matrix stack + vertex buffer + embedded `vdp1_cmdt_list_t` header exceeded
the ~9.6 KB budget measured in the design spec — reduce
`SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES` first (a HWRAM cost), not
`SOURCEBOOT_VDP1_COMMAND_CAPACITY` (which lives in LWRAM, with ~1 MiB
free).

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/sourceboot/main.c
git commit -m "feat(saturn): wire VDP1 emission into sourceboot main loop"
```

---

### Task 15: Mutation testing pass

**Files:**
- Modify (temporarily, then restore): `src/port/saturn/gfx/saturn_matrix.h`, `src/port/saturn/gfx/saturn_fast3d_frontend.c`

Per the project's global mutation-testing rule and the design spec's
explicit list. Apply each mutation, confirm a test fails, then restore.

- [ ] **Step 1: Mutation 1 — flip the `>>16` shift direction**

In `sm64_saturn_matrix_mul`, change `sum >> 16` to `sum << 16`.
Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL (multiple matrix tests break). Restore the `>>16`.

- [ ] **Step 2: Mutation 2 — swap a row/column index in the decode**

In `sm64_saturn_matrix_decode`, change `out->m[i][j]` to `out->m[j][i]`.
Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL (`test_matrix_decode_translation` breaks, since its fixture
is deliberately non-symmetric — row 3 carries the translation, no other
row does). Restore.

- [ ] **Step 3: Mutation 3 — negate a translation term**

In `sm64_saturn_fast3d_resolve_triangle`'s vertex transform, change
`(mp->m[3][0] / 65536.0f)` to `-(mp->m[3][0] / 65536.0f)`.
Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL — `test_frontend_g_tri1_modelview_translation_shifts_screen_x`
(Task 9) asserts `resolved[0].x[0..2] == -7840`; under this mutation the
loaded +50.0 X translation is applied with the wrong sign, producing
`screen_x == -23840` instead, which fails that assertion.
`test_frontend_g_tri1_resolves_triangle` and the backface test do **not**
catch this mutation: neither display list issues a `G_MTX`, so the
modelview and projection matrices stay at their identity defaults
throughout, `mp` is `identity * identity = identity`, and `mp->m[3][0]`
is exactly 0 in both — negating a value that is always 0 is a no-op.
Restore.

- [ ] **Step 4: Mutation 4 — flip the F3DEX2 push-XOR**

In `sm64_saturn_fast3d_decode_command`'s `G_MTX` case, change
`SM64_SATURN_C0(w0, 0, 8) ^ G_MTX_PUSH` to
`SM64_SATURN_C0(w0, 0, 8)` (remove the XOR).
Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL — `test_frontend_g_mtx_load_modelview` submits the
pre-XORed wire byte `(G_MTX_LOAD | G_MTX_MODELVIEW) ^ G_MTX_PUSH` (0x03).
Correct code decodes this back to `G_MTX_LOAD` only (0x02, no push bit),
so depth stays at 1. Removing the XOR decodes the same wire byte as the
raw 0x03 = `G_MTX_PUSH | G_MTX_LOAD`, which pushes a stack level, taking
depth to 2 and breaking `assert(frontend.matrix_stack.depth == 1)`.
Restore.

- [ ] **Step 5: Mutation 5 — off-by-one the stack depth guard**

In `sm64_saturn_matrix_stack_push`, change
`stack->depth >= SM64_SATURN_MATRIX_STACK_DEPTH` to
`stack->depth > SM64_SATURN_MATRIX_STACK_DEPTH`.
Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL (`test_matrix_stack_overflow` allows an 11th push,
depth reaches 12, breaking the fixed-size `entries[11]` array's bound —
at minimum `stack.depth == 11` after the loop fails since it now reads 12).
Restore.

- [ ] **Step 6: Mutation 6 — overflow guard**

In `sm64_saturn_matrix_mul`, change
`narrowed > INT32_MAX || narrowed < INT32_MIN` to `false` (never flag
overflow).
Run: `make -f Makefile.saturn.mk verify-runtime-contracts`
Expected: FAIL (`test_matrix_multiply_overflow_guard` asserts
`overflowed` is true). Restore.

- [ ] **Step 7: Record survival rate and restore all mutations**

All 6 mutations above should have failed a test (0% survival). If any
mutation did NOT cause a test failure, strengthen that specific test per
the mutation step's own note before moving on — per the project's global
rule, >20% survival means the tests need work. Confirm the final restored
state matches Task 1-14's committed code exactly:

```bash
git diff
```
Expected: empty (all mutations restored, nothing uncommitted).

---

### Task 16: Full regression pass

**Files:** none (verification only)

- [ ] **Step 1: Run the full sourceboot build and verify chain**

```powershell
C:\msys64\usr\bin\bash.exe -lc "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/src/port/saturn/sourceboot && source ../../../../.yaul.env && make source-assets -j2 && make -j2 && make verify"
```
Expected: succeeds, same E2/`.cart_rodata` checks as the handoff's
baseline.

- [ ] **Step 2: Run the repo-root host verify-all chain**

```powershell
.\sm64-port\.venv-saturn-tools\Scripts\python.exe -c "import subprocess; subprocess.run(['make', '-f', 'Makefile.saturn.mk', 'verify-all'], check=True)"
```
(or, from the msys2 bash shell used elsewhere in this repo:
`make -f Makefile.saturn.mk verify-all`)

Expected: succeeds — 59 existing Python tests unaffected, plus the native
contract-test binary (now including every `test_matrix_*`/`test_frontend_*`
added in Tasks 1-9) via the `verify-runtime-contracts` prerequisite added
in Task 13, built with the corrected recipe from Task 5.5.

- [ ] **Step 3: Confirm the headless no-cart negative control is unaffected**

This increment doesn't touch `source_cart.c`/the cart-load path, so the
existing negative control should be unchanged. Re-run it only if you want
a fresh capture for the record — it is not a new acceptance requirement of
this plan:

```powershell
.\sm64-port\.venv-saturn-tools\Scripts\python.exe .\sm64-port\tools\saturn\capture_hwtest.py `
  --ymir "D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent\apps\ymir-headless\Release\ymir-headless.exe" `
  --ipl "C:\Users\estee\AppData\Local\Temp\Sega Saturn BIOS (USA).bin" `
  --game .\sm64-port\build\saturn\sourceboot\e2-bob\sm64-saturn-sourceboot-e2.cue `
  --bios-input --frames 240 --handoff-yield --post-poke-frames 900 `
  --probe-address <resolve-from-current-build-map> --probe-count 28 --allow-invalid `
  --output .\sm64-port\build\saturn\sourceboot\e2-bob\ymir-sourceboot-no-cart-yield-probe.json
```

- [ ] **Step 4: Update the visual timeline only if you also captured a frame**

This plan's acceptance does not require a screenshot (per the design
spec's corrected acceptance criteria — that remains gated on you manually
driving the cart-profiled Ymir GUI to a `READY` probe). If you do capture
one after this plan lands, add it to
`docs/saturn/evidence/TIMELINE.md` as its own follow-up commit, explicitly
labelled untextured/flat-shaded, per the handoff's visual-gate wording —
not as part of this plan's commits.

- [ ] **Step 5: Final commit**

If Steps 1-2 required any fixes beyond what earlier tasks committed:

```bash
git add -A
git commit -m "fix(saturn): resolve regressions found in full verify pass"
```

If nothing needed fixing, there is nothing to commit here — Tasks 1-14
already committed everything.

---

## Follow-up outside this plan: correct the design spec's GBI_FLOATS premise

Not a task in this plan (the spec is a separate, already-committed
document, and correcting it doesn't require any of the code changes
above). Before or shortly after this plan lands,
`docs/superpowers/specs/2026-07-20-fast3d-matrix-stack-design.md` should
have its "Module: `saturn_matrix.{h,c}`" section's claim —
"N64 source matrices are natively s15.16, stored as split
integer/fraction 16-bit halves... Q16.16 reconstructs them with zero
conversion loss" — corrected to reflect that this build's
`F3DEX_GBI_2E`/`GBI_FLOATS` configuration means the real wire format is
plain floats, not a split s15.16 encoding (see this plan's "Revision
note" at the top for the full citation chain). The Q16.16 **target**
format decision itself remains correct and does not need to change —
only the spec's description of the **source** format does.

## Self-review notes (written during plan authoring and after the code-review pass)

- **Spec coverage**: every in-scope item from the design spec's "Scope"
  section has a task — matrix decode (1), multiply (2), stack (3), lazy
  MP (4), frontend state (5), host-test build fix (5.5, added by review),
  G_MTX/G_POPMTX (6), G_MOVEMEM/G_GEOMETRYMODE (7), G_VTX (8),
  G_TRI1/G_TRI2 + cull + reject + depth-bucket (9), VDP1 backend storage
  (10), emission (11), linker (12), verify-all gate (13), wiring (14),
  mutation testing (15), regression (16).
- **Ambiguity resolved, not hidden**: the pre-review draft flagged several
  bit-arithmetic spots as "re-derive by hand, don't trust verbatim" —
  those were exactly where the adversarial code review found real bugs
  (G_TRI1 index doubling, the G_MTX push-XOR test, and the identity-matrix
  decode fixture). All have been corrected with verified values rather
  than left as caveats for the next reader to re-discover.
- **Type consistency check**: `sm64_saturn_mtx_t`, `sm64_saturn_matrix_stack_t`,
  `sm64_saturn_fast3d_vertex_t` (now `float` position fields, not
  `int16_t` — corrected for GBI_FLOATS), `sm64_saturn_resolved_triangle_t`,
  and `sm64_saturn_fast3d_viewport_t` are each defined exactly once (Tasks
  1, 3, 5) and referenced identically by name in every later task — no
  renaming drift.
- **Counter semantics check**: `triangles_emitted` (frontend-only, Task 9)
  and `triangles_vdp1_emitted` (Yaul-dependent, Task 11) are now distinct
  fields, as are `reject_command_capacity` (resolved-triangle buffer) and
  `reject_vdp1_arena_capacity` (VDP1 command arena) — a profiling
  consumer can now tell which of two structurally different ceilings was
  hit.
