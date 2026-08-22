/* Host fixture for the actor pose-slot pool.
 *
 * The load-bearing property is the last one: two tenants holding slots at the
 * same time must never see each other's bytes.  That is the bug this pool
 * exists to prevent and the one that would ship silently, so it is checked by
 * writing a distinct pattern over every byte of every live slot and reading
 * all of them back afterwards, not by comparing pointers.
 */
#include <stdio.h>
#include <string.h>

#include "saturn_actor_pose_pool.h"

static int failures;

#define CHECK(cond, label)                                                    \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s (%s:%d)\n", (label), __FILE__, __LINE__);         \
            failures++;                                                       \
        }                                                                     \
    } while (0)

/* 3 x 64 + 2 x 256 + 2 x 512 = 192 + 512 + 1024 = 1728 bytes. */
#define ARENA_BYTES 4096U
static uint8_t arena[ARENA_BYTES];

static const sm64_saturn_pose_pool_class_plan_t plan[] = {
    { 64U, 3U }, { 256U, 2U }, { 512U, 2U },
};
#define PLAN_COUNT ((uint16_t)(sizeof(plan) / sizeof(plan[0])))
#define PLAN_BLOCKS 7U
#define PLAN_BYTES 1728U

static void fill(sm64_saturn_pose_pool_t *pool,
                 sm64_saturn_pose_handle_t handle, uint8_t pattern,
                 uint32_t bytes)
{
    uint8_t *const p = (uint8_t *)sm64_saturn_pose_pool_resolve(pool, handle);
    if (p == NULL) {
        printf("FAIL fill: handle did not resolve\n");
        failures++;
        return;
    }
    memset(p, pattern, bytes);
}

static int check_fill(sm64_saturn_pose_pool_t *pool,
                      sm64_saturn_pose_handle_t handle, uint8_t pattern,
                      uint32_t bytes)
{
    const uint8_t *const p =
        (const uint8_t *)sm64_saturn_pose_pool_resolve(pool, handle);
    uint32_t i;
    if (p == NULL)
        return 0;
    for (i = 0U; i < bytes; i++) {
        if (p[i] != pattern)
            return 0;
    }
    return 1;
}

static void test_init_contract(void)
{
    sm64_saturn_pose_pool_t pool;
    sm64_saturn_pose_pool_class_plan_t bad[3];

    CHECK(sm64_saturn_pose_pool_init(&pool, arena, ARENA_BYTES, plan,
                                     PLAN_COUNT),
          "init accepts a well-formed plan");
    CHECK(pool.slot_count == PLAN_BLOCKS, "init creates every planned block");
    CHECK(pool.arena_used == PLAN_BYTES, "init consumes exactly the plan bytes");
    CHECK(pool.live_count == 0U, "init leaves nothing live");
    CHECK(pool.class_count == PLAN_COUNT, "init records every class");

    /* Descending plan: `claim` only widens by class index, so an unsorted
     * plan would silently hand out an undersized block. */
    bad[0] = plan[2];
    bad[1] = plan[1];
    bad[2] = plan[0];
    CHECK(!sm64_saturn_pose_pool_init(&pool, arena, ARENA_BYTES, bad, 3U),
          "init rejects a descending plan");
    CHECK(pool.ready == 0U, "a rejected plan leaves the pool unusable");

    /* Arena too small for the plan. */
    CHECK(!sm64_saturn_pose_pool_init(&pool, arena, 128U, plan, PLAN_COUNT),
          "init rejects a plan that does not fit the arena");

    /* Unaligned class size. */
    bad[0].bytes = 66U;
    bad[0].count = 1U;
    CHECK(!sm64_saturn_pose_pool_init(&pool, arena, ARENA_BYTES, bad, 1U),
          "init rejects an unaligned class size");

    CHECK(!sm64_saturn_pose_pool_init(&pool, NULL, ARENA_BYTES, plan,
                                      PLAN_COUNT),
          "init rejects a NULL arena");
}

