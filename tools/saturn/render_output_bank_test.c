#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include "saturn_render_job_queue.h"
#include "saturn_render_output_bank.h"

typedef struct publish_context {
    sm64_saturn_render_output_bank_t *bank;
    const sm64_saturn_render_job_t *job;
    uint16_t job_index;
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
    if (sm64_saturn_render_output_bank_publish(
            context->bank, context->job, context->job_index,
            context->claimed_state))
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
    uint8_t owner_lane = 0xFFU;
    uint32_t payload = 0x12345678U;

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
    if (!expect(sm64_saturn_render_output_bank_publish(
                    &terrain, &k_world_lower, 3U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER),
                "master claim must publish its descriptor-owned terrain lane"))
        return 1;
    if (!expect(sm64_saturn_render_output_bank_owner_lane(
                    &terrain, &k_world_lower, 3U, &owner_lane) &&
                    owner_lane == SM64_SATURN_RENDER_OUTPUT_LANE_MASTER,
                "published terrain lane must come from the actual claim")) return 1;
    if (!expect(!sm64_saturn_render_output_bank_publish(
                    &terrain, &k_actor_lower, 3U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "actor descriptor must not publish through terrain bank")) return 1;
    if (!expect(!sm64_saturn_render_output_bank_publish(
                    &terrain, &k_world_lower, 3U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "second owner must not overwrite published terrain lane")) return 1;

    /* A master may steal a descriptor whose logical input begins in the old
     * slave region.  Its output is nevertheless master-cached, and a peer
     * reader must take the P2/cache-through alias. */
    if (!expect(!sm64_saturn_render_output_bank_reader_needs_cache_through(
                    &terrain, &k_world_lower, 3U,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER),
                "stealing master must keep its terrain output cached")) return 1;
    if (!expect(sm64_saturn_render_output_bank_reader_needs_cache_through(
                    &terrain, &k_world_lower, 3U,
                    SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE),
                "peer reader must use cache-through terrain output")) return 1;
    if (!expect(sm64_saturn_render_output_bank_read_range(
                    &terrain, &k_world_lower, 3U,
                    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER, &payload) == &payload,
                "host owning reader retains its cached output pointer")) return 1;

    sm64_saturn_render_output_bank_init(
        &actor, SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR);
    publish_context_t master = {
        .bank = &actor, .job = &k_actor_lower, .job_index = 6U,
        .claimed_state = SM64_SATURN_RENDER_JOB_CLAIMED_MASTER};
    publish_context_t slave = {
        .bank = &actor, .job = &k_actor_lower, .job_index = 6U,
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
                    &actor, &k_actor_lower, 6U, &owner_lane) &&
                    owner_lane == (master.published != 0L
                        ? SM64_SATURN_RENDER_OUTPUT_LANE_MASTER
                        : SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE),
                "race winner must be the recorded actor output owner")) return 1;

    puts("render output-bank fixture: PASS");
    return 0;
}
