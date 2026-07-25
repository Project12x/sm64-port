#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "saturn_frame_profile.h"
#include "saturn_gouraud.h"
#include "saturn_gouraud_bank.h"
#include "saturn_command_arena.h"
#include "saturn_memory_arena.h"
#include "saturn_projected_workarea.h"
#include "saturn_render_queue.h"
#include "saturn_transform.h"
#include "saturn_matrix.h"
#include "saturn_matrix_kernels.h"
#include "saturn_matrix_ctors.h"
#include "saturn_light_q16.h"
#include "types.h"
#include "saturn_fast3d_frontend.h"
#include "PR/gbi.h"

static void test_identity_camera(void)
{
    const sm64_saturn_camera_transform_t camera = {
        .position = {10, 20, 30},
        .right = {1 << 16, 0, 0},
        .up = {0, 1 << 16, 0},
        .forward = {0, 0, 1 << 16}
    };
    const sm64_saturn_vec3i_t view = sm64_saturn_world_to_view(
        &camera, (sm64_saturn_vec3i_t){14, 26, 38});

    assert(view.x == 4);
    assert(view.y == 6);
    assert(view.z == 8);
}

static void test_q16_normalization(void)
{
    const sm64_saturn_vec3i_t normalized =
        sm64_saturn_vec3_normalize_q16((sm64_saturn_vec3i_t){3, 4, 0});

    assert(normalized.x == 39321);
    assert(normalized.y == 52428);
    assert(normalized.z == 0);
}

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
    assert(out.m[3][1] == (int32_t)(-8.5f * 65536.0f)); /* -557056 */
    assert(out.m[3][2] == (int32_t)(0.25f * 65536.0f));  /* 16384 */
    assert(out.m[3][3] == (1 << 16));
}

/* Exercises sm64_saturn_matrix_decode_q16 directly -- the native-Q16.16
 * wire decode Task 5 adds alongside the float decode above. Compiled
 * unconditionally (not gated on SATURN_MTX_IS_Q16): this host suite
 * never defines that macro (it exercises the frontend's generic/float
 * decode path throughout), but the q16 decode function itself is always
 * available and testable in isolation regardless of which macro the
 * frontend build happens to key off. */
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

static void test_matrix_multiply_accumulator_overflow_guard(void)
{
    sm64_saturn_mtx_t a, b, result;
    bool overflowed;

    sm64_saturn_matrix_identity(&a);
    sm64_saturn_matrix_identity(&b);
    /* All four k-terms of entry [0][0] at the extreme -- the
     * accumulation itself (not just the final narrowed value)
     * must be caught, since 2^62+2^62 already overflows int64_t
     * on the second term, well before all four are summed. */
    for (int k = 0; k < 4; k++) {
        a.m[0][k] = INT32_MIN;
        b.m[k][0] = INT32_MIN;
    }

    overflowed = sm64_saturn_matrix_mul(&a, &b, &result);

    assert(overflowed);
}

static void test_matrix_multiply_nontrivial(void)
{
    sm64_saturn_mtx_t a, b, result;
    bool overflowed;

    sm64_saturn_matrix_identity(&a);
    a.m[0][0] = 2 << 16; /* scale X by 2.0 */
    a.m[1][1] = 3 << 16; /* scale Y by 3.0 */

    sm64_saturn_matrix_identity(&b);
    b.m[3][0] = 10 << 16; /* translate X by 10.0 */
    b.m[3][1] = 5 << 16;  /* translate Y by 5.0 */

    overflowed = sm64_saturn_matrix_mul(&a, &b, &result);

    assert(!overflowed);
    /* res[0][0] = a[0][0]*b[0][0] = 2.0 * 1.0 = 2.0 */
    assert(result.m[0][0] == (2 << 16));
    /* res[1][1] = a[1][1]*b[1][1] = 3.0 * 1.0 = 3.0 */
    assert(result.m[1][1] == (3 << 16));
    /* res[3][0] = a[3][0]*b[0][0] + a[3][1]*b[1][0] + a[3][2]*b[2][0] + a[3][3]*b[3][0]
     *           = 0 + 0 + 0 + 1.0*10.0 = 10.0 */
    assert(result.m[3][0] == (10 << 16));
}

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
    /* Matches gfx_pc.c's gfx_sp_pop_matrix in spirit -- popping past the
     * bottom of the stack is a silent no-op rather than a trap -- but
     * this port floors at depth 1, not depth 0: stack_top()/stack_load()
     * unconditionally index entries[depth - 1] (depth == 0 promotes to
     * int arithmetic and evaluates entries[-1], not entries[255] -- not
     * an unsigned wraparound), a wild out-of-bounds access with no MMU
     * to catch it on real SH-2 hardware. A balanced display list never
     * triggers this path anyway. */
    sm64_saturn_matrix_stack_pop(&stack, 5);
    assert(stack.depth == 1);

    /* Confirms the stack resumes normal push behavior after being
     * driven to the floor -- no lingering corruption from the
     * pop-past-floor path (Task 4's mp-dirty tracking leans on this
     * same stack staying coherent across a pop-to-floor). */
    assert(sm64_saturn_matrix_stack_push(&stack));
    assert(stack.depth == 2);
}

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

static void test_matrix_mp_cache_invalidates_across_pop(void)
{
    sm64_saturn_matrix_stack_t stack;
    sm64_saturn_mtx_t projection, loaded;
    const sm64_saturn_mtx_t *mp;

    sm64_saturn_matrix_stack_init(&stack);
    sm64_saturn_matrix_identity(&projection);
    sm64_saturn_matrix_stack_set_projection(&stack, &projection);

    assert(sm64_saturn_matrix_stack_push(&stack));
    sm64_saturn_matrix_identity(&loaded);
    loaded.m[3][0] = 42 << 16;
    sm64_saturn_matrix_stack_load(&stack, &loaded);

    mp = sm64_saturn_matrix_stack_mp(&stack);
    assert(mp->m[3][0] == (42 << 16)); /* caches the loaded (non-identity) top */

    sm64_saturn_matrix_stack_pop(&stack, 1); /* restores the untouched identity below */
    mp = sm64_saturn_matrix_stack_mp(&stack);
    /* If pop() failed to mark mp_dirty, this would incorrectly still
     * return the stale 42<<16 value cached before the pop. */
    assert(mp->m[3][0] == 0);
}

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
    /* Gwords.w0/w1 are uintptr_t in this port's PR/gbi.h (not the classic
     * N64 u32), specifically so a real host/target pointer round-trips
     * through w1 without truncation -- narrowing through (uint32_t) here
     * would corrupt the address on a 64-bit host. */
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
    /* Push three times first (depth 1 -> 4) so there is headroom between
     * "popped exactly one level" (depth 3) and the stack's depth-1 floor.
     * A single push (depth 1 -> 2) is NOT enough here: sm64_saturn_
     * matrix_stack_pop() silently floors at depth 1 rather than trapping
     * an over-large count (see saturn_matrix.h), so from depth 2 a
     * correct pop(1) and a buggy, unscaled pop(64) both land on the same
     * floor of depth 1 -- indistinguishable, and this test would pass
     * even with the /64 divide missing entirely. Starting from depth 4
     * makes the two outcomes different: pop(1) -> depth 3, unscaled
     * pop(64) -> floors at depth 1. */
    (void)sm64_saturn_matrix_stack_push(&frontend.matrix_stack);
    (void)sm64_saturn_matrix_stack_push(&frontend.matrix_stack);
    (void)sm64_saturn_matrix_stack_push(&frontend.matrix_stack);
    assert(frontend.matrix_stack.depth == 4);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    /* If the /64 scaling is missing, this would attempt to pop 64
     * levels instead of 1 and desync the stack all the way down to its
     * depth-1 floor instead of depth 3 -- asserting depth==3 (exactly
     * one level popped from 4) catches that directly. */
    assert(frontend.matrix_stack.depth == 3);
}

static void test_frontend_rdp_vs_sp_opcode_classification(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    Gfx list[3];
    struct SPTask task;

    /* G_SETCIMG is a genuine, always-defined-regardless-of-dialect RDP
     * command (include/PR/gbi.h ~L179-205) -- must land in rdp_commands.
     * (G_SETTIMG was the originally-proposed opcode here, but verifying
     * against the actual switch in sm64_saturn_fast3d_count_command
     * shows it is deliberately bucketed under texture_commands instead,
     * alongside G_SETTILE/G_LOADBLOCK/etc -- so it would not exercise
     * the rdp_commands path this test is meant to lock in. G_SETCIMG has
     * no such special-cased bucket and falls straight into the generic
     * RDP case list.) */
    list[0].words.w0 = (uint32_t)G_SETCIMG << 24;
    list[0].words.w1 = 0;
    /* G_GEOMETRYMODE is SP-side but, under F3DEX_GBI_2E's opcode
     * numbering, lands in the same high-byte numeric range as genuine
     * RDP opcodes -- this is exactly the case a naive threshold-based
     * classification would get wrong (see Task 5.5's fix). It has no
     * dedicated counter field in sm64_saturn_fast3d_profile_t, so it
     * must fall through to other_commands, not rdp_commands. */
    list[1].words.w0 = (uint32_t)G_GEOMETRYMODE << 24;
    list[1].words.w1 = 0;
    list[2] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.rdp_commands == 1);
    /* Expected count is 2, not 1: G_GEOMETRYMODE lands here as intended,
     * but the list's own G_ENDDL terminator (required so the walk knows
     * where to stop) ALSO has no dedicated case in
     * sm64_saturn_fast3d_count_command today and therefore falls to
     * this same default branch. Verified directly against the switch --
     * count_command runs unconditionally for every command including
     * G_ENDDL, before sm64_saturn_fast3d_frontend_submit's separate
     * opcode == G_ENDDL check ends the walk. */
    assert(frontend.profile.other_commands == 2);
}

/* The four tests below close a coverage gap left by
 * test_frontend_g_mtx_load_modelview: that test deliberately decodes to
 * LOAD-only (no push), so it never exercises the G_MTX_PUSH branch, the
 * G_MTX_PROJECTION branch (either sub-case), the modelview-multiply
 * (no-load) branch, or max_modelview_depth_reached's high-water-mark
 * behavior. The push-XOR bit is called out in the design spec as the
 * single most likely place to introduce a silent bug, and three more
 * tasks are about to add more opcodes to this same switch, so this
 * coverage is added now while it is cheap. */

static void test_frontend_g_mtx_push_before_load(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const float translate_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        7.0f, 0.0f, 0.0f, 1.0f
    };
    Gfx list[2];
    struct SPTask task;

    /* Decoded semantics wanted: PUSH | LOAD | MODELVIEW. Per make_g_mtx's
     * contract, pre-XOR with G_MTX_PUSH so the frontend's own decode-time
     * XOR (raw ^ G_MTX_PUSH) cancels back to that value -- note this
     * simplifies to a raw wire byte of plain G_MTX_LOAD, since XORing a
     * value that already has the PUSH bit set, by PUSH again, clears it. */
    list[0] = make_g_mtx(
        (uint8_t)((G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW) ^ G_MTX_PUSH),
        translate_floats);
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    assert(frontend.matrix_stack.depth == 1);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    /* Push must happen before load (matches the reference's ordering and
     * sm64_saturn_matrix_stack_push's own copy-then-load semantics):
     * entries[0] -- the level pushed FROM -- must remain untouched
     * identity, and the loaded matrix must land in entries[1] -- the new
     * top -- not entries[0]. If push were skipped (XOR bug swallowing
     * the PUSH bit), depth would stay 1 and entries[0] would hold the
     * translation instead. */
    assert(frontend.matrix_stack.depth == 2);
    assert(frontend.matrix_stack.entries[0].m[0][0] == (1 << 16));
    assert(frontend.matrix_stack.entries[0].m[3][0] == 0);
    assert(frontend.matrix_stack.entries[1].m[3][0] == (7 << 16));
}

