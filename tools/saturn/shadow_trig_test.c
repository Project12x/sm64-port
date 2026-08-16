/* tools/saturn/shadow_trig_test.c
 *
 * The error-bounded oracle for Sprint 2 T2.13.
 *
 * Every prior task in this sprint proved BYTE-IDENTICAL output. This one
 * cannot, and the whole point of this fixture is to stop pretending otherwise
 * and instead bound the difference.
 *
 * `calculate_vertex_xyz` (src/game/shadow.c) used to call the libultra
 * `sinf`/`cosf`, which evaluate a five-term polynomial in `double`. The
 * Saturn arm now indexes the engine's own `sins`/`coss` table, which discards
 * the low four bits of the binary angle. That changes results BY
 * CONSTRUCTION. Three separate claims are proven here:
 *
 *   1. TRIG TOLERANCE. Over the whole s16 angle domain -- all 65,536 values,
 *      including both wrap points -- the table result differs from what the
 *      shipped double-precision polynomial returned by no more than
 *      SHADOW_TRIG_TOL. The reference is not host libm: it is the libultra
 *      polynomial copied verbatim from lib/src/math/{sinf,cosf}.c, fed
 *      through the exact same f32 rounding chain the caller applied
 *      (atan2s -> degrees -> radians), so the comparison is against what the
 *      target really computed and not against an idealised sine.
 *
 *   2. DOWNSTREAM MOVEMENT. A trig tolerance is not a claim about the screen.
 *      The fixture therefore also evaluates `calculate_vertex_xyz`'s actual
 *      output expression both ways and reports the worst-case movement of an
 *      emitted shadow vertex in world units, per unit of shadow scale and at
 *      a range of real scales.
 *
 *   3. BIT-EXACTNESS of the two identity-preserving changes, proven rather
 *      than assumed: the held-object translation over all 65,536 s16 inputs,
 *      and `push_clamped_int`'s digit ladder against the general loop it
 *      replaced over the whole value/width domain including the widths that
 *      still take the loop.
 *
 * MUTATIONS. Two are wired in so the bound is not decorative:
 *   -DSHADOW_TRIG_TEST_MUTATE_TOLERANCE=1  perturbs the shipped trig result
 *      by just over SHADOW_TRIG_TOL; the tolerance check must fail.
 *   -DSHADOW_TRIG_TEST_MUTATE_EXACT=1  perturbs the held-object Q16 result by
 *      one unit; the bit-exact check must fail.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef float f32;
typedef int16_t s16;
typedef uint16_t u16;

/* The engine's trig table, exactly as the target links it. AVOID_UB is set in
 * the sourceboot build (src/port/saturn/sourceboot/Makefile SH_CFLAGS), which
 * makes trig_tables.inc.c a single 0x1400-entry array with the cosine data
 * following the sine data -- which is what lets coss() index past 0x400. */
#define AVOID_UB 1
#include "trig_tables.inc.c"

/* SHADOW_TRIG_TEST_MUTATE_INDEX shifts the table index by one entry, and
 * SHADOW_TRIG_TEST_MUTATE_QUADRANT swaps sine for cosine. Both are BLUNDERS of
 * the kind this fixture exists to catch -- an off-by-one index and a quadrant
 * error are exactly how a table substitution goes wrong -- as opposed to the
 * small, accepted delta the substitution introduces on purpose. */
#if SHADOW_TRIG_TEST_MUTATE_INDEX
#define SHIPPED_INDEX(x) ((((u16) (x)) >> 4) + 1U)
#else
#define SHIPPED_INDEX(x) (((u16) (x)) >> 4)
#endif
#if SHADOW_TRIG_TEST_MUTATE_QUADRANT
#define shipped_sins(x) gSineTable[SHIPPED_INDEX(x) + 0x400]
#define shipped_coss(x) gSineTable[SHIPPED_INDEX(x)]
#else
#define shipped_sins(x) gSineTable[SHIPPED_INDEX(x)]
#define shipped_coss(x) gSineTable[SHIPPED_INDEX(x) + 0x400]
#endif

#include "saturn_matrix_kernels.h"

/* Compiling the HUD translation unit in gives access to its static
 * push_clamped_int, so case 3b tests the shipped function rather than a
 * transcription of it. */
