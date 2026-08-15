#ifndef SM64_SATURN_PRENOTIFY_PROFILE_H
#define SM64_SATURN_PRENOTIFY_PROFILE_H

#include <stdint.h>

/*
 * Sprint 2 T2.4 pre-notification decomposition (docs/superpowers/plans/
 * 2026-08-15-sprint2-cadence-recovery.md).
 *
 * WHY THIS EXISTS.  The cadence rig
 * (src/port/saturn/runtime/saturn_render_overlap_phase.c) counts whole
 * VBlank crossings, so the largest block in the frame -- the
 * pre-notification window, construction_begin -> slave NOTIFIED marker,
 * measured at 18.92 VBlanks/frame and bit-identical across three builds --
 * cannot be decomposed by it at all.  Sub-VBlank resolution needs the
 * SH-2 free-running timer.
 *
 * REFERENCE.  SlaveDriver Engine `PROFILE.C:11-30,41-45,66-80`
 * (work/upstream/slavedriver-engine @ a898659, GPL, reference-only --
 * no source copied), recorded as lesson L14 in
 * docs/saturn/evidence/reports/sprint2-t2_0-reference-sweep.md.  Reused
 * shape, not code: fixed node table, zero allocation, nestable push/pop
 * that charges the elapsed interval to the node on top of the stack at
 * every transition, and raw FRT register reads rather than a library call.
 * Divergences, and why:
 *   - SlaveDriver keys nodes by string pointer and grows a 60-node tree at
 *     runtime.  This build uses a compile-time node id and a flat table:
 *     the nesting here is static and known, so the search loop and the
 *     `assert(nmNodes<MAXNMNODES)` failure mode are both unnecessary.
 *   - SlaveDriver's setFastTimer() selects cycles/32 (`PROFILE.C:11-22`).
 *     That wraps a 16-bit FRT every 2,097,152 SH-2 cycles, about 4.7
 *     VBlanks.  Sub-stages here can plausibly reach ten VBlanks, so this
 *     profiler selects cycles/128 instead: 8,388,608 cycles, about 18.7
 *     VBlanks per wrap.  See "WRAP SAFETY" below.
 *   - Totals are accumulated into 32 bits, because the whole window is
 *     about 66,000 ticks -- just past a single 16-bit wrap.
 *
 * DIAGNOSTIC-ONLY.  Every declaration that can emit code or data is behind
 * `SATURN_DIAGNOSTIC_MODE != 0 && defined(__sh__)`, mirroring
 * saturn_peak_probe.h (T2.1).  A product build (SATURN_DIAGNOSTIC_MODE=0)
 * sees the typedefs and no-op macros only -- zero bytes of code or state.
 * Mode 2 is the profiling mode: mode 1 additionally compiles the animation
 * sweep, which overrides Mario's animation and would perturb the very
 * route being measured.
 *
 * WRAP SAFETY.  The 16-bit FRT is extended to 32 bits by summing
 * `(uint16_t)(now - last)` at each probe.  That is exact provided no two
 * consecutive probes are more than one wrap apart.  Two guards:
 *   1. The accumulator is re-seeded at every window begin, so the long
 *      idle between frames is never accumulated across.
 *   2. `max_raw_interval` publishes the largest single 16-bit interval the
 *      run ever observed.  A value approaching 0xFFFF means the margin is
 *      gone and the totals must not be trusted; well below it is positive
 *      evidence that no interval wrapped.
 *
 * PERTURBATION.  A probe is two byte reads of an on-chip I/O register plus
 * a handful of ALU ops -- call it 40 SH-2 cycles.  Probes are placed at
 * stage boundaries only, never inside a loop over scene data.  With ~30
 * probe events per frame that is ~1,200 cycles against a pre-notification
 * window of roughly 8.5 million, i.e. ~0.014%.  Publication to LWRAM
 * happens once per window, not per sample.
 *
 * STORAGE.  Working state is ordinary cached HWRAM `.bss` (fast).  The
 * published record lives in NOLOAD `.lwram_bss` and is written through the
 * SH-2 P2 cache-through alias once per window, so headless Ymir's
 * `mem.peek` observes backing LWRAM rather than a stale cache line -- the
 * sourceboot_cadence_trace / saturn_peak_probe pattern.
 */

