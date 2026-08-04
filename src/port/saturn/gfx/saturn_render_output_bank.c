#include "saturn_render_output_bank.h"

#include <string.h>

static inline void output_bank_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static inline sm64_saturn_render_output_bank_t *output_bank_cache_through(
    sm64_saturn_render_output_bank_t *bank)
{
    return (sm64_saturn_render_output_bank_t *)
        sm64_saturn_dual_frame_cache_through(bank);
}

static bool output_claim_try(volatile uint32_t *claim)
{
    if (claim == NULL) return false;
#if defined(__sh__)
    uint32_t acquired;
    __asm__ volatile("tas.b @%1\n\tmovt %0" : "=r" (acquired)
                     : "r" (claim) : "memory");
    return acquired != 0U;
#else
    return __sync_bool_compare_and_swap(claim, 0U, 1U);
#endif
}

static void output_claim_release(volatile uint32_t *claim)
{
    output_bank_fence();
    *claim = 0U;
    output_bank_fence();
}

static bool output_kind_valid(sm64_saturn_render_output_bank_kind_t kind)
{
    return kind == SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN ||
        kind == SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR;
}

static bool output_claimed_state_valid(
    sm64_saturn_render_job_state_t claimed_state)
{
    return claimed_state == SM64_SATURN_RENDER_JOB_CLAIMED_MASTER ||
        claimed_state == SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE;
}

static uint8_t output_lane_from_claim(
    sm64_saturn_render_job_state_t claimed_state)
{
    return claimed_state == SM64_SATURN_RENDER_JOB_CLAIMED_MASTER
        ? SM64_SATURN_RENDER_OUTPUT_LANE_MASTER
        : SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE;
}

void sm64_saturn_render_output_bank_init(
    sm64_saturn_render_output_bank_t *bank,
    sm64_saturn_render_output_bank_kind_t kind)
{
    bank = output_bank_cache_through(bank);
    if (bank == NULL || !output_kind_valid(kind)) return;
    memset(bank, 0, sizeof(*bank));
    output_bank_fence();
    bank->kind = (uint32_t)kind;
    output_bank_fence();
}

sm64_saturn_render_output_bank_kind_t
sm64_saturn_render_output_bank_kind_for_job(
    const sm64_saturn_render_job_t *job)
{
    if (job == NULL) return SM64_SATURN_RENDER_OUTPUT_BANK_INVALID;
    switch ((sm64_saturn_render_job_type_t)job->type) {
    case SM64_SATURN_RENDER_JOB_WORLD_ADMIT:
    case SM64_SATURN_RENDER_JOB_WORLD_LOWER:
        return SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN;
    case SM64_SATURN_RENDER_JOB_ACTOR_ADMIT:
    case SM64_SATURN_RENDER_JOB_ACTOR_LOWER:
        return SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR;
    default:
        return SM64_SATURN_RENDER_OUTPUT_BANK_INVALID;
    }
}

bool sm64_saturn_render_output_bank_publish(
    sm64_saturn_render_output_bank_t *bank,
    const sm64_saturn_render_job_t *job, uint16_t job_index,
    sm64_saturn_render_job_state_t claimed_state)
{
    bank = output_bank_cache_through(bank);
    const sm64_saturn_render_output_bank_kind_t kind =
        sm64_saturn_render_output_bank_kind_for_job(job);
    if (bank == NULL || job == NULL || job->snapshot_generation == 0U ||
        job_index >= SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY ||
        !output_claimed_state_valid(claimed_state) ||
        !output_kind_valid(kind) || bank->kind != (uint32_t)kind)
        return false;

    sm64_saturn_render_output_release_t *const release =
        &bank->release[job_index];
    if (!output_claim_try(&release->claim)) return false;
    output_bank_fence();
    if (release->ready != 0U) {
        output_claim_release(&release->claim);
        return false;
    }
    release->generation = job->snapshot_generation;
    release->job_index = job_index;
    release->claimed_state = (uint32_t)claimed_state;
    output_bank_fence();
    release->ready = 1U;
    output_claim_release(&release->claim);
    return true;
}

bool sm64_saturn_render_output_bank_owner_lane(
    const sm64_saturn_render_output_bank_t *bank,
    const sm64_saturn_render_job_t *job, uint16_t job_index,
    uint8_t *owner_lane)
{
    if (owner_lane != NULL) *owner_lane = UINT8_MAX;
    bank = output_bank_cache_through((sm64_saturn_render_output_bank_t *)bank);
    const sm64_saturn_render_output_bank_kind_t kind =
        sm64_saturn_render_output_bank_kind_for_job(job);
    if (bank == NULL || job == NULL || owner_lane == NULL ||
        job->snapshot_generation == 0U ||
        job_index >= SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY ||
        !output_kind_valid(kind) || bank->kind != (uint32_t)kind)
        return false;
    const sm64_saturn_render_output_release_t *const release =
        &bank->release[job_index];
    if (release->ready == 0U) return false;
    output_bank_fence();
    const sm64_saturn_render_job_state_t claimed_state =
        (sm64_saturn_render_job_state_t)release->claimed_state;
    if (release->generation != job->snapshot_generation ||
        release->job_index != job_index || !output_claimed_state_valid(claimed_state))
        return false;
    output_bank_fence();
    *owner_lane = output_lane_from_claim(claimed_state);
    return true;
}

bool sm64_saturn_render_output_bank_reader_needs_cache_through(
    const sm64_saturn_render_output_bank_t *bank,
    const sm64_saturn_render_job_t *job, uint16_t job_index,
    uint8_t reader_lane)
{
    uint8_t owner_lane;
    if (reader_lane > SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE ||
        !sm64_saturn_render_output_bank_owner_lane(bank, job, job_index,
                                                   &owner_lane))
        return false;
    return reader_lane != owner_lane;
}

const void *sm64_saturn_render_output_bank_read_range(
    const sm64_saturn_render_output_bank_t *bank,
    const sm64_saturn_render_job_t *job, uint16_t job_index,
    uint8_t reader_lane, const void *cached)
{
    uint8_t owner_lane;
    if (cached == NULL || reader_lane > SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE ||
        !sm64_saturn_render_output_bank_owner_lane(bank, job, job_index,
                                                   &owner_lane))
        return NULL;
    return sm64_saturn_dual_frame_read_range(reader_lane, owner_lane, cached);
}
