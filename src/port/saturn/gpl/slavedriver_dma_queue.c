/*
 * GPL-3.0-or-later. Close-port of Lobotomy-Software/SlaveDriver-Engine's
 * bounded DMA request ring (DMA.C), commit
 * a8986591557b6e680550d3c23970284d3b38ff8f.
 * Material modifications: Yaul/libyaul calls replace raw register pokes;
 * queue overflow is reported instead of relying on assert().
 */
#include "slavedriver_dma_queue.h"
#include <yaul.h>
#include <stdbool.h>
#include <string.h>

#ifndef SATURN_DMA_QUEUE_INITIAL_SEQUENCE
#define SATURN_DMA_QUEUE_INITIAL_SEQUENCE 1U
#endif

/* Yaul's scu/map.h defines the same physical LWRAM window. Keep this local
 * queue guard independent of a particular Yaul include layout: every cached,
 * uncached, or purge alias must be rejected before scu_dma_transfer() can
 * program it, because either direction locks real hardware. */
#define SATURN_DMA_QUEUE_CPU_PHYSICAL_MASK UINT32_C(0x0FFFFFFF)
#define SATURN_DMA_QUEUE_LWRAM_BASE UINT32_C(0x00200000)
#define SATURN_DMA_QUEUE_LWRAM_SIZE UINT32_C(0x00100000)

typedef struct saturn_dma_request {
        void *dst;
        const void *src;
        size_t len;
        saturn_dma_queue_mode_t mode;
        saturn_dma_queue_sequence_t sequence;
} saturn_dma_request_t;
static saturn_dma_request_t _queue[SATURN_DMA_QUEUE_CAPACITY];
static size_t _head;
static size_t _tail;
static bool _active;
static volatile bool _cpu_dmac_completed;
static saturn_dma_queue_sequence_t _next_sequence;
static uint32_t _wait_ticks;

typedef enum saturn_dma_completion_status {
        SATURN_DMA_COMPLETION_NONE = 0,
        SATURN_DMA_COMPLETION_RETIRED,
        SATURN_DMA_COMPLETION_FAILED,
} saturn_dma_completion_status_t;
typedef struct saturn_dma_completion {
        saturn_dma_queue_sequence_t sequence;
        saturn_dma_completion_status_t status;
} saturn_dma_completion_t;
static saturn_dma_completion_t _completions[SATURN_DMA_QUEUE_CAPACITY];
static size_t _completion_head;

static size_t
_next_index(size_t index)
{
        return (index + 1U) & (SATURN_DMA_QUEUE_CAPACITY - 1U);
}

static void
_cpu_dmac_complete_ihr(void *work)
{
        (void)work;
        _cpu_dmac_completed = true;
}

static bool
_scu_range_intersects_lwram(const void *address, size_t len)
{
        const uint64_t start = (uintptr_t)address &
            SATURN_DMA_QUEUE_CPU_PHYSICAL_MASK;
        const uint64_t end = start + (uint64_t)len;
        const uint64_t lwram_end = (uint64_t)SATURN_DMA_QUEUE_LWRAM_BASE +
            SATURN_DMA_QUEUE_LWRAM_SIZE;
        return start < lwram_end && end > SATURN_DMA_QUEUE_LWRAM_BASE;
}

static bool
_request_valid(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode)
{
        if (dst == NULL || src == NULL || len == 0U) {
                return false;
        }
        if (mode != SATURN_DMA_QUEUE_CPU && mode != SATURN_DMA_QUEUE_SCU &&
            mode != SATURN_DMA_QUEUE_CPU_DMAC) {
                return false;
        }
        if (mode == SATURN_DMA_QUEUE_CPU_DMAC) {
                return ((((uintptr_t)dst | (uintptr_t)src | len) & 3U) == 0U);
        }
        if (mode != SATURN_DMA_QUEUE_SCU) {
                return true;
        }
        if (len > UINT32_MAX) {
                return false;
        }
        return !_scu_range_intersects_lwram(dst, len) &&
            !_scu_range_intersects_lwram(src, len);
}

static saturn_dma_completion_status_t
_completion_status(saturn_dma_queue_sequence_t sequence)
{
        for (size_t index = 0U; index < SATURN_DMA_QUEUE_CAPACITY; index++) {
                if (_completions[index].sequence == sequence) {
                        return _completions[index].status;
                }
        }
        return SATURN_DMA_COMPLETION_NONE;
}