static void test_frontend_g_mtx_projection_load_and_multiply(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* Translation by (10.0, 5.0), loaded first. */
    static const float translate_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        10.0f, 5.0f, 0.0f, 1.0f
    };
    /* Scale by (2.0, 3.0), multiplied in second (no LOAD bit). */
    static const float scale_floats[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    Gfx list[3];
    struct SPTask task;

    /* First: G_MTX_PROJECTION | G_MTX_LOAD sets the projection outright
     * (exercises the G_MTX_PROJECTION + load sub-branch). */
    list[0] = make_g_mtx(
        (uint8_t)((G_MTX_PROJECTION | G_MTX_LOAD) ^ G_MTX_PUSH),
        translate_floats);
    /* Second: G_MTX_PROJECTION only, no LOAD, multiplies the new matrix
     * into the existing projection (exercises the G_MTX_PROJECTION +
     * multiply sub-branch): composed = mul(decoded=scale,
     * projection=translate), the same mul(new, old) order the modelview
     * multiply branch uses (gfx_pc.c's gfx_sp_matrix). */
    list[1] = make_g_mtx((uint8_t)(G_MTX_PROJECTION ^ G_MTX_PUSH), scale_floats);
    list[2] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    /* Neither projection command should touch the modelview stack. */
    assert(frontend.matrix_stack.depth == 1);
    assert(frontend.matrix_stack.entries[0].m[0][0] == (1 << 16));

    /* Hand-derived: mul(scale, translate)[i][j] = sum_k scale[i][k] *
     * translate[k][j]. scale is diagonal, so for i<3 the only nonzero
     * scale[i][k] is k==i, giving row i = scale[i][i] * translate[i][*].
     * Row 3 (the translation row) passes through unchanged because
     * scale[3][3]==1 and scale[3][k]==0 for k!=3, so row 3 of the
     * product equals translate's row 3 verbatim: [10, 5, 0, 1]. */
    assert(frontend.matrix_stack.projection.m[0][0] == (2 << 16));
    assert(frontend.matrix_stack.projection.m[1][1] == (3 << 16));
    assert(frontend.matrix_stack.projection.m[2][2] == (1 << 16));
    assert(frontend.matrix_stack.projection.m[3][0] == (10 << 16));
    assert(frontend.matrix_stack.projection.m[3][1] == (5 << 16));
    assert(frontend.matrix_stack.projection.m[3][3] == (1 << 16));
}

static void test_frontend_g_mtx_modelview_multiply(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* Translation by (10.0, 5.0), loaded first. */
    static const float translate_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        10.0f, 5.0f, 0.0f, 1.0f
    };
    /* Scale by (2.0, 3.0), multiplied in second (no LOAD, no PUSH). */
    static const float scale_floats[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    Gfx list[3];
    struct SPTask task;

    /* First command: LOAD the translation matrix into modelview (no
     * push). */
    list[0] = make_g_mtx((uint8_t)((G_MTX_LOAD | G_MTX_MODELVIEW) ^ G_MTX_PUSH),
                          translate_floats);
    /* Second command: MODELVIEW only, no LOAD and no PUSH -- exercises
     * the "multiply into current top" branch. Decode computes
     * composed = mul(decoded=scale, top=translate), i.e. new_top =
     * scale * translate. */
    list[1] = make_g_mtx((uint8_t)(G_MTX_MODELVIEW ^ G_MTX_PUSH), scale_floats);
    list[2] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.matrix_stack.depth == 1);
    /* Hand-derived exactly as in the projection-multiply test above:
     * mul(scale, translate) leaves scaled diagonal entries for i<3 and
     * passes the translation row (row 3: [10, 5, 0, 1]) through
     * unchanged. Not just "some values changed" -- these are the exact
     * expected entries of the product, verified by direct computation
     * from sm64_saturn_matrix_mul's row-major definition (out[i][j] =
     * sum_k a[i][k]*b[k][j]), the same way Task 2's multiply tests were
     * hand-derived. */
    assert(frontend.matrix_stack.entries[0].m[0][0] == (2 << 16));
    assert(frontend.matrix_stack.entries[0].m[1][1] == (3 << 16));
    assert(frontend.matrix_stack.entries[0].m[2][2] == (1 << 16));
    assert(frontend.matrix_stack.entries[0].m[3][0] == (10 << 16));
    assert(frontend.matrix_stack.entries[0].m[3][1] == (5 << 16));
    assert(frontend.matrix_stack.entries[0].m[3][3] == (1 << 16));
}

static void test_frontend_g_mtx_high_water_mark(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const float identity_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    Gfx list[4];
    struct SPTask task;

    /* Two PUSH|LOAD commands (depth 1 -> 2 -> 3), then a POPMTX(1)
     * (depth 3 -> 2). */
    list[0] = make_g_mtx(
        (uint8_t)((G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW) ^ G_MTX_PUSH),
        identity_floats);
    list[1] = make_g_mtx(
        (uint8_t)((G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW) ^ G_MTX_PUSH),
        identity_floats);
    list[2].words.w0 = (uint32_t)G_POPMTX << 24;
    list[2].words.w1 = 1U * 64U;
    list[3] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    assert(frontend.matrix_stack.depth == 1);
    assert(frontend.profile.max_modelview_depth_reached == 0);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    /* Final depth is 2 (1 -> push -> 2 -> push -> 3 -> pop(1) -> 2), but
     * the peak reached mid-walk was 3. max_modelview_depth_reached is
     * only updated inside the G_MTX case (not G_POPMTX), so it must
     * still read 3 here -- a high-water mark, not the current value. */
    assert(frontend.matrix_stack.depth == 2);
    assert(frontend.profile.max_modelview_depth_reached == 3);
}

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
    /* Hand-derived from the decode's own formula (source_y =
     * SM64_SATURN_SOURCE_SCREEN_HEIGHT - (vtrans[1]/4 + height/2), y =
     * source_y - letterbox_crop), NOT the plan draft's original claim of
     * 0: with vtrans[1]=448, height=224 -> source_y = 240-(112+112) =
     * 16; letterbox_crop = (240-224)/2 = 8; y = 16-8 = 8. Verified by
     * running the decode and observing this exact value -- the decode
     * always anchors on the real N64 240-line space (it has no way to
     * know this synthetic input was pre-expressed in 224-line terms),
     * so an "already 224-native" input still gets the same fixed
     * 240-anchor + 8-line crop applied as any other input, landing on
     * y=8 rather than y=0. */
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

/* test_frontend_g_geometrymode above only ever ANDs against a
 * geometry_mode of 0, so 0 & anything == 0 regardless of the
 * clear-mask's polarity -- it cannot catch a sign-inverted clear-mask.
 * This test chains a second G_GEOMETRYMODE command against a nonzero
 * prior state (the pattern every real display list uses after its
 * first frame's setup) to actually exercise clear-then-set semantics:
 * G_SHADE must survive untouched, G_ZBUFFER must be cleared, and
 * G_CULL_BACK must be set. Hand-traced: cmd0 sets geometry_mode =
 * G_SHADE|G_ZBUFFER (0x5) from a starting value of 0. cmd1's w0 low-24
 * bits are ~G_ZBUFFER & 0xFFFFFF = 0xFFFFFE, the keep-mask that,
 * ANDed against 0x5, clears bit 0 (G_ZBUFFER) and keeps bit 2
 * (G_SHADE) -> 0x4; ORing in G_CULL_BACK's bit yields the final state
 * asserted below. */
static void test_frontend_g_geometrymode_clear_and_set(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    Gfx list[3];
    struct SPTask task;

    /* First command: clear-mask = 0 (touches nothing), set-mask =
     * G_SHADE|G_ZBUFFER. Starting from geometry_mode == 0, this simply
     * establishes that starting state. */
    list[0].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0x000000U;
    list[0].words.w1 = G_SHADE | G_ZBUFFER;
    /* Second command: keep-mask (w0 bits 0-23) = ~G_ZBUFFER & 0xFFFFFF
     * -- every bit except G_ZBUFFER's survives the AND, so G_SHADE
     * (not in the set-mask either) passes through unchanged while
     * G_ZBUFFER is dropped. Set-mask (w1) = G_CULL_BACK. This is
     * exactly the scenario a polarity-inverted clear-mask gets
     * backwards (it would instead clear G_SHADE and keep G_ZBUFFER). */
    list[1].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) |
                        (uint32_t)(~(uint32_t)G_ZBUFFER & 0xFFFFFFU);
    list[1].words.w1 = G_CULL_BACK;
    list[2] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert((frontend.geometry_mode & G_SHADE) != 0);     /* survived */
    assert((frontend.geometry_mode & G_ZBUFFER) == 0);   /* cleared */
    assert((frontend.geometry_mode & G_CULL_BACK) != 0); /* set */
}

