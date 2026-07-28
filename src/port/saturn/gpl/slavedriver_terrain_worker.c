/* GPL-3.0-or-later close-port; see slavedriver_terrain_worker.h. */
#include "slavedriver_terrain_worker.h"

bool sm64_saturn_terrain_worker_run(
    const sm64_saturn_terrain_worker_job_t *job,
    sm64_saturn_dual_worker_stats_t *stats)
{
    if (job == NULL || job->range == NULL || job->count == 0U) {
        if (stats != NULL) *stats = (sm64_saturn_dual_worker_stats_t){0};
        return false;
    }
    return sm64_saturn_dual_worker_run(
        job->range, job->context, job->count, job->slave_begin, stats);
}
