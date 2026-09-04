/*
 * Task 4 live-Mario bridge.
 *
 * The generated mesh/pose bank is promoted from the M2 turntable unchanged;
 * this file only selects a source pose from the real engine's read-only
 * graph-node state.  The walking bank is intentionally used for the current
 * walking-family staging set; other animation IDs use the generated neutral
 * source animation until their source pose bank is promoted.
 */
#include "saturn_actor_bridge.h"
#include "saturn_actor_bundle.h"
#include "saturn_actor_pose_owner.h"
#include "saturn_actor_pose_pool.h"
#include "saturn_actor_rigid_pose.h"
#include "saturn_render_native_math.h"
#include "port/saturn/platform/saturn_cart_code.h"

#include <string.h>

#include "game/level_update.h"
#include "game/camera.h"
#include "game/mario.h"
#include "game/object_list_processor.h"
#include "graph_node.h"
#include "mario_animation_ids.h"
#include "object_fields.h"

#include "saturn_mario_actor_mesh.h"

#ifndef SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
#define SATURN_FEATURE_COMPLETE_MARIO_ANIMATION 0
#endif

#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
extern const uint8_t sm64_saturn_mario_actor_bank_data[];
extern const uint32_t sm64_saturn_mario_actor_bank_size;
/* One published pose per in-flight render generation.  This is a pipeline
 * depth (`sm64_saturn_render_snapshot_bank_t.slot[2]`), not an actor count. */
#define COMPLETE_ACTOR_POSE_SLOT_COUNT 2U
/* Mario's key is outside the generic key space by construction: its low
 * half is 0x524F, and a generic key's low half is a source pool slot
 * bounded by SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY.  See
 * saturn_actor_pose_owner.h for the partition. */
#define COMPLETE_ACTOR_OWNER_MARIO 0x4D41524FUL /* 'MARO' */
_Static_assert((COMPLETE_ACTOR_OWNER_MARIO & 0xFFFFUL) >=
                   SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY,
               "Mario pose owner key is reachable from a source pool slot");

/* The joint matrices are workspace, not published state: nothing downstream
 * of the evaluator reads them (`sm64_saturn_actor_pose_view_t` carries only
 * vertices, lights and the root translation, and the render worker -- master
 * and slave alike -- reads only those).  They were per-slot in the shipped
 * build, which cost 1,280 B of HWRAM per generation for bytes no consumer
 * ever looks at.  Exactly one evaluator lane exists: the master sim tick
 * calls complete_actor_evaluate once per tick and no other caller is compiled
 * into the product tuple.  A second concurrent evaluator would need a second
 * workspace, 1,280 B each. */
#define COMPLETE_ACTOR_JOINT_CAPACITY 20U
typedef struct complete_actor_pose_slot {
    int16_t vertices[SM64_MARIO_VERTEX_COUNT][3];
    uint8_t lights[SM64_MARIO_VERTEX_COUNT];
    sm64_saturn_actor_pose_view_t evaluated;
    uint16_t animation_id;
    uint16_t animation_frame;
    uint8_t valid;
} complete_actor_pose_slot_t;
static sm64_saturn_actor_bank_view_t complete_actor_bank;
static uint8_t complete_actor_next_pose_slot;
static uint8_t complete_actor_bank_ready;

/* --- pose-slot pools ----------------------------------------------------
 *
 * Two arenas, one allocator.  Residency is the priority mechanism: Mario is
 * the only tenant of the hot HWRAM arena, so no number of enemies can starve
 * him, and no enemy pays for his 2,968-byte skinned pose.  Everyone else
 * claims from the cold LWRAM arena, which is 16-bit and slower but has
 * ~88 KB free against HWRAM tight ~2 KB of margin.
 *
 * Memory record: hot arena = 2 * sizeof(complete_actor_pose_slot_t) = 6,000 B
 * of HWRAM (the shipped build spent 8,560 B on the same two slots), plus
 * 1,280 B of shared joint workspace.  Cold arena = 7,168 B of LWRAM.
 * Lifetime is the process; the owner is this file; reset happens only at bank
 * re-validation, which is once. */
/* The class plan, re-derived once the published rigid-part layout was
 * settled at 24 B per part (saturn_actor_rigid_pose.h).
 * r2-pose-slot-pooling.md section 2 sized these against a 48 B part and
 * section 8.2 asked for the plan to be re-derived if a smaller part was
 * chosen.  It was.
 *
 *   small 256 B = a 10-part pose exactly.  Goomba scale.
 *   large 640 B = a 24-part pose (592 B) with room for the header.  It
 *                 covers every jointed actor in the BOB closure, and
 *                 the shipped 20-joint Mario hierarchy at 496 B.
 *
 * Sixteen blocks is still eight enemies at two in-flight render
 * generations, which is what the pipeline depth demands; the arena is
 * 7,168 B instead of 10,240 B, so the smaller part layout returns
 * 3,072 B of LWRAM. */
#define COMPLETE_ACTOR_COLD_ARENA_BYTES 7168U
#define COMPLETE_ACTOR_COLD_SMALL_BYTES 256U
#define COMPLETE_ACTOR_COLD_LARGE_BYTES 640U

static int32_t complete_actor_joint_workspace[COMPLETE_ACTOR_JOINT_CAPACITY * 16U];
static uint8_t complete_actor_hot_arena[
    COMPLETE_ACTOR_POSE_SLOT_COUNT * sizeof(complete_actor_pose_slot_t)]
    __attribute__((aligned(4)));
static sm64_saturn_pose_handle_t complete_actor_mario_handle[
    COMPLETE_ACTOR_POSE_SLOT_COUNT];
static uint8_t complete_actor_pools_ready;

/* See the fill site in sm64_saturn_mario_actor_snapshot() for why this exists
 * and what its two comparisons decide. */
typedef struct sm64_saturn_mario_orient_probe {
    uint32_t magic;
    uint32_t samples;
    uint32_t face_angle;   /* gMarioState->faceAngle[1], u16 */
    uint32_t gfx_angle;    /* graph object's rendered angle[1], u16; ~0 = absent */
    uint32_t camera_yaw;   /* gLakituState.yaw, u16 */
    uint32_t vel_x;        /* world units, int32 */
    uint32_t vel_z;
    uint32_t forward_vel;
    uint32_t action;
    /* Frame of reference. Mario's own pipeline measured clean on 2026-09-03
     * (faceAngle == gfx.angle exactly; faceAngle == heading(vel) to 1 deg once
     * negative forwardVel is accounted for), so the remaining question is
     * where the camera stands relative to the way he faces: a camera that
     * LEADS instead of TRAILS shows his face when you expect his back, and
     * still reads correctly in profile -- exactly the reported symptom. */
    uint32_t pos_x;
    uint32_t pos_z;
    uint32_t cam_pos_x;
    uint32_t cam_pos_z;
    uint32_t cam_focus_x;
    uint32_t cam_focus_z;
} sm64_saturn_mario_orient_probe_t;

#define SM64_SATURN_MARIO_ORIENT_PROBE_MAGIC 0x4D4F5249U /* 'MORI' */

volatile sm64_saturn_mario_orient_probe_t g_sm64_saturn_mario_orient_probe
    __attribute__((section(".lwram_bss"), used, aligned(4)));

