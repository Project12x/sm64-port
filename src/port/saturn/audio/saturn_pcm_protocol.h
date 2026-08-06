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
    SM64_SATURN_PCM_COMPLETION_PRODUCER_OFFSET = 0x04026U,
    SM64_SATURN_PCM_COMPLETION_CONSUMER_OFFSET = 0x04028U,
    SM64_SATURN_PCM_COMPLETION_SATURATED_OFFSET = 0x0402AU,
    SM64_SATURN_PCM_COMPLETION_PROTOCOL_FAULTS_OFFSET = 0x0402CU,
    SM64_SATURN_PCM_ACTIVE_GENERATION_HIGH_OFFSET = 0x0402EU,
    SM64_SATURN_PCM_ACTIVE_GENERATION_LOW_OFFSET = 0x04030U,
    SM64_SATURN_PCM_PREPARED_GENERATION_HIGH_OFFSET = 0x04032U,
    SM64_SATURN_PCM_PREPARED_GENERATION_LOW_OFFSET = 0x04034U,
    SM64_SATURN_PCM_LAST_COMPLETION_STATUS_OFFSET = 0x04036U,
    SM64_SATURN_PCM_LAST_COMPLETION_DETAIL_OFFSET = 0x04038U,
    SM64_SATURN_PCM_SOUND_SERVICE_TICK_OFFSET = 0x0403AU,
    SM64_SATURN_PCM_ACTIVE_VOICE_COUNT_OFFSET = 0x0403CU,
    SM64_SATURN_PCM_ABI_FLAGS_OFFSET = 0x0403EU,

    SM64_SATURN_PCM_CONTROL_RING_OFFSET = 0x04040U,
    SM64_SATURN_PCM_CONTROL_RING_COUNT = 8U,
    SM64_SATURN_PCM_COMMAND_BYTES = 16U,
    SM64_SATURN_PCM_CONTROL_RING_BYTES =
        SM64_SATURN_PCM_CONTROL_RING_COUNT * SM64_SATURN_PCM_COMMAND_BYTES,
    SM64_SATURN_PCM_SFX_RING_OFFSET = 0x040C0U,
    SM64_SATURN_PCM_SFX_RING_COUNT = 24U,
    SM64_SATURN_PCM_SFX_RING_BYTES =
        SM64_SATURN_PCM_SFX_RING_COUNT * SM64_SATURN_PCM_COMMAND_BYTES,
    SM64_SATURN_PCM_COMPLETION_RING_OFFSET = 0x04240U,
    SM64_SATURN_PCM_COMPLETION_RING_COUNT = 32U,
    SM64_SATURN_PCM_COMPLETION_BYTES = 16U,
    SM64_SATURN_PCM_COMPLETION_RING_BYTES =
        SM64_SATURN_PCM_COMPLETION_RING_COUNT *
        SM64_SATURN_PCM_COMPLETION_BYTES,

    SM64_SATURN_PCM_RESERVE_OFFSET = 0x05000U,
    SM64_SATURN_PCM_BANK_OFFSET = 0x08000U,
    SM64_SATURN_PCM_PROTOCOL_MAGIC = 0x5036U,
    SM64_SATURN_PCM_PROTOCOL_VERSION_V1 = 1U,
    SM64_SATURN_PCM_PROTOCOL_VERSION = 2U,
    SM64_SATURN_PCM_STATUS_BOOTING = 1U,
    SM64_SATURN_PCM_STATUS_READY = 2U,
    SM64_SATURN_PCM_ABI_FLAG_COMPLETION = 0x0001U,
    SM64_SATURN_PCM_ABI_FLAG_STATUS_WRITING = 0x0002U,
    SM64_SATURN_PCM_ABI_STATUS_SEQUENCE_MASK = 0xFFFCU,
    SM64_SATURN_PCM_ABI_STATUS_PUBLICATION_STEP = 0x0004U,
    SM64_SATURN_PCM_COMPLETION_CONTROL_RESERVE = 8U,

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

