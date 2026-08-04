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

static bool open_phase(
    const sm64_saturn_render_job_t *job,
    const sm64_saturn_render_callback_context_bank_t *contexts,
    const sm64_saturn_render_job_queue_t *queue,
    sm64_saturn_render_job_state_t claim, uint32_t sequence,
    uint16_t bytes, const void *payload,
    sm64_saturn_render_callback_context_access_t *access)
{
    switch (job->callback_id) {
    case SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT:
        return sm64_saturn_render_callback_context_open_world_admit(
            contexts, queue, 0U, claim, sequence, bytes, payload, access);
    case SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER:
        return sm64_saturn_render_callback_context_open_world_lower(
            contexts, queue, 0U, claim, sequence, bytes, payload, access);
    case SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT:
        return sm64_saturn_render_callback_context_open_actor_admit(
            contexts, queue, 0U, claim, sequence, bytes, payload, access);
    case SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER:
        return sm64_saturn_render_callback_context_open_actor_lower(
            contexts, queue, 0U, claim, sequence, bytes, payload, access);
    default: return false;
    }
}

static bool open_wrong_phase(
    const sm64_saturn_render_job_t *job,
    const sm64_saturn_render_callback_context_bank_t *contexts,
    const sm64_saturn_render_job_queue_t *queue,
    sm64_saturn_render_job_state_t claim, uint32_t sequence,
    uint16_t bytes, const void *payload,
    sm64_saturn_render_callback_context_access_t *access)
{
    return job->callback_id == SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT
        ? sm64_saturn_render_callback_context_open_actor_lower(
              contexts, queue, 0U, claim, sequence, bytes, payload, access)
        : sm64_saturn_render_callback_context_open_world_admit(
              contexts, queue, 0U, claim, sequence, bytes, payload, access);
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
        !expect(open_phase(&job, &contexts, &queue, claim, 23U,
            sizeof(payload), &payload,
            &access), "context open failed"))
        return 0;
    return expect(access.payload == &payload,
                  "host alias must preserve the bounded payload address") &&
        expect(access.cache_through == expected_cache_through,
               "peer claimant must select cache-through context access") &&
        expect(access.phase == job.callback_id && access.sequence == 23U,
               "callback must receive exact phase and sequence identity");
}

static int run_identity_mutations(sm64_saturn_render_job_t job)
{
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_callback_context_bank_t contexts;
    const payload_t payload = {0xA11CE55U};
    sm64_saturn_render_callback_context_access_t access;
    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_callback_context_bank_init(&contexts);
    if (!sm64_saturn_render_job_queue_publish(&queue, 7U, &job, 1U) ||
        !sm64_saturn_render_callback_context_publish(
            &contexts, &queue, 7U, 0U, 51U, sizeof(payload), 0U) ||
        !sm64_saturn_render_job_queue_claim_index(
            &queue, 7U, 0U, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE))
        return 0;

    contexts.entries[0].payload_bytes = (uint16_t)(sizeof(payload) + 1U);
    if (!expect(!open_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 51U,
                    sizeof(payload), &payload, &access),
                "corrupt payload bound must fail closed"))
        return 0;
    contexts.entries[0].payload_bytes = sizeof(payload);
    contexts.entries[0].producer_lane = 2U;
    if (!expect(!open_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 51U,
                    sizeof(payload), &payload, &access),
                "corrupt producer lane must fail closed"))
        return 0;
    contexts.entries[0].producer_lane = 0U;
    contexts.entries[0].job_index = 1U;
    if (!expect(!open_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 51U,
                    sizeof(payload), &payload, &access),
                "corrupt stored descriptor index must fail closed"))
        return 0;
    contexts.entries[0].job_index = 0U;
    contexts.entries[0].generation = 8U;
    if (!expect(!open_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 51U,
                    sizeof(payload), &payload, &access),
                "post-publication generation corruption must fail closed"))
        return 0;
    contexts.entries[0].generation = 7U;
    const uint16_t phase = contexts.entries[0].phase;
    contexts.entries[0].phase = phase ==
            SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT
        ? SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER
        : SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT;
    if (!expect(!open_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 51U,
                    sizeof(payload), &payload, &access),
                "corrupt callback phase must fail closed"))
        return 0;
    contexts.entries[0].phase = phase;
    if (!expect(!open_wrong_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 51U,
                    sizeof(payload), &payload, &access),
                "wrong phase-specific callback opener must fail closed"))
        return 0;
    contexts.entries[0].ready = 0U;
    if (!expect(!open_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 51U,
                    sizeof(payload), &payload, &access),
                "incomplete callback publication must fail closed"))
        return 0;
    contexts.entries[0].ready = 1U;
    contexts.entries[0].sequence = 50U;
    if (!expect(!open_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 51U,
                    sizeof(payload), &payload, &access),
                "stale callback sequence must fail closed"))
        return 0;
    if (!expect(!open_phase(&job, &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, 50U,
                    sizeof(payload), &payload, &access),
                "wrong claimant must fail closed"))
        return 0;
    if (!expect(!sm64_saturn_render_callback_context_open(
                    &contexts, &queue,
                    SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 50U,
                    sizeof(payload), &payload, &access),
                "out-of-range callback identity must fail closed"))
        return 0;
    return 1;
}

int main(void)
{
    const sm64_saturn_render_job_t terrain_admit = {
        SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
        SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT, 7U, 0U, 2U, 0U, 2U};
    const sm64_saturn_render_job_t terrain = {
        SM64_SATURN_RENDER_JOB_WORLD_LOWER,
        SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER, 7U, 0U, 2U, 0U, 2U};
    const sm64_saturn_render_job_t mario_admit = {
        SM64_SATURN_RENDER_JOB_ACTOR_ADMIT,
        SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT, 7U, 0U, 2U, 0U, 2U};
    const sm64_saturn_render_job_t mario = {
        SM64_SATURN_RENDER_JOB_ACTOR_LOWER,
        SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER, 7U, 0U, 2U, 0U, 2U};
    if (!run_claim(terrain_admit, SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, 0U) ||
        !run_claim(terrain_admit, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 1U) ||
        !run_claim(terrain, SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, 0U) ||
        !run_claim(terrain, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 1U) ||
        !run_claim(mario_admit, SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, 0U) ||
        !run_claim(mario_admit, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 1U) ||
        !run_claim(mario, SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, 0U) ||
        !run_claim(mario, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, 1U) ||
        !run_identity_mutations(terrain_admit) ||
        !run_identity_mutations(terrain) ||
        !run_identity_mutations(mario_admit) ||
        !run_identity_mutations(mario))
        return 1;

    puts("render callback context fixture: PASS");
    return 0;
}
