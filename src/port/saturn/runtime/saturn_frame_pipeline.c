#include "saturn_frame_pipeline.h"
#include "port/saturn/platform/saturn_cart_code.h"

#include <stddef.h>

#if defined(SM64_SATURN_FRAME_PIPELINE_TEST_FOUR_TICK)
#define SM64_SATURN_FRAME_MAX_SIM_TICKS 4U
#else
#define SM64_SATURN_FRAME_MAX_SIM_TICKS 2U
#endif

#define SM64_SATURN_FRAME_SIM_VBLANK_DIVISOR 2U

static void pipeline_observe_vblank(sm64_saturn_frame_pipeline_t *pipeline,
                                    uint32_t vblank_count)
{
    const uint32_t elapsed = vblank_count - pipeline->last_vblank_count;
    uint32_t tick_credits;
    uint32_t fractional_fields;
    uint32_t capacity;
    uint32_t accepted;

    if (elapsed == 0U) return;
#if !defined(SM64_SATURN_FRAME_PIPELINE_TEST_READD_CREDIT)
    pipeline->last_vblank_count = vblank_count;
#endif
    /* Presentation observes every field, while authoritative SM64 simulation
     * remains 30 Hz. Split the quotient/remainder calculation so the
     * wrap-safe uint32_t field delta never overflows when the carried odd
     * field is added. */
    tick_credits = elapsed / SM64_SATURN_FRAME_SIM_VBLANK_DIVISOR;
    fractional_fields = pipeline->sim_vblank_remainder +
                        (elapsed % SM64_SATURN_FRAME_SIM_VBLANK_DIVISOR);
    tick_credits += fractional_fields / SM64_SATURN_FRAME_SIM_VBLANK_DIVISOR;
    pipeline->sim_vblank_remainder =
        (uint8_t)(fractional_fields % SM64_SATURN_FRAME_SIM_VBLANK_DIVISOR);
    if (!pipeline->presentation_pending) {
        pipeline->presentation_pending = true;
    }

    capacity = SM64_SATURN_FRAME_MAX_SIM_TICKS -
               pipeline->sim_ticks_this_presentation;
    if (capacity > pipeline->available_sim_credit)
        capacity -= pipeline->available_sim_credit;
    else
        capacity = 0U;
    accepted = tick_credits < capacity ? tick_credits : capacity;
    pipeline->available_sim_credit =
        (uint8_t)(pipeline->available_sim_credit + accepted);
    pipeline->dropped_sim_tick_credits += tick_credits - accepted;
}

static void pipeline_finish_presentation(
    sm64_saturn_frame_pipeline_t *pipeline)
{
    pipeline->dropped_sim_tick_credits += pipeline->available_sim_credit;
    pipeline->available_sim_credit = 0U;
    pipeline->presentation_pending = false;
}

static void pipeline_promote_queued_snapshot(
    sm64_saturn_frame_pipeline_t *pipeline)
{
    if (!pipeline->queued_snapshot_valid) return;
    pipeline->render_generation = pipeline->queued_snapshot_generation;
    pipeline->queued_snapshot_valid = false;
    pipeline->render_active = true;
    pipeline->render_completed_valid = false;
    pipeline->transfer_completed_valid = false;
    pipeline->render_service_started = false;
    pipeline->transfer_started = false;
}

SM64_SATURN_CART_COLD
void sm64_saturn_frame_pipeline_init(sm64_saturn_frame_pipeline_t *pipeline,
                                     uint32_t vblank_count,
                                     uint32_t displayed_generation)
{
    if (pipeline == NULL) return;
    *pipeline = (sm64_saturn_frame_pipeline_t){0};
    pipeline->last_vblank_count = vblank_count;
    pipeline->displayed_generation = displayed_generation;
    pipeline->simulation_generation = displayed_generation;
    pipeline->action_generation = displayed_generation;
}