static void test_claim_release_reuse(void)
{
    sm64_saturn_pose_pool_t pool;
    sm64_saturn_pose_handle_t a, b, c;
    void *pa;

    CHECK(sm64_saturn_pose_pool_init(&pool, arena, ARENA_BYTES, plan,
                                     PLAN_COUNT), "reuse: init");

    a = sm64_saturn_pose_pool_claim(&pool, 0x1001U, 40U);
    CHECK(sm64_saturn_pose_handle_is_valid(a), "claim of 40 B succeeds");
    CHECK(pool.slot[a.index - 1U].block_bytes == 64U,
          "claim of 40 B lands in the 64 B class, not a larger one");
    CHECK(pool.live_count == 1U, "claim raises live_count");
    pa = sm64_saturn_pose_pool_resolve(&pool, a);
    CHECK(pa != NULL, "a live handle resolves");

    CHECK(sm64_saturn_pose_pool_release(&pool, a), "release of a live handle");
    CHECK(pool.live_count == 0U, "release lowers live_count");
    CHECK(sm64_saturn_pose_pool_resolve(&pool, a) == NULL,
          "a released handle no longer resolves");
    CHECK(pool.stale_resolves == 1U, "a stale resolve is counted");
    CHECK(!sm64_saturn_pose_pool_release(&pool, a),
          "double release is rejected");

    b = sm64_saturn_pose_pool_claim(&pool, 0x1002U, 40U);
    CHECK(sm64_saturn_pose_handle_is_valid(b), "the block is reusable");
    CHECK(sm64_saturn_pose_pool_resolve(&pool, b) == pa,
          "reuse returns the same block bytes");
    CHECK(sm64_saturn_pose_pool_resolve(&pool, a) == NULL,
          "the old handle stays dead after the block is re-let");
    CHECK(b.generation != a.generation,
          "re-letting a block advances its generation");

    /* find / release_owner */
    c = sm64_saturn_pose_pool_find(&pool, 0x1002U);
    CHECK(sm64_saturn_pose_handle_is_valid(c), "find locates a live owner");
    CHECK(sm64_saturn_pose_pool_resolve(&pool, c) == pa,
          "find returns a usable handle");
    CHECK(!sm64_saturn_pose_handle_is_valid(
              sm64_saturn_pose_pool_find(&pool, 0x1001U)),
          "find does not report a released owner");
    CHECK(sm64_saturn_pose_pool_release_owner(&pool, 0x1002U) == 1U,
          "release_owner releases the owner's slot");
    CHECK(pool.live_count == 0U, "release_owner empties the pool");
}

/* The whole point: every live slot owns its bytes exclusively. */
static void test_full_arena_disjointness(void)
{
    sm64_saturn_pose_pool_t pool;
    sm64_saturn_pose_handle_t held[PLAN_BLOCKS];
    uint32_t extent[PLAN_BLOCKS];
    uint16_t n = 0U;
    uint16_t i;

    CHECK(sm64_saturn_pose_pool_init(&pool, arena, ARENA_BYTES, plan,
                                     PLAN_COUNT), "disjointness: init");
    memset(arena, 0, sizeof(arena));

    /* Claim every block the plan created, smallest requests first so the
     * class-escalation path is exercised too. */
    for (i = 0U; i < PLAN_BLOCKS; i++) {
        const uint32_t want = (i < 3U) ? 64U : ((i < 5U) ? 256U : 512U);
        sm64_saturn_pose_handle_t h =
            sm64_saturn_pose_pool_claim(&pool, 0x2000U + i, want);
        if (!sm64_saturn_pose_handle_is_valid(h))
            break;
        held[n] = h;
        extent[n] = pool.slot[h.index - 1U].block_bytes;
        /* Write over the WHOLE block, not just the requested extent, so an
         * overlap of even one byte is destructive and observable. */
        fill(&pool, h, (uint8_t)(0x40U + i), extent[n]);
        n++;
    }
    CHECK(n == PLAN_BLOCKS, "every planned block is claimable");

    /* Now read every one of them back. */
    for (i = 0U; i < n; i++) {
        char label[64];
        snprintf(label, sizeof(label),
                 "slot %u still holds its own bytes (no aliasing)",
                 (unsigned)i);
        CHECK(check_fill(&pool, held[i], (uint8_t)(0x40U + i), extent[i]),
              label);
    }

    /* Exhaustion. */
    {
        const uint16_t failures_before = pool.claim_failures;
        sm64_saturn_pose_handle_t over =
            sm64_saturn_pose_pool_claim(&pool, 0x3000U, 64U);
        CHECK(!sm64_saturn_pose_handle_is_valid(over),
              "claim on an exhausted pool returns the invalid handle");
        CHECK(pool.claim_failures == (uint16_t)(failures_before + 1U),
              "exhaustion is counted");
        CHECK(sm64_saturn_pose_pool_largest_free_extent(&pool) == 0U,
              "an exhausted pool reports no free extent");
    }
    /* ... and exhaustion must not have disturbed anybody. */
    for (i = 0U; i < n; i++) {
        CHECK(check_fill(&pool, held[i], (uint8_t)(0x40U + i), extent[i]),
              "a failed claim does not evict or corrupt a live tenant");
    }
    CHECK(pool.peak_live_count == PLAN_BLOCKS, "peak occupancy is recorded");

    /* Oversize request: larger than any class. */
    CHECK(!sm64_saturn_pose_handle_is_valid(
              sm64_saturn_pose_pool_claim(&pool, 0x3001U, 4096U)),
          "a request larger than every class fails");

    /* Release one small block; the freed block must be re-lettable and must
     * not overlap anything still held. */
    CHECK(sm64_saturn_pose_pool_release(&pool, held[0]), "release one tenant");
    {
        sm64_saturn_pose_handle_t fresh =
            sm64_saturn_pose_pool_claim(&pool, 0x4000U, 64U);
        CHECK(sm64_saturn_pose_handle_is_valid(fresh),
              "a released block is immediately re-lettable");
        fill(&pool, fresh, 0xEEU, 64U);
        for (i = 1U; i < n; i++) {
            CHECK(check_fill(&pool, held[i], (uint8_t)(0x40U + i), extent[i]),
                  "re-letting a freed block does not touch a live tenant");
        }
        CHECK(check_fill(&pool, fresh, 0xEEU, 64U),
              "the re-let block holds its new owner's bytes");
        CHECK(sm64_saturn_pose_pool_resolve(&pool, held[0]) == NULL,
              "the previous owner's handle is dead after re-letting");
    }
}

