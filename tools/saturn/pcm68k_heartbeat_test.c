#include <assert.h>
#include <stdint.h>

#include "pcm68k_heartbeat.h"
#include "saturn_pcm_protocol.h"

int main(void)
{
    uint8_t sound_ram[SM64_SATURN_PCM_MAILBOX_OFFSET + 16U] = {0};
    uint16_t heartbeat = 0xFFFFU;

    sm64_saturn_pcm68k_publish_boot(sound_ram);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_MAGIC_OFFSET) ==
           SM64_SATURN_PCM_PROTOCOL_MAGIC);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_VERSION_OFFSET) ==
           SM64_SATURN_PCM_PROTOCOL_VERSION);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_STATUS_OFFSET) ==
           SM64_SATURN_PCM_STATUS_BOOTING);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_HEARTBEAT_OFFSET) == 0U);

    sm64_saturn_pcm68k_publish_tick(sound_ram, &heartbeat);
    assert(heartbeat == 0U);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_STATUS_OFFSET) ==
           SM64_SATURN_PCM_STATUS_READY);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_HEARTBEAT_OFFSET) == 0U);

    sm64_saturn_pcm68k_publish_tick(sound_ram, &heartbeat);
    assert(heartbeat == 1U);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_HEARTBEAT_OFFSET) == 1U);
    return 0;
}
