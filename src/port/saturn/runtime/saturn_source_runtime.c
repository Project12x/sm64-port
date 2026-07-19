#include <string.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "types.h"
#include <yaul.h>

#include "controller_saturn.h"
#include "saturn_source_runtime.h"

static sm64_saturn_source_task_submit_fn sTaskSubmit;
static void *sTaskSubmitContext;
static sm64_saturn_source_runtime_state_t sState;

/* Matches the original public game/main.h boundary. This target owns the
 * implementation, but the startup preflight invokes it before its definition
 * below. */
void exec_display_list(struct SPTask *task);

void sm64_saturn_source_runtime_configure(
        sm64_saturn_source_task_submit_fn submit, void *context) {
    sTaskSubmit = submit;
    sTaskSubmitContext = context;
}

void sm64_saturn_source_runtime_init_controllers(
        uint8_t *controller_bits, OSContStatus *statuses, uint32_t count) {
    controller_saturn.init();
    if (statuses != NULL && count != 0U)
        (void)memset(statuses, 0, sizeof(*statuses) * count);
    if (controller_bits != NULL)
        *controller_bits = 1U;
}

void sm64_saturn_source_runtime_begin_input(void) {
    /* `controller_saturn.read` consumes the most recent SMPC collection. The
     * normal Yaul VBlank cadence continues to issue INTBACK requests. */
}

void sm64_saturn_source_runtime_read_controllers(OSContPad *pads,
                                                  uint32_t count) {
    if (pads == NULL || count == 0U)
        return;

    controller_saturn.read(&pads[0]);
    sState.input_polls++;
    if (count > 1U) {
        (void)memset(&pads[1], 0, sizeof(*pads) * (count - 1U));
        for (uint32_t index = 1; index < count; index++)
            pads[index].errnum = CONT_NO_RESPONSE_ERROR;
    }
}

void sm64_saturn_source_runtime_audio_tick(void) {
    /* Audio is intentionally a source-frame service even while the early E2
     * visual slice has no SCSP transport. Replacing this no-op with AudioAPI
     * must not change game-loop or renderer ownership. */
    sState.audio_ticks++;
}

void sm64_saturn_source_runtime_wait_vblank(void) {
    vdp2_tvmd_vblank_in_wait();
    vdp2_tvmd_vblank_out_wait();
}

bool sm64_saturn_source_runtime_preflight_task(void) {
    /* This is deliberately a source-ABI smoke test, not a replacement render
     * path: exactly one normal Fast3D END command reaches the configured
     * consumer through the same public dispatcher game_init.c uses. */
    static const Gfx end_display_list[] = {
        {{ ((uintptr_t)G_ENDDL << 24), 0U }},
    };
    struct SPTask task;
    const uint32_t submitted_before = sState.submitted_tasks;

    (void)memset(&task, 0, sizeof(task));
    task.task.t.type = M_GFXTASK;
    task.task.t.data_ptr = (u64 *)end_display_list;
    exec_display_list(&task);
    sState.preflight_tasks++;
    if (sTaskSubmit == NULL ||
        sState.submitted_tasks != (submitted_before + 1U)) {
        sState.preflight_failures++;
        return false;
    }
    return true;
}

const sm64_saturn_source_runtime_state_t *
sm64_saturn_source_runtime_state(void) {
    return &sState;
}

/* Original game code calls this symbol through `display_and_vsync()`. The
 * generated Saturn Fast3D front end will register the source-identified task
 * consumer; dropping a task is measured explicitly during E2 bring-up. */
void exec_display_list(struct SPTask *task) {
    if (task == NULL) {
        sState.unhandled_tasks++;
        return;
    }
    sState.submitted_tasks++;
    if (sTaskSubmit != NULL) {
        sTaskSubmit(task, sTaskSubmitContext);
        return;
    }
    sState.unhandled_tasks++;
}
