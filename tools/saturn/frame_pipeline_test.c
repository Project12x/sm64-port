#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "saturn_frame_pipeline.h"

static int expect_action(sm64_saturn_frame_pipeline_t *pipeline,
                         uint32_t vblank_count,
                         sm64_saturn_frame_action_t expected,
                         uint32_t expected_generation,
                         int failure)
{
    const sm64_saturn_frame_action_t actual =
        sm64_saturn_frame_pipeline_step(pipeline, vblank_count);
    if (actual != expected ||
        sm64_saturn_frame_pipeline_action_generation(pipeline) !=
            expected_generation) {
        return failure;
    }
    return 0;
}

static int test_complete_frame_is_generation_coherent(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 100U, 0U);
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 1);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 2);
    if (failure != 0) return failure;
    if (sm64_saturn_frame_pipeline_render_complete(&pipeline, 2U)) return 3;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 4;

    /* The bounded recovery tick advances authoritative state while snapshot
     * generation 1 remains the immutable render/transfer generation. */
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 5);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 6);
    if (failure != 0) return failure;
    if (sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 2U)) return 7;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 8;
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 9);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != 0U) return 10;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 11;
    if (pipeline.displayed_generation != 1U ||
        pipeline.simulation_generation != 2U ||
        pipeline.render_completed_generation != 1U ||
        pipeline.transfer_completed_generation != 1U) {
        return 12;
    }

    /* Publication cannot reopen the field's already-consumed service slot. */
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 13);
    if (failure != 0) return failure;
    /* The next observed field opens one service opportunity for snapshot 2. */
    failure = expect_action(&pipeline, 103U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 14);
    if (failure != 0) return failure;
    return 0;
}

static int test_generation_zero_is_valid_after_wrap(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 10U, UINT32_MAX);
    failure = expect_action(&pipeline, 11U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 0U, 13);
    if (failure != 0) return failure;
    /* Zero is a wrapped generation, not an implicit completion sentinel. */
    failure = expect_action(&pipeline, 11U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 0U, 14);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 0U)) return 15;
    failure = expect_action(&pipeline, 11U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 0U, 16);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 0U)) return 17;
    failure = expect_action(&pipeline, 11U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 0U, 18);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != UINT32_MAX) return 19;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 0U, true))
        return 27;
    if (pipeline.displayed_generation != 0U) return 28;
    return 0;
}

static int test_queued_generation_zero_survives_wrap(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 30U, UINT32_MAX - 1U);
    failure = expect_action(&pipeline, 32U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, UINT32_MAX, 70);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 32U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS,
                            UINT32_MAX, 71);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, UINT32_MAX))
        return 72;
    failure = expect_action(&pipeline, 32U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 0U, 73);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 32U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS,
                            UINT32_MAX, 74);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, UINT32_MAX))
        return 75;
    failure = expect_action(&pipeline, 32U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME,
                            UINT32_MAX, 76);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != UINT32_MAX - 1U) return 79;
    if (!sm64_saturn_frame_pipeline_publish_complete(
            &pipeline, UINT32_MAX, true)) {
        return 80;
    }
    /* The wrapped queued snapshot is real, but cannot consume a second
     * SERVICE slot in field 32 after publication promoted it. */
    failure = expect_action(&pipeline, 32U,
                            SM64_SATURN_FRAME_WAIT_VBLANK,
                            UINT32_MAX, 77);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 33U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 0U, 78);
    if (failure != 0) return failure;
    return 0;
}

static int test_slow_generation_has_one_normal_and_one_recovery_tick(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    uint32_t sim_actions = 0U;
    uint32_t service_actions = 0U;
    bool saw_reuse = false;

    sm64_saturn_frame_pipeline_init(&pipeline, 40U, 9U);
    for (uint32_t outer_iteration = 0U; outer_iteration < 12U;
         outer_iteration++) {
        const sm64_saturn_frame_action_t action =
            sm64_saturn_frame_pipeline_step(&pipeline, 44U);
        if (action == SM64_SATURN_FRAME_RUN_SIM_TICK) sim_actions++;
        if (action == SM64_SATURN_FRAME_SERVICE_RENDER_JOBS)
            service_actions++;
        if (action == SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME) {
            saw_reuse = true;
            break;
        }
    }
    if (sim_actions != 2U) return 20;
    if (service_actions == 0U) return 21;
    if (!saw_reuse) return 22;
    if (pipeline.displayed_generation != 9U ||
        pipeline.previous_frame_reuse_count != 1U) {
        return 23;
    }
    /* Four elapsed ticks minus the presentation-scoped two-tick budget. */
    if (pipeline.dropped_sim_credit != 2U) return 24;

    if (sm64_saturn_frame_pipeline_step(&pipeline, 44U) !=
        SM64_SATURN_FRAME_WAIT_VBLANK) {
        return 26;
    }

    /* Re-entering the outer loop with the same VBlank observation must not
     * recreate a simulation budget. */
    for (uint32_t outer_iteration = 0U; outer_iteration < 8U;
         outer_iteration++) {
        if (sm64_saturn_frame_pipeline_step(&pipeline, 44U) ==
            SM64_SATURN_FRAME_RUN_SIM_TICK) {
            return 25;
        }
    }
    return 0;
}