static sm64_saturn_pose_pool_t complete_actor_hot_pool
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static sm64_saturn_pose_pool_t complete_actor_cold_pool
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static uint8_t complete_actor_cold_arena[COMPLETE_ACTOR_COLD_ARENA_BYTES]
    __attribute__((section(".lwram_bss"), used, aligned(4)));

static complete_actor_pose_slot_t *complete_actor_slot(uint8_t index)
{
    if (index >= COMPLETE_ACTOR_POSE_SLOT_COUNT ||
        complete_actor_pools_ready == 0U)
        return NULL;
    return (complete_actor_pose_slot_t *)sm64_saturn_pose_pool_resolve(
        &complete_actor_hot_pool, complete_actor_mario_handle[index]);
}

/* The reduced bake keeps a record for every animation ID and aliases the ones
 * it does not bake onto their nearest baked neighbour, so no requested ID can
 * evaluate to nothing.  This ID is the last resort if the bank itself is
 * unusable for the requested ID: the neutral idle the =0 path already used.
 * It is always present in the bake list. */
#define COMPLETE_ACTOR_FALLBACK_ANIMATION_ID ((int16_t)MARIO_ANIM_IDLE_HEAD_CENTER)

/* Per-frame-invariant bank slices, staged once out of CART into LWRAM.  The
 * bank lives in `.cart_rodata`; without this the pose evaluator re-reads 424
 * vertex records and 20 joint records across the A-bus every frame for data
 * that never changes.  HWRAM has 2,552 B of slack and LWRAM has 92,704 B
 * free, so the copy is funded from the idle pool. */
static sm64_saturn_actor_vertex_t complete_actor_staged_vertices[
    SM64_MARIO_VERTEX_COUNT]
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static sm64_saturn_actor_joint_t complete_actor_staged_joints[20U]
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static uint8_t complete_actor_slices_staged;

/* Live-observable evidence for the reduced bake and the LWRAM staging.  Kept
 * in LWRAM so it costs no HWRAM, and `used` so --gc-sections cannot discard
 * it.  `probe_*` are filled once from the shipped bank; the counters are the
 * proof that the per-frame path never needed the last-resort fallback. */
#define COMPLETE_ACTOR_PROBE_ID_COUNT 4U
typedef struct sm64_saturn_complete_actor_probe {
    uint32_t magic;
    uint32_t probe_values_offset[COMPLETE_ACTOR_PROBE_ID_COUNT];
    uint16_t probe_animation_id[COMPLETE_ACTOR_PROBE_ID_COUNT];
    uint16_t probe_frame_count[COMPLETE_ACTOR_PROBE_ID_COUNT];
    uint16_t bank_animation_count;
    uint16_t bank_vertex_count;
    uint16_t staged_slices;
    uint16_t evaluate_calls;
    uint16_t fallback_hits;
    uint16_t last_fallback_requested_id;
    /* Pose-slot pool evidence.  `pool_selftest_mask` is the boot-time
     * claim/release/reuse/exhaustion/disjointness sequence run against real
     * LWRAM on the real SH-2; `pool_alias_faults` is the per-tick check that
     * two simultaneously held cold slots never see each other bytes. */
    uint16_t pool_ready;
    uint16_t pool_selftest_mask;
    uint16_t pool_hot_live;
    uint16_t pool_hot_slot_bytes;
    uint16_t pool_cold_live;
    uint16_t pool_cold_peak_live;
    uint16_t pool_cold_claim_failures;
    uint16_t pool_cold_stale_resolves;
    uint16_t pool_cold_slot_count;
    uint16_t pool_frames_checked;
    uint16_t pool_alias_faults;
    uint16_t pool_cold_arena_bytes;
    /* Rigid-part lane (the second evaluator) and the despawn release path.
     * `rigid_selftest_mask` is a boot-time exercise of the real evaluator
     * against the real bank, publishing into a real cold-LWRAM block.
     * `despawn_releases` is the counter that would have sat at zero for the
     * whole session while the pool leaked: it counts pose slots actually
     * handed back by the object lifecycle. */
    uint16_t rigid_selftest_mask;
    uint16_t rigid_part_count;
    uint16_t rigid_pose_bytes;
    uint16_t rigid_part_bytes;
    uint16_t despawn_edges;
    uint16_t despawn_releases;
    /* The first non-Mario actor bank, evaluated through the rigid lane.
     * `enemy_bank_*` are read straight out of the bank header resolved from
     * the resident S64F actor bundle -- no second copy is linked, so this
     * costs zero cart bytes.  `enemy_rigid_*` are the same six checks the
     * Mario arm runs, against a 13-joint Bob-omb hierarchy and a real
     * two-animation stream. */
    uint16_t enemy_bank_ready;
    uint16_t enemy_bank_joint_count;
    uint16_t enemy_bank_animation_count;
    uint16_t enemy_rigid_selftest_mask;
    uint16_t enemy_rigid_part_count;
    uint16_t enemy_rigid_pose_bytes;
} sm64_saturn_complete_actor_probe_t;
sm64_saturn_complete_actor_probe_t sm64_saturn_complete_actor_probe
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static const uint16_t complete_actor_probe_ids[COMPLETE_ACTOR_PROBE_ID_COUNT] = {
    29U,  /* MARIO_ANIM_CREDITS_WAVING  - not baked; aliases its neighbour */
    28U,  /* MARIO_ANIM_CLIMB_DOWN_LEDGE - baked; the expected alias target */
    195U, /* MARIO_ANIM_IDLE_HEAD_LEFT  - baked; requested by the route      */
    114U, /* MARIO_ANIM_RUNNING         - baked; requested by the route      */
};

static void complete_actor_stage_slices(void)
{
    uint16_t index;
    if (complete_actor_bank.bank.vertex_count > SM64_MARIO_VERTEX_COUNT ||
        complete_actor_bank.bank.joint_count > 20U)
        return;
    for (index = 0U; index < complete_actor_bank.bank.vertex_count; index++) {
        if (!sm64_saturn_actor_bank_vertex(&complete_actor_bank, index,
                                           &complete_actor_staged_vertices[index]) ||
            complete_actor_staged_vertices[index].joint_ordinal >=
                complete_actor_bank.bank.joint_count)
            return;
    }
    for (index = 0U; index < complete_actor_bank.bank.joint_count; index++) {
        if (!sm64_saturn_actor_bank_joint(&complete_actor_bank, index,
                                          &complete_actor_staged_joints[index]) ||
            complete_actor_staged_joints[index].parent_ordinal >= (int16_t)index ||
            complete_actor_staged_joints[index].parent_ordinal < -1)
            return;
    }
    complete_actor_slices_staged = 1U;
}

static void complete_actor_publish_probe(void)
{
    sm64_saturn_complete_actor_probe.bank_animation_count =
        complete_actor_bank.bank.animation_count;
    sm64_saturn_complete_actor_probe.bank_vertex_count =
        complete_actor_bank.bank.vertex_count;
    sm64_saturn_complete_actor_probe.staged_slices = complete_actor_slices_staged;
    for (uint16_t index = 0U; index < COMPLETE_ACTOR_PROBE_ID_COUNT; index++) {
        sm64_saturn_actor_animation_record_t record;
        sm64_saturn_complete_actor_probe.probe_animation_id[index] =
            complete_actor_probe_ids[index];
        if (!sm64_saturn_actor_bank_animation(&complete_actor_bank,
                                              complete_actor_probe_ids[index],
                                              &record))
            continue;
        sm64_saturn_complete_actor_probe.probe_frame_count[index] =
            record.frame_count;
        sm64_saturn_complete_actor_probe.probe_values_offset[index] =
            record.values_offset;
    }
    sm64_saturn_complete_actor_probe.magic = 0x53414E4DUL; /* 'SANM' */
}

