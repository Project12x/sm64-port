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
static mock_scu_transfer_t s_active_cpu_transfer;
static uint32_t s_cpu_transfer_starts;
static uint32_t s_cpu_waits;
static int s_cpu_busy;
static int s_cpu_address_error;
static cpu_dmac_cfg_t s_cpu_cfg;
static uint32_t s_cpu_configures;
static uint32_t s_cpu_stops;
static uint32_t s_cpu_enables;

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

void cpu_dmac_status_get(cpu_dmac_status_t *status)
{
    assert(status != NULL);
    /* Match the pinned Yaul false-idle case: DE=1/TE=0 is active in hardware,
     * but cpu_dmac_status_get().channel_busy reports zero. Queue retirement
     * must therefore be driven by the completion IHR, never this field. */
    *status = (cpu_dmac_status_t){ .enabled = 1U,
                                  .address_error = (unsigned)s_cpu_address_error,
                                  .channel_busy = 0U };
}

void cpu_dmac_channel_config_set(const cpu_dmac_cfg_t *cfg)
{
    assert(cfg != NULL);
    assert(cfg->channel == 0U);
    s_cpu_cfg = *cfg;
    s_cpu_configures++;
}

void cpu_dmac_channel_start(cpu_dmac_channel_t channel)
{
    assert(channel == 0U);
    assert(!s_cpu_busy);
    s_active_cpu_transfer = (mock_scu_transfer_t){
        (void *)s_cpu_cfg.dst, (const void *)s_cpu_cfg.src,
        s_cpu_cfg.len, channel
    };
    s_cpu_transfer_starts++;
    s_cpu_busy = 1;
}

void cpu_dmac_channel_stop(cpu_dmac_channel_t channel)
{
    assert(channel == 0U);
    s_cpu_stops++;
    s_cpu_busy = 0;
}

void cpu_dmac_enable(void)
{
    s_cpu_enables++;
}

void cpu_dmac_transfer(cpu_dmac_channel_t channel, void *dst,
                       const void *src, size_t len)
{
    (void)channel; (void)dst; (void)src; (void)len;
    assert(!"queue must not enter Yaul's blocking cpu_dmac_transfer helper");
}

static void
mock_cpu_complete(void)
{
    assert(s_cpu_busy);
    memcpy(s_active_cpu_transfer.dst, s_active_cpu_transfer.src,
           s_active_cpu_transfer.len);
    s_cpu_busy = 0;
    assert(s_cpu_cfg.ihr != NULL);
    s_cpu_cfg.ihr(s_cpu_cfg.ihr_work);
}