static int test_incomplete_bank_is_reused_never_published(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 7U, 3U);
    failure = expect_action(&pipeline, 8U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 4U, 30);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 8U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 4U, 31);
    if (failure != 0) return failure;

    /* A render completion is not a transferable/publishable bank. */
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 4U)) return 32;
    failure = expect_action(&pipeline, 8U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 4U, 33);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 8U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 3U, 34);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != 3U ||
        pipeline.previous_frame_reuse_count != 1U) {
        return 35;
    }

    /* Completion after the missed boundary is retained and published at the
     * next observed presentation boundary, with the exact same generation. */
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 4U)) return 36;
    failure = expect_action(&pipeline, 8U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 3U, 37);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 9U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 4U, 38);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != 3U) return 39;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 4U, true))
        return 64;
    if (pipeline.displayed_generation != 4U) return 39;
    return 0;
}

static int test_reuse_does_not_reopen_budget_before_publication(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    uint32_t sim_actions = 0U;

    sm64_saturn_frame_pipeline_init(&pipeline, 60U, 0U);
    for (uint32_t vblank = 61U; vblank <= 65U; vblank++) {
        bool reused = false;
        for (uint32_t outer_iteration = 0U; outer_iteration < 8U;
             outer_iteration++) {
            const sm64_saturn_frame_action_t action =
                sm64_saturn_frame_pipeline_step(&pipeline, vblank);
            if (action == SM64_SATURN_FRAME_RUN_SIM_TICK) sim_actions++;
            if (action == SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME) {
                reused = true;
                break;
            }
        }
        if (!reused) return 46;
    }
    /* A missed field is not a completed presentation generation. Repeated
     * outer iterations and later VBlanks therefore cannot manufacture a new
     * normal+recovery budget while generation 1 remains incomplete. */
    if (sim_actions > 2U) return 47;
    if (pipeline.simulation_generation > 2U) return 48;
    if (pipeline.dropped_sim_credit < 3U) return 49;
    return 0;
}

static int test_transfer_service_precedes_terminal_wait(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 20U, 0U);
    failure = expect_action(&pipeline, 21U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 40);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 21U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 41);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 42;
    failure = expect_action(&pipeline, 21U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 43);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 21U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 0U, 44);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 21U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 45);
    if (failure != 0) return failure;
    return 0;
}

static int test_publish_requires_exact_success_acknowledgement(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 80U, 0U);
    failure = expect_action(&pipeline, 81U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 81);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 81U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 82);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 83;
    failure = expect_action(&pipeline, 81U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 84);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 85;
    failure = expect_action(&pipeline, 81U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 86);
    if (failure != 0) return failure;

    if (pipeline.displayed_generation != 0U ||
        pipeline.sim_ticks_this_presentation != 1U) {
        return 87;
    }
    if (sm64_saturn_frame_pipeline_publish_complete(&pipeline, 2U, true))
        return 88;
    failure = expect_action(&pipeline, 81U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 89);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, false))
        return 90;
    if (pipeline.displayed_generation != 0U ||
        pipeline.sim_ticks_this_presentation != 1U ||
        pipeline.previous_frame_reuse_count != 1U) {
        return 91;
    }
    failure = expect_action(&pipeline, 82U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 92);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 93;
    if (pipeline.displayed_generation != 1U) return 94;
    return 0;
}

static int test_queued_snapshot_counts_against_next_generation_budget(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 100U, 0U);
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 50);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 51);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 52;
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 53);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 54);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 55;
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 56);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 65;
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 57);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 66);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 2U)) return 58;

    /* Snapshot 2 was produced by the prior recovery tick. It consumes the
     * normal slot of generation 2's budget, leaving exactly one recovery
     * tick even when two more fields elapsed before render completion. */
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 3U, 59);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 2U, 60);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 2U)) return 61;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 2U, 62);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 2U, true))
        return 67;
    if (pipeline.simulation_generation != 3U ||
        pipeline.displayed_generation != 2U) {
        return 63;
    }
    return 0;
}

int main(void)
{
    int failure = test_complete_frame_is_generation_coherent();
    if (failure != 0) return failure;
    failure = test_generation_zero_is_valid_after_wrap();
    if (failure != 0) return failure;
    failure = test_queued_generation_zero_survives_wrap();
    if (failure != 0) return failure;
    failure = test_slow_generation_has_one_normal_and_one_recovery_tick();
    if (failure != 0) return failure;
    failure = test_incomplete_bank_is_reused_never_published();
    if (failure != 0) return failure;
    failure = test_transfer_service_precedes_terminal_wait();
    if (failure != 0) return failure;
    failure = test_publish_requires_exact_success_acknowledgement();
    if (failure != 0) return failure;
    failure = test_reuse_does_not_reopen_budget_before_publication();
    if (failure != 0) return failure;
    failure = test_queued_snapshot_counts_against_next_generation_budget();
    if (failure != 0) return failure;
    puts("frame pipeline contract: PASS");
    return 0;
}