/* Two synthetic cold tenants, held for the life of the session.  They stand
 * in for the enemies R2 will spawn: the point of keeping them is that the
 * per-tick check below proves, on target, that two simultaneously held slots
 * do not alias -- the failure this pool exists to prevent, and the one that
 * would otherwise ship silently because it looks like nothing until an enemy
 * wears another enemy pose. */
/* The synthetic tenants sit in the reserved key band so that no generic
 * actor key -- at any incarnation, including the wrap value -- can ever
 * name one of them.  The old 0xE00000xx constants had low halves inside
 * the source pool slot range and would have collided with a real actor
 * whose incarnation reached the matching high half. */
#define COMPLETE_ACTOR_OWNER_EXERCISE_A \
    sm64_saturn_pose_owner_reserved(0xE000U, 1U)
#define COMPLETE_ACTOR_OWNER_EXERCISE_B \
    sm64_saturn_pose_owner_reserved(0xE000U, 2U)
#define COMPLETE_ACTOR_OWNER_DRAIN \
    sm64_saturn_pose_owner_reserved(0xE002U, 0U)
#define COMPLETE_ACTOR_OWNER_PAST_END \
    sm64_saturn_pose_owner_reserved(0xE003U, 0U)
#define COMPLETE_ACTOR_OWNER_REUSE \
    sm64_saturn_pose_owner_reserved(0xE004U, 0U)
#define COMPLETE_ACTOR_OWNER_OVERSIZE \
    sm64_saturn_pose_owner_reserved(0xE005U, 0U)
#define COMPLETE_ACTOR_OWNER_RIGID \
    sm64_saturn_pose_owner_reserved(0xE006U, 0U)
/* The first non-Mario tenant.  It is a *reserved* key, not a generic instance
 * key, because this is still a boot-time exercise: no object-pool slot owns
 * it, so it can never alias a real actor's `(incarnation << 16) | pool_slot`. */
#define COMPLETE_ACTOR_OWNER_RIGID_ENEMY \
    sm64_saturn_pose_owner_reserved(0xE007U, 0U)

/* The resident S64F actor bundle, handed over by the sourceboot scene owner
 * once it has validated it.  The bundle already ships in CART for the generic
 * actor path, so the first enemy bank costs *zero* additional cart bytes: this
 * lane reads the copy that is already there rather than linking a second one.
 * Set once at scene bring-up, read once on the first bank use; never per
 * frame. */
static const uint8_t *complete_actor_enemy_bundle
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static uint32_t complete_actor_enemy_bundle_bytes
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static uint16_t complete_actor_enemy_family
    __attribute__((section(".lwram_bss"), used, aligned(4)));
static uint16_t complete_actor_enemy_model
    __attribute__((section(".lwram_bss"), used, aligned(4)));

#define COMPLETE_ACTOR_EXERCISE_TENANTS 2U
#define COMPLETE_ACTOR_EXERCISE_STRIPE 32U
static sm64_saturn_pose_handle_t complete_actor_exercise[
    COMPLETE_ACTOR_EXERCISE_TENANTS];

enum {
    POOL_SELFTEST_HOT_INIT = 1U << 0,
    POOL_SELFTEST_COLD_INIT = 1U << 1,
    POOL_SELFTEST_MARIO_CLAIMED = 1U << 2,
    POOL_SELFTEST_MARIO_DISJOINT = 1U << 3,
    POOL_SELFTEST_TWO_TENANTS = 1U << 4,
    POOL_SELFTEST_TENANTS_DISJOINT = 1U << 5,
    POOL_SELFTEST_EXHAUSTION_FAILS = 1U << 6,
    POOL_SELFTEST_EXHAUSTION_HARMLESS = 1U << 7,
    POOL_SELFTEST_RELEASE_KILLS_HANDLE = 1U << 8,
    POOL_SELFTEST_REUSE_OK = 1U << 9,
    POOL_SELFTEST_OVERSIZE_FAILS = 1U << 10
};

SM64_SATURN_CART_COLD
static void complete_actor_stripe(sm64_saturn_pose_handle_t handle,
                                  uint8_t pattern)
{
    uint8_t *const bytes = (uint8_t *)sm64_saturn_pose_pool_resolve(
        &complete_actor_cold_pool, handle);
    uint16_t index;
    if (bytes == NULL) return;
    for (index = 0U; index < COMPLETE_ACTOR_EXERCISE_STRIPE; index++)
        bytes[index] = pattern;
}

SM64_SATURN_CART_COLD
static uint8_t complete_actor_stripe_ok(sm64_saturn_pose_handle_t handle,
                                        uint8_t pattern)
{
    const uint8_t *const bytes = (const uint8_t *)
        sm64_saturn_pose_pool_resolve(&complete_actor_cold_pool, handle);
    uint16_t index;
    if (bytes == NULL) return 0U;
    for (index = 0U; index < COMPLETE_ACTOR_EXERCISE_STRIPE; index++) {
        if (bytes[index] != pattern) return 0U;
    }
    return 1U;
}

/* Boot-time exercise of the cold pool against real LWRAM.  Runs once, so it
 * must not spend HWRAM -- the per-tick witness that used to keep these
 * helpers hot is gone. */
