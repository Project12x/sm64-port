/* Differential fixture for Task 1's render-only native math seam. */
#include <assert.h>
#include <stdint.h>

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
    test_signed_division_matches_reference();
    return 0;
}
