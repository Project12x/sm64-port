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
        SATURN_DMA_QUEUE_SCU,
        SATURN_DMA_QUEUE_CPU_DMAC
} saturn_dma_queue_mode_t;

/* Called after all boot DMA has retired. From this handoff until shutdown the
 * queue exclusively owns master-CPU DMAC channel 0 and SCU-DMA level 0. */
void saturn_dma_queue_init(void);
/* Read-only request validation. This is the single address/mode authority used
 * by submit and by callers that must prove a complete plan before mutation. */
int saturn_dma_queue_request_valid(
    void *dst, const void *src, size_t len, saturn_dma_queue_mode_t mode);
saturn_dma_queue_sequence_t saturn_dma_queue_submit(
    void *dst, const void *src, size_t len, saturn_dma_queue_mode_t mode);
int saturn_dma_queue_submit_pair(
    void *first_dst, const void *first_src, size_t first_len,
    saturn_dma_queue_mode_t first_mode,
    void *second_dst, const void *second_src, size_t second_len,
    saturn_dma_queue_mode_t second_mode,
    saturn_dma_queue_sequence_t *first_sequence,
    saturn_dma_queue_sequence_t *second_sequence);
void saturn_dma_queue_kick(void);
void saturn_dma_queue_poll(void);
int saturn_dma_queue_sequence_retired(saturn_dma_queue_sequence_t sequence);
int saturn_dma_queue_sequence_failed(saturn_dma_queue_sequence_t sequence);
int saturn_dma_queue_sequence_started(saturn_dma_queue_sequence_t sequence);
/* Returns nonzero if sequence is already retired or was retired by this call.
 * Returns zero for an invalid or non-outstanding future sequence. */
int saturn_dma_queue_wait(saturn_dma_queue_sequence_t sequence);
/* Return and clear actual FRT ticks spent in queue waits since the previous
 * sample. This is diagnostic-only; it never changes queue scheduling. */
uint32_t saturn_dma_queue_wait_ticks_take(void);
int saturn_dma_queue_idle(void);

/* Compatibility helpers for one-shot boot uploads. Frame work should submit,
 * defer kick to its VRAM dependency boundary, then wait for the returned
 * completion sequence before reusing the source bank. */
void saturn_dma_queue_drain(void);
void saturn_dma_queue_transfer_wait(void *dst, const void *src, size_t len,
    saturn_dma_queue_mode_t mode);
#endif
