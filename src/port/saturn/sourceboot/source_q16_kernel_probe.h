#ifndef SM64_SATURN_SOURCE_Q16_KERNEL_PROBE_H
#define SM64_SATURN_SOURCE_Q16_KERNEL_PROBE_H

#include <stdint.h>

/* A debugger-readable, cache-through result block for the one-shot target
 * vector. Magic is written last, so an observer never accepts a torn result. */
#define SM64_SATURN_Q16_KERNEL_PROBE_MAGIC 0x51313631U /* "Q161" */
#define SM64_SATURN_Q16_KERNEL_PROBE_VERSION 1U

typedef struct sm64_saturn_q16_kernel_probe {
    uint32_t magic;
    uint32_t version;
    int32_t multiply_positive;
    int32_t multiply_negative;
    int32_t quotient;
    uint32_t status;
} sm64_saturn_q16_kernel_probe_t;

extern volatile sm64_saturn_q16_kernel_probe_t sourceboot_q16_kernel_probe;

void sm64_saturn_sourceboot_q16_kernel_probe_run(void);

#endif
