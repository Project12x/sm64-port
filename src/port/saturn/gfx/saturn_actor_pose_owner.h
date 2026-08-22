/* Pose-pool owner keys: the stable per-instance identity a pose slot is let
 * under, and the partition of the key space that keeps two tenants from ever
 * colliding.
 *
 * THE PROBLEM
 * -----------
 * `sm64_saturn_pose_pool_find` and `sm64_saturn_pose_pool_release_owner` both
 * match on a `uint32_t owner_key`.  A key that is not stable across the two
 * in-flight render generations makes `find` miss its own slot every other
 * frame; a key that two different actors can both produce lets one actor
 * release -- or re-find -- another actor's pose.  An object's *address* is
 * neither: SM64 recycles `gObjectPool` entries, so the same address is a
 * different actor after a despawn/respawn, and it is not carried across the
 * pointer-free snapshot boundary at all.
 *
 * THE KEY
 * -------
 * The runtime already computes exactly the composite this needs.
 * `sm64_saturn_actor_instance_bank_capture` publishes
 *
 *     instance_key = (incarnation << 16) | pool_slot
 *
 * (saturn_actor_instance.c) where `pool_slot` is the source object-pool index
 * (0..239, validated twice) and `incarnation` is bumped by the geo observer
 * only on a dead->live edge for that slot (saturn_geo_state_observer.c).
 * **The pose-pool owner key IS the instance key.**  Reusing it rather than
 * inventing a parallel identity is what makes the stability argument short:
 *
 *   1. NON-ZERO.  `incarnation` is bumped to at least 1 before a slot goes
 *      live and is forced to 1 on wrap, so the high half is never 0 for a
 *      live actor and the key can never collide with
 *      `SM64_SATURN_POSE_POOL_OWNER_NONE`.
 *   2. STABLE ACROSS BOTH IN-FLIGHT GENERATIONS.  `incarnation[slot]` is
 *      touched only inside `begin_object` and only when `live[slot]` is
 *      false.  A continuously observed actor keeps `live[slot] == 1` across
 *      `end_frame`, so its key is byte-identical every frame it is alive --
 *      which is the property `find` needs and the reason a snapshot may carry
 *      a key instead of a handle.
 *   3. NO SIMULTANEOUS ALIAS.  Two live actors have different `pool_slot`s:
 *      one object occupies one pool slot, and `seen[]` rejects a second
 *      observation of the same slot within a frame.
 *   4. NO ALIAS ACROSS TIME.  A respawn into a recycled slot sees
 *      `live[slot] == 0` and bumps `incarnation`, so the new tenant's key
 *      differs from the dead one's in the high half.  The despawn edge in
 *      `end_frame` fires *before* that bump, so the release always names the
 *      departing actor, never its successor.
 *
 * THE KEY SPACE, PARTITIONED
 * --------------------------
 *   0x00000000                    OWNER_NONE, reserved by the pool.
 *   any << 16 | slot, slot < 240  a generic actor instance.
 *   any << 16 | slot, slot >= 240 reserved: Mario, and the target evidence
 *                                 scaffolding's synthetic tenants.
 *
 * The low half is the discriminator because it is the half a generic key
 * *bounds* -- `pool_slot < SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY` is checked
 * in both `begin_object` and `instance_bank_capture`.  Reserving on the high
 * half instead would not be sound: `incarnation` is free to reach any 16-bit
 * value, so a long-lived slot could eventually mint a key that collides with
 * a reserved tag.  Reserving on the bounded half makes the partition
 * structural rather than probabilistic.
 */
#ifndef SM64_SATURN_ACTOR_POSE_OWNER_H
#define SM64_SATURN_ACTOR_POSE_OWNER_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_actor_instance.h"

/* Every key at or above this low half is reserved and can never be minted
 * from a source pool slot. */
#define SM64_SATURN_POSE_OWNER_RESERVED_BASE 0xFF00U

_Static_assert(SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY <=
                   SM64_SATURN_POSE_OWNER_RESERVED_BASE,
               "the source object pool can mint a reserved pose owner key");

/* The key a generic actor instance holds its pose slot under.  This is the
 * same value as `sm64_saturn_actor_instance_snapshot_t.instance_key`. */
static inline uint32_t sm64_saturn_pose_owner_compose(uint16_t pool_slot,
                                                      uint16_t incarnation)
{
    return ((uint32_t)incarnation << 16) | (uint32_t)pool_slot;
}

/* Identity, named: the snapshot's instance key is the pose owner key.  This
 * exists so the coupling is a call site somebody can grep for rather than an
 * undocumented convention. */
static inline uint32_t sm64_saturn_pose_owner_from_instance_key(
    uint32_t instance_key)
{
    return instance_key;
}

static inline bool sm64_saturn_pose_owner_is_generic(uint32_t owner_key)
{
    return owner_key != 0UL &&
           (owner_key & 0xFFFFUL) < SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY &&
           (owner_key >> 16) != 0UL;
}

/* A key no source pool slot can produce, for Mario and for the target
 * evidence scaffolding. */
static inline uint32_t sm64_saturn_pose_owner_reserved(uint16_t tag,
                                                       uint16_t ordinal)
{
    return ((uint32_t)tag << 16) |
           (uint32_t)(SM64_SATURN_POSE_OWNER_RESERVED_BASE + (ordinal & 0xFFU));
}

#endif
