#ifndef SM64_SATURN_SCENE_PROFILE_H
#define SM64_SATURN_SCENE_PROFILE_H

#include <stdint.h>

/*
 * Probe-safe, scene-neutral visibility profile.  A viewer exposes one
 * volatile instance under the `saturn_scene_profile` symbol so Ymir can read
 * phase timings without guessing from the legacy HWTEST telemetry address.
 * Keep this record POD and 16-bit-field-only: it is deliberately readable as
 * a big-endian SH-2 memory window from the remote debugger.  The two fields
 * named `mario_rejected` and `mario_culled` are queue-wide totals (the legacy
 * names are retained for probe compatibility); the static-only counts below
 * remain separately reported for the cache decision.
 */
typedef struct sm64_saturn_scene_profile {
    uint16_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t sort_mario_ticks;
    uint16_t static_rebuild_ticks;
    uint16_t emit_ticks;
    uint16_t sort_cache_hit;
    uint16_t static_cache_hit;
    uint16_t static_stream_count;
    uint16_t mario_visible;
    uint16_t mario_rejected;
    uint16_t mario_culled;
    uint16_t mario_edge_changed;
    uint16_t static_rejected;
    uint16_t static_culled;
    uint16_t mario_bsp_tests;
    uint16_t mario_bsp_refined_clusters;
    uint16_t mario_bsp_straddlers;
    uint16_t scene_dirty;
    uint16_t mario_dirty;
    uint16_t static_valid;
} sm64_saturn_scene_profile_t;

#define SM64_SATURN_SCENE_PROFILE_MAGIC 0x5343U /* `SC` */
#define SM64_SATURN_SCENE_PROFILE_VERSION 1U

#endif