#define SM64_SATURN_PRENOTIFY_PROFILE_MAGIC 0x46505246u /* 'FPRF' */
#define SM64_SATURN_PRENOTIFY_PROFILE_VERSION 1u
#define SM64_SATURN_PRENOTIFY_PROFILE_NODES 16u
#define SM64_SATURN_PRENOTIFY_PROFILE_DEPTH 8u

/* Node ids.  Node 0 is the window itself; its self time is whatever the
 * named stages did not account for, i.e. the unattributed remainder. */
enum {
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_WINDOW = 0,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_SNAPSHOT_ACQUIRE,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_ACTOR_POSE,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_BANK_OPEN,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_SPATIAL_ADMIT,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_WORK_ORDER,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_POSITION_SET,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_FRAME_RESET,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_PREPARE_MARIO,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_ACTOR_CLOSURE,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MARIO_CTX,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_QUEUE_RESET,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_GRAPH_PUBLISH,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_QUEUE_CONTEXTS,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_NOTIFY,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_SPARE,
};

/* Published record.  `sequence_begin == sequence_end` and even identifies a
 * stable sample, matching sourceboot_cadence_trace's discipline. */
typedef struct {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t sequence_begin;
    /* Completed pre-notification windows contributing to the accumulators. */
    volatile uint32_t windows;
    /* FRT TCR as actually read back after the divider write (low two bits
     * are the internal-clock select: 0=/8, 1=/32, 2=/128). */
    volatile uint32_t frt_tcr;
    volatile uint32_t window_ticks_last;
    volatile uint32_t window_ticks_accum;
    volatile uint32_t window_ticks_max;
    /* Largest single 16-bit inter-probe interval observed anywhere in the
     * run.  The wrap-safety witness; see the header comment. */
    volatile uint32_t max_raw_interval;
    /* Stack overflow/underflow, or a begin that found the previous window
     * still open (an abandoned window).  Non-zero invalidates nothing on
     * its own but must be reported. */
    volatile uint32_t faults;
    volatile uint32_t node_ticks_accum[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    volatile uint32_t node_ticks_max[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    volatile uint32_t node_ticks_last[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    /* L12 companion: the master does not spin on the render-job slave (it
     * returns PENDING and services other frame-pipeline actions), so these
     * measure the slave window's wall time on the master's clock rather
     * than a spin.  notify -> retirement, then retirement -> terminal. */
    volatile uint32_t retire_events;
    volatile uint32_t notify_to_retire_accum;
    volatile uint32_t notify_to_retire_last;
    volatile uint32_t notify_to_retire_max;
    volatile uint32_t finalize_events;
    volatile uint32_t finalize_ticks_accum;
    volatile uint32_t finalize_ticks_last;
    volatile uint32_t finalize_ticks_max;
    /* Slave-side busy time, written only by the slave SH-2 (its own FRT). */
    volatile uint32_t slave_entries;
    volatile uint32_t slave_busy_accum;
    volatile uint32_t slave_busy_last;
    volatile uint32_t slave_busy_max;
    volatile uint32_t slave_frt_tcr;
    volatile uint32_t sequence_end;
} sm64_saturn_prenotify_profile_t;

_Static_assert(sizeof(sm64_saturn_prenotify_profile_t) == 288U,
               "frame profile ABI must remain seventy-two words");

#if defined(SATURN_DIAGNOSTIC_MODE) && SATURN_DIAGNOSTIC_MODE != 0 && \
    defined(__sh__)

/* SH-2 on-chip FRT block.  Offsets per SlaveDriver PROFILE.C:5-10 and
 * libyaul third_party/libyaul/libyaul/scu/bus/cpu/cpu_frt.c: TIER +0,
 * FTCSR +1, FRC high +2, FRC low +3, TCR +6. */
#define SM64_SATURN_PRENOTIFY_PROFILE_FRT_BASE 0xFFFFFE10u
/* SH-2 P2 cache-through window (libyaul CPU_CACHE_THROUGH, restated so
 * this header does not pull yaul into shared translation units). */
#define SM64_SATURN_PRENOTIFY_PROFILE_CACHE_THROUGH 0x20000000ul

typedef struct {
    uint32_t node_ticks[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    uint32_t elapsed;
    uint32_t max_raw;
    uint32_t faults;
    uint16_t last16;
    uint16_t notify16;
    uint16_t retire16;
    uint8_t stack[SM64_SATURN_PRENOTIFY_PROFILE_DEPTH];
    uint8_t depth;
    uint8_t active;
    uint8_t notified;
    uint8_t retired;
} sm64_saturn_prenotify_profile_state_t;

/* Both defined in src/port/saturn/sourceboot/main.c, diagnostic builds
 * only.  The state is master-owned cached HWRAM; the record is NOLOAD
 * LWRAM reached through P2. */
extern sm64_saturn_prenotify_profile_state_t g_sm64_saturn_prenotify_profile_state;
extern volatile sm64_saturn_prenotify_profile_t g_sm64_saturn_prenotify_profile;

static inline volatile sm64_saturn_prenotify_profile_t *
sm64_saturn_prenotify_profile_visible(void)
{
    return (volatile sm64_saturn_prenotify_profile_t *)(
        SM64_SATURN_PRENOTIFY_PROFILE_CACHE_THROUGH |
        (uintptr_t)&g_sm64_saturn_prenotify_profile);
}

static inline uint16_t sm64_saturn_prenotify_profile_frt(void)
{
    volatile const uint8_t *const frt =
        (volatile const uint8_t *)SM64_SATURN_PRENOTIFY_PROFILE_FRT_BASE;
    const uint16_t high = frt[2];
    const uint16_t low = frt[3];
    return (uint16_t)((high << 8) | low);
}

/* Select the internal clock / 128 on whichever SH-2 executes this.  The FRT
 * block is CPU-local, so the master and the slave are configured
 * independently.  Only the low two bits of TCR are touched; the input-edge
 * bit is preserved.  SlaveDriver's setFastTimer() does the same write with
 * a /32 selector (PROFILE.C:11-22). */
static inline uint8_t sm64_saturn_prenotify_profile_select_clock(void)
{
    volatile uint8_t *const frt =
        (volatile uint8_t *)SM64_SATURN_PRENOTIFY_PROFILE_FRT_BASE;
    frt[6] = (uint8_t)((frt[6] & (uint8_t)~0x03u) | 0x02u);
    return frt[6];
}

/* Charge the interval since the previous probe to the node on top of the
 * stack, then advance the extended clock.  This is SlaveDriver's
 * `currentNode->totalTime += (getTimer()-lastTime)&0xffff` discipline
 * (PROFILE.C:66-80): every accumulation interval is one probe gap, so no
 * single accumulation ever spans a stage. */
static inline void sm64_saturn_prenotify_profile_charge(void)
{
    sm64_saturn_prenotify_profile_state_t *const state =
        &g_sm64_saturn_prenotify_profile_state;
    const uint16_t now = sm64_saturn_prenotify_profile_frt();
    const uint16_t raw = (uint16_t)(now - state->last16);
    state->last16 = now;
    state->elapsed += raw;
    if ((uint32_t)raw > state->max_raw) state->max_raw = raw;
    state->node_ticks[state->stack[state->depth]] += raw;
}

static inline void sm64_saturn_prenotify_profile_begin(void)
{
    sm64_saturn_prenotify_profile_state_t *const state =
        &g_sm64_saturn_prenotify_profile_state;
    if (state->active) state->faults++; /* previous window abandoned */
    for (uint32_t node = 0U; node < SM64_SATURN_PRENOTIFY_PROFILE_NODES; node++)
        state->node_ticks[node] = 0U;
    state->elapsed = 0U;
    state->depth = 0U;
    state->stack[0] = (uint8_t)SM64_SATURN_PRENOTIFY_PROFILE_NODE_WINDOW;
    state->active = 1U;
    state->last16 = sm64_saturn_prenotify_profile_frt();
}

static inline void sm64_saturn_prenotify_profile_push(uint32_t node)
{
    sm64_saturn_prenotify_profile_state_t *const state =
        &g_sm64_saturn_prenotify_profile_state;
    if (!state->active) return;
    if (node >= SM64_SATURN_PRENOTIFY_PROFILE_NODES ||
        state->depth + 1U >= SM64_SATURN_PRENOTIFY_PROFILE_DEPTH) {
        state->faults++;
        return;
    }
    sm64_saturn_prenotify_profile_charge();
    state->depth++;
    state->stack[state->depth] = (uint8_t)node;
}

static inline void sm64_saturn_prenotify_profile_pop(void)
{
    sm64_saturn_prenotify_profile_state_t *const state =
        &g_sm64_saturn_prenotify_profile_state;
    if (!state->active) return;
    if (state->depth == 0U) {
        state->faults++;
        return;
    }
    sm64_saturn_prenotify_profile_charge();
    state->depth--;
}

/* Close the window and publish.  Called from the NOTIFIED marker observer,
 * which is the exact instant the cadence rig stamps notification_vblank --
 * so this total and the rig's pre-notification crossings measure the same
 * interval and can be cross-checked against each other. */
static inline void sm64_saturn_prenotify_profile_end(void)
{
    sm64_saturn_prenotify_profile_state_t *const state =
        &g_sm64_saturn_prenotify_profile_state;
    if (!state->active) return;
    sm64_saturn_prenotify_profile_charge();
    if (state->depth != 0U) state->faults++;
    state->active = 0U;
    state->notify16 = state->last16;
    state->notified = 1U;
    state->retired = 0U;

    volatile sm64_saturn_prenotify_profile_t *const record =
        sm64_saturn_prenotify_profile_visible();
    const uint32_t sequence = record->sequence_end + 1U;
    record->sequence_end = sequence;
    record->windows++;
    record->window_ticks_last = state->elapsed;
    record->window_ticks_accum += state->elapsed;
    if (state->elapsed > record->window_ticks_max)
        record->window_ticks_max = state->elapsed;
    record->max_raw_interval = state->max_raw;
    record->faults = state->faults;
    for (uint32_t node = 0U; node < SM64_SATURN_PRENOTIFY_PROFILE_NODES; node++) {
        const uint32_t ticks = state->node_ticks[node];
        record->node_ticks_last[node] = ticks;
        record->node_ticks_accum[node] += ticks;
        if (ticks > record->node_ticks_max[node])
            record->node_ticks_max[node] = ticks;
    }
    record->sequence_begin = sequence;
}

/* Slave-window wall time on the master's clock (L12).  One 16-bit delta is
 * exact here because the interval is ~2.7 VBlanks against a 18.7-VBlank
 * wrap at /128. */
static inline void sm64_saturn_prenotify_profile_mark_retired(void)
{
    sm64_saturn_prenotify_profile_state_t *const state =
        &g_sm64_saturn_prenotify_profile_state;
    if (!state->notified) return;
    const uint16_t now = sm64_saturn_prenotify_profile_frt();
    const uint32_t ticks = (uint16_t)(now - state->notify16);
    state->retire16 = now;
    state->notified = 0U;
    state->retired = 1U;

    volatile sm64_saturn_prenotify_profile_t *const record =
        sm64_saturn_prenotify_profile_visible();
    record->retire_events++;
    record->notify_to_retire_last = ticks;
    record->notify_to_retire_accum += ticks;
    if (ticks > record->notify_to_retire_max)
        record->notify_to_retire_max = ticks;
}

static inline void sm64_saturn_prenotify_profile_mark_terminal(void)
{
    sm64_saturn_prenotify_profile_state_t *const state =
        &g_sm64_saturn_prenotify_profile_state;
    if (!state->retired) return;
    const uint32_t ticks =
        (uint16_t)(sm64_saturn_prenotify_profile_frt() - state->retire16);
    state->retired = 0U;

    volatile sm64_saturn_prenotify_profile_t *const record =
        sm64_saturn_prenotify_profile_visible();
    record->finalize_events++;
    record->finalize_ticks_last = ticks;
    record->finalize_ticks_accum += ticks;
    if (ticks > record->finalize_ticks_max)
        record->finalize_ticks_max = ticks;
}

/* Slave-owned.  The slave SH-2 has its own FRT block, so it selects its own
 * divider once and writes only fields no master path touches. */
static inline void sm64_saturn_prenotify_profile_slave_begin(uint16_t *start)
{
    volatile sm64_saturn_prenotify_profile_t *const record =
        sm64_saturn_prenotify_profile_visible();
    if (record->slave_frt_tcr == 0U)
        record->slave_frt_tcr =
            0x100u | (uint32_t)sm64_saturn_prenotify_profile_select_clock();
    *start = sm64_saturn_prenotify_profile_frt();
}

static inline void sm64_saturn_prenotify_profile_slave_end(uint16_t start)
{
    const uint32_t ticks =
        (uint16_t)(sm64_saturn_prenotify_profile_frt() - start);
    volatile sm64_saturn_prenotify_profile_t *const record =
        sm64_saturn_prenotify_profile_visible();
    record->slave_entries++;
    record->slave_busy_last = ticks;
    record->slave_busy_accum += ticks;
    if (ticks > record->slave_busy_max) record->slave_busy_max = ticks;
}

#define SM64_SATURN_PRENOTIFY_PROFILE_BEGIN() sm64_saturn_prenotify_profile_begin()
#define SM64_SATURN_PRENOTIFY_PROFILE_PUSH(node) \
    sm64_saturn_prenotify_profile_push((uint32_t)(node))
#define SM64_SATURN_PRENOTIFY_PROFILE_POP() sm64_saturn_prenotify_profile_pop()
#define SM64_SATURN_PRENOTIFY_PROFILE_END() sm64_saturn_prenotify_profile_end()
#define SM64_SATURN_PRENOTIFY_PROFILE_RETIRED() \
    sm64_saturn_prenotify_profile_mark_retired()
#define SM64_SATURN_PRENOTIFY_PROFILE_TERMINAL() \
    sm64_saturn_prenotify_profile_mark_terminal()
#define SM64_SATURN_PRENOTIFY_PROFILE_SLAVE_SCOPE_BEGIN(name) \
    uint16_t name; \
    sm64_saturn_prenotify_profile_slave_begin(&(name))
#define SM64_SATURN_PRENOTIFY_PROFILE_SLAVE_SCOPE_END(name) \
    sm64_saturn_prenotify_profile_slave_end((name))

#else /* product build, or host translation unit */

#define SM64_SATURN_PRENOTIFY_PROFILE_BEGIN() ((void)0)
#define SM64_SATURN_PRENOTIFY_PROFILE_PUSH(node) ((void)0)
#define SM64_SATURN_PRENOTIFY_PROFILE_POP() ((void)0)
#define SM64_SATURN_PRENOTIFY_PROFILE_END() ((void)0)
#define SM64_SATURN_PRENOTIFY_PROFILE_RETIRED() ((void)0)
#define SM64_SATURN_PRENOTIFY_PROFILE_TERMINAL() ((void)0)
#define SM64_SATURN_PRENOTIFY_PROFILE_SLAVE_SCOPE_BEGIN(name) ((void)0)
#define SM64_SATURN_PRENOTIFY_PROFILE_SLAVE_SCOPE_END(name) ((void)0)

#endif /* SATURN_DIAGNOSTIC_MODE != 0 && __sh__ */

#endif /* SM64_SATURN_PRENOTIFY_PROFILE_H */
