#include <stdint.h>
#include <stdio.h>

#include "saturn_render_job_bridge.h"
#include "saturn_render_payload_bank.h"

static int expect(int condition, const char *message)
{
    if (condition) return 1;
    fputs(message, stderr);
    fputc('\n', stderr);
    return 0;
}

int main(void)
{
    uint32_t terrain_master[8] = {0};
    uint32_t terrain_slave[8] = {0};
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_output_bank_t terrain;
    sm64_saturn_render_output_bank_t actor;
    sm64_saturn_render_payload_bank_t payload;
    sm64_saturn_render_job_execution_t execution;
    uint16_t index;
    const sm64_saturn_render_job_t job = {
        .type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
        .snapshot_generation = 9U, .input_offset = 200U, .input_count = 1U,
        .output_offset = 3U, .output_capacity = 2U,
    };

    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_output_bank_init(
        &terrain, SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN);
    sm64_saturn_render_output_bank_init(
        &actor, SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR);
    sm64_saturn_render_payload_bank_init(
        &payload, terrain_master, terrain_slave, sizeof(terrain_master[0]), 8U);
    if (!expect(sm64_saturn_render_job_queue_publish(&queue, 9U, &job, 1U),
                "publish")) return 1;
    if (!expect(sm64_saturn_render_job_queue_claim_master(&queue, 9U, &index),
                "master claim")) return 1;
    if (!expect(sm64_saturn_render_job_bridge_begin_output(
                    &queue, &terrain, &actor, index, &execution),
                "bridge begin")) return 1;
    uint32_t *writer = sm64_saturn_render_payload_bank_write(
        &payload, &execution);
    if (!expect(writer == &terrain_master[3],
                "stolen input range must select master physical payload")) return 1;
    writer[0] = 0xA5A5U;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 9U, index, SM64_SATURN_RENDER_JOB_CLAIMED_MASTER),
                "complete")) return 1;
    if (!expect(sm64_saturn_render_payload_bank_read(
                    &payload, &queue, &terrain, &actor, index,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER) == &terrain_master[3],
                "done reader must retain exact descriptor payload")) return 1;
    if (!expect(terrain_slave[3] == 0U, "slave payload must stay untouched"))
        return 1;
    puts("render job payload bank fixture: PASS");
    return 0;
}