uint32_t sm64_saturn_frame_pipeline_next_generation(uint32_t generation)
{
    const uint32_t next = generation + 1U;
    return next != 0U ? next : 1U;
}

static sm64_saturn_frame_action_t pipeline_run_sim_tick(
    sm64_saturn_frame_pipeline_t *pipeline)
{
    pipeline->available_sim_credit--;
    pipeline->sim_ticks_this_presentation++;
    pipeline->simulation_generation =
        sm64_saturn_frame_pipeline_next_generation(
            pipeline->simulation_generation);
    pipeline->action_generation = pipeline->simulation_generation;
    if (!pipeline->render_active) {
        pipeline->render_active = true;
        pipeline->render_generation = pipeline->simulation_generation;
        pipeline->render_completed_valid = false;
        pipeline->transfer_completed_valid = false;
        pipeline->render_service_started = false;
        pipeline->transfer_started = false;
    } else {
        pipeline->queued_snapshot_generation = pipeline->simulation_generation;
        pipeline->queued_snapshot_valid = true;
    }
    return SM64_SATURN_FRAME_RUN_SIM_TICK;
}

sm64_saturn_frame_action_t sm64_saturn_frame_pipeline_step(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t vblank_count)
{
    bool publishable;

    if (pipeline == NULL) return SM64_SATURN_FRAME_WAIT_VBLANK;
    pipeline_observe_vblank(pipeline, vblank_count);

    if (pipeline->publish_pending) {
        pipeline->action_generation = pipeline->displayed_generation;
        return SM64_SATURN_FRAME_WAIT_VBLANK;
    }

#if defined(SM64_SATURN_FRAME_PIPELINE_TEST_PUBLISH_INCOMPLETE)
    publishable = pipeline->render_active && pipeline->transfer_started;
#else
    publishable = pipeline->render_active &&
                  pipeline->render_completed_valid &&
                  pipeline->render_completed_generation ==
                      pipeline->render_generation &&
                  pipeline->transfer_completed_valid &&
                  pipeline->transfer_completed_generation ==
                      pipeline->render_generation;
#endif
    if (pipeline->presentation_pending && publishable) {
        const uint32_t generation = pipeline->render_generation;
        pipeline->publish_pending = true;
        pipeline->publish_generation = generation;
        /* Publication consumes this field's remaining service opportunities.
         * A promoted bank cannot start SERVICE/POLL until another VBlank is
         * actually observed. */
        pipeline->render_service_vblank = pipeline->last_vblank_count;
        pipeline->render_service_vblank_valid = true;
        pipeline->transfer_poll_vblank = pipeline->last_vblank_count;
        pipeline->transfer_poll_vblank_valid = true;
        pipeline->action_generation = generation;
        return SM64_SATURN_FRAME_PUBLISH_FRAME;
    }

    if (pipeline->available_sim_credit != 0U &&
        (!pipeline->render_active || pipeline->render_service_started ||
         (pipeline->render_completed_valid &&
          pipeline->render_completed_generation ==
              pipeline->render_generation))) {
        return pipeline_run_sim_tick(pipeline);
    }

    if (pipeline->render_active &&
        (!pipeline->render_completed_valid ||
         pipeline->render_completed_generation != pipeline->render_generation)) {
        const bool serviced_this_vblank =
            pipeline->render_service_vblank_valid &&
            pipeline->render_service_vblank == pipeline->last_vblank_count;
        if (pipeline->presentation_pending && serviced_this_vblank) {
            pipeline->action_generation = pipeline->displayed_generation;
            pipeline_finish_presentation(pipeline);
            pipeline->previous_frame_reuse_count++;
            return SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME;
        }
        if (serviced_this_vblank) {
            pipeline->action_generation = pipeline->displayed_generation;
            return SM64_SATURN_FRAME_WAIT_VBLANK;
        }
        pipeline->render_service_started = true;
        pipeline->render_service_vblank = pipeline->last_vblank_count;
        pipeline->render_service_vblank_valid = true;
        pipeline->action_generation = pipeline->render_generation;
        return SM64_SATURN_FRAME_SERVICE_RENDER_JOBS;
    }

    if (pipeline->render_active &&
        pipeline->render_completed_valid &&
        pipeline->render_completed_generation == pipeline->render_generation &&
        (!pipeline->transfer_completed_valid ||
         pipeline->transfer_completed_generation != pipeline->render_generation)) {
        const bool polled_this_vblank =
            pipeline->transfer_poll_vblank_valid &&
            pipeline->transfer_poll_vblank == pipeline->last_vblank_count;
        if (pipeline->presentation_pending && polled_this_vblank) {
            pipeline->action_generation = pipeline->displayed_generation;
            pipeline_finish_presentation(pipeline);
            pipeline->previous_frame_reuse_count++;
            return SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME;
        }
        if (polled_this_vblank) {
            pipeline->action_generation = pipeline->displayed_generation;
            return SM64_SATURN_FRAME_WAIT_VBLANK;
        }
        pipeline->transfer_started = true;
        pipeline->transfer_generation = pipeline->render_generation;
        pipeline->transfer_poll_vblank = pipeline->last_vblank_count;
        pipeline->transfer_poll_vblank_valid = true;
        pipeline->action_generation = pipeline->transfer_generation;
        return SM64_SATURN_FRAME_POLL_TRANSFERS;
    }

    if (pipeline->presentation_pending) {
        pipeline->action_generation = pipeline->displayed_generation;
        pipeline_finish_presentation(pipeline);
        pipeline->previous_frame_reuse_count++;
        return SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME;
    }

    pipeline->action_generation = pipeline->displayed_generation;
    return SM64_SATURN_FRAME_WAIT_VBLANK;
}

