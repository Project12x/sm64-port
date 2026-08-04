#include "saturn_render_snapshot.h"

#include <string.h>

static void sm64_saturn_render_snapshot_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static sm64_saturn_render_snapshot_slot_t *snapshot_slot(
    sm64_saturn_render_snapshot_bank_t *bank,
    const sm64_saturn_render_snapshot_t *snapshot)
{
    uint32_t index;

    if (bank == NULL || snapshot == NULL) return NULL;
    for (index = 0U; index < 2U; index++) {
        if (snapshot == sm64_saturn_render_snapshot_owner_payload(
                &bank->slot[index])) return &bank->slot[index];
    }
    return NULL;
}

void sm64_saturn_render_snapshot_reset(sm64_saturn_render_snapshot_bank_t *bank)
{
    uint32_t index;

    if (bank == NULL) return;
    for (index = 0U; index < 2U; index++) {
        volatile sm64_saturn_render_snapshot_release_t *const release =
            sm64_saturn_render_snapshot_release_uncached(&bank->slot[index]);
        sm64_saturn_render_snapshot_t *const payload =
            sm64_saturn_render_snapshot_owner_payload(&bank->slot[index]);
        if (release->state != SM64_SATURN_RENDER_SNAPSHOT_FREE) continue;
        if (!sm64_saturn_render_snapshot_release_claim_try(release)) continue;
        if (release->state != SM64_SATURN_RENDER_SNAPSHOT_FREE) {
            sm64_saturn_render_snapshot_release_claim_release(release);
            continue;
        }
        memset(payload, 0, sizeof(*payload));
        release->generation = 0U;
        release->state = SM64_SATURN_RENDER_SNAPSHOT_FREE;
        sm64_saturn_render_snapshot_release_claim_release(release);
    }
    sm64_saturn_render_snapshot_fence();
}

bool sm64_saturn_render_snapshot_generation_valid(
    const sm64_saturn_render_snapshot_t *snapshot, uint32_t generation)
{
    return snapshot != NULL && generation != 0U &&
        snapshot->generation == generation &&
        snapshot->camera.generation == generation &&
        snapshot->actor_generation == generation;
}

bool sm64_saturn_render_snapshot_begin_write(
    sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation,
    sm64_saturn_render_snapshot_t **out)
{
    uint32_t index;

    if (out != NULL) *out = NULL;
    if (bank == NULL || out == NULL || generation == 0U) return false;
    for (index = 0U; index < 2U; index++) {
        sm64_saturn_render_snapshot_slot_t *const slot = &bank->slot[index];
        volatile sm64_saturn_render_snapshot_release_t *const release =
            sm64_saturn_render_snapshot_release_uncached(slot);
        sm64_saturn_render_snapshot_t *const payload =
            sm64_saturn_render_snapshot_owner_payload(slot);
        if (release->state != SM64_SATURN_RENDER_SNAPSHOT_FREE) continue;
        if (!sm64_saturn_render_snapshot_release_claim_try(release)) continue;
        if (release->state != SM64_SATURN_RENDER_SNAPSHOT_FREE) {
            sm64_saturn_render_snapshot_release_claim_release(release);
            continue;
        }
        memset(payload, 0, sizeof(*payload));
        payload->generation = generation;
        release->generation = generation;
        sm64_saturn_render_snapshot_fence();
        release->state = SM64_SATURN_RENDER_SNAPSHOT_WRITING;
        sm64_saturn_render_snapshot_release_claim_release(release);
        *out = sm64_saturn_render_snapshot_owner_payload(slot);
        return true;
    }
    return false;
}

bool sm64_saturn_render_snapshot_publish(
    sm64_saturn_render_snapshot_bank_t *bank,
    sm64_saturn_render_snapshot_t *snapshot)
{
    sm64_saturn_render_snapshot_slot_t *const slot = snapshot_slot(bank, snapshot);
    volatile sm64_saturn_render_snapshot_release_t *const release =
        sm64_saturn_render_snapshot_release_uncached(slot);

    if (slot == NULL || release == NULL ||
        !sm64_saturn_render_snapshot_release_claim_try(release)) return false;
    if (release->state != SM64_SATURN_RENDER_SNAPSHOT_WRITING ||
        !sm64_saturn_render_snapshot_generation_valid(snapshot,
                                                       snapshot->generation) ||
        release->generation != snapshot->generation) {
        sm64_saturn_render_snapshot_release_claim_release(release);
        return false;
    }
    sm64_saturn_render_snapshot_fence();
    release->generation = snapshot->generation;
    sm64_saturn_render_snapshot_fence();
    release->state = SM64_SATURN_RENDER_SNAPSHOT_READY;
    sm64_saturn_render_snapshot_release_claim_release(release);
    return true;
}

