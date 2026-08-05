#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "saturn_gouraud_bank.h"
#include "saturn_vdp1_frame_bank.h"
#include "slavedriver_dma_queue.h"

static saturn_dma_queue_mode_t s_first_mode;
static saturn_dma_queue_mode_t s_second_mode;
static uint32_t s_pair_submits;
static uint32_t s_kicks;
static uint32_t s_waits;
static bool s_pair_accept = true;
static bool s_command_retired;
static bool s_gouraud_retired;
static bool s_command_failed;
static bool s_gouraud_failed;

static sm64_saturn_vdp1_frame_bank_t ready_bank(void);
static sm64_saturn_vdp1_transfer_targets_t targets(void);

int saturn_dma_queue_submit_pair(
    void *first_dst, const void *first_src, size_t first_len,
    saturn_dma_queue_mode_t first_mode,
    void *second_dst, const void *second_src, size_t second_len,
    saturn_dma_queue_mode_t second_mode,
    saturn_dma_queue_sequence_t *first_sequence,
    saturn_dma_queue_sequence_t *second_sequence)
{
    assert(first_dst == (void *)(uintptr_t)0x25C00000U);
    assert(first_src == (void *)(uintptr_t)0x20200000U);
    assert(first_len == 4U * 32U);
    assert(second_dst == (void *)(uintptr_t)0x25C10000U);
    assert(second_src == (void *)(uintptr_t)0x26000000U);
    assert(second_len == 2U * sizeof(sm64_saturn_gouraud_table_t));
    s_first_mode = first_mode;
    s_second_mode = second_mode;
    s_pair_submits++;
    if (!s_pair_accept) {
        *first_sequence = SATURN_DMA_QUEUE_SEQUENCE_INVALID;
        *second_sequence = SATURN_DMA_QUEUE_SEQUENCE_INVALID;
        return 0;
    }
    *first_sequence = 11U;
    *second_sequence = 12U;
    return 1;
}

saturn_dma_queue_sequence_t saturn_dma_queue_submit(
    void *dst, const void *src, size_t len, saturn_dma_queue_mode_t mode)
{
    (void)dst; (void)src; (void)len; (void)mode;
    return SATURN_DMA_QUEUE_SEQUENCE_INVALID;
}

void saturn_dma_queue_kick(void) { s_kicks++; }
void saturn_dma_queue_poll(void) {}
int saturn_dma_queue_sequence_retired(saturn_dma_queue_sequence_t sequence)
{
    return sequence == 11U ? s_command_retired :
           sequence == 12U ? s_gouraud_retired : 0;
}
int saturn_dma_queue_sequence_failed(saturn_dma_queue_sequence_t sequence)
{
    return sequence == 11U ? s_command_failed :
           sequence == 12U ? s_gouraud_failed : 0;
}
int saturn_dma_queue_sequence_started(saturn_dma_queue_sequence_t sequence)
{ (void)sequence; return 0; }
int saturn_dma_queue_wait(saturn_dma_queue_sequence_t sequence)
{
    s_waits++;
    if (sequence == 11U) s_command_retired = true;
    if (sequence == 12U) s_gouraud_retired = true;
    return sequence == 11U || sequence == 12U;
}

static void reset_transport(void)
{
    s_first_mode = SATURN_DMA_QUEUE_CPU;
    s_second_mode = SATURN_DMA_QUEUE_CPU;
    s_pair_submits = 0U;
    s_kicks = 0U;
    s_waits = 0U;
    s_pair_accept = true;
    s_command_retired = false;
    s_gouraud_retired = false;
    s_command_failed = false;
    s_gouraud_failed = false;
}

static void test_first_failure_drains_sibling_before_quarantine(void)
{
    reset_transport();
    sm64_saturn_vdp1_frame_bank_t bank = ready_bank();
    const sm64_saturn_vdp1_transfer_targets_t transfer_targets = targets();
    assert(sm64_saturn_vdp1_frame_bank_submit_transfers(
        &bank, &transfer_targets));
    s_command_failed = true;
    assert(!sm64_saturn_vdp1_frame_bank_poll_transfers(&bank));
    assert(bank.state == SM64_SATURN_VDP1_FRAME_BANK_TRANSFERRING);
    assert(bank.command_transfer_obligation == SM64_SATURN_VDP1_TRANSFER_FAILED);
    assert(bank.gouraud_transfer_obligation == SM64_SATURN_VDP1_TRANSFER_PENDING);
    assert(s_kicks == 1U);
    s_gouraud_retired = true;
    assert(!sm64_saturn_vdp1_frame_bank_poll_transfers(&bank));
    assert(bank.state == SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED);
}

static sm64_saturn_vdp1_frame_bank_t ready_bank(void)
{
    static sm64_saturn_gouraud_bank_t gouraud = {
        .staging = (sm64_saturn_gouraud_table_t *)(uintptr_t)0x26000000U,
        .capacity = 8U,
        .used = 2U,
        .vram_base = (uintptr_t)0x25C10000U,
    };
    sm64_saturn_vdp1_frame_bank_t bank = {
        .command_storage = (void *)(uintptr_t)0x20200000U,
        .gouraud_storage = (void *)(uintptr_t)0x26000000U,
        .gouraud_bank = &gouraud,
        .command_capacity = 8U,
        .command_count = 4U,
        .gouraud_count = 2U,
        .snapshot_generation = 7U,
        .worker_ticket = 7U,
        .state = SM64_SATURN_VDP1_FRAME_BANK_READY,
    };
    return bank;
}

