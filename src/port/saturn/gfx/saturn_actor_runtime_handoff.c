#include "saturn_actor_runtime_handoff.h"

#include <string.h>

static bool descriptors_match_snapshot(
    const sm64_saturn_actor_instance_snapshot_t *snapshots,
    uint16_t snapshot_count,
    const sm64_saturn_actor_instance_descriptor_t *descriptors,
    uint16_t descriptor_count)
{
    uint16_t index;
    for (index = 0U; index < descriptor_count; index++) {
        sm64_saturn_actor_instance_descriptor_t expected;
        const sm64_saturn_actor_instance_descriptor_t *const supplied =
            &descriptors[index];
        if (supplied->snapshot_index >= snapshot_count ||
            !sm64_saturn_actor_instance_descriptor_from_snapshot(
                &snapshots[supplied->snapshot_index], supplied->snapshot_index,
                supplied->source_order, supplied->material_id,
                supplied->output_class, supplied->output_offset,
                supplied->output_capacity, &expected) ||
            memcmp(&expected, supplied, sizeof(expected)) != 0)
            return false;
    }
    return true;
}

static void quarantine_after_acquire(
    sm64_saturn_actor_runtime_handoff_t *handoff)
{
    if (handoff->bank != NULL)
        (void)sm64_saturn_actor_instance_bank_quarantine(
            handoff->bank, handoff->generation);
    handoff->state = SM64_SATURN_ACTOR_HANDOFF_EMPTY;
    handoff->snapshots = NULL;
    handoff->queue_published = 0U;
}

void sm64_saturn_actor_runtime_handoff_init(
    sm64_saturn_actor_runtime_handoff_t *handoff)
{
    if (handoff != NULL) memset(handoff, 0, sizeof(*handoff));
}

bool sm64_saturn_actor_runtime_handoff_begin(
    sm64_saturn_actor_runtime_handoff_t *handoff,
    sm64_saturn_actor_instance_bank_t *bank,
    sm64_saturn_actor_instance_queue_t *queue, uint8_t bank_index,
    uint32_t generation,
    const sm64_saturn_actor_instance_descriptor_t *descriptors,
    uint16_t descriptor_count, uint16_t manifest_capacity,
    uint16_t output_record_capacity, sm64_saturn_actor_batch_t *batches,
    uint16_t batch_capacity)
{
    uint16_t snapshot_count = 0U;
    const sm64_saturn_actor_instance_snapshot_t *snapshots;
    if (handoff == NULL || bank == NULL || queue == NULL || generation == 0U ||
        bank_index >= 2U || handoff->state != SM64_SATURN_ACTOR_HANDOFF_EMPTY ||
        descriptor_count > manifest_capacity ||
        manifest_capacity == 0U ||
        manifest_capacity > SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE ||
        output_record_capacity > SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING ||
        (descriptor_count != 0U && descriptors == NULL) ||
        (batch_capacity != 0U && batches == NULL) ||
        batch_capacity < descriptor_count)
        return false;

    snapshots = sm64_saturn_actor_instance_bank_acquire(
        bank, bank_index, generation, &snapshot_count);
    if (snapshots == NULL) return false;

    handoff->bank = bank;
    handoff->queue = queue;
    handoff->snapshots = snapshots;
    handoff->batches = batches;
    handoff->generation = generation;
    handoff->snapshot_count = snapshot_count;
    handoff->batch_capacity = batch_capacity;
    handoff->bank_index = bank_index;
    handoff->state = SM64_SATURN_ACTOR_HANDOFF_ACQUIRED;
    if (snapshot_count > manifest_capacity || descriptor_count > snapshot_count ||
        !descriptors_match_snapshot(snapshots, snapshot_count, descriptors,
                                    descriptor_count)) {
        quarantine_after_acquire(handoff);
        return false;
    }
    if (!sm64_saturn_actor_instance_queue_publish(
            queue, generation, manifest_capacity, output_record_capacity,
            descriptors, descriptor_count)) {
        quarantine_after_acquire(handoff);
        return false;
    }
    handoff->queue_published = 1U;
    handoff->state = SM64_SATURN_ACTOR_HANDOFF_QUEUED;
    return true;
}

bool sm64_saturn_actor_runtime_handoff_finalize(
    sm64_saturn_actor_runtime_handoff_t *handoff)
{
    if (handoff == NULL ||
        (handoff->state != SM64_SATURN_ACTOR_HANDOFF_QUEUED &&
         handoff->state != SM64_SATURN_ACTOR_HANDOFF_TERMINAL))
        return false;
    if (handoff->state == SM64_SATURN_ACTOR_HANDOFF_QUEUED) {
        if (!sm64_saturn_actor_instance_queue_all_terminal(
                handoff->queue, handoff->generation))
            return false;
        handoff->state = SM64_SATURN_ACTOR_HANDOFF_TERMINAL;
    }
    if (!sm64_saturn_actor_batches_build(
            handoff->queue, handoff->generation, handoff->batches,
            handoff->batch_capacity, &handoff->summary)) {
        return false;
    }
    if (!sm64_saturn_actor_instance_bank_complete(
            handoff->bank, handoff->bank_index)) {
        return false;
    }
    handoff->state = SM64_SATURN_ACTOR_HANDOFF_BATCHED;
    return true;
}

bool sm64_saturn_actor_runtime_handoff_acknowledge_consumed(
    sm64_saturn_actor_runtime_handoff_t *handoff)
{
    if (handoff == NULL || handoff->state != SM64_SATURN_ACTOR_HANDOFF_BATCHED ||
        handoff->output_consumer_acknowledged != 0U)
        return false;
    handoff->output_consumer_acknowledged = 1U;
    handoff->state = SM64_SATURN_ACTOR_HANDOFF_CONSUMED;
    return true;
}

bool sm64_saturn_actor_runtime_handoff_retire(
    sm64_saturn_actor_runtime_handoff_t *handoff)
{
    if (handoff == NULL || handoff->state != SM64_SATURN_ACTOR_HANDOFF_CONSUMED ||
        handoff->output_consumer_acknowledged == 0U)
        return false;
    if (!handoff->queue_reset_completed &&
        !sm64_saturn_actor_instance_queue_reset_retired(
            handoff->queue, handoff->generation))
        return false;
    handoff->queue_reset_completed = 1U;
    if (!sm64_saturn_actor_instance_bank_retire(
            handoff->bank, handoff->bank_index))
        return false;
    handoff->state = SM64_SATURN_ACTOR_HANDOFF_RETIRED;
    return true;
}
