/*
 * GPL-3.0-or-later. Close-port of the queueing design in
 * Lobotomy-Software/SlaveDriver-Engine, DMA.C/DMA.H, commit
 * a8986591557b6e680550d3c23970284d3b38ff8f.
 *
 * This Yaul adaptation uses uintptr_t-safe bookkeeping and libyaul's SCU-DMA
 * API instead of the original SDK's raw register writes.
 */
#ifndef SM64_SATURN_SLAVEDRIVER_DMA_QUEUE_H
#define SM64_SATURN_SLAVEDRIVER_DMA_QUEUE_H
#include <stdint.h>
#include <stddef.h>

#define SATURN_DMA_QUEUE_CAPACITY 16U

typedef uint32_t saturn_dma_queue_sequence_t;
#define SATURN_DMA_QUEUE_SEQUENCE_INVALID ((saturn_dma_queue_sequence_t)0U)

typedef enum saturn_dma_queue_mode {
        SATURN_DMA_QUEUE_CPU,
        SATURN_DMA_QUEUE_SCU
} saturn_dma_queue_mode_t;

void saturn_dma_queue_init(void);
saturn_dma_queue_sequence_t saturn_dma_queue_submit(
    void *dst, const void *src, size_t len, saturn_dma_queue_mode_t mode);
void saturn_dma_queue_kick(void);
void saturn_dma_queue_poll(void);
void saturn_dma_queue_wait(saturn_dma_queue_sequence_t sequence);
int saturn_dma_queue_idle(void);

/* Compatibility helpers for one-shot boot uploads. Frame work should submit,
 * defer kick to its VRAM dependency boundary, then wait for the returned
 * completion sequence before reusing the source bank. */
void saturn_dma_queue_drain(void);
void saturn_dma_queue_transfer_wait(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode);
#endif
