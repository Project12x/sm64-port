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

static int test_two_fields_produce_one_sim_tick(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 100U, 0U);
    failure = expect_action(&pipeline, 101U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 0U, 95);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 101U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 96);
    if (failure != 0) return failure;
    /* The odd field remainder survives the missed presentation edge. */
    failure = expect_action(&pipeline, 102U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 97);
    if (failure != 0) return failure;
    if (pipeline.simulation_generation != 1U) return 98;
    return 0;
}

static int test_vblank_delta_wrap_preserves_half_rate_remainder(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, UINT32_MAX, 0U);
    failure = expect_action(&pipeline, 0U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 0U, 99);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 1U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 100);
    if (failure != 0) return failure;
    return 0;
}

static int test_nonzero_generation_successor_contract(void)
{
    /* Sourceboot's authoritative sourceboot_sim_tick_count must use this
     * same helper before publishing its render snapshot; a raw ++ would
     * diverge from the scheduler at wrap and publish forbidden generation 0. */
    if (sm64_saturn_frame_pipeline_next_generation(0U) != 1U) return 101;
    if (sm64_saturn_frame_pipeline_next_generation(1U) != 2U) return 102;
    if (sm64_saturn_frame_pipeline_next_generation(UINT32_MAX) != 1U)
        return 103;
    return 0;
}

static int test_complete_frame_is_generation_coherent(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 100U, 0U);
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 1);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 2);
    if (failure != 0) return failure;
    if (sm64_saturn_frame_pipeline_render_complete(&pipeline, 2U)) return 3;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 4;

    /* The bounded recovery tick advances authoritative state while snapshot
     * generation 1 remains the immutable render/transfer generation. */
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 5);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 6);
    if (failure != 0) return failure;
    if (sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 2U)) return 7;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 8;
    failure = expect_action(&pipeline, 104U,
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
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 13);
    if (failure != 0) return failure;
    /* The next observed field opens one service opportunity for snapshot 2. */
    failure = expect_action(&pipeline, 105U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 14);
    if (failure != 0) return failure;
    return 0;
}

static int test_generation_wrap_skips_zero(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 10U, UINT32_MAX);
    failure = expect_action(&pipeline, 12U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 13);
    if (failure != 0) return failure;
    /* Downstream snapshot and VDP1 bank contracts reserve generation zero. */
    failure = expect_action(&pipeline, 12U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 14);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 15;
    failure = expect_action(&pipeline, 12U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 16);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 17;
    failure = expect_action(&pipeline, 12U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 18);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != UINT32_MAX) return 19;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 27;
    if (pipeline.displayed_generation != 1U) return 28;
    return 0;
}

static int test_queued_generation_wrap_skips_zero(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 30U, UINT32_MAX - 1U);
    failure = expect_action(&pipeline, 34U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, UINT32_MAX, 70);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 34U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS,
                            UINT32_MAX, 71);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, UINT32_MAX))
        return 72;
    failure = expect_action(&pipeline, 34U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 73);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 34U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS,
                            UINT32_MAX, 74);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, UINT32_MAX))
        return 75;
    failure = expect_action(&pipeline, 34U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME,
                            UINT32_MAX, 76);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != UINT32_MAX - 1U) return 79;
    if (!sm64_saturn_frame_pipeline_publish_complete(
            &pipeline, UINT32_MAX, true)) {
        return 80;
    }
    /* The wrapped nonzero queued snapshot cannot consume a second
     * SERVICE slot in field 34 after publication promoted it. */
    failure = expect_action(&pipeline, 34U,
                            SM64_SATURN_FRAME_WAIT_VBLANK,
                            UINT32_MAX, 77);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 35U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 78);
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
            sm64_saturn_frame_pipeline_step(&pipeline, 48U);
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
    /* Eight elapsed fields produce four tick credits; two exceed the
     * presentation-scoped normal+recovery budget. */
    if (pipeline.dropped_sim_tick_credits != 2U) return 24;

    if (sm64_saturn_frame_pipeline_step(&pipeline, 48U) !=
        SM64_SATURN_FRAME_WAIT_VBLANK) {
        return 26;
    }

    /* Re-entering the outer loop with the same VBlank observation must not
     * recreate a simulation budget. */
    for (uint32_t outer_iteration = 0U; outer_iteration < 8U;
         outer_iteration++) {
        if (sm64_saturn_frame_pipeline_step(&pipeline, 48U) ==
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
    failure = expect_action(&pipeline, 9U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 4U, 30);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 9U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 4U, 31);
    if (failure != 0) return failure;

    /* A render completion is not a transferable/publishable bank. */
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 4U)) return 32;
    failure = expect_action(&pipeline, 9U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 4U, 33);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 9U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 3U, 34);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != 3U ||
        pipeline.previous_frame_reuse_count != 1U) {
        return 35;
    }

    /* Completion after the missed boundary is retained and published at the
     * next observed presentation boundary, with the exact same generation. */
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 4U)) return 36;
    failure = expect_action(&pipeline, 9U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 3U, 37);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 10U,
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
    for (uint32_t vblank = 61U; vblank <= 66U; vblank++) {
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
    if (pipeline.dropped_sim_tick_credits < 1U) return 49;
    return 0;
}

