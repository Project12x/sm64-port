/* See saturn_actor_pose_pool.h for the design rationale. */
#include "saturn_actor_pose_pool.h"

#include <string.h>

#define POOL_FREE_LIST_END ((int16_t)-1)

static uint32_t pool_align_up(uint32_t value)
{
    const uint32_t mask = SM64_SATURN_POSE_POOL_ALIGNMENT - 1U;
    return (value + mask) & ~mask;
}

static sm64_saturn_pose_pool_slot_t *pool_slot_for_handle(
    const sm64_saturn_pose_pool_t *pool, sm64_saturn_pose_handle_t handle)
{
    uint16_t index;

    if (pool == NULL || pool->ready == 0U || handle.index == 0U)
        return NULL;
    index = (uint16_t)(handle.index - 1U);
    if (index >= pool->slot_count)
        return NULL;
    {
        sm64_saturn_pose_pool_slot_t *const slot =
            (sm64_saturn_pose_pool_slot_t *)&pool->slot[index];
        if (slot->in_use == 0U || slot->generation != handle.generation)
            return NULL;
        return slot;
    }
}

bool sm64_saturn_pose_pool_init(
    sm64_saturn_pose_pool_t *pool, void *arena, uint32_t arena_bytes,
    const sm64_saturn_pose_pool_class_plan_t *plan, uint16_t plan_count)
{
    uint16_t class_index;
    uint16_t slot_index = 0U;
    uint32_t cursor = 0U;

    if (pool == NULL)
        return false;
    memset(pool, 0, sizeof(*pool));
    for (class_index = 0U; class_index < SM64_SATURN_POSE_POOL_MAX_CLASSES;
         class_index++)
        pool->class_free_head[class_index] = POOL_FREE_LIST_END;
    if (arena == NULL || plan == NULL || plan_count == 0U ||
        plan_count > SM64_SATURN_POSE_POOL_MAX_CLASSES)
        return false;

    /* The plan must be strictly ascending: `claim` escalates to the next
     * class index when a class is empty, and that is only a widening step if
     * the classes are ordered. */
    for (class_index = 0U; class_index < plan_count; class_index++) {
        if (plan[class_index].bytes == 0U || plan[class_index].count == 0U)
            return false;
        if (class_index > 0U &&
            plan[class_index].bytes <= plan[class_index - 1U].bytes)
            return false;
        if (pool_align_up(plan[class_index].bytes) != plan[class_index].bytes)
            return false;
        slot_index = (uint16_t)(slot_index + plan[class_index].count);
        if (slot_index > SM64_SATURN_POSE_POOL_MAX_SLOTS)
            return false;
    }

    /* Lay the blocks out largest-class-last so the arena is packed in plan
     * order; every block is aligned because every class size is. */
    slot_index = 0U;
    for (class_index = 0U; class_index < plan_count; class_index++) {
        uint16_t block;
        pool->class_bytes[class_index] = plan[class_index].bytes;
        for (block = 0U; block < plan[class_index].count; block++) {
            sm64_saturn_pose_pool_slot_t *const slot = &pool->slot[slot_index];
            if (cursor + plan[class_index].bytes > arena_bytes)
                return false;
            slot->owner_key = SM64_SATURN_POSE_POOL_OWNER_NONE;
            slot->offset = cursor;
            slot->block_bytes = plan[class_index].bytes;
            slot->extent = 0U;
            slot->generation = 1U;
            slot->class_index = (uint8_t)class_index;
            slot->in_use = 0U;
            slot->next_free = pool->class_free_head[class_index];
            pool->class_free_head[class_index] = (int16_t)slot_index;
            cursor += plan[class_index].bytes;
            slot_index++;
        }
    }

    pool->arena = (uint8_t *)arena;
    pool->arena_bytes = arena_bytes;
    pool->arena_used = cursor;
    pool->class_count = plan_count;
    pool->slot_count = slot_index;
    pool->ready = 1U;
    return true;
}

sm64_saturn_pose_handle_t sm64_saturn_pose_pool_claim(
    sm64_saturn_pose_pool_t *pool, uint32_t owner_key, uint32_t extent_bytes)
{
    uint16_t class_index;

    if (pool == NULL || pool->ready == 0U)
        return sm64_saturn_pose_handle_invalid();
    if (owner_key == SM64_SATURN_POSE_POOL_OWNER_NONE || extent_bytes == 0U) {
        pool->claim_failures++;
        return sm64_saturn_pose_handle_invalid();
    }
    for (class_index = 0U; class_index < pool->class_count; class_index++) {
        int16_t head;
        if (pool->class_bytes[class_index] < extent_bytes)
            continue; /* too small for this request */
        head = pool->class_free_head[class_index];
#if defined(SM64_SATURN_POSE_POOL_MUTATE_CLAIM_REUSE)
        /* Mutation: hand out a block that is already held instead of a free
         * one.  Must be caught by the two-tenant disjointness fixture. */
        head = (int16_t)0;
#endif
        if (head == POOL_FREE_LIST_END)
            continue; /* right size, none free -- widen */
        {
            sm64_saturn_pose_pool_slot_t *const slot = &pool->slot[head];
            sm64_saturn_pose_handle_t handle;
            pool->class_free_head[class_index] = slot->next_free;
            slot->next_free = POOL_FREE_LIST_END;
            slot->owner_key = owner_key;
            slot->extent = (uint16_t)extent_bytes;
            slot->in_use = 1U;
            pool->live_count++;
            if (pool->live_count > pool->peak_live_count)
                pool->peak_live_count = pool->live_count;
            handle.index = (uint16_t)(head + 1);
            handle.generation = slot->generation;
            return handle;
        }
    }
    /* Exhaustion, or an extent larger than any class.  Degradation contract:
     * the caller keeps whatever slot it already holds and the actor renders
     * with its last published pose, or unposed if it never had one.  No live
     * slot is disturbed and nothing is evicted -- an actor that is already
     * animating never loses its animation to a newcomer. */
    pool->claim_failures++;
    return sm64_saturn_pose_handle_invalid();
}

