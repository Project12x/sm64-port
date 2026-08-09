/* TARGET_SATURN engine-math seam: the original float API remains intact, but
 * atan2's hot scalar arithmetic is decoded into Q16.16 then completed with
 * native integer operations.  The conversion is deliberately one-way: code
 * outside this seam continues to own Vec3f and float state. */
#ifndef SM64_SATURN_ENGINE_MATH_Q16_H
#define SM64_SATURN_ENGINE_MATH_Q16_H

#include <stdint.h>

#if defined(__sh__)
#include <yaul.h>

/* SH-2 DIVU is at CPU(0x0f00): DVSR 0xffffff00, DVCR 0xffffff08,
 * DVDNTL 0xffffff14 -- same literal address used by the proven-correct
 * launch pattern in gpl/slavedriver_projection.h. DVCR's overflow bit is
 * sticky and does not auto-clear on the next division. */
#define SM64_SATURN_DIVU_DVCR ((volatile uint32_t *)0xFFFFFF08u)
#endif

static inline int32_t
sm64_saturn_float_to_q16_trunc(float value)
{
    union { float f; uint32_t u; } bits = { value };
    const uint32_t sign = bits.u >> 31;
    const uint32_t exponent = (bits.u >> 23) & 0xFFU;
    uint32_t magnitude;
    uint32_t mantissa;
    int32_t shift;

    if (exponent == 0U) return 0;
    /* Keep the negative endpoint one unit inside INT32_MIN: atan2 needs the
     * magnitude after selecting its quadrant, and negating INT32_MIN would
     * be undefined.  Both endpoints are already outside the captured seam. */
    if (exponent >= 142U) return sign != 0U ? -INT32_MAX : INT32_MAX;

    mantissa = (bits.u & 0x007FFFFFU) | 0x00800000U;
    shift = (int32_t)exponent - 134;
    magnitude = shift >= 0 ? mantissa << shift : mantissa >> -shift;
    if (magnitude == 0U) return 0;
    return sign != 0U ? -(int32_t)magnitude : (int32_t)magnitude;
}

static inline uint32_t
sm64_saturn_atan2_q16_index(uint32_t y, uint32_t x)
{
    uint32_t high;
    uint32_t low;

    if (x == 0U) return 0;
    high = y >> 22;
    low = y << 10;
    {
        const uint32_t rounded = low + (x >> 1);
        high += rounded < low;
        low = rounded;
    }
#if defined(__sh__)
    /* Clear a prior DVCR overflow before launch; collect checks the fresh bit. */
    *SM64_SATURN_DIVU_DVCR &= ~1u;
    cpu_divu_64_32_set(high, low, x);
    {
        const uint32_t quotient = cpu_divu_status_get() ? 1024U : cpu_divu_quotient_get();
#if defined(SM64_SATURN_TEST_MUTATE_ATAN2_Q16)
        return quotient == 0U ? 1U : quotient - 1U;
#else
        return quotient;
#endif
    }
#else
    {
        const uint32_t quotient = (uint32_t)((((uint64_t)high << 32) | low) / x);
#if defined(SM64_SATURN_TEST_MUTATE_ATAN2_Q16)
        return quotient == 0U ? 1U : quotient - 1U;
#else
        return quotient;
#endif
    }
#endif
}

static inline uint16_t
sm64_saturn_atan2_lookup_q16(int32_t y, int32_t x, const int16_t *table)
{
    if (x == 0) return table[0];
    return table[sm64_saturn_atan2_q16_index((uint32_t)y, (uint32_t)x)];
}

#endif