static void test_frontend_g_vtx_transform(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t one_vertex = {
        /* Non-zero, pairwise-distinct position and a distinct alpha --
         * deliberately NOT all-zero. sm64_saturn_fast3d_frontend_init
         * memsets the frontend to 0, so an all-zero fixture can't
         * distinguish a correctly decoded position from a dropped
         * assignment or a transposed x/y/z axis -- both would silently
         * read back as (0,0,0) either way. Distinct values per axis
         * close that gap. */
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

static void test_frontend_g_vtx_rejects_out_of_range_dest(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t ten_vertices[10] = {
        { .ob = {1.0f, 1.0f, 1.0f}, .cn = {255, 255, 255, 255} },
        /* remaining 9 entries can be zero-initialized by the array's
         * default; only the destination-range rejection is being
         * tested here, not per-vertex content */
    };
    Gfx list[2];
    struct SPTask task;

    /* dest_index=60, n_vertices=10: writes to slots 60-63 should
     * succeed (4 slots), slots 64-69 should be rejected (6 rejects) --
     * SM64_SATURN_FAST3D_MAX_VERTICES is 64, so valid indices are 0-63.
     * Encode n=10 at C0(12,8) and end_index=70 at C0(1,7) (so
     * dest_index = 70-10 = 60). Verified: C0(12,8) reads w0 bits 12-19,
     * (10U<<12) places 10 (0b00001010, 8 bits) exactly there, so
     * C0(12,8)==10. C0(1,7) reads w0 bits 1-7, (70U<<1) places 70
     * (0b1000110, 7 bits -- fits exactly in the 7-bit field) exactly
     * there, so C0(1,7)==70. The two fields (bits 1-7 and bits 12-19)
     * don't overlap, so neither term corrupts the other. dest_index =
     * 70-10 = 60, matching the claim above. */
    list[0].words.w0 = ((uint32_t)G_VTX << 24) | (10U << 12) | (70U << 1);
    list[0].words.w1 = (uintptr_t)ten_vertices;
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.reject_vertex_range == 6);
    assert(frontend.vertices[60].x == 1.0f); /* first in-range write succeeded */
}

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

    list[0] = make_g_mtx((uint8_t)((G_MTX_LOAD | G_MTX_MODELVIEW) ^ G_MTX_PUSH),
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
    /* viewport is {x=0,y=8,width=320,height=224} (see
     * test_frontend_g_movemem_viewport's own derivation for this exact
     * vp fixture -- the source_y=16/letterbox_crop=8 arithmetic there
     * applies identically here since both tests share the same vp
     * values). screen_x only reads viewport.x/width, so the y origin
     * doesn't affect this assertion.
     *
     * screen_x = viewport.x + (cx*0.5+0.5)*viewport.width, where cx is
     * the PERSPECTIVE-DIVIDED clip x (x/w) per this function's own
     * transform code -- not the raw pre-divide x. Hand-derived: with the
     * +50.0 X translation applied, pre-divide x = -100+50 = -50 for
     * every vertex here (all three share model x=-100); w = z = 500
     * (same z-dependent projection as the other G_TRI1 tests). So
     * cx = -50/500 = -0.1, and screen_x = 0 + (-0.1*0.5+0.5)*320 =
     * 0.45*320 = 144 for all three corners (subject to float rounding,
     * verified by running this test rather than assumed). This is the
     * assertion Task 15's Mutation 3 (negate a translation term) must
     * break -- unlike the other two G_TRI1 tests above, whose display
     * lists never issue a G_MTX and so leave mp->m[3][0] at exactly 0
     * (identity*identity), making that same negation an unobservable
     * no-op there. */
    assert(frontend.resolved[0].x[0] == 144);
    assert(frontend.resolved[0].x[1] == 144);
    assert(frontend.resolved[0].x[2] == 144);
}

/* Regression test for a reviewer-caught bug: before the fix, the only
 * pre-divide reject was `w <= 0.0f`. A small *positive* w (camera very
 * close to geometry -- an ordinary occurrence, not pathological) makes
 * cx/cy enormous, and the (int16_t) narrowing cast on the resulting
 * screen_x/screen_y was undefined behavior with no bound -- raising the
 * near-plane threshold alone can't fix this either, since it's an x/w
 * *ratio* problem, not a w-magnitude problem (a large-but-plausible
 * model-space x with a "safe" w still overflows int16_t).
 *
 * All three vertices here share model x=100, z=0.1 (all three s x=100
 * exactly reproduces the reviewer's own repro case: cx = 100/0.1 = 1000,
 * screen_x_f = (1000*0.5+0.5)*320 = 160160 -- far outside int16_t's
 * [-32768, 32767] range, which was UB before this fix). y varies per
 * vertex (-50, 50, 0) specifically to exercise BOTH clamp directions in
 * one triangle, hand-traced below against the standard vp fixture
 * (viewport {x=0,y=8,width=320,height=224}, same reconstruction as
 * test_frontend_g_movemem_viewport) and this test's projection (w = z
 * raw, same as every other G_TRI1 test in this file):
 *
 *   v0 y=-50: cy = -50/0.1 = -500;
 *             screen_y_f = 8 + (1.0-(-500*0.5+0.5))*224
 *                        = 8 + 250.5*224 = 56120 -- > INT16_MAX, clamps high.
 *   v1 y=50:  cy = 50/0.1 = 500;
 *             screen_y_f = 8 + (1.0-(500*0.5+0.5))*224
 *                        = 8 + (-249.5)*224 = -55880 -- < INT16_MIN, clamps low.
 *   v2 y=0:   cy = 0;
 *             screen_y_f = 8 + (1.0-0.5)*224 = 120 -- in range, no clamp.
 *
 * So this one triangle exercises: x clamped high (all 3 corners, since
 * model x is shared), y clamped high (v0), y clamped low (v1), and y
 * left unclamped (v2) -- both saturation directions plus the pass-
 * through case, in a single test.
 *
 * Regardless of clamping, this triangle is still correctly rejected
 * afterward -- verified two independent ways by hand: (1) cw = w = 0.1
 * truncates to (int32_t)0 when pushed into the workarea (raw-units
 * convention, see the NEAR/FAR_DEPTH comment in
 * saturn_fast3d_frontend.c), and 0 < NEAR_DEPTH(64) fails
 * sm64_saturn_projected_quad_is_visible's z-bound check on its own; (2)
 * all 3 corners clamp to screen_x=32767 (x > clip_viewport.right=320),
 * so clip_and includes CLIP_RIGHT for every corner regardless of y,
 * failing the `clip_and != SM64_SATURN_CLIP_NONE` check too. Either
 * failure alone would reject this triangle -- both hold here, so the
 * rejected-not-resolved outcome does not depend on getting the more
 * fragile of the two checks exactly right. This proves the fix's point:
 * the previously-UB narrowing cast now runs to a defined, saturated
 * value, and the pipeline still produces the same sane, rejected
 * verdict this obviously-offscreen geometry deserves. */
static void test_frontend_g_tri1_small_w_clamps_screen_coords(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t verts[3] = {
        { .ob = {100.0f, -50.0f, 0.1f}, .cn = {255, 0, 0, 255} },
        { .ob = {100.0f,  50.0f, 0.1f}, .cn = {0, 255, 0, 255} },
        { .ob = {100.0f,   0.0f, 0.1f}, .cn = {0, 0, 255, 255} },
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    sm64_saturn_mtx_t projection;
    Gfx list[4];
    struct SPTask task;
    const uint32_t vtx_w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = vtx_w0;
    list[1].words.w1 = (uintptr_t)verts;
    list[2].words.w0 = ((uint32_t)G_TRI1 << 24) |
                        (0U << 16) | (2U << 8) | (4U << 0);
    list[2].words.w1 = 0;
    list[3] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16; /* w = z (raw units) */
    projection.m[3][3] = 0;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.triangles_transformed == 1);
    /* Rejected, not resolved -- verified by hand above via two
     * independent, redundant checks (z-bound and clip_and), so this
     * outcome doesn't hinge on a single fragile comparison. The point of
     * this test is that reaching this assertion at all (rather than
     * crashing or reading a garbage value from an out-of-range
     * (int16_t) cast) proves the clamp fix is doing its job. */
    assert(frontend.profile.reject_near_far == 1);
    assert(frontend.profile.reject_degenerate == 0);
    assert(frontend.resolved_count == 0);
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
     * fields read twice. corner_rgb1555[0] is corner 0's color (the
     * same vertex-0 color the old flat color_rgb1555 field carried),
     * kept here since this test's whole point is distinguishing the
     * two triangles' vertex sources, not exercising per-corner packing
     * itself (see test_frontend_resolved_triangle_carries_corner_colors
     * for that). */
    assert(frontend.resolved[0].corner_rgb1555[0] !=
           frontend.resolved[1].corner_rgb1555[0]);
    assert(frontend.resolved[0].x[0] != frontend.resolved[1].x[0] ||
           frontend.resolved[0].y[0] != frontend.resolved[1].y[0]);
}

static void test_frontend_g_tri2_distinct_depth_buckets(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* Triangle A at z=1000 (near-ish), Triangle B at z=6000 (far) -- same
     * shape and screen footprint as test_frontend_g_tri2_two_triangles,
     * only z differs, isolating depth_bucket as the one thing this test
     * checks (no existing test asserts on depth_bucket at all, despite it
     * being the field the whole painter's-algorithm VDP1 emission adapter
     * (saturn_fast3d_vdp1_emit.c) depends on for draw order). */
    static const Vtx_t verts[6] = {
        { .ob = {-100.0f, -100.0f, 1000.0f}, .cn = {255, 0, 0, 255} },
        { .ob = { 100.0f, -100.0f, 1000.0f}, .cn = {0, 255, 0, 255} },
        { .ob = {   0.0f,  100.0f, 1000.0f}, .cn = {0, 0, 255, 255} },
        { .ob = {-100.0f, -100.0f, 6000.0f}, .cn = {255, 255, 0, 255} },
        { .ob = { 100.0f, -100.0f, 6000.0f}, .cn = {0, 255, 255, 255} },
        { .ob = {   0.0f,  100.0f, 6000.0f}, .cn = {255, 0, 255, 255} },
    };
    static const Vp_t vp = {
        .vscale = {320 * 2, 224 * 2, 0, 0},
        .vtrans = {320 * 2, 224 * 2, 0, 0}
    };
    sm64_saturn_mtx_t projection;
    Gfx list[4];
    struct SPTask task;
    uint32_t vtx_w0;

    vtx_w0 = ((uint32_t)G_VTX << 24) | (6U << 12) | (6U << 1);

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = vtx_w0;
    list[1].words.w1 = (uintptr_t)verts;
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

    assert(frontend.resolved_count == 2);
    /* Exact expected buckets, from the same formula as
     * saturn_fast3d_frontend.c's depth_bucket computation:
     * (z - NEAR_DEPTH) * (DEPTH_BUCKETS - 1) / (FAR_DEPTH - NEAR_DEPTH),
     * NEAR_DEPTH=1, FAR_DEPTH=16384, DEPTH_BUCKETS=16 (constants
     * recalibrated 2026-07-21 against live Bob data -- see the comment
     * at their definition).
     * A: (1000-1)*15/16383 = 14985/16383 = 0 (integer division).
     * B: (6000-1)*15/16383 = 89985/16383 = 5 (integer division). */
    assert(frontend.resolved[0].depth_bucket == 0U);
    assert(frontend.resolved[1].depth_bucket == 5U);
    /* The property saturn_fast3d_vdp1_emit.c's far-to-near painter's-
     * algorithm walk actually depends on: farther geometry gets a
     * numerically higher bucket than nearer geometry. */
    assert(frontend.resolved[1].depth_bucket > frontend.resolved[0].depth_bucket);
}

static void test_frontend_resolved_capacity_exceeds_old_192_cap(void)
{
    /* Proves the resolved-triangle buffer capacity was actually raised
     * past the old 192-triangle ceiling by resolving 200 independent,
     * valid triangles in a single frame and confirming none were
     * rejected for capacity. The display list is built programmatically
     * (a for-loop, not a literal fixture) -- each iteration repeats the
     * exact vertex pattern already proven correct by
     * test_frontend_g_tri1_resolves_triangle (z=500, identity modelview,
     * projection.m[2][3]=1<<16 so cw==z). */
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
    struct SPTask task;
    /* TRIANGLE_COUNT is a true compile-time constant (enum, not a VLA
     * extent) that drives both the loop bound below and list[]'s size,
     * so the two can never drift out of sync the way two separate
     * literals could -- matching this file's other tests, which all use
     * plain compile-time-sized literal arrays. UPPER_SNAKE_CASE matches
     * this codebase's actual enum-constant convention (e.g.
     * HWTEST_STATUS_CART_PRESENT in src/port/saturn/hwtest/main.c,
     * SM64_SATURN_SOURCE_CART_STAGE_RESET in
     * src/port/saturn/sourceboot/source_cart.h). */
    enum { TRIANGLE_COUNT = 200 }; /* > old 192 cap, < new 1536 cap */
    Gfx list[1 + TRIANGLE_COUNT * 2 + 1];
    int cmd = 0;
    int t;

    list[cmd].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[cmd].words.w1 = (uintptr_t)&vp;
    cmd++;

    for (t = 0; t < TRIANGLE_COUNT; t++) {
        /* n=3, dest_index=0 every iteration -- the same encoding this
         * file's other G_VTX tests use; overwriting the same 3-vertex
         * decode slot each time is fine since every iteration loads the
         * identical, already-proven vertex data. */
        list[cmd].words.w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);
        list[cmd].words.w1 = (uintptr_t)verts;
        cmd++;
        list[cmd].words.w0 = ((uint32_t)G_TRI1 << 24) |
                              (0U << 16) | (2U << 8) | (4U << 0);
        list[cmd].words.w1 = 0;
        cmd++;
    }
    list[cmd] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16;
    projection.m[3][3] = 0;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.resolved_count == (uint16_t)TRIANGLE_COUNT);
    assert(frontend.profile.reject_command_capacity == 0);
    assert(frontend.profile.triangles_transformed == (uint32_t)TRIANGLE_COUNT);
}

