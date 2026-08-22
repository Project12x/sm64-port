/* Minimal positive-domain sqrtf service for the freestanding SH target.
 *
 * SM64 calls sqrtf with sums of squares and squared distances. Yaul's SH
 * toolchain does not ship libm. musl sqrtf.c (blob 740d81c, MIT-compatible)
 * was inspected first, but its reciprocal-square-root table, fenv helpers,
 * and internal libm ABI are a poor fit for this target.
 *
 * W2 (2026-08-22): the implementation is now integer-only. It previously ran
 * four Newton iterations in float --
 *
 *     estimate.u = (estimate.u >> 1) + 0x1FC00000U;
 *     for (uint8_t i = 0; i < 4; i++)
 *         estimate.f = 0.5f * (estimate.f + value / estimate.f);
 *
 * -- which is four __divsf3, four __addsf3 and four __mulsf3 per call on a
 * CPU with no FPU. The exponent is halved by arithmetic on the exponent
 * field and the significand comes from a 24-step digit-by-digit integer
 * square root, so no soft-float routine is reached at all.
 *
 * Precision: the result is the correctly rounded (round-to-nearest-even)
 * IEEE-754 single-precision square root on every finite non-negative input,
 * verified exhaustively over all 2^32 bit patterns by verify-sqrtf-fixed.
 * The four-iteration float version was within 1 ulp of that but not equal to
 * it -- it differed on 25.0% of normal inputs -- so this is a bounded change
 * to what the game computes, not a bit-exact substitution. See
 * docs/saturn/evidence/reports/w2-softfloat-call-sites.md.
 *
 * Behaviour deliberately preserved from the float version, because callers
 * depend on it and it is not IEEE sqrt: a negative input (including -0.0f)
 * returns +0.0f rather than NaN.
 *
 * Behaviour deliberately changed, both outside the reachable domain of
 * "sums of squares of world coordinates" and both strictly more correct:
 *   - +INFINITY now returns +INFINITY. The float version's second Newton
 *     iteration evaluated inf/inf and returned NaN.
 *   - subnormal inputs are now correctly rounded. The seed the float version
 *     used does not converge in four iterations down there: it returned
 *     2.03e-21 for the smallest subnormal against a true 1.05e-22.
 * NaN still returns a quiet NaN, as before.
 */
#include <stdint.h>

/* Deliberate host-fixture mutations, on the precedent of
 * SM64_SATURN_TEST_MUTATE_Q16_DIV in saturn_render_native_math.h:58. No
 * product build defines any of these, so none reaches the image;
 * verify-sqrtf-fixed-mutation requires each one to make the oracle FAIL. */
#if defined(SM64_SATURN_TEST_MUTATE_SQRTF_TRIAL)
#define SM64_SATURN_SQRTF_TRIAL_SHIFT 1 /* blunder: 2R+1 instead of 4R+1 */
#else
#define SM64_SATURN_SQRTF_TRIAL_SHIFT 2
#endif
#if defined(SM64_SATURN_TEST_MUTATE_SQRTF_RADICAND)
#define SM64_SATURN_SQRTF_RADICAND_SHIFT 22 /* blunder: off-by-one radicand scale */
#else
#define SM64_SATURN_SQRTF_RADICAND_SHIFT 23
#endif

