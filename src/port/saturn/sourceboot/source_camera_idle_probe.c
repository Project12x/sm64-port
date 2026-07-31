#include <stddef.h>
#include <stdint.h>

#include <cpu/cache.h>

#include "game/level_update.h"
#include "source_camera_idle_probe.h"

#ifndef SATURN_CAMERA_IDLE_START_TICK
#define SATURN_CAMERA_IDLE_START_TICK 0
#endif
#ifndef SATURN_CAMERA_IDLE_DISCOVERY
#define SATURN_CAMERA_IDLE_DISCOVERY 0
#endif
#ifndef SATURN_CAMERA_VARIANT
#define SATURN_CAMERA_VARIANT 1
#endif

#define SOURCEBOOT_SCC1_MAGIC 0x53434331U
#define SOURCEBOOT_SCC1_VERSION 1U
#define SOURCEBOOT_SBR4_MAGIC 0x53425234U
#define SOURCEBOOT_SBR4_VERSION 4U
#define SOURCEBOOT_REPLAY_TICKS 2000U
#define SOURCEBOOT_CAMERA_ROUTE_ID 2U
#define SOURCEBOOT_STABLE_TICKS 60U

typedef struct sourceboot_mario_semantic {
    uint32_t action;
    uint32_t pos_bits[3];
    uint16_t face_angle[3];
} sourceboot_mario_semantic_t;

sourceboot_camera_idle_capture_t sourceboot_camera_idle_capture
    __attribute__((section(".lwram_camera_capture"), aligned(32), used));

static sm64_saturn_camera_probe_snapshot_t s_sourceboot_camera_candidate;
static sourceboot_mario_semantic_t s_sourceboot_mario_candidate;
static uint32_t s_sourceboot_candidate_first_tick;
static uint32_t s_sourceboot_stable_ticks;
static uint32_t s_sourceboot_sample_count;
static uint8_t s_sourceboot_candidate_valid;
static uint8_t s_sourceboot_capture_failed;
static uint8_t s_sourceboot_capture_complete;

static volatile uint32_t *sourceboot_capture_words(void)
{
    return (volatile uint32_t *)(CPU_CACHE_THROUGH |
        (uintptr_t)&sourceboot_camera_idle_capture);
}

static uint32_t sourceboot_f32_bits(float value)
{
    union {
        float value;
        uint32_t bits;
    } raw;
    raw.value = value;
    return raw.bits;
}

static uint32_t sourceboot_pack_applied_input(
        const sm64_saturn_source_runtime_state_t *runtime)
{
    return ((uint32_t)runtime->last_applied_buttons << 16) |
           ((uint32_t)(uint8_t)runtime->last_applied_stick_x << 8) |
           (uint32_t)(uint8_t)runtime->last_applied_stick_y;
}

static int sourceboot_mario_read(sourceboot_mario_semantic_t *mario)
{
    if (gMarioState == NULL)
        return 0;
    mario->action = gMarioState->action;
    mario->pos_bits[0] = sourceboot_f32_bits(gMarioState->pos[0]);
    mario->pos_bits[1] = sourceboot_f32_bits(gMarioState->pos[1]);
    mario->pos_bits[2] = sourceboot_f32_bits(gMarioState->pos[2]);
    mario->face_angle[0] = (uint16_t)gMarioState->faceAngle[0];
    mario->face_angle[1] = (uint16_t)gMarioState->faceAngle[1];
    mario->face_angle[2] = (uint16_t)gMarioState->faceAngle[2];
    return 1;
}