SM64_SATURN_CART_COLD
static uint16_t complete_actor_pool_selftest(void)
{
    sm64_saturn_pose_handle_t filler[8];
    sm64_saturn_pose_handle_t reused;
    uint16_t mask = 0U;
    uint16_t filled = 0U;
    uint16_t index;

    complete_actor_exercise[0] = sm64_saturn_pose_pool_claim(
        &complete_actor_cold_pool, COMPLETE_ACTOR_OWNER_EXERCISE_A,
        COMPLETE_ACTOR_COLD_LARGE_BYTES);
    complete_actor_exercise[1] = sm64_saturn_pose_pool_claim(
        &complete_actor_cold_pool, COMPLETE_ACTOR_OWNER_EXERCISE_B,
        COMPLETE_ACTOR_COLD_SMALL_BYTES);
    if (sm64_saturn_pose_handle_is_valid(complete_actor_exercise[0]) &&
        sm64_saturn_pose_handle_is_valid(complete_actor_exercise[1]))
        mask |= POOL_SELFTEST_TWO_TENANTS;

    complete_actor_stripe(complete_actor_exercise[0], 0xA5U);
    complete_actor_stripe(complete_actor_exercise[1], 0x5AU);
    if (complete_actor_stripe_ok(complete_actor_exercise[0], 0xA5U) &&
        complete_actor_stripe_ok(complete_actor_exercise[1], 0x5AU))
        mask |= POOL_SELFTEST_TENANTS_DISJOINT;

    /* Drain everything that is left, then confirm exhaustion is a refusal
     * rather than an eviction. */
    for (index = 0U; index < 8U; index++) {
        filler[index] = sm64_saturn_pose_pool_claim(
            &complete_actor_cold_pool,
            sm64_saturn_pose_owner_reserved(0xE001U, index),
            COMPLETE_ACTOR_COLD_SMALL_BYTES);
        if (!sm64_saturn_pose_handle_is_valid(filler[index])) break;
        filled++;
    }
    while (sm64_saturn_pose_pool_largest_free_extent(&complete_actor_cold_pool)
           != 0U) {
        if (!sm64_saturn_pose_handle_is_valid(sm64_saturn_pose_pool_claim(
                &complete_actor_cold_pool, COMPLETE_ACTOR_OWNER_DRAIN,
                COMPLETE_ACTOR_COLD_SMALL_BYTES)))
            break;
    }
    if (!sm64_saturn_pose_handle_is_valid(sm64_saturn_pose_pool_claim(
            &complete_actor_cold_pool, COMPLETE_ACTOR_OWNER_PAST_END,
            COMPLETE_ACTOR_COLD_SMALL_BYTES)))
        mask |= POOL_SELFTEST_EXHAUSTION_FAILS;
    if (complete_actor_stripe_ok(complete_actor_exercise[0], 0xA5U) &&
        complete_actor_stripe_ok(complete_actor_exercise[1], 0x5AU))
        mask |= POOL_SELFTEST_EXHAUSTION_HARMLESS;

    /* Release one filler; its handle must die and its block must come back. */
    if (filled > 0U) {
        const sm64_saturn_pose_handle_t dead = filler[0];
        if (sm64_saturn_pose_pool_release(&complete_actor_cold_pool, dead) &&
            sm64_saturn_pose_pool_resolve(&complete_actor_cold_pool, dead) ==
                NULL)
            mask |= POOL_SELFTEST_RELEASE_KILLS_HANDLE;
        reused = sm64_saturn_pose_pool_claim(&complete_actor_cold_pool,
                                             COMPLETE_ACTOR_OWNER_REUSE,
                                             COMPLETE_ACTOR_COLD_SMALL_BYTES);
        if (sm64_saturn_pose_handle_is_valid(reused) &&
            sm64_saturn_pose_pool_resolve(&complete_actor_cold_pool, reused) !=
                NULL &&
            sm64_saturn_pose_pool_resolve(&complete_actor_cold_pool, dead) ==
                NULL)
            mask |= POOL_SELFTEST_REUSE_OK;
    }

    /* Nothing may be larger than the largest class. */
    if (!sm64_saturn_pose_handle_is_valid(sm64_saturn_pose_pool_claim(
            &complete_actor_cold_pool, COMPLETE_ACTOR_OWNER_OVERSIZE, 0xFFFFU)))
        mask |= POOL_SELFTEST_OVERSIZE_FAILS;

    /* Hand the arena back to the two long-lived tenants. */
    for (index = 0U; index < 8U; index++) {
        (void)sm64_saturn_pose_pool_release_owner(&complete_actor_cold_pool,
                                                  sm64_saturn_pose_owner_reserved(0xE001U, index));
    }
    (void)sm64_saturn_pose_pool_release_owner(&complete_actor_cold_pool,
                                              COMPLETE_ACTOR_OWNER_DRAIN);
    (void)sm64_saturn_pose_pool_release_owner(&complete_actor_cold_pool,
                                              COMPLETE_ACTOR_OWNER_REUSE);
    if (complete_actor_stripe_ok(complete_actor_exercise[0], 0xA5U) &&
        complete_actor_stripe_ok(complete_actor_exercise[1], 0x5AU))
        mask |= POOL_SELFTEST_TENANTS_DISJOINT;

    /* Hand the two exercise blocks back.  They used to be held for the whole
     * session so a per-tick witness could keep re-striping them; that witness
     * is gone (see below), so holding them would just be two cold blocks of
     * the sixteen permanently unavailable to real tenants. */
    (void)sm64_saturn_pose_pool_release_owner(&complete_actor_cold_pool,
                                              COMPLETE_ACTOR_OWNER_EXERCISE_A);
    (void)sm64_saturn_pose_pool_release_owner(&complete_actor_cold_pool,
                                              COMPLETE_ACTOR_OWNER_EXERCISE_B);
    complete_actor_exercise[0] = sm64_saturn_pose_handle_invalid();
    complete_actor_exercise[1] = sm64_saturn_pose_handle_invalid();
    return mask;
}

/* The per-tick pool alias witness has been REMOVED.
 *
 * It re-striped both long-lived cold tenants with a tick-varying byte and
 * read them back every time Mario's pose was evaluated -- two 32-byte writes
 * plus two 32-byte compares per frame, inlined into complete_actor_evaluate
 * and therefore resident in HWRAM rather than CART.
 *
 * It did its job: `pool_alias_faults == 0` over 199 consecutive on-target
 * evaluations (r2-pose-slot-pooling.md, reconfirmed in
 * r2-rigid-part-evaluator.md) is the evidence that the arena partition never
 * lets one tenant touch another. That is a property of the partition, fixed
 * at init, so a per-frame re-proof buys nothing once it has been observed.
 * The boot-time selftest above still proves disjointness on every boot.
 *
 * `pool_frames_checked` and `pool_alias_faults` remain in the probe struct
 * and now stay at their boot values; no build gate reads them. */

enum {
    RIGID_SELFTEST_CLAIMED = 1U << 0,
    RIGID_SELFTEST_EVALUATED = 1U << 1,
    RIGID_SELFTEST_PART_COUNT = 1U << 2,
    RIGID_SELFTEST_FRAMES_DIFFER = 1U << 3,
    RIGID_SELFTEST_UNDERSIZE_REFUSED = 1U << 4,
    RIGID_SELFTEST_RELEASED = 1U << 5,
    /* Only the enemy arm asks for this: a second baked animation ID must
     * decode as well as the first. */
    RIGID_SELFTEST_ALT_ANIMATION = 1U << 6
};

/* Boot-time exercise of the rigid-part lane on the real SH-2: claim a cold
 * block, evaluate a real hierarchy out of the real bank into it, and prove
 * the published bytes are what the layout says they are.  Cold code -- it
 * runs once, so it must not spend HWRAM.
 *
 * The bank used is the shipped Mario bank because it is the only actor bank
 * that exists today.  This is NOT how Mario is drawn: he keeps the skinned
 * lane untouched.  It is a 20-joint hierarchy with a real animation stream,
 * which is the input the rigid lane has to survive when enemy banks arrive. */
