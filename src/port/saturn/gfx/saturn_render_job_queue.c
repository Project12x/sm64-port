#include "saturn_render_job_queue.h"

#include <string.h>

#if defined(__sh__)
#include <yaul.h>
#endif

typedef struct sm64_saturn_render_job_slave_attachment {
    sm64_saturn_render_job_queue_t *queue;
    const sm64_saturn_render_job_callback_table_t *callbacks;
    void *context;
    uint32_t attached;
} sm64_saturn_render_job_slave_attachment_t;

#if defined(__sh__)
#define SM64_SATURN_RENDER_JOB_SHARED __uncached
#else
#define SM64_SATURN_RENDER_JOB_SHARED
#endif

static sm64_saturn_render_job_slave_attachment_t s_slave_attachment
    SM64_SATURN_RENDER_JOB_SHARED;

#if defined(__sh__)
static void render_job_queue_slave_entry(void)
{
    sm64_saturn_render_job_slave_attachment_t *const attachment =
        &s_slave_attachment;
    if (attachment->attached == 0U || attachment->queue == NULL)
        return;
    const uint32_t generation = attachment->queue->generation;
    if (generation != 0U)
        (void)sm64_saturn_render_job_queue_drain_slave(
            attachment->queue, generation, attachment->callbacks,
            attachment->context);
}
#endif

static inline void sm64_saturn_render_job_queue_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static inline sm64_saturn_render_job_queue_t *
sm64_saturn_render_job_queue_cache_through(sm64_saturn_render_job_queue_t *queue)
{
    if (queue == NULL) return NULL;
#if defined(__sh__)
    return (sm64_saturn_render_job_queue_t *)(CPU_CACHE_THROUGH |
                                               (uintptr_t)queue);
#else
    return queue;
#endif
}

static inline bool release_claim_try(volatile uint32_t *claim)
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

static inline void release_claim_release(volatile uint32_t *claim)
{
    sm64_saturn_render_job_queue_fence();
    *claim = 0U;
    sm64_saturn_render_job_queue_fence();
}

static bool descriptor_valid(const sm64_saturn_render_job_t *job,
                             uint32_t generation)
{
    return job != NULL && job->snapshot_generation == generation &&
        job->type >= SM64_SATURN_RENDER_JOB_WORLD_ADMIT &&
        job->type <= SM64_SATURN_RENDER_JOB_ACTOR_LOWER &&
        job->callback_id >= SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT &&
        job->callback_id <= SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER &&
        job->input_count != 0U && job->output_capacity != 0U;
}

static bool output_spans_disjoint(const sm64_saturn_render_job_t *jobs,
                                  uint16_t count)
{
    for (uint16_t left = 0U; left < count; left++) {
        const uint32_t left_begin = jobs[left].output_offset;
        const uint32_t left_end = left_begin + jobs[left].output_capacity;
        for (uint16_t right = (uint16_t)(left + 1U); right < count; right++) {
            const uint32_t right_begin = jobs[right].output_offset;
            const uint32_t right_end = right_begin + jobs[right].output_capacity;
            if (left_begin < right_end && right_begin < left_end) return false;
        }
    }
    return true;
}

void sm64_saturn_render_job_queue_init(sm64_saturn_render_job_queue_t *queue)
{
    queue = sm64_saturn_render_job_queue_cache_through(queue);
    if (queue == NULL) return;
    memset(queue, 0, sizeof(*queue));
    sm64_saturn_render_job_queue_fence();
}

bool sm64_saturn_render_job_queue_slave_attach(
    sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context)
{
    if (queue == NULL || callbacks == NULL || s_slave_attachment.attached != 0U)
        return false;
    s_slave_attachment.queue = sm64_saturn_render_job_queue_cache_through(queue);
    s_slave_attachment.callbacks = callbacks;
    s_slave_attachment.context = context;
    sm64_saturn_render_job_queue_fence();
#if defined(__sh__)
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(render_job_queue_slave_entry);
#endif
    s_slave_attachment.attached = 1U;
    sm64_saturn_render_job_queue_fence();
    return true;
}

bool sm64_saturn_render_job_queue_slave_notify(void)
{
    if (s_slave_attachment.attached == 0U ||
        s_slave_attachment.queue == NULL)
        return false;
#if defined(__sh__)
    cpu_dual_slave_notify();
#endif
    return true;
}

