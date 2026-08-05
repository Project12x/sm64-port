#include "saturn_render_overlap_phase.h"

#include <stddef.h>

static void phase_clear_active(sm64_saturn_render_overlap_phase_t *phase)
{
    phase->active_generation = 0U;
    phase->snapshot_identity = NULL;
    phase->build_bank_identity = NULL;
    phase->active = false;
    phase->bound = false;
    phase->notified = false;
    phase->retired = false;
}

void sm64_saturn_render_overlap_phase_init(
    sm64_saturn_render_overlap_phase_t *phase)
{
    if (phase != NULL) *phase = (sm64_saturn_render_overlap_phase_t){0};
}

bool sm64_saturn_render_overlap_phase_begin(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    uint32_t construction_begin_vblank)
{
    if (phase == NULL || generation == 0U || phase->active) return false;
    phase->active_generation = generation;
    phase->construction_begin_vblank = construction_begin_vblank;
    phase->snapshot_identity = NULL;
    phase->build_bank_identity = NULL;
    phase->active = true;
    phase->bound = false;
    phase->notified = false;
    phase->retired = false;
    return true;
}

bool sm64_saturn_render_overlap_phase_bind(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    const void *snapshot_identity, const void *build_bank_identity)
{
    if (phase == NULL || !phase->active || phase->bound ||
        generation != phase->active_generation || snapshot_identity == NULL ||
        build_bank_identity == NULL)
        return false;
    phase->snapshot_identity = snapshot_identity;
    phase->build_bank_identity = build_bank_identity;
    phase->bound = true;
    return true;
}

bool sm64_saturn_render_overlap_phase_retains(
    const sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    const void *snapshot_identity, const void *build_bank_identity)
{
    return phase != NULL && phase->active && phase->bound &&
        generation == phase->active_generation &&
        snapshot_identity == phase->snapshot_identity &&
        build_bank_identity == phase->build_bank_identity;
}

bool sm64_saturn_render_overlap_phase_notification_published(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    uint32_t notification_vblank)
{
    if (phase == NULL || !phase->active || !phase->bound || phase->notified ||
        generation != phase->active_generation)
        return false;
    phase->notification_vblank = notification_vblank;
    phase->notified = true;
    return true;
}

bool sm64_saturn_render_overlap_phase_retirement_published(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    uint32_t retirement_vblank)
{
    if (phase == NULL || !phase->active || !phase->notified || phase->retired ||
        generation != phase->active_generation)
        return false;
    phase->retirement_vblank = retirement_vblank;
    phase->retired = true;
    return true;
}

bool sm64_saturn_render_overlap_phase_terminal(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    const void *snapshot_identity, const void *build_bank_identity,
    uint32_t terminal_vblank)
{
    if (!sm64_saturn_render_overlap_phase_retains(
            phase, generation, snapshot_identity, build_bank_identity))
        return false;
    if (phase->notified && !phase->retired) return false;
    uint32_t construction = terminal_vblank -
        phase->construction_begin_vblank;
    if (phase->notified) {
        const uint32_t finalization =
            terminal_vblank - phase->retirement_vblank;
#if defined(SM64_SATURN_RENDER_OVERLAP_PHASE_TEST_OMIT_START)
        construction = finalization;
#else
        const uint32_t start_construction =
            phase->notification_vblank - phase->construction_begin_vblank;
        construction = start_construction + finalization;
#endif
        phase->slave_work_vblank_crossings +=
            phase->retirement_vblank - phase->notification_vblank;
        phase->slave_work_count++;
        phase->master_finalize_vblank_crossings += finalization;
        phase->master_finalize_count++;
    }
    phase->construction_vblank_crossings += construction;
    phase->construction_count++;
    phase_clear_active(phase);
    return true;
}

bool sm64_saturn_render_overlap_phase_abort(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    uint32_t terminal_vblank)
{
    if (phase == NULL || !phase->active || phase->notified ||
        generation != phase->active_generation)
        return false;
    phase->construction_vblank_crossings +=
        terminal_vblank - phase->construction_begin_vblank;
    phase->construction_count++;
    phase_clear_active(phase);
    return true;
}
