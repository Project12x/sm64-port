#ifndef SM64_SATURN_SOURCE_RUNTIME_H
#define SM64_SATURN_SOURCE_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include <ultra64.h>

#include "saturn_input_replay.h"

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

/* Per-source-tick audio service, installed by the target the same way the
 * task submitter is. game_init.c calls sm64_saturn_source_runtime_audio_tick()
 * where the original loop called audio_game_loop_tick(); this hook is what
 * that call reaches. Registered rather than linked directly because this file
 * is also built into castleviewer and the host runtime-contract test, neither
 * of which links an audio_signal_game_loop_tick. */
typedef void (*sm64_saturn_source_audio_tick_fn)(void);

typedef struct sm64_saturn_source_runtime_state {
    uint32_t input_polls;
    uint32_t submitted_tasks;
    uint32_t unhandled_tasks;
    uint32_t audio_ticks;
    uint32_t preflight_tasks;
    uint32_t preflight_failures;
    uint32_t input_replay_ticks;
    uint16_t input_replay_sample;
    uint16_t last_applied_buttons;
    int8_t last_applied_stick_x;
    int8_t last_applied_stick_y;
    bool input_replay_active;
    bool input_replay_complete;
    /* Appended telemetry: normal source walks versus use of the reserved
     * scene-graph suppression policy. Accepted sourceboot builds leave the
     * policy fail-closed, so the suppressed counter remains zero. */
    uint32_t scene_graph_walks;
    uint32_t scene_graph_walks_suppressed;
} sm64_saturn_source_runtime_state_t;

void sm64_saturn_source_runtime_configure(
    sm64_saturn_source_task_submit_fn submit, void *context);
void sm64_saturn_source_runtime_configure_audio_tick(
    sm64_saturn_source_audio_tick_fn tick);
void sm64_saturn_source_runtime_configure_input_replay(
    const sm64_saturn_input_replay_sample_t *samples, uint16_t sample_count);
void sm64_saturn_source_runtime_init_controllers(
    uint8_t *controller_bits, OSContStatus *statuses, uint32_t count);
void sm64_saturn_source_runtime_begin_input(void);
void sm64_saturn_source_runtime_read_controllers(OSContPad *pads,
                                                  uint32_t count);
void sm64_saturn_source_runtime_audio_tick(void);

/* Audio cadence, decoupled from the render rate (2026-09-04).
 *
 * Every timing constant in saturn_audio_policy.h is denominated in ticks
 * and tuned for the N64's 30 Hz audio_game_loop_tick:
 * DISCRETE_FRESHNESS is 10 ticks, CONTINUOUS_GRACE is 2, and publish_bank()
 * deliberately promotes at most one sound per bank per tick, mirroring the
 * original's per-bank arbitration. The Saturn game loop runs at the
 * PRESENTED frame rate, measured at 5.8 Hz on build 3b57f03a29442a5c, so
 * every one of those constants silently stretched about 5x: a discrete
 * sound could wait 10 ticks = 1.72 s before publishing and still be
 * considered fresh. Sounds arrived audibly late, which is what the owner
 * reported once the SFX bake landed.
 *
 * Pacing moves the cadence onto the VBlank counter instead. Set a nonzero
 * interval and the game-loop entry point stops driving the policy; the
 * target calls ..._audio_tick_paced() from its frame loop instead, which
 * services the policy once per `interval` VBlanks of REAL time.
 *
 * Deliberately at most one service per call, with the timestamp set to the
 * observed VBlank rather than advanced by the interval: a stall must not
 * bank credit and then fire a burst. Bursting would be worse than useless
 * here, because every service in the burst observes identical state -- the
 * playing sound has not finished and no new request has arrived -- so
 * publish_bank() would make the same decision N times while the freshness
 * clock advanced N ticks, aging sounds out early for no benefit. */
#define SM64_SATURN_SOURCE_AUDIO_TICK_VBLANK_INTERVAL 2U
void sm64_saturn_source_runtime_set_audio_tick_pacing(uint16_t vblank_interval);
void sm64_saturn_source_runtime_audio_tick_paced(uint32_t vblank_count);
void sm64_saturn_source_runtime_wait_vblank(void);

/* Demo-path scheduler hook. Suppression skips only the source display-list
 * submission at the Saturn presentation boundary; the authoritative source
 * tick, VBlank wait, and global-timer increment still run unchanged. */
void sm64_saturn_source_runtime_set_display_suppressed(bool suppressed);
bool sm64_saturn_source_runtime_display_suppressed(void);
/* Reserved for a future behavior-tested state-only geo seam. Sourceboot must
 * not enable this policy while geo_process_root() owns source state updates. */
void sm64_saturn_source_runtime_set_scene_graph_suppressed(bool suppressed);
bool sm64_saturn_source_runtime_scene_graph_suppressed(void);
void sm64_saturn_source_runtime_note_scene_graph_walk(bool suppressed);
bool sm64_saturn_source_runtime_preflight_task(void);
const sm64_saturn_source_runtime_state_t *
sm64_saturn_source_runtime_state(void);

#endif
