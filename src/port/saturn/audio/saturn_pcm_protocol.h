/* Saturn SH-2 <-> MC68000 semantic-audio wire protocol.
 *
 * The fixed sound-RAM mailbox is an original, byte-addressed ABI. It uses
 * the separate-binary/fixed-location pattern studied in
 * ponut64/SCSP_poneSound@31782e4c61337327f23eb9aa45ecd37fe0944ea0
 * (MIT; documentation.md, PROJ/main.c, jo_demo/pcmsys.{c,h}). No upstream
 * struct or driver code is copied. See docs/saturn/audio/PCM68K_PROVENANCE.md.
 */
#ifndef SM64_SATURN_PCM_PROTOCOL_H
#define SM64_SATURN_PCM_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SM64_SATURN_PCM_SOUND_RAM_BYTES = 0x80000U,
    SM64_SATURN_PCM_DRIVER_END = 0x04000U,
    SM64_SATURN_PCM_MAILBOX_OFFSET = 0x04000U,
    SM64_SATURN_PCM_MAILBOX_BYTES = 0x01000U,
    SM64_SATURN_PCM_MAGIC_OFFSET = 0x04000U,
    SM64_SATURN_PCM_VERSION_OFFSET = 0x04002U,
    SM64_SATURN_PCM_STATUS_OFFSET = 0x04004U,
    SM64_SATURN_PCM_HEARTBEAT_OFFSET = 0x04006U,

    SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET = 0x04008U,
    SM64_SATURN_PCM_CONTROL_CONSUMER_OFFSET = 0x0400AU,
    SM64_SATURN_PCM_SFX_PRODUCER_OFFSET = 0x0400CU,
    SM64_SATURN_PCM_SFX_CONSUMER_OFFSET = 0x0400EU,
    SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET = 0x04010U,
    SM64_SATURN_PCM_VOICES_STARTED_OFFSET = 0x04012U,
    SM64_SATURN_PCM_UNKNOWN_OPCODES_OFFSET = 0x04014U,
    SM64_SATURN_PCM_LAST_OPCODE_OFFSET = 0x04016U,
    SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET = 0x04018U,
    SM64_SATURN_PCM_INVALID_SAMPLES_OFFSET = 0x0401AU,
    SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET = 0x0401CU,
    SM64_SATURN_PCM_CONTROL_SATURATED_OFFSET = 0x0401EU,
    SM64_SATURN_PCM_SFX_SATURATED_OFFSET = 0x04020U,
    SM64_SATURN_PCM_CONTROL_CONSUMED_OFFSET = 0x04022U,
    SM64_SATURN_PCM_SFX_CONSUMED_OFFSET = 0x04024U,

    SM64_SATURN_PCM_CONTROL_RING_OFFSET = 0x04040U,
    SM64_SATURN_PCM_CONTROL_RING_COUNT = 8U,
    SM64_SATURN_PCM_COMMAND_BYTES = 16U,
    SM64_SATURN_PCM_CONTROL_RING_BYTES =
        SM64_SATURN_PCM_CONTROL_RING_COUNT * SM64_SATURN_PCM_COMMAND_BYTES,
    SM64_SATURN_PCM_SFX_RING_OFFSET = 0x040C0U,
    SM64_SATURN_PCM_SFX_RING_COUNT = 24U,
    SM64_SATURN_PCM_SFX_RING_BYTES =
        SM64_SATURN_PCM_SFX_RING_COUNT * SM64_SATURN_PCM_COMMAND_BYTES,

    SM64_SATURN_PCM_RESERVE_OFFSET = 0x05000U,
    SM64_SATURN_PCM_BANK_OFFSET = 0x08000U,
    SM64_SATURN_PCM_PROTOCOL_MAGIC = 0x5036U,
    SM64_SATURN_PCM_PROTOCOL_VERSION_V1 = 1U,
    SM64_SATURN_PCM_PROTOCOL_VERSION = 2U,
    SM64_SATURN_PCM_STATUS_BOOTING = 1U,
    SM64_SATURN_PCM_STATUS_READY = 2U,

    /* Historical v1 proof constants. They document the owner-heard artifact;
     * no v2 producer or consumer accepts this layout. */
    SM64_SATURN_PCM_V1_PRODUCER_OFFSET = 0x04008U,
    SM64_SATURN_PCM_V1_CONSUMER_OFFSET = 0x0400AU,
    SM64_SATURN_PCM_V1_COMMANDS_CONSUMED_OFFSET = 0x0400CU,
    SM64_SATURN_PCM_V1_VOICES_STARTED_OFFSET = 0x0400EU,
    SM64_SATURN_PCM_V1_UNKNOWN_OPCODES_OFFSET = 0x04010U,
    SM64_SATURN_PCM_V1_LAST_OPCODE_OFFSET = 0x04012U,
    SM64_SATURN_PCM_V1_ACTIVE_SLOT_OFFSET = 0x04014U,
    SM64_SATURN_PCM_V1_INVALID_SAMPLES_OFFSET = 0x04016U,
    SM64_SATURN_PCM_V1_PROTOCOL_FAULTS_OFFSET = 0x04018U,
    SM64_SATURN_PCM_V1_RING_OFFSET = 0x04040U,
    SM64_SATURN_PCM_V1_RING_COUNT = 32U,
    SM64_SATURN_PCM_V1_RING_BYTES = 512U,
    SM64_SATURN_PCM_V1_OPCODE_PLAY = 1U,
    SM64_SATURN_PCM_V1_OPCODE_STOP_ALL = 2U,
    SM64_SATURN_PCM_V1_OPCODE_SET_MASTER = 3U,
};

