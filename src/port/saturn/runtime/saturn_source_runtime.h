#ifndef SM64_SATURN_SOURCE_RUNTIME_H
#define SM64_SATURN_SOURCE_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include <ultra64.h>

/* `SPTask` is a tagged source type declared by include/types.h.  The runtime
 * only transports its pointer, but the declaration has to be at file scope:
 * declaring it first in a callback parameter would create a prototype-scoped
 * tag and an incompatible callback signature. */
struct SPTask;

/*
 * Saturn's platform seam beneath the original SM64 frame loop.
 *
 * `src/game/game_init.c` remains authoritative for controller state decoding,
 * level-script execution, object updates, geo traversal, and SPTask creation.
 * This module only supplies the console services that surround those calls.
 * It follows the PS1-port shape (source loop -> target task front end) without
 * importing its GTE, packet, or VRAM backend.
 */
typedef void (*sm64_saturn_source_task_submit_fn)(struct SPTask *task,
                                                   void *context);

typedef struct sm64_saturn_source_runtime_state {
    uint32_t input_polls;
    uint32_t submitted_tasks;
    uint32_t unhandled_tasks;
    uint32_t audio_ticks;
    uint32_t preflight_tasks;
    uint32_t preflight_failures;
} sm64_saturn_source_runtime_state_t;

void sm64_saturn_source_runtime_configure(
    sm64_saturn_source_task_submit_fn submit, void *context);
void sm64_saturn_source_runtime_init_controllers(
    uint8_t *controller_bits, OSContStatus *statuses, uint32_t count);
void sm64_saturn_source_runtime_begin_input(void);
void sm64_saturn_source_runtime_read_controllers(OSContPad *pads,
                                                  uint32_t count);
void sm64_saturn_source_runtime_audio_tick(void);
void sm64_saturn_source_runtime_wait_vblank(void);
bool sm64_saturn_source_runtime_preflight_task(void);
const sm64_saturn_source_runtime_state_t *
sm64_saturn_source_runtime_state(void);

#endif
