/* Source-built Saturn MC68000 heartbeat service.
 *
 * The fixed-location, separately built sound-CPU boundary follows the pattern
 * studied in ponut64/SCSP_poneSound@31782e4c61337327f23eb9aa45ecd37fe0944ea0
 * (MIT). This polling loop and byte-addressed mailbox ABI are original project
 * code; no upstream sound driver or control structure is copied.
 */
#include <stdint.h>

#include "pcm68k_heartbeat.h"
#include "pcm_voice.h"
#include "scsp_regs.h"
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
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_SFX_PRODUCER_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_SFX_CONSUMER_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_VOICES_STARTED_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_UNKNOWN_OPCODES_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_LAST_OPCODE_OFFSET,
                            SM64_SATURN_AUDIO_OPCODE_NOP);
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET,
                            0xFFFFU);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_INVALID_SAMPLES_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_CONTROL_SATURATED_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_SFX_SATURATED_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_CONTROL_CONSUMED_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_SFX_CONSUMED_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_STARTS_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_FAULTS_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_VM_TICKS_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_ACTIVE_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_NOTES_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_SEQUENCE_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_SEQUENCE_BYTES_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_REJECT_MASK_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_MALFORMED_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_DROPPED_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_CONSUME_FAIL_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_MUSIC_SCSP_FAIL_OFFSET, 0);
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_SFX_REFRESHES_OFFSET, 0);
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

/* The driver state lives in .bss, not on the 1,020-byte reserved stack
 * (linker.ld: 0x3C00..0x3FFC).  A ~2 KB stack local here corrupted note
 * bindings on deep call frames (diagnostic failure 0x0340).  start.S clears
 * .bss before pcm68k_main, and sm64_saturn_pcm_voice_state_init() below
 * still performs the same explicit field initialization the local received. */
static sm64_saturn_pcm_voice_state_t s_voice_state;
_Static_assert(sizeof(sm64_saturn_pcm_voice_state_t) <= 768,
               "voice state must stay far below the 1020-byte reserved stack");

void pcm68k_main(void)
{
    volatile uint8_t *const sound_ram = (volatile uint8_t *)(uintptr_t)0;
    volatile uint8_t *const scsp_registers =
        (volatile uint8_t *)(uintptr_t)SM64_SATURN_SCSP_SLOT_BASE;
    uint16_t heartbeat = 0;

    sm64_saturn_pcm68k_publish_boot(sound_ram);
    sm64_saturn_pcm_voice_state_init(&s_voice_state);

    for (;;) {
        (void)sm64_saturn_pcm68k_consume_mapped_zero(scsp_registers,
                                                     &s_voice_state);
        sm64_saturn_pcm68k_publish_tick(sound_ram, &heartbeat);
    }
}
