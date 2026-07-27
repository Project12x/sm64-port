#include <stdint.h>

#include <yaul.h>

#include "source_q16_kernel_probe.h"
#include "../gfx/saturn_q16_sh2.h"
#include "../gpl/slavedriver_projection.h"

volatile sm64_saturn_q16_kernel_probe_t sourceboot_q16_kernel_probe;

/* Volatile inputs prevent this target-vector exercise being folded into a
 * compile-time constant. The execution order is deliberate: DIVU launch,
 * independent multiplies, then quotient collect. */
static volatile int32_t sourceboot_q16_probe_inputs[] = {
    0x00018000, /* 1.5 */
    0x00008000, /* 0.5 */
    (int32_t)0xffff0000 /* -1.0 */
};

void __attribute__((noinline))
sm64_saturn_sourceboot_q16_kernel_probe_run(void)
{
    volatile sm64_saturn_q16_kernel_probe_t * const result =
        (volatile sm64_saturn_q16_kernel_probe_t *)(CPU_CACHE_THROUGH |
                                                    (uintptr_t)&sourceboot_q16_kernel_probe);
    sm64_saturn_divu_q16_t divide = { 0 };
    int32_t quotient = 0;
    const int32_t one_point_five = sourceboot_q16_probe_inputs[0];
    const int32_t half = sourceboot_q16_probe_inputs[1];
    const int32_t minus_one = sourceboot_q16_probe_inputs[2];

    result->magic = 0;
    result->version = SM64_SATURN_Q16_KERNEL_PROBE_VERSION;
    if (!sm64_saturn_divu_q16_start(&divide, one_point_five, half)) {
        result->status = 1u;
        result->magic = SM64_SATURN_Q16_KERNEL_PROBE_MAGIC;
        return;
    }

    result->multiply_positive = sm64_saturn_q16_mul_sh2(one_point_five, half);
    result->multiply_negative = sm64_saturn_q16_mul_sh2(minus_one, half);
    if (!sm64_saturn_divu_q16_collect(&divide, &quotient)) {
        result->status = 2u;
        result->magic = SM64_SATURN_Q16_KERNEL_PROBE_MAGIC;
        return;
    }

    result->quotient = quotient;
    result->status = (result->multiply_positive == 0x0000c000 &&
                      result->multiply_negative == (int32_t)0xffff8000 &&
                      quotient == 0x00030000) ? 0u : 4u;
    result->magic = SM64_SATURN_Q16_KERNEL_PROBE_MAGIC;
}
