#include "saturn_camera_role.h"
#include "saturn_camera_fixed.h"

#include "game/camera.h"
#include "game/game_init.h"
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

#if SATURN_CAMERA_VARIANT == 3
/* S4.1: per-tick yaw delta from the C-buttons.
 *
 * The step adds this straight onto a 16-bit angle (0x10000 == 360 degrees),
 * so 0x0200 is about 2.8 degrees per simulation tick. Deliberately a constant
 * rate rather than an acceleration curve: the fixed camera has no collision or
 * obstruction handling, and a fast sweep through terrain would expose that far
 * more than it would help. Held rather than edge-triggered, because the player
 * needs to be able to line up a view, not step it.
 *
 * Pitch stays 0. The step applies pitch without clamping it, so driving it
 * from input would let the camera pass through vertical and invert; giving it
 * a real range belongs with the obstruction work, not here. */
#define SATURN_CAMERA_YAW_STEP 0x0200
#define SATURN_CAMERA_PITCH_STEP 0x0140
/* About 56 degrees. The camera has no obstruction handling, so the useful
 * limit is wherever the view stops clipping terrain rather than straight
 * down; and clamping short of vertical is what stops the orbit passing over
 * the top and inverting, which the raw accumulate would happily do. */
#define SATURN_CAMERA_PITCH_LIMIT 0x2800

static int16_t saturn_camera_input_yaw(void)
{
    uint16_t held;
    int32_t yaw = 0;
    if (gPlayer1Controller == NULL) return 0;
    held = (uint16_t)gPlayer1Controller->buttonDown;
    if (held & L_CBUTTONS) yaw -= SATURN_CAMERA_YAW_STEP;
    if (held & R_CBUTTONS) yaw += SATURN_CAMERA_YAW_STEP;
    return (int16_t)yaw;
}

/* Returns the delta that lands inside the limit, rather than a fixed step the
 * evaluator would accumulate past it -- the step adds input straight onto the
 * stored angle and does no clamping of its own. */
static int16_t saturn_camera_input_pitch(int16_t current)
{
    uint16_t held;
    int32_t target;
    int32_t delta = 0;
    if (gPlayer1Controller == NULL) return 0;
    held = (uint16_t)gPlayer1Controller->buttonDown;
    if (held & U_CBUTTONS) delta += SATURN_CAMERA_PITCH_STEP;
    if (held & D_CBUTTONS) delta -= SATURN_CAMERA_PITCH_STEP;
    if (delta == 0) return 0;
    target = (int32_t)current + delta;
    if (target > SATURN_CAMERA_PITCH_LIMIT) target = SATURN_CAMERA_PITCH_LIMIT;
    if (target < -SATURN_CAMERA_PITCH_LIMIT) target = -SATURN_CAMERA_PITCH_LIMIT;
    return (int16_t)(target - (int32_t)current);
}
#endif

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
        sm64_saturn_camera_fixed_step(
            &s_fixed_camera, gMarioState, saturn_camera_input_yaw(),
            saturn_camera_input_pitch(s_fixed_camera.pitch));
    sm64_saturn_camera_fixed_publish(&s_fixed_camera, camera, &gLakituState);
    /* The camera HUD icon is driven by this and was never called on the fixed
     * path, so it sat frozen on whatever state the last Lakitu update left. */
    (void)update_camera_hud_status(camera);
    return true;
#else
    (void)camera;
#endif
    return false;
}
