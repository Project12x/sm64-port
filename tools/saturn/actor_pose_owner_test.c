/* Host fixture for the pose-slot owner key and the despawn release path.
 *
 * Two things are under test and they are the same thing seen from two ends:
 *
 *   1. the owner key an actor holds its pose slot under is stable while the
 *      actor lives and never aliases another actor, living or dead; and
 *   2. `sm64_saturn_pose_pool_release_owner` is actually reached when an
 *      actor despawns, so the pool does not leak to exhaustion inside a
 *      level.
 *
 * The leak arm is the reason this file exists.  `release_owner` shipped
 * tested and uncalled (r2-pose-slot-pooling.md section 8.5): the pool would
 * have run out of blocks the first time a level killed and respawned enough
 * enemies, and nothing would have reported it as anything worse than actors
 * quietly going stiff.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "saturn_actor_instance.h"
#include "saturn_actor_pose_owner.h"
#include "saturn_actor_pose_pool.h"
#include "saturn_actor_rigid_pose.h"

static int g_failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static void check_eq_u32(uint32_t got, uint32_t want, const char *what)
{
    if (got != want) {
        fprintf(stderr, "FAIL: %s (got %lu, want %lu)\n", what,
                (unsigned long)got, (unsigned long)want);
        g_failures++;
    }
}

/* ---- the scene under test --------------------------------------------- */

#define ARENA_BYTES 6144U
#define SMALL_BYTES 256U
#define LARGE_BYTES 640U
#define SMALL_COUNT 8U
#define LARGE_COUNT 6U

static uint8_t g_arena[ARENA_BYTES];
static sm64_saturn_pose_pool_t g_pool;
static sm64_saturn_geo_state_observer_t g_observer;
static uint16_t g_released_total;
static uint16_t g_release_calls;

/* The production wiring, in the shape the bridge uses it: the observer
 * reports a live->dead edge, and the sink turns (slot, incarnation) into the
 * instance key and releases whatever that key holds. */
static void despawn_sink(uint16_t pool_slot, uint16_t incarnation,
                         void *context)
{
    sm64_saturn_pose_pool_t *pool = (sm64_saturn_pose_pool_t *)context;
    const uint32_t owner =
        sm64_saturn_pose_owner_compose(pool_slot, incarnation);
    g_release_calls++;
    g_released_total = (uint16_t)(g_released_total +
                                  sm64_saturn_pose_pool_release_owner(pool,
                                                                      owner));
}

static void reset_scene(void)
{
    static const sm64_saturn_pose_pool_class_plan_t plan[] = {
        { SMALL_BYTES, SMALL_COUNT },
        { LARGE_BYTES, LARGE_COUNT },
    };
    memset(g_arena, 0, sizeof(g_arena));
    check(sm64_saturn_pose_pool_init(&g_pool, g_arena, ARENA_BYTES, plan, 2U),
          "the cold class plan partitions its arena");
    sm64_saturn_geo_state_observer_init(&g_observer,
                                        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE);
    sm64_saturn_geo_state_observer_set_despawn_sink(despawn_sink, &g_pool);
    g_released_total = 0U;
    g_release_calls = 0U;
}

static void observe(uint16_t pool_slot)
{
    sm64_saturn_actor_source_observation_t observation;
    memset(&observation, 0, sizeof(observation));
    observation.pool_slot = pool_slot;
    observation.family_id = 1U;
    observation.model_id = 1U;
    observation.actor_bank_id = 1U;
    observation.active = 1U;
    observation.render_active = 1U;
    check(sm64_saturn_geo_state_observer_begin_object(&g_observer,
                                                      &observation),
          "an active object is observed");
    check(sm64_saturn_geo_state_observer_end_object(&g_observer),
          "the observation closes");
}

/* The key the runtime would publish for the slot's current tenant. */
static uint32_t live_owner_key(uint16_t pool_slot)
{
    return sm64_saturn_pose_owner_compose(pool_slot,
                                          g_observer.incarnation[pool_slot]);
}

/* ---- key-space partition ---------------------------------------------- */