float sqrtf(float value) {
    union { float f; uint32_t u; } bits;
    uint32_t significand;
    uint32_t radicand_high;
    uint32_t radicand_low;
    uint32_t root;
    uint32_t remainder;
    uint32_t mantissa;
    uint32_t biased;
    int32_t exponent;
    int32_t shift;

    bits.f = value;
    if ((bits.u & 0x80000000U) != 0U) {
        /* Negative and -0.0f both returned +0.0f before this change. */
        return 0.0f;
    }
    biased = (bits.u >> 23) & 0xFFU;
    mantissa = bits.u & 0x007FFFFFU;
    if (biased == 0xFFU) {
        /* +inf passes through; a NaN is quieted and propagated. */
        if (mantissa != 0U) {
            bits.u |= 0x00400000U;
        }
        return bits.f;
    }
    if (biased == 0U) {
        if (mantissa == 0U) {
            return 0.0f;
        }
        /* Normalise the subnormal: value == 1.mantissa * 2^exponent. */
        exponent = -126;
        while ((mantissa & 0x00800000U) == 0U) {
            mantissa <<= 1;
            exponent--;
        }
        mantissa &= 0x007FFFFFU;
    } else {
        exponent = (int32_t) biased - 127;
    }

    /* value == significand * 2^(exponent-23) with significand in [2^23,2^24).
     * Force an even exponent by folding one power of two into the
     * significand, so the square root's exponent is exactly exponent/2 and
     * its significand is sqrt of a value in [1,4) -- i.e. in [1,2), already
     * normalised. */
    significand = mantissa | 0x00800000U;
#if defined(SM64_SATURN_TEST_MUTATE_SQRTF_PARITY)
    if ((exponent & 1) == 0) { /* blunder: fold on the wrong parity */
#else
    if ((exponent & 1) != 0) {
#endif
        significand <<= 1;
        exponent--;
    }

    /* radicand = significand << 23, a 48-bit value: taking its integer square
     * root yields exactly the 24-bit result significand, truncated.
     * significand < 2^25, so the top half fits in 16 bits. */
    radicand_high = significand >> (32 - SM64_SATURN_SQRTF_RADICAND_SHIFT);
    radicand_low = significand << SM64_SATURN_SQRTF_RADICAND_SHIFT;

    root = 0U;
    remainder = 0U;
    /* Digit-by-digit (restoring) square root, two radicand bits per step.
     * With R the root so far and rem = N - R*R, admitting bit b gives
     * (2R+b)^2 = 4R*R + b*(4R+1), so the trial subtrahend is 4R+1 and the
     * new remainder is 4*rem + pair - b*(4R+1). Both stay below 2^27, so
     * everything is 32-bit and no 64-bit helper is linked. The two loops
     * split the 48-bit radicand at its 32-bit boundary -- pairs are aligned
     * on even bit positions and 32 is even, so no pair straddles it -- and
     * every shift count is a compile-time constant, which matters on a CPU
     * whose variable shift is a libgcc call. */
    for (shift = 14; shift >= 0; shift -= 2) {
        const uint32_t pair = (radicand_high >> shift) & 3U;
        const uint32_t trial = (root << SM64_SATURN_SQRTF_TRIAL_SHIFT) | 1U;
        remainder = (remainder << 2) | pair;
        root <<= 1;
        if (remainder >= trial) {
            remainder -= trial;
            root |= 1U;
        }
    }
    for (shift = 30; shift >= 0; shift -= 2) {
        const uint32_t pair = (radicand_low >> shift) & 3U;
        const uint32_t trial = (root << SM64_SATURN_SQRTF_TRIAL_SHIFT) | 1U;
        remainder = (remainder << 2) | pair;
        root <<= 1;
        if (remainder >= trial) {
            remainder -= trial;
            root |= 1U;
        }
    }

    /* root == floor(sqrt(radicand)) in [2^23, 2^24) and remainder is the
     * residue. The exact root lies at root + remainder/(2*root+1), so it is
     * above the halfway point exactly when remainder > root. An exact tie
     * needs remainder == root + 1/4, which no integer attains, so
     * round-half-to-even never has to break one. */
#if defined(SM64_SATURN_TEST_MUTATE_SQRTF_ROUND)
    if (0) { /* blunder: truncate instead of rounding to nearest */
#else
    if (remainder > root) {
#endif
        root++;
        if (root == 0x01000000U) {
            root >>= 1;
            exponent += 2;
        }
    }
    bits.u = ((uint32_t) ((exponent >> 1) + 127) << 23) | (root & 0x007FFFFFU);
    return bits.f;
}
