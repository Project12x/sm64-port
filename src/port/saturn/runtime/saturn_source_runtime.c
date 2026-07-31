#include <string.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "types.h"
#if defined(SM64_SATURN_RUNTIME_CONTRACT_TEST)
#include "pc/controller/controller_api.h"

extern struct ControllerAPI controller_saturn;
extern struct MarioState *gMarioState;

static void vdp2_tvmd_vblank_in_wait(void)
{
}

static void vdp2_tvmd_vblank_out_wait(void)
{
}
#else
#include <yaul.h>

#include "controller_saturn.h"
#include "game/level_update.h"
#endif
#include "saturn_source_runtime.h"

static sm64_saturn_source_task_submit_fn sTaskSubmit;
static void *sTaskSubmitContext;
static sm64_saturn_source_runtime_state_t sState;
static sm64_saturn_input_replay_t sInputReplay;
static bool sDisplaySuppressed;

/* Matches the original public game/main.h boundary. This target owns the
 * implementation, but the startup preflight invokes it before its definition
 * below. */
void exec_display_list(struct SPTask *task);

void sm64_saturn_source_runtime_configure(
        sm64_saturn_source_task_submit_fn submit, void *context) {
    sTaskSubmit = submit;
    sTaskSubmitContext = context;
}

void sm64_saturn_source_runtime_configure_input_replay(
        const sm64_saturn_input_replay_sample_t *samples, uint16_t sample_count) {
    sm64_saturn_input_replay_init(&sInputReplay, samples, sample_count);
    sState.input_replay_ticks = 0U;
    sState.input_replay_sample = 0U;
    sState.last_applied_buttons = 0U;
    sState.last_applied_stick_x = 0;
    sState.last_applied_stick_y = 0;
    sState.input_replay_active = sInputReplay.enabled;
    sState.input_replay_complete = sInputReplay.complete;
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
    if (sInputReplay.enabled) {
        /* Do not spend deterministic route samples while the source boot is
         * still constructing its authoritative Mario state.  Renderer
         * profiles can make this bootstrap interval longer; consuming input
         * there makes the same route begin at different gameplay ticks in
         * the interpreted and demo builds. */
        if (gMarioState == NULL) {
            pads[0].button = 0U;
            pads[0].stick_x = 0;
            pads[0].stick_y = 0;
            pads[0].errnum = 0;
        } else {
            sm64_saturn_input_replay_apply(&sInputReplay, &pads[0].button,
                                           &pads[0].stick_x, &pads[0].stick_y);
            pads[0].errnum = 0;
            sState.input_replay_ticks = sInputReplay.ticks_consumed;
            sState.input_replay_sample = sInputReplay.sample_index;
            sState.input_replay_complete = sInputReplay.complete;
        }
    }
    sState.last_applied_buttons = pads[0].button;
    sState.last_applied_stick_x = pads[0].stick_x;
    sState.last_applied_stick_y = pads[0].stick_y;
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

void sm64_saturn_source_runtime_set_display_suppressed(bool suppressed) {
    sDisplaySuppressed = suppressed;
}

bool sm64_saturn_source_runtime_display_suppressed(void) {
    return sDisplaySuppressed;
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
