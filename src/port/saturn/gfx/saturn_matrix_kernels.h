#ifndef SM64_SATURN_MATRIX_KERNELS_H
#define SM64_SATURN_MATRIX_KERNELS_H

#include <stdint.h>

#include "saturn_q16_sh2.h"

/* Q16.16 integer math primitives for the Saturn render-matrix pipeline.
 *
 * WHY THIS EXISTS: live evidence (2026-07-22, see
 * docs/saturn/evidence/e2-sourceboot-gmatstack-corruption-2026-07-22.md)
 * proved the pinned SH-2 toolchain (GCC 14.3.0) miscompiles/corrupts the
 * engine's soft-float matrix math (mtxf_lookat/mtxf_mul feeding
 * gMatStack) for real, non-degenerate camera inputs, and GCC 15.2.0 has
 * a different miscompilation of the same function at -Os. SH-2 has no
 * FPU; every float op is a libgcc soft-float call. These kernels remove
 * that dependency entirely for the rendering matrix path: 32-bit integer
 * and Q16.16 fixed-point only, using SH-2's native 32x32->64 multiply.
 *
 * PRECEDENT: Sega's own SGL uses the identical 16.16 FIXED format for
 * this exact job -- slLookAt(FIXED*, FIXED*, ANGLE) is a fixed-point
 * camera lookat (SGL MATH.TXT, SL_DEF.H:909; docs/saturn/
 * SGL_REFERENCE_NOTES.md), Sonic Z-Treme builds on that API, and
 * SlaveDriver ships integer sqrt tables (FLASH/SQRTTAB.H). These
 * kernels are clean-room implementations of the standard algorithms --
 * behavior-grounded in those references, no code copied (SGL is
 * proprietary doc-study-only; keeping this header GPL-free avoids
 * mixing GPL next to SM64-adjacent code).
 *
 * Host-testable by design (no Yaul dependency), same as saturn_matrix.h. */

extern const int32_t gSaturnSineTableQ16[0x1400];

/* Mirrors math_util.h's macros exactly: index = (u16)angle >> 4,
 * cosine = sine + 0x400, sine reads at high indices intentionally
 * overflowing into the cosine data. */
static inline int32_t sm64_saturn_sins_q16(int32_t angle)
{
    return gSaturnSineTableQ16[(uint16_t) angle >> 4];
}

static inline int32_t sm64_saturn_coss_q16(int32_t angle)
{
    return gSaturnSineTableQ16[((uint16_t) angle >> 4) + 0x400];
}

/* Q16.16 * Q16.16 -> Q16.16 via 64-bit intermediate (SH-2 dmuls.l).
 *
 * Overflow contract: safe whenever |a|,|b| <= (1 << 16) (i.e. both
 * operands represent magnitudes <= 1.0 in Q16.16) -- the 64-bit product
 * then satisfies |a*b| <= 1<<32, so the >>16 narrowing back to int32_t
 * never wraps. Every sin/cos-table lookup (sm64_saturn_sins_q16 /
 * sm64_saturn_coss_q16) and any direct product of two such lookups
 * (e.g. this sprint's rotation cross-terms like sxsy/sxcy/cxsy)
 * provably satisfies this bound.
 *
 * NOT every planned caller does, though -- verified against the plan's
 * own Task 3/4 text, not assumed: this sprint's Q16 mirror of
 * mtxf_scale_vec3f (sm64_saturn_mtxq_scale_vec3f) multiplies a general
 * matrix entry (bounded only by this port's +-32768 Q16.16 ceiling, see
 * saturn_matrix.h's decode note) by a general per-axis scale factor
 * pulled from real engine data (graph-node/object scale, which SM64
 * does push above 1x) -- neither operand is sin/cos-shaped, so that
 * call site's safety is a property of real-world data staying in
 * range, not a guarantee this function provides. It needs its own
 * justification (or a checked variant) when it lands, not an appeal to
 * this comment.
 *
 * Larger operands are NOT range-checked here (no bool-overflow return,
 * unlike sm64_saturn_matrix_mul in saturn_matrix.h) -- deliberately, to
 * avoid an API-wide signature change for a helper whose majority use is
 * the provably-bounded sin/cos case. If a future caller needs unbounded
 * Q16.16 operands with a hard safety guarantee, add an overflow-checked
 * variant rather than assuming this one covers it. */
