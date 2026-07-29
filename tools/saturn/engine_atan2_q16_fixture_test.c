/* Host runner for the TARGET_SATURN atan2 Q16 seam.  The Python harness
 * supplies captured IEEE-754 pairs and their source-route angle results. */
#include <stdint.h>
#include <stdio.h>

#if defined(ENGINE_ATAN2_Q16_TARGET)
#define TARGET_SATURN 1
#endif
#define NON_MATCHING 1
#define AVOID_UB 1
#define _LANGUAGE_C 1
#define F3DEX_GBI_2E 1

#include "saturn_engine_math_q16.h"
#include "engine/math_util.c"

Vec3f gVec3fZero = { 0.0f, 0.0f, 0.0f };
void guMtxF2L(float matrix[4][4], Mtx *dest)
{
    (void)matrix;
    (void)dest;
}
f32 find_floor(f32 x, f32 y, f32 z, struct Surface **floor)
{
    (void)x;
    (void)y;
    (void)z;
    (void)floor;
    return 0.0f;
}

static float float_from_bits(uint32_t bits)
{
    union { float f; uint32_t u; } value = { .u = bits };
    return value.f;
}

int main(void)
{
    char function[32];
    uint32_t y_bits;
    uint32_t x_bits;
    unsigned expected;

    while (scanf("%31s %x %x %u", function, &y_bits, &x_bits, &expected) == 4) {
        const float y = float_from_bits(y_bits);
        const float x = float_from_bits(x_bits);
        const unsigned actual = function[5] == 's' ?
            (uint16_t)atan2s(y, x) : atan2_lookup(y, x);
        if (actual != expected) {
            fprintf(stderr, "%s %08x %08x: expected %u, got %u\n",
                    function, y_bits, x_bits, expected, actual);
            return 1;
        }
    }
    return ferror(stdin) ? 2 : 0;
}