void cpu_dmac_transfer_wait(cpu_dmac_channel_t channel)
{
    assert(channel == 0U);
    s_cpu_waits++;
    if (s_cpu_busy) {
        memcpy(s_active_cpu_transfer.dst, s_active_cpu_transfer.src,
               s_active_cpu_transfer.len);
        s_cpu_busy = 0;
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
    memset(&s_active_cpu_transfer, 0, sizeof(s_active_cpu_transfer));
    s_cpu_transfer_starts = 0U;
    s_cpu_waits = 0U;
    s_cpu_busy = 0;
    s_cpu_address_error = 0;
    memset(&s_cpu_cfg, 0, sizeof(s_cpu_cfg));
    s_cpu_configures = 0U;
    s_cpu_stops = 0U;
    s_cpu_enables = 0U;
    saturn_dma_queue_init();
}

static void test_cpu_dmac_error_is_failure_not_retirement(void)
{
    uint32_t source = UINT32_C(0x11223344);
    uint32_t destination = 0U;

    reset_mock();
    const saturn_dma_queue_sequence_t sequence = saturn_dma_queue_submit(
        &destination, &source, sizeof(source), SATURN_DMA_QUEUE_CPU_DMAC);
    saturn_dma_queue_kick();
    s_cpu_address_error = 1;
    saturn_dma_queue_poll();
    assert(!saturn_dma_queue_sequence_retired(sequence));
    assert(saturn_dma_queue_sequence_failed(sequence));
    assert(!saturn_dma_queue_wait(sequence));
}

static void test_cpu_dmac_submit_is_wait_free_and_poll_driven(void)
{
    uint32_t source = UINT32_C(0x12345678);
    uint32_t destination = 0U;

    reset_mock();
    const saturn_dma_queue_sequence_t sequence = saturn_dma_queue_submit(
        &destination, &source, sizeof(source), SATURN_DMA_QUEUE_CPU_DMAC);
    assert(sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(s_cpu_transfer_starts == 0U);
    assert(s_cpu_waits == 0U);
    assert(!saturn_dma_queue_sequence_started(sequence));

    saturn_dma_queue_kick();
    assert(s_cpu_transfer_starts == 1U);
    assert(saturn_dma_queue_sequence_started(sequence));
    assert(s_cpu_waits == 0U);
    assert(destination == 0U);
    saturn_dma_queue_poll();
    assert(!saturn_dma_queue_sequence_retired(sequence));

    mock_cpu_complete();
    saturn_dma_queue_poll();
    assert(destination == source);
    assert(saturn_dma_queue_sequence_retired(sequence));
    assert(s_cpu_waits == 0U);
}

static void test_cpu_dmac_false_idle_status_cannot_retire_early(void)
{
    uint32_t source = UINT32_C(0xA5A55A5A);
    uint32_t destination = 0U;

    reset_mock();
    const saturn_dma_queue_sequence_t sequence = saturn_dma_queue_submit(
        &destination, &source, sizeof(source), SATURN_DMA_QUEUE_CPU_DMAC);
    assert(sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    saturn_dma_queue_kick();
    assert(s_cpu_transfer_starts == 1U);
    assert(s_cpu_configures == 1U);
    assert(saturn_dma_queue_sequence_started(sequence));
    saturn_dma_queue_poll();
    assert(!saturn_dma_queue_sequence_retired(sequence));
    assert(destination == 0U);
    mock_cpu_complete();
    saturn_dma_queue_poll();
    assert(saturn_dma_queue_sequence_retired(sequence));
    assert(destination == source);
}

static void test_scu_kick_does_not_enter_yaul_while_level_busy(void)
{
    uint8_t source = 0x5AU;
    uint8_t destination = 0U;

    reset_mock();
    s_busy = 1;
    assert(saturn_dma_queue_submit(&destination, &source, sizeof(source),
                                   SATURN_DMA_QUEUE_SCU) !=
           SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    saturn_dma_queue_kick();
    assert(s_transfer_starts == 0U);
    s_busy = 0;
    saturn_dma_queue_kick();
    assert(s_transfer_starts == 1U);
}

static void test_pair_submission_is_atomic_when_only_one_slot_remains(void)
{
    uint8_t source[SATURN_DMA_QUEUE_CAPACITY] = { 0U };
    uint8_t destination[SATURN_DMA_QUEUE_CAPACITY] = { 0U };
    saturn_dma_queue_sequence_t first = SATURN_DMA_QUEUE_SEQUENCE_INVALID;
    saturn_dma_queue_sequence_t second = SATURN_DMA_QUEUE_SEQUENCE_INVALID;

    reset_mock();
    for (uint32_t index = 0U; index < SATURN_DMA_QUEUE_CAPACITY - 2U; index++) {
        assert(saturn_dma_queue_submit(&destination[index], &source[index], 1U,
                                       SATURN_DMA_QUEUE_CPU) !=
               SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    }
    assert(!saturn_dma_queue_submit_pair(
        &destination[14], &source[14], 1U, SATURN_DMA_QUEUE_CPU,
        &destination[15], &source[15], 1U, SATURN_DMA_QUEUE_CPU,
        &first, &second));
    assert(first == SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(second == SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    /* A single descriptor still fits. If the failed pair partially committed,
     * this submit would fail or skip the next sequence. */
    assert(saturn_dma_queue_submit(&destination[14], &source[14], 1U,
                                   SATURN_DMA_QUEUE_CPU) == 13U);
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
    /* The host target seeds next_sequence at UINT32_MAX - 1, so this fixed
     * bounded queue crosses the nonzero sequence wrap while it fills. */
    assert(last == 13U);
    assert(saturn_dma_queue_submit(&destination[ACCEPTED], &source[ACCEPTED],
                                   sizeof(source[ACCEPTED]),
                                   SATURN_DMA_QUEUE_CPU) ==
           SATURN_DMA_QUEUE_SEQUENCE_INVALID);

    assert(saturn_dma_queue_wait(last));
    assert(saturn_dma_queue_idle());
    assert(memcmp(source, destination, ACCEPTED) == 0);

    source[ACCEPTED] = 0x7EU;
    last = saturn_dma_queue_submit(&destination[ACCEPTED], &source[ACCEPTED],
                                   sizeof(source[ACCEPTED]),
                                   SATURN_DMA_QUEUE_CPU);
    assert(last != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(saturn_dma_queue_wait(last));
    assert(saturn_dma_queue_idle());
    assert(destination[ACCEPTED] == source[ACCEPTED]);

    reset_mock();
    source[0] = 0x5AU;
    last = saturn_dma_queue_submit(&destination[0], &source[0],
                                   sizeof(source[0]), SATURN_DMA_QUEUE_SCU);
    s_complete_on_poll = 1;
    assert(saturn_dma_queue_wait(last));
    assert(destination[0] == source[0]);
    assert(saturn_dma_queue_idle());
}

static void
test_wait_accepts_retired_and_rejects_non_outstanding(void)
{
    uint8_t source = 0x5CU;
    uint8_t destination = 0U;

    reset_mock();
    const saturn_dma_queue_sequence_t completed = saturn_dma_queue_submit(
        &destination, &source, sizeof(source), SATURN_DMA_QUEUE_CPU);
    assert(completed != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(saturn_dma_queue_wait(completed));
    assert(destination == source);
    /* Re-waiting an already retired descriptor must complete immediately,
     * rather than spinning for equality against a later retire sequence. */
    assert(saturn_dma_queue_wait(completed));
    assert(!saturn_dma_queue_wait(completed + 1U));
    assert(saturn_dma_queue_idle());
}

static void
test_submit_rejects_illegal_requests_without_fifo_mutation(void)
{
    uint8_t source_a = 0x11U;
    uint8_t source_b = 0x22U;
    uint8_t destination_a = 0U;
    uint8_t destination_b = 0U;
    const void * const lwram_cached = (const void *)(uintptr_t)0x00200000U;
    const void * const lwram_uncached = (const void *)(uintptr_t)0x20200000U;
    void * const lwram_purge = (void *)(uintptr_t)0x40200000U;

    reset_mock();
    const saturn_dma_queue_sequence_t first = saturn_dma_queue_submit(
        &destination_a, &source_a, sizeof(source_a), SATURN_DMA_QUEUE_CPU);
    assert(first != SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(saturn_dma_queue_submit(NULL, &source_a, sizeof(source_a),
                                   SATURN_DMA_QUEUE_SCU) ==
           SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(saturn_dma_queue_submit(&destination_a, NULL, sizeof(source_a),
                                   SATURN_DMA_QUEUE_SCU) ==
           SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(saturn_dma_queue_submit(&destination_a, &source_a, 0U,
                                   SATURN_DMA_QUEUE_SCU) ==
           SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(saturn_dma_queue_submit(&destination_a, lwram_cached, 1U,
                                   SATURN_DMA_QUEUE_SCU) ==
           SATURN_DMA_QUEUE_SEQUENCE_INVALID);
    assert(saturn_dma_queue_submit(lwram_purge, lwram_uncached, 1U,
                                   SATURN_DMA_QUEUE_SCU) ==
           SATURN_DMA_QUEUE_SEQUENCE_INVALID);

    const saturn_dma_queue_sequence_t second = saturn_dma_queue_submit(
        &destination_b, &source_b, sizeof(source_b), SATURN_DMA_QUEUE_CPU);
    assert(second == first + 1U);
    assert(saturn_dma_queue_wait(second));
    assert(destination_a == source_a);
    assert(destination_b == source_b);
    assert(saturn_dma_queue_idle());
}

static void test_request_preflight_is_read_only_and_authoritative(void)
{
    void * const vdp1 = (void *)(uintptr_t)0x25C00000U;
    const void * const hwram = (const void *)(uintptr_t)0x06000000U;
    const void * const lwram = (const void *)(uintptr_t)0x00200000U;
    const void * const lwram_alias = (const void *)(uintptr_t)0x20200000U;
    const void * const lwram_straddle =
        (const void *)(uintptr_t)0x001FFFFCU;
    uint8_t source = 0x91U, destination = 0U;

    reset_mock();
    assert(saturn_dma_queue_request_valid(
        vdp1, hwram, 2560U, SATURN_DMA_QUEUE_SCU));
    assert(!saturn_dma_queue_request_valid(
        vdp1, lwram, 1U, SATURN_DMA_QUEUE_SCU));
    assert(!saturn_dma_queue_request_valid(
        vdp1, lwram_alias, 1U, SATURN_DMA_QUEUE_SCU));
    assert(!saturn_dma_queue_request_valid(
        vdp1, lwram_straddle, 8U, SATURN_DMA_QUEUE_SCU));
    assert(!saturn_dma_queue_request_valid(
        NULL, hwram, 1U, SATURN_DMA_QUEUE_SCU));
    assert(!saturn_dma_queue_request_valid(
        vdp1, NULL, 1U, SATURN_DMA_QUEUE_SCU));
    assert(!saturn_dma_queue_request_valid(
        vdp1, hwram, 0U, SATURN_DMA_QUEUE_SCU));
    assert(!saturn_dma_queue_request_valid(
        vdp1, hwram, 1U, (saturn_dma_queue_mode_t)99));
#if SIZE_MAX > UINT32_MAX
    assert(!saturn_dma_queue_request_valid(
        vdp1, hwram, (size_t)UINT32_MAX + 1U, SATURN_DMA_QUEUE_SCU));
#endif
    /* Preflight does not consume a slot or sequence. */
    assert(saturn_dma_queue_submit(&destination, &source, 1U,
                                   SATURN_DMA_QUEUE_CPU) ==
           UINT32_MAX - 1U);
}

int
main(void)
{
    test_submit_only_copies_descriptor();
    test_kick_starts_one_and_poll_retires_fifo();
    test_bounded_wrap_and_wait_drain();
    test_wait_accepts_retired_and_rejects_non_outstanding();
    test_submit_rejects_illegal_requests_without_fifo_mutation();
    test_request_preflight_is_read_only_and_authoritative();
    test_cpu_dmac_submit_is_wait_free_and_poll_driven();
    test_cpu_dmac_false_idle_status_cannot_retire_early();
    test_scu_kick_does_not_enter_yaul_while_level_busy();
    test_pair_submission_is_atomic_when_only_one_slot_remains();
    test_cpu_dmac_error_is_failure_not_retirement();
    return 0;
}
