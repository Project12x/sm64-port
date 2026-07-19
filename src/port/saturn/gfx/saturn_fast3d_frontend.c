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
        default:
            if (opcode >= G_NOOP)
                profile->rdp_commands++;
            else
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
