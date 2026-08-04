/* Source-built Saturn MC68000 heartbeat service.
 *
 * The fixed-location, separately built sound-CPU boundary follows the pattern
 * studied in ponut64/SCSP_poneSound@31782e4c61337327f23eb9aa45ecd37fe0944ea0
 * (MIT). This polling loop and byte-addressed mailbox ABI are original project
 * code; no upstream sound driver or control structure is copied.
 */
#include <stdint.h>

#include "pcm68k_heartbeat.h"
#include "saturn_pcm_protocol.h"

void sm64_saturn_pcm68k_publish_boot(volatile uint8_t *sound_ram)
{
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_VERSION_OFFSET,
                            SM64_SATURN_PCM_PROTOCOL_VERSION);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_STATUS_OFFSET,
                            SM64_SATURN_PCM_STATUS_BOOTING);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_HEARTBEAT_OFFSET, 0);
}

void sm64_saturn_pcm68k_publish_tick(volatile uint8_t *sound_ram,
                                     uint16_t *heartbeat)
{
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_STATUS_OFFSET,
                            SM64_SATURN_PCM_STATUS_READY);
    *heartbeat = (uint16_t)(*heartbeat + 1U);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_HEARTBEAT_OFFSET,
                            *heartbeat);
}

void pcm68k_main(void)
{
    volatile uint8_t *const sound_ram = (volatile uint8_t *)(uintptr_t)0;
    uint16_t heartbeat = 0;

    sm64_saturn_pcm68k_publish_boot(sound_ram);

    for (;;) {
        sm64_saturn_pcm68k_publish_tick(sound_ram, &heartbeat);
    }
}
