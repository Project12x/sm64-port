#include <string.h>
#include <stdbool.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "types.h"

#include "saturn_fast3d_frontend.h"
#include "saturn_projected_workarea.h"
#include "saturn_quad_map.h"
#include "saturn_render_native_math.h"

/* saturn_fast3d_frontend.h stores quad-map entries as plain uint32_t so it
 * need not include a generated build artifact (see its comment on
 * quad_entries). Tie the two together here, where the generated header IS
 * in scope, so a change to the generated encoding's width is a compile
 * error rather than a silent misread of the table. */
_Static_assert(sizeof(sm64_saturn_quad_map_entry_t) == sizeof(uint32_t),
               "quad map entry must stay a 32-bit word");
/* The ordinal->slot side array is indexed by a live triangle ordinal, which
 * is bounded by the longest row the generator actually emitted. Asserting
 * against the GENERATED constant rather than a remembered number is what
 * keeps the bound from drifting: regenerate a map with a longer row and the
 * build stops here instead of writing past the array. */
_Static_assert(SM64_SATURN_QUAD_MAP_MAX_ENTRIES <=
                   SM64_SATURN_FAST3D_QUAD_SLOT_CAPACITY,
               "quad map row longer than the frontend's slot array; raise "
               "SM64_SATURN_FAST3D_QUAD_SLOT_CAPACITY");
/* Every quad_slot_stamp generation still on submit()'s call stack must be
 * distinct, or a caller could read a nested list's slots as its own. A new
 * generation is taken once per display list entered, and entering one costs
 * at least one command, so a single submit() can take at most
 * SM64_SATURN_FAST3D_MAX_COMMANDS + 1 of them -- far short of the 16-bit
 * counter's period, which is why the wrap handling in
 * sm64_saturn_fast3d_quad_map_bind() can never fire mid-list-stack. */
_Static_assert(SM64_SATURN_FAST3D_MAX_COMMANDS < 0xFFFFU,
               "generation counter must not be able to wrap within one "
               "display-list call stack");

#define SM64_SATURN_C0(w0, pos, width) \
    (((w0) >> (pos)) & ((1U << (width)) - 1U))
#define SM64_SATURN_C1(w1, pos, width) \
    (((w1) >> (pos)) & ((1U << (width)) - 1U))

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

static void sm64_saturn_fast3d_count_command(
    sm64_saturn_fast3d_profile_t *profile, uint8_t opcode)
{
    switch (opcode) {
        case G_MTX:
            profile->matrix_commands++;
            break;
        case G_VTX:
            profile->vertex_commands++;
            break;
        case G_DL:
            /* Control flow is accounted for by the caller. */
            break;
        case (uint8_t)G_TRI1:
            profile->triangle_count++;
            break;
#if defined(F3DEX_GBI) || defined(F3DLP_GBI) || defined(F3DEX_GBI_2)
        case (uint8_t)G_TRI2:
            profile->triangle_count += 2U;
            break;
#endif

        case (uint8_t)G_TEXTURE:
        case G_SETTIMG:
        case G_SETTILE:
        case G_SETTILESIZE:
        case G_LOADBLOCK:
        case G_LOADTILE:
        case G_LOADTLUT:
            profile->texture_commands++;
            break;
        /*
         * Remaining genuine RDP pass-through commands (include/PR/gbi.h:
         * 179-205 -- always defined the same way regardless of GBI dialect,
         * unlike G_MTX/G_VTX/G_DL/etc. above).
         *
         * Under the classic (pre-F3DEX_GBI_2) numbering these all satisfied
         * `opcode >= G_NOOP` by construction: the top two bits of the opcode
         * byte are the DMA/Immediate/RDP tag (see include/PR/gbi.h:43-51),
         * and G_NOOP (0xc0) was the lowest value with both bits set. Once
         * F3DEX_GBI_2E is defined, G_NOOP is redefined to 0x00 (making that
         * comparison a tautology) and several SP-side commands (G_POPMTX,
         * G_GEOMETRYMODE, G_MOVEWORD, G_MOVEMEM, G_LOAD_UCODE, G_ENDDL,
         * G_SPNOOP, G_RDPHALF_1/2, G_SETOTHERMODE_L/H, G_SPECIAL_1/2/3,
         * G_DMA_IO) are assigned opcode bytes in the very same high range as
         * these real RDP commands -- so no single numeric threshold can
         * separate "RDP command" from "SP command" any more under this
         * dialect. Enumerate the true RDP set explicitly instead (matching
         * how src/pc/gfx/gfx_pc.c itself dispatches: named case labels, not
         * a range check). Opcodes the RSP microcode generates internally
         * (G_TRI_FILL/G_TRI_SHADE/... , include/PR/gbi.h:216-223) are
         * omitted -- they never appear in an authored source display list.
         *
         * Note G_MOVEWORD and G_MOVEMEM specifically: this function only
         * ever buckets them into other_commands below (their opcode bytes
         * aren't in the enumerated RDP set) -- that classification is
         * unchanged and correct. Real semantic handling for both (numLights
         * decode, G_MV_LIGHT light payload decode) lives in the separate
         * sm64_saturn_fast3d_decode_command below, which this function's
         * (profile, opcode)-only signature has no way to reach (see that
         * function's own header comment). The two functions classify and
         * decode independently; neither implies the other already exists.
         */
        case G_SETCIMG:
        case G_SETZIMG:
        case G_SETCOMBINE:
        case G_SETENVCOLOR:
        case G_SETPRIMCOLOR:
        case G_SETBLENDCOLOR:
        case G_SETFOGCOLOR:
        case G_SETFILLCOLOR:
        case G_FILLRECT:
        case G_RDPSETOTHERMODE:
        case G_SETPRIMDEPTH:
        case G_SETSCISSOR:
        case G_SETCONVERT:
        case G_SETKEYR:
        case G_SETKEYGB:
        case G_RDPFULLSYNC:
        case G_RDPTILESYNC:
        case G_RDPPIPESYNC:
        case G_RDPLOADSYNC:
        case G_TEXRECTFLIP:
        case G_TEXRECT:
            profile->rdp_commands++;
            break;
        default:
            profile->other_commands++;
            break;
    }
}

/* Calibrated in raw, unscaled world/model units -- matching the UNIT
 * convention castleviewer's own NEAR_DEPTH/FAR_DEPTH use (castleviewer/
 * main.c:36-37: NEAR_DEPTH=128, FAR_DEPTH=8192; its projected z is stored
 * raw, a single >>16-reduced dot product, never re-multiplied by 65536).
 * The actual NEAR_DEPTH value below (64) intentionally differs from
 * castleviewer's 128 -- only the "raw units, not scaled by 65536" storage
 * convention is shared, not the specific threshold. This increment's cw
 * (clip-space w, in the same raw units once divided by 65536.0f below)
 * must be stored the same way -- do NOT multiply by 65536.0f again before
 * pushing into the workarea, or every real triangle's depth would appear
 * to be tens of thousands of units out of range and get near/far-rejected. */
/* Calibrated against live Bob-omb Battlefield data (2026-07-21 headless
 * counter/snapshot session, docs/saturn/HANDOFF addendum trail):
 * - NEAR = 1, not castleviewer's 64/128: SM64 renders HUD and other
 *   orthographic geometry with w exactly 1.0; a perspective-scaled near
 *   threshold silently killed all of it (first-reject snapshot showed an
 *   on-screen quad at z=[1,1], clip_and=0). Perspective geometry
 *   approaching the camera is still guarded by the strict w>0 reject,
 *   the VDP1-window coordinate clamp, and the span cap.
 * - FAR = 16384, not 8192: SM64's stock perspective far plane is 12800;
 *   8192 wrongly culled 419 of 1431 real triangles in the measured
 *   frame (distant floor/mountain). 16384 covers stock content with
 *   headroom. */
#define SM64_SATURN_NEAR_DEPTH 1
#define SM64_SATURN_FAR_DEPTH 16384
#define SM64_SATURN_MAX_PROJECTED_SPAN 640

/* Physical screen width -- unlike height, this doesn't differ between
 * the N64 source (320x240) and the Saturn target (320x224), so there is
 * no source/target split the way SM64_SATURN_SOURCE_SCREEN_HEIGHT/
 * SM64_SATURN_TARGET_SCREEN_HEIGHT have (Task 7). Used below for
 * clip_viewport's true-screen bounds, alongside the existing
 * SM64_SATURN_TARGET_SCREEN_HEIGHT constant Task 7 already defined. */
#define SM64_SATURN_TARGET_SCREEN_WIDTH 320

/* Bind the quad map row for `list`, or unbind if the list has no row.
 *
 * Called once per display list ENTERED -- the task's own entry list, a
 * G_DL call, and a G_DL branch (no_push) alike. Resetting triangle_ordinal
 * here is what makes the key `(display_list, list_ordinal)` rather than a
 * global count, and it matters on the branch path too: the offline walker
 * treats gsSPBranchList as an unmodelled macro and poisons it, so a branch
 * target never has a row at all, but the counter must not carry across or
 * every ordinal after the branch would be shifted.
 *
 * Taking a fresh slot generation here is what retires the previous list's
 * ordinal->slot entries without touching them. It also covers the case a
 * per-entry clear would miss: the SAME display list invoked twice in one
 * frame, where the pointer and every ordinal repeat but the recorded
 * resolved[] slots belong to the first invocation. */
