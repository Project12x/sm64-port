#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_frame_profile.h"
#include "saturn_gouraud.h"
#include "saturn_command_arena.h"
#include "saturn_memory_arena.h"
#include "saturn_projected_workarea.h"
#include "saturn_render_queue.h"
#include "saturn_transform.h"
#include "saturn_matrix.h"
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
    return 0;
}