bool sm64_saturn_pose_pool_release(
    sm64_saturn_pose_pool_t *pool, sm64_saturn_pose_handle_t handle)
{
    sm64_saturn_pose_pool_slot_t *const slot =
        pool_slot_for_handle(pool, handle);
    uint16_t class_index;

    if (slot == NULL)
        return false;
#if defined(SM64_SATURN_POSE_POOL_MUTATE_SKIP_RELEASE)
    /* Mutation: acknowledge the release without performing it.  Must be
     * caught by the stale-handle and reuse fixtures. */
    return true;
#endif
    class_index = slot->class_index;
    slot->in_use = 0U;
    slot->owner_key = SM64_SATURN_POSE_POOL_OWNER_NONE;
    slot->extent = 0U;
    slot->generation++;
    if (slot->generation == 0U)
        slot->generation = 1U; /* 0 is never a live generation */
    slot->next_free = pool->class_free_head[class_index];
    pool->class_free_head[class_index] =
        (int16_t)(handle.index - 1U);
    if (pool->live_count > 0U)
        pool->live_count--;
    return true;
}

void *sm64_saturn_pose_pool_resolve(
    sm64_saturn_pose_pool_t *pool, sm64_saturn_pose_handle_t handle)
{
    sm64_saturn_pose_pool_slot_t *const slot =
        pool_slot_for_handle(pool, handle);
    if (slot == NULL) {
        if (pool != NULL && handle.index != 0U)
            pool->stale_resolves++;
        return NULL;
    }
#if defined(SM64_SATURN_POSE_POOL_MUTATE_ALIAS)
    /* Mutation: every slot resolves to the base of the arena, so two live
     * tenants share bytes.  Must be caught by the disjointness sweep. */
    return pool->arena;
#else
    return pool->arena + slot->offset;
#endif
}

const void *sm64_saturn_pose_pool_resolve_const(
    const sm64_saturn_pose_pool_t *pool, sm64_saturn_pose_handle_t handle)
{
    const sm64_saturn_pose_pool_slot_t *const slot =
        pool_slot_for_handle(pool, handle);
    if (slot == NULL)
        return NULL;
    return pool->arena + slot->offset;
}

sm64_saturn_pose_handle_t sm64_saturn_pose_pool_find(
    const sm64_saturn_pose_pool_t *pool, uint32_t owner_key)
{
    uint16_t index;

    if (pool == NULL || pool->ready == 0U ||
        owner_key == SM64_SATURN_POSE_POOL_OWNER_NONE)
        return sm64_saturn_pose_handle_invalid();
    for (index = 0U; index < pool->slot_count; index++) {
        if (pool->slot[index].in_use != 0U &&
            pool->slot[index].owner_key == owner_key) {
            sm64_saturn_pose_handle_t handle;
            handle.index = (uint16_t)(index + 1U);
            handle.generation = pool->slot[index].generation;
            return handle;
        }
    }
    return sm64_saturn_pose_handle_invalid();
}

uint16_t sm64_saturn_pose_pool_release_owner(
    sm64_saturn_pose_pool_t *pool, uint32_t owner_key)
{
    uint16_t released = 0U;
    uint16_t index;

    if (pool == NULL || pool->ready == 0U ||
        owner_key == SM64_SATURN_POSE_POOL_OWNER_NONE)
        return 0U;
    for (index = 0U; index < pool->slot_count; index++) {
        sm64_saturn_pose_handle_t handle;
        if (pool->slot[index].in_use == 0U ||
            pool->slot[index].owner_key != owner_key)
            continue;
        handle.index = (uint16_t)(index + 1U);
        handle.generation = pool->slot[index].generation;
        if (sm64_saturn_pose_pool_release(pool, handle))
            released++;
    }
    return released;
}

uint32_t sm64_saturn_pose_pool_largest_free_extent(
    const sm64_saturn_pose_pool_t *pool)
{
    uint32_t largest = 0U;
    uint16_t class_index;

    if (pool == NULL || pool->ready == 0U)
        return 0U;
    for (class_index = 0U; class_index < pool->class_count; class_index++) {
        if (pool->class_free_head[class_index] == POOL_FREE_LIST_END)
            continue;
        if (pool->class_bytes[class_index] > largest)
            largest = pool->class_bytes[class_index];
    }
    return largest;
}