static void
sm64_saturn_fast3d_quad_map_bind(sm64_saturn_fast3d_frontend_t *frontend,
                                 const Gfx *list)
{
    sm64_saturn_fast3d_profile_t *profile = &frontend->profile;

    frontend->quad_entries = NULL;
    frontend->quad_entry_count = 0U;
    frontend->triangle_ordinal = 0U;

    /* Allocate from the monotonic counter, NOT by incrementing the current
     * generation -- see the quad_slot_next_generation comment in
     * saturn_fast3d_frontend.h. Incrementing the current one hands two
     * sibling nested lists the same stamp, because the caller's generation
     * is restored between them. */
    frontend->quad_slot_next_generation++;
    if (frontend->quad_slot_next_generation == 0U) {
        /* Wrapped. Retire every stamp explicitly and restart at 1, so a
         * stamp written 65,536 generations ago cannot alias a live one.
         * A caller's generation may still be on the call stack when this
         * fires, but it was allocated before the wrap and can never be
         * reissued within the same submit() -- see the MAX_COMMANDS
         * assertion at the top of this file -- so that caller simply finds
         * its own slots retired and declines. */
        (void)memset(frontend->quad_slot_stamp, 0,
                     sizeof(frontend->quad_slot_stamp));
        frontend->quad_slot_next_generation = 1U;
    }
    frontend->quad_slot_generation = frontend->quad_slot_next_generation;

    for (uint16_t i = 0U; i < sm64_saturn_quad_map_list_count; i++) {
        if (sm64_saturn_quad_map_lists[i].display_list == list) {
            if (sm64_saturn_quad_map_lists[i].entry_count >
                    SM64_SATURN_FAST3D_QUAD_SLOT_CAPACITY) {
                /* Cannot be indexed safely, so it is not used at all. The
                 * _Static_assert above makes this unreachable for the
                 * table this binary was built against; it exists so a
                 * mismatched table declines loudly instead of writing past
                 * quad_slot[]. */
                profile->quad_map_mismatch++;
                break;
            }
            frontend->quad_entries =
                (const uint32_t *)sm64_saturn_quad_map_lists[i].entries;
            frontend->quad_entry_count =
                sm64_saturn_quad_map_lists[i].entry_count;
            break;
        }
    }
}

/* A paired entry's four corner codes must all name a real corner: 0-2 this
 * triangle's own, 3-5 its partner's. Anything else is a corrupt or stale
 * word, and this compiler never treats an unrecognised entry as safe. */
static bool sm64_saturn_fast3d_quad_entry_corners_valid(uint32_t entry)
{
    for (uint32_t c = 0U; c < 4U; c++) {
        if (SM64_SATURN_QUAD_MAP_CORNER(entry, c) >=
                SM64_SATURN_QUAD_MAP_CORNER_LIMIT) {
            return false;
        }
    }
    return true;
}

#ifdef SATURN_MTX_IS_Q16
/* Q16.16 perspective divide used by the target hot path.  A tiny positive
 * clip w can produce a quotient outside the signed Q16 range; the float
 * reference then clamps that value at the VDP1 coordinate boundary.  Keep
 * the same defined behaviour by saturating the quotient instead of dropping
 * the triangle on a helper overflow. */
static int32_t sm64_saturn_fast3d_q16_div_clamped(int32_t numerator,
                                                   int32_t denominator)
{
    int32_t result;
    if (denominator == 0) {
        return numerator < 0 ? INT32_MIN : INT32_MAX;
    }
    if (sm64_saturn_div_s64_s32((int64_t)numerator * INT64_C(65536),
                                denominator, &result)) {
        return result;
    }
    return ((numerator < 0) != (denominator < 0)) ? INT32_MIN : INT32_MAX;
}

/* Map a normalized Q16 coordinate to pixels with truncation toward zero,
 * matching the source float cast before the int16 narrowing/clamp. */
static int32_t sm64_saturn_fast3d_q16_viewport_coord(int32_t ndc,
                                                      int16_t origin,
                                                      int16_t extent,
                                                      bool flip_y)
{
    const int64_t one = INT64_C(1) << 16;
    const int64_t adjusted = flip_y ? (one - ndc) : (ndc + one);
    int32_t pixels;
    if (!sm64_saturn_div_s64_s32(adjusted * extent, 2 * one, &pixels)) {
        pixels = adjusted < 0 ? INT32_MIN : INT32_MAX;
    }
    return (int32_t)origin + pixels;
}
#endif

/* Painter-ordering bucket for one primitive's furthest corner. Factored out
 * of the resolve stage because a merged quad must RECOMPUTE its bucket over
 * all four corners (max of both triangles' max_z) rather than inherit
 * either side's -- see the max_z-not-center_z note at the original call
 * site, whose reasoning extends from three corners to four. */
static uint16_t sm64_saturn_fast3d_depth_bucket(int32_t max_z)
{
    int32_t bucket = 0;
    const int64_t scaled = (int64_t)(max_z - SM64_SATURN_NEAR_DEPTH) *
        (SM64_SATURN_FAST3D_DEPTH_BUCKETS - 1);
    if (!sm64_saturn_div_s64_s32(
            scaled, SM64_SATURN_FAR_DEPTH - SM64_SATURN_NEAR_DEPTH,
            &bucket)) {
        bucket = scaled < 0 ? 0 : SM64_SATURN_FAST3D_DEPTH_BUCKETS - 1;
    }
    if (bucket < 0) bucket = 0;
    if (bucket >= SM64_SATURN_FAST3D_DEPTH_BUCKETS)
        bucket = SM64_SATURN_FAST3D_DEPTH_BUCKETS - 1;
    return (uint16_t)bucket;
}

static __attribute__((unused)) const sm64_saturn_fast3d_cached_vertex_t *
sm64_saturn_fast3d_transform_vertex(sm64_saturn_fast3d_frontend_t *frontend,
                                    uint8_t index)
{
    sm64_saturn_fast3d_cached_vertex_t *cached = &frontend->transformed[index];

    if (cached->generation != frontend->transform_generation) {
        const sm64_saturn_fast3d_vertex_t *vertex = &frontend->vertices[index];
        const sm64_saturn_mtx_t *mp =
            sm64_saturn_matrix_stack_mp(&frontend->matrix_stack);
        const int32_t x = sm64_saturn_float_to_q16(vertex->x);
        const int32_t y = sm64_saturn_float_to_q16(vertex->y);
        const int32_t z = sm64_saturn_float_to_q16(vertex->z);

        cached->clip_x = sm64_saturn_q16_mul(x, mp->m[0][0]) +
                         sm64_saturn_q16_mul(y, mp->m[1][0]) +
                         sm64_saturn_q16_mul(z, mp->m[2][0]) + mp->m[3][0];
        cached->clip_y = sm64_saturn_q16_mul(x, mp->m[0][1]) +
                         sm64_saturn_q16_mul(y, mp->m[1][1]) +
                         sm64_saturn_q16_mul(z, mp->m[2][1]) + mp->m[3][1];
        cached->clip_w = sm64_saturn_q16_mul(x, mp->m[0][3]) +
                         sm64_saturn_q16_mul(y, mp->m[1][3]) +
                         sm64_saturn_q16_mul(z, mp->m[2][3]) + mp->m[3][3];
        cached->generation = frontend->transform_generation;
    }
    return cached;
}

#ifdef SM64_SATURN_FAST3D_Q16_TRACE
static void
sm64_saturn_fast3d_q16_trace_capture(
    sm64_saturn_fast3d_frontend_t *frontend, const uint8_t idx[3],
    const float cx[3], const float cy[3], const float cw[3])
{
    sm64_saturn_fast3d_q16_trace_t *trace = &frontend->q16_trace;
    const uint32_t write_count = trace->write_count++;
    sm64_saturn_fast3d_q16_trace_record_t *record =
        &trace->records[write_count % SM64_SATURN_FAST3D_Q16_TRACE_CAPACITY];
    const sm64_saturn_mtx_t *mp =
        sm64_saturn_matrix_stack_mp(&frontend->matrix_stack);

    if (write_count >= SM64_SATURN_FAST3D_Q16_TRACE_CAPACITY) {
        trace->dropped_count++;
    }
    (void)memcpy(record->mp, mp->m, sizeof(record->mp));
    record->viewport[0] = frontend->viewport.x;
    record->viewport[1] = frontend->viewport.y;
    record->viewport[2] = frontend->viewport.width;
    record->viewport[3] = frontend->viewport.height;
    record->geometry_mode = frontend->geometry_mode;
    for (int c = 0; c < 3; c++) {
        const sm64_saturn_fast3d_vertex_t *vertex = &frontend->vertices[idx[c]];
        record->source_xyz[c][0] = vertex->x;
        record->source_xyz[c][1] = vertex->y;
        record->source_xyz[c][2] = vertex->z;
        record->float_clip_xyw[c][0] = cx[c];
        record->float_clip_xyw[c][1] = cy[c];
        record->float_clip_xyw[c][2] = cw[c];
    }
    (void)memcpy(record->dir_col, frontend->lights.dir_col,
                 sizeof(record->dir_col));
    (void)memcpy(record->amb_col, frontend->lights.amb_col,
                 sizeof(record->amb_col));
    (void)memcpy(record->dir_dir, frontend->lights.dir_dir,
                 sizeof(record->dir_dir));
    record->num_lights = frontend->lights.num_lights;
    record->lighting_enabled =
        (frontend->geometry_mode & G_LIGHTING) != 0U ? 1U : 0U;
}
#endif

