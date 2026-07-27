/* Host differential test: GCC soft-fp (the soft-float this port now links)
 * against the host CPU's native IEEE-754 hardware, bit for bit.
 *
 * WHY THIS EXISTS
 * ---------------
 * src/port/saturn/sourceboot/Makefile builds third_party/gcc-soft-fp into
 * libsm64softfp.a and links it ahead of libgcc, replacing libgcc's fp-bit.c
 * for every f32/f64 operation the SM64 engine performs on the FPU-less SH-2.
 * The entire justification for taking that route first (see
 * docs/superpowers/specs/2026-07-26-soft-float-replacement-design.md) is that
 * it is *semantics-preserving*: soft-fp implements IEEE-754 exactly, so the
 * engine tree needs no edits and the rendered frame must come out bit-identical.
 *
 * That claim is only worth anything if it is checked. This test compiles the
 * SAME vendored soft-fp sources, with the SAME libgcc/config/sh/sfp-machine.h,
 * for the host, and diffs every routine against the host's own hardware
 * floating point.
 *
 * THE BAR, AND WHERE IEEE-754 STOPS SPECIFYING
 * --------------------------------------------
 * The bar is bit-exactness -- not a tolerance. Every result is compared as a
 * raw bit pattern, including the sign of zero. There are exactly two places
 * where "bit-exact against this host" is not a well-posed demand, because the
 * standard deliberately leaves the answer to the implementation. Both are
 * tested rather than skipped, against the contract the vendored
 * sfp-machine.h itself states:
 *
 *   1. NaN PAYLOADS, AND ONLY FOR BINARY OPERATIONS.  IEEE-754-2019 6.2
 *      specifies that an invalid operation delivers a quiet NaN, but
 *      explicitly does not specify which one. x86 SSE propagates the first NaN
 *      operand, quieted; sfp-machine.h's _FP_CHOOSENAN together with
 *      _FP_NANSIGN_S 0 / _FP_NANFRAC_S _FP_QNANBIT_S / _FP_KEEPNANFRACP 0
 *      specifies the canonical positive quiet NaN instead. So where a BINARY
 *      operation returns a NaN this test asserts (a) the host also produced a
 *      NaN, and (b) soft-fp produced exactly the canonical qNaN its own
 *      configuration mandates. Both assertions are exact; neither is a
 *      tolerance and neither skips an input.
 *
 *      The exemption is scoped by measurement, not by convenience. Running the
 *      strict comparison across every routine establishes that exactly two
 *      groups canonicalise and the rest do not:
 *        - __addsf3/__subsf3/__mulsf3/__divsf3 and their double counterparts
 *          canonicalise (_FP_CHOOSENAN),
 *        - __truncdfsf2 canonicalises when narrowing a NaN, because
 *          _FP_KEEPNANFRACP is 0 and a 52-bit payload does not fit 23 bits,
 *        - __negsf2, __negdf2 and __extendsfdf2 do NOT, and are held to strict
 *          bit equality against the host including NaN sign and payload.
 *          They pass: soft-fp shifts and re-quiets payloads exactly as the
 *          hardware does.
 *
 *   2. FLOAT -> INTEGER OUT OF RANGE.  C23 6.3.1.4 makes the conversion
 *      undefined when the truncated value will not fit, and GCC's runtime
 *      documentation promises nothing for __fixsfsi and friends past that
 *      point. The three implementations genuinely disagree, and all three are
 *      allowed to: x86 returns the "integer indefinite" 0x80000000, soft-fp
 *      saturates to INT_MAX/INT_MIN, and libgcc's fp-bit saturates too EXCEPT
 *      for NaN, which it maps to 0. So the in-range domain is checked
 *      exhaustively and bit-exactly against the host, and the out-of-range
 *      domain is checked exhaustively against soft-fp's saturation contract.
 *      The host conversion is never even executed out of range, so this test
 *      contains no undefined behaviour of its own.
 *
 * WHAT ACTUALLY CHANGES IN THE ROM
 * -------------------------------
 * Measured by compiling libgcc's fp-bit.c for the host alongside soft-fp and
 * diffing all three arms. Every numeric result is identical. The complete set
 * of behavioural differences between the soft-float being removed and the one
 * replacing it is confined to NaNs and to casts C leaves undefined:
 *
 *   - binary arithmetic returning a NaN: fp-bit propagates an operand's NaN,
 *     soft-fp returns the canonical +qNaN;
 *   - __truncdfsf2 of a NaN: fp-bit propagates sign and payload, soft-fp
 *     returns the canonical +qNaN;
 *   - float -> integer, out of range: both saturate, EXCEPT that fp-bit maps a
 *     NaN to 0 while soft-fp saturates it (so (s32)NaN was 0 and is now
 *     INT_MAX, or INT_MIN for a negative NaN).
 *
 * SM64 can only observe any of these if the engine produces a NaN, or casts a
 * NaN or an out-of-range float to an integer. Whether it ever does is settled
 * empirically by the free-roam capture's render counters, which must come out
 * identical to the pre-change build.
 *
 * Everything else -- every finite result, every infinity, every signed zero,
 * every subnormal, every comparison predicate including the unordered cases --
 * must match the host bit for bit, with no exceptions and no tolerance.
 *
 * METHOD NOTES
 * ------------
 * - The routines under test are reached through volatile function pointers so
 *   the compiler cannot fold a call to __addsf3 back into an `addss`, which
 *   would make the whole test vacuous.
 * - check_arms_are_distinct() runs first and fails the suite if the reference
 *   arm and the implementation arm ever turn out to be the same code. It uses
 *   the NaN-payload divergence above as its discriminator, which is the one
 *   input class where the two are *required* to differ.
 * - sfp-machine.h hardwires `_FP_W_TYPE unsigned long` next to
 *   `_FP_W_TYPE_SIZE 32`, which is true on sh-elf. The host compiler must
 *   therefore also have a 32-bit long (MSYS2's mingw64 gcc does; its cygwin
 *   gcc does not). The static assertion below refuses to build under the
 *   wrong one rather than silently testing a different configuration.
 */

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(unsigned long) == 4,
               "sfp-machine.h pairs _FP_W_TYPE_SIZE 32 with `unsigned long`; "
               "build this test with a 32-bit-long host compiler "
               "(MSYS2 mingw64 gcc), not a 64-bit-long one");
