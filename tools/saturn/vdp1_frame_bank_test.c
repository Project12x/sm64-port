#include <assert.h>
#include <stdint.h>

#include "saturn_gouraud_bank.h"
#include "saturn_vdp1_frame_bank.h"

static void test_region_contract(void)
{
    assert(sm64_saturn_vdp1_frame_bank_command_source_is_lwram(
        (const void *)(uintptr_t)0x00200000U, 0x10000U));
    assert(sm64_saturn_vdp1_frame_bank_command_source_is_lwram(
        (const void *)(uintptr_t)0x20200000U, 0x10000U));
    assert(!sm64_saturn_vdp1_frame_bank_command_source_is_lwram(
        (const void *)(uintptr_t)0x06010000U, 0x10000U));
    assert(!sm64_saturn_vdp1_frame_bank_command_source_is_lwram(
        (const void *)(uintptr_t)0x002F0000U, 0x20000U));

    assert(sm64_saturn_vdp1_frame_bank_gouraud_source_is_hwram(
        (const void *)(uintptr_t)0x06010000U, 0x3000U));
    assert(sm64_saturn_vdp1_frame_bank_gouraud_source_is_hwram(
        (const void *)(uintptr_t)0x26010000U, 0x3000U));
    assert(!sm64_saturn_vdp1_frame_bank_gouraud_source_is_hwram(
        (const void *)(uintptr_t)0x00210000U, 0x3000U));
    assert(!sm64_saturn_vdp1_frame_bank_gouraud_source_is_hwram(
        (const void *)(uintptr_t)0x060FF000U, 0x3000U));
}

static void init_set(sm64_saturn_vdp1_frame_bank_set_t *set,
                     sm64_saturn_gouraud_bank_t gouraud[2])
{
    sm64_saturn_gouraud_bank_init(
        &gouraud[0], (sm64_saturn_gouraud_table_t *)(uintptr_t)0x06010000U,
        1536U, 0x25C40000U);
    sm64_saturn_gouraud_bank_init(
        &gouraud[1], (sm64_saturn_gouraud_table_t *)(uintptr_t)0x06013000U,
        1536U, 0x25C40000U);
    assert(sm64_saturn_vdp1_frame_bank_set_init(
        set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00210000U, 2048U, &gouraud[0], &gouraud[1]));
}

