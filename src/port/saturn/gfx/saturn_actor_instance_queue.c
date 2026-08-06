#include "saturn_actor_instance_queue.h"

#include <string.h>

#if defined(__sh__)
#include <cpu/cache.h>
#endif

/* Pattern-only reuse: publication ordering, TAS.B ownership, and terminal
 * retirement mirror this repository's proven saturn_render_job_queue.c. */
static inline void actor_queue_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static inline sm64_saturn_actor_instance_queue_t *actor_queue_uncached(
    sm64_saturn_actor_instance_queue_t *queue)
{
    if (queue == NULL) return NULL;
#if defined(__sh__)
    return (sm64_saturn_actor_instance_queue_t *)(CPU_CACHE_THROUGH |
                                                   (uintptr_t)queue);
#else
    return queue;
#endif
}

static bool actor_claim_try(volatile uint32_t *claim)
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

static void actor_claim_release(volatile uint32_t *claim)
{
    actor_queue_fence();
    *claim = 0U;
    actor_queue_fence();
}

static bool hash_nonzero(const uint32_t hash[8])
{
    uint32_t value = 0U;
    uint16_t index;
    for (index = 0U; index < 8U; index++) value |= hash[index];
    return value != 0U;
}

static bool hash_equal(const uint32_t left[8], const uint32_t right[8])
{
    uint16_t index;
    for (index = 0U; index < 8U; index++)
        if (left[index] != right[index]) return false;
    return true;
}

static bool output_class_valid(uint8_t output_class)
{
    return output_class == SM64_SATURN_ACTOR_OUTPUT_OPAQUE ||
           output_class == SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT;
}

bool sm64_saturn_actor_instance_descriptor_from_snapshot(
    const sm64_saturn_actor_instance_snapshot_t *snapshot,
    uint16_t snapshot_index, uint16_t source_order, uint16_t material_id,
    uint8_t output_class, uint16_t output_offset, uint16_t output_capacity,
    sm64_saturn_actor_instance_descriptor_t *descriptor)
{
    uint16_t word;
    const uint32_t output_end = (uint32_t)output_offset + output_capacity;
    if (descriptor != NULL) memset(descriptor, 0, sizeof(*descriptor));
    if (snapshot == NULL || descriptor == NULL || snapshot->generation == 0U ||
        snapshot->scene_package_generation == 0U ||
        snapshot->instance_key == 0U || snapshot->actor_bank_id == 0U ||
        snapshot->family_id == 0U ||
        snapshot->model_id == SM64_SATURN_ACTOR_INSTANCE_MODEL_NONE ||
        snapshot->active == 0U || snapshot->render_active == 0U ||
        !hash_nonzero(snapshot->actor_bank_hash_words) ||
        !output_class_valid(output_class) || output_capacity == 0U ||
        output_end > SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING)
        return false;

    descriptor->generation = snapshot->generation;
    descriptor->scene_package_generation =
        snapshot->scene_package_generation;
    descriptor->instance_key = snapshot->instance_key;
    descriptor->actor_bank_id = snapshot->actor_bank_id;
    for (word = 0U; word < 8U; word++)
        descriptor->actor_bank_hash_words[word] =
            snapshot->actor_bank_hash_words[word];
    descriptor->family_id = snapshot->family_id;
    descriptor->model_id = snapshot->model_id;
    descriptor->snapshot_index = snapshot_index;
    descriptor->source_order = source_order;
    descriptor->material_id = material_id;
    descriptor->output_offset = output_offset;
    descriptor->output_capacity = output_capacity;
    descriptor->output_class = output_class;
    return true;
}

void sm64_saturn_actor_instance_queue_init(
    sm64_saturn_actor_instance_queue_t *queue)
{
    queue = actor_queue_uncached(queue);
    if (queue == NULL) return;
    memset(queue, 0, sizeof(*queue));
    actor_queue_fence();
}

static bool descriptor_valid(
    const sm64_saturn_actor_instance_descriptor_t *descriptor,
    uint32_t generation, uint16_t manifest_capacity,
    uint16_t output_record_capacity)
{
    const uint32_t output_end = (uint32_t)descriptor->output_offset +
                                descriptor->output_capacity;
    return descriptor->generation == generation &&
        descriptor->scene_package_generation != 0U &&
        descriptor->instance_key != 0U && descriptor->actor_bank_id != 0U &&
        descriptor->family_id != 0U &&
        descriptor->model_id != SM64_SATURN_ACTOR_INSTANCE_MODEL_NONE &&
        descriptor->snapshot_index < manifest_capacity &&
        descriptor->output_capacity != 0U && descriptor->reserved == 0U &&
        hash_nonzero(descriptor->actor_bank_hash_words) &&
        output_class_valid(descriptor->output_class) &&
        output_end <= output_record_capacity;
}