static void
_completion_record(saturn_dma_queue_sequence_t sequence,
    saturn_dma_completion_status_t status)
{
        _completions[_completion_head] = (saturn_dma_completion_t){
            sequence, status
        };
        _completion_head = _next_index(_completion_head);
}

static bool
_sequence_outstanding(saturn_dma_queue_sequence_t sequence)
{
        for (size_t index = _tail; index != _head;
             index = _next_index(index)) {
                if (_queue[index].sequence == sequence) {
                        return true;
                }
        }
        return false;
}

void
saturn_dma_queue_init(void)
{
        /* Channel 0 becomes queue-owned only after boot-time users have
         * retired. Reset its enable bit once at that ownership handoff; all
         * later completion is signalled by our configured IHR. */
        cpu_dmac_channel_stop(0);
        cpu_dmac_enable();
        _head = 0U;
        _tail = 0U;
        _active = false;
        _cpu_dmac_completed = false;
        _next_sequence = SATURN_DMA_QUEUE_INITIAL_SEQUENCE;
        if (_next_sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                _next_sequence = 1U;
        }
        memset(_completions, 0, sizeof(_completions));
        _completion_head = 0U;
        _wait_ticks = 0U;
}

saturn_dma_queue_sequence_t
saturn_dma_queue_submit(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode)
{
        if (!_request_valid(dst, src, len, mode)) {
                return SATURN_DMA_QUEUE_SEQUENCE_INVALID;
        }
        const size_t next = _next_index(_head);
        if (next == _tail) {
                return SATURN_DMA_QUEUE_SEQUENCE_INVALID;
        }
        if (_next_sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                _next_sequence = 1U;
        }
        const saturn_dma_queue_sequence_t sequence = _next_sequence++;
        _queue[_head] = (saturn_dma_request_t){ dst, src, len, mode, sequence };
        _head = next;
        return sequence;
}

int
saturn_dma_queue_submit_pair(
    void *first_dst, const void *first_src, size_t first_len,
    saturn_dma_queue_mode_t first_mode,
    void *second_dst, const void *second_src, size_t second_len,
    saturn_dma_queue_mode_t second_mode,
    saturn_dma_queue_sequence_t *first_sequence,
    saturn_dma_queue_sequence_t *second_sequence)
{
        if (first_sequence == NULL || second_sequence == NULL) {
                return 0;
        }
        *first_sequence = SATURN_DMA_QUEUE_SEQUENCE_INVALID;
        *second_sequence = SATURN_DMA_QUEUE_SEQUENCE_INVALID;
        if (!_request_valid(first_dst, first_src, first_len, first_mode) ||
            !_request_valid(second_dst, second_src, second_len, second_mode)) {
                return 0;
        }
        const size_t after_first = _next_index(_head);
        const size_t after_second = _next_index(after_first);
        if (after_first == _tail || after_second == _tail) {
                return 0;
        }
        if (_next_sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                _next_sequence = 1U;
        }
        *first_sequence = _next_sequence++;
        if (_next_sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                _next_sequence = 1U;
        }
        *second_sequence = _next_sequence++;
        _queue[_head] = (saturn_dma_request_t){ first_dst, first_src,
            first_len, first_mode, *first_sequence };
        _queue[after_first] = (saturn_dma_request_t){ second_dst, second_src,
            second_len, second_mode, *second_sequence };
        _head = after_second;
        return 1;
}

void
saturn_dma_queue_kick(void)
{
        if (_active || _tail == _head) {
                return;
        }
        const saturn_dma_request_t *request = &_queue[_tail];
        if (request->mode == SATURN_DMA_QUEUE_SCU &&
            scu_dma_level_busy(0) != 0U) {
                return;
        }
        _active = true;
        if (request->mode == SATURN_DMA_QUEUE_SCU) {
                if (request->len != 0U) {
                        scu_dma_transfer(0, request->dst, request->src,
                                         request->len);
                }
        } else if (request->mode == SATURN_DMA_QUEUE_CPU_DMAC) {
                const cpu_dmac_cfg_t config = {
                    .channel = 0,
                    .src_mode = CPU_DMAC_SOURCE_INCREMENT,
                    .dst_mode = CPU_DMAC_DESTINATION_INCREMENT,
                    .stride = CPU_DMAC_STRIDE_4_BYTES,
                    .bus_mode = CPU_DMAC_BUS_MODE_CYCLE_STEAL,
                    .src = (uintptr_t)request->src,
                    .dst = CPU_CACHE_THROUGH | (uintptr_t)request->dst,
                    .len = request->len,
                    .ihr = _cpu_dmac_complete_ihr,
                    .ihr_work = NULL,
                };
                _cpu_dmac_completed = false;
                cpu_dmac_channel_config_set(&config);
                cpu_dmac_channel_start(0);
                cpu_dmac_enable();
        } else {
                if (request->len != 0U) {
                        memcpy(request->dst, request->src, request->len);
                }
        }
}

