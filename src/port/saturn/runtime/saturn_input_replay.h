#ifndef SM64_SATURN_INPUT_REPLAY_H
#define SM64_SATURN_INPUT_REPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A target-test input stream.  It is deliberately expressed in OSContPad
 * values: the original controller, Mario, camera, object, and level code
 * remains the only consumer and owner of the resulting game state. */
typedef struct sm64_saturn_input_replay_sample {
    uint16_t ticks;
    int8_t stick_x;
    int8_t stick_y;
    uint16_t buttons;
} sm64_saturn_input_replay_sample_t;

typedef struct sm64_saturn_input_replay {
    const sm64_saturn_input_replay_sample_t *samples;
    uint16_t sample_count;
    uint16_t sample_index;
    uint16_t ticks_in_sample;
    uint32_t ticks_consumed;
    bool enabled;
    bool complete;
} sm64_saturn_input_replay_t;

static inline void sm64_saturn_input_replay_init(
    sm64_saturn_input_replay_t *replay,
    const sm64_saturn_input_replay_sample_t *samples, uint16_t sample_count)
{
    replay->samples = samples;
    replay->sample_count = sample_count;
    replay->sample_index = 0U;
    replay->ticks_in_sample = 0U;
    replay->ticks_consumed = 0U;
    replay->enabled = samples != NULL && sample_count != 0U;
    replay->complete = !replay->enabled;
}

static inline uint32_t sm64_saturn_input_replay_total_ticks(
    const sm64_saturn_input_replay_sample_t *samples, uint16_t sample_count)
{
    uint32_t total = 0U;
    for (uint16_t index = 0U; index < sample_count; index++)
        total += samples[index].ticks;
    return total;
}

/* Writes one sample to a pad-shaped destination and advances exactly one
 * source-input tick.  After the route, neutral input is supplied forever. */
static inline void sm64_saturn_input_replay_apply(
    sm64_saturn_input_replay_t *replay, uint16_t *buttons, int8_t *stick_x,
    int8_t *stick_y)
{
    if (!replay->enabled || replay->complete) {
        *buttons = 0U;
        *stick_x = 0;
        *stick_y = 0;
        return;
    }

    const sm64_saturn_input_replay_sample_t *sample =
        &replay->samples[replay->sample_index];
    *buttons = sample->buttons;
    *stick_x = sample->stick_x;
    *stick_y = sample->stick_y;
    replay->ticks_in_sample++;
    replay->ticks_consumed++;

    if (replay->ticks_in_sample == sample->ticks) {
        replay->sample_index++;
        replay->ticks_in_sample = 0U;
        if (replay->sample_index == replay->sample_count)
            replay->complete = true;
    }
}

#endif
