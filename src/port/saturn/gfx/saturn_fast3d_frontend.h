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

/* HWRAM budget note: this struct is ~5,036 bytes (measured via sizeof against
 * the real F3DEX_GBI_2E build flags), grown from 44 bytes by this task's
 * addition of matrix_stack/vertices[]/resolved[]. The design spec measured
 * ~9,628 bytes of free HWRAM for the sourceboot target before this task
 * landed (docs/superpowers/specs/2026-07-20-fast3d-matrix-stack-design.md),
 * leaving roughly 4,600 bytes remaining after this struct -- comfortable
 * margin for the rest of this plan's additions (Task 10's VDP1 command list
 * goes to LWRAM, not HWRAM, so it doesn't compete with this budget). If a
 * future change needs more headroom, SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES
 * (16 bytes/entry) and SM64_SATURN_FAST3D_MAX_VERTICES (16 bytes/entry) are
 * the two knobs to shrink first -- see Task 14's build-verification step for
 * the actual link-time check. */
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
