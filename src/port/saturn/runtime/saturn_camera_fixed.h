#ifndef SM64_SATURN_CAMERA_FIXED_H
#define SM64_SATURN_CAMERA_FIXED_H

#include <stdint.h>

struct Camera;
struct LakituState;
struct MarioState;

typedef struct sm64_saturn_camera_fixed_state {
    int32_t desired_focus[3];
    int32_t rendered_focus[3];
    int32_t rendered_position[3];
    int16_t yaw;
    int16_t pitch;
    int32_t distance;
    int32_t vertical_offset;
    uint32_t diagnostics;
} sm64_saturn_camera_fixed_state_t;

#define SM64_SATURN_CAMERA_FIXED_DIAG_SATURATED (1U << 0)
#define SM64_SATURN_CAMERA_FIXED_DIAG_UNSUPPORTED_OBSTRUCTION (1U << 1)

void sm64_saturn_camera_fixed_init(sm64_saturn_camera_fixed_state_t *state,
                                   const struct Camera *source_camera);
void sm64_saturn_camera_fixed_step(sm64_saturn_camera_fixed_state_t *state,
                                   const struct MarioState *mario,
                                   int16_t input_yaw, int16_t input_pitch);
void sm64_saturn_camera_fixed_publish(const sm64_saturn_camera_fixed_state_t *state,
                                      struct Camera *camera,
                                      struct LakituState *lakitu);

#endif
