#include "saturn_actor_batch.h"

#include <string.h>

static bool batch_compatible(
    const sm64_saturn_actor_batch_t *batch,
    const sm64_saturn_actor_instance_descriptor_t *descriptor,
    uint16_t descriptor_index)
{
    return batch->first_descriptor + batch->descriptor_count ==
            descriptor_index &&
        batch->family_id == descriptor->family_id &&
        batch->material_id == descriptor->material_id &&
        batch->output_class == descriptor->output_class;
}

bool sm64_saturn_actor_batches_build(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    sm64_saturn_actor_batch_t *batches, uint16_t batch_capacity,
    sm64_saturn_actor_batch_summary_t *summary)
{
    uint16_t count, index;
    if (summary != NULL) memset(summary, 0, sizeof(*summary));
    if (queue == NULL || summary == NULL || generation == 0U ||
        (batch_capacity != 0U && batches == NULL) ||
        !sm64_saturn_actor_instance_queue_all_terminal(queue, generation) ||
        !sm64_saturn_actor_instance_queue_count(queue, generation, &count))
        return false;

    for (index = 0U; index < count; index++) {
        const sm64_saturn_actor_instance_job_state_t state =
            sm64_saturn_actor_instance_queue_state(queue, generation, index);
        if (state == SM64_SATURN_ACTOR_QUARANTINED) {
            summary->quarantined_instance_count++;
            continue;
        }
        if (state == SM64_SATURN_ACTOR_JOB_DONE) {
            const sm64_saturn_actor_instance_descriptor_t *const descriptor =
                sm64_saturn_actor_instance_queue_descriptor(
                    queue, generation, index);
            const sm64_saturn_actor_instance_result_t *const result =
                sm64_saturn_actor_instance_queue_result(
                    queue, generation, index);
            sm64_saturn_actor_batch_t *batch;
            if (descriptor == NULL || result == NULL) return false;
            if (summary->batch_count != 0U &&
                batch_compatible(&batches[summary->batch_count - 1U],
                                 descriptor, index)) {
                batch = &batches[summary->batch_count - 1U];
                batch->descriptor_count++;
            } else {
                if (summary->batch_count >= batch_capacity) return false;
                batch = &batches[summary->batch_count++];
                memset(batch, 0, sizeof(*batch));
                batch->first_descriptor = index;
                batch->descriptor_count = 1U;
                batch->family_id = descriptor->family_id;
                batch->material_id = descriptor->material_id;
                batch->output_class = descriptor->output_class;
            }
            batch->output_record_count += result->output_count;
            summary->completed_instance_count++;
            summary->output_record_count += result->output_count;
        }
    }
    return true;
}
