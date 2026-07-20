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
 * encoding (16 consecutive row-major floats) into Q16.16.
 *
 * Assumption, not enforced by this function: every entry reachable
 * through SM64's object/camera graph stays within Q16.16's integer
 * ceiling of +-32768. `(int32_t)(f * 65536.0f)` is undefined behavior
 * per C11 6.3.1.4p1 once the scaled value exceeds INT32_MAX/INT32_MIN
 * (e.g. f == 40000.0f scales to 2621440000.0, past INT32_MAX), so a
 * matrix entry outside that range is a caller-side data-assumption
 * violation, not a case this function detects or guards against. This
 * mirrors the assumption-over-enforcement treatment Task 2's
 * sm64_saturn_matrix_mul documents for the same fixed-point ceiling. */
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
 * entry.
 *
 * A single term's magnitude (max 2^31*2^31=2^62) always fits int64_t on
 * its own, but summing four such terms at the format's true extreme can
 * overflow int64_t *during accumulation*, before any narrowing happens
 * (2^62+2^62 already equals 2^63, one past INT64_MAX). Letting `sum`
 * itself cross into signed overflow is undefined behavior, and the
 * resulting wraparound can coincidentally land back in a value whose
 * final narrowed form looks in-range -- checking only the final
 * narrowed value cannot catch that case. Each term is therefore
 * checked against `sum` immediately before it is added, so `sum` itself
 * can never cross into UB. The separate final-narrowed-value check
 * still matters on top of that: a sum can stay safely within int64_t
 * range while still narrowing to something outside Q16.16's int32_t
 * range. The two checks catch different failure modes, not redundant
 * ones. */
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
                const int64_t term = (int64_t)a->m[i][k] * (int64_t)b->m[k][j];
                /* Check before adding so `sum` itself can never cross
                 * into signed-overflow UB -- a single term's magnitude
                 * (max 2^31*2^31=2^62) always fits int64_t on its own,
                 * but four terms summed at the format's extreme can
                 * overflow int64_t during accumulation, before any
                 * narrowing ever happens. Checking only the final
                 * narrowed value (the previous approach) cannot catch
                 * this: the wraparound can coincidentally land back in
                 * a value that looks in-range. */
                if (term >= 0) {
                    if (sum > INT64_MAX - term) {
                        overflowed = true;
                        sum = INT64_MAX;
                    } else {
                        sum += term;
                    }
                } else {
                    if (sum < INT64_MIN - term) {
                        overflowed = true;
                        sum = INT64_MIN;
                    } else {
                        sum += term;
                    }
                }
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

#define SM64_SATURN_MATRIX_STACK_DEPTH 11U

typedef struct sm64_saturn_matrix_stack {
    sm64_saturn_mtx_t entries[SM64_SATURN_MATRIX_STACK_DEPTH];
    sm64_saturn_mtx_t projection;
    sm64_saturn_mtx_t mp;
    uint8_t depth;
    bool overflowed;
    bool mp_dirty;
    bool mp_overflowed;
} sm64_saturn_matrix_stack_t;

static inline void
sm64_saturn_matrix_stack_init(sm64_saturn_matrix_stack_t *stack)
{
    sm64_saturn_matrix_identity(&stack->entries[0]);
    stack->depth = 1;
    stack->overflowed = false;
    sm64_saturn_matrix_identity(&stack->projection);
    stack->mp_dirty = true;
    stack->mp_overflowed = false;
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
    stack->mp_dirty = true;
    return true;
}

/* Matches gfx_pc.c's gfx_sp_pop_matrix: pops `count` levels, silently
 * stopping rather than trapping when the requested count would pop past
 * the bottom of the stack (the reference guards with
 * `if (modelview_matrix_stack_size > 0)` inside its while loop). This
 * port floors at depth 1, not depth 0: unlike the reference -- which
 * re-checks `modelview_matrix_stack_size > 0` before every subsequent
 * array access after a pop -- this port's stack_top()/stack_load()
 * unconditionally index entries[depth - 1]. On real SH-2 hardware
 * there's no MMU: letting depth reach 0 would make that index an
 * out-of-bounds access (entries[depth - 1] with depth==0 promotes to int
 * arithmetic and evaluates entries[-1], not entries[255] -- uint8_t's
 * whole range fits in int, so `depth - 1` is signed int arithmetic, not
 * an unsigned wraparound), landing before the array in memory rather
 * than past its end, and either way not a fault the hardware catches.
 * Flooring at 1 keeps the invariant "entries[depth-1]
 * is always the valid base identity matrix" intact, matching the
 * reference's real-world behavior anyway since gfx_sp_reset() never lets
 * the stack size drop below the base entry in a balanced display list.
 * Callers must pre-divide the raw G_POPMTX data word by 64 -- see
 * saturn_fast3d_frontend.c's G_POPMTX decode (a later task).
 *
 * Marks mp_dirty whenever count > 0 was requested, even if the depth-1
 * floor clamps the loop to fewer actual pops than requested (including
 * zero, when already at the floor) -- conservative rather than tracking
 * a precise "did the top entry actually change" flag. A spurious dirty
 * mark here costs one redundant sm64_saturn_matrix_mul the next time
 * stack_mp() is called (recomposing the same top against the same
 * projection), not a correctness bug. */
static inline void
sm64_saturn_matrix_stack_pop(sm64_saturn_matrix_stack_t *stack,
                             uint32_t count)
{
    if (count > 0) {
        stack->mp_dirty = true;
    }
    while (count-- > 0 && stack->depth > 1) {
        stack->depth--;
    }
}

static inline void
sm64_saturn_matrix_stack_load(sm64_saturn_matrix_stack_t *stack,
                              const sm64_saturn_mtx_t *m)
{
    stack->entries[stack->depth - 1] = *m;
    stack->mp_dirty = true;
}

static inline sm64_saturn_mtx_t *
sm64_saturn_matrix_stack_top(sm64_saturn_matrix_stack_t *stack)
{
    return &stack->entries[stack->depth - 1];
}

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

#endif