const sm64_saturn_render_snapshot_t *
sm64_saturn_render_snapshot_acquire_ready(
    sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation)
{
    uint32_t index;

    if (bank == NULL || generation == 0U) return NULL;
    for (index = 0U; index < 2U; index++) {
        sm64_saturn_render_snapshot_slot_t *const slot = &bank->slot[index];
        volatile sm64_saturn_render_snapshot_release_t *const release =
            sm64_saturn_render_snapshot_release_uncached(slot);
        const sm64_saturn_render_snapshot_t *const payload =
            sm64_saturn_render_snapshot_peer_payload(slot);
        if (release->state != SM64_SATURN_RENDER_SNAPSHOT_READY ||
            release->generation != generation) continue;
        if (!sm64_saturn_render_snapshot_release_claim_try(release)) continue;
        sm64_saturn_render_snapshot_fence();
        if (release->state != SM64_SATURN_RENDER_SNAPSHOT_READY ||
            release->generation != generation ||
            !sm64_saturn_render_snapshot_generation_valid(payload, generation)) {
            sm64_saturn_render_snapshot_release_claim_release(release);
            continue;
        }
        release->state = SM64_SATURN_RENDER_SNAPSHOT_RENDERING;
        sm64_saturn_render_snapshot_fence();
        sm64_saturn_render_snapshot_release_claim_release(release);
        return payload;
    }
    return NULL;
}

bool sm64_saturn_render_snapshot_complete(
    sm64_saturn_render_snapshot_bank_t *bank,
    const sm64_saturn_render_snapshot_t *snapshot)
{
    sm64_saturn_render_snapshot_slot_t *const slot = snapshot_slot(bank, snapshot);
    volatile sm64_saturn_render_snapshot_release_t *const release =
        sm64_saturn_render_snapshot_release_uncached(slot);

    if (slot == NULL || release == NULL ||
        !sm64_saturn_render_snapshot_release_claim_try(release)) return false;
    if (release->state != SM64_SATURN_RENDER_SNAPSHOT_RENDERING) {
        sm64_saturn_render_snapshot_release_claim_release(release);
        return false;
    }
    sm64_saturn_render_snapshot_fence();
    release->state = SM64_SATURN_RENDER_SNAPSHOT_COMPLETE;
    sm64_saturn_render_snapshot_release_claim_release(release);
    return true;
}

bool sm64_saturn_render_snapshot_retire(
    sm64_saturn_render_snapshot_bank_t *bank,
    const sm64_saturn_render_snapshot_t *snapshot)
{
    sm64_saturn_render_snapshot_slot_t *const slot = snapshot_slot(bank, snapshot);
    volatile sm64_saturn_render_snapshot_release_t *const release =
        sm64_saturn_render_snapshot_release_uncached(slot);

    if (slot == NULL || release == NULL ||
        !sm64_saturn_render_snapshot_release_claim_try(release)) return false;
    if (release->state != SM64_SATURN_RENDER_SNAPSHOT_COMPLETE) {
        sm64_saturn_render_snapshot_release_claim_release(release);
        return false;
    }
    memset(sm64_saturn_render_snapshot_owner_payload(slot), 0,
           sizeof(slot->snapshot));
    release->generation = 0U;
    sm64_saturn_render_snapshot_fence();
    release->state = SM64_SATURN_RENDER_SNAPSHOT_FREE;
    sm64_saturn_render_snapshot_release_claim_release(release);
    return true;
}

bool sm64_saturn_render_snapshot_quarantine(
    sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation)
{
    uint32_t index;

    if (bank == NULL || generation == 0U) return false;
    for (index = 0U; index < 2U; index++) {
        sm64_saturn_render_snapshot_slot_t *const slot = &bank->slot[index];
        volatile sm64_saturn_render_snapshot_release_t *const release =
            sm64_saturn_render_snapshot_release_uncached(slot);
        if (release->generation != generation ||
            release->state == SM64_SATURN_RENDER_SNAPSHOT_FREE ||
            release->state == SM64_SATURN_RENDER_SNAPSHOT_QUARANTINED) continue;
        if (!sm64_saturn_render_snapshot_release_claim_try(release)) continue;
        if (release->generation != generation ||
            release->state == SM64_SATURN_RENDER_SNAPSHOT_FREE ||
            release->state == SM64_SATURN_RENDER_SNAPSHOT_QUARANTINED) {
            sm64_saturn_render_snapshot_release_claim_release(release);
            continue;
        }
        sm64_saturn_render_snapshot_fence();
        release->state = SM64_SATURN_RENDER_SNAPSHOT_QUARANTINED;
        sm64_saturn_render_snapshot_release_claim_release(release);
        return true;
    }
    return false;
}
