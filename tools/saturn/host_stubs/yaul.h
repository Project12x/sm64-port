#ifndef SM64_SATURN_DMA_QUEUE_HOST_STUB_YAUL_H
#define SM64_SATURN_DMA_QUEUE_HOST_STUB_YAUL_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t scu_dma_level_t;
typedef uint32_t cpu_dmac_channel_t;

typedef struct cpu_dmac_status {
    unsigned int enabled:1;
    unsigned int priority_mode:1;
    unsigned int address_error:1;
    unsigned int nmi_interrupt:1;
    unsigned int channel_enabled:2;
    unsigned int channel_busy:2;
} cpu_dmac_status_t;

void scu_dma_transfer(scu_dma_level_t level, void *dst, const void *src,
                      size_t len);
uint32_t scu_dma_level_busy(scu_dma_level_t level);
void scu_dma_transfer_wait(scu_dma_level_t level);
void cpu_dmac_status_get(cpu_dmac_status_t *status);
void cpu_dmac_transfer(cpu_dmac_channel_t channel, void *dst,
                       const void *src, size_t len);
void cpu_dmac_transfer_wait(cpu_dmac_channel_t channel);

#endif
