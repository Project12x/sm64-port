#ifndef SM64_SATURN_MATH_ROUTE_CAPTURE_H
#define SM64_SATURN_MATH_ROUTE_CAPTURE_H

#include <stdint.h>

#if defined(TARGET_SATURN) && defined(SATURN_SOURCEBOOT_ROUTE_REPLAY) && \
    SATURN_SOURCEBOOT_ROUTE_REPLAY

#define SM64_SATURN_MATH_ROUTE_CAPTURE_MAGIC 0x534D4331U /* "SMC1" */
#define SM64_SATURN_MATH_ROUTE_CAPTURE_VERSION 2U
#define SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES 64U

typedef struct sm64_saturn_math_route_sample {
    uint32_t y_bits;
    uint32_t x_bits;
    uint32_t result;
} sm64_saturn_math_route_sample_t;

typedef struct sm64_saturn_math_route_capture {
    uint32_t magic;
    uint32_t version;
    uint32_t replay_ticks;
    uint32_t atan2s_calls;
    uint32_t atan2_lookup_calls;
    uint32_t atan2s_samples;
    uint32_t atan2_lookup_samples;
    sm64_saturn_math_route_sample_t atan2s[SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES];
    sm64_saturn_math_route_sample_t atan2_lookup[SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES];
} sm64_saturn_math_route_capture_t;

extern volatile sm64_saturn_math_route_capture_t sourceboot_math_route_capture;

static inline uint32_t sm64_saturn_math_route_float_bits(float value)
{
    union { float f; uint32_t u; } bits = { value };
    return bits.u;
}

static inline void sm64_saturn_math_route_record_atan2s(float y, float x, uint16_t result)
{
    const uint32_t call = sourceboot_math_route_capture.atan2s_calls++;
    const uint32_t slot = call % SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES;
    sm64_saturn_math_route_sample_t *sample =
        (sm64_saturn_math_route_sample_t *)&sourceboot_math_route_capture.atan2s[slot];
    sample->y_bits = sm64_saturn_math_route_float_bits(y);
    sample->x_bits = sm64_saturn_math_route_float_bits(x);
    sample->result = result;
    if (sourceboot_math_route_capture.atan2s_samples <
        SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES)
        sourceboot_math_route_capture.atan2s_samples++;
}

static inline void sm64_saturn_math_route_record_atan2_lookup(float y, float x, uint16_t result)
{
    const uint32_t y_bits = sm64_saturn_math_route_float_bits(y);
    const uint32_t x_bits = sm64_saturn_math_route_float_bits(x);
    const uint32_t call = sourceboot_math_route_capture.atan2_lookup_calls++;
    const uint32_t slot = call % SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES;
    sm64_saturn_math_route_sample_t *sample =
        (sm64_saturn_math_route_sample_t *)&sourceboot_math_route_capture.atan2_lookup[slot];
    sample->y_bits = y_bits;
    sample->x_bits = x_bits;
    sample->result = result;
    if (sourceboot_math_route_capture.atan2_lookup_samples <
        SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES)
        sourceboot_math_route_capture.atan2_lookup_samples++;
}

static inline void sm64_saturn_math_route_record_atan2_lookup_bits(uint32_t y_bits,
                                                                    uint32_t x_bits,
                                                                    uint16_t result)
{
    const uint32_t call = sourceboot_math_route_capture.atan2_lookup_calls++;
    const uint32_t slot = call % SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES;
    sm64_saturn_math_route_sample_t *sample =
        (sm64_saturn_math_route_sample_t *)&sourceboot_math_route_capture.atan2_lookup[slot];
    sample->y_bits = y_bits;
    sample->x_bits = x_bits;
    sample->result = result;
    if (sourceboot_math_route_capture.atan2_lookup_samples <
        SM64_SATURN_MATH_ROUTE_CAPTURE_SAMPLES)
        sourceboot_math_route_capture.atan2_lookup_samples++;
}

#endif
#endif
