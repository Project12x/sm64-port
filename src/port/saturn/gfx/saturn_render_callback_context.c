#include "saturn_render_callback_context.h"

#include <string.h>

#include "saturn_dual_frame_bank.h"

void sm64_saturn_render_callback_context_bank_init(
    sm64_saturn_render_callback_context_bank_t *bank)
{
    if (bank != NULL) memset(bank, 0, sizeof(*bank));
}

bool sm64_saturn_render_callback_context_publish(
    sm64_saturn_render_callback_context_bank_t *bank,
    const sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t job_index, uint32_t sequence, uint16_t payload_bytes,
    uint8_t producer_lane)
{
    if (bank == NULL || queue == NULL || sequence == 0U ||
        payload_bytes == 0U || producer_lane > 1U)
        return false;
    const sm64_saturn_render_job_t *const job =
        sm64_saturn_render_job_queue_published_job(queue, generation,
                                                   job_index);
    if (job == NULL || job->callback_id == 0U) return false;
    sm64_saturn_render_callback_context_release_t *const release =
        (sm64_saturn_render_callback_context_release_t *)
            sm64_saturn_dual_frame_cache_through(&bank->entries[job_index]);
    release->ready = 0U;
    release->generation = generation;
    release->sequence = sequence;
    release->job_index = job_index;
    release->payload_bytes = payload_bytes;
    release->phase = job->callback_id;
    release->producer_lane = producer_lane;
    sm64_saturn_dual_frame_compiler_fence();
    release->ready = 1U;
    return true;
}

bool sm64_saturn_render_callback_context_open(
    const sm64_saturn_render_callback_context_bank_t *bank,
    const sm64_saturn_render_job_queue_t *queue, uint16_t job_index,
    sm64_saturn_render_job_state_t claimed_state, uint32_t expected_sequence,
    uint16_t expected_payload_bytes, const void *cached_payload,
    sm64_saturn_render_callback_context_access_t *access)
{
    if (access != NULL)
        *access = (sm64_saturn_render_callback_context_access_t){0};
    if (bank == NULL || queue == NULL || access == NULL ||
        cached_payload == NULL || expected_sequence == 0U ||
        expected_payload_bytes == 0U ||
        job_index >= SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY ||
        (claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
         claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE))
        return false;
    const uint8_t reader_lane =
        claimed_state == SM64_SATURN_RENDER_JOB_CLAIMED_MASTER ? 0U : 1U;
    const sm64_saturn_render_callback_context_release_t *const release =
        (const sm64_saturn_render_callback_context_release_t *)
            sm64_saturn_dual_frame_cache_through(&bank->entries[job_index]);
    if (release->ready == 0U) return false;
    sm64_saturn_dual_frame_compiler_fence();
    const sm64_saturn_render_job_t *const job =
        sm64_saturn_render_job_queue_claimed_job(
            queue, release->generation, job_index, claimed_state);
    if (job == NULL || release->job_index != job_index ||
        release->generation != job->snapshot_generation ||
        release->sequence != expected_sequence ||
        release->payload_bytes != expected_payload_bytes ||
        release->phase != job->callback_id || release->producer_lane > 1U)
        return false;
    access->payload = sm64_saturn_dual_frame_read_range(
        reader_lane, release->producer_lane, cached_payload);
    access->sequence = release->sequence;
    access->phase = release->phase;
    access->payload_bytes = release->payload_bytes;
    access->cache_through = reader_lane != release->producer_lane ? 1U : 0U;
    return access->payload != NULL;
}

static bool open_phase(
    const sm64_saturn_render_callback_context_bank_t *bank,
    const sm64_saturn_render_job_queue_t *queue, uint16_t job_index,
    sm64_saturn_render_job_state_t claimed_state, uint32_t expected_sequence,
    uint16_t expected_payload_bytes, const void *cached_payload,
    uint16_t expected_phase,
    sm64_saturn_render_callback_context_access_t *access)
{
    return sm64_saturn_render_callback_context_open(
               bank, queue, job_index, claimed_state, expected_sequence,
               expected_payload_bytes, cached_payload, access) &&
        access->phase == expected_phase;
}

#define SM64_SATURN_DEFINE_CONTEXT_PHASE_OPEN(name, phase_id) \
bool sm64_saturn_render_callback_context_open_##name( \
    const sm64_saturn_render_callback_context_bank_t *bank, \
    const sm64_saturn_render_job_queue_t *queue, uint16_t job_index, \
    sm64_saturn_render_job_state_t claimed_state, uint32_t expected_sequence, \
    uint16_t expected_payload_bytes, const void *cached_payload, \
    sm64_saturn_render_callback_context_access_t *access) \
{ \
    return open_phase(bank, queue, job_index, claimed_state, \
                      expected_sequence, expected_payload_bytes, \
                      cached_payload, phase_id, access); \
}

SM64_SATURN_DEFINE_CONTEXT_PHASE_OPEN(
    world_admit, SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT)
SM64_SATURN_DEFINE_CONTEXT_PHASE_OPEN(
    world_lower, SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER)
SM64_SATURN_DEFINE_CONTEXT_PHASE_OPEN(
    actor_admit, SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT)
SM64_SATURN_DEFINE_CONTEXT_PHASE_OPEN(
    actor_lower, SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER)
#undef SM64_SATURN_DEFINE_CONTEXT_PHASE_OPEN
