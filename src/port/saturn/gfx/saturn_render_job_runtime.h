/* The sole CPU-DUAL owner for an activated A5 render-job queue. */
#ifndef SM64_SATURN_RENDER_JOB_RUNTIME_H
#define SM64_SATURN_RENDER_JOB_RUNTIME_H

#include "saturn_render_job_queue.h"
#include "saturn_render_job_graph.h"

typedef struct sm64_saturn_render_job_runtime_telemetry {
    uint32_t notified_generation;
    uint32_t retired_generation;
    uint32_t notify_sequence;
    uint32_t retired_sequence;
    uint32_t master_claims[SM64_SATURN_RENDER_JOB_CALLBACK_COUNT];
    uint32_t slave_claims[SM64_SATURN_RENDER_JOB_CALLBACK_COUNT];
    uint32_t master_wait_iterations;
    uint32_t master_failures;
    uint32_t slave_failures;
    uint32_t quarantined;
} sm64_saturn_render_job_runtime_telemetry_t;

/* Deprecated raw-queue activation. It fails closed: dependent live work must
 * enter through graph-aware activation below. */
bool sm64_saturn_render_job_runtime_activate(
    sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context);

/* May succeed once for the process lifetime. It installs the one Yaul polling
 * entry on SH-2 but deliberately does not publish work. The graph-aware path
 * is the only runtime activation permitted by the renderer cutover. */
bool sm64_saturn_render_job_runtime_activate_graph(
    sm64_saturn_render_job_graph_t *graph,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context);

/* The master calls this only after queue publication. */
void sm64_saturn_render_job_runtime_notify(void);

/* True only after the polling slave has returned from the most recently
 * notified pass. Queue terminal state alone is insufficient: the master may
 * not recycle descriptor-owned payload while the peer still has a callback
 * on its stack. */
bool sm64_saturn_render_job_runtime_slave_retired(void);

/* Public for the host fixture and the SH-2 polling entry. */
uint16_t sm64_saturn_render_job_runtime_poll_slave(void);

/* The master drains eligible work only after it has exhausted simulation.
 * It never bypasses graph dependency admission. */
uint16_t sm64_saturn_render_job_runtime_drain_master(void);

/* Master-only end-of-frame publication. The caller counts its existing
 * retirement spin locally, then records once so telemetry adds no call or
 * shared-write overhead inside the hot wait loop. */
void sm64_saturn_render_job_runtime_record_master_wait(uint32_t iterations);
void sm64_saturn_render_job_runtime_refresh_terminal_telemetry(void);
bool sm64_saturn_render_job_runtime_telemetry_snapshot(
    sm64_saturn_render_job_runtime_telemetry_t *telemetry);

#endif
