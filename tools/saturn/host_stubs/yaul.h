#ifndef SM64_SATURN_DMA_QUEUE_HOST_STUB_YAUL_H
#define SM64_SATURN_DMA_QUEUE_HOST_STUB_YAUL_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t scu_dma_level_t;
typedef uint32_t cpu_dmac_channel_t;
typedef uint32_t cpu_dmac_src_t;
typedef uint32_t cpu_dmac_dst_t;
typedef uint32_t cpu_dmac_stride_t;
typedef uint32_t cpu_dmac_bus_mode_t;
typedef void (*cpu_dmac_ihr_t)(void *);

/* Mirrors the scalar layout used by the pinned VDP1 partition ABI.  Host
 * source-scene tests need this complete type to retain the actor-only region
 * that target code receives from Yaul. */
typedef struct vdp1_vram_partitions {
    void *cmdt_base;
    uint32_t cmdt_size;
    void *texture_base;
    uint32_t texture_size;
    void *gouraud_base;
    uint32_t gouraud_size;
    void *clut_base;
    uint32_t clut_size;
    void *remaining_base;
    uint32_t remaining_size;
} vdp1_vram_partitions_t;

#define CPU_DMAC_SOURCE_INCREMENT 1U
#define CPU_DMAC_DESTINATION_INCREMENT 1U
#define CPU_DMAC_STRIDE_4_BYTES 2U
#define CPU_DMAC_BUS_MODE_CYCLE_STEAL 0U
#define CPU_CACHE_THROUGH ((uintptr_t)0U)

typedef struct cpu_dmac_cfg {
    cpu_dmac_channel_t channel;
    cpu_dmac_src_t src_mode;
    cpu_dmac_dst_t dst_mode;
    cpu_dmac_stride_t stride;
    cpu_dmac_bus_mode_t bus_mode;
    uintptr_t src;
    uintptr_t dst;
    uint32_t len;
    cpu_dmac_ihr_t ihr;
    void *ihr_work;
} cpu_dmac_cfg_t;

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
void cpu_dmac_channel_config_set(const cpu_dmac_cfg_t *cfg);
void cpu_dmac_channel_start(cpu_dmac_channel_t channel);
void cpu_dmac_channel_stop(cpu_dmac_channel_t channel);
void cpu_dmac_enable(void);
void cpu_dmac_transfer(cpu_dmac_channel_t channel, void *dst,
                       const void *src, size_t len);
void cpu_dmac_transfer_wait(cpu_dmac_channel_t channel);

#endif
