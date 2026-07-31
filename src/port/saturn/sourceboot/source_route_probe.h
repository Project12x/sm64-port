#ifndef SM64_SATURN_SOURCE_ROUTE_PROBE_H
#define SM64_SATURN_SOURCE_ROUTE_PROBE_H

#include <stdint.h>

#define SM64_SATURN_SOURCE_ROUTE_PROBE_MAGIC 0x53425234U /* "SBR4" */
#define SM64_SATURN_SOURCE_ROUTE_PROBE_VERSION 5U

typedef struct sm64_saturn_camera_timing {
    uint32_t ticks_last;
    uint32_t ticks_accum;
    uint32_t invocations;
    uint16_t ticks_max;
} sm64_saturn_camera_timing_t;

extern sm64_saturn_camera_timing_t sm64_saturn_camera_timing;

static inline void sm64_saturn_camera_timing_reset(void)
{
    sm64_saturn_camera_timing.ticks_last = 0;
    sm64_saturn_camera_timing.ticks_accum = 0;
    sm64_saturn_camera_timing.invocations = 0;
    sm64_saturn_camera_timing.ticks_max = 0;
}

static inline void sm64_saturn_camera_timing_record(uint16_t start, uint16_t end)
{
    const uint16_t elapsed = (uint16_t)(end - start);
    sm64_saturn_camera_timing.ticks_last = elapsed;
    sm64_saturn_camera_timing.ticks_accum += elapsed;
    sm64_saturn_camera_timing.invocations++;
    if (elapsed > sm64_saturn_camera_timing.ticks_max) {
        sm64_saturn_camera_timing.ticks_max = elapsed;
    }
}

typedef struct sm64_saturn_source_route_probe {
    uint32_t magic;
    uint32_t version;
    uint32_t atan2_variant;
    uint32_t replay_ticks;
    uint32_t global_timer;
    uint32_t mario_action;
    uint32_t mario_pos_x_bits;
    uint32_t mario_pos_y_bits;
    uint32_t mario_pos_z_bits;
    uint32_t mario_face_angle_x;
    uint32_t mario_face_angle_y;
    uint32_t mario_face_angle_z;
    uint32_t camera_pos_x_bits;
    uint32_t camera_pos_y_bits;
    uint32_t camera_pos_z_bits;
    uint32_t camera_mode;
    uint32_t triangles_transformed;
    uint32_t triangles_emitted;
    uint32_t triangles_vdp1_emitted;
    uint32_t reject_near_far;
    uint32_t reject_backface;
    uint32_t reject_degenerate;
    uint32_t reject_vertex_range;
    uint32_t reject_command_capacity;
    uint32_t reject_vdp1_arena_capacity;
    uint32_t reject_w_nonpositive;
    uint32_t reject_z_near;
    uint32_t reject_z_far;
    uint32_t reject_offscreen;
    uint32_t reject_span;
    uint32_t reject_w_nonpositive_overflow_suspect;
    uint32_t fault_flags;
    uint32_t frame_serial;
    uint32_t sim_frt_ticks_accum;
    uint32_t render_frt_ticks_accum;
    uint32_t render_frt_ticks_last;
    uint32_t master_wait_ticks;
    uint32_t slave_busy_ticks;
    uint32_t slave_jobs_completed;
    uint32_t slave_timeouts;
    uint32_t camera_ticks_last;
    uint32_t camera_ticks_accum;
    uint32_t camera_invocations;
    uint32_t camera_ticks_max;
} sm64_saturn_source_route_probe_t;

#endif