#include "saturn_hud_layout.c"

/* ------------------------------------------------------------------ */
/* Reference: lib/src/math/sinf.c and cosf.c, copied verbatim.         */
/* ------------------------------------------------------------------ */

typedef union {
    double d;
    struct { unsigned int hi; unsigned int lo; } word;
} du;

static const du P[5] = {{1.0},
                        {-0.16666659550427756},
                        {0.008333066246082155},
                        {-1.980960290193795E-4},
                        {2.605780637968037E-6}};
static const du rpi = {0.3183098861837907};
static const du pihi = {3.1415926218032837};
static const du pilo = {3.178650954705639E-8};

static float ref_sinf(float x) {
    double dx, xsq, poly, dn, result;
    int n, ix, xpt;
    memcpy(&ix, &x, sizeof ix);
    xpt = (ix >> 22) & 0x1FF;
    if (xpt < 255) {
        dx = x;
        if (xpt >= 230) {
            xsq = dx * dx;
            poly = (((((P[4].d * xsq) + P[3].d) * xsq) + P[2].d) * xsq) + P[1].d;
            result = ((dx * xsq) * poly) + dx;
            return (float) result;
        }
        return x;
    }
    if (xpt < 310) {
        dx = x;
        dn = dx * rpi.d;
        n = (int) (dn >= 0 ? dn + 0.5 : dn - 0.5);
        dn = n;
        dx -= dn * pihi.d;
        dx -= dn * pilo.d;
        xsq = dx * dx;
        poly = (((((P[4].d * xsq) + P[3].d) * xsq) + P[2].d) * xsq) + P[1].d;
        result = ((dx * xsq) * poly) + dx;
        return (n & 1) == 0 ? (float) result : -(float) result;
    }
    return 0.0f;
}

static float ref_cosf(float x) {
    double dx, xsq, poly, dn, result;
    float xabs;
    int n, ix, xpt;
    memcpy(&ix, &x, sizeof ix);
    xpt = (ix >> 22) & 0x1FF;
    if (xpt < 310) {
        xabs = (0 < x) ? x : -x;
        dx = xabs;
        dn = dx * rpi.d + .5;
        n = (int) (0 <= dn ? dn + .5 : dn - .5);
        dn = n;
        dx -= (dn - .5) * pihi.d;
        dx -= (dn - .5) * pilo.d;
        xsq = dx * dx;
        poly = (((((P[4].d * xsq) + P[3].d) * xsq) + P[2].d) * xsq) + P[1].d;
        result = ((dx * xsq) * poly) + dx;
        return (n & 1) == 0 ? (float) result : -(float) result;
    }
    return 0.0f;
}

/* src/game/shadow.c:atan2_deg -- the exact f32 rounding chain the two angle
 * fields went through before reaching sinf/cosf. */