static bool descriptors_disjoint_and_unique(
    const sm64_saturn_actor_instance_descriptor_t *descriptors,
    uint16_t count)
{
    uint16_t left, right;
    for (left = 0U; left < count; left++) {
        const uint32_t left_end = (uint32_t)descriptors[left].output_offset +
                                  descriptors[left].output_capacity;
        if (left != 0U &&
            descriptors[left - 1U].source_order >= descriptors[left].source_order)
            return false;
        for (right = (uint16_t)(left + 1U); right < count; right++) {
            const uint32_t right_end =
                (uint32_t)descriptors[right].output_offset +
                descriptors[right].output_capacity;
            if (descriptors[left].instance_key ==
                    descriptors[right].instance_key ||
                descriptors[left].snapshot_index ==
                    descriptors[right].snapshot_index)
                return false;
            if (descriptors[left].output_offset < right_end &&
                descriptors[right].output_offset < left_end)
                return false;
        }
    }
    return true;
}

bool sm64_saturn_actor_instance_queue_publish(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t manifest_capacity, uint16_t output_record_capacity,
    const sm64_saturn_actor_instance_descriptor_t *descriptors,
    uint16_t count)
{
    uint16_t index;
    queue = actor_queue_uncached(queue);
    if (queue == NULL || generation == 0U || queue->generation != 0U ||
        manifest_capacity == 0U ||
        manifest_capacity > SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE ||
        output_record_capacity > SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING ||
        count > manifest_capacity || (count != 0U && descriptors == NULL))
        return false;
    for (index = 0U; index < count; index++)
        if (!descriptor_valid(&descriptors[index], generation,
                              manifest_capacity, output_record_capacity))
            return false;
    if (!descriptors_disjoint_and_unique(descriptors, count)) return false;

    if (count != 0U)
        memcpy(queue->descriptors, descriptors,
               (size_t)count * sizeof(*descriptors));
    memset(queue->results, 0, sizeof(queue->results));
    for (index = 0U; index < count; index++) {
        queue->release[index].claim = 0U;
        queue->release[index].state = SM64_SATURN_ACTOR_JOB_EMPTY;
        queue->release[index].generation = generation;
    }
    queue->count = count;
    queue->manifest_capacity = manifest_capacity;
    queue->output_record_capacity = output_record_capacity;
    actor_queue_fence();
    queue->generation = generation;
    actor_queue_fence();
    for (index = 0U; index < count; index++)
        queue->release[index].state = SM64_SATURN_ACTOR_JOB_READY;
    actor_queue_fence();
    return true;
}

static bool claim(sm64_saturn_actor_instance_queue_t *queue,
                  uint32_t generation, uint32_t claimed_state,
                  uint16_t *descriptor_index)
{
    uint16_t index;
    queue = actor_queue_uncached(queue);
    if (descriptor_index != NULL) *descriptor_index = UINT16_MAX;
    if (queue == NULL || descriptor_index == NULL || generation == 0U ||
        queue->generation != generation) return false;
    for (index = 0U; index < queue->count; index++) {
        sm64_saturn_actor_instance_release_t *const release =
            &queue->release[index];
        if (release->generation != generation ||
            release->state != SM64_SATURN_ACTOR_JOB_READY ||
            !actor_claim_try(&release->claim))
            continue;
        actor_queue_fence();
        if (queue->generation == generation &&
            release->generation == generation &&
            release->state == SM64_SATURN_ACTOR_JOB_READY) {
            release->state = claimed_state;
            actor_queue_fence();
            actor_claim_release(&release->claim);
            *descriptor_index = index;
            return true;
        }
        actor_claim_release(&release->claim);
    }
    return false;
}

bool sm64_saturn_actor_instance_queue_claim_master(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t *descriptor_index)
{
    return claim(queue, generation, SM64_SATURN_ACTOR_JOB_CLAIMED_MASTER,
                 descriptor_index);
}

