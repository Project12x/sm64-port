#ifndef SM64_SATURN_PRENOTIFY_PROFILE_H
#define SM64_SATURN_PRENOTIFY_PROFILE_H

#include <stdint.h>

/*
 * Sprint 2 T2.4 pre-notification decomposition, extended by T2.5 to look
 * inside demo_prepare_mario() (docs/superpowers/plans/
 * 2026-08-15-sprint2-cadence-recovery.md).
 *
 * T2.5 CHANGES, and why (see sprint2-t2_4-prenotification-profile.md S7):
 *   - Eight sub-nodes were added under PREPARE_MARIO, which T2.4 measured at
 *     69.24% of the window and never decomposed.  The two DEPTH nodes are
 *     pushed once per meshlet (31 per pass), never per vertex: an FRT read
 *     is not free and a per-vertex probe would have cost more than the
 *     thing it measured.  Per-vertex cost is derived by division against a
 *     vertex count that is a compile-time property of the mesh (see
 *     saturn_actor_meshlets.c actor_meshlet_live_depth_bounds).
 *   - T2.4 defect 1/2 fixed by REMOVAL.  notify_to_retire and finalize_ticks
 *     subtracted one SH-2's FRT from the other's (the RETIRED marker
 *     observer runs on the slave; the FRT is a per-CPU on-chip block), so
 *     both fields and the two mark_*() entry points are gone.  With them go
 *     the only slave writes to master-owned state.
 *   - T2.4 defect 2 also fixed structurally: the working state is now
 *     __uncached, matching what the shipped cadence rig already does for
 *     sourceboot_render_overlap_phase (sourceboot/main.c).  Belt and
 *     braces -- after the removal above no slave path touches it at all.
 *   - T2.4 defect 3 fixed: end() no longer counts the deliberately-pushed
 *     NOTIFY node as a fault.  The window's expected closing depth is
 *     exactly 1; end_depth_max publishes what was actually seen, and only
 *     depth > 1 is a fault.  The harness's exit code now means something.
 *   - node_calls_last[] was added so per-call cost is measured rather than
 *     assumed.
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
#define SM64_SATURN_PRENOTIFY_PROFILE_VERSION 4u
#define SM64_SATURN_PRENOTIFY_PROFILE_NODES 24u
#define SM64_SATURN_PRENOTIFY_PROFILE_DEPTH 8u
/* T2.8: the corrected frame is ~15 VBlanks (sprint2-t2_7-a9a-regression-
 * attribution.md S1.2; the 41-VBlank figure quoted elsewhere came from a
 * hand-derived FPS that included the pre-gameplay ramp).  A 32-entry ring of
 * per-VBlank VDP1 COPR samples therefore spans about two whole frames.
 * Power of two so the ISR index advance is a mask, not a modulo. */