SM64_SATURN_CART_COLD
static uint16_t complete_actor_rigid_selftest(
    const sm64_saturn_actor_bank_view_t *bank, uint32_t owner,
    int16_t animation_id, int16_t second_frame, int16_t alt_animation_id,
    const sm64_saturn_actor_joint_t *staged_joints,
    uint16_t *out_part_count, uint16_t *out_pose_bytes)
{
    sm64_saturn_pose_handle_t handle;
    sm64_saturn_rigid_pose_t *pose;
    sm64_saturn_mtx_t *const workspace =
        (sm64_saturn_mtx_t *)(void *)complete_actor_joint_workspace;
    uint16_t mask = 0U;
    uint32_t extent;
    sm64_saturn_rigid_part_t first_part;

    /* The composition workspace is shared with the skinned lane, so the rigid
     * lane is capped by that array, not by the published layout: a hierarchy
     * deeper than COMPLETE_ACTOR_JOINT_CAPACITY needs 64 B more of HWRAM per
     * extra part before it can be evaluated here. */
    if (bank == NULL || bank->bank.joint_count == 0U ||
        bank->bank.joint_count > COMPLETE_ACTOR_JOINT_CAPACITY)
        return mask;
    extent = SM64_SATURN_RIGID_POSE_BYTES(bank->bank.joint_count);
    if (extent > COMPLETE_ACTOR_COLD_LARGE_BYTES)
        return mask;

    handle = sm64_saturn_pose_pool_claim(&complete_actor_cold_pool,
                                         owner, extent);
    if (!sm64_saturn_pose_handle_is_valid(handle))
        return mask;
    mask |= RIGID_SELFTEST_CLAIMED;
    pose = (sm64_saturn_rigid_pose_t *)sm64_saturn_pose_pool_resolve(
        &complete_actor_cold_pool, handle);
    if (pose == NULL)
        return mask;

    if (sm64_saturn_actor_rigid_pose_evaluate(
            bank, animation_id, 0, staged_joints,
            workspace, pose, extent) &&
        pose->flags == SM64_SATURN_RIGID_POSE_VALID) {
        mask |= RIGID_SELFTEST_EVALUATED;
        if (pose->part_count == bank->bank.joint_count)
            mask |= RIGID_SELFTEST_PART_COUNT;
        first_part = pose->part[bank->bank.joint_count - 1U];
        /* A second frame of the same animation must move at least the last
         * part, or the lane is publishing a bind pose and calling it
         * animation. */
        if (sm64_saturn_actor_rigid_pose_evaluate(
                bank, animation_id, second_frame, staged_joints,
                workspace, pose, extent) &&
            memcmp(&first_part, &pose->part[bank->bank.joint_count - 1U],
                   sizeof(first_part)) != 0)
            mask |= RIGID_SELFTEST_FRAMES_DIFFER;
        /* Every baked animation must decode, not just the first one: an
         * unbaked or misaliased ID must never be reachable at draw time. */
        if (alt_animation_id >= 0 &&
            sm64_saturn_actor_rigid_pose_evaluate(
                bank, alt_animation_id, 0, staged_joints,
                workspace, pose, extent) &&
            pose->flags == SM64_SATURN_RIGID_POSE_VALID &&
            pose->animation_id == (uint16_t)alt_animation_id)
            mask |= RIGID_SELFTEST_ALT_ANIMATION;
    }

    /* A block that cannot hold the hierarchy must be refused, not
     * overrun -- this is the check that keeps a deep actor from writing past
     * its pool block into its neighbour's pose. */
    if (!sm64_saturn_actor_rigid_pose_evaluate(
            bank, animation_id, 0, staged_joints,
            workspace, pose, extent - 1U) &&
        pose->flags == 0U)
        mask |= RIGID_SELFTEST_UNDERSIZE_REFUSED;

    if (out_part_count != NULL) *out_part_count = pose->part_count;
    if (out_pose_bytes != NULL) *out_pose_bytes = (uint16_t)extent;

    /* Hand the block back: this exercise is scaffolding and must not hold a
     * block a real enemy could use. */
    if (sm64_saturn_pose_pool_release_owner(&complete_actor_cold_pool,
                                            owner) == 1U &&
        sm64_saturn_pose_pool_resolve(&complete_actor_cold_pool, handle) == NULL)
        mask |= RIGID_SELFTEST_RELEASED;
    return mask;
}

/* The despawn release path, wired to the object lifecycle.
 *
 * r2-pose-slot-pooling.md section 8.5: `release_owner` shipped tested and
 * uncalled, so the pool would have leaked to exhaustion inside a level the
 * moment enemies started claiming.  The geo observer already knows exactly
 * when an actor leaves -- a pool slot that was live and was not seen this
 * frame -- and it already carries the incarnation that makes the key unique
 * across recycling.  This turns that edge into a release.
 *
 * Releasing immediately, rather than deferring a generation, is deliberate:
 * the pool's generation check kills every outstanding handle to the block, so
 * a render consumer still holding last frame's handle resolves NULL and skips
 * the actor.  The actor is despawning, so skipping it is the correct picture,
 * and it can never be shown wearing the next tenant's pose. */
static void complete_actor_despawn_sink(uint16_t pool_slot,
                                        uint16_t incarnation, void *context)
{
    const uint32_t owner =
        sm64_saturn_pose_owner_compose(pool_slot, incarnation);
    uint16_t released;
    (void)context;
    if (sm64_saturn_complete_actor_probe.despawn_edges != UINT16_MAX)
        sm64_saturn_complete_actor_probe.despawn_edges++;
    released = sm64_saturn_pose_pool_release_owner(&complete_actor_cold_pool,
                                                   owner);
    if (sm64_saturn_complete_actor_probe.despawn_releases <=
        (uint16_t)(UINT16_MAX - released))
        sm64_saturn_complete_actor_probe.despawn_releases =
            (uint16_t)(sm64_saturn_complete_actor_probe.despawn_releases +
                       released);
}

/* The first non-Mario actor evaluated through the rigid lane.
 *
 * r2-rigid-part-evaluator.md section 8.1 named "a Bob-omb or Goomba .s64b
 * bake" as the blocker and recorded that `mario.s64b` was the only bank in
 * the tree.  That is true of the standalone bank *files*; it is not true of
 * the shipped package.  `bob-area1-actors-v3.s64f` already carries a
 * validated S64B v2 bank for MODEL_BLACK_BOBOMB (family ordinal 7, model
 * 0x00BC) -- 13 joints, two animations, 6,288 B -- baked by
 * `tools/saturn/actor_variant_bank.py`, which is generic, not Mario-specific.
 * So the bank did not have to be authored; it had to be *reached*.
 *
 * 13 parts is 16 + 13*24 = 328 B, which the 640 B cold class holds, and 13 is
 * inside the 20-part shared-workspace ceiling, so no class-plan or workspace
 * edit is needed for this actor.
 *
 * This is boot-time scaffolding, exactly like the Mario arm: it claims one
 * cold block, evaluates, and hands the block straight back.  Nothing here
 * runs per frame and nothing here draws. */
