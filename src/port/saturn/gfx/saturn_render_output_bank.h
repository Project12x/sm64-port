/* Descriptor-owned output-bank publication for opportunistic SH-2 jobs.
 *
 * This is original project code, pattern-informed by SlaveDriver's disjoint
 * result ownership (WALLS.C:1803-1950 at a8986591557b6e680550d3c23970284d3b38ff8f,
 * GPL-3.0-or-later).  No upstream scheduler code is copied.  A descriptor
 * selects terrain or actor storage by kind; the CPU that actually claims the
 * descriptor publishes the lane.  Logical work offsets never select a cache
 * alias, so a stealing master cannot later read its own writes through P2.
 */
#ifndef SM64_SATURN_RENDER_OUTPUT_BANK_H
#define SM64_SATURN_RENDER_OUTPUT_BANK_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_dual_frame_bank.h"
#include "saturn_render_job_queue.h"

typedef enum sm64_saturn_render_output_bank_kind {
    SM64_SATURN_RENDER_OUTPUT_BANK_INVALID = 0U,
    SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN = 1U,
    SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR = 2U,
} sm64_saturn_render_output_bank_kind_t;

typedef enum sm64_saturn_render_output_lane {
    SM64_SATURN_RENDER_OUTPUT_LANE_MASTER = 0U,
    SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE = 1U,
} sm64_saturn_render_output_lane_t;

/* `ready` is the final release word. The bank must be allocated in the
 * renderer's uncached shared partition; these helpers additionally select P2
 * for peer metadata reads so callers cannot accidentally poll cached state. */
typedef struct sm64_saturn_render_output_release {
    volatile uint32_t generation;
    volatile uint32_t job_index;
    volatile uint32_t claimed_state;
    volatile uint32_t claim;
    volatile uint32_t ready;
} sm64_saturn_render_output_release_t;

typedef struct sm64_saturn_render_output_bank {
    uint32_t kind;
    sm64_saturn_render_output_release_t
        release[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
} sm64_saturn_render_output_bank_t;

_Static_assert(sizeof(sm64_saturn_render_output_release_t) == 20U,
               "output release ABI must remain fixed-width and pointer-free");

void sm64_saturn_render_output_bank_init(
    sm64_saturn_render_output_bank_t *bank,
    sm64_saturn_render_output_bank_kind_t kind);
sm64_saturn_render_output_bank_kind_t
sm64_saturn_render_output_bank_kind_for_job(
    const sm64_saturn_render_job_t *job);
bool sm64_saturn_render_output_bank_publish(
    sm64_saturn_render_output_bank_t *bank,
    const sm64_saturn_render_job_t *job, uint16_t job_index,
    sm64_saturn_render_job_state_t claimed_state);
bool sm64_saturn_render_output_bank_owner_lane(
    const sm64_saturn_render_output_bank_t *bank,
    const sm64_saturn_render_job_t *job, uint16_t job_index,
    uint8_t *owner_lane);
bool sm64_saturn_render_output_bank_reader_needs_cache_through(
    const sm64_saturn_render_output_bank_t *bank,
    const sm64_saturn_render_job_t *job, uint16_t job_index,
    uint8_t reader_lane);
const void *sm64_saturn_render_output_bank_read_range(
    const sm64_saturn_render_output_bank_t *bank,
    const sm64_saturn_render_job_t *job, uint16_t job_index,
    uint8_t reader_lane, const void *cached);

#endif