static int test_transfer_service_precedes_terminal_wait(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 20U, 0U);
    failure = expect_action(&pipeline, 22U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 40);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 22U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 41);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 42;
    failure = expect_action(&pipeline, 22U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 43);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 22U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 0U, 44);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 22U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 45);
    if (failure != 0) return failure;
    return 0;
}

static int test_publish_requires_exact_success_acknowledgement(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 80U, 0U);
    failure = expect_action(&pipeline, 82U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 81);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 82U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 82);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 83;
    failure = expect_action(&pipeline, 82U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 84);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 85;
    failure = expect_action(&pipeline, 82U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 86);
    if (failure != 0) return failure;

    if (pipeline.displayed_generation != 0U ||
        pipeline.sim_ticks_this_presentation != 1U) {
        return 87;
    }
    if (sm64_saturn_frame_pipeline_publish_complete(&pipeline, 2U, true))
        return 88;
    failure = expect_action(&pipeline, 82U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 89);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, false))
        return 90;
    if (pipeline.displayed_generation != 0U ||
        pipeline.sim_ticks_this_presentation != 1U ||
        pipeline.previous_frame_reuse_count != 1U) {
        return 91;
    }
    failure = expect_action(&pipeline, 83U,
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
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 50);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 51);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 52;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 53);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 54);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 55;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 56);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 65;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 57);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 108U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 66);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 2U)) return 58;

    /* Snapshot 2 was produced by the prior recovery tick. It consumes the
     * normal slot of generation 2's budget, leaving exactly one recovery
     * tick even when four more fields elapsed before render completion. */
    failure = expect_action(&pipeline, 108U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 3U, 59);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 108U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 2U, 60);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 2U)) return 61;
    failure = expect_action(&pipeline, 108U,
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

static int test_pending_render_spans_fields_and_excludes_queued_generation(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 100U, 0U);
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 104);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 105);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 106);
    if (failure != 0) return failure;
    if (!pipeline.queued_snapshot_valid ||
        pipeline.queued_snapshot_generation != 2U) {
        return 107;
    }
    if (sm64_saturn_frame_pipeline_render_complete(&pipeline, 2U)) return 108;
    if (sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 2U))
        return 109;
    if (sm64_saturn_frame_pipeline_publish_complete(&pipeline, 2U, true))
        return 110;

    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 0U, 111);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 104U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 112);
    if (failure != 0) return failure;

    /* Generation 1 stays active and is serviced at most once per newly
     * observed field. Reuse consumes each missed presentation edge without
     * promoting or servicing queued generation 2. */
    failure = expect_action(&pipeline, 105U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 113);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 105U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 0U, 114);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 105U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 115);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 106U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 116);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 117;
    failure = expect_action(&pipeline, 106U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 118);
    if (failure != 0) return failure;
    if (sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 2U))
        return 119;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U))
        return 120;
    failure = expect_action(&pipeline, 106U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 121);
    if (failure != 0) return failure;
    if (sm64_saturn_frame_pipeline_publish_complete(&pipeline, 2U, true))
        return 122;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 123;
    if (pipeline.displayed_generation != 1U ||
        pipeline.render_generation != 2U ||
        !pipeline.render_active) {
        return 124;
    }

    /* Publication cannot reopen service in the same field; the promoted
     * generation begins only at the next observed field. */
    failure = expect_action(&pipeline, 106U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 125);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 107U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 126);
    if (failure != 0) return failure;
    return 0;
}

int main(void)
{
    int failure = test_two_fields_produce_one_sim_tick();
    if (failure != 0) return failure;
    failure = test_vblank_delta_wrap_preserves_half_rate_remainder();
    if (failure != 0) return failure;
    failure = test_nonzero_generation_successor_contract();
    if (failure != 0) return failure;
    failure = test_complete_frame_is_generation_coherent();
    if (failure != 0) return failure;
    failure = test_generation_wrap_skips_zero();
    if (failure != 0) return failure;
    failure = test_queued_generation_wrap_skips_zero();
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
    failure = test_pending_render_spans_fields_and_excludes_queued_generation();
    if (failure != 0) return failure;
    puts("frame pipeline contract: PASS");
    return 0;
}
