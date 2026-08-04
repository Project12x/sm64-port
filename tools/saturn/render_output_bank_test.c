#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include "saturn_render_job_queue.h"
#include "saturn_render_output_bank.h"

typedef struct publish_context {
    sm64_saturn_render_output_bank_t *bank;
    sm64_saturn_render_job_queue_t *queue;
    sm64_saturn_render_job_state_t claimed_state;
    volatile LONG published;
} publish_context_t;

static const sm64_saturn_render_job_t k_world_lower = {
    .type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
    .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
    .snapshot_generation = 23U,
    .input_offset = 96U,
    .input_count = 12U,
    .output_offset = 48U,
    .output_capacity = 12U,
};

static const sm64_saturn_render_job_t k_actor_lower = {
    .type = SM64_SATURN_RENDER_JOB_ACTOR_LOWER,
    .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER,
    .snapshot_generation = 23U,
    .input_offset = 144U,
    .input_count = 8U,
    .output_offset = 80U,
    .output_capacity = 8U,
};

static DWORD WINAPI publish_once(void *opaque)
{
    publish_context_t *context = opaque;
    uint16_t job_index = 0U;
    const int claimed = context->claimed_state ==
            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER
        ? sm64_saturn_render_job_queue_claim_master(context->queue, 23U,
                                                    &job_index)
        : sm64_saturn_render_job_queue_claim_slave(context->queue, 23U,
                                                   &job_index);
    if (claimed && sm64_saturn_render_output_bank_publish(
            context->bank, context->queue, job_index))
        InterlockedIncrement(&context->published);
    return 0UL;
}

static int expect(int condition, const char *message)
{
    if (condition) return 1;
    fprintf(stderr, "%s\n", message);
    return 0;
}

int main(void)
{
    sm64_saturn_render_output_bank_t terrain;
    sm64_saturn_render_output_bank_t actor;
    sm64_saturn_render_job_queue_t queue;
    uint8_t owner_lane = 0xFFU;
    uint32_t payload = 0x12345678U;
    uint16_t job_index = 0U;

    sm64_saturn_render_output_bank_init(
        &terrain, SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN);
    sm64_saturn_render_output_bank_init(
        &actor, SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR);

    if (!expect(sm64_saturn_render_output_bank_kind_for_job(&k_world_lower) ==
                    SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN,
                "world descriptor must own the terrain output bank")) return 1;
    if (!expect(sm64_saturn_render_output_bank_kind_for_job(&k_actor_lower) ==
                    SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR,
                "actor descriptor must own the actor output bank")) return 1;
    sm64_saturn_render_job_queue_init(&queue);
    if (!expect(sm64_saturn_render_job_queue_publish(
                    &queue, 23U, &k_world_lower, 1U),
                "terrain queue descriptor must publish before output claim"))
        return 1;
    if (!expect(!sm64_saturn_render_output_bank_publish(
                    &terrain, &queue, 0U),
                "forged output publication without a queue claim must fail"))
        return 1;
    if (!expect(sm64_saturn_render_job_queue_claim_master(&queue, 23U,
                                                           &job_index),
                "actual master queue claim must succeed")) return 1;
    if (!expect(sm64_saturn_render_output_bank_publish(
                    &terrain, &queue, job_index),
                "actual master queue claim must publish terrain lane"))
        return 1;
    if (!expect(sm64_saturn_render_output_bank_owner_lane(
                    &terrain, &queue, job_index, &owner_lane) &&
                    owner_lane == SM64_SATURN_RENDER_OUTPUT_LANE_MASTER,
                "published terrain lane must come from the actual claim")) return 1;
    if (!expect(!sm64_saturn_render_output_bank_publish(
                    &actor, &queue, job_index),
                "world descriptor must not publish through actor bank")) return 1;
    if (!expect(!sm64_saturn_render_output_bank_publish(
                    &terrain, &queue, job_index),
                "second owner must not overwrite published terrain lane")) return 1;

    /* A master may steal a descriptor whose logical input begins in the old
     * slave region.  Its output is nevertheless master-cached, and a peer
     * reader must take the P2/cache-through alias. */
    if (!expect(!sm64_saturn_render_output_bank_reader_needs_cache_through(
                    &terrain, &queue, job_index,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER),
                "stealing master must keep its terrain output cached")) return 1;
    if (!expect(sm64_saturn_render_output_bank_reader_needs_cache_through(
                    &terrain, &queue, job_index,
                    SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE),
                "peer reader must use cache-through terrain output")) return 1;
    if (!expect(sm64_saturn_render_output_bank_read_range(
                    &terrain, &queue, job_index,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER, &payload) == &payload,
                "host owning reader retains its cached output pointer")) return 1;

    sm64_saturn_render_output_bank_init(
        &actor, SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR);
    sm64_saturn_render_job_queue_init(&queue);
    if (!expect(sm64_saturn_render_job_queue_publish(
                    &queue, 23U, &k_actor_lower, 1U),
                "actor queue descriptor must publish before race")) return 1;
    publish_context_t master = {
        .bank = &actor, .queue = &queue,
        .claimed_state = SM64_SATURN_RENDER_JOB_CLAIMED_MASTER};
    publish_context_t slave = {
        .bank = &actor, .queue = &queue,
        .claimed_state = SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE};
    HANDLE master_thread = CreateThread(NULL, 0U, publish_once, &master, 0U,
                                        NULL);
    HANDLE slave_thread = CreateThread(NULL, 0U, publish_once, &slave, 0U,
                                       NULL);
    if (!expect(master_thread != NULL && slave_thread != NULL,
                "lane publication race threads must start")) return 1;
    WaitForSingleObject(master_thread, INFINITE);
    WaitForSingleObject(slave_thread, INFINITE);
    CloseHandle(master_thread);
    CloseHandle(slave_thread);
    if (!expect(master.published + slave.published == 1L,
                "exactly one claimed CPU may publish an output lane")) return 1;
    if (!expect(sm64_saturn_render_output_bank_owner_lane(
                    &actor, &queue, 0U, &owner_lane) &&
                    owner_lane == (master.published != 0L
                        ? SM64_SATURN_RENDER_OUTPUT_LANE_MASTER
                        : SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE),
                "race winner must be the recorded actor output owner")) return 1;

    puts("render output-bank fixture: PASS");
    return 0;
}