uint32_t sm64_saturn_frame_pipeline_action_generation(
    const sm64_saturn_frame_pipeline_t *pipeline)
{
    return pipeline != NULL ? pipeline->action_generation : 0U;
}

bool sm64_saturn_frame_pipeline_render_complete(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation)
{
    if (pipeline == NULL || !pipeline->render_active ||
        !pipeline->render_service_started ||
        generation != pipeline->render_generation) {
        return false;
    }
    pipeline->render_completed_generation = generation;
    pipeline->render_completed_valid = true;
    return true;
}

bool sm64_saturn_frame_pipeline_transfer_complete(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation)
{
    if (pipeline == NULL || !pipeline->transfer_started ||
        pipeline->render_completed_generation != generation ||
        pipeline->transfer_generation != generation) {
        return false;
    }
    pipeline->transfer_completed_generation = generation;
    pipeline->transfer_completed_valid = true;
    return true;
}

bool sm64_saturn_frame_pipeline_publish_complete(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation,
    bool succeeded)
{
    bool queued_snapshot;

    if (pipeline == NULL || !pipeline->publish_pending ||
        pipeline->publish_generation != generation) {
        return false;
    }
    pipeline->publish_pending = false;
    if (!succeeded) {
        pipeline_finish_presentation(pipeline);
        pipeline->previous_frame_reuse_count++;
        pipeline->action_generation = pipeline->displayed_generation;
        return true;
    }

    queued_snapshot = pipeline->queued_snapshot_valid;
    pipeline->displayed_generation = generation;
    pipeline->render_active = false;
    pipeline->transfer_started = false;
    pipeline_finish_presentation(pipeline);
    /* A missed deadline consumes only a display opportunity. The
     * normal+recovery budget belongs to a successfully completed
     * presentation generation and resets only when that generation is
     * actually published. A queued recovery snapshot has already consumed
     * the normal slot for the next generation. */
    pipeline->sim_ticks_this_presentation = queued_snapshot ? 1U : 0U;
    pipeline_promote_queued_snapshot(pipeline);
    pipeline->action_generation = pipeline->displayed_generation;
    return true;
}