bool sm64_saturn_render_job_queue_publish(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    const sm64_saturn_render_job_t *jobs, uint16_t count)
{
    queue = sm64_saturn_render_job_queue_cache_through(queue);
    if (queue == NULL || jobs == NULL || generation == 0U || count == 0U ||
        count > SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY || queue->count != 0U)
        return false;
    for (uint16_t index = 0U; index < count; index++)
        if (!descriptor_valid(&jobs[index], generation)) return false;
    if (!output_spans_disjoint(jobs, count)) return false;

    /* Descriptors are complete before their release records become READY;
     * queue generation is the final publication field observed by a peer. */
    memcpy(queue->jobs, jobs, (size_t)count * sizeof(*jobs));
    for (uint16_t index = 0U; index < count; index++) {
        queue->release[index].claim = 0U;
        queue->release[index].state = SM64_SATURN_RENDER_JOB_EMPTY;
        queue->release[index].generation = generation;
    }
    queue->count = count;
    sm64_saturn_render_job_queue_fence();
    queue->generation = generation;
    sm64_saturn_render_job_queue_fence();
    for (uint16_t index = 0U; index < count; index++)
        queue->release[index].state = SM64_SATURN_RENDER_JOB_READY;
    return true;
}

static bool claim(sm64_saturn_render_job_queue_t *queue, uint32_t generation,
                  sm64_saturn_render_job_state_t claimed_state,
                  uint16_t *job_index)
{
    queue = sm64_saturn_render_job_queue_cache_through(queue);
    if (job_index != NULL) *job_index = 0U;
    if (queue == NULL || job_index == NULL || generation == 0U ||
        queue->generation != generation) return false;
    for (uint16_t index = 0U; index < queue->count; index++) {
        sm64_saturn_render_job_release_t *const release = &queue->release[index];
        if (release->state != SM64_SATURN_RENDER_JOB_READY ||
            release->generation != generation || !release_claim_try(&release->claim))
            continue;
        sm64_saturn_render_job_queue_fence();
        if (queue->generation == generation &&
            release->generation == generation &&
            release->state == SM64_SATURN_RENDER_JOB_READY) {
            release->state = claimed_state;
            sm64_saturn_render_job_queue_fence();
            release_claim_release(&release->claim);
            *job_index = index;
            return true;
        }
        release_claim_release(&release->claim);
    }
    return false;
}

bool sm64_saturn_render_job_queue_claim_master(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t *job_index)
{
    return claim(queue, generation, SM64_SATURN_RENDER_JOB_CLAIMED_MASTER,
                 job_index);
}

bool sm64_saturn_render_job_queue_claim_slave(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t *job_index)
{
    return claim(queue, generation, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE,
                 job_index);
}

static bool terminal(sm64_saturn_render_job_queue_t *queue, uint32_t generation,
                     uint16_t job_index,
                     sm64_saturn_render_job_state_t claimed_state,
                     sm64_saturn_render_job_state_t terminal_state)
{
    queue = sm64_saturn_render_job_queue_cache_through(queue);
    if (queue == NULL || generation == 0U || job_index >= queue->count ||
        queue->generation != generation) return false;
    sm64_saturn_render_job_release_t *const release = &queue->release[job_index];
    if (!release_claim_try(&release->claim)) return false;
    sm64_saturn_render_job_queue_fence();
    if (queue->generation != generation || release->generation != generation ||
        release->state != claimed_state) {
        release_claim_release(&release->claim);
        return false;
    }
    release->state = terminal_state;
    sm64_saturn_render_job_queue_fence();
    release_claim_release(&release->claim);
    return true;
}

bool sm64_saturn_render_job_queue_complete(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t job_index, sm64_saturn_render_job_state_t claimed_state)
{
    return terminal(queue, generation, job_index, claimed_state,
                    SM64_SATURN_RENDER_JOB_DONE);
}

bool sm64_saturn_render_job_queue_fail(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t job_index, sm64_saturn_render_job_state_t claimed_state)
{
    return terminal(queue, generation, job_index, claimed_state,
                    SM64_SATURN_RENDER_JOB_FAILED);
}

bool sm64_saturn_render_job_queue_all_terminal(
    const sm64_saturn_render_job_queue_t *queue, uint32_t generation)
{
    queue = sm64_saturn_render_job_queue_cache_through(
        (sm64_saturn_render_job_queue_t *)queue);
    if (queue == NULL || generation == 0U || queue->generation != generation ||
        queue->count == 0U) return false;
    for (uint16_t index = 0U; index < queue->count; index++) {
        const uint32_t state = queue->release[index].state;
        if (queue->release[index].generation != generation ||
            (state != SM64_SATURN_RENDER_JOB_DONE &&
             state != SM64_SATURN_RENDER_JOB_FAILED &&
             state != SM64_SATURN_RENDER_JOB_QUARANTINED)) return false;
    }
    return true;
}

