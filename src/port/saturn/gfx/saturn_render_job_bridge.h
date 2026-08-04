/* Descriptor-owned result routing for the A5 render queue.
 *
 * Original project code. This is a narrow adapter over the project-owned A5
 * queue/output-bank contracts: it is pattern-informed by SlaveDriver's
 * disjoint result ownership (WALLS.C:1803-1950 at
 * a8986591557b6e680550d3c23970284d3b38ff8f, GPL-3.0-or-later) and Z-Treme's
 * fixed work-area discipline (workarea.c at
 * cff75451c1616aac1236fc2b44223902b55c706b, GPLv3). No upstream code is
 * copied. Descriptor output span + successful queue claim, never a callback
 * begin offset or fixed CPU split, select result ownership.
 */
#ifndef SM64_SATURN_RENDER_JOB_BRIDGE_H
#define SM64_SATURN_RENDER_JOB_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_render_output_bank.h"

typedef struct sm64_saturn_render_job_execution {
    uint16_t job_index;
    uint16_t output_offset;
    uint16_t output_capacity;
    uint8_t writer_lane;
} sm64_saturn_render_job_execution_t;

bool sm64_saturn_render_job_bridge_begin_output(
    sm64_saturn_render_job_queue_t *queue,
    sm64_saturn_render_output_bank_t *terrain,
    sm64_saturn_render_output_bank_t *actor, uint16_t job_index,
    sm64_saturn_render_job_execution_t *execution);

const void *sm64_saturn_render_job_bridge_read_output(
    const sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_output_bank_t *terrain,
    const sm64_saturn_render_output_bank_t *actor, uint16_t job_index,
    uint8_t reader_lane, const void *cached);

#endif