bool sm64_saturn_actor_instance_queue_claim_slave(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t *descriptor_index)
{
    return claim(queue, generation, SM64_SATURN_ACTOR_JOB_CLAIMED_SLAVE,
                 descriptor_index);
}

static uint32_t state_for_lane(uint8_t lane)
{
    if (lane == SM64_SATURN_ACTOR_CLAIMED_MASTER)
        return SM64_SATURN_ACTOR_JOB_CLAIMED_MASTER;
    if (lane == SM64_SATURN_ACTOR_CLAIMED_SLAVE)
        return SM64_SATURN_ACTOR_JOB_CLAIMED_SLAVE;
    return SM64_SATURN_ACTOR_JOB_EMPTY;
}

static uint8_t identity_reason(
    const sm64_saturn_actor_instance_descriptor_t *descriptor,
    const sm64_saturn_actor_instance_snapshot_t *snapshot,
    uint16_t output_count)
{
    if (snapshot == NULL || snapshot->generation != descriptor->generation)
        return SM64_SATURN_ACTOR_QUARANTINE_STALE_GENERATION;
    if (snapshot->scene_package_generation !=
        descriptor->scene_package_generation)
        return SM64_SATURN_ACTOR_QUARANTINE_STALE_PACKAGE_GENERATION;
    if (snapshot->instance_key != descriptor->instance_key)
        return SM64_SATURN_ACTOR_QUARANTINE_STALE_INSTANCE;
    if (snapshot->actor_bank_id != descriptor->actor_bank_id ||
        snapshot->family_id != descriptor->family_id ||
        snapshot->model_id != descriptor->model_id ||
        !hash_equal(snapshot->actor_bank_hash_words,
                    descriptor->actor_bank_hash_words))
        return SM64_SATURN_ACTOR_QUARANTINE_STALE_BANK;
    if (output_count > descriptor->output_capacity)
        return SM64_SATURN_ACTOR_QUARANTINE_OUTPUT_OVERFLOW;
    return SM64_SATURN_ACTOR_QUARANTINE_NONE;
}

static bool terminal(sm64_saturn_actor_instance_queue_t *queue,
                     uint32_t generation, uint16_t descriptor_index,
                     uint8_t lane, uint8_t reason, uint16_t output_count)
{
    sm64_saturn_actor_instance_release_t *release;
    sm64_saturn_actor_instance_result_t *result;
    const uint32_t claimed_state = state_for_lane(lane);
    queue = actor_queue_uncached(queue);
    if (queue == NULL || generation == 0U ||
        queue->generation != generation || descriptor_index >= queue->count ||
        claimed_state == SM64_SATURN_ACTOR_JOB_EMPTY)
        return false;
    release = &queue->release[descriptor_index];
    if (!actor_claim_try(&release->claim)) return false;
    actor_queue_fence();
    if (release->generation != generation ||
        release->state != claimed_state) {
        actor_claim_release(&release->claim);
        return false;
    }
    result = &queue->results[descriptor_index];
    result->generation = generation;
    result->instance_key = queue->descriptors[descriptor_index].instance_key;
    result->output_count = reason == SM64_SATURN_ACTOR_QUARANTINE_NONE
        ? output_count : 0U;
    result->lane = lane;
    result->reason = reason;
    actor_queue_fence();
    release->state = reason == SM64_SATURN_ACTOR_QUARANTINE_NONE
        ? SM64_SATURN_ACTOR_JOB_DONE : SM64_SATURN_ACTOR_QUARANTINED;
    actor_queue_fence();
    actor_claim_release(&release->claim);
    return true;
}

bool sm64_saturn_actor_instance_queue_complete(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index, uint8_t lane,
    const sm64_saturn_actor_instance_snapshot_t *observed_snapshot,
    uint16_t output_count)
{
    const sm64_saturn_actor_instance_descriptor_t *descriptor =
        sm64_saturn_actor_instance_queue_claimed_descriptor(
            queue, generation, descriptor_index, lane);
    if (descriptor == NULL) return false;
    const uint8_t reason = identity_reason(
        descriptor, observed_snapshot, output_count);
    return terminal(queue, generation, descriptor_index, lane, reason,
                    output_count) &&
        reason == SM64_SATURN_ACTOR_QUARANTINE_NONE;
}