bool sm64_saturn_render_job_queue_reset_retired(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation)
{
    uint16_t locked = 0U;

    queue = sm64_saturn_render_job_queue_cache_through(queue);
    if (!sm64_saturn_render_job_queue_all_terminal(queue, generation)) return false;
    for (; locked < queue->count; locked++)
        if (!release_claim_try(&queue->release[locked].claim)) {
            while (locked > 0U) {
                locked--;
                release_claim_release(&queue->release[locked].claim);
            }
            return false;
        }
    for (uint16_t index = 0U; index < queue->count; index++) {
        sm64_saturn_render_job_release_t *const release = &queue->release[index];
        if (release->generation != generation ||
            (release->state != SM64_SATURN_RENDER_JOB_DONE &&
             release->state != SM64_SATURN_RENDER_JOB_FAILED &&
             release->state != SM64_SATURN_RENDER_JOB_QUARANTINED)) {
            while (locked > 0U) {
                locked--;
                release_claim_release(&queue->release[locked].claim);
            }
            return false;
        }
    }
    memset(queue->jobs, 0, sizeof(queue->jobs));
    queue->count = 0U;
    sm64_saturn_render_job_queue_fence();
    queue->generation = 0U;
    for (uint16_t index = 0U; index < SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY;
         index++) {
        queue->release[index].generation = 0U;
        queue->release[index].state = SM64_SATURN_RENDER_JOB_EMPTY;
        release_claim_release(&queue->release[index].claim);
    }
    return true;
}

const sm64_saturn_render_job_t *sm64_saturn_render_job_queue_job(
    const sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t job_index)
{
    queue = sm64_saturn_render_job_queue_cache_through(
        (sm64_saturn_render_job_queue_t *)queue);
    if (queue == NULL || generation == 0U || queue->generation != generation ||
        job_index >= queue->count ||
        queue->release[job_index].generation != generation ||
        queue->release[job_index].state != SM64_SATURN_RENDER_JOB_DONE)
        return NULL;
    return &queue->jobs[job_index];
}

const sm64_saturn_render_job_t *sm64_saturn_render_job_queue_done_job(
    const sm64_saturn_render_job_queue_t *queue, uint16_t job_index)
{
    queue = sm64_saturn_render_job_queue_cache_through(
        (sm64_saturn_render_job_queue_t *)queue);
    if (queue == NULL || job_index >= queue->count || queue->generation == 0U ||
        queue->release[job_index].generation != queue->generation ||
        queue->release[job_index].state != SM64_SATURN_RENDER_JOB_DONE)
        return NULL;
    return &queue->jobs[job_index];
}

static sm64_saturn_render_job_callback_fn callback_resolve(
    const sm64_saturn_render_job_callback_table_t *callbacks,
    uint16_t callback_id)
{
    if (callbacks == NULL ||
        callback_id < SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT ||
        callback_id > SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER)
        return NULL;
    return callbacks->entries[callback_id -
                              SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT];
}

static uint16_t drain(sm64_saturn_render_job_queue_t *queue,
                      uint32_t generation,
                      sm64_saturn_render_job_state_t claimed_state,
                      const sm64_saturn_render_job_callback_table_t *callbacks,
                      void *context)
{
    uint16_t completed = 0U;
    uint16_t job_index = 0U;
    bool claimed;

    do {
        claimed = claimed_state == SM64_SATURN_RENDER_JOB_CLAIMED_MASTER
            ? sm64_saturn_render_job_queue_claim_master(queue, generation,
                                                        &job_index)
            : sm64_saturn_render_job_queue_claim_slave(queue, generation,
                                                       &job_index);
        if (!claimed) break;

        queue = sm64_saturn_render_job_queue_cache_through(queue);
        const sm64_saturn_render_job_t *const job =
            queue == NULL ? NULL : &queue->jobs[job_index];
        const sm64_saturn_render_job_callback_fn callback =
            job == NULL ? NULL : callback_resolve(callbacks, job->callback_id);
        const bool succeeded = callback != NULL &&
            callback(job, claimed_state, context);
        if (succeeded)
            (void)sm64_saturn_render_job_queue_complete(
                queue, generation, job_index, claimed_state);
        else
            (void)sm64_saturn_render_job_queue_fail(
                queue, generation, job_index, claimed_state);
        completed++;
    } while (claimed);
    return completed;
}

uint16_t sm64_saturn_render_job_queue_drain_master(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context)
{
    return drain(queue, generation, SM64_SATURN_RENDER_JOB_CLAIMED_MASTER,
                 callbacks, context);
}

uint16_t sm64_saturn_render_job_queue_drain_slave(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context)
{
    return drain(queue, generation, SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE,
                 callbacks, context);
}
