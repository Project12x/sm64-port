#include <stdint.h>

#include "saturn_matrix_kernels.h"

#if defined(__GNUC__)
#define PROBE_NOINLINE __attribute__((noinline, used))
#else
#define PROBE_NOINLINE
#endif

PROBE_NOINLINE int32_t probe_float_to_q16(float value)
{
    return sm64_saturn_float_to_q16(value);
}

PROBE_NOINLINE float probe_q16_to_float(int32_t value)
{
    return sm64_saturn_q16_to_float(value);
}
