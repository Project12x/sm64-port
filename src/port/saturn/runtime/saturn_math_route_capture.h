#ifndef SM64_SATURN_MATH_ROUTE_CAPTURE_H
#define SM64_SATURN_MATH_ROUTE_CAPTURE_H

#include <stdint.h>

#if defined(TARGET_SATURN) && defined(SATURN_SOURCEBOOT_ROUTE_REPLAY) && \
    SATURN_SOURCEBOOT_ROUTE_REPLAY

#define SM64_SATURN_MATH_ROUTE_CAPTURE_MAGIC 0x534D4331U /* "SMC1" */
#define SM64_SATURN_MATH_ROUTE_CAPTURE_VERSION 1U

typedef struct sm64_saturn_math_route_capture {
    uint32_t magic;
    uint32_t version;
    uint32_t replay_ticks;
    uint32_t atan2s_calls;
    uint32_t atan2_lookup_calls;
    uint32_t last_atan2s_y_bits;
    uint32_t last_atan2s_x_bits;
    uint32_t last_atan2_lookup_y_bits;
    uint32_t last_atan2_lookup_x_bits;
} sm64_saturn_math_route_capture_t;

extern volatile sm64_saturn_math_route_capture_t sourceboot_math_route_capture;

static inline uint32_t sm64_saturn_math_route_float_bits(float value)
{
    union { float f; uint32_t u; } bits = { value };
    return bits.u;
}

static inline void sm64_saturn_math_route_record_atan2s(float y, float x)
{
    sourceboot_math_route_capture.atan2s_calls++;
    sourceboot_math_route_capture.last_atan2s_y_bits = sm64_saturn_math_route_float_bits(y);
    sourceboot_math_route_capture.last_atan2s_x_bits = sm64_saturn_math_route_float_bits(x);
}

static inline void sm64_saturn_math_route_record_atan2_lookup(float y, float x)
{
    sourceboot_math_route_capture.atan2_lookup_calls++;
    sourceboot_math_route_capture.last_atan2_lookup_y_bits = sm64_saturn_math_route_float_bits(y);
    sourceboot_math_route_capture.last_atan2_lookup_x_bits = sm64_saturn_math_route_float_bits(x);
}

#endif
#endif
