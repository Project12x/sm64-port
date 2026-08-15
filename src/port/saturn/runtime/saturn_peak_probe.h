#ifndef SM64_SATURN_PEAK_PROBE_H
#define SM64_SATURN_PEAK_PROBE_H

#include <stdint.h>

/*
 * Sprint 2 T2.1 instrumented peak capture (docs/superpowers/plans/
 * 2026-08-15-sprint2-cadence-recovery.md).  Target-visible run-long
 * high-water accumulators for the two capacity-shrink gates that have no
 * durable rail in the product build:
 *
 *  - gGfxPool master display-list usage: src/game/game_init.c computes
 *    `entries = gDisplayListHead - gGfxPool->buffer` once per gfx task
 *    (create_gfx_task_structure) and discards it.  It is a live per-frame
 *    value, so host-side sampling misses peaks between samples.
 *  - VDP1 command count: sourceboot's fast3d profile carries
 *    `vdp1_command_highwater`, but sm64_saturn_fast3d_frontend_submit()
 *    memsets the whole profile every frame and the highwater field is NOT
 *    in its preserved-field list, so that "highwater" is effectively the
 *    latest published frame's command_count, not a run-long maximum.
 *
 * DIAGNOSTIC-ONLY: everything that can emit code or data is gated behind
 * SATURN_DIAGNOSTIC_MODE != 0 (the tuple's diagnostic knob; the
 * any-nonzero guard mirrors saturn_demo_render.c's established
 * `#if SATURN_DIAGNOSTIC_MODE` telemetry block).  A product build
 * (SATURN_DIAGNOSTIC_MODE=0) sees only this typedef and macros -- zero
 * bytes of code or state.  Mode 2 is the peak-capture mode: mode 1 also
 * compiles the animation sweep, whose +296 B .text / +92 B .data pushed
 * the link over the HWRAM floor at the R1 tuple's 504 B slack AND whose
 * Mario-animation override would perturb the measured route.
 *
 * Storage is NOLOAD `.lwram_bss` (the tracked diagnostic/telemetry arena;
 * not crt0-zeroed, so sourceboot_reset_lwram_state() initializes it
 * explicitly).  All accesses go through the SH-2 P2 cache-through alias,
 * the sourceboot_cadence_trace pattern, so headless Ymir's mem.peek
 * observes backing LWRAM rather than cached lines.
 */

#define SM64_SATURN_PEAK_PROBE_MAGIC 0x504B5042u /* 'PKPB' */

typedef struct {
    volatile uint32_t magic;
    /* gGfxPool master display list, in Gfx entries (8 B each), sampled at
     * create_gfx_task_structure() -- the per-frame final value. */
    volatile uint32_t gfx_pool_entries_last;
    volatile uint32_t gfx_pool_entries_highwater;
    volatile uint32_t gfx_pool_task_count;
    /* Published VDP1 frame-bank command_count (includes the setup
     * commands), mirrored from the fast3d profile each telemetry update;
     * the highwater here is monotonic for the whole run. */
    volatile uint32_t vdp1_commands_last;
    volatile uint32_t vdp1_commands_highwater;
    /* Published gouraud-table count, same mirror discipline. */
    volatile uint32_t vdp1_gouraud_last;
    volatile uint32_t vdp1_gouraud_highwater;
} sm64_saturn_peak_probe_t;

#if defined(SATURN_DIAGNOSTIC_MODE) && SATURN_DIAGNOSTIC_MODE != 0

/* Defined in src/port/saturn/sourceboot/main.c (diagnostic builds only). */
extern volatile sm64_saturn_peak_probe_t g_sm64_saturn_peak_probe;

/* SH-2 P2 cache-through window (libyaul CPU_CACHE_THROUGH; restated here
 * so shared game code does not grow a yaul include). */
#define SM64_SATURN_PEAK_PROBE_CACHE_THROUGH 0x20000000ul

static inline volatile sm64_saturn_peak_probe_t *
sm64_saturn_peak_probe_visible(void)
{
    return (volatile sm64_saturn_peak_probe_t *)(
        SM64_SATURN_PEAK_PROBE_CACHE_THROUGH |
        (uintptr_t)&g_sm64_saturn_peak_probe);
}

#endif /* SATURN_DIAGNOSTIC_MODE != 0 */

#endif /* SM64_SATURN_PEAK_PROBE_H */
