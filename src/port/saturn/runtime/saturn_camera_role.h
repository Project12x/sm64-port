#ifndef SM64_SATURN_CAMERA_ROLE_H
#define SM64_SATURN_CAMERA_ROLE_H

#include <stdbool.h>
#include <stdint.h>

struct Camera;

typedef enum sm64_saturn_camera_role {
    SM64_SATURN_CAMERA_SOURCE_BASELINE = 1,
    SM64_SATURN_CAMERA_BYPASS_DIAGNOSTIC = 2,
    SM64_SATURN_CAMERA_FIXED_CANDIDATE = 3,
} sm64_saturn_camera_role_t;

bool sm64_saturn_camera_role_update(struct Camera *camera);
void sm64_saturn_camera_bypass_arm(uint32_t source_tick);
bool sm64_saturn_camera_bypass_is_armed(void);

#endif
