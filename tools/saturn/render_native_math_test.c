/* Differential fixture for Task 1's render-only native math seam. */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_render_native_math.h"

static void test_world_units_match_safe_c_casts(void)
{
    static const float samples[] = {
        -32767.75f, -8192.5f, -1.999f, -1.0f, -0.999f, -0.0f,
        0.0f, 0.999f, 1.0f, 1.999f, 127.5f, 8192.5f, 32767.75f
    };

    for (uint32_t index = 0; index < sizeof(samples) / sizeof(samples[0]);
         index++) {
        assert(sm64_saturn_world_unit_from_float(samples[index]) ==
               (int32_t)samples[index]);
    }
}

static float float_from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* C does not define a float-to-int cast for out-of-range or non-finite
 * values. The render bridge instead has an explicit saturating contract. */
static void test_world_units_saturate_and_classify_nonfinite(void)
{
    static const struct {
        uint32_t bits;
        int32_t expected;
    } samples[] = {
        {0x4EFFFFFFU, 2147483520}, /* largest finite integer below INT32_MAX */
        {0x4F000000U, INT32_MAX},  /* +2^31 saturates */
        {0xCF000000U, INT32_MIN},  /* -2^31 is exactly representable */
        {0xCF000001U, INT32_MIN},  /* below INT32_MIN saturates */
        {0x7F800000U, INT32_MAX},  /* +Inf saturates */
        {0xFF800000U, INT32_MIN},  /* -Inf saturates */
        {0x7FC00000U, INT32_MAX},  /* positive-sign NaN saturates */
        {0xFFC00000U, INT32_MIN},  /* negative-sign NaN saturates */
    };

    for (uint32_t index = 0; index < sizeof(samples) / sizeof(samples[0]);
         index++) {
        assert(sm64_saturn_world_unit_from_float(float_from_bits(samples[index].bits)) ==
               samples[index].expected);
    }
}

static void test_signed_division_matches_reference(void)
{
    static const struct {
        int64_t dividend;
        int32_t divisor;
    } samples[] = {
        {((int64_t)1 << 45) - 1, 32767},
        {-((int64_t)1 << 45) + 1, 32767},
        {((int64_t)32767 << 16), 181},
        {-((int64_t)32767 << 16), 181},
        {123456789012LL, -12345},
        {-123456789012LL, -12345}
    };

    for (uint32_t index = 0; index < sizeof(samples) / sizeof(samples[0]);
         index++) {
        int32_t quotient = 0;
        assert(sm64_saturn_div_s64_s32(samples[index].dividend,
                                       samples[index].divisor, &quotient));
        assert(quotient == (int32_t)(samples[index].dividend /
                                     samples[index].divisor));
    }
    assert(!sm64_saturn_div_s64_s32(1, 1, NULL));
    assert(!sm64_saturn_div_s64_s32(1, 0, &(int32_t){0}));
    assert(!sm64_saturn_div_s64_s32(INT64_MAX, 1, &(int32_t){0}));
}

int main(void)
{
    test_world_units_match_safe_c_casts();
    test_world_units_saturate_and_classify_nonfinite();
    test_signed_division_matches_reference();
    return 0;
}
