#include <assert.h>
#include <stdint.h>

#include "saturn_render_snapshot.h"

static void test_rejects_zero_generation(void)
{
    sm64_saturn_render_snapshot_bank_t bank = {0};
    sm64_saturn_render_snapshot_t *slot = (sm64_saturn_render_snapshot_t *)1;

    assert(!sm64_saturn_render_snapshot_begin_write(&bank, 0U, &slot));
    assert(slot == NULL);
}

static void test_lifecycle_publishes_one_matching_generation(void)
{
    sm64_saturn_render_snapshot_bank_t bank = {0};
    sm64_saturn_render_snapshot_t *slot = NULL;

    assert(sm64_saturn_render_snapshot_begin_write(&bank, 7U, &slot));
    assert(slot != NULL);
    slot->camera.generation = 7U;
    slot->actor_generation = 7U;
    assert(sm64_saturn_render_snapshot_publish(&bank, slot));
    assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 7U) == slot);
    assert(!sm64_saturn_render_snapshot_retire(&bank, slot));
    assert(sm64_saturn_render_snapshot_complete(&bank, slot));
    assert(sm64_saturn_render_snapshot_retire(&bank, slot));
}

static void test_rejects_stale_and_mixed_generations(void)
{
    sm64_saturn_render_snapshot_bank_t bank = {0};
    sm64_saturn_render_snapshot_t *slot = NULL;

    assert(sm64_saturn_render_snapshot_begin_write(&bank, 9U, &slot));
    slot->camera.generation = 8U;
    slot->actor_generation = 9U;
    assert(!sm64_saturn_render_snapshot_publish(&bank, slot));
    slot->camera.generation = 9U;
    slot->actor_generation = 8U;
    assert(!sm64_saturn_render_snapshot_generation_valid(slot, 9U));
    assert(!sm64_saturn_render_snapshot_publish(&bank, slot));
    slot->actor_generation = 9U;
    assert(sm64_saturn_render_snapshot_generation_valid(slot, 9U));
    assert(!sm64_saturn_render_snapshot_generation_valid(slot, 8U));
    assert(sm64_saturn_render_snapshot_publish(&bank, slot));
    assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 8U) == NULL);
    assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 9U) == slot);
}

static void test_rejects_double_acquire_and_quarantined_reuse(void)
{
    sm64_saturn_render_snapshot_bank_t bank = {0};
    sm64_saturn_render_snapshot_t *slot = NULL;
    sm64_saturn_render_snapshot_t *other = NULL;

    assert(sm64_saturn_render_snapshot_begin_write(&bank, 11U, &slot));
    slot->camera.generation = 11U;
    slot->actor_generation = 11U;
    assert(sm64_saturn_render_snapshot_publish(&bank, slot));
    assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 11U) == slot);
    assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 11U) == NULL);
    assert(sm64_saturn_render_snapshot_quarantine(&bank, 11U));
    assert(!sm64_saturn_render_snapshot_complete(&bank, slot));
    assert(!sm64_saturn_render_snapshot_retire(&bank, slot));
    assert(sm64_saturn_render_snapshot_begin_write(&bank, 12U, &other));
    assert(other != slot);
    sm64_saturn_render_snapshot_reset(&bank);
    assert(!sm64_saturn_render_snapshot_begin_write(&bank, 13U, &other));
}

static void test_claim_lock_allows_one_contender(void)
{
    sm64_saturn_render_snapshot_bank_t bank = {0};
    sm64_saturn_render_snapshot_t *slot = NULL;
    volatile sm64_saturn_render_snapshot_release_t *release;

    assert(sm64_saturn_render_snapshot_begin_write(&bank, 19U, &slot));
    slot->camera.generation = 19U;
    slot->actor_generation = 19U;
    assert(sm64_saturn_render_snapshot_publish(&bank, slot));
    release = sm64_saturn_render_snapshot_release_uncached(&bank.slot[0]);
    assert(sm64_saturn_render_snapshot_release_claim_try(release));
    /* This is the second concurrent contender's acquire attempt while the
     * first has atomically claimed the release record. */
    assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 19U) == NULL);
    sm64_saturn_render_snapshot_release_claim_release(release);
    assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 19U) == slot);
}

static void test_quarantine_waits_for_claim_then_stays_terminal(void)
{
    sm64_saturn_render_snapshot_bank_t bank = {0};
    sm64_saturn_render_snapshot_t *slot = NULL;
    volatile sm64_saturn_render_snapshot_release_t *release;

    assert(sm64_saturn_render_snapshot_begin_write(&bank, 23U, &slot));
    slot->camera.generation = 23U;
    slot->actor_generation = 23U;
    assert(sm64_saturn_render_snapshot_publish(&bank, slot));
    release = sm64_saturn_render_snapshot_release_uncached(&bank.slot[0]);
    assert(sm64_saturn_render_snapshot_release_claim_try(release));
    /* A terminal transition never writes around an in-flight READY claimant. */
    assert(!sm64_saturn_render_snapshot_quarantine(&bank, 23U));
    assert(release->state == SM64_SATURN_RENDER_SNAPSHOT_READY);
    sm64_saturn_render_snapshot_release_claim_release(release);
    assert(sm64_saturn_render_snapshot_quarantine(&bank, 23U));
    assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 23U) == NULL);
    assert(!sm64_saturn_render_snapshot_complete(&bank, slot));
    assert(!sm64_saturn_render_snapshot_retire(&bank, slot));
}

int main(void)
{
    test_rejects_zero_generation();
    test_lifecycle_publishes_one_matching_generation();
    test_rejects_stale_and_mixed_generations();
    test_rejects_double_acquire_and_quarantined_reuse();
    test_claim_lock_allows_one_contender();
    test_quarantine_waits_for_claim_then_stays_terminal();
    return 0;
}
