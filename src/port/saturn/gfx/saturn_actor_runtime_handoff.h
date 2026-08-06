#ifndef SM64_SATURN_ACTOR_RUNTIME_HANDOFF_H
#define SM64_SATURN_ACTOR_RUNTIME_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_actor_batch.h"

typedef enum sm64_saturn_actor_runtime_handoff_state {
    SM64_SATURN_ACTOR_HANDOFF_EMPTY = 0U,
    SM64_SATURN_ACTOR_HANDOFF_ACQUIRED = 1U,
    SM64_SATURN_ACTOR_HANDOFF_QUEUED = 2U,
    SM64_SATURN_ACTOR_HANDOFF_TERMINAL = 3U,
    SM64_SATURN_ACTOR_HANDOFF_BATCHED = 4U,
    SM64_SATURN_ACTOR_HANDOFF_CONSUMED = 5U,
    SM64_SATURN_ACTOR_HANDOFF_RETIRED = 6U,
} sm64_saturn_actor_runtime_handoff_state_t;

/* Lifecycle-only owner for one immutable Task 14 snapshot bank generation.
 * Descriptors, batch storage, and all output policy remain caller-owned. */
typedef struct sm64_saturn_actor_runtime_handoff {
    sm64_saturn_actor_instance_bank_t *bank;
    sm64_saturn_actor_instance_queue_t *queue;
    const sm64_saturn_actor_instance_snapshot_t *snapshots;
    sm64_saturn_actor_batch_t *batches;
    sm64_saturn_actor_batch_summary_t summary;
    uint32_t generation;
    uint16_t snapshot_count;
    uint16_t batch_capacity;
    uint8_t bank_index;
    uint8_t state;
    uint8_t output_consumer_acknowledged;
    uint8_t queue_published;
    uint8_t queue_reset_completed;
} sm64_saturn_actor_runtime_handoff_t;

void sm64_saturn_actor_runtime_handoff_init(
    sm64_saturn_actor_runtime_handoff_t *handoff);
bool sm64_saturn_actor_runtime_handoff_begin(
    sm64_saturn_actor_runtime_handoff_t *handoff,
    sm64_saturn_actor_instance_bank_t *bank,
    sm64_saturn_actor_instance_queue_t *queue, uint8_t bank_index,
    uint32_t generation,
    const sm64_saturn_actor_instance_descriptor_t *descriptors,
    uint16_t descriptor_count, uint16_t manifest_capacity,
    uint16_t output_record_capacity, sm64_saturn_actor_batch_t *batches,
    uint16_t batch_capacity);
bool sm64_saturn_actor_runtime_handoff_finalize(
    sm64_saturn_actor_runtime_handoff_t *handoff);
bool sm64_saturn_actor_runtime_handoff_acknowledge_consumed(
    sm64_saturn_actor_runtime_handoff_t *handoff);
bool sm64_saturn_actor_runtime_handoff_retire(
    sm64_saturn_actor_runtime_handoff_t *handoff);

#endif
