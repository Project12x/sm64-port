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
        if (snapshot == &bank->slot[index].snapshot) return &bank->slot[index];
    }
    return NULL;
}

void sm64_saturn_render_snapshot_reset(sm64_saturn_render_snapshot_bank_t *bank)
{
    uint32_t index;

    if (bank == NULL) return;
    for (index = 0U; index < 2U; index++) {
        memset(&bank->slot[index].snapshot, 0, sizeof(bank->slot[index].snapshot));
        bank->slot[index].release.generation = 0U;
        bank->slot[index].release.state = SM64_SATURN_RENDER_SNAPSHOT_FREE;
    }
    sm64_saturn_render_snapshot_fence();
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
        if (slot->release.state != SM64_SATURN_RENDER_SNAPSHOT_FREE) continue;
        memset(&slot->snapshot, 0, sizeof(slot->snapshot));
        slot->snapshot.generation = generation;
        slot->release.generation = generation;
        sm64_saturn_render_snapshot_fence();
        slot->release.state = SM64_SATURN_RENDER_SNAPSHOT_WRITING;
        *out = &slot->snapshot;
        return true;
    }
    return false;
}

bool sm64_saturn_render_snapshot_publish(
    sm64_saturn_render_snapshot_bank_t *bank,
    sm64_saturn_render_snapshot_t *snapshot)
{
    sm64_saturn_render_snapshot_slot_t *const slot = snapshot_slot(bank, snapshot);

    if (slot == NULL || slot->release.state != SM64_SATURN_RENDER_SNAPSHOT_WRITING ||
        snapshot->generation == 0U ||
        slot->release.generation != snapshot->generation ||
        snapshot->camera.generation != snapshot->generation ||
        snapshot->actor_generation != snapshot->generation) return false;
    sm64_saturn_render_snapshot_fence();
    slot->release.generation = snapshot->generation;
    sm64_saturn_render_snapshot_fence();
    slot->release.state = SM64_SATURN_RENDER_SNAPSHOT_READY;
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
        if (slot->release.state != SM64_SATURN_RENDER_SNAPSHOT_READY ||
            slot->release.generation != generation) continue;
        sm64_saturn_render_snapshot_fence();
        if (slot->snapshot.generation != generation ||
            slot->snapshot.camera.generation != generation ||
            slot->snapshot.actor_generation != generation) return NULL;
        slot->release.state = SM64_SATURN_RENDER_SNAPSHOT_RENDERING;
        sm64_saturn_render_snapshot_fence();
        return &slot->snapshot;
    }
    return NULL;
}

bool sm64_saturn_render_snapshot_complete(
    sm64_saturn_render_snapshot_bank_t *bank,
    const sm64_saturn_render_snapshot_t *snapshot)
{
    sm64_saturn_render_snapshot_slot_t *const slot = snapshot_slot(bank, snapshot);

    if (slot == NULL || slot->release.state != SM64_SATURN_RENDER_SNAPSHOT_RENDERING) {
        return false;
    }
    sm64_saturn_render_snapshot_fence();
    slot->release.state = SM64_SATURN_RENDER_SNAPSHOT_COMPLETE;
    return true;
}

bool sm64_saturn_render_snapshot_retire(
    sm64_saturn_render_snapshot_bank_t *bank,
    const sm64_saturn_render_snapshot_t *snapshot)
{
    sm64_saturn_render_snapshot_slot_t *const slot = snapshot_slot(bank, snapshot);

    if (slot == NULL || slot->release.state != SM64_SATURN_RENDER_SNAPSHOT_COMPLETE) {
        return false;
    }
    memset(&slot->snapshot, 0, sizeof(slot->snapshot));
    slot->release.generation = 0U;
    sm64_saturn_render_snapshot_fence();
    slot->release.state = SM64_SATURN_RENDER_SNAPSHOT_FREE;
    return true;
}

bool sm64_saturn_render_snapshot_quarantine(
    sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation)
{
    uint32_t index;

    if (bank == NULL || generation == 0U) return false;
    for (index = 0U; index < 2U; index++) {
        sm64_saturn_render_snapshot_slot_t *const slot = &bank->slot[index];
        if (slot->release.generation != generation ||
            slot->release.state == SM64_SATURN_RENDER_SNAPSHOT_FREE ||
            slot->release.state == SM64_SATURN_RENDER_SNAPSHOT_QUARANTINED) continue;
        sm64_saturn_render_snapshot_fence();
        slot->release.state = SM64_SATURN_RENDER_SNAPSHOT_QUARANTINED;
        return true;
    }
    return false;
}
