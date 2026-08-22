/* Pose-slot pool: variable-extent, generation-checked slot allocation for
 * animated actors.
 *
 * WHY THIS EXISTS
 * ---------------
 * The complete-animation path shipped a fixed global
 * `complete_actor_pose_slots[2]` -- 8,560 B of HWRAM reserved for exactly one
 * actor (Mario) at exactly two in-flight render generations.  A second
 * animated actor cannot be added to that shape, and HWRAM has ~2 KB of margin,
 * so N Mario-sized slots is not available either.
 *
 * WHAT A POSE SLOT IS
 * -------------------
 * A slot holds the *published* pose of one actor for one in-flight render
 * generation: the bytes a render consumer reads while the producer is already
 * evaluating the next generation.  Scratch that the consumer never reads (the
 * joint matrix workspace) is deliberately NOT per-slot -- see the bridge.
 *
 * WHO NEEDS ONE
 * -------------
 * Only actors whose drawable geometry changes per frame.  Of SM64 130 actor
 * directories, 74 contain no `GEO_ANIMATED_PART` at all (coin, tree, star,
 * metal_box, thwomp, warp_pipe, mushroom_1up, koopa_shell, springboard ...);
 * those need a transform, not a pose slot, and must never claim from here.
 *
 * SHAPE OF THE ALLOCATOR
 * ----------------------
 * A caller-supplied byte arena is statically partitioned at init into a small
 * number of fixed-size block classes.  Allocation is an O(1) free-list pop
 * from the smallest class that fits, so there is no fragmentation, no
 * coalescing, no search, and the worst case is bounded at init time.  The
 * arena is supplied by the caller, which is how the hot/cold residency split
 * is expressed: the caller passes an HWRAM array for a latency-critical
 * tenant and an LWRAM array for everyone else.  The allocator itself has no
 * opinion about residency.
 *
 * HANDLES
 * -------
 * A handle is (index, generation).  `release` bumps the slot generation, so a
 * stale handle resolves to NULL rather than to whatever actor now owns the
 * block.  This is the single defence against the failure this whole design
 * exists to prevent: two actors silently reading each other's vertex data.
 */
#ifndef SM64_SATURN_ACTOR_POSE_POOL_H
#define SM64_SATURN_ACTOR_POSE_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SM64_SATURN_POSE_POOL_MAX_SLOTS 24U
#define SM64_SATURN_POSE_POOL_MAX_CLASSES 4U
#define SM64_SATURN_POSE_POOL_ALIGNMENT 4U

/* Owner key 0 is reserved: it means "no owner", so `find` cannot match a free
 * slot and a caller cannot accidentally claim under the null key. */
#define SM64_SATURN_POSE_POOL_OWNER_NONE 0UL

typedef struct sm64_saturn_pose_handle {
    uint16_t index;      /* slot index + 1; 0 is the invalid handle */
    uint16_t generation; /* must equal the slot generation to resolve */
} sm64_saturn_pose_handle_t;

/* One entry of the init-time partition plan: `count` blocks of `bytes` each. */
typedef struct sm64_saturn_pose_pool_class_plan {
    uint16_t bytes;
    uint16_t count;
} sm64_saturn_pose_pool_class_plan_t;

typedef struct sm64_saturn_pose_pool_slot {
    uint32_t owner_key;
    uint32_t offset;      /* byte offset of the block inside the arena */
    uint16_t block_bytes; /* size of the backing block */
    uint16_t extent;      /* bytes the owner asked for (<= block_bytes) */
    uint16_t generation;  /* odd == live, even == free; bumped on release */
    uint8_t class_index;
    uint8_t in_use;
    int16_t next_free;    /* free-list link within the class, -1 terminates */
    int16_t reserved;
} sm64_saturn_pose_pool_slot_t;

typedef struct sm64_saturn_pose_pool {
    uint8_t *arena;
    uint32_t arena_bytes;
    uint32_t arena_used;
    sm64_saturn_pose_pool_slot_t slot[SM64_SATURN_POSE_POOL_MAX_SLOTS];
    int16_t class_free_head[SM64_SATURN_POSE_POOL_MAX_CLASSES];
    uint16_t class_bytes[SM64_SATURN_POSE_POOL_MAX_CLASSES];
    uint16_t class_count;
    uint16_t slot_count;
    uint16_t live_count;
    uint16_t peak_live_count;
    uint16_t claim_failures; /* exhaustion + oversize, for the degradation record */
    uint16_t stale_resolves; /* handles rejected because the slot moved on */
    uint8_t ready;
    uint8_t reserved[3];
} sm64_saturn_pose_pool_t;

static inline sm64_saturn_pose_handle_t sm64_saturn_pose_handle_invalid(void)
{
    sm64_saturn_pose_handle_t handle = { 0U, 0U };
    return handle;
}

static inline bool sm64_saturn_pose_handle_is_valid(
    sm64_saturn_pose_handle_t handle)
{
    return handle.index != 0U;
}

/* Partition `arena` according to `plan` (which must be ascending by `bytes`).
 * Fails closed -- returns false and leaves the pool unusable -- if the plan
 * does not fit, needs more slots than the table holds, or is unsorted. */
bool sm64_saturn_pose_pool_init(
    sm64_saturn_pose_pool_t *pool, void *arena, uint32_t arena_bytes,
    const sm64_saturn_pose_pool_class_plan_t *plan, uint16_t plan_count);

/* Claim the smallest free block that holds `extent_bytes`, escalating to
 * larger classes when the exact class is empty.  Returns the invalid handle
 * on exhaustion; never disturbs a live slot. */
sm64_saturn_pose_handle_t sm64_saturn_pose_pool_claim(
    sm64_saturn_pose_pool_t *pool, uint32_t owner_key, uint32_t extent_bytes);

/* Return a block to its class and invalidate every outstanding handle to it. */
bool sm64_saturn_pose_pool_release(
    sm64_saturn_pose_pool_t *pool, sm64_saturn_pose_handle_t handle);

/* Resolve to the block bytes, or NULL when the handle is stale/invalid. */
void *sm64_saturn_pose_pool_resolve(
    sm64_saturn_pose_pool_t *pool, sm64_saturn_pose_handle_t handle);
const void *sm64_saturn_pose_pool_resolve_const(
    const sm64_saturn_pose_pool_t *pool, sm64_saturn_pose_handle_t handle);

/* First live slot held by `owner_key`, so a producer can re-find its slot
 * without carrying a handle through an immutable snapshot. */
sm64_saturn_pose_handle_t sm64_saturn_pose_pool_find(
    const sm64_saturn_pose_pool_t *pool, uint32_t owner_key);

/* Release every slot held by `owner_key`.  Returns how many were released.
 * This is the scene-transition / actor-despawn path. */
uint16_t sm64_saturn_pose_pool_release_owner(
    sm64_saturn_pose_pool_t *pool, uint32_t owner_key);

/* Bytes still claimable in the largest non-empty class; 0 when exhausted. */
uint32_t sm64_saturn_pose_pool_largest_free_extent(
    const sm64_saturn_pose_pool_t *pool);

#endif