/* Per-triangle pipeline for G_TRI1/G_TRI2: transform the three indexed
 * vertices through the current MP (modelview*projection) matrix,
 * perspective-divide, backface-cull in pre-viewport Y-up clip space, map
 * to viewport pixels, then feed the projected corners into the existing
 * near/far + span rejection and max_z depth-bucketing. See the design
 * spec's "Per-triangle pipeline" section for the authoritative step
 * order this function follows. */
static void
sm64_saturn_fast3d_resolve_triangle(sm64_saturn_fast3d_frontend_t *frontend,
                                    uint8_t i0, uint8_t i1, uint8_t i2)
{
    sm64_saturn_fast3d_profile_t *profile = &frontend->profile;
    const sm64_saturn_mtx_t *mp =
        sm64_saturn_matrix_stack_mp(&frontend->matrix_stack);
#ifndef SATURN_MTX_IS_Q16
    const sm64_saturn_mtx_float_cache_t *mp_float =
        &frontend->matrix_stack.mp_float;
#endif
    /* Bring-up diagnostic -- see the profile struct's
     * dbg_mp_compose_overflowed_ever comment (saturn_fast3d_frontend.h). */
    profile->dbg_mp_compose_overflowed_ever =
        frontend->matrix_stack.mp_overflowed ? 1U : 0U;
    const uint8_t idx[3] = {i0, i1, i2};
#ifdef SATURN_MTX_IS_Q16
    int32_t cx[3], cy[3]; /* normalized clip-space x/w and y/w, Q16.16 */
    int32_t cw[3];        /* raw clip w in world units, not Q16 scale */
    int32_t clip_w_q16[3];
#else
    float cx[3], cy[3], cw[3]; /* pre-viewport clip-space x/w, y/w, and
                                 * raw w (NOT further scaled -- see the
                                 * NEAR/FAR_DEPTH comment above) */
#endif
    sm64_saturn_projected_vertex_t projected_storage[4];
    sm64_saturn_projected_workarea_t workarea;
    uint16_t projected_indices[4];
    /* Zero-initialized so the bring-up diagnostics below read defined
     * values even on the (structurally unreachable here) analyze-failure
     * path, where the quad is never populated. */
    sm64_saturn_projected_quad_t quad = {0};
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
    uint16_t corner_color[3];

    /* ---- Quad map lookup, once per triangle command ------------------
     *
     * The ordinal advances for EVERY triangle command that reaches this
     * function, including the ones rejected below, because the offline
     * walker counts commands and knows nothing about culling. G_TRI2
     * therefore advances it twice, once per call, and a nested G_DL's
     * triangles are counted by the child's own ordinal (reset in
     * sm64_saturn_fast3d_quad_map_bind), never the parent's. Any
     * divergence here silently mis-pairs triangles, so the counting rule
     * is deliberately the simplest one that can match the walker: one
     * increment per resolve call, no exceptions. */
    const uint16_t ordinal = frontend->triangle_ordinal;
    uint32_t entry = SM64_SATURN_QUAD_MAP_NONE;
    bool merge_into_pending = false;
    uint16_t merge_slot = 0U;
    int32_t merge_max_z = 0;
    bool open_pair = false;

    if (ordinal < frontend->quad_entry_count) {
        entry = frontend->quad_entries[ordinal];
    } else if (frontend->quad_entries != NULL) {
        /* Past the end of this list's row. NOT a fault: quad_map.py trims
         * each row after its last paired ordinal, and the encoding is
         * built so a missing tail decodes as "do not merge" -- the
         * generated header lists this case alongside a zeroed page and an
         * absent display list. Counted separately from quad_map_mismatch
         * so the latter can stay a hard must-be-zero gate. */
        profile->quad_ordinal_past_row++;
    }
    frontend->triangle_ordinal++;

    if (SM64_SATURN_QUAD_MAP_IS_PAIRED(entry) &&
        !sm64_saturn_fast3d_quad_entry_corners_valid(entry)) {
        profile->quad_map_mismatch++;
        entry = SM64_SATURN_QUAD_MAP_NONE;
    }

    if (SM64_SATURN_QUAD_MAP_IS_PAIRED(entry)) {
        const uint32_t partner = SM64_SATURN_QUAD_MAP_PARTNER(entry);

        if (partner >= (uint32_t)frontend->quad_entry_count ||
            partner == (uint32_t)ordinal) {
            /* A partner ordinal outside the row it is supposed to index,
             * or a triangle paired with itself: the table disagrees with
             * itself. Refuse the merge and say so. */
            profile->quad_map_mismatch++;
        } else if (!SM64_SATURN_QUAD_MAP_IS_PAIRED(
                       frontend->quad_entries[partner]) ||
                   SM64_SATURN_QUAD_MAP_PARTNER(
                       frontend->quad_entries[partner]) !=
                       (uint32_t)ordinal) {
            /* Both halves must name each other. A one-sided claim is a
             * broken table -- quad_map.py refuses to emit one -- and
             * merging on it would decode the cycle out of a word that was
             * never written for this pair. */
            profile->quad_map_mismatch++;
        } else if (partner > (uint32_t)ordinal) {
            /* The partner is still ahead of us: leave this triangle's
             * resolved[] slot behind for it, once we know we have one. */
            open_pair = true;
            if (partner != (uint32_t)ordinal + 1U) {
                /* Shape statistic only, counted once per pair at the lower
                 * ordinal: since the ordinal->slot array landed these merge
                 * exactly like adjacent ones. See the header comment. */
                profile->quad_pair_not_adjacent++;
            }
        } else if (frontend->quad_slot_stamp[partner] !=
                       frontend->quad_slot_generation) {
            /* The partner was walked earlier in THIS invocation of THIS
             * list but never reached resolved[] -- culled, clipped,
             * degenerate, or over the buffer ceiling. There is nothing to
             * merge into. A stale index from another list, another
             * invocation or another frame lands here too, because its
             * stamp belongs to a retired generation. */
            profile->quad_pairs_declined++;
        } else if (frontend->quad_slot[partner] >= frontend->resolved_count) {
            /* Belt and braces, and knowingly so: a live stamp always names
             * a slot below the current resolved_count, because slots are
             * only recorded immediately after being written and
             * resolved_count never shrinks within a frame. This branch
             * survives mutation testing unpunished for exactly that reason
             * -- nothing a well-formed run can do reaches it. It is kept
             * because the failure it guards against is an out-of-range
             * write into resolved[], and because it is the branch that
             * turns a broken generation stamp into a counted decline
             * rather than silent corruption. */
            profile->quad_map_mismatch++;
        } else {
            merge_into_pending = true;
            merge_slot = frontend->quad_slot[partner];
            merge_max_z = frontend->quad_slot_max_z[partner];
        }
    }

    profile->triangles_transformed++;

    for (int c = 0; c < 3; c++) {
        if (idx[c] >= SM64_SATURN_FAST3D_MAX_VERTICES) {
            profile->reject_vertex_range++;
            return;
        }
        const sm64_saturn_fast3d_vertex_t *v = &frontend->vertices[idx[c]];
#ifdef SATURN_MTX_IS_Q16
        /* Row-vector transform in the authoritative Q16.16 domain.  Vertex
         * positions are decoded from their IEEE representation once; no
         * target float multiply, divide, or libgcc soft-float helper remains
         * in this loop. */
        const int32_t mx = sm64_saturn_float_to_q16(v->x);
        const int32_t my = sm64_saturn_float_to_q16(v->y);
        const int32_t mz = sm64_saturn_float_to_q16(v->z);
        const int32_t x = sm64_saturn_q16_mul(mx, mp->m[0][0]) +
                          sm64_saturn_q16_mul(my, mp->m[1][0]) +
                          sm64_saturn_q16_mul(mz, mp->m[2][0]) + mp->m[3][0];
        const int32_t y = sm64_saturn_q16_mul(mx, mp->m[0][1]) +
                          sm64_saturn_q16_mul(my, mp->m[1][1]) +
                          sm64_saturn_q16_mul(mz, mp->m[2][1]) + mp->m[3][1];
        const int32_t w = sm64_saturn_q16_mul(mx, mp->m[0][3]) +
                          sm64_saturn_q16_mul(my, mp->m[1][3]) +
                          sm64_saturn_q16_mul(mz, mp->m[2][3]) + mp->m[3][3];
        if (w <= 0) {
            profile->reject_near_far++;
            profile->reject_w_nonpositive++;
            /* Bring-up diagnostic -- see the profile struct's
             * reject_w_nonpositive_overflow_suspect comment. w is already
             * known <= 0.0f on this path; -1.0e6f is ~30x this boot's
             * largest plausible real coordinate (the forced intro-cutscene
             * spline tops out near 30,000 units; Bob-omb Battlefield's own
             * geometry stays within +-8192) -- comfortably beyond any real
             * value, comfortably short of the hundred-million-plus
             * magnitudes a fixed-point overflow produces. */
            if (w == INT32_MIN) {
                profile->reject_w_nonpositive_overflow_suspect++;
            }
            if (profile->reject_w_nonpositive == 1U) {
                /* Bring-up diagnostic -- see the profile struct's
                 * dbg_first_w_reject_* comment (saturn_fast3d_frontend.h). */
                profile->dbg_first_w_reject_mx = v->x;
                profile->dbg_first_w_reject_my = v->y;
                profile->dbg_first_w_reject_mz = v->z;
                profile->dbg_first_w_reject_w = sm64_saturn_q16_to_float(w);
                profile->dbg_first_w_reject_mp03 = mp->m[0][3];
                profile->dbg_first_w_reject_mp13 = mp->m[1][3];
                profile->dbg_first_w_reject_mp23 = mp->m[2][3];
                profile->dbg_first_w_reject_mp33 = mp->m[3][3];
                profile->dbg_first_w_reject_triangle_ordinal =
                    profile->triangles_transformed;
                profile->dbg_first_w_reject_corner = (uint8_t)c;
                profile->dbg_first_w_reject_stack_depth =
                    frontend->matrix_stack.depth;
            }
            return;
        }
        cx[c] = sm64_saturn_fast3d_q16_div_clamped(x, w);
        cy[c] = sm64_saturn_fast3d_q16_div_clamped(y, w);
        clip_w_q16[c] = w;
        cw[c] = w >> 16;
#else
        /* Compatibility path for host/float-wire builds. */
        const float mx = v->x, my = v->y, mz = v->z;
        const float x = mx * mp_float->x[0] + my * mp_float->x[1] +
                        mz * mp_float->x[2] + mp_float->x[3];
        const float y = mx * mp_float->y[0] + my * mp_float->y[1] +
                        mz * mp_float->y[2] + mp_float->y[3];
        const float w = mx * mp_float->w[0] + my * mp_float->w[1] +
                        mz * mp_float->w[2] + mp_float->w[3];
        if (w <= 0.0f) {
            profile->reject_near_far++;
            profile->reject_w_nonpositive++;
            if (w <= -1.0e6f) profile->reject_w_nonpositive_overflow_suspect++;
            if (profile->reject_w_nonpositive == 1U) {
                profile->dbg_first_w_reject_mx = mx;
                profile->dbg_first_w_reject_my = my;
                profile->dbg_first_w_reject_mz = mz;
                profile->dbg_first_w_reject_w = w;
                profile->dbg_first_w_reject_mp03 = mp->m[0][3];
                profile->dbg_first_w_reject_mp13 = mp->m[1][3];
                profile->dbg_first_w_reject_mp23 = mp->m[2][3];
                profile->dbg_first_w_reject_mp33 = mp->m[3][3];
                profile->dbg_first_w_reject_triangle_ordinal = profile->triangles_transformed;
                profile->dbg_first_w_reject_corner = (uint8_t)c;
                profile->dbg_first_w_reject_stack_depth = frontend->matrix_stack.depth;
            }
            return;
        }
        cx[c] = x / w;
        cy[c] = y / w;
        cw[c] = w;
#endif
    }

#ifdef SM64_SATURN_FAST3D_Q16_TRACE
#ifdef SATURN_MTX_IS_Q16
    {
        float trace_cx[3], trace_cy[3], trace_cw[3];
        for (int c = 0; c < 3; c++) {
            trace_cx[c] = sm64_saturn_q16_to_float(cx[c]);
            trace_cy[c] = sm64_saturn_q16_to_float(cy[c]);
            trace_cw[c] = sm64_saturn_q16_to_float(clip_w_q16[c]);
        }
        sm64_saturn_fast3d_q16_trace_capture(frontend, idx,
                                             trace_cx, trace_cy, trace_cw);
    }
#else
    sm64_saturn_fast3d_q16_trace_capture(frontend, idx, cx, cy, cw);
#endif
#endif

    /* Backface cull in pre-viewport, Y-up clip space -- matching both
     * castleviewer's view_triangle_facing/view_triangle_is_culled
     * (main.c:503-518, pre-projection 3D view space) and the
     * reference's gfx_sp_tri1 (gfx_pc.c:729-751, perspective-divided
     * x/w,y/w clip space). Do NOT move this after the viewport map
     * below -- that space is Y-down (see the viewport decode's Y-flip
     * in Task 7) and would invert this sign. */
    if (frontend->geometry_mode & G_CULL_BOTH) {
#ifdef SATURN_MTX_IS_Q16
        const int64_t dx1 = (int64_t)cx[0] - cx[1];
        const int64_t dy1 = (int64_t)cy[0] - cy[1];
        const int64_t dx2 = (int64_t)cx[2] - cx[1];
        const int64_t dy2 = (int64_t)cy[2] - cy[1];
        const int64_t cross = dx1 * dy2 - dy1 * dx2;
        switch (frontend->geometry_mode & G_CULL_BOTH) {
            case G_CULL_FRONT:
                if (cross <= 0) { profile->reject_backface++; return; }
                break;
            case G_CULL_BACK:
                if (cross >= 0) { profile->reject_backface++; return; }
                break;
            case G_CULL_BOTH:
                profile->reject_backface++;
                return;
        }
#else
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
#endif
    }

    /* Clamp before narrowing to int16_t -- a small positive w (camera
     * very close to geometry) makes cx/cy enormous, and this port has
     * no downstream hardware clipper to catch it (unlike the reference,
     * which relies on the host GPU's own clip stage). Raising the
     * near-plane threshold alone doesn't fully close this: a large
     * model-space x with a "safe" w still overflows int16_t. Clamping
     * converts what would be UB into a saturated, visually-wrong-but-
     * defined value -- the triangle's true visibility is still decided
     * correctly afterward by the near/far quad check below, which uses
     * the unclamped cw.
     *
     * The clamp bounds are VDP1's hardware-valid coordinate window --
     * X in [-2048, +2047], Y in [-1024, +1023] (SGL FAQ 3-4(c); see
     * docs/saturn/SGL_REFERENCE_NOTES.md) -- not int16 limits:
     * coordinates beyond that window are not merely offscreen, they can
     * WRAP drawing back onto the visible screen on real hardware. Today
     * this is defense-in-depth rather than a live bug: any triangle
     * with a post-clamp coordinate outside roughly +/-960 is already
     * rejected below by the span check (SM64_SATURN_MAX_PROJECTED_SPAN)
     * plus the offscreen visibility check, so nothing outside the VDP1
     * window can currently be emitted. Clamping to the hardware window
     * makes that guarantee local and explicit instead of an emergent
     * property of the span constant staying small -- if the span budget
     * is ever raised past the window size, this clamp is what keeps
     * emitted coordinates hardware-valid. */
    for (int c = 0; c < 3; c++) {
#ifdef SATURN_MTX_IS_Q16
        const int32_t screen_x_i =
            sm64_saturn_fast3d_q16_viewport_coord(cx[c], frontend->viewport.x,
                                                  frontend->viewport.width,
                                                  false);
        const int32_t screen_y_i =
            sm64_saturn_fast3d_q16_viewport_coord(cy[c], frontend->viewport.y,
                                                  frontend->viewport.height,
                                                  true);
        screen_x[c] = (int16_t)(screen_x_i < -2048 ? -2048 :
                                (screen_x_i > 2047 ? 2047 : screen_x_i));
        screen_y[c] = (int16_t)(screen_y_i < -1024 ? -1024 :
                                (screen_y_i > 1023 ? 1023 : screen_y_i));
#else
        const float screen_x_f = frontend->viewport.x +
            (cx[c] * 0.5f + 0.5f) * frontend->viewport.width;
        const float screen_y_f = frontend->viewport.y +
            (1.0f - (cy[c] * 0.5f + 0.5f)) * frontend->viewport.height;
        screen_x[c] = (int16_t)(screen_x_f < -2048.0f ? -2048 :
                                (screen_x_f > 2047.0f ? 2047 :
                                 screen_x_f));
        screen_y[c] = (int16_t)(screen_y_f < -1024.0f ? -1024 :
                                (screen_y_f > 1023.0f ? 1023 :
                                 screen_y_f));
#endif
    }

    sm64_saturn_projected_workarea_init(&workarea, projected_storage, 4);
    for (int c = 0; c < 3; c++) {
        (void)sm64_saturn_projected_workarea_push(
            &workarea,
            (sm64_saturn_projected_vertex_t){
                screen_x[c], screen_y[c],
#ifdef SATURN_MTX_IS_Q16
                cw[c] /* raw units -- do not scale by 65536.0f
                                 * here, see the NEAR/FAR_DEPTH comment
                                 * above this function */
#else
                (int32_t)cw[c] /* raw units -- do not scale by 65536.0f
                                 * here, see the NEAR/FAR_DEPTH comment
                                 * above this function */
#endif
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
        /* Bring-up attribution: mirror is_visible's individual
         * conditions so live counters can name which limit killed the
         * triangle (see the profile struct's diagnostics comment). The
         * conditions are re-evaluated here rather than returned from
         * is_visible to keep that shared header's contract untouched. */
        if (quad.min_z < SM64_SATURN_NEAR_DEPTH) {
            profile->reject_z_near++;
        }
        if (quad.max_z > SM64_SATURN_FAR_DEPTH) {
            profile->reject_z_far++;
        }
        if (quad.clip_and != SM64_SATURN_CLIP_NONE) {
            profile->reject_offscreen++;
        }
        if ((int32_t)quad.max_x - quad.min_x >
                SM64_SATURN_MAX_PROJECTED_SPAN ||
            (int32_t)quad.max_y - quad.min_y >
                SM64_SATURN_MAX_PROJECTED_SPAN) {
            profile->reject_span++;
        }
        if (profile->reject_near_far ==
                profile->reject_w_nonpositive + 1U) {
            /* First quad-level reject this frame: snapshot magnitudes. */
            profile->dbg_first_reject_min_z = quad.min_z;
            profile->dbg_first_reject_max_z = quad.max_z;
            profile->dbg_first_reject_min_x = quad.min_x;
            profile->dbg_first_reject_max_x = quad.max_x;
            profile->dbg_first_reject_min_y = quad.min_y;
            profile->dbg_first_reject_max_y = quad.max_y;
            profile->dbg_first_reject_clip_and = quad.clip_and;
        }
        return;
    }

    if ((int32_t)screen_x[0] == screen_x[1] &&
        (int32_t)screen_x[1] == screen_x[2] &&
        (int32_t)screen_y[0] == screen_y[1] &&
        (int32_t)screen_y[1] == screen_y[2]) {
        profile->reject_degenerate++;
        return;
    }

    /* A merge writes back into the partner's existing slot, so it consumes
     * no new capacity -- that is the entire point of the feature. Only a
     * genuinely new primitive is charged against the buffer. */
    if (!merge_into_pending &&
        frontend->resolved_count >=
            SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES) {
        profile->reject_command_capacity++;
        return;
    }

    sm64_saturn_resolved_triangle_t *out =
        merge_into_pending ? &frontend->resolved[merge_slot]
                           : &frontend->resolved[frontend->resolved_count++];
    /* Per-corner colors for VDP1 Gouraud (design spec 2026-07-24) --
     * replaces the old vertex-0-only flat pack now that Task 3 makes
     * every vertex's lit/unlit color correct, not just vertex 0's.
     *
     * Bit 15 (0x8000) is the RGB flag, not spare: VDP2's sprite-layer
     * decode treats framebuffer pixels with MSB=1 as RGB1555 and MSB=0
     * as palette data, so a color packed without it composites as a
     * (bogus) palette index -- typically invisible. Diagnosed live when
     * a byte-perfect VDP1 command table with CMDCOLR=0x7FFF plotted to
     * an all-black sprite layer; castleviewer's own neutral color
     * constant (0xC210) carries the bit, and the SGL sprite manual's
     * mixed palette/RGB framebuffer convention is recorded in
     * docs/saturn/SGL_REFERENCE_NOTES.md -- this still applies to every
     * corner entry below, not just a single flat color.
     *
     * LANE ORDER -- BLUE is the high field, RED the low one. This is not
     * the intuitive order and it was wrong here from this pack's original
     * introduction until 2026-07-24. Yaul's own `union rgb1555`
     * (third_party/libyaul/libyaul/gamemath/gamemath/color/rgb1555.h:19-33)
     * declares the bitfields `msb:1; b:5; g:5; r:5`, and SH-2 is
     * big-endian, so allocation runs MSB-first: msb=bit 15, BLUE=bits
     * 14-10, green=bits 9-5, RED=bits 4-0. Its RGB1555_INITIALIZER
     * likewise reorders its (r,g,b) arguments to (b,g,r) on the way in.
     * Packing red into bits 14-10 therefore ships every chromatic surface
     * with red and blue exchanged -- invisible on achromatic geometry
     * (r==g==b is a fixed point of the swap), which is why it survived a
     * live capture and a human visual review of a mostly-grey frame.
     * This file is deliberately Yaul-free so it stays host-testable, so
     * it cannot just call RGB1555(); the repo's other packer,
     * tools/saturn/extract_mario_textures.py:36, independently uses this
     * same (blue<<10)|(green<<5)|red order. Verify against those two
     * references, not against intuition, before touching this line. */
    for (int c = 0; c < 3; c++) {
        const sm64_saturn_fast3d_vertex_t *v = &frontend->vertices[idx[c]];
        corner_color[c] = (uint16_t)(0x8000U |
            ((v->b >> 3) << 10) | ((v->g >> 3) << 5) | (v->r >> 3));
    }
    if ((frontend->geometry_mode & G_FOG) != 0U) {
        /* Degradation contract (design spec 2026-07-24): fog is dropped
         * entirely this cycle (rendering it is fidelity-ladder item 2,
         * out of scope) -- the triangle still resolves normally, only
         * counted so the cost stays visible in captures. */
        profile->fog_dropped_triangles++;
    }

    if (merge_into_pending) {
        /* The held partner's three source corners are still sitting in
         * out->[0..2]; snapshot them before overwriting, because the
         * cycle can name them in any order. */
        int16_t px[3], py[3];
        uint16_t pc[3];

        for (int c = 0; c < 3; c++) {
            px[c] = out->x[c];
            py[c] = out->y[c];
            pc[c] = out->corner_rgb1555[c];
        }

        /* The offline compiler already walked the merged boundary and
         * wrote its four corners, in VDP1 winding order, into this entry:
         * codes 0-2 name THIS triangle's own corners in source order,
         * codes 3-5 the partner's (code - 3). Both halves of a pair carry
         * the same cycle in the same order, so decoding either one gives
         * the same quad -- there is deliberately no boundary walk here.
         * Corner codes were range-checked at lookup time. */
        for (int c = 0; c < 4; c++) {
            const uint32_t code =
                SM64_SATURN_QUAD_MAP_CORNER(entry, (uint32_t)c);

            if (code < 3U) {
                out->x[c] = screen_x[code];
                out->y[c] = screen_y[code];
                out->corner_rgb1555[c] = corner_color[code];
            } else {
                const uint32_t k = code - 3U;

                out->x[c] = px[k];
                out->y[c] = py[k];
                out->corner_rgb1555[c] = pc[k];
            }
        }

        /* RECOMPUTED over both triangles, never inherited: max_z is the
         * furthest corner, and a merged quad's furthest corner may belong
         * to either half. (Same reason the original line below refuses
         * quad.center_z -- that reads only indices[0]/[2] and drops a
         * corner's depth entirely.) */
        out->depth_bucket = sm64_saturn_fast3d_depth_bucket(
            merge_max_z > quad.max_z ? merge_max_z : quad.max_z);
        profile->quads_merged++;
        /* triangles_emitted still counts BOTH triangles: it means "source
         * triangles that survived resolve", which merging does not change.
         * The command-count saving shows up in triangles_vdp1_emitted and
         * in quads_merged, not here. */
        profile->triangles_emitted++;
        return;
    }

    for (int c = 0; c < 3; c++) {
        out->x[c] = screen_x[c];
        out->y[c] = screen_y[c];
        out->corner_rgb1555[c] = corner_color[c];
    }
    /* max_z buckets correctly for this (i0,i1,i2,i2) convention -- do
     * not switch this to quad.center_z, which reads only indices[0]/[2]
     * and would silently drop i1's depth (see design spec's "Painter
     * ordering reuse" finding). */
    out->depth_bucket = sm64_saturn_fast3d_depth_bucket(quad.max_z);
    /* Triangle: corner 3 repeats corner 2, the degenerate-quad convention
     * VDP1 has always been given here. A merged pair takes the branch
     * above instead and writes a genuine fourth corner. */
    out->x[3] = out->x[2];
    out->y[3] = out->y[2];
    out->corner_rgb1555[3] = out->corner_rgb1555[2];
    profile->triangles_emitted++;

    if (open_pair && ordinal < SM64_SATURN_FAST3D_QUAD_SLOT_CAPACITY) {
        /* Record the slot ONLY here, on the far side of every rejection
         * path, so a triangle that never reached resolved[] leaves no
         * stamp for its partner to merge into. The primitive written above
         * is already complete and correct as a degenerate quad; if the
         * partner never arrives, or arrives and is itself rejected, this
         * simply stays a triangle. The merge is an overwrite of a finished
         * primitive, never a deferral of one. */
        frontend->quad_slot[ordinal] = (uint16_t)(frontend->resolved_count - 1U);
        frontend->quad_slot_max_z[ordinal] = quad.max_z;
        frontend->quad_slot_stamp[ordinal] = frontend->quad_slot_generation;
    }
}

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
    const uint32_t w0 = command->words.w0;
    /* Gwords.w1 is uintptr_t in this port's PR/gbi.h (not the classic N64
     * u32) specifically so a real host/target pointer round-trips
     * through it without truncation, matching the existing G_DL handling
     * a few lines below in sm64_saturn_fast3d_frontend_submit. Keep the
     * full width here too -- narrowing to uint32_t before reconstructing
     * the G_MTX float pointer would corrupt the address on a 64-bit
     * host. The G_POPMTX /64 divide below works identically at this
     * width since the encoded count is always a small value. */
    const uintptr_t w1 = command->words.w1;
    const uint8_t opcode = (uint8_t)(w0 >> 24);

    switch (opcode) {
        case G_MTX: {
            /* F3DEX_GBI_2E (this build) inverts the push bit relative to
             * the raw parameter -- see gfx_pc.c:1373,
             * `gfx_sp_matrix(C0(0, 8) ^ G_MTX_PUSH, ...)`. */
            const uint8_t params =
                (uint8_t)(SM64_SATURN_C0(w0, 0, 8) ^ G_MTX_PUSH);
            /* Under the float wire format (GBI_FLOATS, the #else path
             * below -- used off-Saturn and by this file's own host
             * tests), w1 points at 16 consecutive row-major floats, NOT
             * a split s15.16 int32 array. Under SATURN_MTX_IS_Q16 (the
             * #ifdef path below), the SAME w1 pointer instead refers to
             * the identical 64-byte payload reinterpreted as 16
             * consecutive row-major s32 Q16.16 values -- what
             * rendering_graph_node.c's saturn_mtxq_write_wire and the
             * TARGET_SATURN guMtxF2L override actually write there (see
             * saturn_matrix.h's SATURN_MTX_IS_Q16 note). gbi_floats is
             * declared only in the #else arm below, not unconditionally:
             * under SATURN_MTX_IS_Q16 the wire buffer's effective type is
             * int32_t[4][4] (set by saturn_mtxq_write_wire's memcpy, C11
             * 6.5p6), so reading it through a float-typed lvalue would be
             * an access through an incompatible type -- real strict-
             * aliasing UB (C11 6.5p7), not just a style concern. The only
             * other place this file ever reads gbi_floats (the
             * dbg_root_mtx_source_m22 diagnostic further down) is guarded
             * by the same #ifndef for the same reason. */
            sm64_saturn_mtx_t decoded;

#ifdef SATURN_MTX_IS_Q16
            sm64_saturn_matrix_decode_q16((const int32_t *)(uintptr_t)w1, &decoded);
#else
            const float *gbi_floats = (const float *)(uintptr_t)w1;
            sm64_saturn_matrix_decode(gbi_floats, &decoded);
#endif

            /* Bring-up diagnostic -- see the profile struct's
             * dbg_bad_mtx_* comment. m[2][2] == INT32_MIN is the proven
             * live corruption sentinel (0xE200001C source bits). */
            if (profile->dbg_bad_mtx_ordinal == 0U &&
                decoded.m[2][2] == INT32_MIN) {
                profile->dbg_bad_mtx_w1 = (uint32_t)w1;
                profile->dbg_bad_mtx_ordinal = profile->matrix_commands;
                profile->dbg_bad_mtx_params = params;
            }

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
                bool took_mul_path = false;
                bool mul_overflowed = false;

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
                    took_mul_path = true;
                    mul_overflowed = sm64_saturn_matrix_mul(
                        &decoded,
                        sm64_saturn_matrix_stack_top(
                            &frontend->matrix_stack),
                        &composed);
                    sm64_saturn_matrix_stack_load(&frontend->matrix_stack,
                                                  &composed);
                }
                /* Bring-up diagnostic -- see the profile struct's
                 * dbg_root_mtx_* comment (saturn_fast3d_frontend.h).
                 * Overwritten (not gated to "first only") on every
                 * modelview command that lands at the un-pushed root
                 * depth, so this always reflects the most recent such
                 * write -- exactly what a later triangle at that same
                 * depth would have used as its "top". */
                if (frontend->matrix_stack.depth == 1U) {
#ifndef SATURN_MTX_IS_Q16
                    /* gbi_floats only exists in this (non-Q16) build --
                     * see the strict-aliasing note where it's declared
                     * above for why it's guarded out entirely under
                     * SATURN_MTX_IS_Q16 rather than read via a
                     * differently-typed cast. This field is also
                     * admittedly meaningless in Q16 builds regardless,
                     * since nothing downstream consumes it there. */
                    profile->dbg_root_mtx_source_m22 = gbi_floats[2 * 4 + 2];
#endif
                    profile->dbg_root_mtx_decoded_m22 = decoded.m[2][2];
                    profile->dbg_root_mtx_post_top_m22 =
                        frontend->matrix_stack.entries[0].m[2][2];
                    profile->dbg_root_mtx_params = params;
                    profile->dbg_root_mtx_took_mul_path =
                        took_mul_path ? 1U : 0U;
                    profile->dbg_root_mtx_mul_overflowed =
                        mul_overflowed ? 1U : 0U;
                    profile->dbg_root_mtx_command_ordinal =
                        profile->matrix_commands;
                }
                /* Mirrors gfx_pc.c:590: every modelview (non-
                 * projection) G_MTX command -- push, pop-adjacent
                 * load, or multiply alike -- unconditionally marks the
                 * light direction stale. Task 3's lit G_VTX path
                 * re-transforms lazily against the matrix_stack top
                 * (by then fully composed for this command) before
                 * its next use. */
                frontend->lights.lights_changed = true;
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
            /* Deliberate, documented deviation from gfx_pc.c: its own
             * gfx_sp_pop_matrix (gfx_pc.c:595-604) never sets
             * lights_changed. That's only safe there because a pop is
             * always followed by more modelview commands before any
             * lit vertex uses the popped-to matrix in practice; this
             * port marks the light direction stale here too, since a
             * pop genuinely does change the matrix_stack top, and a
             * stale cached coeff would otherwise be silently wrong if
             * a lit G_VTX ever ran immediately after a pop with no
             * intervening G_MTX. Recompute is idempotent and lazy
             * (Task 3), so this only costs a possible extra
             * recompute, never a wrong answer. */
            frontend->lights.lights_changed = true;
            break;
        }
        case G_MOVEMEM: {
            const uint8_t index = (uint8_t)SM64_SATURN_C0(w0, 0, 8);
            if (index == G_MV_VIEWPORT) {
                const Vp_t *vp = (const Vp_t *)w1;
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
            } else if (index == G_MV_LIGHT) {
                /* gSPLight wire: w0 bits 8-15 = byte-offset/8 (gDma2p,
                 * gbi.h:1801-1807, matching the C0(8,8)*8 recovery
                 * gfx_pc.c:1387 already performs above for `index`
                 * itself); lightidx = offset/24 - 2 (gfx_pc.c:971).
                 *
                 * SLOT ASSIGNMENT -- the ambient's wire slot is
                 * num_lights-1, read from the display list, NOT a fixed
                 * index. gfx_pc.c stores every light into
                 * current_lights[lightidx] and then reads the ambient
                 * back from current_lights[current_num_lights - 1]
                 * (:638-640) with directionals at i < num-1 (:628,
                 * :642). So under gSPSetLights1 the ambient is at idx 1,
                 * but under gSPSetLights2 it is at idx 2 and idx 1 is a
                 * SECOND DIRECTIONAL. An earlier version of this decode
                 * hardcoded "ambient == idx 1", justified by a comment
                 * arguing that this frontend clamps num_lights to 2 --
                 * a non-sequitur, since this port's own clamp has no
                 * influence whatsoever on which slot the display list
                 * puts the ambient in. The effect was that a
                 * >1-directional list silently wrote directional #2's
                 * color over amb_col and dropped the real ambient
                 * entirely -- strictly more degradation than the design
                 * spec's contract row promises ("first directional
                 * used"). Ambient is tested FIRST below so that the
                 * degenerate num_lights==1 case (ambient at idx 0, no
                 * directionals) also resolves the way gfx_pc.c would.
                 *
                 * Not reachable in any binary shipped today -- the only
                 * >1-directional emitter in the tree is
                 * src/goddard/renderer.c, reached solely via
                 * levels/intro/geo.c, which no Saturn target compiles --
                 * but it becomes live the moment the intro/menu path is
                 * enabled, which is planned work.
                 *
                 * Every other lightidx is deliberately ignored,
                 * matching gfx_pc.c:972's own
                 * `lightidx >= 0 && lightidx <= MAX_LIGHTS` bounds
                 * check (its comment: "skip lookat"). This is not a
                 * theoretical guard: src/game/rendering_graph_node.c
                 * unconditionally emits gSPLookAt every frame under
                 * F3DEX_GBI_2 (this build's dialect, gbi.h:90-92;
                 * confirmed live call sites at rendering_graph_node.c
                 * :269 and :1172), which itself expands to two
                 * G_MOVEMEM/G_MV_LIGHT commands at G_MVO_LOOKATX=0 and
                 * G_MVO_LOOKATY=24 (gbi.h:1259-1260) -- lightidx -2
                 * and -1. An earlier draft of this case used an
                 * unconditional `else` for the ambient branch, which
                 * would let that same-frame gSPLookAt silently
                 * overwrite amb_col with lookat direction bytes
                 * reinterpreted as a color; found by reading
                 * rendering_graph_node.c to verify this decode against
                 * real call sites, not by either given test (both only
                 * ever send lightidx 0/1). Guarding the memcpy itself
                 * below (not just its consumption) also means an
                 * out-of-range lightidx never dereferences w1 at all. */
                const uint32_t offset = SM64_SATURN_C0(w0, 8, 8) * 8U;
                const int32_t lightidx = (int32_t)(offset / 24U) - 2;

                const int32_t ambient_idx =
                    (int32_t)frontend->lights.num_lights - 1;

                if (lightidx == ambient_idx || lightidx == 0) {
                    /* Copy the 12-byte Light_t payload via memcpy
                     * (strict-aliasing-safe, matching this file's
                     * established discipline elsewhere); for the
                     * ambient this reads 4 bytes past Ambient_t's own
                     * 8 -- same as gfx_pc.c:974's documented behavior
                     * ("NOTE: reads out of bounds if it is an ambient
                     * light"), safe here because SM64 ambients live
                     * inside a Lights1 with the directional Light
                     * contiguous immediately after them
                     * (gbi.h:1438-1441 -- confirmed: Ambient is 8
                     * bytes, Light starts right after at struct
                     * offset 8). Only col[0..2] (Light_t/Ambient_t
                     * both put col at byte offset 0-2) is ever read
                     * from the ambient slot, so the extra bytes
                     * spilling into the neighboring Light are never
                     * used. Light_t layout confirmed against
                     * include/PR/gbi.h:1398-1405: col[3],pad1,
                     * colc[3],pad2,dir[3](signed),pad3 -- dir starts
                     * at byte offset 8 (3+1+3+1). */
                    uint8_t raw[12];
                    (void)memcpy(raw, (const void *)w1, sizeof(raw));
                    if (lightidx == ambient_idx) {
                        frontend->lights.amb_col[0] = raw[0];
                        frontend->lights.amb_col[1] = raw[1];
                        frontend->lights.amb_col[2] = raw[2];
                    } else {
                        frontend->lights.dir_col[0] = raw[0];
                        frontend->lights.dir_col[1] = raw[1];
                        frontend->lights.dir_col[2] = raw[2];
                        frontend->lights.dir_dir[0] = (int8_t)raw[8];
                        frontend->lights.dir_dir[1] = (int8_t)raw[9];
                        frontend->lights.dir_dir[2] = (int8_t)raw[10];
                    }
                    /* Second deliberate deviation from the anchor (the
                     * first is G_POPMTX above, documented there):
                     * gfx_pc.c's own G_MV_LIGHT case (:970-977) only
                     * memcpys into current_lights[] and never touches
                     * lights_changed -- it recomputes the light
                     * direction on matrix changes alone. That is unsafe
                     * here for the same reason the G_POPMTX deviation
                     * exists: a display list may change the light
                     * itself between two lit vertex batches with no
                     * intervening matrix command, which would leave
                     * coeff_q16 cached from the PREVIOUS light's
                     * direction. Real in-tree instance:
                     * actors/koopa/model.inc.c:2080-2081 emits a
                     * gSPLight pair mid-list between lit batches.
                     * Recompute is lazy and idempotent (Task 3), so the
                     * cost is at most one extra recompute, never a
                     * wrong color. */
                    frontend->lights.lights_changed = true;
                }
            }
            break;
        }
        case G_MOVEWORD: {
            /* gfx_pc.c dispatch (F3DEX_GBI_2, this build's dialect --
             * gbi.h:90-92 makes F3DEX_GBI_2E imply F3DEX_GBI_2):
             * gfx_sp_moveword(C0(16,8), C0(0,16), w1) (gfx_pc.c:1394);
             * index=C0(16,8), data=w1 (offset=C0(0,16) is read by the
             * dispatch but unused by the G_MW_NUMLIGHT handler itself,
             * gfx_pc.c:989-1000). G_MW_NUMLIGHT (gfx_pc.c:991-993):
             * num = data/24 + 1 (includes the ambient; NUML(n)=n*24,
             * gbi.h:2516). Degradation contract: exactly 1 directional
             * is EVALUATED; anything else is counted here and the extra
             * directionals are ignored at decode, matching this
             * frontend's fixed dir_col/dir_dir/amb_col shape (no
             * light array, unlike gfx_pc.c's current_lights[]).
             *
             * The raw count is stored unclamped. It is not cosmetic
             * bookkeeping: the G_MV_LIGHT case above needs it to locate
             * the ambient, whose wire slot is num_lights-1
             * (gfx_pc.c:638-640). Clamping it here -- as this code
             * originally did -- is what made a >1-directional list write
             * directional #2 over the ambient. */
            const uint8_t mw_index = (uint8_t)SM64_SATURN_C0(w0, 16, 8);

            if (mw_index == G_MW_NUMLIGHT) {
                const uint8_t num = (uint8_t)(w1 / 24U + 1U);

                if (num != 2U) {
                    profile->unsupported_num_lights++;
                }
                frontend->lights.num_lights = num;
                frontend->lights.lights_changed = true;
            }
            break;
        }
        case G_VTX: {
            /* F3DEX_GBI_2 dispatch formula (gfx_pc.c:1408):
             * n_vertices = C0(12,8), dest_index = C0(1,7) - n_vertices.
             * w1 is a raw pointer (this port's display lists reference
             * final-linked addresses, not N64 segments -- consistent
             * with the existing G_DL handling in this file), already
             * uintptr_t (no narrowing to guard against). */
            const uint32_t n_vertices = SM64_SATURN_C0(w0, 12, 8);
            const uint32_t end_index = SM64_SATURN_C0(w0, 1, 7);
            /* Vtx_t and Vtx_tn share byte layout (gbi.h:1112-1136): both
             * lead with ob[3]/flag/tc[2], then 4 trailing bytes -- this
             * cast is valid either way. Which MEANING those trailing
             * bytes carry is decided per-vertex below by the `lit` gate,
             * not fixed by this cast: read as RGBA (Vtx_t.cn) when
             * unlit, or as a packed s8 normal + alpha (Vtx_tn.n[3]/.a)
             * under G_LIGHTING -- SM64 runs with G_LIGHTING on globally
             * (src/game/game_init.c:131), so the lit path is the common
             * case in real play, not a rare one. */
            const Vtx_t *src = (const Vtx_t *)w1;
            const bool lit = (frontend->geometry_mode & G_LIGHTING) != 0U;
            uint32_t dest_index;

            /* Guard against underflow: a malformed w0 encoding
             * n_vertices > end_index would otherwise wrap dest_index to
             * a huge uint32_t value, and the per-vertex bounds check
             * below could be satisfied again partway through the loop
             * once dest_index + i wraps back around -- silently
             * corrupting low vertex slots with an out-of-bounds read of
             * src[]. Reject the whole command up front instead. */
            if (n_vertices > end_index) {
                profile->reject_vertex_range++;
                break;
            }
            dest_index = end_index - n_vertices;

            if (lit && frontend->lights.lights_changed) {
                /* Lazy, once per change (gfx_pc.c:627-635): light
                 * DIRECTIONS re-transform against the current modelview
                 * top; vertex normals stay raw and are evaluated fresh
                 * per vertex below. Placed after the underflow guard
                 * above (not at the very top of the case) so a
                 * malformed/rejected command doesn't spend a recompute
                 * for vertices that will never be processed. */
                sm64_saturn_light_recompute_coeffs(
                    &frontend->lights,
                    sm64_saturn_matrix_stack_top(&frontend->matrix_stack));
            }

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
                if (lit) {
                    /* Under G_LIGHTING the trailing Vtx bytes are a
                     * packed s8 normal (Vtx_tn.n, gbi.h:1134), NOT a
                     * color -- reading them as RGB unconditionally was
                     * the pre-Gouraud bug this branch fixes. The
                     * uint8->int8 casts rely on GCC's two's-complement
                     * conversion (this project is GCC-only, both host
                     * and SH-2 targets). */
                    const int8_t n[3] = { (int8_t)src[i].cn[0],
                                          (int8_t)src[i].cn[1],
                                          (int8_t)src[i].cn[2] };
                    uint8_t rgb[3];
                    sm64_saturn_light_eval_vertex(&frontend->lights, n,
                                                  rgb);
                    frontend->vertices[dest].r = rgb[0];
                    frontend->vertices[dest].g = rgb[1];
                    frontend->vertices[dest].b = rgb[2];
                    frontend->vertices[dest].a = src[i].cn[3];
                    profile->lit_vertices++;
                } else {
                    frontend->vertices[dest].r = src[i].cn[0];
                    frontend->vertices[dest].g = src[i].cn[1];
                    frontend->vertices[dest].b = src[i].cn[2];
                    frontend->vertices[dest].a = src[i].cn[3];
                    profile->unlit_vertices++;
                }
            }
            break;
        }
        case G_GEOMETRYMODE: {
            /* gfx_pc.c:1428 dispatches gfx_sp_geometry_mode(~C0(0,24),
             * w1); the function body then does `geometry_mode &=
             * ~clear` (gfx_pc.c:932-935). That's ~(~C0(0,24)) =
             * C0(0,24) net -- the raw wire bits in w0's low 24 bits
             * are themselves the correct AND-mask to keep, no extra
             * negation needed. (An earlier draft of this case applied
             * one anyway, inverting the polarity -- invisible only
             * because geometry_mode starts at 0, where AND with
             * anything is still 0; it breaks on the very next
             * G_GEOMETRYMODE command once state is nonzero.) */
            frontend->geometry_mode =
                (frontend->geometry_mode & SM64_SATURN_C0(w0, 0, 24)) | w1;
            break;
        }
        case (uint8_t)G_TRI1: {
            /* F3DEX_GBI_2 body (gfx_pc.c:1440):
             * gfx_sp_tri1(C0(16,8)/2, C0(8,8)/2, C0(0,8)/2). Indices are
             * vertex-buffer offsets *2 (see this task's design-spec
             * note on __gsSP1Triangle_w1's doubled encoding). */
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
        default:
            break;
    }
}

