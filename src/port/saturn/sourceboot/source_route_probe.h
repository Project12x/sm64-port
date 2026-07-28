#ifndef SM64_SATURN_SOURCE_ROUTE_PROBE_H
#define SM64_SATURN_SOURCE_ROUTE_PROBE_H

#include <stdint.h>

#define SM64_SATURN_SOURCE_ROUTE_PROBE_MAGIC 0x53425232U /* "SBR2" */
#define SM64_SATURN_SOURCE_ROUTE_PROBE_VERSION 2U

typedef struct sm64_saturn_source_route_probe {
    uint32_t magic;
    uint32_t version;
    uint32_t replay_ticks;
    uint32_t global_timer;
    uint32_t mario_action;
    uint32_t mario_pos_x_bits;
    uint32_t mario_pos_y_bits;
    uint32_t mario_pos_z_bits;
    uint32_t camera_mode;
    uint32_t triangles_transformed;
    uint32_t triangles_vdp1_emitted;
    uint32_t fault_flags;
    uint32_t command_capacity_rejects;
    uint32_t frame_serial;
    uint32_t sim_frt_ticks_accum;
    uint32_t render_frt_ticks_accum;
    uint32_t render_frt_ticks_last;
    uint32_t master_wait_ticks;
    uint32_t slave_busy_ticks;
    uint32_t slave_jobs_completed;
    uint32_t slave_timeouts;
} sm64_saturn_source_route_probe_t;

#endif
