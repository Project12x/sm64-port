#ifndef SM64_SATURN_DMA_QUEUE_HOST_STUB_YAUL_H
#define SM64_SATURN_DMA_QUEUE_HOST_STUB_YAUL_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t scu_dma_level_t;

void scu_dma_transfer(scu_dma_level_t level, void *dst, const void *src,
                      size_t len);
uint32_t scu_dma_level_busy(scu_dma_level_t level);
void scu_dma_transfer_wait(scu_dma_level_t level);

#endif