/* Two tenants of DIFFERENT sizes, held simultaneously across simulated
 * frames, each rewriting its own slot every frame -- the shape the runtime
 * actually uses. */
static void test_simultaneous_tenants_across_frames(void)
{
    sm64_saturn_pose_pool_t pool;
    sm64_saturn_pose_handle_t big, small_a, small_b;
    uint16_t frame;

    CHECK(sm64_saturn_pose_pool_init(&pool, arena, ARENA_BYTES, plan,
                                     PLAN_COUNT), "frames: init");
    big = sm64_saturn_pose_pool_claim(&pool, 0xA1U, 512U);
    small_a = sm64_saturn_pose_pool_claim(&pool, 0xB2U, 200U);
    small_b = sm64_saturn_pose_pool_claim(&pool, 0xC3U, 60U);
    CHECK(sm64_saturn_pose_handle_is_valid(big) &&
              sm64_saturn_pose_handle_is_valid(small_a) &&
              sm64_saturn_pose_handle_is_valid(small_b),
          "three tenants of three sizes hold slots at once");
    CHECK(pool.live_count == 3U, "three slots are live");

    for (frame = 0U; frame < 16U; frame++) {
        const uint8_t pa = (uint8_t)(0x10U + frame);
        const uint8_t pb = (uint8_t)(0x50U + frame);
        const uint8_t pc = (uint8_t)(0x90U + frame);
        fill(&pool, big, pa, 512U);
        fill(&pool, small_a, pb, 256U);
        fill(&pool, small_b, pc, 64U);
        CHECK(check_fill(&pool, big, pa, 512U) &&
                  check_fill(&pool, small_a, pb, 256U) &&
                  check_fill(&pool, small_b, pc, 64U),
              "each tenant reads back its own frame pattern");
    }

    /* Despawn the middle tenant mid-session; the others are untouched. */
    {
        const uint8_t pa = 0x77U;
        const uint8_t pc = 0x33U;
        fill(&pool, big, pa, 512U);
        fill(&pool, small_b, pc, 64U);
        CHECK(sm64_saturn_pose_pool_release_owner(&pool, 0xB2U) == 1U,
              "an actor leaving releases exactly its own slot");
        CHECK(pool.live_count == 2U, "the survivors keep their slots");
        CHECK(check_fill(&pool, big, pa, 512U) &&
                  check_fill(&pool, small_b, pc, 64U),
              "a despawn does not disturb the surviving tenants");
        CHECK(sm64_saturn_pose_pool_resolve(&pool, small_a) == NULL,
              "the departed actor's handle is dead");
    }
}

int main(void)
{
    test_init_contract();
    test_claim_release_reuse();
    test_full_arena_disjointness();
    test_simultaneous_tenants_across_frames();

    if (failures != 0) {
        printf("actor pose pool: %d failure(s)\n", failures);
        return 1;
    }
    printf("actor pose pool: all checks passed\n");
    return 0;
}