static void test_frontend_submit_resets_resolved_count_each_frame(void)
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

    vtx_w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = vtx_w0;
    list[1].words.w1 = (uintptr_t)verts;
    list[2].words.w0 = ((uint32_t)G_TRI1 << 24) |
                        (0U << 16) | (2U << 8) | (4U << 0);
    list[2].words.w1 = 0;
    list[3] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;

    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_matrix_identity(&projection);
    projection.m[2][3] = 1 << 16;
    projection.m[3][3] = 0;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);

    /* Same single-triangle display list submitted across two separate
     * "frames" (two calls to frontend_submit on the same frontend, as a
     * real per-frame render loop will do from Task 14 onward). If
     * resolved_count isn't reset between frames, it accumulates forever
     * -- second frame ends with 2 resolved triangles instead of 1, and
     * after SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES frames every
     * subsequent frame silently renders nothing at all
     * (reject_command_capacity trips permanently). */
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);
    assert(frontend.resolved_count == 1);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);
    assert(frontend.resolved_count == 1);
    assert(frontend.profile.triangles_transformed == 1);
    assert(frontend.profile.reject_command_capacity == 0);
}

static void test_frame_profile(void)
{
    sm64_saturn_frame_profile_t profile = {
        .update_ticks = 1,
        .sort_ticks = 2,
        .command_ticks = 3,
        .wait_ticks = 4,
        .vblank_ticks = 5
    };

    assert(sm64_saturn_frame_profile_render_total(&profile) == 10);
    assert(sm64_saturn_frame_profile_loop_total(&profile) == 15);
    assert(sm64_saturn_frame_profile_rate_x10(1500, profile.loop_ticks) == 100);
    assert(sm64_saturn_frame_profile_rate_x10(1500, 0) == 0);
}

static void test_bounded_memory_arena(void)
{
    uint8_t storage[32];
    sm64_saturn_memory_arena_t arena;
    sm64_saturn_memory_arena_init(&arena, storage, sizeof(storage));

    assert(sm64_saturn_memory_arena_alloc(&arena, 3, 8) == &storage[0]);
    assert(sm64_saturn_memory_arena_alloc(&arena, 8, 8) == &storage[8]);
    assert(arena.used == 16);
    assert(arena.peak == 16);
    assert(!arena.overflowed);
    assert(sm64_saturn_memory_arena_alloc(&arena, 17, 8) == NULL);
    assert(arena.overflowed);
    assert(arena.used == 16);

    sm64_saturn_memory_arena_reset(&arena);
    assert(arena.used == 0);
    assert(arena.peak == 16);
    assert(!arena.overflowed);
}

static void test_source_identified_render_queue(void)
{
    sm64_saturn_render_item_t items[2];
    uint16_t order[2];
    sm64_saturn_render_queue_t queue;
    sm64_saturn_render_queue_init(&queue, items, order, 2);

    assert(sm64_saturn_render_queue_push(&queue,
        (sm64_saturn_render_item_t){
            .depth_key = 900,
            .source_bank = 7,
            .source_primitive = 42,
            .lowered_index = 3,
            .kind = SM64_SATURN_RENDER_WORLD,
            .pass = SM64_SATURN_PASS_OPAQUE
        }));
    assert(sm64_saturn_render_queue_push(&queue,
        (sm64_saturn_render_item_t){
            .depth_key = 700,
            .source_bank = 9,
            .source_primitive = 11,
            .lowered_index = 5,
            .kind = SM64_SATURN_RENDER_ACTOR,
            .pass = SM64_SATURN_PASS_OPAQUE
        }));
    assert(queue.count == 2);
    assert(queue.order[0] == 0 && queue.order[1] == 1);
    assert(queue.items[0].source_primitive == 42);
    assert(queue.items[1].lowered_index == 5);
    assert(!sm64_saturn_render_queue_push(&queue,
        (sm64_saturn_render_item_t){0}));
    assert(queue.overflowed);

    sm64_saturn_render_queue_reset(&queue);
    assert(queue.count == 0);
    assert(!queue.overflowed);
}

static void test_projected_workarea(void)
{
    sm64_saturn_projected_vertex_t storage[4];
    sm64_saturn_projected_workarea_t workarea;
    uint16_t indices[4];
    const sm64_saturn_viewport_t viewport = {
        .left = 0,
        .top = 0,
        .right = 319,
        .bottom = 223
    };
    sm64_saturn_projected_quad_t quad;

    sm64_saturn_projected_workarea_init(&workarea, storage, 4);
    assert(workarea.count == 0);
    assert(workarea.peak == 0);
    assert(!workarea.overflowed);

    assert(sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){10, 10, 100}, &indices[0]));
    assert(sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){330, 10, 200}, &indices[1]));
    assert(sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){330, 200, 300}, &indices[2]));
    assert(sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){10, 200, 400}, &indices[3]));
    assert(workarea.count == 4);
    assert(workarea.peak == 4);
    assert(indices[0] == 0 && indices[3] == 3);

    assert(sm64_saturn_projected_quad_analyze(
        &workarea, indices, &viewport, &quad));
    assert(quad.min_x == 10 && quad.max_x == 330);
    assert(quad.min_y == 10 && quad.max_y == 200);
    assert(quad.min_z == 100 && quad.max_z == 400);
    assert(quad.center_z == 200);
    assert(quad.clip_and == SM64_SATURN_CLIP_NONE);
    assert(quad.clip_or == SM64_SATURN_CLIP_RIGHT);
    assert(sm64_saturn_projected_quad_is_visible(&quad, 64, 512, 512));
    assert(!sm64_saturn_projected_quad_is_visible(&quad, 128, 512, 512));
    assert(!sm64_saturn_projected_quad_is_visible(&quad, 64, 256, 512));
    assert(!sm64_saturn_projected_quad_is_visible(&quad, 64, 512, 128));

    for (uint8_t corner = 0; corner < 4; corner++) {
        storage[corner].x = -10;
    }
    assert(sm64_saturn_projected_quad_analyze(
        &workarea, indices, &viewport, &quad));
    assert((quad.clip_and & SM64_SATURN_CLIP_LEFT) != 0);
    assert(!sm64_saturn_projected_quad_is_visible(&quad, 64, 512, 512));

    indices[3] = 4;
    assert(!sm64_saturn_projected_quad_analyze(
        &workarea, indices, &viewport, &quad));
    assert(!sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){0, 0, 0}, NULL));
    assert(workarea.overflowed);

    sm64_saturn_projected_workarea_reset(&workarea);
    assert(workarea.count == 0);
    assert(workarea.peak == 4);
    assert(!workarea.overflowed);
}

static void test_bounded_command_arena(void)
{
    sm64_saturn_command_arena_t arena;
    uint16_t first;

    sm64_saturn_command_arena_init(&arena, 6, 2);
    assert(sm64_saturn_command_arena_begin(&arena) == 2);
    assert(sm64_saturn_command_arena_reserve(&arena, 2, &first));
    assert(first == 2);
    assert(sm64_saturn_command_arena_reserve(&arena, 1, &first));
    assert(first == 4);
    assert(!sm64_saturn_command_arena_reserve(&arena, 1, &first));
    assert(arena.overflowed);
    assert(sm64_saturn_command_arena_finish(&arena) == 5);
    assert(arena.live_count == 6);
    assert(arena.peak == 6);

    assert(sm64_saturn_command_arena_begin(&arena) == 5);
    assert(!arena.overflowed);
    assert(sm64_saturn_command_arena_reserve(&arena, 1, &first));
    assert(sm64_saturn_command_arena_finish(&arena) == 3);
    assert(arena.live_count == 4);
    assert(arena.peak == 6);
}

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
    assert(sm64_saturn_q16_mul(-(1 << 16), -(1 << 16)) == (1 << 16));
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

/* Float mirror of gfx_pc.c's lighting math (calculate_normal_dir at
 * :534-542 + the gfx_sp_vertex lighting block at :626-657), used as the
 * differential ground truth for the Q16 evaluator.
 *
 * TRANSPOSE ORIENTATION -- verified directly against the real
 * gfx_transposed_matrix_mul (gfx_pc.c:528-532), NOT the plan draft's
 * original claim of `coeffs[i] = sum_j M[j][i] * light_dir[j]`. The real
 * helper is:
 *   res[0] = a[0]*b[0][0] + a[1]*b[0][1] + a[2]*b[0][2];
 *   res[1] = a[0]*b[1][0] + a[1]*b[1][1] + a[2]*b[1][2];
 *   res[2] = a[0]*b[2][0] + a[1]*b[2][1] + a[2]*b[2][2];
 * i.e. res[i] = sum_j a[j]*b[i][j] -- dot of the light direction with ROW
 * i of the modelview matrix, so coeffs[i] = sum_j light_dir[j] * M[i][j].
 * It's called "transposed" relative to THIS SAME FILE's own vertex-
 * POSITION transform convention (gfx_sp_vertex:616-619: x_i =
 * sum_j v[j]*MP[j][i], the opposite index order) -- not relative to how
 * the modelview matrix itself is stored. sm64_saturn_mtx_t.m[row][col]
 * mirrors gfx_pc.c's float matrix[row][col] one-to-one with no
 * additional swap (saturn_matrix.h's sm64_saturn_matrix_mul matches
 * gfx_matrix_mul index-for-index, and saturn_matrix_ctors.h's
 * mtxq_lookat comment confirms "write in the float code's exact
 * layout"), so this reference and the Q16 evaluator below both use
 * mv->m[i][j] (row i, column j), not mv->m[j][i]. */
static void ref_light_eval(const int8_t light_dir[3],
                           const uint8_t light_col[3],
                           const uint8_t amb_col[3],
                           const sm64_saturn_mtx_t *mv,
                           const int8_t n[3], uint8_t out_rgb[3])
{
    float ld[3] = { light_dir[0] / 127.0f, light_dir[1] / 127.0f,
                    light_dir[2] / 127.0f };
    float c[3];
    float mag;
    float intensity;
    int ch[3];

    for (int i = 0; i < 3; i++) {
        c[i] = ld[0] * sm64_saturn_q16_to_float(mv->m[i][0])
             + ld[1] * sm64_saturn_q16_to_float(mv->m[i][1])
             + ld[2] * sm64_saturn_q16_to_float(mv->m[i][2]);
    }
    mag = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
    if (mag != 0.0f) {
        c[0] /= mag; c[1] /= mag; c[2] /= mag;
    }
    intensity = (n[0] * c[0] + n[1] * c[1] + n[2] * c[2]) / 127.0f;
    for (int i = 0; i < 3; i++) {
        ch[i] = amb_col[i];
        if (intensity > 0.0f) {
            ch[i] += (int)(intensity * light_col[i]);
        }
        out_rgb[i] = ch[i] > 255 ? 255 : (uint8_t)ch[i];
    }
}

