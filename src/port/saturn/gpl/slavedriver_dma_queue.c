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
#define SATURN_DMA_QUEUE_SEQUENCE_HALF_RANGE UINT32_C(0x80000000)

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
static saturn_dma_queue_sequence_t _next_sequence;
static saturn_dma_queue_sequence_t _retired_sequence;
static uint32_t _wait_ticks;

static size_t
_next_index(size_t index)
{
        return (index + 1U) & (SATURN_DMA_QUEUE_CAPACITY - 1U);
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
        if (mode != SATURN_DMA_QUEUE_CPU && mode != SATURN_DMA_QUEUE_SCU) {
                return false;
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

static bool
_sequence_retired_through(saturn_dma_queue_sequence_t sequence)
{
        return _retired_sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID &&
            (uint32_t)(_retired_sequence - sequence) <
                SATURN_DMA_QUEUE_SEQUENCE_HALF_RANGE;
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
        _head = 0U;
        _tail = 0U;
        _active = false;
        _next_sequence = SATURN_DMA_QUEUE_INITIAL_SEQUENCE;
        if (_next_sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                _next_sequence = 1U;
        }
        _retired_sequence = SATURN_DMA_QUEUE_SEQUENCE_INVALID;
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

void
saturn_dma_queue_kick(void)
{
        if (_active || _tail == _head) {
                return;
        }
        const saturn_dma_request_t *request = &_queue[_tail];
        _active = true;
        if (request->mode == SATURN_DMA_QUEUE_SCU) {
                if (request->len != 0U) {
                        scu_dma_transfer(0, request->dst, request->src,
                                         request->len);
                }
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
        _retired_sequence = request->sequence;
        _tail = _next_index(_tail);
        _active = false;
}

int
saturn_dma_queue_wait(saturn_dma_queue_sequence_t sequence)
{
        if (sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                return 0;
        }
        /* A caller may recheck a completed fence. Modular subtraction keeps
         * this correct across UINT32_MAX -> 1 while the fixed FIFO bounds the
         * live sequence distance to fewer than 16 descriptors. */
        if (_sequence_retired_through(sequence)) {
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
        while (!_sequence_retired_through(sequence)) {
                saturn_dma_queue_kick();
                saturn_dma_queue_poll();
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
