/* Master-only stable actor-instance batch construction. */
#ifndef SM64_SATURN_ACTOR_BATCH_H
#define SM64_SATURN_ACTOR_BATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_actor_instance_queue.h"

typedef struct sm64_saturn_actor_batch {
    uint16_t first_descriptor;
    uint16_t descriptor_count;
    uint16_t family_id;
    uint16_t material_id;
    uint32_t output_record_count;
    uint8_t output_class;
    uint8_t reserved[3];
} sm64_saturn_actor_batch_t;

typedef struct sm64_saturn_actor_batch_summary {
    uint16_t batch_count;
    uint16_t completed_instance_count;
    uint16_t quarantined_instance_count;
    uint16_t reserved;
    uint32_t output_record_count;
} sm64_saturn_actor_batch_summary_t;

_Static_assert(sizeof(sm64_saturn_actor_batch_t) == 16U,
               "actor batch ABI changed");

/* The queue must be terminal. DONE descriptors are visited in published
 * source/painter order; quarantined descriptors are skipped without moving
 * either neighbor. Only adjacent compatible descriptors share a batch. */
bool sm64_saturn_actor_batches_build(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    sm64_saturn_actor_batch_t *batches, uint16_t batch_capacity,
    sm64_saturn_actor_batch_summary_t *summary);

#endif