typedef enum sm64_saturn_audio_opcode {
    SM64_SATURN_AUDIO_OPCODE_NOP = 0x0000U,
    SM64_SATURN_AUDIO_OPCODE_RESET = 0x0001U,
    SM64_SATURN_AUDIO_OPCODE_MUTE = 0x0002U,
    SM64_SATURN_AUDIO_OPCODE_SET_MASTER = 0x0003U,
    SM64_SATURN_AUDIO_OPCODE_PACKAGE_PREPARE = 0x0004U,
    SM64_SATURN_AUDIO_OPCODE_PACKAGE_COMMIT = 0x0005U,
    SM64_SATURN_AUDIO_OPCODE_SEQ_START = 0x0006U,
    SM64_SATURN_AUDIO_OPCODE_SEQ_STOP = 0x0007U,
    SM64_SATURN_AUDIO_OPCODE_SEQ_FADE = 0x0008U,
    SM64_SATURN_AUDIO_OPCODE_SEQ_CHANNEL_FADE = 0x0009U,
    SM64_SATURN_AUDIO_OPCODE_BANK_MASK = 0x000AU,
    SM64_SATURN_AUDIO_OPCODE_SOUND_MODE = 0x000BU,
    SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH = 0x0100U,
    SM64_SATURN_AUDIO_OPCODE_STOP_HANDLE = 0x0101U,
    SM64_SATURN_AUDIO_OPCODE_STOP_SOURCE = 0x0102U,
    SM64_SATURN_AUDIO_OPCODE_STOP_BANK = 0x0103U,
} sm64_saturn_audio_opcode_t;

_Static_assert(SM64_SATURN_PCM_DRIVER_END == SM64_SATURN_PCM_MAILBOX_OFFSET,
               "driver must end at mailbox");
_Static_assert(SM64_SATURN_PCM_CONTROL_RING_OFFSET >=
                   SM64_SATURN_PCM_MAILBOX_OFFSET,
               "control ring must begin in mailbox");
_Static_assert(SM64_SATURN_PCM_SFX_CONSUMED_OFFSET + 2U <=
                   SM64_SATURN_PCM_CONTROL_RING_OFFSET,
               "status header must end before rings");
_Static_assert(SM64_SATURN_PCM_CONTROL_RING_OFFSET +
                       SM64_SATURN_PCM_CONTROL_RING_BYTES ==
                   SM64_SATURN_PCM_SFX_RING_OFFSET,
               "control and SFX rings must be contiguous");
_Static_assert(SM64_SATURN_PCM_SFX_RING_OFFSET +
                       SM64_SATURN_PCM_SFX_RING_BYTES <=
                   SM64_SATURN_PCM_MAILBOX_OFFSET +
                       SM64_SATURN_PCM_MAILBOX_BYTES,
               "rings must fit in mailbox");
_Static_assert(SM64_SATURN_PCM_RESERVE_OFFSET >=
                   SM64_SATURN_PCM_MAILBOX_OFFSET +
                       SM64_SATURN_PCM_MAILBOX_BYTES,
               "reserve must not overlap mailbox");
_Static_assert(SM64_SATURN_PCM_BANK_OFFSET > SM64_SATURN_PCM_RESERVE_OFFSET,
               "PCM bank must follow reserve");
_Static_assert(SM64_SATURN_PCM_BANK_OFFSET < SM64_SATURN_PCM_SOUND_RAM_BYTES,
               "PCM bank must fit sound RAM");

static inline void
sm64_saturn_pcm_put_be16(volatile uint8_t *base, uint16_t offset,
                         uint16_t value)
{
    base[offset] = (uint8_t)(value >> 8);
    base[(uint16_t)(offset + 1U)] = (uint8_t)value;
}

static inline uint16_t
sm64_saturn_pcm_get_be16(const volatile uint8_t *base, uint16_t offset)
{
    return (uint16_t)(((uint16_t)base[offset] << 8) |
                      (uint16_t)base[(uint16_t)(offset + 1U)]);
}

/* Cursors range over two laps. Equal cursors are empty; a distance of count
 * is full, so all physical slots are usable without a shared count field. */
static inline bool
sm64_saturn_pcm_ring_cursor_is_valid(uint16_t cursor, uint16_t count)
{
    return count != 0U && cursor < (uint16_t)(count * 2U);
}

static inline uint16_t
sm64_saturn_pcm_ring_cursor_next(uint16_t cursor, uint16_t count)
{
    const uint16_t next = (uint16_t)(cursor + 1U);
    return next == (uint16_t)(count * 2U) ? 0U : next;
}

static inline uint16_t
sm64_saturn_pcm_ring_cursor_slot(uint16_t cursor, uint16_t count)
{
    return cursor >= count ? (uint16_t)(cursor - count) : cursor;
}

static inline uint16_t
sm64_saturn_pcm_ring_occupancy(uint16_t producer, uint16_t consumer,
                               uint16_t count)
{
    return producer >= consumer
               ? (uint16_t)(producer - consumer)
               : (uint16_t)(producer + (uint16_t)(count * 2U) - consumer);
}

static inline uint16_t sm64_saturn_pcm_v1_ring_next(uint16_t index)
{
    return index + 1U == SM64_SATURN_PCM_V1_RING_COUNT
               ? 0U
               : (uint16_t)(index + 1U);
}

static inline bool
sm64_saturn_audio_opcode_is_control(sm64_saturn_audio_opcode_t opcode)
{
    return opcode >= SM64_SATURN_AUDIO_OPCODE_RESET &&
           opcode <= SM64_SATURN_AUDIO_OPCODE_SOUND_MODE;
}

static inline bool
sm64_saturn_audio_opcode_is_sfx(sm64_saturn_audio_opcode_t opcode)
{
    return opcode >= SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH &&
           opcode <= SM64_SATURN_AUDIO_OPCODE_STOP_BANK;
}

#endif