static void test_light_q16_matches_float_reference(void)
{
    /* Real BOB Lights1 values -- copied from the actual
     * levels/bob/areas/1/1/model.inc.c:2..5 fixture:
     *   gdSPDefLights1(0x66, 0x66, 0x66,
     *                  0xff, 0xff, 0xff, 0x28, 0x28, 0x28)
     * gdSPDefLights1(ar,ag,ab, r1,g1,b1, x1,y1,z1) (gbi.h:1485) maps to
     * (ambient color, light color, light direction) -- NOT the plan
     * draft's placeholder {0x33,0x33,0x33} ambient / {0xcc,0xcc,0xcc}
     * light color (the direction happened to already match). */
    static const uint8_t amb[3] = { 0x66, 0x66, 0x66 };
    static const uint8_t col[3] = { 0xff, 0xff, 0xff };
    static const int8_t dir[3] = { 0x28, 0x28, 0x28 };

    sm64_saturn_mtx_t mv[3];
    sm64_saturn_light_state_t st;
    int mi;

    sm64_saturn_matrix_identity(&mv[0]);
    sm64_saturn_mtxq_rotate_zxy_and_translate(&mv[1],
        (const int32_t[3]){ 0, 0, 0 }, 0x1234, -0x0800, 0x4000);
    /* non-uniform scale exercises the normalize step */
    sm64_saturn_mtxq_scale_vec3f(&mv[2], &mv[1],
        (const int32_t[3]){ 3 << 15, 1 << 16, 5 << 14 });

    for (mi = 0; mi < 3; mi++) {
        sm64_saturn_light_state_init(&st);
        memcpy(st.dir_col, col, 3);
        memcpy(st.amb_col, amb, 3);
        memcpy(st.dir_dir, dir, 3);
        st.num_lights = 2;
        st.lights_changed = true;
        sm64_saturn_light_recompute_coeffs(&st, &mv[mi]);
        assert(!st.lights_changed);

        for (int nx = -128; nx <= 127; nx += 24) {
            for (int ny = -128; ny <= 127; ny += 24) {
                for (int nz = -128; nz <= 127; nz += 24) {
                    const int8_t n[3] = { (int8_t)nx, (int8_t)ny,
                                          (int8_t)nz };
                    uint8_t want[3], got[3];
                    ref_light_eval(dir, col, amb, &mv[mi], n, want);
                    sm64_saturn_light_eval_vertex(&st, n, got);
                    for (int i = 0; i < 3; i++) {
                        int d = (int)got[i] - (int)want[i];
                        if (d < 0) { d = -d; }
                        assert(d <= 2); /* integer sqrt + Q16 rounding */
                    }
                }
            }
        }
    }
}

static void test_light_q16_zero_normal_gets_ambient(void)
{
    sm64_saturn_light_state_t st;
    sm64_saturn_mtx_t ident;
    static const int8_t zero_n[3] = { 0, 0, 0 };
    uint8_t got[3];

    sm64_saturn_matrix_identity(&ident);
    sm64_saturn_light_state_init(&st);
    st.amb_col[0] = 10; st.amb_col[1] = 20; st.amb_col[2] = 30;
    st.dir_col[0] = 200; st.dir_col[1] = 200; st.dir_col[2] = 200;
    st.dir_dir[2] = 127;
    st.num_lights = 2;
    st.lights_changed = true;
    sm64_saturn_light_recompute_coeffs(&st, &ident);
    sm64_saturn_light_eval_vertex(&st, zero_n, got);
    assert(got[0] == 10 && got[1] == 20 && got[2] == 30);
}

static void test_light_q16_recompute_handles_extreme_matrix_entries(void)
{
    /* Regression test for a real signed-overflow UB found by code
     * review: a modelview entry at the port's documented Q16.16
     * ceiling (exactly what sm64_saturn_float_to_q16's own saturation
     * path produces for any out-of-range float) dotted with a
     * max-magnitude light direction used to make the pre-normalize
     * halving loop's abs-value computation overflow, silently
     * bypassing the loop entirely (negating INT32_MIN wrapped back to
     * INT32_MIN itself, which the buggy int32_t check read as "small
     * enough"). */
    sm64_saturn_light_state_t st;
    sm64_saturn_mtx_t extreme;
    int64_t mag2;

    sm64_saturn_matrix_identity(&extreme);
    extreme.m[0][0] = INT32_MIN;
    extreme.m[1][1] = INT32_MIN;
    extreme.m[2][2] = INT32_MIN;

    sm64_saturn_light_state_init(&st);
    st.dir_dir[0] = -128;
    st.dir_dir[1] = -128;
    st.dir_dir[2] = -128;
    st.lights_changed = true;
    sm64_saturn_light_recompute_coeffs(&st, &extreme);

    /* Must land close to unit magnitude in Q16.16 (~65536), not still
     * at the ~2^31 scale the broken loop used to leave it at -- a
     * generous bound (32768..131072) that confirms the catastrophic
     * ~32768x scale bug is gone, not a tight precision claim. */
    mag2 = (int64_t)st.coeff_q16[0] * st.coeff_q16[0]
         + (int64_t)st.coeff_q16[1] * st.coeff_q16[1]
         + (int64_t)st.coeff_q16[2] * st.coeff_q16[2];
    assert(mag2 > ((int64_t)32768 * 32768));
    assert(mag2 < ((int64_t)131072 * 131072));

    /* Sign check: with all-negative dir_dir dotted against an
     * all-negative-diagonal matrix, every per-axis dot product is
     * NEGATIVE*NEGATIVE = POSITIVE, so the correctly-computed direction
     * must come out with all three components positive. The int32_t
     * version of this function corrupted this specific input by
     * silently overflowing the premature (int32_t)(sum/127) narrow
     * (2,164,392,968 does not fit in int32_t) BEFORE the halving loop
     * ever ran, which flips the sign via implementation-defined
     * wraparound (2,164,392,968 - 2^32 = -2,130,574,328) -- so this
     * assertion catches that regression even though it happens not to
     * be the exact "zero-iterations" bypass path, and even though the
     * magnitude-only check above passes either way (normalize is
     * sign-agnostic, so a sign flip alone doesn't move mag2). */
    assert(st.coeff_q16[0] > 0);
    assert(st.coeff_q16[1] > 0);
    assert(st.coeff_q16[2] > 0);
}

/* Second regression test isolating the TRUE "zero iterations" bypass
 * the code review's prose specifically described, with the cascade
 * actually reaching sm64_saturn_q16_vec3_normalize's own overflow --
 * NOT a single-axis construction. A single axis at exactly INT32_MIN
 * (with the other two already exactly zero) was tried first and found,
 * empirically, to produce the IDENTICAL final answer under both the
 * pre-fix and post-fix code: normalize rescales to unit length
 * regardless of the input's absolute scale, so when only one component
 * is ever nonzero, skipping the halving loop entirely is harmless --
 * mag2 stays at (INT32_MIN)^2 = 2^62, safely inside int64_t, and
 * isqrt64/division both still land on the same correct-looking answer
 * either way. That construction was DISCARDED as a non-discriminating
 * test (confirmed by actually reverting the implementation and
 * running it -- it passed against the buggy code too, proving
 * nothing) rather than kept as a false sense of coverage.
 *
 * This version puts TWO axes simultaneously at the exact
 * self-wraparound value (dir_dir[0]=dir_dir[1]=127 against
 * m[0][0]=m[1][1]=INT32_MIN, each dividing back to exactly INT32_MIN
 * with zero remainder, so negating either in int32_t arithmetic wraps
 * back to INT32_MIN itself -- still negative, so the pre-fix
 * `a0 <= (1<<20)` check incorrectly reads it as "already small enough"
 * and the loop exits in zero iterations for BOTH axes at once). With
 * TWO components left at full ~2^31 magnitude, normalize's own
 * mag2 = v0^2 + v1^2 = 2 * (2^31)^2 = 2^63 -- one past INT64_MAX,
 * genuinely overflowing the int64_t accumulation (not just narrowing
 * afterward) -- so isqrt64(mag2 <= 0) returns 0, and normalize's own
 * `if (mag == 0) return;` leaves v completely UNCHANGED: the pre-fix
 * code returns raw ~2^31-magnitude values as "coeff_q16", off by
 * roughly 2^31/65536 = 32768x from the correct ~65536 (Q16.16
 * unit-length) answer -- the exact cascade the code review described.
 * Empirically confirmed against the reverted pre-fix implementation
 * before trusting this as a regression test (see this task's commit
 * message for the verification transcript). */
static void test_light_q16_recompute_handles_dual_axis_int32_min_cascade(void)
{
    sm64_saturn_light_state_t st;
    sm64_saturn_mtx_t mv;

    sm64_saturn_matrix_identity(&mv);
    mv.m[0][0] = INT32_MIN;
    mv.m[1][1] = INT32_MIN;
    /* row/col 2 stay identity; dir_dir[2] stays 0 below, so
     * coeff_q16[2] is unaffected by either version and isn't asserted
     * on here -- this test isolates axes 0 and 1 only. */

    sm64_saturn_light_state_init(&st);
    st.dir_dir[0] = 127;
    st.dir_dir[1] = 127;
    st.lights_changed = true;
    sm64_saturn_light_recompute_coeffs(&st, &mv);

    /* Correct answer sits close to Q16.16 unit magnitude split across
     * two axes (~65536/sqrt(2) = ~46341 each); the pre-fix bug's raw
     * output sits near -INT32_MIN in magnitude instead. Bounds are
     * checked as plain integer comparisons, deliberately avoiding any
     * multiplication in this assertion -- squaring the pre-fix bug's
     * own ~2^31-magnitude output here would risk re-triggering the
     * exact int64 overflow this test exists to catch, inside the test
     * itself. */
    assert(st.coeff_q16[0] < 0 && st.coeff_q16[0] > -131072);
    assert(st.coeff_q16[1] < 0 && st.coeff_q16[1] > -131072);
}

