/* Host oracle for the integer sqrtf that replaced the four-iteration float
 * Newton solver (W2, docs/saturn/evidence/reports/w2-softfloat-call-sites.md).
 *
 * WHAT THIS BOUNDS, AND WHAT IT REFUSES TO CLAIM
 *
 * It does NOT claim byte-identity with the implementation it replaces. The
 * replaced version was itself up to 1 ulp away from the correctly rounded
 * square root on 25% of normal inputs, so no exact-arithmetic implementation
 * can reproduce it. What this fixture pins instead:
 *
 *   1. TOLERANCE, stated as exact equality. The shipped sqrtf must equal the
 *      correctly rounded IEEE-754 single-precision square root on EVERY
 *      finite non-negative input -- worst case 0 ulp, not "small". The
 *      reference is double-precision sqrt narrowed to float, which is
 *      correctly rounded for float radicands because 2*24+2 <= 53.
 *   2. DIVERGENCE from the pre-W2 float body, which is pinned verbatim below
 *      and swept alongside, so the fixture reports the worst ulp and worst
 *      relative movement the change actually causes rather than asserting it.
 *   3. BLUNDERS, named individually so a failure says which one happened:
 *      exponent-halving sign and off-by-one, exponent parity fold, radicand
 *      scale off-by-one, trial-subtrahend form, lost rounding, significand
 *      renormalisation overflow, monotonicity, and every special value.
 *   4. The non-IEEE contract the callers depend on: a negative input returns
 *      +0.0f, not NaN.
 *
 * Stability across frames needs no measurement here and is asserted by
 * construction: sqrtf is a pure function of its argument with no state, no
 * accumulator and no feedback, so its error cannot drift between frames. The
 * risk that remains is a fixed per-call offset, which is what item 2 bounds.
 *
 * Sweep size: SQRTF_TEST_STRIDE (default 1) selects every 2^32 bit pattern.
 */
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The shipped translation unit itself, not a transcription of it. The rename
 * is not cosmetic: GCC treats `sqrtf` as a builtin and would otherwise fold
 * calls to the host FPU instruction and never execute the shipped body at
 * all -- which is exactly how a fixture ends up testing nothing. */
#define sqrtf sm64_saturn_sqrtf_under_test
#include "../../src/port/saturn/compat/sqrtf.c"
#undef sqrtf

#ifndef SQRTF_TEST_STRIDE
#define SQRTF_TEST_STRIDE 1u
#endif

/* Deliberate tolerance mutation: the stated bound is exact equality, so the
 * tightest mutation that can exist is one ulp. If a 1-ulp perturbation
 * survives, the bound is decorative. */
static float shipped(float value) {
    float result = sm64_saturn_sqrtf_under_test(value);
#if defined(SQRTF_TEST_MUTATE_TOLERANCE)
    union { float f; uint32_t u; } bits;
    bits.f = result;
    if ((bits.u & 0x7F800000u) != 0x7F800000u && bits.u != 0u) {
        bits.u += 1u;
        result = bits.f;
    }
#endif
    return result;
}

/* The pre-W2 body, pinned verbatim from sqrtf.c at 70703228 so the divergence
 * this change causes is measured against the implementation it replaced and
 * not against an idealisation of it. */
static float pre_w2_float_newton(float value) {
    if (value <= 0.0f) return 0.0f;
    union { float f; uint32_t u; } estimate = { .f = value };
    estimate.u = (estimate.u >> 1) + 0x1FC00000U;
    for (uint8_t iteration = 0; iteration < 4; iteration++)
        estimate.f = 0.5f * (estimate.f + value / estimate.f);
    return estimate.f;
}

