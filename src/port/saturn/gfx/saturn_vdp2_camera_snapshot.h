#ifndef SM64_SATURN_VDP2_CAMERA_SNAPSHOT_H
#define SM64_SATURN_VDP2_CAMERA_SNAPSHOT_H

#include <stdint.h>

/* Immutable camera state co-owned by one published VDP1/VDP2 generation. */
typedef struct sm64_saturn_vdp2_camera_snapshot {
    int16_t yaw;
    int16_t pitch;
    uint8_t valid;
    uint32_t generation;
} sm64_saturn_vdp2_camera_snapshot_t;

#endif