static void test_key_space(void)
{
    check_eq_u32(sm64_saturn_pose_owner_compose(7U, 3U), 0x00030007UL,
                 "the key is incarnation<<16 | pool_slot");
    /* This is the coupling the whole design rests on: the pose owner key and
     * the snapshot's instance_key are the same 32 bits, derived the same way
     * in saturn_actor_instance.c. */
    check_eq_u32(sm64_saturn_pose_owner_from_instance_key(0x00030007UL),
                 sm64_saturn_pose_owner_compose(7U, 3U),
                 "the instance key is the owner key");

    check(sm64_saturn_pose_owner_is_generic(sm64_saturn_pose_owner_compose(
              SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY - 1U, 1U)),
          "the last source pool slot mints a generic key");
    check(!sm64_saturn_pose_owner_is_generic(0UL),
          "OWNER_NONE is not a generic key");
    check(!sm64_saturn_pose_owner_is_generic(
              sm64_saturn_pose_owner_compose(7U, 0U)),
          "incarnation 0 is not a live actor");
    check(!sm64_saturn_pose_owner_is_generic(0x4D41524FUL),
          "Mario's reserved key is not in the generic space");

    /* The reserved band must be unreachable from any pool slot at any
     * incarnation -- including the wrap value, which a long-lived slot can
     * genuinely reach. */
    {
        uint32_t incarnation;
        int collided = 0;
        for (incarnation = 1UL; incarnation <= 0xFFFFUL; incarnation++) {
            uint16_t ordinal;
            for (ordinal = 0U; ordinal < 4U; ordinal++) {
                const uint32_t reserved = sm64_saturn_pose_owner_reserved(
                    (uint16_t)incarnation, ordinal);
                if (sm64_saturn_pose_owner_is_generic(reserved)) collided = 1;
            }
        }
        check(!collided,
              "no reserved key is reachable from the generic key space");
    }
}

/* ---- stability -------------------------------------------------------- */

static void test_key_is_stable_while_alive(void)
{
    uint32_t first;
    int frame;
    reset_scene();

    sm64_saturn_geo_state_observer_begin_frame(&g_observer, 1U);
    observe(11U);
    sm64_saturn_geo_state_observer_end_frame(&g_observer);
    first = live_owner_key(11U);
    check(sm64_saturn_pose_owner_is_generic(first),
          "a live actor's key is a generic key");

    /* Eight consecutive frames, which is four times the render pipeline's
     * two in-flight generations. */
    for (frame = 2; frame <= 9; frame++) {
        sm64_saturn_geo_state_observer_begin_frame(&g_observer,
                                                   (uint32_t)frame);
        observe(11U);
        sm64_saturn_geo_state_observer_end_frame(&g_observer);
        check_eq_u32(live_owner_key(11U), first,
                     "a continuously live actor keeps one key");
    }
    check_eq_u32(g_release_calls, 0U,
                 "a live actor is never reported as despawned");

    /* Despawn, then respawn into the same recycled slot. */
    sm64_saturn_geo_state_observer_begin_frame(&g_observer, 10U);
    sm64_saturn_geo_state_observer_end_frame(&g_observer);
    check_eq_u32(g_release_calls, 1U, "the despawn edge fires exactly once");

    sm64_saturn_geo_state_observer_begin_frame(&g_observer, 11U);
    observe(11U);
    sm64_saturn_geo_state_observer_end_frame(&g_observer);
    check(live_owner_key(11U) != first,
          "a respawn into a recycled slot mints a different key");
    check(sm64_saturn_pose_owner_is_generic(live_owner_key(11U)),
          "the respawned actor's key is still generic");
}

static void test_keys_do_not_alias_between_actors(void)
{
    uint16_t left, right;
    uint32_t keys[8];
    reset_scene();
    sm64_saturn_geo_state_observer_begin_frame(&g_observer, 1U);
    for (left = 0U; left < 8U; left++) observe((uint16_t)(left * 5U + 1U));
    sm64_saturn_geo_state_observer_end_frame(&g_observer);
    for (left = 0U; left < 8U; left++)
        keys[left] = live_owner_key((uint16_t)(left * 5U + 1U));
    for (left = 0U; left < 8U; left++) {
        for (right = (uint16_t)(left + 1U); right < 8U; right++)
            check(keys[left] != keys[right],
                  "two simultaneously live actors have different keys");
    }
}

/* ---- the leak ---------------------------------------------------------- */

/* Every actor stripes its own block, so a release that frees the wrong
 * tenant's bytes is destructive and visible, not merely miscounted. */