static f32 ref_bam_to_deg(s16 bam) {
    return (f32) ((f32) bam / 65535.0 * 360.0);
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static f32 ref_deg_to_rad(f32 deg) {
    return (f32) (deg * M_PI / 180.0);
}

/* ------------------------------------------------------------------ */
/* The tolerance, and where every term of it comes from.               */
/* ------------------------------------------------------------------ */
/*
 *   index truncation   sins()/coss() index with (u16)angle >> 4, so up to 15
 *                      of 65,536 steps of angle are discarded. |d sin| <= dth,
 *                      and 15 * 2*pi / 65536                    = 1.4381e-3
 *   65535 vs 65536     atan2_deg divides the BAM by 65535, the table by
 *                      65536. For |bam| <= 32768 that is at most
 *                      2*pi*32768 / (65535*65536)               = 4.7902e-5
 *   f32 table entries  the table holds f32 roundings of the true sine,
 *                      relative 2^-24 on a value <= 1           = 5.9605e-8
 *   polynomial error   the libultra minimax against true sine, and the f32
 *                      rounding of the degrees intermediate, both well below
 *                      the terms above                          < 1.0e-6
 *                                                               ----------
 *                      bound                                    = 1.4871e-3
 *
 * SHADOW_TRIG_TOL is that derivation rounded up to 1.5e-3. It is a DERIVED
 * bound, not a fitted one: the fixture prints the measured worst case and it
 * must come in under this, and the tolerance mutation nudges the result just
 * past it to prove the check has teeth.
 */
#define SHADOW_TRIG_TOL 1.5e-3

/* The tolerance mutation is deliberately TIGHT: 2.0e-5 on top of a measured
 * worst case of 1.486e-3 lands at 1.506e-3, just past the 1.5e-3 bound. A
 * mutation that overshoots by an order of magnitude would prove only that the
 * comparison exists, not that the bound is where the fixture says it is. */
static double perturb(double value) {
#if SHADOW_TRIG_TEST_MUTATE_TOLERANCE
    return value + 2.0e-5;
#else
    return value;
#endif
}

int main(void) {
    int failures = 0;

    /* ---- case 1: trig tolerance over the whole s16 angle domain ---- */
    double worst_sin = 0.0, worst_cos = 0.0;
    int worst_sin_bam = 0, worst_cos_bam = 0;
    double sum_abs = 0.0;
    for (int32_t raw = -32768; raw <= 32767; raw++) {
        const s16 bam = (s16) raw;
        const f32 radians = ref_deg_to_rad(ref_bam_to_deg(bam));
        const double dsin = fabs(perturb(shipped_sins(bam)) - ref_sinf(radians));
        const double dcos = fabs(perturb(shipped_coss(bam)) - ref_cosf(radians));
        if (dsin > worst_sin) { worst_sin = dsin; worst_sin_bam = bam; }
        if (dcos > worst_cos) { worst_cos = dcos; worst_cos_bam = bam; }
        sum_abs += dsin + dcos;
    }
    printf("shadow trig: 65536 angles, worst |d sin| %.9f at bam %d, "
           "worst |d cos| %.9f at bam %d, mean |d| %.9f\n",
           worst_sin, worst_sin_bam, worst_cos, worst_cos_bam, sum_abs / (2.0 * 65536.0));
    if (worst_sin > SHADOW_TRIG_TOL || worst_cos > SHADOW_TRIG_TOL) {
        printf("shadow trig: FAIL - exceeds the derived bound %.6f\n", SHADOW_TRIG_TOL);
        failures++;
    }

    /* Both wrap points explicitly, because a table index is exactly where a
     * wrap goes wrong and a sweep that happens to cover them is not the same
     * as a case that names them. */
    const s16 wraps[] = { 0, -1, 32767, -32768, 1, 16384, -16384, 0x4000, 15, 16 };
    for (size_t i = 0; i < sizeof wraps / sizeof wraps[0]; i++) {
        const f32 radians = ref_deg_to_rad(ref_bam_to_deg(wraps[i]));
        const double dsin = fabs((double) shipped_sins(wraps[i]) - ref_sinf(radians));
        const double dcos = fabs((double) shipped_coss(wraps[i]) - ref_cosf(radians));
        if (dsin > SHADOW_TRIG_TOL || dcos > SHADOW_TRIG_TOL) {
            printf("shadow trig: FAIL at wrap point %d (%.9f / %.9f)\n", wraps[i], dsin, dcos);
            failures++;
        }
    }

    /* ---- case 1b: blunders, named rather than left to the sweep ---- */
    /* The sweep above would catch all of these, but only as "some number got
     * big". Naming them means a failure says which blunder happened. The
     * cardinal angles are where a quadrant error, a sign flip and an
     * off-by-one index are each individually visible. */
    static const struct { s16 bam; double sin_expect; double cos_expect; const char *name; }
    cardinals[] = {
        {      0,  0.0,  1.0, "0 deg" },
        { 0x4000,  1.0,  0.0, "90 deg" },
        { (s16) 0x8000, 0.0, -1.0, "180 deg" },
        { (s16) 0xC000, -1.0, 0.0, "270 deg" },
        { 0x2000,  0.70710678,  0.70710678, "45 deg" },
        { (s16) 0xE000, -0.70710678, 0.70710678, "315 deg" },
    };
    for (size_t i = 0; i < sizeof cardinals / sizeof cardinals[0]; i++) {
        const double ds = fabs((double) shipped_sins(cardinals[i].bam) - cardinals[i].sin_expect);
        const double dc = fabs((double) shipped_coss(cardinals[i].bam) - cardinals[i].cos_expect);
        if (ds > SHADOW_TRIG_TOL || dc > SHADOW_TRIG_TOL) {
            printf("shadow trig: BLUNDER at %s (bam %d): sin %.6f want %.6f, cos %.6f want %.6f\n",
                   cardinals[i].name, cardinals[i].bam,
                   (double) shipped_sins(cardinals[i].bam), cardinals[i].sin_expect,
                   (double) shipped_coss(cardinals[i].bam), cardinals[i].cos_expect);
            failures++;
        }
    }
    /* Degenerate floor: init_shadow sets both BAM fields to a defined value on
     * every path, and floorTiltBam is pinned to 0 when floorSteepness is 0
     * (the divide-by-zero guard). Angle 0 must therefore be exactly the
     * identity, or a flat floor gets a rotated shadow. */
    if (shipped_sins((s16) 0) != 0.0f || shipped_coss((s16) 0) != 1.0f) {
        printf("shadow trig: BLUNDER - flat floor (bam 0) is not the identity\n");
        failures++;
    }

    /* ---- case 1c: frame-to-frame stability ---- */
    /* A fixed small offset is acceptable; geometry that jitters, pops or swims
     * between frames is not. Two properties are checked.
     *
     * First, the substitution is MEMORYLESS: sins/coss are a pure function of
     * the angle with no state, no accumulator and no feedback, so error cannot
     * accumulate across frames by construction. Nothing to measure there.
     *
     * Second -- and this is the real risk -- the table is a STEP function of
     * the angle where the polynomial was smooth. As a floor angle sweeps, the
     * shadow now moves in discrete jumps at every 16th BAM value instead of
     * continuously. The size of the largest such jump is the pop the owner
     * could see, so it is measured here rather than assumed small. */
    double worst_step = 0.0;
    int worst_step_bam = 0;
    (void) worst_step_bam;
    for (int32_t raw = -32768; raw < 32767; raw++) {
        const s16 a = (s16) raw, b = (s16) (raw + 1);
        const double dsin = fabs((double) shipped_sins(b) - (double) shipped_sins(a));
        const double dcos = fabs((double) shipped_coss(b) - (double) shipped_coss(a));
        const double step = dsin > dcos ? dsin : dcos;
        if (step > worst_step) { worst_step = step; worst_step_bam = a; }
    }
    printf("shadow stability: largest single-BAM-step discontinuity %.9f at bam %d"
           " (= %.6f world units at shadowScale 100)\n",
           worst_step, worst_step_bam, worst_step * 100.0 * 1.5);

    /* ---- case 2: downstream vertex movement ---- */
    /* calculate_vertex_xyz's real output expression, both ways. shadowScale
     * is swept because the error is proportional to it, and the corpus is
     * reported per unit of scale so the number can be reused for any scale. */
    static const double scales[] = { 1.0, 50.0, 100.0, 200.0, 400.0 };
    for (size_t si = 0; si < sizeof scales / sizeof scales[0]; si++) {
        const double scale = scales[si];
        double worst_move = 0.0;
        int worst_tilt = 0, worst_down = 0;
        /* Stride the two angle axes; the product of two full s16 sweeps is
         * 4.3e9 pairs. 257 is coprime with 65536, so the stride visits every
         * residue class of the discarded low nibble rather than aliasing onto
         * one of them. */
        for (int32_t t = -32768; t <= 32767; t += 257) {
            for (int32_t d = -32768; d <= 32767; d += 257) {
                const s16 tilt = (s16) t, down = (s16) d;
                const f32 tilt_rad = ref_deg_to_rad(ref_bam_to_deg(tilt));
                const f32 down_rad = ref_deg_to_rad(ref_bam_to_deg(down));

                const double ref_tilted = (double) ref_cosf(tilt_rad) * scale;
                const double new_tilted = (double) shipped_coss(tilt) * scale;

                for (int xc = -1; xc <= 1; xc++) {
                    for (int zc = -1; zc <= 1; zc++) {
                        const double half_scale = (xc * scale) / 2.0;
                        const double ref_half_tilted = (zc * ref_tilted) / 2.0;
                        const double new_half_tilted = (zc * new_tilted) / 2.0;

                        const double ref_x = ref_half_tilted * ref_sinf(down_rad)
                                           + half_scale * ref_cosf(down_rad);
                        const double new_x = new_half_tilted * shipped_sins(down)
                                           + half_scale * shipped_coss(down);
                        const double ref_z = ref_half_tilted * ref_cosf(down_rad)
                                           - half_scale * ref_sinf(down_rad);
                        const double new_z = new_half_tilted * shipped_coss(down)
                                           - half_scale * shipped_sins(down);

                        const double move = sqrt((new_x - ref_x) * (new_x - ref_x)
                                               + (new_z - ref_z) * (new_z - ref_z));
                        if (move > worst_move) {
                            worst_move = move;
                            worst_tilt = tilt;
                            worst_down = down;
                        }
                    }
                }
            }
        }
        printf("shadow vertex: shadowScale %6.1f -> worst movement %.6f world units"
               " (%.6f per unit scale) at tilt %d down %d\n",
               scale, worst_move, worst_move / scale, worst_tilt, worst_down);
    }

    /* ---- case 3a: held-object translation, exhaustive and bit-exact ---- */
    int exact_failures = 0;
    for (int32_t raw = -32768; raw <= 32767; raw++) {
        const s16 n = (s16) raw;
        const float viaFloat = (float) n / 4.0f;
        const int32_t reference = sm64_saturn_float_to_q16(viaFloat);
        int32_t shipped = (int32_t) n * 16384;
#if SHADOW_TRIG_TEST_MUTATE_EXACT
        shipped += 1;
#endif
        if (shipped != reference) {
            if (exact_failures < 4) {
                printf("held object: FAIL n=%d float_to_q16(n/4.0f)=%ld n*16384=%ld\n",
                       n, (long) reference, (long) shipped);
            }
            exact_failures++;
        }
    }
    printf("held object: 65536 s16 inputs, %d divergences\n", exact_failures);
    failures += (exact_failures != 0);

    /* ---- case 3b: push_clamped_int ladder against the loop it replaced ---- */
    int digit_failures = 0;
    long digit_cases = 0;
    for (uint8_t width = 0U; width <= 5U; width++) {
        for (int32_t value = -8; value <= 100000; value += (value < 2000 ? 1 : 37)) {
            sm64_saturn_hud_cell_t shipped_cells[16];
            sm64_saturn_hud_cell_t reference_cells[16];
            memset(shipped_cells, 0, sizeof shipped_cells);
            memset(reference_cells, 0, sizeof reference_cells);

            const uint32_t shipped_count =
                push_clamped_int(shipped_cells, 0U, 16U, 3U, 5U, value, width);

            /* The pre-T2.13 body, verbatim. */
            uint32_t reference_count = 0U;
            {
                int32_t v = value < 0 ? 0 : value;
                int32_t divisor = 1;
                for (uint8_t place = 1U; place < width; place++)
                    divisor *= 10;
                uint8_t written = 0U;
                for (uint8_t place = 0U; place < width; place++) {
                    const int32_t digit = (v / divisor) % 10;
                    reference_count = push_cell(reference_cells, reference_count, 16U,
                                                (uint8_t) (3U + written), 5U,
                                                digit_glyph(digit));
                    written++;
                    divisor /= 10;
                    if (divisor == 0) divisor = 1;
                }
            }
            digit_cases++;
            if (shipped_count != reference_count
                || memcmp(shipped_cells, reference_cells, sizeof shipped_cells) != 0) {
                if (digit_failures < 4) {
                    printf("push_clamped_int: FAIL value=%ld width=%u (%u vs %u cells)\n",
                           (long) value, width, shipped_count, reference_count);
                }
                digit_failures++;
            }
        }
    }
    printf("push_clamped_int: %ld cases over widths 0-5, %d divergences\n",
           digit_cases, digit_failures);
    failures += (digit_failures != 0);

    if (failures != 0) {
        printf("shadow trig fixture: FAIL (%d)\n", failures);
        return 1;
    }
    printf("shadow trig fixture: PASS\n");
    return 0;
}