static void test_lifecycle_and_ticket_retirement(void)
{
    sm64_saturn_vdp1_frame_bank_set_t set;
    sm64_saturn_gouraud_bank_t gouraud[2];
    sm64_saturn_vdp1_frame_bank_t *first = NULL;
    sm64_saturn_vdp1_frame_bank_t *second = NULL;
    init_set(&set, gouraud);

    assert(!sm64_saturn_vdp1_frame_bank_begin_build(&set, 0U, &first));
    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, 10U, &first));
    assert(first == &set.banks[0]);
    assert(first->command_storage == (void *)(uintptr_t)0x00200000U);
    assert(first->gouraud_storage == (void *)(uintptr_t)0x06010000U);
    assert(first->state == SM64_SATURN_VDP1_FRAME_BANK_BUILDING);
    assert(!sm64_saturn_vdp1_frame_bank_begin_build(&set, 10U, &second));
    assert(!sm64_saturn_vdp1_frame_bank_ready(first, 2U, 0U, 99U));
    assert(!sm64_saturn_vdp1_frame_bank_ready(first, 2049U, 0U, 99U));
    assert(!sm64_saturn_vdp1_frame_bank_ready(first, 12U, 1537U, 99U));
    assert(!sm64_saturn_vdp1_frame_bank_ready(first, 12U, 2U,
                                               SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID));
    assert(sm64_saturn_vdp1_frame_bank_ready(first, 12U, 2U, 100U));
    assert(first->state == SM64_SATURN_VDP1_FRAME_BANK_READY);
    assert(!sm64_saturn_vdp1_frame_bank_publish(&set, first));

    assert(sm64_saturn_vdp1_frame_bank_begin_transfers(first, 200U, 201U));
    assert(first->state == SM64_SATURN_VDP1_FRAME_BANK_TRANSFERRING);
    assert(!sm64_saturn_vdp1_frame_bank_record_transfers_retired(
        first, 200U, 999U));
    assert(!sm64_saturn_vdp1_frame_bank_publish(&set, first));
    assert(sm64_saturn_vdp1_frame_bank_record_transfers_retired(
        first, 200U, 201U));
    assert(sm64_saturn_vdp1_frame_bank_publish(&set, first));
    assert(first->state == SM64_SATURN_VDP1_FRAME_BANK_PUBLISHED);
    assert(set.published == first);

    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, 11U, &second));
    assert(second == &set.banks[1]);
    assert(!sm64_saturn_vdp1_frame_bank_retire(&set, 10U));
    assert(set.published == first);
    assert(!sm64_saturn_vdp1_frame_bank_retire(&set, 9U));

    assert(sm64_saturn_vdp1_frame_bank_ready(second, 8U, 0U, 101U));
    assert(sm64_saturn_vdp1_frame_bank_record_synchronous_complete(second));
    assert(second->command_transfer_obligation ==
           SM64_SATURN_VDP1_TRANSFER_RETIRED);
    assert(second->gouraud_transfer_obligation ==
           SM64_SATURN_VDP1_TRANSFER_NOOP);
    assert(sm64_saturn_vdp1_frame_bank_publish(&set, second));
    assert(set.published == second);
    assert(first->state == SM64_SATURN_VDP1_FRAME_BANK_PUBLISHED);
    sm64_saturn_vdp1_frame_bank_t *const retired_bank = first;
    assert(!sm64_saturn_vdp1_frame_bank_begin_build(&set, 12U, &first));
    assert(sm64_saturn_vdp1_frame_bank_retire(&set, 10U));
    assert(retired_bank->state == SM64_SATURN_VDP1_FRAME_BANK_FREE);
    assert(!sm64_saturn_vdp1_frame_bank_begin_build(&set, 11U, &first));
    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, 12U, &first));
}

static void test_init_rejects_alias_overlap_and_misalignment(void)
{
    sm64_saturn_vdp1_frame_bank_set_t set;
    sm64_saturn_gouraud_bank_t gouraud[2];
    sm64_saturn_gouraud_bank_init(
        &gouraud[0], (sm64_saturn_gouraud_table_t *)(uintptr_t)0x06010000U,
        1536U, 0x25C40000U);
    sm64_saturn_gouraud_bank_init(
        &gouraud[1], (sm64_saturn_gouraud_table_t *)(uintptr_t)0x06013000U,
        1536U, 0x25C40000U);

    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00200000U, 2048U, &gouraud[0], &gouraud[1]));
    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200001U,
        (void *)(uintptr_t)0x00210000U, 2048U, &gouraud[0], &gouraud[1]));
    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00208000U, 2048U, &gouraud[0], &gouraud[1]));
    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00210000U, 2048U, &gouraud[0], &gouraud[0]));
    sm64_saturn_gouraud_bank_t *const overlapping_object =
        (sm64_saturn_gouraud_bank_t *)((uint8_t *)&gouraud[0] +
            _Alignof(sm64_saturn_gouraud_bank_t));
    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00210000U, 2048U,
        &gouraud[0], overlapping_object));
    sm64_saturn_gouraud_bank_t *const misaligned_object =
        (sm64_saturn_gouraud_bank_t *)((uint8_t *)&gouraud[1] + 1U);
    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00210000U, 2048U,
        &gouraud[0], misaligned_object));

    gouraud[1].staging = gouraud[0].staging;
    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00210000U, 2048U, &gouraud[0], &gouraud[1]));
    gouraud[1].staging =
        (sm64_saturn_gouraud_table_t *)(uintptr_t)0x06011000U;
    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00210000U, 2048U, &gouraud[0], &gouraud[1]));
    gouraud[1].staging =
        (sm64_saturn_gouraud_table_t *)(uintptr_t)0x06013004U;
    assert(!sm64_saturn_vdp1_frame_bank_set_init(
        &set, (void *)(uintptr_t)0x00200000U,
        (void *)(uintptr_t)0x00210000U, 2048U, &gouraud[0], &gouraud[1]));
}