bool sm64_saturn_actor_instance_queue_fail(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index, uint8_t lane, uint8_t reason)
{
    if (reason == SM64_SATURN_ACTOR_QUARANTINE_NONE ||
        reason > SM64_SATURN_ACTOR_QUARANTINE_OUTPUT_OVERFLOW)
        return false;
    return terminal(queue, generation, descriptor_index, lane, reason, 0U);
}

static uint16_t drain(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint8_t lane, const sm64_saturn_actor_instance_processor_t *processor)
{
    uint16_t claimed_count = 0U;
    uint16_t descriptor_index;
    bool claimed;
    if (processor == NULL || processor->snapshots == NULL ||
        processor->snapshot_count == 0U || processor->generation != generation ||
        processor->reserved != 0U || processor->process == NULL)
        return 0U;

    do {
        claimed = lane == SM64_SATURN_ACTOR_CLAIMED_MASTER
            ? sm64_saturn_actor_instance_queue_claim_master(
                queue, generation, &descriptor_index)
            : sm64_saturn_actor_instance_queue_claim_slave(
                queue, generation, &descriptor_index);
        if (claimed) {
            const sm64_saturn_actor_instance_descriptor_t *const descriptor =
                sm64_saturn_actor_instance_queue_claimed_descriptor(
                    queue, generation, descriptor_index, lane);
            uint16_t output_count = 0U;
            claimed_count++;
            if (descriptor == NULL ||
                descriptor->snapshot_index >= processor->snapshot_count) {
                (void)terminal(
                    queue, generation, descriptor_index, lane,
                    SM64_SATURN_ACTOR_QUARANTINE_STALE_INSTANCE, 0U);
                continue;
            }
            const sm64_saturn_actor_instance_snapshot_t *const snapshot =
                &processor->snapshots[descriptor->snapshot_index];
            const uint8_t reason = identity_reason(descriptor, snapshot, 0U);
            if (reason != SM64_SATURN_ACTOR_QUARANTINE_NONE) {
                (void)terminal(queue, generation, descriptor_index, lane,
                               reason, 0U);
                continue;
            }
            if (!processor->process(descriptor, snapshot, lane,
                                    processor->context, &output_count)) {
                (void)sm64_saturn_actor_instance_queue_fail(
                    queue, generation, descriptor_index, lane,
                    SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE);
                continue;
            }
            (void)sm64_saturn_actor_instance_queue_complete(
                queue, generation, descriptor_index, lane, snapshot,
                output_count);
        }
    } while (claimed);
    return claimed_count;
}

uint16_t sm64_saturn_actor_instance_queue_drain_master(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    const sm64_saturn_actor_instance_processor_t *processor)
{
    return drain(queue, generation, SM64_SATURN_ACTOR_CLAIMED_MASTER,
                 processor);
}

uint16_t sm64_saturn_actor_instance_queue_drain_slave(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    const sm64_saturn_actor_instance_processor_t *processor)
{
    return drain(queue, generation, SM64_SATURN_ACTOR_CLAIMED_SLAVE,
                 processor);
}

sm64_saturn_actor_instance_job_state_t
sm64_saturn_actor_instance_queue_state(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index)
{
    queue = actor_queue_uncached((sm64_saturn_actor_instance_queue_t *)queue);
    if (queue == NULL || generation == 0U ||
        queue->generation != generation || descriptor_index >= queue->count ||
        queue->release[descriptor_index].generation != generation)
        return SM64_SATURN_ACTOR_JOB_EMPTY;
    return (sm64_saturn_actor_instance_job_state_t)
        queue->release[descriptor_index].state;
}

const sm64_saturn_actor_instance_descriptor_t *
sm64_saturn_actor_instance_queue_descriptor(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index)
{
    queue = actor_queue_uncached((sm64_saturn_actor_instance_queue_t *)queue);
    if (queue == NULL || generation == 0U ||
        queue->generation != generation || descriptor_index >= queue->count ||
        queue->release[descriptor_index].generation != generation)
        return NULL;
    return &queue->descriptors[descriptor_index];
}

const sm64_saturn_actor_instance_descriptor_t *
sm64_saturn_actor_instance_queue_claimed_descriptor(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index, uint8_t lane)
{
    if (sm64_saturn_actor_instance_queue_state(
            queue, generation, descriptor_index) != state_for_lane(lane))
        return NULL;
    return sm64_saturn_actor_instance_queue_descriptor(
        queue, generation, descriptor_index);
}

