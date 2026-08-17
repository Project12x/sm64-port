#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

    /* Field 104's single service slot was already consumed by generation 1,
     * so publication does not reopen it. */
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
    /* T2.17. Follow-up polls of the submitted generation are admitted inside
     * the submit field: they re-read the DMA queue's status word and write
     * no VDP1 command VRAM. None of them may publish an incomplete bank. */
    for (uint32_t retry = 0U; retry < 6U; retry++) {
        failure = expect_action(&pipeline, 9U,
                                SM64_SATURN_FRAME_POLL_TRANSFERS, 4U, 136);
        if (failure != 0) return failure;
    }
    /* Once the submit field ends without retirement, the conservative
     * one-poll-per-field schedule resumes and the missed presentation edge
     * is consumed by previous-frame reuse. */
    failure = expect_action(&pipeline, 10U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 4U, 137);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 10U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 3U, 34);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != 3U ||
        pipeline.previous_frame_reuse_count != 1U) {
        return 35;
    }

    /* Completion after the missed boundary is retained and published at the
     * next observed presentation boundary, with the exact same generation. */
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 4U)) return 36;
    failure = expect_action(&pipeline, 10U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 3U, 37);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 11U,
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
    /* T2.17: the submit field admits follow-up polls ... */
    for (uint32_t retry = 0U; retry < 4U; retry++) {
        failure = expect_action(&pipeline, 22U,
                                SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 138);
        if (failure != 0) return failure;
    }
    /* ... and the next field returns to one poll, then reuse, then the
     * terminal wait. Transfer service still precedes the terminal wait. */
    failure = expect_action(&pipeline, 23U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 139);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 23U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 0U, 44);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 23U,
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

    /* Field 106's service slot was consumed by generation 1; publication
     * does not reopen it, so the promoted generation begins at the next
     * observed field. */
    failure = expect_action(&pipeline, 106U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 125);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 107U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 126);
    if (failure != 0) return failure;
    return 0;
}

/* T2.17. The publication field admits render service for the generation
 * publication promoted, but never a command-VRAM overwrite and never a second
 * publication. This is the load-bearing half of the old blanket per-field
 * epoch: SERVICE builds into the frame bank publication just retired and
 * touches no VDP1 register, while POLL fences against VDP1 and DMAs over the
 * resident command list VDP1 is plotting from. */
static int test_publication_field_admits_service_but_not_transfer(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 200U, 0U);
    failure = expect_action(&pipeline, 204U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 150);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 204U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 151);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 152;
    failure = expect_action(&pipeline, 204U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 153);
    if (failure != 0) return failure;
    if (!pipeline.queued_snapshot_valid) return 154;

    /* Publication happens in a later field than the one that serviced the
     * render -- the shipped schedule's shape, where a render spans many
     * fields and completes near a boundary. */
    failure = expect_action(&pipeline, 205U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 155);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U))
        return 156;
    failure = expect_action(&pipeline, 205U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 157);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 158;
    if (pipeline.displayed_generation != 1U ||
        pipeline.render_generation != 2U || !pipeline.render_active) {
        return 159;
    }

    /* Service of the promoted generation is admitted in the publication
     * field. This is the cycle the per-field epoch used to spend on a raster
     * spin (T2.16 measured ~0.90 VB/frame of it). */
    failure = expect_action(&pipeline, 205U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 160);
    if (failure != 0) return failure;
    /* Exactly one: the per-field service epoch is otherwise intact. */
    failure = expect_action(&pipeline, 205U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 161);
    if (failure != 0) return failure;

    /* Even with the promoted render complete, the transfer slot publication
     * consumed stays consumed: no command-VRAM overwrite may start in the
     * field whose publication started the plot, so no second publication,
     * plot start, or frame-buffer change can occur in it either. */
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 2U)) return 162;
    failure = expect_action(&pipeline, 205U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 163);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != 1U) return 164;
    /* Admitting service in the publication field also makes generation 2's
     * recovery tick admissible one field earlier -- render_service_started
     * is the sim arm's gate. The budget itself is unchanged: one normal slot
     * (already consumed by the queued snapshot) plus one recovery tick. */
    failure = expect_action(&pipeline, 206U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 3U, 165);
    if (failure != 0) return failure;
    if (pipeline.sim_ticks_this_presentation != 2U) return 166;
    /* The next observed field opens the transfer slot. */
    failure = expect_action(&pipeline, 206U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 2U, 167);
    if (failure != 0) return failure;
    /* And exactly one submit in it. */
    failure = expect_action(&pipeline, 206U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 2U, 168);
    if (failure != 0) return failure;
    return 0;
}

