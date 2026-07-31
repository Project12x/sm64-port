#ifndef SM64_SATURN_CAMERA_PROBE_H
#define SM64_SATURN_CAMERA_PROBE_H

#include <stdint.h>
#include <ultra64.h>

#define SM64_SATURN_CAMERA_PROBE_STATE_WORDS 77U

enum {
    SM64_SATURN_CAMERA_PROBE_FLAG_CAMERA_VALID = 1U << 0,
    SM64_SATURN_CAMERA_PROBE_FLAG_REPLAY_COMPLETE = 1U << 1,
    SM64_SATURN_CAMERA_PROBE_FLAG_NEUTRAL_INPUT = 1U << 2,
    SM64_SATURN_CAMERA_PROBE_FLAG_NO_CUTSCENE = 1U << 3,
    SM64_SATURN_CAMERA_PROBE_FLAG_NO_TRANSITION = 1U << 4,
    SM64_SATURN_CAMERA_PROBE_FLAG_MARIO_QUIESCENT = 1U << 5,
};

typedef struct sm64_saturn_camera_q_diagnostics {
    uint32_t overflow_count;
    uint32_t saturation_count;
    uint32_t divide_fault_count;
    uint32_t unexpected_reseed_count;
    uint32_t range_fallback_count;
    uint32_t bridge_export_count;
    uint32_t bridge_import_count;
    uint32_t shadow_generation;
} sm64_saturn_camera_q_diagnostics_t;

typedef struct sm64_saturn_camera_probe_snapshot {
    uint32_t state_words[SM64_SATURN_CAMERA_PROBE_STATE_WORDS];
    uint32_t camera_flags;
    uint32_t source_dispatch;
    sm64_saturn_camera_q_diagnostics_t diagnostics;
} sm64_saturn_camera_probe_snapshot_t;

s32 sm64_saturn_camera_probe_read(
    sm64_saturn_camera_probe_snapshot_t *snapshot);

#endif
