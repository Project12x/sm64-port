#include <string.h>
#include <stdbool.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "types.h"

#include "saturn_fast3d_frontend.h"

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
