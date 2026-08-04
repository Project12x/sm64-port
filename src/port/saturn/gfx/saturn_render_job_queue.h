/* Immutable, bounded render work queue shared by the two SH-2s.
 *
 * This is project-owned scheduler code.  It is pattern-informed by the
 * disjoint result ownership in SlaveDriver Engine (WALLS.C:1803-1950,
 * a8986591557b6e680550d3c23970284d3b38ff8f, GPL-3.0-or-later) and the
 * persistent slave consumer studied in SONIC Z-TREME (cff7545, GPL-3.0).
 * Neither scheduler was copied: this fixed descriptor ABI and state machine
 * are specific to the sourceboot snapshot/render boundary.
 */
#ifndef SM64_SATURN_RENDER_JOB_QUEUE_H
#define SM64_SATURN_RENDER_JOB_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#define SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY 8U

typedef enum sm64_saturn_render_job_state {
    SM64_SATURN_RENDER_JOB_EMPTY = 0U,
    SM64_SATURN_RENDER_JOB_READY = 1U,
    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER = 2U,
    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE = 3U,
    SM64_SATURN_RENDER_JOB_DONE = 4U,
    SM64_SATURN_RENDER_JOB_FAILED = 5U,
    SM64_SATURN_RENDER_JOB_QUARANTINED = 6U,
} sm64_saturn_render_job_state_t;

typedef enum sm64_saturn_render_job_type {
    SM64_SATURN_RENDER_JOB_WORLD_ADMIT = 1U,
    SM64_SATURN_RENDER_JOB_WORLD_LOWER = 2U,
    SM64_SATURN_RENDER_JOB_ACTOR_ADMIT = 3U,
    SM64_SATURN_RENDER_JOB_ACTOR_LOWER = 4U,
} sm64_saturn_render_job_type_t;

/* Callback IDs resolve through the renderer's static table.  A job never
 * crosses CPUs with a function pointer, a live source-state pointer, or a
 * VDP/allocator pointer. */
typedef enum sm64_saturn_render_job_callback {
    SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT = 1U,
    SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER = 2U,
    SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT = 3U,
    SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER = 4U,
} sm64_saturn_render_job_callback_t;

typedef struct sm64_saturn_render_job {
    uint16_t type;
    uint16_t callback_id;
    uint32_t snapshot_generation;
    uint16_t input_offset;
    uint16_t input_count;
    uint16_t output_offset;
    uint16_t output_capacity;
} sm64_saturn_render_job_t;

/* The table belongs to the local renderer, not to a published descriptor.
 * Jobs cross SH-2s by fixed callback ID only; the polling consumer resolves
 * that ID after it has claimed the immutable descriptor. */
typedef bool (*sm64_saturn_render_job_callback_fn)(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, void *context);

#define SM64_SATURN_RENDER_JOB_CALLBACK_COUNT 4U

typedef struct sm64_saturn_render_job_callback_table {
    sm64_saturn_render_job_callback_fn
        entries[SM64_SATURN_RENDER_JOB_CALLBACK_COUNT];
} sm64_saturn_render_job_callback_table_t;

/* State and claim are independent uncached 32-bit words.  TAS.B claims the
 * zero word before each state transition, so a descriptor has exactly one
 * owner from READY until it is terminal. */
typedef struct sm64_saturn_render_job_release {
    volatile uint32_t generation;
    volatile uint32_t state;
    volatile uint32_t claim;
} sm64_saturn_render_job_release_t;

typedef struct sm64_saturn_render_job_queue {
    sm64_saturn_render_job_t jobs[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    sm64_saturn_render_job_release_t
        release[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    volatile uint32_t generation;
    volatile uint32_t count;
} sm64_saturn_render_job_queue_t;

/* Source-only preparation for the future queue cutover. This records one
 * immutable callback-table/context owner but cannot register, wake, or
 * otherwise activate a CPU-DUAL slave. The atomic live cutover must replace
 * this API after every legacy worker dispatch is removed. */
bool sm64_saturn_render_job_queue_source_arm(
    sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context);
bool sm64_saturn_render_job_queue_source_armed(void);

_Static_assert(sizeof(sm64_saturn_render_job_t) == 16U,
               "job descriptor ABI must stay pointer-free and fixed-width");
_Static_assert(sizeof(sm64_saturn_render_job_release_t) == 12U,
               "job release ABI must use fixed-width words");

void sm64_saturn_render_job_queue_init(sm64_saturn_render_job_queue_t *queue);
uint32_t sm64_saturn_render_job_queue_generation(
    const sm64_saturn_render_job_queue_t *queue);
bool sm64_saturn_render_job_queue_publish(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    const sm64_saturn_render_job_t *jobs, uint16_t count);
bool sm64_saturn_render_job_queue_claim_master(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t *job_index);
bool sm64_saturn_render_job_queue_claim_slave(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t *job_index);
bool sm64_saturn_render_job_queue_complete(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t job_index, sm64_saturn_render_job_state_t claimed_state);
bool sm64_saturn_render_job_queue_fail(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t job_index, sm64_saturn_render_job_state_t claimed_state);
bool sm64_saturn_render_job_queue_all_terminal(
    const sm64_saturn_render_job_queue_t *queue, uint32_t generation);
bool sm64_saturn_render_job_queue_reset_retired(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation);
const sm64_saturn_render_job_t *sm64_saturn_render_job_queue_job(
    const sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    uint16_t job_index);
const sm64_saturn_render_job_t *sm64_saturn_render_job_queue_done_job(
    const sm64_saturn_render_job_queue_t *queue, uint16_t job_index);

/* One polling pass claims until no READY work remains. The caller provides a
 * renderer-local static callback table; callback addresses never enter the
 * shared descriptor ABI. A failed/missing callback terminally fails only its
 * own claimed job, preserving the generation's merge order. */
uint16_t sm64_saturn_render_job_queue_drain_master(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context);
uint16_t sm64_saturn_render_job_queue_drain_slave(
    sm64_saturn_render_job_queue_t *queue, uint32_t generation,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context);

#endif
