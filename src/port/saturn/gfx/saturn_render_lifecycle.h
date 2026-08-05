/* Scene-neutral exact-generation render lifetime controller.
 *
 * This project-owned state machine is pattern-informed by the pinned
 * SlaveDriver and Sonic Z-Treme early-dispatch/late-join lifetimes recorded
 * in docs/saturn/UPSTREAM_CODE_LEDGER.md. No upstream code is copied.
 */
#ifndef SM64_SATURN_RENDER_LIFECYCLE_H
#define SM64_SATURN_RENDER_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum sm64_saturn_render_lifecycle_status {
    SM64_SATURN_RENDER_LIFECYCLE_PENDING = 0,
    SM64_SATURN_RENDER_LIFECYCLE_COMPLETE,
    SM64_SATURN_RENDER_LIFECYCLE_FAILED,
} sm64_saturn_render_lifecycle_status_t;

typedef struct sm64_saturn_render_lifecycle {
    uint32_t active_generation;
    bool active;
} sm64_saturn_render_lifecycle_t;

typedef struct sm64_saturn_render_lifecycle_ops {
    bool (*prepare_publish)(void *context, uint32_t generation);
    void (*notify)(void *context);
    bool (*slave_retired)(void *context);
    uint16_t (*drain_master)(void *context);
    bool (*finalize)(void *context, uint32_t generation,
                     uint16_t master_jobs);
    void (*quarantine)(void *context, uint32_t generation);
} sm64_saturn_render_lifecycle_ops_t;

bool sm64_saturn_render_lifecycle_start(
    sm64_saturn_render_lifecycle_t *lifecycle,
    const sm64_saturn_render_lifecycle_ops_t *ops, void *context,
    uint32_t generation);

sm64_saturn_render_lifecycle_status_t sm64_saturn_render_lifecycle_poll(
    sm64_saturn_render_lifecycle_t *lifecycle,
    const sm64_saturn_render_lifecycle_ops_t *ops, void *context,
    uint32_t generation);

#endif
