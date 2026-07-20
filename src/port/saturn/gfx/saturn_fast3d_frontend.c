#include <string.h>
#include <stdbool.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "types.h"

#include "saturn_fast3d_frontend.h"

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

void sm64_saturn_fast3d_frontend_init(
    sm64_saturn_fast3d_frontend_t *frontend)
{
    if (frontend != NULL)
        (void)memset(frontend, 0, sizeof(*frontend));
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