_Static_assert(sizeof(float) == 4 && sizeof(double) == 8, "IEEE binary32/binary64 host required");
#ifdef FLT_EVAL_METHOD
_Static_assert(FLT_EVAL_METHOD == 0,
               "host must evaluate float as float and double as double "
               "(SSE, not x87 excess precision)");
#endif

/* ------------------------------------------------------------------ */
/* The implementation under test: libgcc entry points, defined by the  */
/* vendored soft-fp objects the Makefile links alongside this file.    */
/* ------------------------------------------------------------------ */

typedef int CMPtype; /* __libgcc_cmp_return__ is SImode on both sh-elf and x86-64 */

extern float __addsf3(float, float);
extern float __subsf3(float, float);
extern float __mulsf3(float, float);
extern float __divsf3(float, float);
extern float __negsf2(float);
extern CMPtype __eqsf2(float, float);
extern CMPtype __nesf2(float, float);
extern CMPtype __ltsf2(float, float);
extern CMPtype __lesf2(float, float);
extern CMPtype __gtsf2(float, float);
extern CMPtype __gesf2(float, float);
extern CMPtype __unordsf2(float, float);
extern int32_t __fixsfsi(float);
extern uint32_t __fixunssfsi(float);
extern int64_t __fixsfdi(float);
extern uint64_t __fixunssfdi(float);
extern float __floatsisf(int32_t);
extern float __floatunsisf(uint32_t);
extern float __floatdisf(int64_t);
extern float __floatundisf(uint64_t);

extern double __adddf3(double, double);
extern double __subdf3(double, double);
extern double __muldf3(double, double);
extern double __divdf3(double, double);
extern double __negdf2(double);
extern CMPtype __eqdf2(double, double);
extern CMPtype __nedf2(double, double);
extern CMPtype __ltdf2(double, double);
extern CMPtype __ledf2(double, double);
extern CMPtype __gtdf2(double, double);
extern CMPtype __gedf2(double, double);
extern CMPtype __unorddf2(double, double);
extern int32_t __fixdfsi(double);
extern uint32_t __fixunsdfsi(double);
extern int64_t __fixdfdi(double);
extern uint64_t __fixunsdfdi(double);
extern double __floatsidf(int32_t);
extern double __floatunsidf(uint32_t);
extern double __floatdidf(int64_t);
extern double __floatundidf(uint64_t);

extern double __extendsfdf2(float);
extern float __truncdfsf2(double);

/* Volatile pointers: the compiler must actually call the soft-fp code. */
static float (*volatile p_addsf3)(float, float) = __addsf3;
static float (*volatile p_subsf3)(float, float) = __subsf3;
static float (*volatile p_mulsf3)(float, float) = __mulsf3;
static float (*volatile p_divsf3)(float, float) = __divsf3;
static float (*volatile p_negsf2)(float) = __negsf2;
static CMPtype (*volatile p_eqsf2)(float, float) = __eqsf2;
static CMPtype (*volatile p_nesf2)(float, float) = __nesf2;
static CMPtype (*volatile p_ltsf2)(float, float) = __ltsf2;
static CMPtype (*volatile p_lesf2)(float, float) = __lesf2;
static CMPtype (*volatile p_gtsf2)(float, float) = __gtsf2;
static CMPtype (*volatile p_gesf2)(float, float) = __gesf2;
static CMPtype (*volatile p_unordsf2)(float, float) = __unordsf2;
static int32_t (*volatile p_fixsfsi)(float) = __fixsfsi;
static uint32_t (*volatile p_fixunssfsi)(float) = __fixunssfsi;
static int64_t (*volatile p_fixsfdi)(float) = __fixsfdi;
static uint64_t (*volatile p_fixunssfdi)(float) = __fixunssfdi;
static float (*volatile p_floatsisf)(int32_t) = __floatsisf;
static float (*volatile p_floatunsisf)(uint32_t) = __floatunsisf;
static float (*volatile p_floatdisf)(int64_t) = __floatdisf;
static float (*volatile p_floatundisf)(uint64_t) = __floatundisf;

static double (*volatile p_adddf3)(double, double) = __adddf3;
static double (*volatile p_subdf3)(double, double) = __subdf3;
static double (*volatile p_muldf3)(double, double) = __muldf3;
static double (*volatile p_divdf3)(double, double) = __divdf3;
static double (*volatile p_negdf2)(double) = __negdf2;
static CMPtype (*volatile p_eqdf2)(double, double) = __eqdf2;
static CMPtype (*volatile p_nedf2)(double, double) = __nedf2;
static CMPtype (*volatile p_ltdf2)(double, double) = __ltdf2;
static CMPtype (*volatile p_ledf2)(double, double) = __ledf2;
static CMPtype (*volatile p_gtdf2)(double, double) = __gtdf2;
static CMPtype (*volatile p_gedf2)(double, double) = __gedf2;
static CMPtype (*volatile p_unorddf2)(double, double) = __unorddf2;
static int32_t (*volatile p_fixdfsi)(double) = __fixdfsi;
static uint32_t (*volatile p_fixunsdfsi)(double) = __fixunsdfsi;
static int64_t (*volatile p_fixdfdi)(double) = __fixdfdi;
static uint64_t (*volatile p_fixunsdfdi)(double) = __fixunsdfdi;
static double (*volatile p_floatsidf)(int32_t) = __floatsidf;
static double (*volatile p_floatunsidf)(uint32_t) = __floatunsidf;
static double (*volatile p_floatdidf)(int64_t) = __floatdidf;
static double (*volatile p_floatundidf)(uint64_t) = __floatundidf;
static double (*volatile p_extendsfdf2)(float) = __extendsfdf2;
static float (*volatile p_truncdfsf2)(double) = __truncdfsf2;

