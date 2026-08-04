#include <stdint.h>
#include <stdio.h>

#include "saturn_render_job_bridge.h"

static const sm64_saturn_render_job_t k_jobs[] = {
    {.type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
     .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
     .snapshot_generation = 41U, .input_offset = 160U, .input_count = 12U,
     .output_offset = 96U, .output_capacity = 12U},
    {.type = SM64_SATURN_RENDER_JOB_ACTOR_LOWER,
     .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER,
     .snapshot_generation = 41U, .input_offset = 32U, .input_count = 8U,
     .output_offset = 20U, .output_capacity = 8U},
};

static int expect(int condition, const char *message)
{
    if (condition) return 1;
    fprintf(stderr, "%s\n", message);
    return 0;
}

int main(void)
{
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_output_bank_t terrain;
    sm64_saturn_render_output_bank_t actor;
    uint16_t job_index = 0U;
    sm64_saturn_render_job_execution_t execution = {0};
    uint32_t payload = 0x12345678U;

    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_output_bank_init(
        &terrain, SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN);
    sm64_saturn_render_output_bank_init(
        &actor, SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR);
    if (!expect(sm64_saturn_render_job_queue_publish(
                    &queue, 41U, k_jobs, 2U),
                "bridge fixture must publish immutable jobs")) return 1;
    const sm64_saturn_render_job_callback_table_t callbacks = {{NULL, NULL,
                                                                  NULL, NULL}};
    if (!expect(sm64_saturn_render_job_queue_slave_attach(
                    &queue, &callbacks, NULL),
                "queue must accept one persistent polling attachment"))
        return 1;
    if (!expect(!sm64_saturn_render_job_queue_slave_attach(
                    &queue, &callbacks, NULL),
                "queue must reject a second polling callback attachment"))
        return 1;
    if (!expect(sm64_saturn_render_job_queue_slave_notify(),
                "attached queue must accept an explicit slave notification"))
        return 1;

    /* `input_offset` is deliberately in the old slave range.  A master
     * claimant must still retain its cached range, selected by descriptor
     * output span rather than a fixed split or callback begin value. */
    if (!expect(sm64_saturn_render_job_queue_claim_master(&queue, 41U,
                                                           &job_index),
                "master must steal the world descriptor")) return 1;
    if (!expect(job_index == 0U, "world descriptor must be claimed first"))
        return 1;
    if (!expect(sm64_saturn_render_job_bridge_begin_output(
                    &queue, &terrain, &actor, job_index, &execution) &&
                    execution.job_index == job_index &&
                    execution.output_offset == 96U &&
                    execution.output_capacity == 12U &&
                    execution.writer_lane == SM64_SATURN_RENDER_OUTPUT_LANE_MASTER,
                "bridge must derive the writer lane from the actual claim"))
        return 1;
    if (!expect(sm64_saturn_render_job_bridge_read_output(
                    &queue, &terrain, &actor, job_index,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER, &payload) == NULL,
                "consumer must reject a claimed but not-done descriptor"))
        return 1;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 41U, job_index,
                    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER),
                "stolen descriptor must complete")) return 1;
    if (!expect(sm64_saturn_render_job_bridge_read_output(
                    &queue, &terrain, &actor, job_index,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER, &payload) ==
                    &payload,
                "master must keep a stolen output span cached")) return 1;
    if (!expect(sm64_saturn_render_job_bridge_read_output(
                    &queue, &terrain, &actor, job_index,
                    SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE, &payload) ==
                    &payload,
                "peer must select the descriptor-owned output span")) return 1;
    if (!expect(sm64_saturn_render_job_queue_claim_slave(&queue, 41U,
                                                          &job_index),
                "slave must claim the actor descriptor")) return 1;
    if (!expect(sm64_saturn_render_job_bridge_begin_output(
                    &queue, &terrain, &actor, job_index, &execution) &&
                    execution.writer_lane == SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE,
                "bridge must derive a slave writer lane from its actual claim"))
        return 1;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 41U, job_index,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "actor descriptor must complete before consumption")) return 1;
    if (!expect(sm64_saturn_render_job_bridge_read_output(
                    &queue, &terrain, &actor, job_index,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER, &payload) ==
                    &payload,
                "master peer must route actor output by descriptor span"))
        return 1;
    if (!expect(sm64_saturn_render_job_bridge_read_output(
                    &queue, &terrain, &actor, job_index + 1U,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER, &payload) == NULL,
                "wrong descriptor index must fail closed")) return 1;

    puts("render job bridge fixture: PASS");
    return 0;
}