static sm64_saturn_vdp1_transfer_targets_t targets(void)
{
    return (sm64_saturn_vdp1_transfer_targets_t){
        .command_vram = (void *)(uintptr_t)0x25C00000U,
        .gouraud_vram = (void *)(uintptr_t)0x25C10000U,
        .command_capacity_bytes = 8U * 32U,
        .gouraud_capacity_bytes = 8U * sizeof(sm64_saturn_gouraud_table_t),
    };
}

static void test_submit_selects_transports_and_performs_no_wait(void)
{
    reset_transport();
    sm64_saturn_vdp1_frame_bank_t bank = ready_bank();
    const sm64_saturn_vdp1_transfer_targets_t transfer_targets = targets();
    assert(sm64_saturn_vdp1_frame_bank_submit_transfers(
        &bank, &transfer_targets));
    assert(s_pair_submits == 1U);
    assert(s_first_mode == SATURN_DMA_QUEUE_CPU_DMAC);
    assert(s_second_mode == SATURN_DMA_QUEUE_SCU);
    assert(s_kicks == 0U);
    assert(s_waits == 0U);
    assert(bank.state == SM64_SATURN_VDP1_FRAME_BANK_TRANSFERRING);
    assert(!sm64_saturn_vdp1_frame_bank_poll_transfers(&bank));
    assert(s_kicks == 1U);
}

static void test_staggered_retirement_requires_both_tickets(void)
{
    reset_transport();
    sm64_saturn_vdp1_frame_bank_t bank = ready_bank();
    const sm64_saturn_vdp1_transfer_targets_t transfer_targets = targets();
    assert(sm64_saturn_vdp1_frame_bank_submit_transfers(
        &bank, &transfer_targets));
    s_command_retired = true;
    assert(!sm64_saturn_vdp1_frame_bank_poll_transfers(&bank));
    assert(bank.command_transfer_obligation == SM64_SATURN_VDP1_TRANSFER_RETIRED);
    assert(bank.gouraud_transfer_obligation == SM64_SATURN_VDP1_TRANSFER_PENDING);
    s_gouraud_retired = true;
    assert(sm64_saturn_vdp1_frame_bank_poll_transfers(&bank));
    assert(bank.gouraud_transfer_obligation == SM64_SATURN_VDP1_TRANSFER_RETIRED);
    assert(sm64_saturn_vdp1_frame_bank_arm_resident_list(&bank));
    assert(!sm64_saturn_vdp1_frame_bank_arm_resident_list(&bank));
}

static void test_failed_atomic_pair_leaves_ready_bank_reusable(void)
{
    reset_transport();
    s_pair_accept = false;
    sm64_saturn_vdp1_frame_bank_t bank = ready_bank();
    const sm64_saturn_vdp1_transfer_targets_t transfer_targets = targets();
    assert(!sm64_saturn_vdp1_frame_bank_submit_transfers(
        &bank, &transfer_targets));
    assert(bank.state == SM64_SATURN_VDP1_FRAME_BANK_READY);
    assert(s_kicks == 0U);
    assert(s_waits == 0U);
}

static void test_destination_contract_is_exact_and_nonoverlapping(void)
{
    reset_transport();
    sm64_saturn_vdp1_frame_bank_t bank = ready_bank();
    sm64_saturn_vdp1_transfer_targets_t transfer_targets = targets();
    transfer_targets.command_vram = (void *)(uintptr_t)0x25C00020U;
    assert(!sm64_saturn_vdp1_frame_bank_submit_transfers(
        &bank, &transfer_targets));
    transfer_targets = targets();
    transfer_targets.command_capacity_bytes -= 32U;
    assert(!sm64_saturn_vdp1_frame_bank_submit_transfers(
        &bank, &transfer_targets));
    transfer_targets = targets();
    transfer_targets.gouraud_vram = (void *)(uintptr_t)0x25C00080U;
    bank.gouraud_bank->vram_base = (uintptr_t)transfer_targets.gouraud_vram;
    assert(!sm64_saturn_vdp1_frame_bank_submit_transfers(
        &bank, &transfer_targets));
    bank.gouraud_bank->vram_base = (uintptr_t)0x25C10000U;
    assert(s_pair_submits == 0U);
}

static void test_publish_wait_counts_only_outstanding_transports(void)
{
    reset_transport();
    sm64_saturn_vdp1_frame_bank_t bank = ready_bank();
    const sm64_saturn_vdp1_transfer_targets_t transfer_targets = targets();
    sm64_saturn_vdp1_wait_stats_t waits = { 0 };
    assert(sm64_saturn_vdp1_frame_bank_submit_transfers(
        &bank, &transfer_targets));
    s_command_retired = true;
    assert(sm64_saturn_vdp1_frame_bank_wait_for_publish(&bank, &waits));
    assert(waits.command_cpu_dmac_waits == 0U);
    assert(waits.gouraud_scu_dma_waits == 1U);
    assert(s_waits == 1U);
}

int main(void)
{
    test_submit_selects_transports_and_performs_no_wait();
    test_staggered_retirement_requires_both_tickets();
    test_failed_atomic_pair_leaves_ready_bank_reusable();
    test_destination_contract_is_exact_and_nonoverlapping();
    test_publish_wait_counts_only_outstanding_transports();
    test_first_failure_drains_sibling_before_quarantine();
    return 0;
}
