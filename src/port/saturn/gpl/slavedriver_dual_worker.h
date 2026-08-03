/*
 * GPL-3.0-or-later. Close-port of Lobotomy-Software/SlaveDriver-Engine's
 * bounded master/slave work hand-off (WALLS.C, commit
 * a8986591557b6e680550d3c23970284d3b38ff8f).
 *
 * The original engine used a monotonic work cursor and a compact result
 * region. This Yaul adaptation keeps the same ownership rule: the slave is
 * given a disjoint half-open range and never writes the master's range.
 */
#ifndef SM64_SATURN_SLAVEDRIVER_DUAL_WORKER_H
#define SM64_SATURN_SLAVEDRIVER_DUAL_WORKER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*sm64_saturn_dual_worker_fn)(void *context,
                                            uint16_t begin,
                                            uint16_t end);

typedef struct sm64_saturn_dual_worker_stats {
    uint32_t slave_jobs_completed;
    uint32_t slave_busy_ticks;
    uint32_t master_wait_ticks;
    uint32_t slave_timeouts;
} sm64_saturn_dual_worker_stats_t;

void sm64_saturn_dual_worker_init(void);

/* A second dispatch is legal only after the previous bounded job has retired.
 * This exposes that ownership invariant without making timing a scheduling
 * policy. */
bool sm64_saturn_dual_worker_is_idle(void);

/* Returns true when the slave completed its assigned range. A false return
 * means the timeout path was taken, but only after cancellation has been
 * observed and the slave has positively retired; callers may then safely
 * reuse the complete result range for a serial fallback. */
bool sm64_saturn_dual_worker_run(sm64_saturn_dual_worker_fn fn,
                                 void *context, uint16_t count,
                                 uint16_t slave_begin,
                                 sm64_saturn_dual_worker_stats_t *stats);

/* Range callbacks should check this between bounded items. */
bool sm64_saturn_dual_worker_cancelled(void);

#endif
