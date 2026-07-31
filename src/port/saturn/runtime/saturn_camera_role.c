#include "saturn_camera_role.h"
#include "saturn_camera_fixed.h"

#include "game/camera.h"
#include "types.h"

extern struct MarioState *gMarioState;

#ifndef SATURN_CAMERA_VARIANT
#define SATURN_CAMERA_VARIANT 1
#endif

#if SATURN_CAMERA_VARIANT == 2
static struct Camera s_bypass_camera;
static struct LakituState s_bypass_lakitu;
static bool s_bypass_armed;
#endif

#if SATURN_CAMERA_VARIANT == 3
static sm64_saturn_camera_fixed_state_t s_fixed_camera;
static bool s_fixed_camera_initialized;
#endif

void sm64_saturn_camera_bypass_arm(uint32_t source_tick)
{
#if SATURN_CAMERA_VARIANT == 2
    (void)source_tick;
    if (!s_bypass_armed && gCamera != NULL) {
        s_bypass_camera = *gCamera;
        s_bypass_lakitu = gLakituState;
        s_bypass_armed = true;
    }
#else
    (void)source_tick;
#endif
}

bool sm64_saturn_camera_bypass_is_armed(void)
{
#if SATURN_CAMERA_VARIANT == 2
    return s_bypass_armed;
#else
    return false;
#endif
}

bool sm64_saturn_camera_role_update(struct Camera *camera)
{
#if SATURN_CAMERA_VARIANT == 2
    if (s_bypass_armed) {
        *camera = s_bypass_camera;
        gLakituState = s_bypass_lakitu;
        return true;
    }
#elif SATURN_CAMERA_VARIANT == 3
    if (!s_fixed_camera_initialized) {
        sm64_saturn_camera_fixed_init(&s_fixed_camera, camera);
        s_fixed_camera_initialized = true;
    }
    if (gMarioState != NULL)
        sm64_saturn_camera_fixed_step(&s_fixed_camera, gMarioState, 0, 0);
    sm64_saturn_camera_fixed_publish(&s_fixed_camera, camera);
    return true;
#else
    (void)camera;
#endif
    return false;
}