static void test_frontend_decodes_lights(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* A real Lights1: ambient {10,20,30}, directional {200,210,220}
     * from direction {40,50,60}. Layout confirmed against the real
     * structs (gbi.h:1398-1441): Light_t = col[3],pad1,colc[3],pad2,
     * dir[3](signed),pad3 (12 bytes); Ambient_t = col[3],pad1,
     * colc[3],pad2 (8 bytes); Lights1 = {Ambient a; Light l[1];} with
     * the directional Light immediately following the Ambient. */
    static const Lights1 lights = gdSPDefLights1(10, 20, 30,
                                                 200, 210, 220,
                                                 40, 50, 60);
    Gfx list[4];
    struct SPTask task;

    /* gSPNumLights(1): F3DEX_GBI_2 G_MOVEWORD dispatch is
     * gfx_sp_moveword(C0(16,8), C0(0,16), w1) (gfx_pc.c:1394, this
     * build's dialect per gbi.h:90-92); index=G_MW_NUMLIGHT at
     * C0(16,8) (gbi.h:1297), offset=G_MWO_NUMLIGHT at C0(0,16)
     * (gbi.h:1312), data=w1=NUML(1)=24 (gbi.h:2516). */
    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 24; /* NUML(1) */
    /* gSPLight(&lights.l[0], 1): G_MOVEMEM dispatch is
     * gfx_sp_movemem(C0(0,8), C0(8,8)*8, w1) (gfx_pc.c:1387); idx (w0
     * bits 0-7) = G_MV_LIGHT (10, gbi.h:1256), w0 bits 8-15 carry
     * ofs/8 where ofs=(n)*24+24 (gSPLight/gDma2p, gbi.h:2556,
     * 1801-1807) -- for n=1, ofs=48, ofs/8=6. */
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.l[0];
    /* gSPLight(&lights.a, 2): ofs=(2)*24+24=72, ofs/8=9. */
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&lights.a;
    list[3] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    /* Perturb away from sm64_saturn_light_state_init's own defaults
     * (num_lights=2, lights_changed=true -- saturn_light_q16.h:61-62)
     * before submit() runs. Without this, the num_lights/lights_changed
     * assertions below would be vacuously true straight out of init,
     * never actually proving the G_MOVEWORD decode ran -- caught by
     * code review, since the mutation pass never exercised these two
     * specific lines either. */
    frontend.lights.num_lights = 0;
    frontend.lights.lights_changed = false;
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.lights.num_lights == 2);
    assert(frontend.lights.dir_col[0] == 200 &&
           frontend.lights.dir_col[1] == 210 &&
           frontend.lights.dir_col[2] == 220);
    assert(frontend.lights.dir_dir[0] == 40 &&
           frontend.lights.dir_dir[1] == 50 &&
           frontend.lights.dir_dir[2] == 60);
    assert(frontend.lights.amb_col[0] == 10 &&
           frontend.lights.amb_col[1] == 20 &&
           frontend.lights.amb_col[2] == 30);
    assert(frontend.lights.lights_changed);
    assert(frontend.profile.unsupported_num_lights == 0);
}

static void test_frontend_counts_unsupported_num_lights(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    Gfx list[2];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 48; /* NUML(2): two directionals -> unsupported */
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    /* Same perturbation as test_frontend_decodes_lights above, and for
     * the same reason: num_lights defaults to 2 straight out of init
     * (saturn_light_q16.h), so the assertion below would otherwise be
     * vacuously true even if the G_MOVEWORD decode were deleted. */
    frontend.lights.num_lights = 0;
    frontend.lights.lights_changed = false;
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.unsupported_num_lights == 1);
    /* Stored RAW (3 = two directionals + ambient), deliberately NOT
     * clamped. This assertion used to read "== 2", annotated "clamped
     * to 1 directional", and that clamp was a real bug: the ambient's
     * wire slot is num_lights-1, so
     * clamping the count to 2 made the decode treat slot 1 -- which
     * under gSPSetLights2 is a second DIRECTIONAL -- as the ambient,
     * silently overwriting the real ambient color. The degradation to a
     * single directional happens at evaluation, not at decode. See
     * test_frontend_lights2_ambient_lands_in_the_right_slot below. */
    assert(frontend.lights.num_lights == 3);
}

/* The ambient's wire slot is num_lights-1, not a fixed index. Under
 * gSPSetLights2 it is slot 2 and slot 1 holds a SECOND DIRECTIONAL.
 * The decode originally hardcoded "ambient == slot 1", so this exact
 * list wrote directional #2's color over amb_col and dropped the real
 * ambient -- strictly worse than the design spec's contract row
 * ("first directional used"). Pins the slot arithmetic against both
 * halves of that bug: the ambient must survive intact, AND the second
 * directional must be ignored rather than replacing the first. */
static void test_frontend_lights2_ambient_lands_in_the_right_slot(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    /* ambient (11,22,33); directional #1 (200,210,220) dir (40,50,60);
     * directional #2 (99,98,97) dir (-10,-20,-30) -- deliberately
     * distinct from every other value so a mix-up is unambiguous. */
    static const Lights2 lights = gdSPDefLights2(
        11, 22, 33,
        200, 210, 220, 40, 50, 60,
        99, 98, 97, -10, -20, -30);
    Gfx list[5];
    struct SPTask task;

    /* gSPNumLights(2): NUML(2) = 48 -> num_lights = 48/24+1 = 3. */
    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 48;
    /* gSPLight(&lights.l[0], 1): ofs=(1)*24+24=48, ofs/8=6 -> idx 0. */
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.l[0];
    /* gSPLight(&lights.l[1], 2): ofs=(2)*24+24=72, ofs/8=9 -> idx 1,
     * the second directional -- must be IGNORED, not stored as ambient. */
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&lights.l[1];
    /* gSPLight(&lights.a, 3): ofs=(3)*24+24=96, ofs/8=12 -> idx 2,
     * which is num_lights-1 and therefore the real ambient. */
    list[3].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (12U << 8) | G_MV_LIGHT;
    list[3].words.w1 = (uintptr_t)&lights.a;
    list[4] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.lights.num_lights == 3);
    /* The real ambient survived -- NOT directional #2's (99,98,97). */
    assert(frontend.lights.amb_col[0] == 11 &&
           frontend.lights.amb_col[1] == 22 &&
           frontend.lights.amb_col[2] == 33);
    /* Directional #1 still holds the slot; #2 was ignored entirely. */
    assert(frontend.lights.dir_col[0] == 200 &&
           frontend.lights.dir_col[1] == 210 &&
           frontend.lights.dir_col[2] == 220);
    assert(frontend.lights.dir_dir[0] == 40 &&
           frontend.lights.dir_dir[1] == 50 &&
           frontend.lights.dir_dir[2] == 60);
    /* Counted as a degradation, per the contract. */
    assert(frontend.profile.unsupported_num_lights == 1);
}

/* Regression test for a real bug found while verifying this task's
 * decode against real call sites (not against either test above,
 * both of which only ever send lightidx 0/1): src/game/
 * rendering_graph_node.c unconditionally emits gSPLookAt every frame
 * under F3DEX_GBI_2 (this build's dialect), which expands to two
 * G_MOVEMEM/G_MV_LIGHT commands at G_MVO_LOOKATX=0 and
 * G_MVO_LOOKATY=24 (gbi.h:1259-1260) -- lightidx -2 and -1
 * (offset/24-2). gfx_pc.c's own gfx_sp_movemem guards this with
 * `lightidx >= 0` (gfx_pc.c:972, comment "skip lookat"); a naive
 * unconditional `else` on lightidx!=0 for the ambient branch would
 * instead treat these as ambient writes and corrupt amb_col with
 * lookat direction bytes reinterpreted as a color, every single
 * frame. Real ambient state (set first here, as it always is in a
 * real display list -- the object's own lighting setup runs before
 * any interleaved lookat refresh) must survive untouched. */
static void test_frontend_g_mv_light_ignores_lookat_offsets(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Lights1 lights = gdSPDefLights1(10, 20, 30,
                                                 200, 210, 220,
                                                 40, 50, 60);
    /* Recognizable non-zero payload standing in for the real engine's
     * `LookAt lookAt;` (rendering_graph_node.c:218) -- only needs to
     * be at least 12 bytes, matching gfx_sp_movemem's own memcpy
     * width for a Light_t. */
    static const Light dummy_lookat = {
        .l = { { 111, 122, 133 }, 0, { 111, 122, 133 }, 0, { 1, 2, 3 }, 0 }
    };
    Gfx list[5];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[0].words.w1 = (uintptr_t)&lights.l[0];
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.a;
    /* gSPLookAtX-shaped command: ofs=G_MVO_LOOKATX=0, ofs/8=0 ->
     * lightidx = 0/24-2 = -2. */
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (0U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&dummy_lookat;
    /* gSPLookAtY-shaped command: ofs=G_MVO_LOOKATY=24, ofs/8=3 ->
     * lightidx = 24/24-2 = -1. */
    list[3].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (3U << 8) | G_MV_LIGHT;
    list[3].words.w1 = (uintptr_t)&dummy_lookat;
    list[4] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.lights.amb_col[0] == 10 &&
           frontend.lights.amb_col[1] == 20 &&
           frontend.lights.amb_col[2] == 30);
    assert(frontend.lights.dir_col[0] == 200 &&
           frontend.lights.dir_col[1] == 210 &&
           frontend.lights.dir_col[2] == 220);
}

/* The normals-as-colors bug fix (this task): under G_LIGHTING, G_VTX's
 * trailing 4 bytes are a packed s8 normal (Vtx_tn.n[3]/.a), not RGBA
 * (Vtx_t.cn) -- SM64 runs with G_LIGHTING on globally
 * (src/game/game_init.c:131), so the pre-fix frontend was misreading
 * every lit vertex's normal as a garbage color. Normal (0,0,127) is
 * straight +Z; with an identity modelview (no G_MTX in this display
 * list) the light coefficient IS the raw light direction, so this
 * reuses ref_light_eval (defined above for Task 1's differential
 * tests) as the expected-value oracle instead of hand-computing one. */
static void test_frontend_lit_vertex_evaluates_lighting(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Lights1 lights = gdSPDefLights1(10, 20, 30,
                                                 200, 210, 220,
                                                 0, 0, 127);
    /* Vtx_tn shape: same bytes as Vtx_t.cn, interpreted as s8 normal
     * (0,0,127) = straight +Z, alpha 255. */
    static const Vtx_t vert = {
        .ob = { 1.0f, 2.0f, 3.0f }, .flag = 0, .tc = { 0, 0 },
        .cn = { 0, 0, 127, 255 }
    };
    Gfx list[6];
    struct SPTask task;
    uint8_t want[3];
    sm64_saturn_mtx_t ident;

    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 24;
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.l[0];
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&lights.a;
    /* set G_LIGHTING: keep-mask C0(0,24)=0xFFFFFF (clear nothing),
     * set-bits w1 = G_LIGHTING */
    list[3].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0xFFFFFFU;
    list[3].words.w1 = G_LIGHTING;
    list[4].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (1U << 1);
    list[4].words.w1 = (uintptr_t)&vert;
    list[5] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    sm64_saturn_matrix_identity(&ident);
    ref_light_eval((const int8_t[3]){ 0, 0, 127 },
                   (const uint8_t[3]){ 200, 210, 220 },
                   (const uint8_t[3]){ 10, 20, 30 },
                   &ident, (const int8_t[3]){ 0, 0, 127 }, want);
    assert((int)frontend.vertices[0].r - (int)want[0] <= 2 &&
           (int)want[0] - (int)frontend.vertices[0].r <= 2);
    assert((int)frontend.vertices[0].g - (int)want[1] <= 2 &&
           (int)want[1] - (int)frontend.vertices[0].g <= 2);
    assert((int)frontend.vertices[0].b - (int)want[2] <= 2 &&
           (int)want[2] - (int)frontend.vertices[0].b <= 2);
    assert(frontend.vertices[0].a == 255);
    assert(frontend.profile.lit_vertices == 1);
    assert(frontend.profile.unlit_vertices == 0);
    assert(!frontend.lights.lights_changed); /* lazy recompute ran */
}

/* Shared fixture for the three light/matrix-coupling tests below.
 *
 * The modelview SWAPS X and Y. With a light direction of (127,0,0), the
 * transformed coefficient is column 0 of that matrix -- (0,1,0), i.e.
 * +Y -- so the two normals below land on opposite sides of the lighting
 * result and any error in WHICH matrix is used, or WHEN it is applied,
 * changes a color rather than just a magnitude:
 *   normal (0,127,0) -> dot is maximal -> fully lit
 *   normal (127,0,0) -> dot is zero    -> ambient only
 * Under an identity modelview those two outcomes are exactly swapped,
 * which is what makes staleness detectable. */
