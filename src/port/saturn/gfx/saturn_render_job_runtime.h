/* The sole CPU-DUAL owner for an activated A5 render-job queue. */
#ifndef SM64_SATURN_RENDER_JOB_RUNTIME_H
#define SM64_SATURN_RENDER_JOB_RUNTIME_H

#include "saturn_render_job_queue.h"

/* May succeed once for the process lifetime.  Activation installs the one
 * Yaul polling entry on SH-2; it deliberately does not publish work. */
bool sm64_saturn_render_job_runtime_activate(
    sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_job_callback_table_t *callbacks, void *context);

/* The master calls this only after queue publication. */
void sm64_saturn_render_job_runtime_notify(void);

/* Public for the host fixture and the SH-2 polling entry. */
uint16_t sm64_saturn_render_job_runtime_poll_slave(void);

#endif
