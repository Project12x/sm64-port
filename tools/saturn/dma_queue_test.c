#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <yaul.h>
#include "slavedriver_dma_queue.h"

typedef struct mock_scu_transfer {
    void *dst;
    const void *src;
    size_t len;
    uint32_t level;
} mock_scu_transfer_t;

static mock_scu_transfer_t s_active_transfer;
static uint32_t s_transfer_starts;
static int s_busy;
static int s_complete_on_poll;

void
scu_dma_transfer(scu_dma_level_t level, void *dst, const void *src, size_t len)
{
    s_active_transfer = (mock_scu_transfer_t){ dst, src, len, level };
    s_transfer_starts++;
    s_busy = 1;
}

uint32_t
scu_dma_level_busy(scu_dma_level_t level)
{
    assert(level == 0U);
    if (s_busy && s_complete_on_poll) {
        memcpy(s_active_transfer.dst, s_active_transfer.src,
               s_active_transfer.len);
        s_busy = 0;
    }
    return (uint32_t)s_busy;
}

void
scu_dma_transfer_wait(scu_dma_level_t level)
{
    assert(level == 0U);
    while (s_busy) {
        s_complete_on_poll = 1;
        (void)scu_dma_level_busy(level);
    }
}

static void
mock_scu_complete(void)
{
    assert(s_busy);
    memcpy(s_active_transfer.dst, s_active_transfer.src, s_active_transfer.len);
    s_busy = 0;
}

static void
reset_mock(void)
{
    memset(&s_active_transfer, 0, sizeof(s_active_transfer));
    s_transfer_starts = 0U;
    s_busy = 0;
    s_complete_on_poll = 0;
    saturn_dma_queue_init();
}

static void
test_submit_only_copies_descriptor(void)
{
    uint8_t source[] = { 1U, 2U, 3U, 4U };
    uint8_t destination[] = { 0U, 0U, 0U, 0U };

    reset_mock();
    const saturn_dma_queue_sequence_t sequence = saturn_dma_queue_submit(
        destination, source, sizeof(source), SATURN_DMA_QUEUE_SCU);
    assert(sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(memcmp(destination, (uint8_t[4]){ 0U, 0U, 0U, 0U },
                  sizeof(destination)) == 0);
    source[0] = 9U;

    saturn_dma_queue_kick();
    assert(s_transfer_starts == 1U);
    assert(s_active_transfer.src == source);
    assert(s_active_transfer.dst == destination);
    assert(s_active_transfer.len == sizeof(source));
    assert(memcmp(destination, (uint8_t[4]){ 0U, 0U, 0U, 0U },
                  sizeof(destination)) == 0);

    mock_scu_complete();
    saturn_dma_queue_poll();
    assert(destination[0] == 9U);
    assert(saturn_dma_queue_idle());
}

static void
test_kick_starts_one_and_poll_retires_fifo(void)
{
    uint8_t source_a = 0xA1U;
    uint8_t source_b = 0xB2U;
    uint8_t destination_a = 0U;
    uint8_t destination_b = 0U;

    reset_mock();
    const saturn_dma_queue_sequence_t first = saturn_dma_queue_submit(
        &destination_a, &source_a, sizeof(source_a), SATURN_DMA_QUEUE_SCU);
    const saturn_dma_queue_sequence_t second = saturn_dma_queue_submit(
        &destination_b, &source_b, sizeof(source_b), SATURN_DMA_QUEUE_SCU);
    assert(first != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(second == first + 1U);

    saturn_dma_queue_kick();
    assert(s_transfer_starts == 1U);
    saturn_dma_queue_kick();
    assert(s_transfer_starts == 1U);
    saturn_dma_queue_poll();
    assert(!saturn_dma_queue_idle());
    assert(destination_a == 0U && destination_b == 0U);

    mock_scu_complete();
    saturn_dma_queue_poll();
    assert(destination_a == source_a);
    assert(destination_b == 0U);
    assert(!saturn_dma_queue_idle());

    saturn_dma_queue_kick();
    assert(s_transfer_starts == 2U);
    mock_scu_complete();
    saturn_dma_queue_poll();
    assert(destination_b == source_b);
    assert(saturn_dma_queue_idle());
}

static void
test_bounded_wrap_and_wait_drain(void)
{
    enum { ACCEPTED = SATURN_DMA_QUEUE_CAPACITY - 1U };
    uint8_t source[SATURN_DMA_QUEUE_CAPACITY] = { 0U };
    uint8_t destination[SATURN_DMA_QUEUE_CAPACITY] = { 0U };
    saturn_dma_queue_sequence_t last = SATURN_DMA_QUEUE_SEQUENCE_INVALID;

    reset_mock();
    for (uint32_t index = 0U; index < ACCEPTED; index++) {
        source[index] = (uint8_t)(index + 1U);
        last = saturn_dma_queue_submit(&destination[index], &source[index],
                                       sizeof(source[index]),
                                       SATURN_DMA_QUEUE_CPU);
        assert(last != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    }
    assert(saturn_dma_queue_submit(&destination[ACCEPTED], &source[ACCEPTED],
                                   sizeof(source[ACCEPTED]),
                                   SATURN_DMA_QUEUE_CPU) ==
           SATURN_DMA_QUEUE_SEQUENCE_INVALID);

    saturn_dma_queue_wait(last);
    assert(saturn_dma_queue_idle());
    assert(memcmp(source, destination, ACCEPTED) == 0);

    source[ACCEPTED] = 0x7EU;
    last = saturn_dma_queue_submit(&destination[ACCEPTED], &source[ACCEPTED],
                                   sizeof(source[ACCEPTED]),
                                   SATURN_DMA_QUEUE_CPU);
    assert(last != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    saturn_dma_queue_wait(last);
    assert(saturn_dma_queue_idle());
    assert(destination[ACCEPTED] == source[ACCEPTED]);

    reset_mock();
    source[0] = 0x5AU;
    last = saturn_dma_queue_submit(&destination[0], &source[0],
                                   sizeof(source[0]), SATURN_DMA_QUEUE_SCU);
    s_complete_on_poll = 1;
    saturn_dma_queue_wait(last);
    assert(destination[0] == source[0]);
    assert(saturn_dma_queue_idle());
}

int
main(void)
{
    test_submit_only_copies_descriptor();
    test_kick_starts_one_and_poll_retires_fifo();
    test_bounded_wrap_and_wait_drain();
    return 0;
}
