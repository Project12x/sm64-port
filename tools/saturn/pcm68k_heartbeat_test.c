#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "pcm68k_heartbeat.h"
#include "saturn_pcm_protocol.h"

int main(void)
{
    /* publish_boot also zeroes the music diagnostics at 0x7F00..0x7F1F, so
     * the model buffer must span sound RAM up to the PCM bank, not just the
     * mailbox window. */
    uint8_t sound_ram[SM64_SATURN_PCM_BANK_OFFSET];
    uint16_t heartbeat = 0xFFFFU;

    memset(sound_ram, 0xA5, sizeof(sound_ram));
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
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                     SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                     SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                     SM64_SATURN_PCM_SFX_PRODUCER_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(sound_ram,
                     SM64_SATURN_PCM_SFX_CONSUMER_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               sound_ram, SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               sound_ram, SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               sound_ram, SM64_SATURN_PCM_CONTROL_SATURATED_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               sound_ram, SM64_SATURN_PCM_SFX_SATURATED_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               sound_ram, SM64_SATURN_PCM_CONTROL_CONSUMED_OFFSET) == 0U);
    assert(sm64_saturn_pcm_get_be16(
               sound_ram, SM64_SATURN_PCM_SFX_CONSUMED_OFFSET) == 0U);

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
