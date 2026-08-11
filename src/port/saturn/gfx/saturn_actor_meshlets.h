/* Bounded generated-actor admission and painter-order preparation. */
#ifndef SM64_SATURN_ACTOR_MESHLETS_H
#define SM64_SATURN_ACTOR_MESHLETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "saturn_actor_instance.h"
#include "saturn_actor_pose.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_render_snapshot.h"

typedef enum sm64_saturn_actor_meshlet_quarantine_reason {
    SM64_SATURN_ACTOR_MESHLET_QUARANTINE_NONE = 0U,
    SM64_SATURN_ACTOR_MESHLET_QUARANTINE_CLAIMANT_FAILURE = 1U,
    SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_GENERATION = 2U,
    SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_BANK = 5U,
    SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW = 6U,
} sm64_saturn_actor_meshlet_quarantine_reason_t;

typedef struct sm64_saturn_actor_draw_ref {
    uint16_t meshlet_id;
    uint16_t primitive_id;
    uint32_t sort_key;
} sm64_saturn_actor_draw_ref_t;

_Static_assert(sizeof(sm64_saturn_actor_draw_ref_t) == 8U,
               "actor meshlet draw-ref ABI changed");

typedef struct sm64_saturn_actor_meshlet_output {
    sm64_saturn_actor_draw_ref_t *opaque;
    sm64_saturn_actor_draw_ref_t *translucent;
    uint16_t *positions;
    uint16_t position_capacity;
    uint16_t opaque_count;
    uint16_t translucent_count;
    uint16_t position_count;
} sm64_saturn_actor_meshlet_output_t;

_Static_assert(offsetof(sm64_saturn_actor_meshlet_output_t, opaque) == 0U,
               "legacy actor meshlet opaque offset changed");
_Static_assert(offsetof(sm64_saturn_actor_meshlet_output_t, translucent) ==
                   sizeof(void *),
               "legacy actor meshlet translucent offset changed");
_Static_assert(offsetof(sm64_saturn_actor_meshlet_output_t, positions) ==
                   2U * sizeof(void *),
               "legacy actor meshlet position pointer offset changed");
_Static_assert(offsetof(sm64_saturn_actor_meshlet_output_t,
                        position_capacity) == 3U * sizeof(void *),
               "legacy actor meshlet capacity offset changed");
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(sm64_saturn_actor_meshlet_output_t) == 20U,
               "32-bit legacy actor meshlet output ABI changed");
#elif UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(sm64_saturn_actor_meshlet_output_t) == 32U,
               "64-bit legacy actor meshlet output ABI changed");
#endif

#define SM64_SATURN_ACTOR_MESHLET_BANK_OUTPUT_ABI 0x4D4F4231UL

/* Type-safe extension for prepare_bank. Keeping the legacy output as the
 * unchanged first member lets the shared core publish the familiar result
 * view without changing any feature-off field offset or sizeof contract. */
typedef struct sm64_saturn_actor_meshlet_bank_output {
    sm64_saturn_actor_meshlet_output_t output;
    sm64_saturn_actor_draw_ref_t *records;
    uint8_t *position_seen;
    uint32_t abi;
    uint16_t draw_capacity;
    uint16_t position_seen_capacity;
    uint8_t quarantine_reason;
    uint8_t reserved;
} sm64_saturn_actor_meshlet_bank_output_t;

/* Compatibility entry point. It shares the bank-neutral admission core while
 * retaining the exact Mario snapshot/pose/capacity contract. */
bool sm64_saturn_actor_meshlets_prepare(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats);

/* Prepare one immutable bank-bound instance. `bank` is the complete validated
 * S64B view: its header-only `bank` member cannot expose pose/geometry spans.
 * The caller owns pose_work, the explicit uniqueness scratch, and every output
 * span. Package-generation freshness is
 * enforced by the queue/handoff, which owns that generation contract. */
bool sm64_saturn_actor_meshlets_prepare_bank(
    const sm64_saturn_actor_bank_view_t *bank,
    const sm64_saturn_actor_instance_snapshot_t *instance,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_pose_work_t *pose_work,
    sm64_saturn_actor_meshlet_bank_output_t *output,
    sm64_saturn_fast3d_profile_t *stats);

#endif
