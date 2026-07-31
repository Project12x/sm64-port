#include "saturn_camera_fixed.h"

#include <limits.h>
#include <string.h>

#include "game/camera.h"
#include "types.h"
#include "port/saturn/gfx/saturn_q16_sh2.h"

extern const int32_t gSaturnSineTableQ16[0x1400];

#define SM64_SATURN_CAMERA_Q16_ONE 0x00010000
#define SM64_SATURN_CAMERA_FOLLOW_DISTANCE (800 * SM64_SATURN_CAMERA_Q16_ONE)
#define SM64_SATURN_CAMERA_FOCUS_HEIGHT (120 * SM64_SATURN_CAMERA_Q16_ONE)

static int32_t sm64_saturn_camera_fixed_add(sm64_saturn_camera_fixed_state_t *state,
                                            int32_t left, int32_t right)
{
    if (right > 0 && left > INT32_MAX - right) {
        state->diagnostics |= SM64_SATURN_CAMERA_FIXED_DIAG_SATURATED;
        return INT32_MAX;
    }
    if (right < 0 && left < INT32_MIN - right) {
        state->diagnostics |= SM64_SATURN_CAMERA_FIXED_DIAG_SATURATED;
        return INT32_MIN;
    }
    return left + right;
}

static int32_t sm64_saturn_camera_fixed_import_q16(f32 source)
{
    uint32_t bits;
    uint32_t significand;
    uint32_t shifted;
    uint32_t exponent;
    int32_t power;

    memcpy(&bits, &source, sizeof(bits));
    exponent = (bits >> 23) & 0xffU;
    if (exponent == 0U)
        return 0;
    if (exponent == 0xffU || exponent > 142U)
        return (bits & 0x80000000U) != 0U ? INT32_MIN : INT32_MAX;
    significand = (bits & 0x007fffffU) | 0x00800000U;
    power = (int32_t)exponent - 134;
    if (power >= 0)
        shifted = significand << (uint32_t)power;
    else if (power > -24)
        shifted = significand >> (uint32_t)(-power);
    else
        shifted = 0U;
    if ((bits & 0x80000000U) != 0U)
        return shifted >= 0x80000000U ? INT32_MIN : -(int32_t)shifted;
    return shifted > INT32_MAX ? INT32_MAX : (int32_t)shifted;
}

static uint32_t sm64_saturn_camera_fixed_export_bits(int32_t value)
{
    uint32_t magnitude;
    uint32_t leading = 0U;
    uint32_t mantissa;
    uint32_t sign = 0U;
    uint32_t exponent;

    if (value == 0)
        return 0U;
    if (value < 0) {
        sign = 0x80000000U;
        magnitude = (uint32_t)(~(uint32_t)value) + 1U;
    } else {
        magnitude = (uint32_t)value;
    }
    while ((magnitude >> (leading + 1U)) != 0U)
        leading++;
    exponent = leading + 111U;
    if (leading <= 23U)
        mantissa = (magnitude & ~(1U << leading)) << (23U - leading);
    else
        mantissa = (magnitude & ~(1U << leading)) >> (leading - 23U);
    return sign | (exponent << 23) | (mantissa & 0x007fffffU);
}

static int32_t sm64_saturn_camera_fixed_approach(sm64_saturn_camera_fixed_state_t *state,
                                                  int32_t current, int32_t target)
{
    int32_t delta;
    int32_t step;

    if (target > current) {
        if (current < 0 && target > INT32_MAX + current) {
            state->diagnostics |= SM64_SATURN_CAMERA_FIXED_DIAG_SATURATED;
            return sm64_saturn_camera_fixed_add(state, current,
                                                16 * SM64_SATURN_CAMERA_Q16_ONE);
        }
        delta = target - current;
        step = delta >> 2;
        if (step == 0)
            step = 1;
        return sm64_saturn_camera_fixed_add(state, current, step);
    }
    if (target < current) {
        if (target < 0 && current > INT32_MAX + target) {
            state->diagnostics |= SM64_SATURN_CAMERA_FIXED_DIAG_SATURATED;
            return sm64_saturn_camera_fixed_add(state, current,
                                                -(16 * SM64_SATURN_CAMERA_Q16_ONE));
        }
        delta = current - target;
        step = delta >> 2;
        if (step == 0)
            step = 1;
        return sm64_saturn_camera_fixed_add(state, current, -step);
    }
    return current;
}

