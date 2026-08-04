#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/port/saturn/gfx/saturn_render_callback_context.h"

typedef struct payload { uint32_t marker; } payload_t;

static int expect(int condition, const char *message)
{
    if (condition) return 1;
    fprintf(stderr, "%s\n", message);
    return 0;
}

static int run_claim(sm64_saturn_render_job_t job,
                     sm64_saturn_render_job_state_t claim,
                     uint8_t expected_cache_through)
{
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_callback_context_bank_t contexts;
    const payload_t payload = {0xC001CAFEU};
    sm64_saturn_render_callback_context_access_t access;
    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_callback_context_bank_init(&contexts);
    if (!expect(sm64_saturn_render_job_queue_publish(&queue, 7U, &job, 1U),
                "queue publication failed") ||
        !expect(sm64_saturn_render_callback_context_publish(
            &contexts, &queue, 7U, 0U, 23U, sizeof(payload), 0U),
                "context publication failed") ||
        !expect(sm64_saturn_render_job_queue_claim_index(
                    &queue, 7U, 0U, claim), "queue claim failed") ||
        !expect(sm64_saturn_render_callback_context_open(
            &contexts, &queue, 0U, claim, 23U, sizeof(payload), &payload,
            &access), "context open failed"))
        return 0;
    return expect(access.payload == &payload,
                  "host alias must preserve the bounded payload address") &&
        expect(access.cache_through == expected_cache_through,
               "peer claimant must select cache-through context access") &&
        expect(access.phase == job.callback_id && access.sequence == 23U,
               "callback must receive exact phase and sequence identity");
}

int main(void)
{
    const sm64_saturn_render_job_t terrain = {
        SM64_SATURN_RENDER_JOB_WORLD_LOWER,
        SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER, 7U, 0U, 2U, 0U, 2U};
    const sm64_saturn_render_job_t mario = {
        SM64_SATURN_RENDER_JOB_ACTOR_LOWER,
        SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER, 7U, 0U, 2U, 0U, 2U};
    if (!run_claim(terrain, SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, 0U) ||
        !run_claim(mario, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 1U))
        return 1;

    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_callback_context_bank_t contexts;
    payload_t payload = {0xC001CAFEU};
    sm64_saturn_render_callback_context_access_t access;
    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_callback_context_bank_init(&contexts);
    if (!sm64_saturn_render_job_queue_publish(&queue, 7U, &terrain, 1U))
        return 2;
    /* The fixture job carries generation 7 and must not publish into 9. */
    if (!expect(!sm64_saturn_render_callback_context_publish(
                    &contexts, &queue, 9U, 0U, 1U, sizeof(payload), 0U),
                "stale descriptor generation must not publish context"))
        return 1;

    sm64_saturn_render_job_t current = terrain;
    current.snapshot_generation = 9U;
    sm64_saturn_render_job_queue_init(&queue);
    if (!expect(sm64_saturn_render_job_queue_publish(
                    &queue, 9U, &current, 1U),
                "current queue publication failed") ||
        !expect(sm64_saturn_render_callback_context_publish(
                    &contexts, &queue, 9U, 0U, 31U, sizeof(payload), 0U),
                "current context publication failed") ||
        !expect(sm64_saturn_render_job_queue_claim_index(
                    &queue, 9U, 0U, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "current slave claim failed"))
        return 2;
    contexts.entries[0].phase = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 31U,
                    sizeof(payload), &payload, &access),
                "corrupt callback phase must fail closed"))
        return 1;
    contexts.entries[0].phase = current.callback_id;
    contexts.entries[0].ready = 0U;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 31U,
                    sizeof(payload), &payload, &access),
                "incomplete callback publication must fail closed"))
        return 1;
    contexts.entries[0].ready = 1U;
    contexts.entries[0].sequence = 30U;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 31U,
                    sizeof(payload), &payload, &access),
                "stale callback sequence must fail closed"))
        return 1;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 30U,
                    sizeof(payload), &payload, &access),
                "out-of-range callback identity must fail closed"))
        return 1;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, 30U,
                    sizeof(payload), &payload, &access),
                "cross-claim callback access must fail closed"))
        return 1;

    /* Repeat the corruption matrix for Mario so a terrain-only phase gate
     * cannot accidentally stand in for actor callback proof. */
    current = mario;
    current.snapshot_generation = 9U;
    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_callback_context_bank_init(&contexts);
    if (!sm64_saturn_render_job_queue_publish(&queue, 9U, &current, 1U) ||
        !sm64_saturn_render_callback_context_publish(
            &contexts, &queue, 9U, 0U, 41U, sizeof(payload), 0U) ||
        !sm64_saturn_render_job_queue_claim_index(
            &queue, 9U, 0U, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE))
        return 2;
    contexts.entries[0].phase = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 41U,
                    sizeof(payload), &payload, &access),
                "corrupt Mario phase must fail closed"))
        return 1;
    contexts.entries[0].phase = current.callback_id;
    contexts.entries[0].ready = 0U;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 41U,
                    sizeof(payload), &payload, &access),
                "incomplete Mario publication must fail closed"))
        return 1;
    contexts.entries[0].ready = 1U;
    contexts.entries[0].sequence = 40U;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 41U,
                    sizeof(payload), &payload, &access),
                "stale Mario sequence must fail closed") ||
        !expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, 40U,
                    sizeof(payload), &payload, &access),
                "wrong Mario claimant must fail closed"))
        return 1;

    puts("render callback context fixture: PASS");
    return 0;
}