#define SM64_SATURN_PRENOTIFY_PROFILE_COPR_RING 32u

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
    /* T2.5 sub-nodes.  All nest under PREPARE_MARIO; ids 0-14 are
     * unchanged so T2.4's ranked table remains directly comparable. */
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MARIO_SETUP,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_PREPARE,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_ADMIT,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_DEPTH_ADMIT,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_PREFIX,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_EMIT,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_DEPTH_EMIT,
    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MARIO_DRAW_ORDER,
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
    /* Largest closing stack depth end() ever saw.  1 is by design (the
     * NOTIFY node is still pushed when the marker fires); anything above
     * that is a genuine push/pop imbalance and is counted in `faults`. */
    volatile uint32_t end_depth_max;
    volatile uint32_t node_ticks_accum[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    volatile uint32_t node_ticks_max[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    volatile uint32_t node_ticks_last[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    /* Push count per node in the most recently closed window.  Makes
     * per-call cost a measurement rather than a source-reading assumption. */
    volatile uint32_t node_calls_last[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    /* L12 companion.  T2.4's notify_to_retire/finalize_ticks fields were
     * removed here: they differenced the master's FRT against the slave's,
     * because the RETIRED marker observer runs on the slave.  The cadence
     * rig's VBlank crossings already measure both intervals correctly.
     * Slave-side busy time, written only by the slave SH-2 (its own FRT),
     * begun and ended on the same CPU, is unaffected and stays. */
    volatile uint32_t slave_entries;
    volatile uint32_t slave_busy_accum;
    volatile uint32_t slave_busy_last;
    volatile uint32_t slave_busy_max;
    volatile uint32_t slave_frt_tcr;
    /* ---------------------------------------------------------------- *
     * T2.8 present path and VDP1 draw fence.
     *
     * WHY THESE LIVE HERE AND NOT IN A SECOND RIG.  The pre-notification
     * window closes at the NOTIFIED marker (sourceboot/main.c), so the
     * present path is entirely outside node accounting.  Rather than stand
     * up a second published record with its own magic, sequence discipline,
     * P2 alias and host decoder, the present path reuses this one: same
     * master ownership, same NOLOAD .lwram_bss storage, same cache-through
     * publication, same capture tool.  These fields are written by
     * sourceboot_present_generation / sourceboot_frame_poll_transfers and
     * by the VBlank-OUT handler; none of them participates in the node
     * stack, so they cannot perturb the T2.4/T2.5/T2.6 numbers.
     *
     * WRAP.  Every span here is accumulated from per-iteration or
     * per-call-site 16-bit FRT differences, never one delta across a whole
     * blocking wait, because at phi/128 a 16-bit FRT wraps at ~312 ms ~=
     * 18.8 VBlanks and a stalled frame can exceed that even though the
     * healthy frame is ~15.  vdp1_fence_max_raw publishes the
     * largest single inter-probe interval the fence ever saw; a value well
     * below 0xFFFF is the positive evidence that nothing aliased.
     * ---------------------------------------------------------------- */
    /* Completed sourceboot_present_generation calls. */
    volatile uint32_t present_windows;
    volatile uint32_t present_ticks_last;
    volatile uint32_t present_ticks_accum;
    volatile uint32_t present_ticks_max;
    /* vdp1_sync_render(): spins on VDP1_FLAG_LIST_XFERRED, then starts the
     * plot (libyaul vdp_sync.c vdp1_sync_render / _vdp1_mode_variable_
     * sync_render). */
    volatile uint32_t vdp1_render_ticks_last;
    volatile uint32_t vdp1_render_ticks_accum;
    volatile uint32_t vdp1_render_ticks_max;
    /* vdp1_sync(): sets SYNC_FLAG_VDP1_SYNC and returns.  Published to
     * prove it does not block, which the sprint-2 gap study assumed it
     * did. */
    volatile uint32_t vdp1_sync_ticks_last;
    volatile uint32_t vdp1_sync_ticks_accum;
    /* sm64_saturn_vdp2_frame_begin + _commit, i.e. HUD text build and the
     * VDP2 shadow-register queue. */
    volatile uint32_t vdp2_commit_ticks_last;
    volatile uint32_t vdp2_commit_ticks_accum;
    /* The real VDP1 draw-end fence: vdp1_sync_wait() in
     * sourceboot_frame_poll_transfers.  SYNC_FLAG_VDP1_SYNC is cleared only
     * in _vdp1_mode_variable_vblank_out, which requires the VBlank-IN
     * handler to have seen EDSR.CEF, so this wait ends at draw-end plus the
     * frame-buffer change. */
    volatile uint32_t vdp1_fence_events;      /* guard reached */
    volatile uint32_t vdp1_fence_waits;       /* guard actually blocked */
    volatile uint32_t vdp1_fence_ticks_last;
    volatile uint32_t vdp1_fence_ticks_accum;
    volatile uint32_t vdp1_fence_ticks_max;
    volatile uint32_t vdp1_fence_iterations_last;
    volatile uint32_t vdp1_fence_iterations_accum;
    volatile uint32_t vdp1_fence_max_raw;
    /* VDP1 status at the fence.  EDSR bit 1 is CEF (draw end). */
    volatile uint32_t vdp1_edsr_entry_last;
    volatile uint32_t vdp1_edsr_cef_entry_count;
    volatile uint32_t vdp1_copr_entry_last;
    volatile uint32_t vdp1_copr_exit_last;
    volatile uint32_t vdp1_lopr_last;
    /* Per-VBlank VDP1 progress, sampled in the VBlank-OUT handler.  COPR is
     * the current command-table address in 8-byte units, so COPR/4 is the
     * command index VDP1 is plotting -- libyaul derives the same value as
     * copr >> 2 in vdp1/cmdt.h vdp1_cmdt_current_get().  vdp1_vblank_cef_count over
     * vdp1_vblank_samples is the fraction of the run's VBlanks at which
     * VDP1 had already finished -- the single number that decides
     * fill-bound versus CPU-bound. */
    volatile uint32_t vdp1_vblank_samples;
    volatile uint32_t vdp1_vblank_cef_count;
    volatile uint32_t vdp1_copr_retired_accum;
    volatile uint32_t vdp1_copr_retired_intervals;
    volatile uint32_t vdp1_copr_retired_max;
    volatile uint32_t vdp1_copr_vblank_ring[
        SM64_SATURN_PRENOTIFY_PROFILE_COPR_RING];
    volatile uint32_t vdp1_copr_vblank_ring_cursor;
    /* Command-source split.  total is the published bank's command_count
     * (per present, and summed); actor and texture are the demo renderer's
     * own run-long counters, which no instrument published before. */
    volatile uint32_t commands_total_last;
    volatile uint32_t commands_total_accum;
    volatile uint32_t commands_actor_accum;
    volatile uint32_t commands_texture_accum;
    /* ---------------------------------------------------------------- *
     * T2.9 spatial admission decomposition.
     *
     * WHY FLAT ACCUMULATORS AND NOT NODE-TREE CHILDREN.  The hot loop
     * inside sm64_saturn_scene_admit_with_scratch() runs 867 iterations
     * per frame (SM64_SATURN_BOB_ADMISSION_CLUSTER_REF_COUNT).  A
     * push/pop pair charges through the uncached working state and costs
     * ~100 SH-2 cycles; three of them per iteration would have added
     * ~260k cycles to a ~2.0M-cycle stage -- 13% perturbation at exactly
     * the granularity the answer lives at, which is the T2.5 per-vertex
     * mistake repeated.  A flat span is two on-chip FRT byte reads plus a
     * register add: ~14 cycles.  The node table is therefore UNCHANGED at
     * 24 entries and ids 0-23 keep their T2.4/T2.5/T2.6/T2.7 meaning, so
     * every earlier ranked table stays directly comparable; these fields
     * sub-divide the SPATIAL_ADMIT node's own total without touching it.
     *
     * CHAINING.  One uint16_t cursor threads the whole call: each span
     * charges "time since the previous probe" to the bucket it names, the
     * same discipline charge() uses for the node tree.  Loop paths that
     * `continue` before reaching a probe therefore have their tail
     * charged to the NEXT probe's bucket -- which is cluster_test.  That
     * over-charges cluster_test by the loop overhead and is stated in the
     * report rather than hidden.
     *
     * WRAP.  Every span is one probe gap inside a stage whose whole
     * measured total is ~16,000 ticks, so a 16-bit difference cannot
     * alias; admit_max_raw publishes the largest gap as the witness.
     * ---------------------------------------------------------------- */
    volatile uint32_t admit_windows;      /* completed admission calls */
    volatile uint32_t admit_fallback_frames; /* generic path returned false */
    volatile uint32_t admit_malformed_frames;
    volatile uint32_t admit_view_setup_ticks_accum;
    volatile uint32_t admit_validate_ticks_accum;
    volatile uint32_t admit_validate_ticks_last;
    volatile uint32_t admit_validate_ticks_max;
    volatile uint32_t admit_scratch_ticks_accum;
    volatile uint32_t admit_frustum_ticks_accum;
    volatile uint32_t admit_node_test_ticks_accum;
    volatile uint32_t admit_cluster_test_ticks_accum;
    volatile uint32_t admit_cluster_test_ticks_last;
    volatile uint32_t admit_cluster_test_ticks_max;
    volatile uint32_t admit_cluster_dedup_ticks_accum;
    volatile uint32_t admit_cluster_dedup_ticks_last;
    volatile uint32_t admit_cluster_dedup_ticks_max;
    volatile uint32_t admit_cluster_emit_ticks_accum;
    volatile uint32_t admit_portal_ticks_accum;
    volatile uint32_t admit_mandatory_ticks_accum;
    volatile uint32_t admit_total_ticks_accum;
    volatile uint32_t admit_total_ticks_last;
    volatile uint32_t admit_total_ticks_max;
    volatile uint32_t admit_max_raw;
    /* Counts.  These answer the owner's question directly: if the scene
     * is 867 clusters and 867 are tested every frame regardless of what
     * is visible, the cost is structural rather than per-test. */
    volatile uint32_t admit_nodes_tested_last;
    volatile uint32_t admit_nodes_admitted_last;
    volatile uint32_t admit_clusters_tested_last;
    volatile uint32_t admit_clusters_admitted_last;
    volatile uint32_t admit_clusters_rejected_last;
    volatile uint32_t admit_clusters_inside_last;
    volatile uint32_t admit_clusters_intersect_last;
    volatile uint32_t admit_clusters_duplicate_last;
    volatile uint32_t admit_portals_tested_last;
    volatile uint32_t admit_output_count_last;
    /* output_has_cluster() is a linear scan of the output list.  calls is
     * how many times it ran; compares is how many uint16 slots it read in
     * total.  compares/calls is the mean scan length, and the ratio of
     * compares to clusters_admitted is the O(n^2) witness. */
    volatile uint32_t admit_dedup_calls_last;
    volatile uint32_t admit_dedup_compares_last;
    volatile uint32_t admit_dedup_compares_accum;
    volatile uint32_t sequence_end;
} sm64_saturn_prenotify_profile_t;

_Static_assert(sizeof(sm64_saturn_prenotify_profile_t) == 860U,
               "frame profile ABI must remain two hundred fifteen words");

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
    uint32_t end_depth_max;
    uint16_t node_calls[SM64_SATURN_PRENOTIFY_PROFILE_NODES];
    uint16_t last16;
    uint8_t stack[SM64_SATURN_PRENOTIFY_PROFILE_DEPTH];
    uint8_t depth;
    uint8_t active;
} sm64_saturn_prenotify_profile_state_t;

/* Both defined in src/port/saturn/sourceboot/main.c, diagnostic builds
 * only.  T2.5: the state is master-owned __uncached HWRAM (the shipped
 * cadence rig's discipline for sourceboot_render_overlap_phase) rather
 * than cached .bss; the record is NOLOAD LWRAM reached through P2. */
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
    for (uint32_t node = 0U; node < SM64_SATURN_PRENOTIFY_PROFILE_NODES; node++) {
        state->node_ticks[node] = 0U;
        state->node_calls[node] = 0U;
    }
    state->node_calls[SM64_SATURN_PRENOTIFY_PROFILE_NODE_WINDOW] = 1U;
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
    if (state->node_calls[node] != UINT16_MAX) state->node_calls[node]++;
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
    /* The NOTIFY node is deliberately left pushed so that this final charge
     * lands on it: end() is reached from inside the notify call, so there is
     * no instant at which it could have been popped first.  Depth 1 is
     * therefore the design, not a fault -- T2.4 counted it as one and made
     * its own acceptance gate meaningless (faults == windows).  Only a real
     * imbalance (depth > 1) is a fault now. */
    if ((uint32_t)state->depth > state->end_depth_max)
        state->end_depth_max = state->depth;
    if (state->depth > 1U) state->faults++;
    state->depth = 0U;
    state->active = 0U;

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
    record->end_depth_max = state->end_depth_max;
    for (uint32_t node = 0U; node < SM64_SATURN_PRENOTIFY_PROFILE_NODES; node++) {
        const uint32_t ticks = state->node_ticks[node];
        record->node_ticks_last[node] = ticks;
        record->node_ticks_accum[node] += ticks;
        if (ticks > record->node_ticks_max[node])
            record->node_ticks_max[node] = ticks;
        record->node_calls_last[node] = state->node_calls[node];
    }
    record->sequence_begin = sequence;
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

/* ------------------------------------------------------------------ *
 * T2.9 flat sub-spans.
 *
 * These do NOT touch the node stack.  They exist so that a stage which is
 * a single node-tree entry (SPATIAL_ADMIT) can be decomposed at loop
 * granularity without paying charge()'s uncached bookkeeping 2,600 times
 * a frame.  The caller keeps the cursor and the accumulators in locals,
 * so the compiler keeps them in registers and one span costs two on-chip
 * FRT byte reads plus a subtract and an add.
 * ------------------------------------------------------------------ */
typedef struct {
    uint32_t validate_ticks;
    uint32_t scratch_ticks;
    uint32_t frustum_ticks;
    uint32_t node_test_ticks;
    uint32_t cluster_test_ticks;
    uint32_t cluster_dedup_ticks;
    uint32_t cluster_emit_ticks;
    uint32_t portal_ticks;
    uint32_t mandatory_ticks;
    uint32_t total_ticks;
    uint32_t max_raw;
    uint32_t dedup_calls;
    uint32_t dedup_compares;
    uint32_t nodes_tested;
    uint32_t nodes_admitted;
    uint32_t clusters_tested;
    uint32_t clusters_admitted;
    uint32_t clusters_rejected;
    uint32_t clusters_inside;
    uint32_t clusters_intersect;
    uint32_t clusters_duplicate;
    uint32_t portals_tested;
    uint32_t output_count;
    uint32_t malformed;
} sm64_saturn_prenotify_admit_t;

static inline uint16_t sm64_saturn_prenotify_profile_span(
    uint16_t start, uint32_t *accum, uint32_t *max_raw)
{
    const uint16_t now = sm64_saturn_prenotify_profile_frt();
    const uint16_t raw = (uint16_t)(now - start);
    *accum += raw;
    if ((uint32_t)raw > *max_raw) *max_raw = raw;
    return now;
}

static inline void sm64_saturn_prenotify_profile_publish_admit(
    const sm64_saturn_prenotify_admit_t *admit)
{
    volatile sm64_saturn_prenotify_profile_t *const record =
        sm64_saturn_prenotify_profile_visible();
    record->admit_windows++;
    record->admit_validate_ticks_accum += admit->validate_ticks;
    record->admit_validate_ticks_last = admit->validate_ticks;
    if (admit->validate_ticks > record->admit_validate_ticks_max)
        record->admit_validate_ticks_max = admit->validate_ticks;
    record->admit_scratch_ticks_accum += admit->scratch_ticks;
    record->admit_frustum_ticks_accum += admit->frustum_ticks;
    record->admit_node_test_ticks_accum += admit->node_test_ticks;
    record->admit_cluster_test_ticks_accum += admit->cluster_test_ticks;
    record->admit_cluster_test_ticks_last = admit->cluster_test_ticks;
    if (admit->cluster_test_ticks > record->admit_cluster_test_ticks_max)
        record->admit_cluster_test_ticks_max = admit->cluster_test_ticks;
    record->admit_cluster_dedup_ticks_accum += admit->cluster_dedup_ticks;
    record->admit_cluster_dedup_ticks_last = admit->cluster_dedup_ticks;
    if (admit->cluster_dedup_ticks > record->admit_cluster_dedup_ticks_max)
        record->admit_cluster_dedup_ticks_max = admit->cluster_dedup_ticks;
    record->admit_cluster_emit_ticks_accum += admit->cluster_emit_ticks;
    record->admit_portal_ticks_accum += admit->portal_ticks;
    record->admit_mandatory_ticks_accum += admit->mandatory_ticks;
    record->admit_total_ticks_accum += admit->total_ticks;
    record->admit_total_ticks_last = admit->total_ticks;
    if (admit->total_ticks > record->admit_total_ticks_max)
        record->admit_total_ticks_max = admit->total_ticks;
    if (admit->max_raw > record->admit_max_raw)
        record->admit_max_raw = admit->max_raw;
    record->admit_nodes_tested_last = admit->nodes_tested;
    record->admit_nodes_admitted_last = admit->nodes_admitted;
    record->admit_clusters_tested_last = admit->clusters_tested;
    record->admit_clusters_admitted_last = admit->clusters_admitted;
    record->admit_clusters_rejected_last = admit->clusters_rejected;
    record->admit_clusters_inside_last = admit->clusters_inside;
    record->admit_clusters_intersect_last = admit->clusters_intersect;
    record->admit_clusters_duplicate_last = admit->clusters_duplicate;
    record->admit_portals_tested_last = admit->portals_tested;
    record->admit_output_count_last = admit->output_count;
    record->admit_dedup_calls_last = admit->dedup_calls;
    record->admit_dedup_compares_last = admit->dedup_compares;
    record->admit_dedup_compares_accum += admit->dedup_compares;
    if (admit->malformed != 0U) record->admit_malformed_frames++;
}

/* The caller-side half: demo_spatial_admit() owns the render_view/scene
 * assembly that happens before the admission call, and it alone can see
 * whether the generic path declined and dropped through to BOB's legacy
 * recursive painter. */
static inline void sm64_saturn_prenotify_profile_publish_admit_view(
    uint32_t setup_ticks, uint32_t fallback)
{
    volatile sm64_saturn_prenotify_profile_t *const record =
        sm64_saturn_prenotify_profile_visible();
    record->admit_view_setup_ticks_accum += setup_ticks;
    if (fallback != 0U) record->admit_fallback_frames++;
}

#define SM64_SATURN_PRENOTIFY_PROFILE_BEGIN() sm64_saturn_prenotify_profile_begin()
#define SM64_SATURN_PRENOTIFY_PROFILE_PUSH(node) \
    sm64_saturn_prenotify_profile_push((uint32_t)(node))
#define SM64_SATURN_PRENOTIFY_PROFILE_POP() sm64_saturn_prenotify_profile_pop()
#define SM64_SATURN_PRENOTIFY_PROFILE_END() sm64_saturn_prenotify_profile_end()
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
#define SM64_SATURN_PRENOTIFY_PROFILE_SLAVE_SCOPE_BEGIN(name) ((void)0)
#define SM64_SATURN_PRENOTIFY_PROFILE_SLAVE_SCOPE_END(name) ((void)0)

#endif /* SATURN_DIAGNOSTIC_MODE != 0 && __sh__ */

#endif /* SM64_SATURN_PRENOTIFY_PROFILE_H */