void sm64_saturn_fast3d_frontend_init(
    sm64_saturn_fast3d_frontend_t *frontend)
{
    if (frontend != NULL) {
        (void)memset(frontend, 0, sizeof(*frontend));
        sm64_saturn_matrix_stack_init(&frontend->matrix_stack);
        sm64_saturn_light_state_init(&frontend->lights);
#ifdef SM64_SATURN_FAST3D_Q16_TRACE
        frontend->q16_trace.magic = SM64_SATURN_FAST3D_Q16_TRACE_MAGIC;
        frontend->q16_trace.version = 1U;
#endif
    }
}

void sm64_saturn_fast3d_frontend_submit(struct SPTask *task, void *context)
{
    sm64_saturn_fast3d_frontend_t *frontend = context;
    sm64_saturn_fast3d_profile_t *profile;
    Gfx *command;
    Gfx *return_stack[SM64_SATURN_FAST3D_MAX_CALL_DEPTH];
    /* Saved alongside return_stack, pushed and popped by the same G_DL /
     * G_ENDDL handling: the quad map is keyed by ordinal WITHIN a display
     * list, so a called list gets its own row and its own counter, and the
     * caller's must come back exactly as it was. */
    const uint32_t *quad_entries_stack[SM64_SATURN_FAST3D_MAX_CALL_DEPTH];
    uint16_t quad_count_stack[SM64_SATURN_FAST3D_MAX_CALL_DEPTH];
    uint16_t quad_ordinal_stack[SM64_SATURN_FAST3D_MAX_CALL_DEPTH];
    uint16_t quad_generation_stack[SM64_SATURN_FAST3D_MAX_CALL_DEPTH];
    uint16_t stack_depth = 0U;
    uint32_t frame_serial;

    if (frontend == NULL)
        return;

    profile = &frontend->profile;
    frame_serial = profile->frame_serial + 1U;
    /* Task 0 timing is sampled by sourceboot after the submit callback and
     * must survive the next callback's normal profile clear.  Keep the last
     * completed phase values and the accumulated sim total across that clear
     * so a capture stopped at a frame boundary still has valid timings. */
    const uint32_t sim_frt_ticks_last = profile->sim_frt_ticks_last;
    const uint32_t sim_frt_ticks_accum = profile->sim_frt_ticks_accum;
    const uint32_t sim_tick_count = profile->sim_tick_count;
    const uint32_t render_frt_ticks_last = profile->render_frt_ticks_last;
    const uint32_t demo_actor_snapshot_valid =
        profile->demo_actor_snapshot_valid;
    const uint32_t demo_actor_pose_vertices = profile->demo_actor_pose_vertices;
    const uint32_t demo_actor_vertices_valid = profile->demo_actor_vertices_valid;
    const uint32_t demo_actor_primitives_emitted =
        profile->demo_actor_primitives_emitted;
    const uint32_t slave_jobs_completed = profile->slave_jobs_completed;
    const uint32_t slave_busy_ticks = profile->slave_busy_ticks;
    const uint32_t master_wait_ticks = profile->master_wait_ticks;
    const uint32_t slave_timeouts = profile->slave_timeouts;
    const uint32_t render_frt_ticks_accum = profile->render_frt_ticks_accum;
    const uint32_t vdp1_commands_last = profile->vdp1_commands_last;
    const uint32_t vdp1_vram_bytes = profile->vdp1_vram_bytes;
    const uint32_t vdp2_display_mask = profile->vdp2_display_mask;
    const uint32_t vdp2_vram_bytes = profile->vdp2_vram_bytes;
    (void)memset(profile, 0, sizeof(*profile));
    profile->frame_serial = frame_serial;
    profile->sim_frt_ticks_last = sim_frt_ticks_last;
    profile->sim_frt_ticks_accum = sim_frt_ticks_accum;
    profile->sim_tick_count = sim_tick_count;
    profile->render_frt_ticks_last = render_frt_ticks_last;
    profile->demo_actor_snapshot_valid = demo_actor_snapshot_valid;
    profile->demo_actor_pose_vertices = demo_actor_pose_vertices;
    profile->demo_actor_vertices_valid = demo_actor_vertices_valid;
    profile->demo_actor_primitives_emitted = demo_actor_primitives_emitted;
    profile->slave_jobs_completed = slave_jobs_completed;
    profile->slave_busy_ticks = slave_busy_ticks;
    profile->master_wait_ticks = master_wait_ticks;
    profile->slave_timeouts = slave_timeouts;
    profile->render_frt_ticks_accum = render_frt_ticks_accum;
    profile->vdp1_commands_last = vdp1_commands_last;
    profile->vdp1_vram_bytes = vdp1_vram_bytes;
    profile->vdp2_display_mask = vdp2_display_mask;
    profile->vdp2_vram_bytes = vdp2_vram_bytes;
    /* resolved[]/resolved_count are per-frame output, not persistent
     * state -- without this reset they accumulate across calls until
     * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES is reached, after which
     * every subsequent frame permanently rejects all triangles via
     * reject_command_capacity (caught by code review of Task 11, before
     * any real per-frame caller existed to make the symptom visible). */
    frontend->resolved_count = 0U;
    /* KNOWN LIMITATION (flagged by this branch's final whole-implementation
     * review, not fixed): unlike resolved_count above, matrix_stack.depth/
     * .overflowed are NOT reset here. Real SM64 display lists always
     * balance every G_MTX_PUSH with a G_POPMTX, so depth is naturally back
     * at its floor by the end of a well-formed list and this is a non-issue
     * in normal operation -- but if a list ever faults mid-processing or
     * hits SM64_SATURN_FAST3D_MAX_COMMANDS before reaching its own pops, an
     * elevated depth would silently carry into the next frame, eating into
     * its push budget and potentially accumulating toward a permanent
     * sm64_saturn_matrix_stack_t.overflowed across many such frames.
     *
     * Not fixed here because a naive per-frame reset of depth/overflowed
     * (mirroring resolved_count's fix) is incompatible with this test
     * file's own established convention: several tests here (e.g.
     * test_frontend_g_popmtx_scales_by_64, test_frontend_g_mtx_high_water_mark)
     * call sm64_saturn_matrix_stack_push()/set_projection() directly to seed
     * matrix_stack state BEFORE a single submit() call, as a convenience
     * standing in for "some prior command already ran" -- an unconditional
     * reset at the top of submit() would silently wipe that seeded state
     * before the test's own display list ever decodes, breaking those
     * tests. A real fix needs those tests restructured to seed state via
     * actual G_MTX display-list commands instead of direct API calls, which
     * is a larger, separately-scoped change beyond this review pass. */

    if (task == NULL) {
        profile->fault_flags = SM64_SATURN_FAST3D_FAULT_NULL_TASK;
        return;
    }

    command = (Gfx *)task->task.t.data_ptr;
    if (command == NULL) {
        profile->fault_flags = SM64_SATURN_FAST3D_FAULT_NULL_DISPLAY_LIST;
        return;
    }
    sm64_saturn_fast3d_quad_map_bind(frontend, command);

    while (profile->command_count < SM64_SATURN_FAST3D_MAX_COMMANDS) {
        const uint8_t opcode = (uint8_t)(command->words.w0 >> 24);

        profile->command_count++;
        sm64_saturn_fast3d_count_command(profile, opcode);
        sm64_saturn_fast3d_decode_command(frontend, command);

        if (opcode == G_DL) {
            Gfx *target = (Gfx *)(uintptr_t)command->words.w1;
            const bool no_push = (command->words.w0 & 0x00010000U) != 0U;

            if (target == NULL) {
                profile->fault_flags |=
                    SM64_SATURN_FAST3D_FAULT_NULL_DISPLAY_LIST;
                return;
            }
            if (no_push) {
                profile->display_list_branches++;
                command = target;
                /* A branch REPLACES the current list rather than nesting
                 * into it, so nothing is saved -- but the ordinal must
                 * still restart, or every triangle past the branch would
                 * be looked up under the wrong key. */
                sm64_saturn_fast3d_quad_map_bind(frontend, target);
                continue;
            }
            if (stack_depth == SM64_SATURN_FAST3D_MAX_CALL_DEPTH) {
                profile->fault_flags |= SM64_SATURN_FAST3D_FAULT_CALL_DEPTH;
                return;
            }
            quad_entries_stack[stack_depth] = frontend->quad_entries;
            quad_count_stack[stack_depth] = frontend->quad_entry_count;
            quad_ordinal_stack[stack_depth] = frontend->triangle_ordinal;
            quad_generation_stack[stack_depth] = frontend->quad_slot_generation;
            return_stack[stack_depth++] = command + 1;
            if (stack_depth > profile->max_call_depth)
                profile->max_call_depth = stack_depth;
            profile->display_list_calls++;
            command = target;
            sm64_saturn_fast3d_quad_map_bind(frontend, target);
            continue;
        }

        if (opcode == (uint8_t)G_ENDDL) {
            if (stack_depth == 0U)
                return;
            command = return_stack[--stack_depth];
            frontend->quad_entries = quad_entries_stack[stack_depth];
            frontend->quad_entry_count = quad_count_stack[stack_depth];
            frontend->triangle_ordinal = quad_ordinal_stack[stack_depth];
            /* Restoring the generation revives the caller's own recorded
             * slots. Any ordinal the nested list happened to overwrite now
             * carries the CHILD's generation, so it reads as retired and
             * declines -- the caller loses that one merge rather than
             * inheriting a slot index that belongs to the child. */
            frontend->quad_slot_generation = quad_generation_stack[stack_depth];
            continue;
        }

        command++;
    }

    profile->fault_flags |= SM64_SATURN_FAST3D_FAULT_COMMAND_LIMIT;
}
