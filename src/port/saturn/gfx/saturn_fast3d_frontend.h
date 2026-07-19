#ifndef SM64_SATURN_FAST3D_FRONTEND_H
#define SM64_SATURN_FAST3D_FRONTEND_H

#include <stdint.h>

#include "port/saturn/runtime/saturn_source_runtime.h"

/*
 * Bounded source-display-list intake for the Saturn renderer.
 *
 * This is deliberately a front end, not a Castle or Mario renderer.  It
 * consumes the SPTask produced by original game code, follows the Fast3D
 * display-list control flow, and preserves a compact source-workload profile
 * for the later IR resolver / VDP1 lowering stage.  Its command ABI is
 * verified against this tree's `src/pc/gfx/gfx_pc.c`, but this implementation
 * is new target code and does not import the PC renderer.
 */
#define SM64_SATURN_FAST3D_MAX_CALL_DEPTH 32U
#define SM64_SATURN_FAST3D_MAX_COMMANDS 16384U

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
} sm64_saturn_fast3d_profile_t;

typedef struct sm64_saturn_fast3d_frontend {
    sm64_saturn_fast3d_profile_t profile;
} sm64_saturn_fast3d_frontend_t;

void sm64_saturn_fast3d_frontend_init(
    sm64_saturn_fast3d_frontend_t *frontend);
void sm64_saturn_fast3d_frontend_submit(struct SPTask *task, void *context);

#endif
