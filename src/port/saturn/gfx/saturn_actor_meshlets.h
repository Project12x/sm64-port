/* Bounded generated-actor admission and painter-order preparation. */
#ifndef SM64_SATURN_ACTOR_MESHLETS_H
#define SM64_SATURN_ACTOR_MESHLETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "saturn_actor_instance.h"
#include "saturn_actor_output.h"
#include "saturn_actor_pose.h"
#include "saturn_fast3d_frontend.h"
#include "saturn_render_snapshot.h"

typedef sm64_saturn_actor_quarantine_reason_t
    sm64_saturn_actor_meshlet_quarantine_reason_t;
typedef sm64_saturn_actor_output_record_t sm64_saturn_actor_draw_ref_t;

#define SM64_SATURN_ACTOR_MESHLET_QUARANTINE_NONE \
    SM64_SATURN_ACTOR_QUARANTINE_NONE
#define SM64_SATURN_ACTOR_MESHLET_QUARANTINE_CLAIMANT_FAILURE \
    SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE
#define SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_GENERATION \
    SM64_SATURN_ACTOR_QUARANTINE_STALE_GENERATION
#define SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_BANK \
    SM64_SATURN_ACTOR_QUARANTINE_STALE_BANK
#define SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW \
    SM64_SATURN_ACTOR_QUARANTINE_OUTPUT_OVERFLOW

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
#define SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT \
    SM64_SATURN_ACTOR_BANK_WORK_LANE_COUNT
#define SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT \
    SM64_SATURN_ACTOR_BANK_WORK_ALIGNMENT

/* Type-safe extension for prepare_bank. Keeping the legacy output as the
 * unchanged first member lets the shared core publish the familiar result
 * view without changing any feature-off field offset or sizeof contract. */
typedef struct sm64_saturn_actor_meshlet_bank_output {
    sm64_saturn_actor_meshlet_output_t output;
    sm64_saturn_actor_output_record_t *records;
    uint8_t *position_seen;
    uint32_t abi;
    uint16_t draw_capacity;
    uint16_t position_seen_capacity;
    uint8_t quarantine_reason;
    uint8_t reserved;
} sm64_saturn_actor_meshlet_bank_output_t;

typedef struct sm64_saturn_actor_meshlet_workspace {
    sm64_saturn_actor_pose_work_t pose_work;
    sm64_saturn_actor_meshlet_bank_output_t output;
    uint32_t scratch_offset;
    uint32_t scratch_size;
    uint8_t lane;
    uint8_t reserved[3];
} sm64_saturn_actor_meshlet_workspace_t;

/* S64B maximum_scratch owns worst-case leading alignment headroom plus both
 * fixed claimant lanes. The raw span begins immediately after the package-
 * resident dependency payload and need not already be aligned; it is not the
 * fixed actor queue/output arena. Query returns per-lane usable bytes, both-
 * lane usable bytes, and the complete advertised reservation respectively. */
bool sm64_saturn_actor_meshlets_workspace_query(
    const sm64_saturn_actor_bank_view_t *bank, uint32_t *lane_bytes,
    uint32_t *usable_bytes, uint32_t *reserved_bytes);
bool sm64_saturn_actor_meshlets_bind_workspace(
    const sm64_saturn_actor_bank_view_t *bank, void *scratch,
    uint32_t scratch_capacity, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_meshlet_workspace_t *workspace);
/* Bind one bank inside the bundle-wide fixed claimant lanes. lane_stride is
 * the validated S64F maximum across every embedded bank; the caller owns one
 * raw two-lane span including worst-case leading alignment headroom. */
bool sm64_saturn_actor_meshlets_bind_bundle_workspace(
    const sm64_saturn_actor_bank_view_t *bank, void *scratch,
    uint32_t scratch_capacity, uint32_t lane_stride, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_meshlet_workspace_t *workspace);

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