static void test_late_completion_cannot_regress_publication(void)
{
    sm64_saturn_vdp1_frame_bank_set_t set;
    sm64_saturn_gouraud_bank_t gouraud[2];
    sm64_saturn_vdp1_frame_bank_t *late = NULL;
    sm64_saturn_vdp1_frame_bank_t *newest = NULL;
    init_set(&set, gouraud);

    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, UINT32_MAX, &late));
    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, 1U, &newest));
    assert(sm64_saturn_vdp1_frame_bank_ready(late, 3U, 0U, 10U));
    assert(sm64_saturn_vdp1_frame_bank_record_synchronous_complete(late));
    assert(sm64_saturn_vdp1_frame_bank_ready(newest, 3U, 0U, 11U));
    assert(sm64_saturn_vdp1_frame_bank_record_synchronous_complete(newest));
    assert(sm64_saturn_vdp1_frame_bank_publish(&set, newest));
    assert(!sm64_saturn_vdp1_frame_bank_publish(&set, late));
    assert(set.published == newest);
    assert(late->state == SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED);
}

static void test_generation_wrap_and_wrong_bank(void)
{
    sm64_saturn_vdp1_frame_bank_set_t set;
    sm64_saturn_gouraud_bank_t gouraud[2];
    sm64_saturn_vdp1_frame_bank_t foreign = {0};
    sm64_saturn_vdp1_frame_bank_t *bank = NULL;
    init_set(&set, gouraud);

    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, UINT32_MAX, &bank));
    assert(sm64_saturn_vdp1_frame_bank_ready(bank, 3U, 0U, UINT32_MAX));
    assert(sm64_saturn_vdp1_frame_bank_record_synchronous_complete(bank));
    assert(!sm64_saturn_vdp1_frame_bank_publish(&set, &foreign));
    assert(sm64_saturn_vdp1_frame_bank_publish(&set, bank));

    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, 1U, &bank));
    assert(!sm64_saturn_vdp1_frame_bank_begin_build(&set, UINT32_MAX, &bank));
}

static void test_failed_build_is_quarantined_and_previous_bank_survives(void)
{
    sm64_saturn_vdp1_frame_bank_set_t set;
    sm64_saturn_gouraud_bank_t gouraud[2];
    sm64_saturn_vdp1_frame_bank_t *published = NULL;
    sm64_saturn_vdp1_frame_bank_t *failed = NULL;
    sm64_saturn_vdp1_frame_bank_t *out = NULL;
    init_set(&set, gouraud);

    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, 20U, &published));
    assert(sm64_saturn_vdp1_frame_bank_ready(published, 9U, 0U, 300U));
    assert(sm64_saturn_vdp1_frame_bank_record_synchronous_complete(published));
    assert(sm64_saturn_vdp1_frame_bank_publish(&set, published));

    assert(sm64_saturn_vdp1_frame_bank_begin_build(&set, 21U, &failed));
    assert(sm64_saturn_vdp1_frame_bank_quarantine(failed));
    assert(failed->state == SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED);
    assert(set.published == published);
    assert(!sm64_saturn_vdp1_frame_bank_begin_build(&set, 22U, &out));
    assert(out == NULL);
}

int main(void)
{
    test_region_contract();
    test_lifecycle_and_ticket_retirement();
    test_init_rejects_alias_overlap_and_misalignment();
    test_late_completion_cannot_regress_publication();
    test_generation_wrap_and_wrong_bank();
    test_failed_build_is_quarantined_and_previous_bank_survives();
    return 0;
}