static const float swap_xy_modelview_floats[16] = {
    0.0f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
};

static void fill_swap_xy_modelview(sm64_saturn_mtx_t *out)
{
    sm64_saturn_matrix_identity(out);
    out->m[0][0] = 0;
    out->m[0][1] = 1 << 16;
    out->m[1][0] = 1 << 16;
    out->m[1][1] = 0;
}

/* The light direction must be re-transformed by the MODELVIEW TOP, not
 * by the composed modelview-projection. Every pre-existing lit test ran
 * with an identity projection, which makes top == mp and leaves that
 * distinction invisible -- swapping one for the other survived the whole
 * suite. This projection carries a shear (m[1][0]) specifically so the
 * two differ in DIRECTION and not merely in scale: the evaluator
 * normalizes its coefficient, so a projection differing only by a
 * uniform scale would still be undetectable after normalization.
 *
 * Column 0 of the modelview is (0,1,0); column 0 of the composed MP is
 * (1,1,0). The normal (127,0,0) is therefore unlit under the correct
 * matrix and lit under the wrong one. */
static void test_frontend_light_coeff_uses_modelview_not_mp(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Lights1 lights = gdSPDefLights1(10, 20, 30,
                                                 200, 210, 220,
                                                 127, 0, 0);
    static const Vtx_t verts[2] = {
        { .ob = { 1.0f, 2.0f, 3.0f }, .cn = { 0, 127, 0, 255 } },
        { .ob = { 4.0f, 5.0f, 6.0f }, .cn = { 127, 0, 0, 255 } },
    };
    Gfx list[6];
    struct SPTask task;
    sm64_saturn_mtx_t modelview;
    sm64_saturn_mtx_t projection;
    uint8_t want_lit[3];
    uint8_t want_dark[3];

    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 24;
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.l[0];
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&lights.a;
    list[3].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0xFFFFFFU;
    list[3].words.w1 = G_LIGHTING;
    list[4].words.w0 = ((uint32_t)G_VTX << 24) | (2U << 12) | (2U << 1);
    list[4].words.w1 = (uintptr_t)verts;
    list[5] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);

    /* Seed the modelview top directly rather than through a G_MTX
     * command: this test is about which matrix the recompute READS, and
     * routing through the decode would also drag in the G_MTX dirty-flag
     * behavior that the next test covers separately. */
    fill_swap_xy_modelview(&frontend.matrix_stack.entries[0]);
    frontend.matrix_stack.mp_dirty = true;

    /* Non-identity projection with a shear, so mp differs from the
     * modelview in direction. */
    sm64_saturn_matrix_identity(&projection);
    projection.m[1][0] = 1 << 16;
    sm64_saturn_matrix_stack_set_projection(&frontend.matrix_stack,
                                            &projection);

    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    fill_swap_xy_modelview(&modelview);
    ref_light_eval((const int8_t[3]){ 127, 0, 0 },
                   (const uint8_t[3]){ 200, 210, 220 },
                   (const uint8_t[3]){ 10, 20, 30 },
                   &modelview, (const int8_t[3]){ 0, 127, 0 }, want_lit);
    ref_light_eval((const int8_t[3]){ 127, 0, 0 },
                   (const uint8_t[3]){ 200, 210, 220 },
                   (const uint8_t[3]){ 10, 20, 30 },
                   &modelview, (const int8_t[3]){ 127, 0, 0 }, want_dark);

    /* The +Y-facing normal is lit by the transformed direction... */
    assert((int)frontend.vertices[0].r - (int)want_lit[0] <= 2 &&
           (int)want_lit[0] - (int)frontend.vertices[0].r <= 2);
    assert((int)frontend.vertices[0].g - (int)want_lit[1] <= 2 &&
           (int)want_lit[1] - (int)frontend.vertices[0].g <= 2);
    /* ...and the +X-facing one receives ambient only. Feeding the
     * evaluator mp instead of the modelview top lights this vertex and
     * breaks these two assertions. */
    assert(frontend.vertices[1].r == want_dark[0]);
    assert(frontend.vertices[1].g == want_dark[1]);
    assert(frontend.vertices[1].b == want_dark[2]);
    assert(want_dark[0] == 10 && want_dark[1] == 20 && want_dark[2] == 30);
    assert(frontend.profile.lit_vertices == 2);
}

/* A modelview G_MTX must invalidate the cached light coefficient. The
 * pre-existing lit test issued no G_MTX at all, so deleting
 * `lights_changed = true` from the G_MTX branch changed nothing it
 * asserted. Here the first lit vertex is evaluated under an identity
 * modelview, a G_MTX then loads the swap-XY matrix, and a second lit
 * vertex must be evaluated under the NEW matrix -- with the normals
 * chosen so a stale coefficient produces the opposite lighting result,
 * not a slightly different one. */
static void test_frontend_g_mtx_invalidates_light_coeff(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Lights1 lights = gdSPDefLights1(10, 20, 30,
                                                 200, 210, 220,
                                                 127, 0, 0);
    /* Both vertices carry the +X normal. Under the initial identity
     * modelview the light direction is still +X, so vertex 0 is LIT.
     * After the swap-XY load the direction becomes +Y, so vertex 1 must
     * fall to ambient only. A missing invalidation leaves vertex 1 lit. */
    static const Vtx_t vert_a = {
        .ob = { 1.0f, 2.0f, 3.0f }, .cn = { 127, 0, 0, 255 }
    };
    static const Vtx_t vert_b = {
        .ob = { 4.0f, 5.0f, 6.0f }, .cn = { 127, 0, 0, 255 }
    };
    Gfx list[8];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 24;
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.l[0];
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&lights.a;
    list[3].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0xFFFFFFU;
    list[3].words.w1 = G_LIGHTING;
    /* Vertex 0 into slot 0, under the identity modelview. */
    list[4].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (1U << 1);
    list[4].words.w1 = (uintptr_t)&vert_a;
    /* Load the swap-XY modelview (LOAD, no push -- see make_g_mtx). */
    list[5] = make_g_mtx((uint8_t)((G_MTX_LOAD | G_MTX_MODELVIEW) ^ G_MTX_PUSH),
                         swap_xy_modelview_floats);
    /* Vertex 1 into slot 1, which must see the NEW matrix. */
    list[6].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (2U << 1);
    list[6].words.w1 = (uintptr_t)&vert_b;
    list[7] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.profile.lit_vertices == 2);
    /* Vertex 0: +X normal against the untransformed +X light -> lit,
     * so clearly brighter than the (10,20,30) ambient floor. */
    assert(frontend.vertices[0].r > 100);
    /* Vertex 1: same normal, but the light is now +Y -> ambient only. */
    assert(frontend.vertices[1].r == 10 &&
           frontend.vertices[1].g == 20 &&
           frontend.vertices[1].b == 30);
}

/* G_POPMTX must invalidate the cached coefficient too. This port sets
 * lights_changed on pop as a deliberate, documented deviation from
 * gfx_pc.c (which does not) -- and until now that hand-argued deviation
 * had no test at all, so deleting it was a silent no-op. Mirror image of
 * the test above: the swap-XY matrix is PUSHED, a lit vertex is
 * evaluated under it, the pop restores identity, and the final vertex
 * must be re-evaluated against the restored matrix. */
static void test_frontend_g_popmtx_invalidates_light_coeff(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Lights1 lights = gdSPDefLights1(10, 20, 30,
                                                 200, 210, 220,
                                                 127, 0, 0);
    static const Vtx_t vert_a = {
        .ob = { 1.0f, 2.0f, 3.0f }, .cn = { 127, 0, 0, 255 }
    };
    static const Vtx_t vert_b = {
        .ob = { 4.0f, 5.0f, 6.0f }, .cn = { 127, 0, 0, 255 }
    };
    Gfx list[9];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEWORD << 24) |
                       ((uint32_t)G_MW_NUMLIGHT << 16) | G_MWO_NUMLIGHT;
    list[0].words.w1 = 24;
    list[1].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (6U << 8) | G_MV_LIGHT;
    list[1].words.w1 = (uintptr_t)&lights.l[0];
    list[2].words.w0 = ((uint32_t)G_MOVEMEM << 24) | (9U << 8) | G_MV_LIGHT;
    list[2].words.w1 = (uintptr_t)&lights.a;
    list[3].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0xFFFFFFU;
    list[3].words.w1 = G_LIGHTING;
    /* PUSH the swap-XY matrix (raw G_MTX_LOAD|G_MTX_MODELVIEW decodes as
     * push+load after the frontend's own XOR with G_MTX_PUSH). */
    list[4] = make_g_mtx((uint8_t)(G_MTX_LOAD | G_MTX_MODELVIEW),
                         swap_xy_modelview_floats);
    /* Under the pushed matrix the light is +Y, so this +X normal is
     * ambient only. */
    list[5].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (1U << 1);
    list[5].words.w1 = (uintptr_t)&vert_a;
    list[6].words.w0 = (uint32_t)G_POPMTX << 24;
    list[6].words.w1 = 1U * 64U;
    /* Back on identity: the light is +X again, so this must be lit. */
    list[7].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (2U << 1);
    list[7].words.w1 = (uintptr_t)&vert_b;
    list[8] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.matrix_stack.depth == 1); /* pushed then popped */
    assert(frontend.profile.lit_vertices == 2);
    assert(frontend.vertices[0].r == 10 &&
           frontend.vertices[0].g == 20 &&
           frontend.vertices[0].b == 30);
    /* Stale coefficients from the pushed matrix would leave this one at
     * the ambient floor too. */
    assert(frontend.vertices[1].r > 100);
}

/* Companion test for the branch above: with no G_GEOMETRYMODE command
 * at all, geometry_mode is 0 (zeroed by frontend_init), so G_LIGHTING
 * is off and cn bytes must still be read as literal RGBA -- confirming
 * this task's gate didn't regress the pre-existing unlit path. The
 * lit_vertices/unlit_vertices assertions here are the mirror image of
 * the ones above: an inverted `lit` condition would misclassify THIS
 * fixture's vertex as lit (since 0 == 0 under an inverted comparison),
 * so together the two tests catch that mutation from either polarity. */
static void test_frontend_unlit_vertex_passes_colors_through(void)
{
    /* No G_LIGHTING: existing behavior, cn bytes are true RGBA. */
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t vert = {
        .ob = { 0.0f, 0.0f, 0.0f }, .flag = 0, .tc = { 0, 0 },
        .cn = { 12, 34, 56, 78 }
    };
    Gfx list[2];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_VTX << 24) | (1U << 12) | (1U << 1);
    list[0].words.w1 = (uintptr_t)&vert;
    list[1] = make_g_enddl();

    (void)memset(&task, 0, sizeof(task));
    task.task.t.data_ptr = (u64 *)list;
    sm64_saturn_fast3d_frontend_init(&frontend);
    sm64_saturn_fast3d_frontend_submit(&task, &frontend);

    assert(frontend.vertices[0].r == 12 && frontend.vertices[0].g == 34 &&
           frontend.vertices[0].b == 56 && frontend.vertices[0].a == 78);
    assert(frontend.profile.unlit_vertices == 1);
    assert(frontend.profile.lit_vertices == 0);
}

