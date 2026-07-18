/*
 * GPL-3.0-or-later. Close-port of Lobotomy-Software/SlaveDriver-Engine's
 * bounded DMA request ring (DMA.C), commit
 * a8986591557b6e680550d3c23970284d3b38ff8f.
 * Material modifications: Yaul/libyaul calls replace raw register pokes;
 * queue overflow is reported instead of relying on assert().
 */
#include "slavedriver_dma_queue.h"
#include <yaul.h>
#include <string.h>

#define SATURN_DMA_QUEUE_SIZE 16U
typedef struct saturn_dma_request {
        void *dst;
        const void *src;
        size_t len;
        saturn_dma_queue_mode_t mode;
} saturn_dma_request_t;
static saturn_dma_request_t _queue[SATURN_DMA_QUEUE_SIZE];
static size_t _head;
static size_t _tail;

void saturn_dma_queue_init(void) { _head = 0; _tail = 0; }

int
saturn_dma_queue_submit(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode)
{
        const size_t next = (_head + 1U) & (SATURN_DMA_QUEUE_SIZE - 1U);
        if (next == _tail) {
                return 0;
        }
        _queue[_head] = (saturn_dma_request_t){dst, src, len, mode};
        _head = next;
        return 1;
}

static void _run(const saturn_dma_request_t *request)
{
        if (request->len == 0U) {
                return;
        }
        if (request->mode == SATURN_DMA_QUEUE_SCU) {
                scu_dma_transfer(0, request->dst, request->src, request->len);
                scu_dma_transfer_wait(0);
        } else {
                memcpy(request->dst, request->src, request->len);
        }
}

void saturn_dma_queue_drain(void)
{
        while (_tail != _head) {
                _run(&_queue[_tail]);
                _tail = (_tail + 1U) & (SATURN_DMA_QUEUE_SIZE - 1U);
        }
}

void
saturn_dma_queue_transfer_wait(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode)
{
        while (!saturn_dma_queue_submit(dst, src, len, mode)) {
                saturn_dma_queue_drain();
        }
        saturn_dma_queue_drain();
}
