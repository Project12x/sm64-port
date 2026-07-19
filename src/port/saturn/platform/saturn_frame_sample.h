#ifndef SM64_SATURN_FRAME_SAMPLE_H
#define SM64_SATURN_FRAME_SAMPLE_H

#include <stdint.h>

/*
 * Stable, all-16-bit remote probe record. The scene profile describes the
 * painter cache; this record is written after the VBlank boundary so a Ymir
 * read observes one complete frame budget rather than rolling counters.
 * `sequence` is odd while the record is being written and even when complete.
 */
typedef struct sm64_saturn_frame_sample {
    uint16_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t sequence;
    uint16_t update_ticks;
    uint16_t sort_ticks;
    uint16_t command_ticks;
    uint16_t wait_ticks;
    uint16_t vblank_ticks;
    uint16_t render_ticks_hi;
    uint16_t render_ticks_lo;
    uint16_t loop_ticks_hi;
    uint16_t loop_ticks_lo;
    uint16_t mario_walking;
    uint16_t animation_frame;
    uint16_t mario_world_x_hi;
    uint16_t mario_world_x_lo;
    uint16_t mario_world_y_hi;
    uint16_t mario_world_y_lo;
    uint16_t mario_world_z_hi;
    uint16_t mario_world_z_lo;
} sm64_saturn_frame_sample_t;

#define SM64_SATURN_FRAME_SAMPLE_MAGIC 0x4653U /* `FS` */
#define SM64_SATURN_FRAME_SAMPLE_VERSION 1U

#endif