SM64_SATURN_CART_COLD
static void complete_actor_enemy_rigid_selftest(void)
{
    sm64_saturn_actor_bundle_view_t bundle;
    sm64_saturn_actor_bundle_variant_t variant;
    sm64_saturn_actor_bank_view_t bank;

    if (complete_actor_enemy_bundle == NULL ||
        complete_actor_enemy_bundle_bytes == 0U ||
        !sm64_saturn_actor_bundle_validate(complete_actor_enemy_bundle,
                                           complete_actor_enemy_bundle_bytes,
                                           &bundle) ||
        !sm64_saturn_actor_bundle_variant(&bundle, complete_actor_enemy_family,
                                          complete_actor_enemy_model,
                                          &variant) ||
        /* Resolve through the real identity-bound path, not a raw offset:
         * it rehashes the bank against the variant record and re-derives the
         * workspace requirements, so a bundle that has drifted from its bank
         * fails closed here rather than composing garbage. */
        !sm64_saturn_actor_bundle_resolve(&bundle, complete_actor_enemy_family,
                                          complete_actor_enemy_model,
                                          variant.source_hash_words[0],
                                          variant.source_hash_words, &bank))
        return;

    sm64_saturn_complete_actor_probe.enemy_bank_ready = 1U;
    sm64_saturn_complete_actor_probe.enemy_bank_joint_count =
        bank.bank.joint_count;
    sm64_saturn_complete_actor_probe.enemy_bank_animation_count =
        bank.bank.animation_count;

    /* Animation 0 at frame 0 and at a mid frame, then animation 1 at frame 0.
     *
     * The comparison frame is deliberately NOT n-1.  The Bob-omb's walk is a
     * closed loop: all eleven of its animated channels return to their
     * frame-0 value at frame 24, so a frame-0-against-n-1 comparison reports
     * an actor that never moves.  Frame 12 is inside the loop for the 25
     * frame animation and clamps to the last frame of the 10 frame one.
     *
     * No staged joint slice exists for this bank, so the joint table is read
     * straight out of CART -- correct for a boot-time exercise, and the exact
     * thing a per-frame caller would have to stage. */
    sm64_saturn_complete_actor_probe.enemy_rigid_selftest_mask =
        complete_actor_rigid_selftest(
            &bank, COMPLETE_ACTOR_OWNER_RIGID_ENEMY, 0, 12,
            bank.bank.animation_count > 1U ? (int16_t)1 : (int16_t)-1, NULL,
            &sm64_saturn_complete_actor_probe.enemy_rigid_part_count,
            &sm64_saturn_complete_actor_probe.enemy_rigid_pose_bytes);
}

void sm64_saturn_complete_actor_set_enemy_bundle(
    const void *bytes, uint32_t byte_count,
    uint16_t family_ordinal, uint16_t model_id)
{
    complete_actor_enemy_bundle = (const uint8_t *)bytes;
    complete_actor_enemy_bundle_bytes = byte_count;
    complete_actor_enemy_family = family_ordinal;
    complete_actor_enemy_model = model_id;
}

static void complete_actor_pools_bring_up(void)
{
    static const sm64_saturn_pose_pool_class_plan_t cold_plan[] = {
        { COMPLETE_ACTOR_COLD_SMALL_BYTES, 8U },
        { COMPLETE_ACTOR_COLD_LARGE_BYTES, 8U },
    };
    const sm64_saturn_pose_pool_class_plan_t hot_plan[] = {
        { (uint16_t)sizeof(complete_actor_pose_slot_t),
          COMPLETE_ACTOR_POSE_SLOT_COUNT },
    };
    uint16_t mask = 0U;
    uint8_t index;

    if (sm64_saturn_pose_pool_init(&complete_actor_hot_pool,
                                   complete_actor_hot_arena,
                                   (uint32_t)sizeof(complete_actor_hot_arena),
                                   hot_plan, 1U))
        mask |= POOL_SELFTEST_HOT_INIT;
    if (sm64_saturn_pose_pool_init(&complete_actor_cold_pool,
                                   complete_actor_cold_arena,
                                   (uint32_t)sizeof(complete_actor_cold_arena),
                                   cold_plan, 2U))
        mask |= POOL_SELFTEST_COLD_INIT;

    if ((mask & POOL_SELFTEST_HOT_INIT) != 0U) {
        uint8_t claimed = 0U;
        for (index = 0U; index < COMPLETE_ACTOR_POSE_SLOT_COUNT; index++) {
            complete_actor_mario_handle[index] = sm64_saturn_pose_pool_claim(
                &complete_actor_hot_pool, COMPLETE_ACTOR_OWNER_MARIO,
                (uint32_t)sizeof(complete_actor_pose_slot_t));
            if (sm64_saturn_pose_handle_is_valid(
                    complete_actor_mario_handle[index]))
                claimed++;
        }
        if (claimed == COMPLETE_ACTOR_POSE_SLOT_COUNT) {
            mask |= POOL_SELFTEST_MARIO_CLAIMED;
            complete_actor_pools_ready = 1U;
        }
    }
    /* Mario two generations must never share bytes; if they did he would
     * tear between an old and a new pose. */
    if (complete_actor_pools_ready != 0U) {
        complete_actor_pose_slot_t *const a = complete_actor_slot(0U);
        complete_actor_pose_slot_t *const b = complete_actor_slot(1U);
        if (a != NULL && b != NULL && a != b) {
            memset(a, 0, sizeof(*a));
            memset(b, 0, sizeof(*b));
            a->animation_id = 0x1234U;
            b->animation_id = 0x5678U;
            if (a->animation_id == 0x1234U && b->animation_id == 0x5678U)
                mask |= POOL_SELFTEST_MARIO_DISJOINT;
            memset(a, 0, sizeof(*a));
            memset(b, 0, sizeof(*b));
        }
    }

    if ((mask & POOL_SELFTEST_COLD_INIT) != 0U) {
        /* The rigid lane runs before the pool self-test drains the arena, so
         * it is exercised against a pool that still has blocks. */
        sm64_saturn_complete_actor_probe.rigid_selftest_mask =
            complete_actor_rigid_selftest(
                &complete_actor_bank, COMPLETE_ACTOR_OWNER_RIGID,
                COMPLETE_ACTOR_FALLBACK_ANIMATION_ID, 3, -1,
                complete_actor_slices_staged != 0U
                    ? complete_actor_staged_joints : NULL,
                &sm64_saturn_complete_actor_probe.rigid_part_count,
                &sm64_saturn_complete_actor_probe.rigid_pose_bytes);
        sm64_saturn_complete_actor_probe.rigid_part_bytes =
            (uint16_t)sizeof(sm64_saturn_rigid_part_t);
        complete_actor_enemy_rigid_selftest();
        mask |= complete_actor_pool_selftest();
    }

    /* Wire despawn to release.  Without this the cold pool leaks a block per
     * enemy per level. */
    sm64_saturn_geo_state_observer_set_despawn_sink(
        complete_actor_despawn_sink, NULL);

    sm64_saturn_complete_actor_probe.pool_selftest_mask = mask;
    sm64_saturn_complete_actor_probe.pool_ready = complete_actor_pools_ready;
    sm64_saturn_complete_actor_probe.pool_hot_live =
        complete_actor_hot_pool.live_count;
    sm64_saturn_complete_actor_probe.pool_hot_slot_bytes =
        (uint16_t)sizeof(complete_actor_pose_slot_t);
    sm64_saturn_complete_actor_probe.pool_cold_live =
        complete_actor_cold_pool.live_count;
    sm64_saturn_complete_actor_probe.pool_cold_peak_live =
        complete_actor_cold_pool.peak_live_count;
    sm64_saturn_complete_actor_probe.pool_cold_claim_failures =
        complete_actor_cold_pool.claim_failures;
    sm64_saturn_complete_actor_probe.pool_cold_stale_resolves =
        complete_actor_cold_pool.stale_resolves;
    sm64_saturn_complete_actor_probe.pool_cold_slot_count =
        complete_actor_cold_pool.slot_count;
    sm64_saturn_complete_actor_probe.pool_cold_arena_bytes =
        COMPLETE_ACTOR_COLD_ARENA_BYTES;
}

