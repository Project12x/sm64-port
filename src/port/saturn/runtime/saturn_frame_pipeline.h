#ifndef SM64_SATURN_FRAME_PIPELINE_H
#define SM64_SATURN_FRAME_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum sm64_saturn_frame_action {
    SM64_SATURN_FRAME_RUN_SIM_TICK,
    SM64_SATURN_FRAME_SERVICE_RENDER_JOBS,
    SM64_SATURN_FRAME_POLL_TRANSFERS,
    SM64_SATURN_FRAME_PUBLISH_FRAME,
    SM64_SATURN_FRAME_WAIT_VBLANK,
    SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME
} sm64_saturn_frame_action_t;

typedef struct sm64_saturn_frame_pipeline {
    uint32_t last_vblank_count;
    uint32_t displayed_generation;
    uint32_t simulation_generation;
    uint32_t render_generation;
    uint32_t render_completed_generation;
    uint32_t transfer_generation;
    uint32_t transfer_completed_generation;
    uint32_t queued_snapshot_generation;
    uint32_t action_generation;
    uint32_t render_service_vblank;
    uint32_t transfer_poll_vblank;
    /* Whole 30 Hz simulation-tick credits discarded after the normal plus
     * recovery budget is exhausted. This counter is not measured in fields. */
    uint32_t dropped_sim_tick_credits;
    uint32_t previous_frame_reuse_count;
    uint8_t available_sim_credit;
    uint8_t sim_ticks_this_presentation;
    uint8_t sim_vblank_remainder;
    bool presentation_pending;
    bool render_active;
    bool queued_snapshot_valid;
    bool render_completed_valid;
    bool transfer_completed_valid;
    bool render_service_started;
    bool transfer_started;
    bool publish_pending;
    bool render_service_vblank_valid;
    bool transfer_poll_vblank_valid;
    uint32_t publish_generation;
} sm64_saturn_frame_pipeline_t;

/* Shared by the scheduler and authoritative source-tick owner. Generation 0
 * is reserved by render-snapshot and VDP1 frame-bank contracts. */
uint32_t sm64_saturn_frame_pipeline_next_generation(uint32_t generation);

void sm64_saturn_frame_pipeline_init(sm64_saturn_frame_pipeline_t *pipeline,
                                     uint32_t vblank_count,
                                     uint32_t displayed_generation);

sm64_saturn_frame_action_t sm64_saturn_frame_pipeline_step(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t vblank_count);

uint32_t sm64_saturn_frame_pipeline_action_generation(
    const sm64_saturn_frame_pipeline_t *pipeline);

bool sm64_saturn_frame_pipeline_render_complete(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation);

bool sm64_saturn_frame_pipeline_transfer_complete(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation);

/* A PUBLISH_FRAME action is intent, not proof that the hardware commit
 * succeeded. The owner must acknowledge the exact generation. Failure keeps
 * the previous display and consumes only the missed presentation edge. */
bool sm64_saturn_frame_pipeline_publish_complete(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation,
    bool succeeded);

#endif