void sm64_saturn_camera_fixed_init(sm64_saturn_camera_fixed_state_t *state,
                                   const struct Camera *source_camera)
{
    uint32_t index;

    memset(state, 0, sizeof(*state));
    for (index = 0U; index < 3U; index++) {
        state->desired_focus[index] = sm64_saturn_camera_fixed_import_q16(source_camera->focus[index]);
        state->rendered_focus[index] = state->desired_focus[index];
        state->rendered_position[index] = sm64_saturn_camera_fixed_import_q16(source_camera->pos[index]);
    }
    state->yaw = source_camera->yaw;
    state->distance = SM64_SATURN_CAMERA_FOLLOW_DISTANCE;
    state->vertical_offset = SM64_SATURN_CAMERA_FOCUS_HEIGHT;
}

void sm64_saturn_camera_fixed_step(sm64_saturn_camera_fixed_state_t *state,
                                   const struct MarioState *mario,
                                   int16_t input_yaw, int16_t input_pitch)
{
    int32_t target_position[3];
    int32_t sin_yaw;
    int32_t cos_yaw;
    uint32_t index = ((uint16_t)state->yaw) >> 4;
    uint32_t axis;

    state->yaw = (int16_t)((uint16_t)state->yaw + (uint16_t)input_yaw);
    state->pitch = (int16_t)((uint16_t)state->pitch + (uint16_t)input_pitch);
    index = ((uint16_t)state->yaw) >> 4;
    sin_yaw = gSaturnSineTableQ16[index];
    cos_yaw = gSaturnSineTableQ16[index + 0x400U];
    for (axis = 0U; axis < 3U; axis++)
        state->desired_focus[axis] = sm64_saturn_camera_fixed_import_q16(mario->pos[axis]);
    state->desired_focus[1] = sm64_saturn_camera_fixed_add(
        state, state->desired_focus[1], state->vertical_offset);
    target_position[0] = sm64_saturn_camera_fixed_add(
        state, state->desired_focus[0], sm64_saturn_q16_mul_sh2(state->distance, sin_yaw));
    target_position[1] = state->desired_focus[1];
    target_position[2] = sm64_saturn_camera_fixed_add(
        state, state->desired_focus[2], sm64_saturn_q16_mul_sh2(state->distance, cos_yaw));
    for (axis = 0U; axis < 3U; axis++) {
        state->rendered_focus[axis] = sm64_saturn_camera_fixed_approach(
            state, state->rendered_focus[axis], state->desired_focus[axis]);
        state->rendered_position[axis] = sm64_saturn_camera_fixed_approach(
            state, state->rendered_position[axis], target_position[axis]);
    }
    state->diagnostics |= SM64_SATURN_CAMERA_FIXED_DIAG_UNSUPPORTED_OBSTRUCTION;
}

void sm64_saturn_camera_fixed_publish(const sm64_saturn_camera_fixed_state_t *state,
                                      struct Camera *camera)
{
    uint32_t axis;
    union { uint32_t bits; f32 value; } published;

    for (axis = 0U; axis < 3U; axis++) {
        published.bits = sm64_saturn_camera_fixed_export_bits(state->rendered_focus[axis]);
        memcpy(&camera->focus[axis], &published.value, sizeof(published.value));
        published.bits = sm64_saturn_camera_fixed_export_bits(state->rendered_position[axis]);
        memcpy(&camera->pos[axis], &published.value, sizeof(published.value));
    }
    camera->yaw = state->yaw;
    camera->nextYaw = state->yaw;
}
