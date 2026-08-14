#include "soundtest_boot.h"

#include "saturn_pcm_protocol.h"

static bool ready(const volatile uint8_t *sound_ram, uint16_t initial_heartbeat)
{
    return sm64_saturn_pcm_get_be16(sound_ram, SM64_SATURN_PCM_MAGIC_OFFSET) ==
               SM64_SATURN_PCM_PROTOCOL_MAGIC &&
           sm64_saturn_pcm_get_be16(sound_ram, SM64_SATURN_PCM_VERSION_OFFSET) ==
               SM64_SATURN_PCM_PROTOCOL_VERSION &&
           sm64_saturn_pcm_get_be16(sound_ram, SM64_SATURN_PCM_STATUS_OFFSET) ==
               SM64_SATURN_PCM_STATUS_READY &&
           sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_HEARTBEAT_OFFSET) !=
               initial_heartbeat;
}

sm64_saturn_soundtest_boot_result_t sm64_saturn_soundtest_boot(
    const sm64_saturn_soundtest_boot_t *boot,
    sm64_saturn_pcm_transport_t *transport)
{
    uint32_t offset;
    uint16_t heartbeat;
    uint16_t tick;
    uint16_t words[7] = {0};
    if (boot == 0 || transport == 0 || boot->sound_ram == 0 ||
        boot->driver == 0 || boot->bank == 0 || boot->sound_off == 0 ||
        boot->set_512k_mode == 0 ||
        boot->copy_region == 0 || boot->sound_on == 0 ||
        boot->wait_vblank == 0 || boot->heartbeat_vblank_budget == 0U) {
        return SM64_SATURN_SOUNDTEST_BOOT_BAD_CONFIG;
    }
    if (boot->driver_bytes == 0U ||
        boot->driver_bytes > SM64_SATURN_PCM_DRIVER_END ||
        boot->bank_bytes == 0U || boot->bank_bytes > 32768U ||
        SM64_SATURN_PCM_BANK_OFFSET + boot->bank_bytes >
            SM64_SATURN_PCM_SOUND_RAM_BYTES) {
        return SM64_SATURN_SOUNDTEST_BOOT_BAD_ASSETS;
    }
    if (!boot->sound_off(boot->context)) {
        return SM64_SATURN_SOUNDTEST_BOOT_SOUND_OFF_FAILED;
    }
    /* Keep the SCSP CPU stopped while its entire RAM image is replaced.
     * Sourceboot already owns a running VDP/SCU pipeline; unlike a standalone
     * sample it must not execute uninitialized SCSP RAM during this handoff. */
    if (!boot->set_512k_mode(boot->context)) {
        return SM64_SATURN_SOUNDTEST_BOOT_512K_MODE_FAILED;
    }
    for (offset = 0U; offset < SM64_SATURN_PCM_SOUND_RAM_BYTES; ++offset) {
        boot->sound_ram[offset] = 0U;
    }
    if (!boot->sound_off(boot->context)) {
        return SM64_SATURN_SOUNDTEST_BOOT_SOUND_OFF_FAILED;
    }
    if (!boot->copy_region(boot->context, boot->sound_ram, boot->driver,
                           boot->driver_bytes) ||
        !boot->copy_region(boot->context,
                           boot->sound_ram + SM64_SATURN_PCM_BANK_OFFSET,
                           boot->bank, boot->bank_bytes)) {
        return SM64_SATURN_SOUNDTEST_BOOT_COPY_FAILED;
    }
    heartbeat = sm64_saturn_pcm_get_be16(boot->sound_ram,
                                         SM64_SATURN_PCM_HEARTBEAT_OFFSET);
    if (!boot->sound_on(boot->context)) {
        return SM64_SATURN_SOUNDTEST_BOOT_SOUND_ON_FAILED;
    }
    for (tick = 0U; tick < boot->heartbeat_vblank_budget; ++tick) {
        boot->wait_vblank(boot->context);
        if (ready(boot->sound_ram, heartbeat)) {
            sm64_saturn_pcm_transport_init(transport, boot->sound_ram);
            words[0] = boot->initial_master_volume;
            if (!sm64_saturn_audio_control_enqueue(
                    transport, SM64_SATURN_AUDIO_OPCODE_SET_MASTER, words)) {
                return SM64_SATURN_SOUNDTEST_BOOT_ENQUEUE_FAILED;
            }
            words[0] = 0U;
            words[1] = 12U;
            words[2] = 0U;
            if (!sm64_saturn_audio_sfx_enqueue(
                    transport, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, words)) {
                return SM64_SATURN_SOUNDTEST_BOOT_ENQUEUE_FAILED;
            }
            return SM64_SATURN_SOUNDTEST_BOOT_READY;
        }
    }
    return SM64_SATURN_SOUNDTEST_BOOT_HEARTBEAT_TIMEOUT;
}