/* T2.17. The case that isolates publication's own transfer stamp: a frame
 * whose transfer retires after its last poll publishes in a field that has
 * had no poll of its own. Nothing but publication's stamp then stands between
 * the promoted generation and a command-VRAM overwrite issued while VDP1 is
 * plotting the list publication just started. */
static int test_publication_field_consumes_the_transfer_slot(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 400U, 0U);
    failure = expect_action(&pipeline, 404U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 190);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 404U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 191);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 192;
    failure = expect_action(&pipeline, 404U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 193);
    if (failure != 0) return failure;
    if (!pipeline.queued_snapshot_valid) return 194;

    failure = expect_action(&pipeline, 405U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 195);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 406U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 196);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 406U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 0U, 197);
    if (failure != 0) return failure;
    /* Retirement lands after field 406's presentation edge was consumed, so
     * publication happens in field 407, which has issued no poll. */
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U))
        return 198;
    failure = expect_action(&pipeline, 406U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 199);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 407U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 200);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 201;

    /* Service of the promoted generation is admitted here. */
    failure = expect_action(&pipeline, 407U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 202);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 2U)) return 203;
    /* Its command-VRAM overwrite is not. Field 407 issued no poll of its own:
     * this refusal is publication's transfer stamp and nothing else. */
    failure = expect_action(&pipeline, 407U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 204);
    if (failure != 0) return failure;
    if (pipeline.displayed_generation != 1U) return 205;
    failure = expect_action(&pipeline, 408U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 3U, 206);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 408U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 2U, 207);
    if (failure != 0) return failure;
    return 0;
}

/* T2.17. The submitting poll -- the one that fences VDP1 and starts the DMA
 * over resident command VRAM -- stays gated at one per observed field. Its
 * follow-ups only re-read DMA status, so they are admitted, but only inside
 * the submit field: a queue that has not retired by the end of that field
 * falls back to the conservative schedule and its presentation edges. */
static int test_followup_polls_are_bounded_to_the_submit_field(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 300U, 0U);
    failure = expect_action(&pipeline, 302U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 170);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 302U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 171);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 172;

    /* The submitting poll. */
    failure = expect_action(&pipeline, 302U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 173);
    if (failure != 0) return failure;
    /* Follow-ups in the submit field: admitted, and they never publish an
     * unretired transfer. */
    for (uint32_t retry = 0U; retry < 32U; retry++) {
        failure = expect_action(&pipeline, 302U,
                                SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 174);
        if (failure != 0) return failure;
    }
    if (pipeline.displayed_generation != 0U) return 175;

    /* Retirement inside the submit field publishes in the submit field --
     * this is the ~0.96 VB/frame the old rule spent waiting for a field
     * boundary that carried no work (T2.16 section 3.3). */
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U))
        return 176;
    failure = expect_action(&pipeline, 302U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 177);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 178;

    /* A second generation reaching transfer in a later field submits once in
     * that field and no more: the submit epoch itself is untouched. */
    failure = expect_action(&pipeline, 304U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 179);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 304U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 180);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 2U)) return 181;
    failure = expect_action(&pipeline, 304U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 2U, 182);
    if (failure != 0) return failure;
    /* Crossing into field 305 without retirement: one poll, then the missed
     * presentation edge, then the terminal wait. */
    failure = expect_action(&pipeline, 305U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 2U, 183);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 305U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 1U, 184);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 305U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 185);
    if (failure != 0) return failure;
    /* And field 306 admits exactly one poll again (behind the recovery tick
     * that field's credit made admissible). */
    failure = expect_action(&pipeline, 306U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 3U, 189);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 306U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 2U, 186);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 306U,
                            SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME, 1U, 187);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 306U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 1U, 188);
    if (failure != 0) return failure;
    return 0;
}