static const sm64_saturn_actor_bank_view_t *complete_bank(void)
{
    if (complete_actor_bank_ready == 0U) {
        complete_actor_bank_ready = (uint8_t)sm64_saturn_actor_bank_validate(
            sm64_saturn_mario_actor_bank_data, sm64_saturn_mario_actor_bank_size,
            &complete_actor_bank);
        if (complete_actor_bank_ready != 0U) {
            complete_actor_stage_slices();
            complete_actor_pools_bring_up();
            complete_actor_publish_probe();
        }
    }
    return complete_actor_bank_ready != 0U ? &complete_actor_bank : NULL;
}

static uint8_t complete_actor_evaluate(
    const sm64_saturn_mario_actor_snapshot_t *snapshot, uint8_t slot_index)
{
    const sm64_saturn_actor_bank_view_t *const bank = complete_bank();
    complete_actor_pose_slot_t *const slot = complete_actor_slot(slot_index);
    sm64_saturn_actor_pose_work_t work;
    const sm64_saturn_actor_vertex_t *const staged_vertices =
        complete_actor_slices_staged != 0U ? complete_actor_staged_vertices : NULL;
    const sm64_saturn_actor_joint_t *const staged_joints =
        complete_actor_slices_staged != 0U ? complete_actor_staged_joints : NULL;
    sm64_saturn_complete_actor_probe.evaluate_calls++;
    /* Fail closed: a slot that cannot be resolved is a released or never
     * granted claim.  Publishing nothing removes Mario for one frame, which
     * is the same degradation the shipped build takes when the bank is
     * unusable -- and it can never publish another tenant bytes. */
    if (bank == NULL || slot == NULL)
        return 0U;
    work.vertices = slot->vertices;
    work.light_intensity = slot->lights;
    work.joint_matrices_q16 = complete_actor_joint_workspace;
    work.vertex_capacity = SM64_MARIO_VERTEX_COUNT;
    work.joint_capacity = COMPLETE_ACTOR_JOINT_CAPACITY;
    work.light_capacity = SM64_MARIO_VERTEX_COUNT;
    if (!sm64_saturn_actor_pose_evaluate_staged(
            bank, snapshot->animation_id, snapshot->animation_frame,
            &work, staged_vertices, staged_joints, NULL,
            &slot->evaluated)) {
        /* Constitution 6: degrade at the smallest safe unit.  The reduced
         * bake already aliases every unbaked ID onto a baked neighbour, so
         * reaching here means the bank cannot serve the requested ID at all.
         * Show the neutral idle at the requested frame rather than removing
         * Mario for the frame -- a wrong pose is a degradation, a missing
         * Mario is a pop.  The requested frame is carried through so the
         * fallback still advances instead of freezing. */
        sm64_saturn_complete_actor_probe.fallback_hits++;
        sm64_saturn_complete_actor_probe.last_fallback_requested_id =
            (uint16_t)snapshot->animation_id;
        if (!sm64_saturn_actor_pose_evaluate_staged(
                bank, COMPLETE_ACTOR_FALLBACK_ANIMATION_ID,
                snapshot->animation_frame, &work, staged_vertices,
                staged_joints, NULL, &slot->evaluated)) {
            slot->valid = 0U;
            return 0U;
        }
    }
    /* The generic pose evaluator correctly transforms the complete bank but
     * deliberately initializes its lighting workspace uniformly.  Preserve
     * the existing, source-generated per-vertex Gouraud contract here until
     * the complete-animation bank carries its own matching light stream. */
    memcpy(slot->lights,
           sm64_mario_animation_light_intensity[
               slot->evaluated.frame % SM64_MARIO_ANIMATION_FRAME_COUNT],
           sizeof(slot->lights));
    slot->animation_id = slot->evaluated.animation_id;
    slot->animation_frame = slot->evaluated.frame;
    slot->valid = 1U;
    return 1U;
}
#endif

#if !SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
/* Feature-off keeps the symbol so the sourceboot scene owner links either
 * way; with no pose pool there is nothing to exercise. */
void sm64_saturn_complete_actor_set_enemy_bundle(
    const void *bytes, uint32_t byte_count,
    uint16_t family_ordinal, uint16_t model_id)
{
    (void)bytes; (void)byte_count; (void)family_ordinal; (void)model_id;
}

static uint8_t is_walking_family(int16_t animation_id)
{
    switch (animation_id) {
    case MARIO_ANIM_WALKING:
    case MARIO_ANIM_WALK_WITH_LIGHT_OBJ:
    case MARIO_ANIM_RUN_WITH_LIGHT_OBJ:
    case MARIO_ANIM_SLOW_WALK_WITH_LIGHT_OBJ:
    case MARIO_ANIM_WALK_PANTING:
    case MARIO_ANIM_WALK_WITH_HEAVY_OBJ:
        return 1U;
    default:
        return 0U;
    }
}
#endif

uint8_t sm64_saturn_mario_actor_snapshot(
    sm64_saturn_mario_actor_snapshot_t *snapshot)
{
    if (snapshot == NULL || gMarioState == NULL) {
        return 0U;
    }
    snapshot->position[0] = sm64_saturn_world_unit_from_float(gMarioState->pos[0]);
    snapshot->position[1] = sm64_saturn_world_unit_from_float(gMarioState->pos[1]);
    snapshot->position[2] = sm64_saturn_world_unit_from_float(gMarioState->pos[2]);
    snapshot->camera_position[0] = sm64_saturn_world_unit_from_float(gLakituState.pos[0]);
    snapshot->camera_position[1] = sm64_saturn_world_unit_from_float(gLakituState.pos[1]);
    snapshot->camera_position[2] = sm64_saturn_world_unit_from_float(gLakituState.pos[2]);
    snapshot->camera_focus[0] = sm64_saturn_world_unit_from_float(gLakituState.focus[0]);
    snapshot->camera_focus[1] = sm64_saturn_world_unit_from_float(gLakituState.focus[1]);
    snapshot->camera_focus[2] = sm64_saturn_world_unit_from_float(gLakituState.focus[2]);
    snapshot->camera_mode = gLakituState.mode;
    snapshot->camera_yaw = gLakituState.yaw;
    snapshot->camera_pitch = gLakituState.oldPitch;
    snapshot->yaw = gMarioState->faceAngle[1];
    snapshot->action = gMarioState->action;
    /* Mario-orientation probe (2026-09-03). He is reported as facing the wrong
     * way moving forward/back but not left/right. Four candidate causes have
     * been ruled out by measurement rather than argument -- the yaw rotation
     * here is term-for-term identical to the generic-actor kernel
     * (saturn_actor_meshlets.c actor_depth_fast) AND to castleviewer's
     * mario_world_point, gSaturnSineTableQ16 matches true sin/cos to 1e-5 over
     * the whole circle, and flipping the Mario back-face cull changed nothing.
     * So record the inputs instead of reasoning about them: whether the
     * SIMULATION is self-consistent (does faceAngle agree with the velocity
     * direction?) and whether the RENDER angle matches the sim (does the graph
     * object's gfx.angle agree with faceAngle?). Those two comparisons split
     * the remaining space cleanly, and neither can be answered from source.
     *
     * Written straight through to .lwram_bss for the same reason as
     * g_sm64_saturn_play_sound_probe: an external reader peeks it, so it must
     * not sit dirty in an SH-2 write-back cache line. */
    g_sm64_saturn_mario_orient_probe.magic = SM64_SATURN_MARIO_ORIENT_PROBE_MAGIC;
    g_sm64_saturn_mario_orient_probe.samples++;
    g_sm64_saturn_mario_orient_probe.face_angle = (uint32_t)(uint16_t)gMarioState->faceAngle[1];
    g_sm64_saturn_mario_orient_probe.gfx_angle = gMarioObject != NULL
        ? (uint32_t)(uint16_t)gMarioObject->header.gfx.angle[1] : 0xFFFFFFFFU;
    g_sm64_saturn_mario_orient_probe.camera_yaw = (uint32_t)(uint16_t)gLakituState.yaw;
    g_sm64_saturn_mario_orient_probe.vel_x =
        (uint32_t)sm64_saturn_world_unit_from_float(gMarioState->vel[0]);
    g_sm64_saturn_mario_orient_probe.vel_z =
        (uint32_t)sm64_saturn_world_unit_from_float(gMarioState->vel[2]);
    g_sm64_saturn_mario_orient_probe.forward_vel =
        (uint32_t)sm64_saturn_world_unit_from_float(gMarioState->forwardVel);
    g_sm64_saturn_mario_orient_probe.action = gMarioState->action;
    g_sm64_saturn_mario_orient_probe.pos_x = (uint32_t)snapshot->position[0];
    g_sm64_saturn_mario_orient_probe.pos_z = (uint32_t)snapshot->position[2];
    g_sm64_saturn_mario_orient_probe.cam_pos_x = (uint32_t)snapshot->camera_position[0];
    g_sm64_saturn_mario_orient_probe.cam_pos_z = (uint32_t)snapshot->camera_position[2];
    g_sm64_saturn_mario_orient_probe.cam_focus_x = (uint32_t)snapshot->camera_focus[0];
    g_sm64_saturn_mario_orient_probe.cam_focus_z = (uint32_t)snapshot->camera_focus[2];
    /* The simulation state is authoritative and can outlive its graph object
     * during level/bootstrap transitions. Keep the actor seam live in that
     * interval by using the neutral generated pose; once the graph object is
     * present again its animation id/frame are consumed verbatim. */
    if (gMarioObject != NULL) {
        snapshot->animation_id = gMarioObject->header.gfx.animInfo.animID;
        snapshot->animation_frame = gMarioObject->header.gfx.animInfo.animFrame;
        snapshot->area_index = gMarioObject->header.gfx.areaIndex;
    } else {
        snapshot->animation_id = 0;
        snapshot->animation_frame = 0;
        snapshot->area_index = -1;
    }
#if !SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    snapshot->walking_bank = is_walking_family(snapshot->animation_id);
#endif
    snapshot->valid = 1U;
    return 1U;
}