static void stripe(sm64_saturn_pose_handle_t handle, uint8_t pattern)
{
    uint8_t *bytes = (uint8_t *)sm64_saturn_pose_pool_resolve(&g_pool, handle);
    uint16_t index;
    if (bytes == NULL) {
        check(0, "a freshly claimed block resolves");
        return;
    }
    for (index = 0U; index < 32U; index++) bytes[index] = pattern;
}

static int stripe_ok(sm64_saturn_pose_handle_t handle, uint8_t pattern)
{
    const uint8_t *bytes =
        (const uint8_t *)sm64_saturn_pose_pool_resolve(&g_pool, handle);
    uint16_t index;
    if (bytes == NULL) return 0;
    for (index = 0U; index < 32U; index++) {
        if (bytes[index] != pattern) return 0;
    }
    return 1;
}

#define WAVE_ENEMIES 6U
#define WAVE_CYCLES 40U

/* The failure this task exists to prevent: spawn a wave, claim a slot each,
 * kill the wave, repeat.  Without the despawn release the pool is empty after
 * the third cycle and every later enemy is stiff. */
static void test_spawn_despawn_does_not_leak(void)
{
    uint16_t cycle;
    uint16_t baseline_live;
    uint16_t worst_peak = 0U;
    uint32_t generation = 1UL;

    reset_scene();
    baseline_live = g_pool.live_count;
    check_eq_u32(baseline_live, 0U, "the pool starts empty");

    for (cycle = 0U; cycle < WAVE_CYCLES; cycle++) {
        sm64_saturn_pose_handle_t handles[WAVE_ENEMIES];
        uint32_t keys[WAVE_ENEMIES];
        uint16_t enemy;

        /* --- the wave spawns and animates for three frames --- */
        sm64_saturn_geo_state_observer_begin_frame(&g_observer, generation++);
        for (enemy = 0U; enemy < WAVE_ENEMIES; enemy++)
            observe((uint16_t)(20U + enemy));
        sm64_saturn_geo_state_observer_end_frame(&g_observer);

        for (enemy = 0U; enemy < WAVE_ENEMIES; enemy++) {
            keys[enemy] = live_owner_key((uint16_t)(20U + enemy));
            handles[enemy] = sm64_saturn_pose_pool_claim(
                &g_pool, keys[enemy],
                SM64_SATURN_RIGID_POSE_BYTES(enemy < 3U ? 8U : 18U));
            check(sm64_saturn_pose_handle_is_valid(handles[enemy]),
                  "an enemy claims a pose slot");
            stripe(handles[enemy], (uint8_t)(0x40U + enemy));
        }
        for (enemy = 0U; enemy < WAVE_ENEMIES; enemy++) {
            check(stripe_ok(handles[enemy], (uint8_t)(0x40U + enemy)),
                  "each enemy reads back its own pose bytes");
            /* `find` is how a producer re-locates its slot without carrying a
             * handle through an immutable snapshot; it must agree. */
            check(sm64_saturn_pose_pool_resolve(
                      &g_pool,
                      sm64_saturn_pose_pool_find(&g_pool, keys[enemy])) ==
                      sm64_saturn_pose_pool_resolve(&g_pool, handles[enemy]),
                  "find re-locates the actor's own slot");
        }
        if (g_pool.peak_live_count > worst_peak)
            worst_peak = g_pool.peak_live_count;

        /* --- the wave is killed: not observed this frame --- */
        sm64_saturn_geo_state_observer_begin_frame(&g_observer, generation++);
        sm64_saturn_geo_state_observer_end_frame(&g_observer);

        check_eq_u32(g_pool.live_count, baseline_live,
                     "the live slot count returns to baseline after a wave");
        for (enemy = 0U; enemy < WAVE_ENEMIES; enemy++) {
            check(sm64_saturn_pose_pool_resolve(&g_pool, handles[enemy]) ==
                      NULL,
                  "a despawned actor's handle stops resolving");
            check(!sm64_saturn_pose_handle_is_valid(
                      sm64_saturn_pose_pool_find(&g_pool, keys[enemy])),
                  "a despawned actor's key finds nothing");
        }
    }

    check_eq_u32(g_pool.live_count, baseline_live,
                 "no slot leaked across 40 spawn/despawn cycles");
    check_eq_u32(worst_peak, WAVE_ENEMIES,
                 "peak live count is one wave, not a creeping total");
    check_eq_u32(g_released_total, WAVE_ENEMIES * WAVE_CYCLES,
                 "every claimed slot was released by its own despawn");
    check_eq_u32(g_pool.claim_failures, 0U,
                 "no claim was ever refused for exhaustion");
}

