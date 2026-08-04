/* Pointer-free publication for immutable renderer callback snapshots. */
#ifndef SM64_SATURN_RENDER_CALLBACK_CONTEXT_H
#define SM64_SATURN_RENDER_CALLBACK_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_render_job_queue.h"

typedef struct sm64_saturn_render_callback_context_release {
    volatile uint32_t generation;
    volatile uint32_t sequence;
    volatile uint16_t job_index;
    volatile uint16_t payload_bytes;
    volatile uint16_t phase;
    volatile uint8_t producer_lane;
    volatile uint8_t ready;
} sm64_saturn_render_callback_context_release_t;

typedef struct sm64_saturn_render_callback_context_bank {
    sm64_saturn_render_callback_context_release_t
        entries[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
} sm64_saturn_render_callback_context_bank_t;

typedef struct sm64_saturn_render_callback_context_access {
    const void *payload;
    uint32_t sequence;
    uint16_t phase;
    uint16_t payload_bytes;
    uint8_t cache_through;
} sm64_saturn_render_callback_context_access_t;

_Static_assert(sizeof(sm64_saturn_render_callback_context_release_t) == 16U,
               "callback publication must stay pointer-free and bounded");

void sm64_saturn_render_callback_context_bank_init(
    sm64_saturn_render_callback_context_bank_t *bank);

/* Publish only identity and bounds. The payload remains in its statically
 * allocated renderer bank and no pointer crosses the SH-2 boundary. */
bool sm64_saturn_render_callback_context_publish(
    sm64_saturn_render_callback_context_bank_t *bank,
    const sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t job_index, uint32_t sequence, uint16_t payload_bytes,
    uint8_t producer_lane);

/* Open only for the exact current claimant and phase. The returned access is
 * caller-local; a peer claimant receives the P2/cache-through payload alias. */
bool sm64_saturn_render_callback_context_open(
    const sm64_saturn_render_callback_context_bank_t *bank,
    const sm64_saturn_render_job_queue_t *queue, uint16_t job_index,
    sm64_saturn_render_job_state_t claimed_state, uint32_t expected_sequence,
    uint16_t expected_payload_bytes, const void *cached_payload,
    sm64_saturn_render_callback_context_access_t *access);

#endif