static uint32_t bits_of(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static float from_bits(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

/* Correctly rounded reference. sqrt() in double is correctly rounded by IEEE
 * 754, and narrowing to float cannot double-round wrongly here because a
 * float radicand needs at most 2*24+2 = 50 significand bits to decide the
 * rounding and double carries 53. */
static float reference(float value) {
    return (float) sqrt((double) value);
}

static int failures = 0;

static void blunder(const char *name, uint32_t input, uint32_t got, uint32_t want) {
    if (failures >= 12) { failures++; return; }
    printf("BLUNDER %s: in=0x%08" PRIx32 " got=0x%08" PRIx32 " want=0x%08" PRIx32
           " (%.9g vs %.9g)\n",
           name, input, got, want, (double) from_bits(got), (double) from_bits(want));
    failures++;
}

static void expect_bits(const char *name, uint32_t input, uint32_t want) {
    const uint32_t got = bits_of(shipped(from_bits(input)));
    if (got != want) blunder(name, input, got, want);
}

static void expect_exact_value(const char *name, float input, float want) {
    const uint32_t got = bits_of(shipped(input));
    if (got != bits_of(want)) blunder(name, bits_of(input), got, bits_of(want));
}

int main(void) {
    printf("sqrtf oracle: shipped integer sqrtf vs correctly rounded IEEE-754,\n"
           "              and vs the pre-W2 four-iteration float Newton body\n");

    /* ---- named blunders, asserted exactly -------------------------------- */

    /* Perfect squares across the exponent range: a sign error or an
     * off-by-one in the exponent halving is individually visible here, and so
     * is a parity-fold error, because consecutive powers of two alternate
     * parity. */
    for (int power = -60; power <= 60; power += 2) {
        const double v = ldexp(1.0, power);
        expect_exact_value("power of four", (float) v, (float) ldexp(1.0, power / 2));
    }
    expect_exact_value("sqrt(1)", 1.0f, 1.0f);
    expect_exact_value("sqrt(4)", 4.0f, 2.0f);
    expect_exact_value("sqrt(9)", 9.0f, 3.0f);
    expect_exact_value("sqrt(2.25)", 2.25f, 1.5f);
    expect_exact_value("sqrt(0.25)", 0.25f, 0.5f);
    expect_exact_value("sqrt(1e12)", 1.0e12f, 1.0e6f);
    /* An odd-exponent input: 2.0 folds, 4.0 does not. Both must be right. */
    expect_exact_value("sqrt(2) parity", 2.0f, reference(2.0f));
    expect_exact_value("sqrt(8) parity", 8.0f, reference(8.0f));

    /* The non-IEEE contract the callers rely on. */
    expect_bits("negative -> +0", 0xBF800000u, 0x00000000u);
    expect_bits("-0 -> +0", 0x80000000u, 0x00000000u);
    expect_bits("-inf -> +0", 0xFF800000u, 0x00000000u);
    expect_bits("+0 -> +0", 0x00000000u, 0x00000000u);

    /* Range boundaries: wraparound at the ends of the exponent field, and the
     * subnormal/normal seam in both directions. */
    expect_bits("min subnormal", 0x00000001u, bits_of(reference(from_bits(0x00000001u))));
    expect_bits("max subnormal", 0x007FFFFFu, bits_of(reference(from_bits(0x007FFFFFu))));
    expect_bits("min normal", 0x00800000u, bits_of(reference(from_bits(0x00800000u))));
    expect_bits("max normal", 0x7F7FFFFFu, bits_of(reference(from_bits(0x7F7FFFFFu))));
    expect_bits("+inf", 0x7F800000u, 0x7F800000u);
    {   /* NaN must stay a quiet NaN. */
        const uint32_t got = bits_of(shipped(from_bits(0x7F800001u)));
        if ((got & 0x7F800000u) != 0x7F800000u || (got & 0x007FFFFFu) == 0u ||
            (got & 0x00400000u) == 0u) {
            blunder("sNaN -> quiet NaN", 0x7F800001u, got, 0x7FC00001u);
        }
    }

    /* Significand renormalisation: rounding the last input of a binade up must
     * carry into the exponent rather than wrap the significand to zero. */
    expect_bits("renormalise carry", 0x407FFFFFu, bits_of(reference(from_bits(0x407FFFFFu))));

    /* ---- exhaustive sweep ------------------------------------------------ */

    uint64_t checked = 0, exact_hits = 0, moved = 0, moved_normal = 0;
    uint32_t worst_ulp = 0, worst_ulp_input = 0;
    uint32_t worst_move_ulp = 0, worst_move_input = 0;
    double worst_rel = 0.0, worst_rel_normal = 0.0;
    uint32_t worst_rel_input = 0, worst_rel_normal_input = 0;
    uint32_t previous_result = 0;
    int monotonic_break = 0;

    for (uint64_t raw = 0; raw <= 0xFFFFFFFFull; raw += SQRTF_TEST_STRIDE) {
        const uint32_t u = (uint32_t) raw;
        const float in = from_bits(u);
        const float got = shipped(in);
        const uint32_t got_bits = bits_of(got);
        checked++;

        if ((u & 0x7F800000u) == 0x7F800000u && (u & 0x007FFFFFu) != 0u) {
            continue; /* NaN: covered by the named case above */
        }
        if ((u & 0x80000000u) != 0u) {
            if (got_bits != 0u) blunder("negative domain", u, got_bits, 0u);
            continue;
        }
        if (u == 0x7F800000u) continue; /* +inf: named case above */

        const uint32_t want_bits = bits_of(reference(in));
        if (got_bits == want_bits) {
            exact_hits++;
        } else {
            const uint32_t ulp = got_bits > want_bits ? got_bits - want_bits
                                                      : want_bits - got_bits;
            if (ulp > worst_ulp) { worst_ulp = ulp; worst_ulp_input = u; }
            blunder("not correctly rounded", u, got_bits, want_bits);
        }

        /* Monotonicity: float bit patterns of non-negative values are ordered,
         * and so must their square roots be. This catches a scale or index
         * blunder that a single-point check could step over. */
        if (SQRTF_TEST_STRIDE == 1u && u != 0u && got_bits < previous_result) {
            if (!monotonic_break) {
                blunder("monotonicity", u, got_bits, previous_result);
                monotonic_break = 1;
            }
        }
        previous_result = got_bits;

        /* Divergence from the implementation this replaced -- reported, not
         * asserted. */
        const float was = pre_w2_float_newton(in);
        const uint32_t was_bits = bits_of(was);
        if (was_bits != got_bits) {
            const int normal_input = (u & 0x7F800000u) != 0u;
            moved++;
            if (normal_input) {
                const uint32_t ulp = got_bits > was_bits ? got_bits - was_bits
                                                        : was_bits - got_bits;
                moved_normal++;
                if (ulp > worst_move_ulp) { worst_move_ulp = ulp; worst_move_input = u; }
            }
            if (isfinite((double) was) && was != 0.0f) {
                const double rel = fabs(((double) got - (double) was) / (double) was);
                if (rel > worst_rel) { worst_rel = rel; worst_rel_input = u; }
                if (normal_input && rel > worst_rel_normal) {
                    worst_rel_normal = rel;
                    worst_rel_normal_input = u;
                }
            }
        }
        if (raw + SQRTF_TEST_STRIDE > 0xFFFFFFFFull) break;
    }

    printf("swept %" PRIu64 " bit patterns at stride %u\n", checked,
           (unsigned) SQRTF_TEST_STRIDE);
    printf("correctly rounded: %" PRIu64 " exact, worst deviation %" PRIu32
           " ulp (input 0x%08" PRIx32 ")\n", exact_hits, worst_ulp, worst_ulp_input);
    printf("divergence from the pre-W2 float body: %" PRIu64 " results moved\n", moved);
    printf("  normal inputs:    %" PRIu64 " moved, worst %" PRIu32 " ulp "
           "(input 0x%08" PRIx32 "), worst relative %.6e (input 0x%08" PRIx32 ")\n",
           moved_normal, worst_move_ulp, worst_move_input,
           worst_rel_normal, worst_rel_normal_input);
    printf("  including subnormals: worst relative %.6e (input 0x%08" PRIx32 ") "
           "-- the pre-W2 seed did not converge below the normal range\n",
           worst_rel, worst_rel_input);

    if (failures != 0) {
        printf("RESULT: FAIL (%d)\n", failures);
        return 1;
    }
    printf("RESULT: shipped sqrtf is the correctly rounded IEEE-754 square root "
           "on every input, and every named blunder case holds.\n");
    return 0;
}
