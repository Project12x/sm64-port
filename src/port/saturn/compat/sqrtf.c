/* Minimal positive-domain sqrtf service for the freestanding SH target.
 *
 * SM64 calls sqrtf with sums of squares and squared distances. Yaul's SH
 * toolchain does not ship libm. musl sqrtf.c (blob 740d81c, MIT-compatible)
 * was inspected first, but its reciprocal-square-root table, fenv helpers,
 * and internal libm ABI are a poor fit for this target. This independent
 * implementation uses an exponent-derived seed and four Newton iterations.
 */
#include <stdint.h>

float sqrtf(float value) {
    if (value <= 0.0f) return 0.0f;
    union { float f; uint32_t u; } estimate = { .f = value };
    estimate.u = (estimate.u >> 1) + 0x1FC00000U;
    for (uint8_t iteration = 0; iteration < 4; iteration++)
        estimate.f = 0.5f * (estimate.f + value / estimate.f);
    return estimate.f;
}