typedef enum sm64_saturn_audio_ring_kind {
    SM64_SATURN_AUDIO_RING_CONTROL = 0U,
    SM64_SATURN_AUDIO_RING_SFX = 1U,
} sm64_saturn_audio_ring_kind_t;

typedef enum sm64_saturn_audio_completion_status {
    SM64_SATURN_AUDIO_COMPLETION_ACCEPTED = 1U,
    SM64_SATURN_AUDIO_COMPLETION_REJECTED = 2U,
    SM64_SATURN_AUDIO_COMPLETION_STALE = 3U,
    SM64_SATURN_AUDIO_COMPLETION_FAULT = 4U,
    SM64_SATURN_AUDIO_COMPLETION_DROPPED_SFX = 5U,
    SM64_SATURN_AUDIO_COMPLETION_FINISHED = 6U,
    SM64_SATURN_AUDIO_COMPLETION_COMMITTED = 7U,
    SM64_SATURN_AUDIO_COMPLETION_PREPARED = 8U,
} sm64_saturn_audio_completion_status_t;

_Static_assert(SM64_SATURN_PCM_DRIVER_END == SM64_SATURN_PCM_MAILBOX_OFFSET,
               "driver must end at mailbox");
_Static_assert(SM64_SATURN_PCM_CONTROL_RING_OFFSET >=
                   SM64_SATURN_PCM_MAILBOX_OFFSET,
               "control ring must begin in mailbox");
_Static_assert(SM64_SATURN_PCM_ABI_FLAGS_OFFSET + 2U <=
                   SM64_SATURN_PCM_CONTROL_RING_OFFSET,
               "status header must end before rings");
_Static_assert(SM64_SATURN_PCM_CONTROL_RING_OFFSET +
                       SM64_SATURN_PCM_CONTROL_RING_BYTES ==
                   SM64_SATURN_PCM_SFX_RING_OFFSET,
               "control and SFX rings must be contiguous");
_Static_assert(SM64_SATURN_PCM_SFX_RING_OFFSET +
                       SM64_SATURN_PCM_SFX_RING_BYTES ==
                   SM64_SATURN_PCM_COMPLETION_RING_OFFSET,
               "completion ring must follow command rings");