const sm64_saturn_actor_instance_result_t *
sm64_saturn_actor_instance_queue_result(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index)
{
    const sm64_saturn_actor_instance_job_state_t state =
        sm64_saturn_actor_instance_queue_state(
            queue, generation, descriptor_index);
    queue = actor_queue_uncached((sm64_saturn_actor_instance_queue_t *)queue);
    if (queue == NULL || (state != SM64_SATURN_ACTOR_JOB_DONE &&
                          state != SM64_SATURN_ACTOR_QUARANTINED))
        return NULL;
    actor_queue_fence();
    if (queue->results[descriptor_index].generation != generation) return NULL;
    return &queue->results[descriptor_index];
}

bool sm64_saturn_actor_instance_queue_count(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t *count)
{
    queue = actor_queue_uncached((sm64_saturn_actor_instance_queue_t *)queue);
    if (count != NULL) *count = 0U;
    if (queue == NULL || count == NULL || generation == 0U ||
        queue->generation != generation)
        return false;
    actor_queue_fence();
    *count = queue->count;
    return *count <= SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE;
}

bool sm64_saturn_actor_instance_queue_all_terminal(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation)
{
    uint16_t index;
    queue = actor_queue_uncached((sm64_saturn_actor_instance_queue_t *)queue);
    if (queue == NULL || generation == 0U || queue->generation != generation)
        return false;
    for (index = 0U; index < queue->count; index++) {
        const uint32_t state = queue->release[index].state;
        if (queue->release[index].generation != generation ||
            (state != SM64_SATURN_ACTOR_JOB_DONE &&
             state != SM64_SATURN_ACTOR_QUARANTINED))
            return false;
    }
    return true;
}

bool sm64_saturn_actor_instance_queue_reset_retired(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation)
{
    uint16_t index, locked = 0U;
    queue = actor_queue_uncached(queue);
    if (!sm64_saturn_actor_instance_queue_all_terminal(queue, generation))
        return false;
    for (; locked < queue->count; locked++)
        if (!actor_claim_try(&queue->release[locked].claim)) {
            while (locked != 0U)
                actor_claim_release(&queue->release[--locked].claim);
            return false;
        }
    memset(queue->descriptors, 0, sizeof(queue->descriptors));
    memset(queue->results, 0, sizeof(queue->results));
    queue->count = 0U;
    queue->manifest_capacity = 0U;
    queue->output_record_capacity = 0U;
    actor_queue_fence();
    queue->generation = 0U;
    for (index = 0U; index < locked; index++) {
        queue->release[index].generation = 0U;
        queue->release[index].state = SM64_SATURN_ACTOR_JOB_EMPTY;
        actor_claim_release(&queue->release[index].claim);
    }
    return true;
}

void sm64_saturn_actor_instance_queue_memory_report(
    sm64_saturn_actor_instance_queue_memory_report_t *report)
{
    if (report == NULL) return;
    report->descriptor_bytes = sizeof(
        ((sm64_saturn_actor_instance_queue_t *)0)->descriptors);
    report->release_bytes = sizeof(
        ((sm64_saturn_actor_instance_queue_t *)0)->release);
    report->result_bytes = sizeof(
        ((sm64_saturn_actor_instance_queue_t *)0)->results);
    report->queue_bytes = sizeof(sm64_saturn_actor_instance_queue_t);
    report->actor_bank_bytes = sizeof(sm64_saturn_actor_instance_bank_t);
    report->observer_bytes = sizeof(sm64_saturn_geo_state_observer_t);
    report->batch_bytes = SM64_SATURN_ACTOR_BATCH_STORAGE_BYTES;
    report->output_storage_bytes =
        SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING *
        sizeof(sm64_saturn_actor_output_record_t);
    report->alignment_padding_bytes = SM64_SATURN_ACTOR_RUNTIME_BYTES -
        (report->actor_bank_bytes + report->observer_bytes +
         report->queue_bytes + report->batch_bytes +
         report->output_storage_bytes);
    report->runtime_bytes = SM64_SATURN_ACTOR_RUNTIME_BYTES;
    report->lwram_budget_bytes = SM64_SATURN_ACTOR_RUNTIME_LWRAM_BUDGET;
    report->capacity = SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE;
    report->output_record_ceiling =
        SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING;
    report->output_record_bytes = sizeof(sm64_saturn_actor_output_record_t);
    report->runtime_alignment = SM64_SATURN_ACTOR_RUNTIME_ALIGNMENT;
}