uint8_t sm64_saturn_mario_actor_pose(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    sm64_saturn_mario_actor_pose_t *pose)
{
    if (snapshot == NULL || pose == NULL || !snapshot->valid) {
        return 0U;
    }
    memset(pose, 0, sizeof(*pose));
#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    {
        const uint8_t index = complete_actor_next_pose_slot++ %
                              COMPLETE_ACTOR_POSE_SLOT_COUNT;
        const complete_actor_pose_slot_t *slot;
        if (!complete_actor_evaluate(snapshot, index))
            return 0U;
        slot = complete_actor_slot(index);
        if (slot == NULL)
            return 0U;
        pose->vertices = slot->evaluated.vertices;
        pose->light_intensity = slot->evaluated.light_intensity;
        pose->frame = slot->evaluated.frame;
        pose->frame_count = slot->evaluated.frame_count;
        pose->vertex_count = slot->evaluated.vertex_count;
        return 1U;
    }
#else
    pose->walking_bank = snapshot->walking_bank;
    pose->vertex_count = SM64_MARIO_VERTEX_COUNT;
    if (snapshot->walking_bank) {
        int32_t frame = snapshot->animation_frame;
        if (frame < 0) frame = 0;
        pose->frame_count = SM64_MARIO_WALKING_ANIMATION_FRAME_COUNT;
        pose->frame = (uint16_t)((uint32_t)frame % pose->frame_count);
        pose->vertices = sm64_mario_walking_animation_vertices[pose->frame];
        pose->light_intensity =
            sm64_mario_walking_animation_light_intensity[pose->frame];
    } else {
        int32_t frame = snapshot->animation_frame;
        if (frame < 0) frame = 0;
        pose->frame_count = SM64_MARIO_ANIMATION_FRAME_COUNT;
        pose->frame = (uint16_t)((uint32_t)frame % pose->frame_count);
        pose->vertices = sm64_mario_animation_vertices[pose->frame];
        pose->light_intensity = sm64_mario_animation_light_intensity[pose->frame];
    }
    return 1U;
#endif
}

uint8_t sm64_saturn_mario_actor_pose_selector(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    sm64_saturn_mario_pose_selector_t *selector)
{
    sm64_saturn_mario_actor_pose_t pose;

    if (selector == NULL) return 0U;
    memset(selector, 0, sizeof(*selector));
    selector->vertex_bank_id = 0U;
    selector->material_bank_id = 0U;
    selector->frame = 0U;
    selector->frame_count = 0U;
    selector->vertex_count = 0U;
    if (!sm64_saturn_mario_actor_pose(snapshot, &pose)) return 0U;
#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    selector->vertex_bank_id = SM64_SATURN_MARIO_VERTEX_BANK_COMPLETE;
    selector->pose_slot = (uint8_t)((complete_actor_next_pose_slot +
                                     COMPLETE_ACTOR_POSE_SLOT_COUNT - 1U) %
                                    COMPLETE_ACTOR_POSE_SLOT_COUNT);
#else
    selector->walking_bank = 0U;
    selector->vertex_bank_id = pose.walking_bank ?
        SM64_SATURN_MARIO_VERTEX_BANK_WALKING :
        SM64_SATURN_MARIO_VERTEX_BANK_NEUTRAL;
#endif
    selector->material_bank_id = SM64_SATURN_MARIO_MATERIAL_BANK_DEFAULT;
    selector->frame = pose.frame;
    selector->frame_count = pose.frame_count;
    selector->vertex_count = pose.vertex_count;
#if !SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    selector->walking_bank = pose.walking_bank;
#endif
    selector->valid = 1U;
    return 1U;
}

uint8_t sm64_saturn_mario_actor_pose_from_selector(
    const sm64_saturn_mario_pose_selector_t *selector,
    sm64_saturn_mario_actor_pose_t *pose)
{
    if (selector == NULL || pose == NULL || selector->valid == 0U) return 0U;
    memset(pose, 0, sizeof(*pose));
#if SATURN_FEATURE_COMPLETE_MARIO_ANIMATION
    {
        /* The selector carries an index into Mario own handle table, never a
         * raw pointer, so a render generation can only ever reach a slot that
         * is still granted to Mario: the pool generation check rejects a
         * released or re-let block. */
        const complete_actor_pose_slot_t *const slot =
            complete_actor_slot(selector->pose_slot);
        if (slot == NULL || slot->valid == 0U)
            return 0U;
        pose->vertices = slot->evaluated.vertices;
        pose->light_intensity = slot->evaluated.light_intensity;
        pose->frame = slot->evaluated.frame;
        pose->frame_count = slot->evaluated.frame_count;
        pose->vertex_count = slot->evaluated.vertex_count;
        return 1U;
    }
#else
    return 0U;
#endif
}
