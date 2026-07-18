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
#include <stddef.h>
typedef enum saturn_dma_queue_mode {
        SATURN_DMA_QUEUE_CPU,
        SATURN_DMA_QUEUE_SCU
} saturn_dma_queue_mode_t;
void saturn_dma_queue_init(void);
int saturn_dma_queue_submit(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode);
void saturn_dma_queue_drain(void);
void saturn_dma_queue_transfer_wait(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode);
#endif