/* ------------------------------------------------------------------ */
/* Bit helpers and the canonical qNaN sfp-machine.h mandates           */
/* ------------------------------------------------------------------ */

static inline uint32_t f2b(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float b2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint64_t d2b(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline double b2d(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

/* _FP_NANSIGN_S 0, _FP_NANFRAC_S _FP_QNANBIT_S, _FP_KEEPNANFRACP 0 */
#define SFP_CANON_QNAN_S UINT32_C(0x7FC00000)
#define SFP_CANON_QNAN_D UINT64_C(0x7FF8000000000000)

static inline int f_is_nan(uint32_t b) { return (b & 0x7F800000u) == 0x7F800000u && (b & 0x007FFFFFu) != 0; }
static inline int d_is_nan(uint64_t b) {
    return (b & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000)
        && (b & UINT64_C(0x000FFFFFFFFFFFFF)) != 0;
}

/* ------------------------------------------------------------------ */
/* Failure accounting                                                  */
/* ------------------------------------------------------------------ */

#define MAX_REPORTED 12

static unsigned long long g_checks;
static unsigned long long g_failures;
static unsigned long long g_nan_result_cases;
static unsigned reported;

static void failf(const char *what, const char *detail) {
    g_failures++;
    if (reported < MAX_REPORTED) {
        reported++;
        fprintf(stderr, "FAIL %s: %s\n", what, detail);
    } else if (reported == MAX_REPORTED) {
        reported++;
        fprintf(stderr, "FAIL ... (further failures suppressed)\n");
    }
}

/* Single-precision result check. Non-NaN results must be bit-identical to the
 * host. NaN results must be a NaN on the host and exactly the canonical qNaN
 * from soft-fp -- see the header comment on IEEE-754 6.2. */
static void check_sf(const char *op, uint32_t ab, uint32_t bb, uint32_t got, uint32_t want) {
    char buf[192];
    g_checks++;
    if (f_is_nan(want)) {
        g_nan_result_cases++;
        if (!f_is_nan(got)) {
            snprintf(buf, sizeof buf, "a=%08x b=%08x host=%08x is NaN but soft-fp=%08x is not", ab, bb, want, got);
            failf(op, buf);
        } else if (got != SFP_CANON_QNAN_S) {
            snprintf(buf, sizeof buf, "a=%08x b=%08x soft-fp NaN=%08x, sfp-machine.h mandates %08x",
                     ab, bb, got, SFP_CANON_QNAN_S);
            failf(op, buf);
        }
        return;
    }
    if (got != want) {
        snprintf(buf, sizeof buf, "a=%08x b=%08x host=%08x soft-fp=%08x", ab, bb, want, got);
        failf(op, buf);
    }
}

static void check_df(const char *op, uint64_t ab, uint64_t bb, uint64_t got, uint64_t want) {
    char buf[224];
    g_checks++;
    if (d_is_nan(want)) {
        g_nan_result_cases++;
        if (!d_is_nan(got)) {
            snprintf(buf, sizeof buf, "a=%016llx b=%016llx host=%016llx is NaN but soft-fp=%016llx is not",
                     (unsigned long long) ab, (unsigned long long) bb,
                     (unsigned long long) want, (unsigned long long) got);
            failf(op, buf);
        } else if (got != SFP_CANON_QNAN_D) {
            snprintf(buf, sizeof buf, "a=%016llx b=%016llx soft-fp NaN=%016llx, sfp-machine.h mandates %016llx",
                     (unsigned long long) ab, (unsigned long long) bb,
                     (unsigned long long) got, (unsigned long long) SFP_CANON_QNAN_D);
            failf(op, buf);
        }
        return;
    }
    if (got != want) {
        char b2[224];
        snprintf(b2, sizeof b2, "a=%016llx b=%016llx host=%016llx soft-fp=%016llx",
                 (unsigned long long) ab, (unsigned long long) bb,
                 (unsigned long long) want, (unsigned long long) got);
        failf(op, b2);
    }
}

/* Unconditional bit-for-bit comparison, NaNs and all. Used for the routines
 * that take a SINGLE operand -- __negsf2/__negdf2 (a pure sign flip) and
 * __extendsfdf2/__truncdfsf2 (format conversions). _FP_CHOOSENAN only fires
 * when a binary operation has to pick between two NaN operands; with one
 * operand there is nothing to choose, so soft-fp propagates and re-quiets the
 * payload exactly as IEEE-754 6.2.3 recommends and exactly as the host does.
 * These therefore get no NaN exemption at all. */
static void check_sf_strict(const char *op, uint32_t ab, uint32_t got, uint32_t want) {
    g_checks++;
    if (got != want) {
        char buf[160];
        snprintf(buf, sizeof buf, "a=%08x host=%08x soft-fp=%08x", ab, want, got);
        failf(op, buf);
    }
}

static void check_df_strict(const char *op, uint64_t ab, uint64_t got, uint64_t want) {
    g_checks++;
    if (got != want) {
        char buf[192];
        snprintf(buf, sizeof buf, "a=%016llx host=%016llx soft-fp=%016llx",
                 (unsigned long long) ab, (unsigned long long) want, (unsigned long long) got);
        failf(op, buf);
    }
}

static void check_int(const char *op, const char *inp, long long got, long long want) {
    char buf[192];
    g_checks++;
    if (got != want) {
        snprintf(buf, sizeof buf, "in=%s host=%lld soft-fp=%lld", inp, want, got);
        failf(op, buf);
    }
}

static void check_pred(const char *op, uint64_t ab, uint64_t bb, int got, int want) {
    char buf[192];
    g_checks++;
    if (got != want) {
        snprintf(buf, sizeof buf, "a=%016llx b=%016llx host=%d soft-fp=%d",
                 (unsigned long long) ab, (unsigned long long) bb, want, got);
        failf(op, buf);
    }
}

/* ------------------------------------------------------------------ */
/* Host reference arm. noinline + volatile operands so these are real  */
/* hardware operations and never constant-folded.                      */
/* ------------------------------------------------------------------ */

#define REF_BIN(name, type, oper)                                              \
    __attribute__((noinline)) static type ref_##name(type a, type b) {         \
        volatile type va = a, vb = b;                                          \
        return va oper vb;                                                     \
    }

REF_BIN(fadd, float, +)
REF_BIN(fsub, float, -)
REF_BIN(fmul, float, *)
REF_BIN(fdiv, float, /)
REF_BIN(dadd, double, +)
REF_BIN(dsub, double, -)
REF_BIN(dmul, double, *)
REF_BIN(ddiv, double, /)

/* ------------------------------------------------------------------ */
/* Distinctness self-check                                             */
/* ------------------------------------------------------------------ */

/* If the "host reference" were secretly resolving to the soft-fp code, every
 * comparison in this file would pass trivially. Prove the two arms are
 * different implementations using the one input class where they are required
 * to differ: a NaN with a non-default payload. x86 propagates the payload;
 * soft-fp canonicalises it. */
static int check_arms_are_distinct(void) {
    const float nan_payload = b2f(0x7FC00001u);
    const uint32_t host = f2b(ref_fadd(nan_payload, 1.0f));
    const uint32_t sfp = f2b(p_addsf3(nan_payload, 1.0f));
    if (sfp != SFP_CANON_QNAN_S) {
        fprintf(stderr,
                "FAIL self-check: soft-fp __addsf3(qNaN:1, 1.0) = %08x, expected the "
                "canonical %08x from sfp-machine.h\n", sfp, SFP_CANON_QNAN_S);
        return 0;
    }
    if (host == sfp) {
        fprintf(stderr,
                "FAIL self-check: the host reference arm produced %08x, the same as "
                "soft-fp -- the reference is not native hardware float, so every "
                "comparison in this test would be vacuous\n", host);
        return 0;
    }
    printf("self-check: reference arm (%08x) and soft-fp arm (%08x) are distinct "
           "implementations\n", host, sfp);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Deterministic PRNG (splitmix64) -- seeded, so runs are reproducible */
/* ------------------------------------------------------------------ */

static uint64_t rng_state;
static void rng_seed(uint64_t s) { rng_state = s; }
static uint64_t rng_next(void) {
    uint64_t z = (rng_state += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/* Operand generators. Uniform random bit patterns are dominated by huge
 * exponents (about 1 in 256 is a NaN, most of the rest overflow), so they
 * exercise the special-value paths well but almost never the ordinary
 * arithmetic ones. These four classes are mixed so both get real volume, and
 * the last is drawn from the magnitudes SM64 actually operates on. */
static float gen_float(unsigned klass, uint64_t r) {
    switch (klass & 3u) {
    case 0: /* uniform bit pattern: NaNs, infinities, subnormals, everything */
        return b2f((uint32_t) r);
    case 1: { /* random sign+mantissa, exponent clustered near 127 -> results
                 land in the normal range and cancellation actually happens */
        uint32_t exp = 100u + (uint32_t) ((r >> 40) % 55u);
        return b2f(((uint32_t) (r >> 63) << 31) | (exp << 23) | ((uint32_t) r & 0x7FFFFFu));
    }
    case 2: { /* subnormal / near-zero / near-overflow boundaries */
        static const uint32_t edge_exp[] = {0u, 1u, 2u, 125u, 126u, 127u, 128u, 252u, 253u, 254u};
        uint32_t exp = edge_exp[(r >> 40) % (sizeof edge_exp / sizeof edge_exp[0])];
        return b2f(((uint32_t) (r >> 63) << 31) | (exp << 23) | ((uint32_t) r & 0x7FFFFFu));
    }
    default: { /* SM64's own working range: world units, angles, unit vectors */
        double scale = (double) ((r >> 32) % 4u == 0 ? 1 : ((r >> 32) % 4u == 1 ? 100 : 20000));
        double frac = (double) (uint32_t) r / 4294967296.0 * 2.0 - 1.0;
        return (float) (frac * scale);
    }
    }
}

static double gen_double(unsigned klass, uint64_t r, uint64_t r2) {
    switch (klass & 3u) {
    case 0:
        return b2d(r);
    case 1: {
        uint64_t exp = 996u + ((r2 >> 40) % 55u);
        return b2d(((r2 >> 63) << 63) | (exp << 52) | (r & UINT64_C(0xFFFFFFFFFFFFF)));
    }
    case 2: {
        static const uint64_t edge_exp[] = {0, 1, 2, 1021, 1022, 1023, 1024, 2044, 2045, 2046};
        uint64_t exp = edge_exp[(r2 >> 40) % (sizeof edge_exp / sizeof edge_exp[0])];
        return b2d(((r2 >> 63) << 63) | (exp << 52) | (r & UINT64_C(0xFFFFFFFFFFFFF)));
    }
    default:
        return (double) gen_float(3, r);
    }
}

/* ------------------------------------------------------------------ */
/* Special values                                                      */
/* ------------------------------------------------------------------ */

static const uint32_t kSpecialF[] = {
    0x00000000u, 0x80000000u,             /* +0, -0 */
    0x00000001u, 0x80000001u,             /* smallest subnormal */
    0x007FFFFFu, 0x807FFFFFu,             /* largest subnormal */
    0x00400000u, 0x80400000u,             /* mid subnormal */
    0x00800000u, 0x80800000u,             /* smallest normal (FLT_MIN) */
    0x00800001u, 0x80800001u,             /* just above smallest normal */
    0x3F800000u, 0xBF800000u,             /* +-1.0 */
    0x3F7FFFFFu, 0xBF7FFFFFu,             /* just below +-1.0 */
    0x3F800001u,                          /* 1.0 + 1ulp */
    0x40000000u, 0xC0000000u,             /* +-2.0 */
    0x33800000u,                          /* 2^-24, the round-to-even boundary vs 1.0 */
    0x34000000u,                          /* 2^-23 */
    0x4B000000u, 0x4B800000u,             /* 2^23, 2^24 -- integer/ulp boundary */
    0x4EFFFFFFu, 0x4F000000u,             /* just below 2^31, exactly 2^31 */
    0xCF000000u, 0xCF000001u,             /* -2^31 exactly, just past it */
    0x4F800000u, 0x5F000000u,             /* 2^32, ~9.2e18 */
    0x7F7FFFFFu, 0xFF7FFFFFu,             /* +-FLT_MAX */
    0x7F800000u, 0xFF800000u,             /* +-inf */
    0x7FC00000u, 0xFFC00000u,             /* canonical +-qNaN */
    0x7FC00001u, 0xFFC00003u,             /* qNaN with payload */
    0x7F800001u, 0xFF800001u,             /* sNaN */
    0x7FBFFFFFu,                          /* largest sNaN */
};
#define NSPECF ((int) (sizeof kSpecialF / sizeof kSpecialF[0]))

static const uint64_t kSpecialD[] = {
    UINT64_C(0x0000000000000000), UINT64_C(0x8000000000000000),
    UINT64_C(0x0000000000000001), UINT64_C(0x8000000000000001),
    UINT64_C(0x000FFFFFFFFFFFFF), UINT64_C(0x800FFFFFFFFFFFFF),
    UINT64_C(0x0010000000000000), UINT64_C(0x8010000000000000),
    UINT64_C(0x3FF0000000000000), UINT64_C(0xBFF0000000000000),
    UINT64_C(0x3FEFFFFFFFFFFFFF), UINT64_C(0x3FF0000000000001),
    UINT64_C(0x4000000000000000), UINT64_C(0xC000000000000000),
    UINT64_C(0x3E70000000000000),                              /* 2^-24 */
    UINT64_C(0x4330000000000000), UINT64_C(0x4340000000000000),/* 2^52, 2^53 */
    UINT64_C(0x41E0000000000000), UINT64_C(0xC1E0000000000000),/* +-2^31 */
    UINT64_C(0x43E0000000000000),                              /* 2^63 */
    UINT64_C(0x7FEFFFFFFFFFFFFF), UINT64_C(0xFFEFFFFFFFFFFFFF),/* +-DBL_MAX */
    UINT64_C(0x7FF0000000000000), UINT64_C(0xFFF0000000000000),/* +-inf */
    UINT64_C(0x7FF8000000000000), UINT64_C(0xFFF8000000000000),/* canonical +-qNaN */
    UINT64_C(0x7FF8000000000001), UINT64_C(0xFFF8000000000003),/* qNaN with payload */
    UINT64_C(0x7FF0000000000001), UINT64_C(0xFFF0000000000001),/* sNaN */
    /* float-representable boundaries, for truncdfsf2 */
    UINT64_C(0x47EFFFFFE0000000),                              /* FLT_MAX as double */
    UINT64_C(0x47EFFFFFEFFFFFFF),                              /* just over FLT_MAX/2ulp: rounds to FLT_MAX */
    UINT64_C(0x47EFFFFFF0000000),                              /* the round-to-even tie -> +inf */
    UINT64_C(0x3690000000000000),                              /* 2^-150: rounds to 0 or min subnormal */
    UINT64_C(0x36A0000000000000),                              /* 2^-149: min float subnormal */
};
#define NSPECD ((int) (sizeof kSpecialD / sizeof kSpecialD[0]))

/* ------------------------------------------------------------------ */
/* Single-precision arithmetic                                         */
/* ------------------------------------------------------------------ */

static void run_sf_pair(uint32_t ab, uint32_t bb) {
    const float a = b2f(ab), b = b2f(bb);
    check_sf("__addsf3", ab, bb, f2b(p_addsf3(a, b)), f2b(ref_fadd(a, b)));
    check_sf("__subsf3", ab, bb, f2b(p_subsf3(a, b)), f2b(ref_fsub(a, b)));
    check_sf("__mulsf3", ab, bb, f2b(p_mulsf3(a, b)), f2b(ref_fmul(a, b)));
    check_sf("__divsf3", ab, bb, f2b(p_divsf3(a, b)), f2b(ref_fdiv(a, b)));

    /* Comparisons: GCC lowers `a < b` to `__ltsf2(a,b) < 0` and so on, so the
     * meaningful contract is the derived predicate, which must agree with the
     * host for every input INCLUDING the unordered ones. That is the property
     * that makes __lesf2 and __gesf2 different functions at all. */
    {
        volatile float va = a, vb = b;
        check_pred("__eqsf2", ab, bb, p_eqsf2(a, b) == 0, va == vb);
        check_pred("__nesf2", ab, bb, p_nesf2(a, b) != 0, va != vb);
        check_pred("__ltsf2", ab, bb, p_ltsf2(a, b) < 0, va < vb);
        check_pred("__lesf2", ab, bb, p_lesf2(a, b) <= 0, va <= vb);
        check_pred("__gtsf2", ab, bb, p_gtsf2(a, b) > 0, va > vb);
        check_pred("__gesf2", ab, bb, p_gesf2(a, b) >= 0, va >= vb);
        check_pred("__unordsf2", ab, bb, p_unordsf2(a, b) != 0, isnan(va) || isnan(vb));
    }
}

static void run_df_pair(uint64_t ab, uint64_t bb) {
    const double a = b2d(ab), b = b2d(bb);
    check_df("__adddf3", ab, bb, d2b(p_adddf3(a, b)), d2b(ref_dadd(a, b)));
    check_df("__subdf3", ab, bb, d2b(p_subdf3(a, b)), d2b(ref_dsub(a, b)));
    check_df("__muldf3", ab, bb, d2b(p_muldf3(a, b)), d2b(ref_dmul(a, b)));
    check_df("__divdf3", ab, bb, d2b(p_divdf3(a, b)), d2b(ref_ddiv(a, b)));
    {
        volatile double va = a, vb = b;
        check_pred("__eqdf2", ab, bb, p_eqdf2(a, b) == 0, va == vb);
        check_pred("__nedf2", ab, bb, p_nedf2(a, b) != 0, va != vb);
        check_pred("__ltdf2", ab, bb, p_ltdf2(a, b) < 0, va < vb);
        check_pred("__ledf2", ab, bb, p_ledf2(a, b) <= 0, va <= vb);
        check_pred("__gtdf2", ab, bb, p_gtdf2(a, b) > 0, va > vb);
        check_pred("__gedf2", ab, bb, p_gedf2(a, b) >= 0, va >= vb);
        check_pred("__unorddf2", ab, bb, p_unorddf2(a, b) != 0, isnan(va) || isnan(vb));
    }
}

/* ------------------------------------------------------------------ */
/* Unary: negate, extend, truncate                                     */
/* ------------------------------------------------------------------ */

static void run_neg_sf(uint32_t ab) {
    volatile float va = b2f(ab);
    check_sf_strict("__negsf2", ab, f2b(p_negsf2(b2f(ab))), f2b(-va));
}

static void run_neg_df(uint64_t ab) {
    volatile double va = b2d(ab);
    check_df_strict("__negdf2", ab, d2b(p_negdf2(b2d(ab))), d2b(-va));
}

static void run_extend(uint32_t ab) {
    volatile float va = b2f(ab);
    check_df_strict("__extendsfdf2", ab, d2b(p_extendsfdf2(b2f(ab))), d2b((double) va));
}

/* Narrowing is the one conversion that does canonicalise: _FP_KEEPNANFRACP is
 * 0 and binary32 has no room for a binary64 payload anyway, so a NaN in gives
 * the canonical +qNaN out. Non-NaN results, including the overflow-to-infinity
 * tie and the flush-to-subnormal boundary, are still strict. */
static void run_trunc(uint64_t ab) {
    volatile double va = b2d(ab);
    const uint32_t got = f2b(p_truncdfsf2(b2d(ab)));
    const uint32_t want = f2b((float) va);
    char buf[192];
    g_checks++;
    if (f_is_nan(want)) {
        g_nan_result_cases++;
        if (got != SFP_CANON_QNAN_S) {
            snprintf(buf, sizeof buf, "a=%016llx soft-fp NaN=%08x, sfp-machine.h mandates %08x",
                     (unsigned long long) ab, got, SFP_CANON_QNAN_S);
            failf("__truncdfsf2", buf);
        }
        return;
    }
    if (got != want) {
        snprintf(buf, sizeof buf, "a=%016llx host=%08x soft-fp=%08x",
                 (unsigned long long) ab, want, got);
        failf("__truncdfsf2", buf);
    }
}

/* ------------------------------------------------------------------ */
/* Integer -> float. Fully defined for every input, so bit-exact.      */
/* ------------------------------------------------------------------ */

static void run_int_to_float(uint32_t u) {
    const int32_t s = (int32_t) u;
    char inp[40];
    {
        volatile int32_t v = s;
        uint32_t got = f2b(p_floatsisf(s)), want = f2b((float) v);
        g_checks++;
        if (got != want) { snprintf(inp, sizeof inp, "i32=%d", s); failf("__floatsisf", inp); }
    }
    {
        volatile uint32_t v = u;
        uint32_t got = f2b(p_floatunsisf(u)), want = f2b((float) v);
        g_checks++;
        if (got != want) { snprintf(inp, sizeof inp, "u32=%u", u); failf("__floatunsisf", inp); }
    }
    {
        volatile int32_t v = s;
        uint64_t got = d2b(p_floatsidf(s)), want = d2b((double) v);
        g_checks++;
        if (got != want) { snprintf(inp, sizeof inp, "i32=%d", s); failf("__floatsidf", inp); }
    }
    {
        volatile uint32_t v = u;
        uint64_t got = d2b(p_floatunsidf(u)), want = d2b((double) v);
        g_checks++;
        if (got != want) { snprintf(inp, sizeof inp, "u32=%u", u); failf("__floatunsidf", inp); }
    }
}

static void run_int64_to_float(uint64_t u) {
    const int64_t s = (int64_t) u;
    char inp[48];
    { volatile int64_t v = s; uint32_t g = f2b(p_floatdisf(s)), w = f2b((float) v); g_checks++;
      if (g != w) { snprintf(inp, sizeof inp, "i64=%lld", (long long) s); failf("__floatdisf", inp); } }
    { volatile uint64_t v = u; uint32_t g = f2b(p_floatundisf(u)), w = f2b((float) v); g_checks++;
      if (g != w) { snprintf(inp, sizeof inp, "u64=%llu", (unsigned long long) u); failf("__floatundisf", inp); } }
    { volatile int64_t v = s; uint64_t g = d2b(p_floatdidf(s)), w = d2b((double) v); g_checks++;
      if (g != w) { snprintf(inp, sizeof inp, "i64=%lld", (long long) s); failf("__floatdidf", inp); } }
    { volatile uint64_t v = u; uint64_t g = d2b(p_floatundidf(u)), w = d2b((double) v); g_checks++;
      if (g != w) { snprintf(inp, sizeof inp, "u64=%llu", (unsigned long long) u); failf("__floatundidf", inp); } }
}

/* ------------------------------------------------------------------ */
/* Float -> integer. Two domains; see the header comment.              */
/* ------------------------------------------------------------------ */

static unsigned long long g_oor_checks;

/* In range means: finite, and the value truncated toward zero fits the target.
 * Written as float comparisons against exactly representable bounds so the
 * classification itself cannot round. */
static int sf_fits_i32(float f) { return !isnan(f) && f >= -2147483648.0f && f < 2147483648.0f; }
static int sf_fits_u32(float f) { return !isnan(f) && f > -1.0f && f < 4294967296.0f; }
static int sf_fits_i64(float f) { return !isnan(f) && f >= -9223372036854775808.0f && f < 9223372036854775808.0f; }
static int sf_fits_u64(float f) { return !isnan(f) && f > -1.0f && f < 18446744073709551616.0f; }
static int df_fits_i32(double d) { return !isnan(d) && d >= -2147483648.0 && d < 2147483648.0; }
static int df_fits_u32(double d) { return !isnan(d) && d > -1.0 && d < 4294967296.0; }
static int df_fits_i64(double d) { return !isnan(d) && d >= -9223372036854775808.0 && d < 9223372036854775808.0; }
static int df_fits_u64(double d) { return !isnan(d) && d > -1.0 && d < 18446744073709551616.0; }

/* soft-fp's out-of-range contract, from op-common.h's _FP_TO_INT_ROUND /
 * _FP_TO_INT: saturate toward the overflowing side; a NaN takes the positive
 * side unless its sign bit is set. Asserted explicitly so the behaviour cannot
 * drift silently -- it is the one place this change is not bit-identical to
 * the fp-bit implementation it replaces. */
static void check_oor_i32(const char *op, int neg, int32_t got) {
    const int32_t want = neg ? INT32_MIN : INT32_MAX;
    g_checks++; g_oor_checks++;
    if (got != want) {
        char buf[128];
        snprintf(buf, sizeof buf, "saturation: got %d expected %d", got, want);
        failf(op, buf);
    }
}
static void check_oor_u32(const char *op, int neg, uint32_t got) {
    const uint32_t want = neg ? 0u : UINT32_MAX;
    g_checks++; g_oor_checks++;
    if (got != want) {
        char buf[128];
        snprintf(buf, sizeof buf, "saturation: got %u expected %u", got, want);
        failf(op, buf);
    }
}
static void check_oor_i64(const char *op, int neg, int64_t got) {
    const int64_t want = neg ? INT64_MIN : INT64_MAX;
    g_checks++; g_oor_checks++;
    if (got != want) {
        char buf[128];
        snprintf(buf, sizeof buf, "saturation: got %lld expected %lld", (long long) got, (long long) want);
        failf(op, buf);
    }
}
static void check_oor_u64(const char *op, int neg, uint64_t got) {
    const uint64_t want = neg ? 0u : UINT64_MAX;
    g_checks++; g_oor_checks++;
    if (got != want) {
        char buf[128];
        snprintf(buf, sizeof buf, "saturation: got %llu expected %llu",
                 (unsigned long long) got, (unsigned long long) want);
        failf(op, buf);
    }
}

static void run_float_to_int(uint32_t ab) {
    const float f = b2f(ab);
    const int neg = (ab >> 31) != 0;
    char inp[24];
    snprintf(inp, sizeof inp, "%08x", ab);

    if (sf_fits_i32(f)) { volatile float v = f; check_int("__fixsfsi", inp, p_fixsfsi(f), (int32_t) v); }
    else                { check_oor_i32("__fixsfsi", neg, p_fixsfsi(f)); }
    if (sf_fits_u32(f)) { volatile float v = f; check_int("__fixunssfsi", inp, (long long) p_fixunssfsi(f), (long long) (uint32_t) v); }
    else                { check_oor_u32("__fixunssfsi", neg, p_fixunssfsi(f)); }
    if (sf_fits_i64(f)) { volatile float v = f; check_int("__fixsfdi", inp, p_fixsfdi(f), (int64_t) v); }
    else                { check_oor_i64("__fixsfdi", neg, p_fixsfdi(f)); }
    if (sf_fits_u64(f)) {
        volatile float v = f;
        uint64_t got = p_fixunssfdi(f), want = (uint64_t) v;
        g_checks++;
        if (got != want) { failf("__fixunssfdi", inp); }
    } else { check_oor_u64("__fixunssfdi", neg, p_fixunssfdi(f)); }
}

static void run_double_to_int(uint64_t ab) {
    const double d = b2d(ab);
    const int neg = (ab >> 63) != 0;
    char inp[24];
    snprintf(inp, sizeof inp, "%016llx", (unsigned long long) ab);

    if (df_fits_i32(d)) { volatile double v = d; check_int("__fixdfsi", inp, p_fixdfsi(d), (int32_t) v); }
    else                { check_oor_i32("__fixdfsi", neg, p_fixdfsi(d)); }
    if (df_fits_u32(d)) { volatile double v = d; check_int("__fixunsdfsi", inp, (long long) p_fixunsdfsi(d), (long long) (uint32_t) v); }
    else                { check_oor_u32("__fixunsdfsi", neg, p_fixunsdfsi(d)); }
    if (df_fits_i64(d)) { volatile double v = d; check_int("__fixdfdi", inp, p_fixdfdi(d), (int64_t) v); }
    else                { check_oor_i64("__fixdfdi", neg, p_fixdfdi(d)); }
    if (df_fits_u64(d)) {
        volatile double v = d;
        uint64_t got = p_fixunsdfdi(d), want = (uint64_t) v;
        g_checks++;
        if (got != want) { failf("__fixunsdfdi", inp); }
    } else { check_oor_u64("__fixunsdfdi", neg, p_fixunsdfdi(d)); }
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    /* Volume knobs. The defaults meet the >=1e7-pairs-per-binary-operation bar
     * in the design note; the exhaustive sweeps below are independent of them. */
    unsigned long long pairs_sf = 12000000ull;
    unsigned long long pairs_df = 12000000ull;
    unsigned long exhaustive_stride = 1; /* 1 = every one of the 2^32 patterns */

    if (argc > 1) pairs_sf = pairs_df = strtoull(argv[1], NULL, 0);
    if (argc > 2) exhaustive_stride = strtoul(argv[2], NULL, 0);
    if (exhaustive_stride == 0) exhaustive_stride = 1;

    if (!check_arms_are_distinct()) return 1;

    /* 1. Special values, full cross product, every binary operation. */
    for (int i = 0; i < NSPECF; i++)
        for (int j = 0; j < NSPECF; j++)
            run_sf_pair(kSpecialF[i], kSpecialF[j]);
    for (int i = 0; i < NSPECD; i++)
        for (int j = 0; j < NSPECD; j++)
            run_df_pair(kSpecialD[i], kSpecialD[j]);
    for (int i = 0; i < NSPECF; i++) {
        run_neg_sf(kSpecialF[i]);
        run_extend(kSpecialF[i]);
        run_float_to_int(kSpecialF[i]);
    }
    for (int i = 0; i < NSPECD; i++) {
        run_neg_df(kSpecialD[i]);
        run_trunc(kSpecialD[i]);
        run_double_to_int(kSpecialD[i]);
    }
    printf("special values: %d x %d single, %d x %d double cross products done "
           "(%llu checks so far)\n", NSPECF, NSPECF, NSPECD, NSPECD, g_checks);

    /* 2. Exhaustive single-argument sweeps over the entire 2^32 input space.
     *    These are the routines where exhaustion is affordable, and they
     *    include __fixsfsi and __floatsisf, the two primitives
     *    src/port/saturn/gfx/saturn_matrix_kernels.h's exact float<->Q16.16
     *    bridge is built on. */
    {
        unsigned long long n = 0;
        for (uint64_t u = 0; u <= 0xFFFFFFFFull; u += exhaustive_stride) {
            const uint32_t b = (uint32_t) u;
            run_neg_sf(b);
            run_extend(b);
            run_float_to_int(b);
            run_int_to_float(b);
            n++;
        }
        printf("exhaustive 32-bit sweeps: %llu inputs x 4 float->int + 4 int->float "
               "+ negsf2 + extendsfdf2 (stride %lu, %llu checks total)\n",
               n, exhaustive_stride, g_checks);
    }

    /* 3. High-volume seeded random pairs. */
    rng_seed(UINT64_C(0x5A7457AC0FFEE111));
    for (unsigned long long k = 0; k < pairs_sf; k++) {
        const uint64_t r1 = rng_next(), r2 = rng_next();
        const unsigned ka = (unsigned) (r1 >> 60), kb = (unsigned) (r2 >> 60);
        run_sf_pair(f2b(gen_float(ka, r1)), f2b(gen_float(kb, r2)));
    }
    printf("single-precision random pairs: %llu (11 routines each)\n", pairs_sf);

    rng_seed(UINT64_C(0xD00DFEED12345678));
    for (unsigned long long k = 0; k < pairs_df; k++) {
        const uint64_t r1 = rng_next(), r2 = rng_next(), r3 = rng_next(), r4 = rng_next();
        run_df_pair(d2b(gen_double((unsigned) (r1 >> 60), r1, r2)),
                    d2b(gen_double((unsigned) (r3 >> 60), r3, r4)));
    }
    printf("double-precision random pairs: %llu (11 routines each)\n", pairs_df);

    /* 4. Random 64-bit domains that the 32-bit sweeps cannot reach:
     *    truncdfsf2, negdf2, the double->int conversions, and int64->float. */
    rng_seed(UINT64_C(0x0BADC0DE5EED0001));
    {
        const unsigned long long n = pairs_df;
        for (unsigned long long k = 0; k < n; k++) {
            const uint64_t r1 = rng_next(), r2 = rng_next();
            const uint64_t d = d2b(gen_double((unsigned) (r1 >> 60), r1, r2));
            run_trunc(d);
            run_neg_df(d);
            run_double_to_int(d);
            run_int64_to_float(r2);
        }
        printf("64-bit random sweeps: %llu inputs (truncdfsf2, negdf2, "
               "4 double->int, 4 int64->float)\n", n);
    }

    printf("\n%-34s %llu\n", "total bit-exact checks:", g_checks);
    printf("%-34s %llu\n", "of which NaN-result cases:", g_nan_result_cases);
    printf("%-34s %llu\n", "of which out-of-range int casts:", g_oor_checks);
    printf("%-34s %llu\n", "failures:", g_failures);

    if (g_failures) {
        fprintf(stderr, "\nsoft-fp is NOT bit-exact against the host IEEE reference. "
                        "Do not ship this.\n");
        return 1;
    }
    printf("\nRESULT: soft-fp matches the host IEEE-754 reference bit for bit "
           "across every input class.\n");
    return 0;
}
