/* Immutable master-to-render snapshot publication for the Saturn path. */
#ifndef SM64_SATURN_RENDER_SNAPSHOT_H
#define SM64_SATURN_RENDER_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_actor_bridge.h"

#if defined(__sh__)
#include <cpu/cache.h>
#endif

/* This project-owned state machine uses only the bank handoff pattern studied
 * in SlaveDriver Engine (a8986591557b6e680550d3c23970284d3b38ff8f,
 * GPL-3.0-or-later) and Sonic Z-Treme (cff75451c1616aac1236fc2b44223902b55c706b,
 * GPL-3.0). No upstream implementation was copied. */

enum sm64_saturn_render_snapshot_state {
    SM64_SATURN_RENDER_SNAPSHOT_FREE = 0U,
    SM64_SATURN_RENDER_SNAPSHOT_WRITING = 1U,
    SM64_SATURN_RENDER_SNAPSHOT_READY = 2U,
    SM64_SATURN_RENDER_SNAPSHOT_RENDERING = 3U,
    SM64_SATURN_RENDER_SNAPSHOT_COMPLETE = 4U,
    SM64_SATURN_RENDER_SNAPSHOT_QUARANTINED = 5U,
};

/* Render-view forward is a unit Q16.16 camera vector. Pre-transform admission
 * projects conservative cluster bounds onto this immutable view-space Z axis. */
typedef struct sm64_saturn_render_view {
    int32_t view_projection_q16[4][4];
    int32_t camera_position_q16[3];
    int32_t camera_focus_q16[3];
    int32_t view_forward_q16[3];
    uint32_t generation;
} sm64_saturn_render_view_t;

/* The source bank deliberately contains scalar copies and generated-bank IDs
 * only. It contains no live SM64, graph-node, VDP1, or VRAM pointer. */
typedef struct sm64_saturn_render_snapshot {
    sm64_saturn_render_view_t camera;
    sm64_saturn_mario_actor_snapshot_t mario;
    sm64_saturn_mario_pose_selector_t mario_pose;
    uint32_t generation;
    uint32_t actor_generation;
    uint32_t scene_id;
    uint32_t area_id;
    uint32_t geometry_bank_id;
    uint32_t material_bank_id;
} sm64_saturn_render_snapshot_t;

/* Keep the small release record distinct from the bulk snapshot. Target code
 * locates this record through its cache-through alias before a peer reads the
 * snapshot payload. */
typedef struct sm64_saturn_render_snapshot_release {
    volatile uint32_t generation;
    volatile uint32_t state;
    volatile uint8_t claim_lock;
    uint8_t reserved[3];
} sm64_saturn_render_snapshot_release_t;

typedef struct sm64_saturn_render_snapshot_slot {
    sm64_saturn_render_snapshot_t snapshot;
    sm64_saturn_render_snapshot_release_t release;
} sm64_saturn_render_snapshot_slot_t;

typedef struct sm64_saturn_render_snapshot_bank {
    sm64_saturn_render_snapshot_slot_t slot[2];
} sm64_saturn_render_snapshot_bank_t;

/* Release state and both producer/peer payload accesses use P2 cache-through
 * aliases on SH-2; the host identity branch preserves the protocol for the
 * lifecycle fixture. */
static inline const void *sm64_saturn_render_snapshot_cache_through(
    const void *cached)
{
    if (cached == NULL) return NULL;
#if defined(__sh__)
    return (const void *)(CPU_CACHE_THROUGH | (uintptr_t)cached);
#else
    return cached;
#endif
}

static inline volatile sm64_saturn_render_snapshot_release_t *
sm64_saturn_render_snapshot_release_uncached(
    sm64_saturn_render_snapshot_slot_t *slot)
{
    if (slot == NULL) return NULL;
    return (volatile sm64_saturn_render_snapshot_release_t *)
        sm64_saturn_render_snapshot_cache_through(&slot->release);
}

/* SH-2 TAS.B performs a bus-atomic zero-to-set transition on the uncached
 * release byte. `movt` returns the single winner; host atomics model the same
 * operation for the lifecycle fixture. */
static inline bool sm64_saturn_render_snapshot_release_claim_try(
    volatile sm64_saturn_render_snapshot_release_t *release)
{
#if defined(__sh__)
    uint32_t acquired;

    if (release == NULL) return false;
    __asm__ volatile("tas.b @%1\n\tmovt %0"
                     : "=r" (acquired)
                     : "r" (&release->claim_lock)
                     : "memory");
    return acquired != 0U;
#else
    return release != NULL &&
        __sync_lock_test_and_set(&release->claim_lock, 1U) == 0U;
#endif
}

static inline void sm64_saturn_render_snapshot_release_claim_release(
    volatile sm64_saturn_render_snapshot_release_t *release)
{
    if (release == NULL) return;
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
    release->claim_lock = 0U;
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

/* Producers and peer consumers both use P2 for bulk payload access. The
 * producer never dirties a P1 cache line that the release store could outrun. */
static inline sm64_saturn_render_snapshot_t *
sm64_saturn_render_snapshot_owner_payload(
    sm64_saturn_render_snapshot_slot_t *slot)
{
    if (slot == NULL) return NULL;
    return (sm64_saturn_render_snapshot_t *)
        sm64_saturn_render_snapshot_cache_through(&slot->snapshot);
}

static inline const sm64_saturn_render_snapshot_t *
sm64_saturn_render_snapshot_peer_payload(
    const sm64_saturn_render_snapshot_slot_t *slot)
{
    return (const sm64_saturn_render_snapshot_t *)
        sm64_saturn_render_snapshot_owner_payload(
            (sm64_saturn_render_snapshot_slot_t *)slot);
}

_Static_assert(sizeof(sm64_saturn_render_view_t) == 104U,
               "render-view ABI must remain fixed-width");
_Static_assert(sizeof(sm64_saturn_render_snapshot_release_t) == 12U,
               "snapshot release record must remain fixed-width");

/* Initialization hygiene for already-FREE slots only. It never releases a
 * WRITING, READY, RENDERING, COMPLETE, or QUARANTINED generation. */
void sm64_saturn_render_snapshot_reset(sm64_saturn_render_snapshot_bank_t *bank);
bool sm64_saturn_render_snapshot_generation_valid(
    const sm64_saturn_render_snapshot_t *snapshot, uint32_t generation);
bool sm64_saturn_render_snapshot_begin_write(
    sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation,
    sm64_saturn_render_snapshot_t **out);
bool sm64_saturn_render_snapshot_publish(
    sm64_saturn_render_snapshot_bank_t *bank,
    sm64_saturn_render_snapshot_t *snapshot);
const sm64_saturn_render_snapshot_t *
sm64_saturn_render_snapshot_acquire_ready(
    sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation);
bool sm64_saturn_render_snapshot_complete(
    sm64_saturn_render_snapshot_bank_t *bank,
    const sm64_saturn_render_snapshot_t *snapshot);
bool sm64_saturn_render_snapshot_retire(
    sm64_saturn_render_snapshot_bank_t *bank,
    const sm64_saturn_render_snapshot_t *snapshot);
bool sm64_saturn_render_snapshot_quarantine(
    sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation);

#endif
