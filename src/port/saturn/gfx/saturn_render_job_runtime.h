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

typedef enum sm64_saturn_render_job_runtime_marker {
    SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_NOTIFIED,
    SM64_SATURN_RENDER_JOB_RUNTIME_MARKER_RETIRED,
} sm64_saturn_render_job_runtime_marker_t;

typedef uint32_t (*sm64_saturn_render_job_runtime_marker_clock_fn)(
    void *context);
typedef void (*sm64_saturn_render_job_runtime_marker_observer_fn)(
    void *context, sm64_saturn_render_job_runtime_marker_t marker,
    uint32_t generation, uint32_t sequence, uint32_t marker_vblank);

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

/* Installs the target-clock sampler used at the two cross-CPU release
 * markers. The observer receives the captured value, so callback latency can
 * never move the recorded notification or retirement boundary. */
bool sm64_saturn_render_job_runtime_observe_markers(
    sm64_saturn_render_job_runtime_marker_observer_fn observer,
    sm64_saturn_render_job_runtime_marker_clock_fn clock, void *context);

/* S1.11 auxiliary slave dispatch.
 *
 * The graph pass owns the notify/retire protocol below, and that protocol
 * admits exactly one pass per frame. A second kind of slave work -- work the
 * master wants overlapped with its own pre-publication phase, before the graph
 * even exists -- therefore cannot ride it.
 *
 * A registered task is offered every slave wake-up BEFORE the graph pass. If
 * it returns true it has consumed that wake-up: it runs to completion and the
 * entry returns without touching notify_sequence/retired_sequence, leaving the
 * graph protocol untouched. Returning false falls through to the graph pass, so
 * a frame with no auxiliary work behaves exactly as it did before.
 *
 * Notifications are latched (MINIT sets the FTCSR ICI flag, and the polling
 * slave clears it before invoking the entry), so a dispatch raised while the
 * slave is still inside a handler is preserved rather than lost. The caller
 * must still ensure at most one dispatch is outstanding at a time. */
typedef bool (*sm64_saturn_render_job_runtime_slave_task_fn)(void);
bool sm64_saturn_render_job_runtime_set_slave_task(
    sm64_saturn_render_job_runtime_slave_task_fn task);

/* Raise one auxiliary dispatch. No-op where there is no slave CPU. */
void sm64_saturn_render_job_runtime_dispatch_slave_task(void);

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
