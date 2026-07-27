/* GPL-3.0-or-later. Close-port of the DIVU launch/independent-work/collect
 * schedule in Lobotomy-Software/SlaveDriver-Engine WALLASM.S:253-353,
 * commit a8986591557b6e680550d3c23970284d3b38ff8f.
 *
 * Material changes: this is a tiny Q16.16 transport API, not SlaveDriver's
 * wall renderer. It uses libyaul's DIVU register contract and exposes an
 * explicit start/collect boundary so callers can fill the hardware latency
 * shadow with their own transform work. */
#ifndef SM64_SATURN_SLAVEDRIVER_PROJECTION_H
#define SM64_SATURN_SLAVEDRIVER_PROJECTION_H

#include <stdbool.h>
#include <stdint.h>

/* SH-2 DIVU is at CPU(0x0f00): DVSR 0xffffff00, DVCR 0xffffff08,
 * DVDNTL 0xffffff14. These agree with pinned libyaul's cpu/map.h and
 * cpu/divu.h; retain literal addresses here so the launch routine remains
 * callable from a narrow GPL component without a Yaul header dependency. */
#define SM64_SATURN_DIVU_DVCR   ((volatile uint32_t *)0xFFFFFF08u)
#define SM64_SATURN_DIVU_DVDNTL ((volatile uint32_t *)0xFFFFFF14u)

typedef struct sm64_saturn_divu_q16 {
    int32_t host_quotient;
    bool started;
    uint32_t invalid_fallback_count;
} sm64_saturn_divu_q16_t;

#if defined(__sh__)
void sm64_saturn_slavedriver_divu_q16_start_asm(int32_t dividend,
                                                int32_t divisor);
#endif

static inline bool
sm64_saturn_divu_q16_start(sm64_saturn_divu_q16_t *op, int32_t dividend,
                           int32_t divisor)
{
    if (op == NULL) {
        return false;
    }
    if (divisor == 0 || (dividend == INT32_MIN && divisor == -1)) {
        op->invalid_fallback_count++;
        return false;
    }
#if defined(__sh__)
    /* Clear a prior DVCR overflow before launch; collect checks the fresh bit. */
    *SM64_SATURN_DIVU_DVCR &= ~1u;
    sm64_saturn_slavedriver_divu_q16_start_asm(dividend, divisor);
#else
    {
        const int64_t scaled_quotient = ((int64_t)dividend << 16) / divisor;
        if (scaled_quotient > INT32_MAX || scaled_quotient < INT32_MIN) {
            op->invalid_fallback_count++;
            return false;
        }
        op->host_quotient = (int32_t)scaled_quotient;
    }
#endif
    op->started = true;
    return true;
}

static inline bool
sm64_saturn_divu_q16_collect(sm64_saturn_divu_q16_t *op, int32_t *quotient)
{
    if (op == NULL || quotient == NULL || !op->started) {
        return false;
    }
#if defined(__sh__)
    if ((*SM64_SATURN_DIVU_DVCR & 1u) != 0u) {
        op->started = false;
        op->invalid_fallback_count++;
        return false;
    }
    *quotient = (int32_t)*SM64_SATURN_DIVU_DVDNTL;
#else
    *quotient = op->host_quotient;
#endif
    op->started = false;
    return true;
}

#endif
