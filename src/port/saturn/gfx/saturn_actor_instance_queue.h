/* Dedicated bounded actor-instance work queue shared by the two SH-2s. */
#ifndef SM64_SATURN_ACTOR_INSTANCE_QUEUE_H
#define SM64_SATURN_ACTOR_INSTANCE_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_actor_instance.h"

#define SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING 4096U

typedef enum sm64_saturn_actor_claim_lane {
    SM64_SATURN_ACTOR_CLAIMED_MASTER = 1U,
    SM64_SATURN_ACTOR_CLAIMED_SLAVE = 2U,
} sm64_saturn_actor_claim_lane_t;

typedef enum sm64_saturn_actor_instance_job_state {
    SM64_SATURN_ACTOR_JOB_EMPTY = 0U,
    SM64_SATURN_ACTOR_JOB_READY = 1U,
    SM64_SATURN_ACTOR_JOB_CLAIMED_MASTER = 2U,
    SM64_SATURN_ACTOR_JOB_CLAIMED_SLAVE = 3U,
    SM64_SATURN_ACTOR_JOB_DONE = 4U,
    SM64_SATURN_ACTOR_QUARANTINED = 5U,
} sm64_saturn_actor_instance_job_state_t;

typedef enum sm64_saturn_actor_output_class {
    SM64_SATURN_ACTOR_OUTPUT_OPAQUE = 1U,
    SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT = 2U,
} sm64_saturn_actor_output_class_t;

typedef enum sm64_saturn_actor_quarantine_reason {
    SM64_SATURN_ACTOR_QUARANTINE_NONE = 0U,
    SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE = 1U,
    SM64_SATURN_ACTOR_QUARANTINE_STALE_GENERATION = 2U,
    SM64_SATURN_ACTOR_QUARANTINE_STALE_PACKAGE_GENERATION = 3U,
    SM64_SATURN_ACTOR_QUARANTINE_STALE_INSTANCE = 4U,
    SM64_SATURN_ACTOR_QUARANTINE_STALE_BANK = 5U,
    SM64_SATURN_ACTOR_QUARANTINE_OUTPUT_OVERFLOW = 6U,
} sm64_saturn_actor_quarantine_reason_t;

/* This pointer-free descriptor is an adapter over Task 14's canonical
 * snapshot ABI.  The full snapshot remains in its immutable generation bank;
 * only identity, ordering, and the claimant-owned output span cross CPUs. */
typedef struct sm64_saturn_actor_instance_descriptor {
    uint32_t generation;
    uint32_t scene_package_generation;
    uint32_t instance_key;
    uint32_t actor_bank_id;
    uint32_t actor_bank_hash_words[8];
    uint16_t family_id;
    uint16_t model_id;
    uint16_t snapshot_index;
    uint16_t source_order;
    uint16_t material_id;
    uint16_t output_offset;
    uint16_t output_capacity;
    uint8_t output_class;
    uint8_t reserved;
} sm64_saturn_actor_instance_descriptor_t;

typedef struct sm64_saturn_actor_instance_release {
    volatile uint32_t generation;
    volatile uint32_t state;
    volatile uint32_t claim;
} sm64_saturn_actor_instance_release_t;

typedef struct sm64_saturn_actor_instance_result {
    uint32_t generation;
    uint32_t instance_key;
    uint16_t output_count;
    uint8_t lane;
    uint8_t reason;
} sm64_saturn_actor_instance_result_t;

typedef struct sm64_saturn_actor_instance_queue {
    sm64_saturn_actor_instance_descriptor_t descriptors[
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    sm64_saturn_actor_instance_release_t release[
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    sm64_saturn_actor_instance_result_t results[
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    volatile uint32_t generation;
    volatile uint16_t count;
    uint16_t manifest_capacity;
    uint16_t output_record_capacity;
    uint16_t reserved;
} sm64_saturn_actor_instance_queue_t;

typedef struct sm64_saturn_actor_instance_queue_memory_report {
    uint32_t descriptor_bytes;
    uint32_t release_bytes;
    uint32_t result_bytes;
    uint32_t queue_bytes;
    uint16_t capacity;
    uint16_t output_record_ceiling;
} sm64_saturn_actor_instance_queue_memory_report_t;

typedef bool (*sm64_saturn_actor_instance_process_fn)(
    const sm64_saturn_actor_instance_descriptor_t *descriptor,
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    void *context, uint16_t *output_count);

/* Processor addresses and snapshot pointers remain renderer-local. Only the
 * fixed descriptor/result/release arrays above are shared between CPUs. */
typedef struct sm64_saturn_actor_instance_processor {
    const sm64_saturn_actor_instance_snapshot_t *snapshots;
    uint16_t snapshot_count;
    uint16_t reserved;
    uint32_t generation;
    sm64_saturn_actor_instance_process_fn process;
    void *context;
} sm64_saturn_actor_instance_processor_t;

_Static_assert(sizeof(sm64_saturn_actor_instance_descriptor_t) == 64U,
               "actor-instance descriptor ABI changed");
_Static_assert(sizeof(sm64_saturn_actor_instance_release_t) == 12U,
               "actor-instance release ABI changed");
_Static_assert(sizeof(sm64_saturn_actor_instance_result_t) == 12U,
               "actor-instance result ABI changed");

bool sm64_saturn_actor_instance_descriptor_from_snapshot(
    const sm64_saturn_actor_instance_snapshot_t *snapshot,
    uint16_t snapshot_index, uint16_t source_order, uint16_t material_id,
    uint8_t output_class, uint16_t output_offset, uint16_t output_capacity,
    sm64_saturn_actor_instance_descriptor_t *descriptor);

void sm64_saturn_actor_instance_queue_init(
    sm64_saturn_actor_instance_queue_t *queue);
bool sm64_saturn_actor_instance_queue_publish(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t manifest_capacity, uint16_t output_record_capacity,
    const sm64_saturn_actor_instance_descriptor_t *descriptors,
    uint16_t count);
bool sm64_saturn_actor_instance_queue_claim_master(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t *descriptor_index);
bool sm64_saturn_actor_instance_queue_claim_slave(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t *descriptor_index);
uint16_t sm64_saturn_actor_instance_queue_drain_master(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    const sm64_saturn_actor_instance_processor_t *processor);
uint16_t sm64_saturn_actor_instance_queue_drain_slave(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    const sm64_saturn_actor_instance_processor_t *processor);
bool sm64_saturn_actor_instance_queue_complete(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index, uint8_t lane,
    const sm64_saturn_actor_instance_snapshot_t *observed_snapshot,
    uint16_t output_count);
bool sm64_saturn_actor_instance_queue_fail(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index, uint8_t lane, uint8_t reason);
sm64_saturn_actor_instance_job_state_t
sm64_saturn_actor_instance_queue_state(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index);
const sm64_saturn_actor_instance_descriptor_t *
sm64_saturn_actor_instance_queue_descriptor(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index);
const sm64_saturn_actor_instance_descriptor_t *
sm64_saturn_actor_instance_queue_claimed_descriptor(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index, uint8_t lane);
const sm64_saturn_actor_instance_result_t *
sm64_saturn_actor_instance_queue_result(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation,
    uint16_t descriptor_index);
bool sm64_saturn_actor_instance_queue_all_terminal(
    const sm64_saturn_actor_instance_queue_t *queue, uint32_t generation);
bool sm64_saturn_actor_instance_queue_reset_retired(
    sm64_saturn_actor_instance_queue_t *queue, uint32_t generation);
void sm64_saturn_actor_instance_queue_memory_report(
    sm64_saturn_actor_instance_queue_memory_report_t *report);

#endif
