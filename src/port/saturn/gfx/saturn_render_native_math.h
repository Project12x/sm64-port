#ifndef SM64_SATURN_RENDER_NATIVE_MATH_H
#define SM64_SATURN_RENDER_NATIVE_MATH_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if defined(__sh__)
#include <yaul.h>
#endif

/* The source simulation owns its float state, but the render snapshot is a
 * one-way integer boundary. Decode IEEE-754 directly so the bridge does not
 * emit a soft-float float-to-int helper. Values outside the sourceboot world
 * envelope saturate rather than invoking C's undefined out-of-range cast. */
static inline int32_t
sm64_saturn_world_unit_from_float(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    const uint32_t exponent = (bits >> 23) & 0xFFU;
    const uint32_t sign = bits >> 31;
    uint32_t magnitude;

    if (exponent < 127U) return 0;
    if (exponent >= 158U) return sign != 0U ? INT32_MIN : INT32_MAX;
    magnitude = (bits & 0x007FFFFFU) | 0x00800000U;
    if (exponent <= 150U)
        magnitude >>= 150U - exponent;
    else
        magnitude <<= exponent - 150U;
    if (sign == 0U) return (int32_t)magnitude;
    return magnitude >= 0x80000000U ? INT32_MIN : -(int32_t)magnitude;
}

/* The SH-2 DIVU consumes a signed 64-bit dividend as high/low 32-bit words
 * and a signed 32-bit divisor. Keeping the host fallback here makes the
 * renderer's differential fixture exercise the same overflow contract. */
static inline bool
sm64_saturn_div_s64_s32(int64_t dividend, int32_t divisor, int32_t *quotient)
{
    if (quotient == NULL || divisor == 0) return false;
#if defined(__sh__)
    const uint64_t raw_dividend = (uint64_t)dividend;
    cpu_divu_64_32_set((uint32_t)(raw_dividend >> 32),
                       (uint32_t)raw_dividend, (uint32_t)divisor);
    if (cpu_divu_status_get()) return false;
    *quotient = (int32_t)cpu_divu_quotient_get();
#else
#if defined(SM64_SATURN_TEST_MUTATE_Q16_DIV)
    /* Deliberate host-fixture mutation. The Make target must reject this. */
    const int64_t result = dividend + divisor;
#else
    const int64_t result = dividend / divisor;
#endif
    if (result > INT32_MAX || result < INT32_MIN) return false;
    *quotient = (int32_t)result;
#endif
    return true;
}

#endif