static int sourceboot_words_equal(const uint32_t *left,
                                  const uint32_t *right, uint32_t count)
{
    uint32_t index;
    for (index = 0U; index < count; index++) {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

static int sourceboot_mario_equal(
        const sourceboot_mario_semantic_t *left,
        const sourceboot_mario_semantic_t *right)
{
    return left->action == right->action &&
        sourceboot_words_equal(left->pos_bits, right->pos_bits, 3U) &&
        left->face_angle[0] == right->face_angle[0] &&
        left->face_angle[1] == right->face_angle[1] &&
        left->face_angle[2] == right->face_angle[2];
}

static uint32_t sourceboot_state_flags(
        const sm64_saturn_camera_probe_snapshot_t *camera)
{
    return camera->camera_flags |
        SM64_SATURN_CAMERA_PROBE_FLAG_REPLAY_COMPLETE |
        SM64_SATURN_CAMERA_PROBE_FLAG_NEUTRAL_INPUT |
        SM64_SATURN_CAMERA_PROBE_FLAG_MARIO_QUIESCENT;
}

static void sourceboot_write_sample(
        uint32_t sample_index, uint32_t source_tick,
        const sm64_saturn_source_runtime_state_t *runtime,
        const sm64_saturn_camera_probe_snapshot_t *camera)
{
    volatile uint32_t *words = sourceboot_capture_words();
    const uint32_t base = SOURCEBOOT_SCC1_HEADER_WORDS +
        sample_index * SOURCEBOOT_SCC1_SAMPLE_WORDS;
    uint32_t index;

    words[base] = source_tick;
    words[base + 1U] = sourceboot_pack_applied_input(runtime);
    words[base + 2U] = sourceboot_state_flags(camera);
    words[base + 3U] = 0U;
    for (index = 0U; index < SM64_SATURN_CAMERA_PROBE_STATE_WORDS; index++)
        words[base + 4U + index] = camera->state_words[index];
}

static void sourceboot_backfill_stable_window(
        const sm64_saturn_source_runtime_state_t *runtime)
{
    uint32_t index;
    for (index = 0U; index < SOURCEBOOT_STABLE_TICKS; index++) {
        sourceboot_write_sample(index, s_sourceboot_candidate_first_tick + index,
                                runtime, &s_sourceboot_camera_candidate);
    }
    s_sourceboot_sample_count = SOURCEBOOT_STABLE_TICKS;
}

static void sourceboot_publish_header(
        const sm64_saturn_source_runtime_state_t *runtime,
        const sm64_saturn_camera_probe_snapshot_t *camera)
{
    volatile uint32_t *words = sourceboot_capture_words();
    const uint32_t idle_start_tick =
        s_sourceboot_candidate_first_tick - SOURCEBOOT_REPLAY_TICKS;

    words[0] = 0U;
    words[1] = SOURCEBOOT_SCC1_VERSION;
    words[2] = SOURCEBOOT_SCC1_HEADER_WORDS;
    words[3] = SOURCEBOOT_SCC1_SAMPLE_WORDS;
    words[4] = SOURCEBOOT_SCC1_SAMPLE_COUNT;
    words[5] = SATURN_CAMERA_VARIANT;
    words[6] = 2U;
    words[7] = SOURCEBOOT_SBR4_MAGIC;
    words[8] = SOURCEBOOT_SBR4_VERSION;
    words[9] = SOURCEBOOT_REPLAY_TICKS;
    words[10] = idle_start_tick;
    words[11] = s_sourceboot_candidate_first_tick;
    words[12] = sourceboot_pack_applied_input(runtime);
    words[13] = sourceboot_state_flags(camera);
    words[14] = camera->diagnostics.overflow_count;
    words[15] = camera->diagnostics.saturation_count;
    words[16] = camera->diagnostics.divide_fault_count;
    words[17] = camera->diagnostics.unexpected_reseed_count;
    words[18] = camera->diagnostics.range_fallback_count;
    words[19] = camera->diagnostics.bridge_export_count;
    words[20] = camera->diagnostics.bridge_import_count;
    words[21] = camera->diagnostics.shadow_generation;
    words[22] = SOURCEBOOT_SCC1_SAMPLE_WORDS *
        SOURCEBOOT_SCC1_SAMPLE_COUNT;
    words[23] = SOURCEBOOT_CAMERA_ROUTE_ID;
    /* Completion is visible only after every sample, counter, and header
     * field has reached the cache-through alias. */
    words[0] = SOURCEBOOT_SCC1_MAGIC;
    s_sourceboot_capture_complete = 1U;
}

void sm64_saturn_sourceboot_camera_idle_probe_reset(void)
{
    volatile uint32_t *words = sourceboot_capture_words();
    uint32_t index;
    for (index = 0U; index < SOURCEBOOT_SCC1_TOTAL_WORDS; index++)
        words[index] = 0U;
    s_sourceboot_candidate_first_tick = 0U;
    s_sourceboot_stable_ticks = 0U;
    s_sourceboot_sample_count = 0U;
    s_sourceboot_candidate_valid = 0U;
    s_sourceboot_capture_failed = 0U;
    s_sourceboot_capture_complete = 0U;
}

void sm64_saturn_sourceboot_camera_idle_probe_record(
        const sm64_saturn_source_runtime_state_t *runtime,
        uint32_t source_tick)
{
    sm64_saturn_camera_probe_snapshot_t camera;
    sourceboot_mario_semantic_t mario;
    int stable;

    if (s_sourceboot_capture_failed || s_sourceboot_capture_complete)
        return;
    if (runtime == NULL || !runtime->input_replay_complete ||
        runtime->input_replay_ticks != SOURCEBOOT_REPLAY_TICKS ||
        sourceboot_pack_applied_input(runtime) != 0U)
        return;
#if !SATURN_CAMERA_IDLE_DISCOVERY
    const uint32_t fixed_first_tick =
        SOURCEBOOT_REPLAY_TICKS + SATURN_CAMERA_IDLE_START_TICK;
    if (!s_sourceboot_candidate_valid && source_tick < fixed_first_tick)
        return;
    if (!s_sourceboot_candidate_valid && source_tick > fixed_first_tick) {
        s_sourceboot_capture_failed = 1U;
        return;
    }
#endif
    if (!sm64_saturn_camera_probe_read(&camera) ||
        !sourceboot_mario_read(&mario) ||
        (camera.camera_flags & 0x19U) != 0x19U) {
#if SATURN_CAMERA_IDLE_DISCOVERY
        s_sourceboot_candidate_valid = 0U;
        s_sourceboot_stable_ticks = 0U;
        return;
#else
        s_sourceboot_capture_failed = 1U;
        return;
#endif
    }

    if (!s_sourceboot_candidate_valid) {
        s_sourceboot_camera_candidate = camera;
        s_sourceboot_mario_candidate = mario;
        s_sourceboot_candidate_first_tick = source_tick;
        s_sourceboot_stable_ticks = 1U;
        s_sourceboot_candidate_valid = 1U;
        return;
    }
    stable = sourceboot_words_equal(
        s_sourceboot_camera_candidate.state_words, camera.state_words,
        SM64_SATURN_CAMERA_PROBE_STATE_WORDS) &&
        sourceboot_mario_equal(&s_sourceboot_mario_candidate, &mario);
    if (!stable) {
        if (s_sourceboot_sample_count != 0U) {
            s_sourceboot_capture_failed = 1U;
            return;
        }
#if SATURN_CAMERA_IDLE_DISCOVERY
        s_sourceboot_camera_candidate = camera;
        s_sourceboot_mario_candidate = mario;
        s_sourceboot_candidate_first_tick = source_tick;
        s_sourceboot_stable_ticks = 1U;
        return;
#else
        s_sourceboot_capture_failed = 1U;
        return;
#endif
    }

    if (s_sourceboot_sample_count == 0U) {
        s_sourceboot_stable_ticks++;
        if (s_sourceboot_stable_ticks < SOURCEBOOT_STABLE_TICKS)
            return;
#if SATURN_CAMERA_IDLE_DISCOVERY
        s_sourceboot_candidate_first_tick = source_tick - 59U;
#endif
        sourceboot_backfill_stable_window(runtime);
    } else {
        if (source_tick !=
            s_sourceboot_candidate_first_tick + s_sourceboot_sample_count) {
            s_sourceboot_capture_failed = 1U;
            return;
        }
        sourceboot_write_sample(s_sourceboot_sample_count, source_tick,
                                runtime, &camera);
        s_sourceboot_sample_count++;
    }
    if (s_sourceboot_sample_count == SOURCEBOOT_SCC1_SAMPLE_COUNT)
        sourceboot_publish_header(runtime, &camera);
}
