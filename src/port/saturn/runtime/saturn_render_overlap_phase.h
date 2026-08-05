#ifndef SM64_SATURN_RENDER_OVERLAP_PHASE_H
#define SM64_SATURN_RENDER_OVERLAP_PHASE_H

#include <stdbool.h>
#include <stdint.h>

/* Master-owned exact-generation retention and VBlank phase accounting. The
 * controller contains no scene or renderer dependency; opaque identities are
 * compared only and are never dereferenced. */
typedef struct sm64_saturn_render_overlap_phase {
    uint32_t active_generation;
    const void *snapshot_identity;
    const void *build_bank_identity;
    uint32_t construction_begin_vblank;
    uint32_t notification_vblank;
    uint32_t retirement_vblank;
    uint32_t construction_vblank_crossings;
    uint32_t construction_count;
    uint32_t slave_work_vblank_crossings;
    uint32_t slave_work_count;
    uint32_t master_finalize_vblank_crossings;
    uint32_t master_finalize_count;
    bool active;
    bool bound;
    bool notified;
    bool retired;
} sm64_saturn_render_overlap_phase_t;

void sm64_saturn_render_overlap_phase_init(
    sm64_saturn_render_overlap_phase_t *phase);
bool sm64_saturn_render_overlap_phase_begin(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    uint32_t construction_begin_vblank);
bool sm64_saturn_render_overlap_phase_bind(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    const void *snapshot_identity, const void *build_bank_identity);
bool sm64_saturn_render_overlap_phase_retains(
    const sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    const void *snapshot_identity, const void *build_bank_identity);
bool sm64_saturn_render_overlap_phase_notification_published(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    uint32_t notification_vblank);
bool sm64_saturn_render_overlap_phase_retirement_published(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    uint32_t retirement_vblank);
bool sm64_saturn_render_overlap_phase_terminal(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    const void *snapshot_identity, const void *build_bank_identity,
    uint32_t terminal_vblank);
bool sm64_saturn_render_overlap_phase_abort(
    sm64_saturn_render_overlap_phase_t *phase, uint32_t generation,
    uint32_t terminal_vblank);

#endif
