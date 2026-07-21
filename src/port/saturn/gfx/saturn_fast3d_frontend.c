#include <string.h>
#include <stdbool.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "types.h"

#include "saturn_fast3d_frontend.h"
#include "saturn_projected_workarea.h"

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

    /* Clamp before narrowing to int16_t -- a small positive w (camera
     * very close to geometry) makes cx/cy enormous, and this port has
     * no downstream hardware clipper to catch it (unlike the reference,
     * which relies on the host GPU's own clip stage). Raising the
     * near-plane threshold alone doesn't fully close this: a large
     * model-space x with a "safe" w still overflows int16_t. Clamping
     * converts what would be UB into a saturated, visually-wrong-but-
     * defined value -- the triangle's true visibility is still decided
     * correctly afterward by the near/far quad check below, which uses
     * the unclamped cw. */
    for (int c = 0; c < 3; c++) {
        const float screen_x_f = frontend->viewport.x +
            (cx[c] * 0.5f + 0.5f) * frontend->viewport.width;
        const float screen_y_f = frontend->viewport.y +
            (1.0f - (cy[c] * 0.5f + 0.5f)) * frontend->viewport.height;
        screen_x[c] = (int16_t)(screen_x_f < (float)INT16_MIN ? INT16_MIN :
                                (screen_x_f > (float)INT16_MAX ? INT16_MAX :
                                 screen_x_f));
        screen_y[c] = (int16_t)(screen_y_f < (float)INT16_MIN ? INT16_MIN :
                                (screen_y_f > (float)INT16_MAX ? INT16_MAX :
                                 screen_y_f));
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
            /* w1 points at 16 consecutive row-major floats under this
             * build's GBI_FLOATS configuration -- NOT a split s15.16
             * int32 array. */
            const float *gbi_floats = (const float *)(uintptr_t)w1;
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
            const Vtx_t *src =
                (const Vtx_t *)w1; /* Vtx_t layout, not Vtx_tn */
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
    }
}

void sm64_saturn_fast3d_frontend_submit(struct SPTask *task, void *context)
{
    sm64_saturn_fast3d_frontend_t *frontend = context;
    sm64_saturn_fast3d_profile_t *profile;
    Gfx *command;
    Gfx *return_stack[SM64_SATURN_FAST3D_MAX_CALL_DEPTH];
    uint16_t stack_depth = 0U;
    uint32_t frame_serial;

    if (frontend == NULL)
        return;

    profile = &frontend->profile;
    frame_serial = profile->frame_serial + 1U;
    (void)memset(profile, 0, sizeof(*profile));
    profile->frame_serial = frame_serial;
    /* resolved[]/resolved_count are per-frame output, not persistent
     * state -- without this reset they accumulate across calls until
     * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES is reached, after which
     * every subsequent frame permanently rejects all triangles via
     * reject_command_capacity (caught by code review of Task 11, before
     * any real per-frame caller existed to make the symptom visible). */
    frontend->resolved_count = 0U;

    if (task == NULL) {
        profile->fault_flags = SM64_SATURN_FAST3D_FAULT_NULL_TASK;
        return;
    }

    command = (Gfx *)task->task.t.data_ptr;
    if (command == NULL) {
        profile->fault_flags = SM64_SATURN_FAST3D_FAULT_NULL_DISPLAY_LIST;
        return;
    }

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
                continue;
            }
            if (stack_depth == SM64_SATURN_FAST3D_MAX_CALL_DEPTH) {
                profile->fault_flags |= SM64_SATURN_FAST3D_FAULT_CALL_DEPTH;
                return;
            }
            return_stack[stack_depth++] = command + 1;
            if (stack_depth > profile->max_call_depth)
                profile->max_call_depth = stack_depth;
            profile->display_list_calls++;
            command = target;
            continue;
        }

        if (opcode == (uint8_t)G_ENDDL) {
            if (stack_depth == 0U)
                return;
            command = return_stack[--stack_depth];
            continue;
        }

        command++;
    }

    profile->fault_flags |= SM64_SATURN_FAST3D_FAULT_COMMAND_LIMIT;
}