/* A despawn must release exactly its own actor and leave the neighbours
 * holding their bytes. */
static void test_despawn_is_not_collateral(void)
{
    sm64_saturn_pose_handle_t stayer, leaver;
    uint32_t stayer_key, leaver_key;

    reset_scene();
    sm64_saturn_geo_state_observer_begin_frame(&g_observer, 1U);
    observe(30U);
    observe(31U);
    sm64_saturn_geo_state_observer_end_frame(&g_observer);
    stayer_key = live_owner_key(30U);
    leaver_key = live_owner_key(31U);
    stayer = sm64_saturn_pose_pool_claim(&g_pool, stayer_key, SMALL_BYTES);
    leaver = sm64_saturn_pose_pool_claim(&g_pool, leaver_key, LARGE_BYTES);
    stripe(stayer, 0xA5U);
    stripe(leaver, 0x5AU);

    /* Only slot 30 is observed this frame: 31 despawns. */
    sm64_saturn_geo_state_observer_begin_frame(&g_observer, 2U);
    observe(30U);
    sm64_saturn_geo_state_observer_end_frame(&g_observer);

    check_eq_u32(g_release_calls, 1U, "exactly one despawn was reported");
    check(sm64_saturn_pose_pool_resolve(&g_pool, leaver) == NULL,
          "the departed actor's slot is gone");
    check(stripe_ok(stayer, 0xA5U),
          "the survivor still holds its own pose bytes");
    check_eq_u32(g_pool.live_count, 1U, "one tenant remains");

    /* And the recycled slot's new tenant must not be able to reach the dead
     * actor's block through the dead key. */
    sm64_saturn_geo_state_observer_begin_frame(&g_observer, 3U);
    observe(30U);
    observe(31U);
    sm64_saturn_geo_state_observer_end_frame(&g_observer);
    check(live_owner_key(31U) != leaver_key,
          "the recycled slot's new tenant has a new key");
    check(!sm64_saturn_pose_handle_is_valid(
              sm64_saturn_pose_pool_find(&g_pool, leaver_key)),
          "the dead key finds nothing after the slot is recycled");
}

/* Without the wiring the pool is exhausted quickly; this states the size of
 * the defect rather than merely asserting the fix. */
static void test_unwired_pool_exhausts(void)
{
    uint16_t cycle;
    uint16_t failures = 0U;
    uint32_t generation = 1UL;

    reset_scene();
    sm64_saturn_geo_state_observer_set_despawn_sink(NULL, NULL);
    for (cycle = 0U; cycle < WAVE_CYCLES; cycle++) {
        uint16_t enemy;
        sm64_saturn_geo_state_observer_begin_frame(&g_observer, generation++);
        for (enemy = 0U; enemy < WAVE_ENEMIES; enemy++)
            observe((uint16_t)(20U + enemy));
        sm64_saturn_geo_state_observer_end_frame(&g_observer);
        for (enemy = 0U; enemy < WAVE_ENEMIES; enemy++) {
            if (!sm64_saturn_pose_handle_is_valid(sm64_saturn_pose_pool_claim(
                    &g_pool, live_owner_key((uint16_t)(20U + enemy)),
                    SMALL_BYTES)))
                failures++;
        }
        sm64_saturn_geo_state_observer_begin_frame(&g_observer, generation++);
        sm64_saturn_geo_state_observer_end_frame(&g_observer);
    }
    check(failures > 0U,
          "an unwired pool really does exhaust, so the fix is load-bearing");
    /* Restore the wiring for anything that runs after this. */
    sm64_saturn_geo_state_observer_set_despawn_sink(despawn_sink, &g_pool);
}

int main(void)
{
    test_key_space();
    test_key_is_stable_while_alive();
    test_keys_do_not_alias_between_actors();
    test_spawn_despawn_does_not_leak();
    test_despawn_is_not_collateral();
    test_unwired_pool_exhausts();

    if (g_failures != 0) {
        fprintf(stderr, "pose-owner fixture: %d failing assertions\n",
                g_failures);
        return 1;
    }
    printf("pose-owner fixture: all checks passed\n");
    return 0;
}
