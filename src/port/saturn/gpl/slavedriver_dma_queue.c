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

static size_t
_next_index(size_t index)
{
        return (index + 1U) & (SATURN_DMA_QUEUE_CAPACITY - 1U);
}

void
saturn_dma_queue_init(void)
{
        _head = 0U;
        _tail = 0U;
        _active = false;
        _next_sequence = 1U;
        _retired_sequence = SATURN_DMA_QUEUE_SEQUENCE_INVALID;
}

saturn_dma_queue_sequence_t
saturn_dma_queue_submit(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode)
{
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

void
saturn_dma_queue_wait(saturn_dma_queue_sequence_t sequence)
{
        if (sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                return;
        }
        while (_retired_sequence != sequence) {
                saturn_dma_queue_kick();
                saturn_dma_queue_poll();
        }
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
        saturn_dma_queue_sequence_t sequence = saturn_dma_queue_submit(
            dst, src, len, mode);
        while (sequence == SATURN_DMA_QUEUE_SEQUENCE_INVALID) {
                saturn_dma_queue_drain();
                sequence = saturn_dma_queue_submit(dst, src, len, mode);
        }
        saturn_dma_queue_wait(sequence);
}