static inline int32_t sm64_saturn_q16_mul(int32_t a, int32_t b)
{
    return sm64_saturn_q16_mul_sh2(a, b);
}

/* Floor integer square root of a non-negative 64-bit value.
 * Bit-by-bit method: pure shifts/compares, no multiply, no float.
 * Q-format usage: isqrt64(Q32 value) yields a Q16 result (sqrt halves
 * the scale exponent); isqrt64(integer) yields integer. */
static inline int64_t sm64_saturn_isqrt64(int64_t v)
{
    int64_t rem = v;
    int64_t root = 0;
    /* 62 = second-to-top bit of a 64-bit magnitude, mirroring the
     * classic 32-bit version's 1<<30 (largest power of 4 that fits). */
    int64_t bit = (int64_t) 1 << 62;

    if (v <= 0) {
        return 0;
    }
    while (bit > rem) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (rem >= root + bit) {
            rem -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* Exact float <-> Q16.16 conversion WITHOUT floating-point arithmetic OR
 * compiler conversion helpers. The IEEE-754 fields are decoded/packed with
 * integer operations, removing _fixsfsi/_floatsisf from graph transforms.
 *
 * float_to_q16: truncates toward zero like the source cast. Out-of-range
 * magnitudes (including infinities and NaNs) saturate to INT32_MAX/MIN
 * (positions beyond +-32767 world units are outside this port's
 * documented range assumption -- see saturn_matrix.h's decode note). */
static inline int32_t sm64_saturn_float_to_q16(float f)
{
    union { float f; uint32_t u; } bits;
    uint32_t exponent;
    uint32_t magnitude;

    bits.f = f;
    exponent = (bits.u >> 23) & 0xFFU;
    if (exponent == 0U) {
        return 0;
    }
    if (exponent >= 142U) {
        return (bits.u & 0x80000000u) ? INT32_MIN : INT32_MAX;
    }
    if (exponent <= 110U) {
        return 0;
    }
    magnitude = (bits.u & 0x007FFFFFU) | 0x00800000U;
    if (exponent < 134U) {
        magnitude >>= 134U - exponent;
    } else {
        magnitude <<= exponent - 134U;
    }
    return (bits.u & 0x80000000U) != 0U
        ? -(int32_t) magnitude : (int32_t) magnitude;
}

/* Q16.16 -> float with IEEE round-to-nearest-even, matching `(float)q`
 * followed by an exact power-of-two scale, but without _floatsisf. */
static inline float sm64_saturn_q16_to_float(int32_t q)
{
    union { float f; uint32_t u; } bits;
    const uint32_t sign = q < 0 ? 0x80000000U : 0U;
    uint32_t magnitude = q < 0 ? 0U - (uint32_t) q : (uint32_t) q;
    uint32_t scan;
    uint32_t significand;
    int highest = 0;
    int exponent;

    if (q == 0) {
        return 0.0f;
    }

    scan = magnitude;
    if (scan >= (UINT32_C(1) << 16)) { scan >>= 16; highest += 16; }
    if (scan >= (UINT32_C(1) << 8))  { scan >>= 8;  highest += 8; }
    if (scan >= (UINT32_C(1) << 4))  { scan >>= 4;  highest += 4; }
    if (scan >= (UINT32_C(1) << 2))  { scan >>= 2;  highest += 2; }
    if (scan >= (UINT32_C(1) << 1))  { highest += 1; }

    exponent = highest + 111; /* 127 bias minus the Q16 fractional bits. */
    if (highest <= 23) {
        significand = magnitude << (23 - highest);
    } else {
        const int shift = highest - 23;
        const uint32_t discarded_mask = (UINT32_C(1) << shift) - 1U;
        const uint32_t discarded = magnitude & discarded_mask;
        const uint32_t halfway = UINT32_C(1) << (shift - 1);
        significand = magnitude >> shift;
        if (discarded > halfway
            || (discarded == halfway && (significand & 1U) != 0U)) {
            significand++;
            if (significand == (UINT32_C(1) << 24)) {
                significand >>= 1;
                exponent++;
            }
        }
    }
    bits.u = sign | ((uint32_t) exponent << 23)
           | (significand & 0x007FFFFFU);
    return bits.f;
}

#endif
