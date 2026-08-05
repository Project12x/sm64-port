#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "saturn_gouraud_bank.h"
#include "saturn_gouraud_transfer.h"

static uint32_t s_submit_calls;
static uint32_t s_drain_calls;
static saturn_dma_queue_sequence_t s_results[2];

saturn_dma_queue_sequence_t saturn_dma_queue_submit(
    void *dst, const void *src, size_t len, saturn_dma_queue_mode_t mode)
{
    (void)dst;
    (void)src;
    (void)len;
    (void)mode;
    return s_results[s_submit_calls++];
}

void saturn_dma_queue_drain(void)
{
    s_drain_calls++;
}

static void reset(saturn_dma_queue_sequence_t first,
                  saturn_dma_queue_sequence_t second)
{
    s_submit_calls = 0U;
    s_drain_calls = 0U;
    s_results[0] = first;
    s_results[1] = second;
}

int main(void)
{
    sm64_saturn_gouraud_table_t staging[2];
    sm64_saturn_gouraud_bank_t bank;
    saturn_dma_queue_sequence_t sequence = 99U;
    bool retried = true;
    assert(sm64_saturn_gouraud_bank_init(
        &bank, staging, 2U, (uintptr_t)0x25C40000U));

    reset(7U, 0U);
    assert(sm64_saturn_gouraud_transfer_submit(
        &bank, &sequence, &retried));
    assert(sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(!retried && s_submit_calls == 0U && s_drain_calls == 0U);

    bank.used = 1U;
    reset(7U, 0U);
    assert(sm64_saturn_gouraud_transfer_submit(
        &bank, &sequence, &retried));
    assert(sequence == 7U);
    assert(!retried && s_submit_calls == 1U && s_drain_calls == 0U);

    reset(SATURN_DMA_QUEUE_SEQUENCE_INVALID, 8U);
    assert(sm64_saturn_gouraud_transfer_submit(
        &bank, &sequence, &retried));
    assert(sequence == 8U);
    assert(retried && s_submit_calls == 2U && s_drain_calls == 1U);

    reset(SATURN_DMA_QUEUE_SEQUENCE_INVALID,
          SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(!sm64_saturn_gouraud_transfer_submit(
        &bank, &sequence, &retried));
    assert(sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(retried && s_submit_calls == 2U && s_drain_calls == 1U);
    return 0;
}