_Static_assert(SM64_SATURN_PCM_COMPLETION_RING_OFFSET +
                       SM64_SATURN_PCM_COMPLETION_RING_BYTES <=
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

static inline void
sm64_saturn_pcm_put_be32(volatile uint8_t *base, uint16_t high_offset,
                         uint32_t value)
{
    sm64_saturn_pcm_put_be16(base, high_offset, (uint16_t)(value >> 16));
    sm64_saturn_pcm_put_be16(base, (uint16_t)(high_offset + 2U),
                             (uint16_t)value);
}

static inline uint32_t
sm64_saturn_pcm_get_be32(const volatile uint8_t *base, uint16_t high_offset)
{
    return ((uint32_t)sm64_saturn_pcm_get_be16(base, high_offset) << 16) |
           (uint32_t)sm64_saturn_pcm_get_be16(
               base, (uint16_t)(high_offset + 2U));
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

static inline bool
sm64_saturn_audio_completion_status_is_valid(uint16_t status)
{
    return status >= SM64_SATURN_AUDIO_COMPLETION_ACCEPTED &&
           status <= SM64_SATURN_AUDIO_COMPLETION_PREPARED;
}

/* Package generations never wrap within a boot. Zero, duplicates, older
 * values, and an attempted UINT32_MAX -> 1 transition are rejected. */
static inline bool sm64_saturn_audio_generation_advances(
    uint32_t active_generation, uint32_t candidate_generation)
{
    return active_generation != 0U && candidate_generation != 0U &&
           candidate_generation > active_generation;
}

/* PLAY_REFRESH word 3 remains the existing 16-bit, non-wrapping per-boot
 * package epoch. Callers with a 32-bit package generation must use this
 * checked conversion; silent truncation would alias an old package. */
static inline bool sm64_saturn_audio_play_refresh_epoch_from_generation(
    uint32_t generation, uint16_t *epoch)
{
    if (epoch == 0 || generation == 0U || generation > 0xFFFFU) {
        return false;
    }
    *epoch = (uint16_t)generation;
    return true;
}

static inline uint16_t
sm64_saturn_pcm_counter_saturating_increment(uint16_t value)
{
    return value == 0xFFFFU ? 0xFFFFU : (uint16_t)(value + 1U);
}

/* ABI flag publication uses bits 2..15 as a bounded sequence, bit 1 as the
 * odd/in-progress marker, and bit 0 as capability. A
 * writer advances stable-even -> writing-odd -> next stable-even; wrap returns
 * to sequence zero only after 16384 complete publications. The writer must not
 * wrap that bounded sequence during one reader's three-attempt snapshot. */
static inline uint16_t
sm64_saturn_pcm_status_publication_begin(uint16_t stable_flags)
{
    return (uint16_t)(stable_flags + 2U);
}

static inline uint16_t
sm64_saturn_pcm_status_publication_finish(uint16_t writing_flags)
{
    return (uint16_t)(writing_flags + 2U);
}

/* The single FIFO reserves its final eight slots for required acknowledgments.
 * This is a producer policy contract only; Wave 2 does not link an MC68000
 * publisher. Nonessential SFX ACCEPTED records stop at occupancy 24. */
static inline bool sm64_saturn_audio_completion_publication_allowed(
    uint16_t occupancy, bool required)
{
    const uint16_t nonessential_limit =
        (uint16_t)(SM64_SATURN_PCM_COMPLETION_RING_COUNT -
                   SM64_SATURN_PCM_COMPLETION_CONTROL_RESERVE);
    return required ? occupancy < SM64_SATURN_PCM_COMPLETION_RING_COUNT
                    : occupancy < nonessential_limit;
}

/* Closed command-ack matrix for this ABI wave. FINISHED deliberately remains
 * illegal until a versioned event record carries its semantic identity. */
static inline bool sm64_saturn_audio_completion_status_opcode_is_legal(
    uint16_t source_ring, sm64_saturn_audio_opcode_t opcode, uint16_t status)
{
    const bool control = source_ring == SM64_SATURN_AUDIO_RING_CONTROL &&
                         sm64_saturn_audio_opcode_is_control(opcode);
    const bool sfx = source_ring == SM64_SATURN_AUDIO_RING_SFX &&
                     sm64_saturn_audio_opcode_is_sfx(opcode);

    if ((!control && !sfx) ||
        !sm64_saturn_audio_completion_status_is_valid(status)) {
        return false;
    }
    switch ((sm64_saturn_audio_completion_status_t)status) {
        case SM64_SATURN_AUDIO_COMPLETION_ACCEPTED:
            return opcode != SM64_SATURN_AUDIO_OPCODE_PACKAGE_PREPARE &&
                   opcode != SM64_SATURN_AUDIO_OPCODE_PACKAGE_COMMIT;
        case SM64_SATURN_AUDIO_COMPLETION_REJECTED:
        case SM64_SATURN_AUDIO_COMPLETION_STALE:
        case SM64_SATURN_AUDIO_COMPLETION_FAULT:
            return true;
        case SM64_SATURN_AUDIO_COMPLETION_DROPPED_SFX:
            return sfx && opcode == SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH;
        case SM64_SATURN_AUDIO_COMPLETION_FINISHED:
            return false;
        case SM64_SATURN_AUDIO_COMPLETION_COMMITTED:
            return control &&
                   opcode == SM64_SATURN_AUDIO_OPCODE_PACKAGE_COMMIT;
        case SM64_SATURN_AUDIO_COMPLETION_PREPARED:
            return control &&
                   opcode == SM64_SATURN_AUDIO_OPCODE_PACKAGE_PREPARE;
        default:
            return false;
    }
}

#endif
