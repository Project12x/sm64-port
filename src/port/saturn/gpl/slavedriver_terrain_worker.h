/*
 * GPL-3.0-or-later. Close-port of SlaveDriver's coarse master/slave terrain
 * hand-off from WALLS.C:1803-1950 at
 * a8986591557b6e680550d3c23970284d3b38ff8f.
 *
 * The job is a single same-frame notification with disjoint half-open ranges.
 * It deliberately exposes no VDP1 or game-state pointer; the callback owns a
 * project-specific immutable frame job and writes only caller-owned results.
 */
#ifndef SM64_SATURN_SLAVEDRIVER_TERRAIN_WORKER_H
#define SM64_SATURN_SLAVEDRIVER_TERRAIN_WORKER_H

#include <stdbool.h>
#include <stdint.h>

#include "slavedriver_dual_worker.h"

typedef struct sm64_saturn_terrain_worker_job {
    sm64_saturn_dual_worker_fn range;
    void *context;
    uint16_t count;
    uint16_t slave_begin;
} sm64_saturn_terrain_worker_job_t;

bool sm64_saturn_terrain_worker_run(
    const sm64_saturn_terrain_worker_job_t *job,
    sm64_saturn_dual_worker_stats_t *stats);

#endif