void saturn_dma_queue_drain(void)
{
        if (_tail != _head) {
                size_t last = _head == 0U ? SATURN_DMA_QUEUE_CAPACITY - 1U :
                    _head - 1U;
                saturn_dma_queue_wait(_queue[last].sequence);
        }
}

void
saturn_dma_queue_poll(void)
{
        if (!_active) {
                return;
        }
        const saturn_dma_request_t *request = &_queue[_tail];
        if (request->mode == SATURN_DMA_QUEUE_SCU && request->len != 0U &&
            scu_dma_level_busy(0) != 0U) {
                return;
        }
        if (request->mode == SATURN_DMA_QUEUE_CPU_DMAC &&
            request->len != 0U) {
                cpu_dmac_status_t status;
                cpu_dmac_status_get(&status);
                if (status.address_error != 0U || status.nmi_interrupt != 0U) {
                        cpu_dmac_channel_stop(0);
                        _completion_record(request->sequence,
                                           SATURN_DMA_COMPLETION_FAILED);
                        _tail = _next_index(_tail);
                        _active = false;
                        return;
                }
                /* Pinned Yaul computes channel_busy incorrectly for the
                 * hardware's active DE=1/TE=0 state. Only the configured
                 * completion interrupt is authoritative. */
                if (!_cpu_dmac_completed) {
                        return;
                }
        }
        _completion_record(request->sequence, SATURN_DMA_COMPLETION_RETIRED);
        _tail = _next_index(_tail);
        _active = false;
}

int
saturn_dma_queue_sequence_retired(saturn_dma_queue_sequence_t sequence)
{
        return sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID &&
            _completion_status(sequence) == SATURN_DMA_COMPLETION_RETIRED;
}

int
saturn_dma_queue_sequence_failed(saturn_dma_queue_sequence_t sequence)
{
        return sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID &&
            _completion_status(sequence) == SATURN_DMA_COMPLETION_FAILED;
}

int
saturn_dma_queue_sequence_started(saturn_dma_queue_sequence_t sequence)
{
        return sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID && _active &&
            _tail != _head && _queue[_tail].sequence == sequence;
}

int
saturn_dma_queue_wait(saturn_dma_queue_sequence_t sequence)
{
        if (sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                return 0;
        }
        if (saturn_dma_queue_sequence_failed(sequence)) {
                return 0;
        }
        /* A caller may recheck a completed fence. Modular subtraction keeps
         * this correct across UINT32_MAX -> 1 while the fixed FIFO bounds the
         * live sequence distance to fewer than 16 descriptors. */
        if (saturn_dma_queue_sequence_retired(sequence)) {
                return 1;
        }
        if (!_sequence_outstanding(sequence)) {
                return 0;
        }
        /* Only measure time spent at a real outstanding fence. The host queue
         * fixture has no FRT device, so its behavior remains deterministic while
         * the Saturn path records the actual wait interval. */
#if !defined(SATURN_DMA_QUEUE_HOST_TEST)
        const uint16_t wait_start = cpu_frt_count_get();
#endif
        while (!saturn_dma_queue_sequence_retired(sequence)) {
                saturn_dma_queue_kick();
                saturn_dma_queue_poll();
                if (saturn_dma_queue_sequence_failed(sequence)) {
                        return 0;
                }
        }
#if !defined(SATURN_DMA_QUEUE_HOST_TEST)
        _wait_ticks += (uint16_t)(cpu_frt_count_get() - wait_start);
#endif
        return 1;
}

uint32_t
saturn_dma_queue_wait_ticks_take(void)
{
        const uint32_t ticks = _wait_ticks;
        _wait_ticks = 0U;
        return ticks;
}

int
saturn_dma_queue_idle(void)
{
        saturn_dma_queue_poll();
        return !_active && _tail == _head;
}

void
saturn_dma_queue_transfer_wait(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode)
{
        if (!_request_valid(dst, src, len, mode)) {
                return;
        }
        saturn_dma_queue_sequence_t sequence = saturn_dma_queue_submit(
            dst, src, len, mode);
        while (sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                saturn_dma_queue_drain();
                sequence = saturn_dma_queue_submit(dst, src, len, mode);
        }
        saturn_dma_queue_wait(sequence);
}