/* Task 4: the resolved triangle must carry all 3 corners' colors, not
 * just vertex 0's -- the pre-Task-4 pack threw away vertex 1 and 2's
 * colors, which Task 3 had just made correct upstream. Reuses
 * test_frontend_g_tri1_resolves_triangle's exact fixture shape (unlit
 * pure red/green/blue verts) so a pass isolates the resolve step's
 * corner-color packing itself, not any lighting-evaluator behavior. */
static void test_frontend_resolved_triangle_carries_corner_colors(void)
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

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = ((uint32_t)G_VTX << 24) | (3U << 12) | (3U << 1);
    list[1].words.w1 = (uintptr_t)verts;
    list[2].words.w0 = ((uint32_t)G_TRI1 << 24) |
                       (0U << 16) | (2U << 8) | (4U << 0);
    list[2].words.w1 = 0;
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

    assert(frontend.resolved_count == 1);
    /* Expected values derive from Yaul's `union rgb1555` bitfield order
     * (msb:1; b:5; g:5; r:5, MSB-first on big-endian SH-2), i.e. BLUE
     * occupies bits 14-10 and RED bits 4-0 -- NOT the intuitive order.
     * These constants were originally written the other way round, from
     * the implementation rather than from the hardware, which is what let
     * an R/B channel swap ship and then certified it as correct. Do not
     * "fix" these to match the code; check the code against
     * third_party/libyaul/.../color/rgb1555.h. */
    assert(frontend.resolved[0].corner_rgb1555[0] == 0x801F); /* red   */
    assert(frontend.resolved[0].corner_rgb1555[1] == 0x83E0); /* green */
    assert(frontend.resolved[0].corner_rgb1555[2] == 0xFC00); /* blue  */
    /* Negative half of the fog-counter pair (the positive half is
     * test_frontend_counts_dropped_fog): this fixture sets no G_FOG, so
     * the counter must stay at 0. Without this, replacing the counter's
     * `geometry_mode & G_FOG` gate with an unconditional increment
     * passes the whole suite -- the positive test alone cannot tell a
     * working gate from no gate at all. */
    assert(frontend.profile.fog_dropped_triangles == 0);
}

/* Companion test: G_FOG is dropped per the design spec's degradation
 * contract (fog rendering is out of scope this cycle) but the triangle
 * must still resolve normally -- only the counter distinguishes this
 * from the no-fog case above. fog_dropped_triangles lives in `profile`,
 * which sm64_saturn_fast3d_frontend_submit's own per-frame reset memsets
 * to 0 (verified by reading that reset code directly, not assumed) --
 * so, like Task 2's unsupported_num_lights, asserting it == 1 here only
 * passes if this task's new increment actually ran; it cannot pass on
 * default-initialized/zeroed state alone. */
static void test_frontend_counts_dropped_fog(void)
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
    Gfx list[5];
    struct SPTask task;

    list[0].words.w0 = ((uint32_t)G_MOVEMEM << 24) | G_MV_VIEWPORT;
    list[0].words.w1 = (uintptr_t)&vp;
    list[1].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0xFFFFFFU;
    list[1].words.w1 = G_FOG;
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

    assert(frontend.resolved_count == 1);
    assert(frontend.profile.fog_dropped_triangles == 1);
}

/* Third fog assertion, pinning the counter's PLACEMENT rather than its
 * gate. The counter sits alongside the resolve write, after every
 * reject path, so it counts triangles that actually resolved and were
 * merely stripped of fog -- not every triangle that carried the G_FOG
 * bit. Hoisting the increment above the reject returns still satisfies
 * both other fog tests (their triangles all resolve), so without this
 * case that move is a silent no-op. Here a degenerate triangle -- all
 * three vertices at one point -- is rejected before the resolve write,
 * with G_FOG set: nothing resolves, so nothing may be counted. */
static void test_frontend_rejected_fog_triangle_is_not_counted(void)
{
    sm64_saturn_fast3d_frontend_t frontend;
    static const Vtx_t verts[3] = {
        { .ob = { 0.0f, 0.0f, 500.0f }, .cn = {255, 0, 0, 255} },
        { .ob = { 0.0f, 0.0f, 500.0f }, .cn = {0, 255, 0, 255} },
        { .ob = { 0.0f, 0.0f, 500.0f }, .cn = {0, 0, 255, 255} },
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
    list[1].words.w0 = ((uint32_t)G_GEOMETRYMODE << 24) | 0xFFFFFFU;
    list[1].words.w1 = G_FOG;
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

    /* Rejected before resolving (which reject bucket caught it is not
     * this test's contract -- only that nothing resolved). */
    assert(frontend.resolved_count == 0);
    assert(frontend.profile.fog_dropped_triangles == 0);
}

/* Task 5: the frame-local Gouraud staging bank is pure bookkeeping
 * (Yaul-free) that will hold one 8-byte table per emitted triangle.
 * This test exercises alloc's two return channels together -- the
 * staging slot pointer AND the device address written to *vram_addr --
 * and the used-prefix accounting init()/begin() drive. A broken
 * address computation (e.g. always returning vram_base instead of
 * advancing by sizeof(table) per slot) is caught by the addr1 assert;
 * a broken used-bytes count (e.g. hardcoded 0, or not multiplying by
 * the table size) is caught by the used_bytes asserts; a begin() that
 * forgets to reset `used` is caught by the post-reset used_bytes == 0
 * assert immediately after. */
static void test_gouraud_bank_alloc_and_used_prefix(void)
{
    static sm64_saturn_gouraud_table_t staging[4];
    sm64_saturn_gouraud_bank_t bank;
    uintptr_t addr0, addr1;
    sm64_saturn_gouraud_table_t *t;

    assert(sm64_saturn_gouraud_bank_init(&bank, staging, 4, 0x25C7F000u));
    sm64_saturn_gouraud_bank_begin(&bank);
    t = sm64_saturn_gouraud_bank_alloc(&bank, &addr0);
    assert(t == &staging[0]);
    assert(addr0 == 0x25C7F000u);
    t = sm64_saturn_gouraud_bank_alloc(&bank, &addr1);
    assert(t == &staging[1]);
    assert(addr1 == 0x25C7F000u + sizeof(sm64_saturn_gouraud_table_t));
    assert(sm64_saturn_gouraud_bank_used_bytes(&bank) ==
           2 * sizeof(sm64_saturn_gouraud_table_t));
    sm64_saturn_gouraud_bank_begin(&bank); /* frame reset */
    assert(sm64_saturn_gouraud_bank_used_bytes(&bank) == 0);
}

/* Companion test: capacity-boundary enforcement (allocs beyond capacity
 * return NULL rather than overrunning the caller's staging array) and
 * the zero-capacity degrade-first contract -- a missing/overlapping
 * VRAM partition yields a bank that is legal to use but always
 * exhausted. The zero-capacity alloc-returns-NULL assert specifically
 * catches an off-by-one `used > capacity` (instead of `>=`) mutation:
 * under `>`, 0 > 0 is false, so a broken bank would allocate slot 0
 * instead of refusing -- this exercises that boundary directly, not
 * just the nonzero-capacity overflow case above it. */
static void test_gouraud_bank_overflow_returns_null(void)
{
    static sm64_saturn_gouraud_table_t staging[2];
    sm64_saturn_gouraud_bank_t bank;
    uintptr_t addr;

    assert(sm64_saturn_gouraud_bank_init(&bank, staging, 2, 0x1000u));
    sm64_saturn_gouraud_bank_begin(&bank);
    assert(sm64_saturn_gouraud_bank_alloc(&bank, &addr) != NULL);
    assert(sm64_saturn_gouraud_bank_alloc(&bank, &addr) != NULL);
    assert(sm64_saturn_gouraud_bank_alloc(&bank, &addr) == NULL);
    /* zero-capacity init (partition missing/overlapping) is legal and
     * yields an always-NULL bank -- the graceful all-flat fallback */
    assert(!sm64_saturn_gouraud_bank_init(&bank, staging, 0, 0x1000u));
    sm64_saturn_gouraud_bank_begin(&bank);
    assert(sm64_saturn_gouraud_bank_alloc(&bank, &addr) == NULL);
}

int main(void)
{
    assert(sm64_saturn_gouraud_neutral_color() == 0xC210U);
    test_identity_camera();
    test_q16_normalization();
    test_frame_profile();
    test_bounded_memory_arena();
    test_source_identified_render_queue();
    test_projected_workarea();
    test_bounded_command_arena();
    test_matrix_decode_identity();
    test_matrix_decode_translation();
    test_matrix_decode_q16_native();
    test_matrix_multiply_identity();
    test_matrix_multiply_overflow_guard();
    test_matrix_multiply_accumulator_overflow_guard();
    test_matrix_multiply_nontrivial();
    test_matrix_stack_push_pop();
    test_matrix_stack_overflow();
    test_matrix_stack_pop_past_floor();
    test_matrix_mp_lazy_composition();
    test_matrix_mp_cache_invalidates_across_pop();
    test_frontend_g_mtx_load_modelview();
    test_frontend_g_popmtx_scales_by_64();
    test_frontend_rdp_vs_sp_opcode_classification();
    test_frontend_g_mtx_push_before_load();
    test_frontend_g_mtx_projection_load_and_multiply();
    test_frontend_g_mtx_modelview_multiply();
    test_frontend_g_mtx_high_water_mark();
    test_frontend_g_movemem_viewport();
    test_frontend_g_movemem_viewport_real_default();
    test_frontend_g_geometrymode();
    test_frontend_g_geometrymode_clear_and_set();
    test_frontend_g_vtx_transform();
    test_frontend_g_vtx_rejects_out_of_range_dest();
    test_frontend_g_tri1_resolves_triangle();
    test_frontend_g_tri1_backface_cull();
    test_frontend_g_tri1_modelview_translation_shifts_screen_x();
    test_frontend_g_tri1_small_w_clamps_screen_coords();
    test_frontend_g_tri2_two_triangles();
    test_frontend_g_tri2_distinct_depth_buckets();
    test_frontend_resolved_capacity_exceeds_old_192_cap();
    test_frontend_submit_resets_resolved_count_each_frame();
    test_kernels_isqrt64();
    test_kernels_q16_mul();
    test_kernels_float_q16_roundtrip();
    test_kernels_trig_lookup();
    test_light_q16_matches_float_reference();
    test_light_q16_zero_normal_gets_ambient();
    test_light_q16_recompute_handles_extreme_matrix_entries();
    test_light_q16_recompute_handles_dual_axis_int32_min_cascade();
    test_frontend_decodes_lights();
    test_frontend_counts_unsupported_num_lights();
    test_frontend_lights2_ambient_lands_in_the_right_slot();
    test_frontend_g_mv_light_ignores_lookat_offsets();
    test_frontend_lit_vertex_evaluates_lighting();
    test_frontend_light_coeff_uses_modelview_not_mp();
    test_frontend_g_mtx_invalidates_light_coeff();
    test_frontend_g_popmtx_invalidates_light_coeff();
    test_frontend_unlit_vertex_passes_colors_through();
    test_frontend_resolved_triangle_carries_corner_colors();
    test_frontend_counts_dropped_fog();
    test_frontend_rejected_fog_triangle_is_not_counted();
    test_gouraud_bank_alloc_and_used_prefix();
    test_gouraud_bank_overflow_returns_null();
    return 0;
}