static int test_transfer_deferral_is_epoch_gated_and_recoverable(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    uint32_t dropped_before;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 500U, 0U);
    failure = expect_action(&pipeline, 504U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 208);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 504U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 209);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 210;
    failure = expect_action(&pipeline, 504U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 211);
    if (failure != 0) return failure;
    if (!pipeline.queued_snapshot_valid ||
        pipeline.queued_snapshot_generation != 2U) return 212;
    failure = expect_action(&pipeline, 504U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 213);
    if (failure != 0) return failure;

    if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 2U)) return 214;
    if (!sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U)) return 215;
    if (pipeline.transfer_started || pipeline.transfer_submit_vblank_valid)
        return 216;
    if (!pipeline.transfer_poll_vblank_valid ||
        pipeline.transfer_poll_vblank != 504U) return 217;
    if (!pipeline.render_active || !pipeline.render_completed_valid ||
        pipeline.render_generation != 1U ||
        pipeline.render_completed_generation != 1U) return 218;
    if (!pipeline.queued_snapshot_valid ||
        pipeline.queued_snapshot_generation != 2U ||
        pipeline.displayed_generation != 0U ||
        pipeline.sim_ticks_this_presentation != 2U ||
        pipeline.previous_frame_reuse_count != 1U ||
        pipeline.presentation_pending) return 219;
    if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U)) return 220;
    failure = expect_action(&pipeline, 504U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 221);
    if (failure != 0) return failure;

    failure = expect_action(&pipeline, 505U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 222);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U)) return 223;
    failure = expect_action(&pipeline, 505U,
                            SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 224);
    if (failure != 0) return failure;

    dropped_before = pipeline.dropped_sim_tick_credits;
    failure = expect_action(&pipeline, 506U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 225);
    if (failure != 0) return failure;
    if (pipeline.sim_ticks_this_presentation != 2U ||
        pipeline.dropped_sim_tick_credits <= dropped_before) return 226;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 227;
    failure = expect_action(&pipeline, 506U,
                            SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 228);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
        return 229;
    failure = expect_action(&pipeline, 506U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 230);
    if (failure != 0) return failure;
    return 0;
}

static int test_transfer_deferral_rejects_invalid_state_without_mutation(void)
{
    sm64_saturn_frame_pipeline_t pipeline;
    sm64_saturn_frame_pipeline_t before;
    int failure;

    sm64_saturn_frame_pipeline_init(&pipeline, 600U, 0U);
    failure = expect_action(&pipeline, 602U,
                            SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 231);
    if (failure != 0) return failure;
    failure = expect_action(&pipeline, 602U,
                            SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 232);
    if (failure != 0) return failure;
    if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 233;

    before = pipeline;
    if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U) ||
        memcmp(&pipeline, &before, sizeof(pipeline)) != 0) return 234;
    failure = expect_action(&pipeline, 602U,
                            SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 235);
    if (failure != 0) return failure;
    before = pipeline;
    if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 0U) ||
        memcmp(&pipeline, &before, sizeof(pipeline)) != 0) return 236;
    if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 237;
    before = pipeline;
    if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U) ||
        memcmp(&pipeline, &before, sizeof(pipeline)) != 0) return 238;
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
    failure = test_publication_field_admits_service_but_not_transfer();
    if (failure != 0) return failure;
    failure = test_publication_field_consumes_the_transfer_slot();
    if (failure != 0) return failure;
    failure = test_transfer_deferral_is_epoch_gated_and_recoverable();
    if (failure != 0) return failure;
    failure = test_transfer_deferral_rejects_invalid_state_without_mutation();
    if (failure != 0) return failure;
    failure = test_followup_polls_are_bounded_to_the_submit_field();
    if (failure != 0) return failure;
    puts("frame pipeline contract: PASS");
    return 0;
}
