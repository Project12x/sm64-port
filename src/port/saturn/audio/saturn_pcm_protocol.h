/* Saturn SH-2 <-> MC68000 PCM transport wire protocol.
 *
 * The fixed sound-RAM mailbox is an original, byte-addressed ABI.  It uses
 * the separate-binary/fixed-location pattern studied in
 * ponut64/SCSP_poneSound@31782e4c61337327f23eb9aa45ecd37fe0944ea0
 * (MIT; documentation.md, PROJ/main.c, jo_demo/pcmsys.{c,h}).  No upstream
 * struct or driver code is copied.  See docs/saturn/audio/PCM68K_PROVENANCE.md.
 */
#ifndef SM64_SATURN_PCM_PROTOCOL_H
#define SM64_SATURN_PCM_PROTOCOL_H

#include <stdint.h>

enum {
    SM64_SATURN_PCM_SOUND_RAM_BYTES = 0x80000U,
    SM64_SATURN_PCM_DRIVER_END = 0x04000U,
    SM64_SATURN_PCM_MAILBOX_OFFSET = 0x04000U,
    SM64_SATURN_PCM_MAILBOX_BYTES = 0x01000U,
    SM64_SATURN_PCM_RING_OFFSET = 0x04040U,
    SM64_SATURN_PCM_RING_COUNT = 32U,
    SM64_SATURN_PCM_COMMAND_BYTES = 16U,
    SM64_SATURN_PCM_RING_BYTES =
        SM64_SATURN_PCM_RING_COUNT * SM64_SATURN_PCM_COMMAND_BYTES,
    SM64_SATURN_PCM_RESERVE_OFFSET = 0x05000U,
    SM64_SATURN_PCM_BANK_OFFSET = 0x08000U,
};

typedef enum sm64_saturn_pcm_opcode {
    SM64_SATURN_PCM_OPCODE_NOP = 0,
    SM64_SATURN_PCM_OPCODE_PLAY = 1,
    SM64_SATURN_PCM_OPCODE_STOP_ALL = 2,
    SM64_SATURN_PCM_OPCODE_SET_MASTER = 3,
} sm64_saturn_pcm_opcode_t;

_Static_assert(SM64_SATURN_PCM_DRIVER_END == SM64_SATURN_PCM_MAILBOX_OFFSET,
               "driver must end at mailbox");
_Static_assert(SM64_SATURN_PCM_RING_OFFSET >= SM64_SATURN_PCM_MAILBOX_OFFSET,
               "ring must begin in mailbox");
_Static_assert(SM64_SATURN_PCM_RING_OFFSET + SM64_SATURN_PCM_RING_BYTES <=
                   SM64_SATURN_PCM_MAILBOX_OFFSET +
                       SM64_SATURN_PCM_MAILBOX_BYTES,
               "ring must fit in mailbox");
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

static inline uint16_t
sm64_saturn_pcm_ring_next(uint16_t index)
{
    return (uint16_t)((index + 1U) % SM64_SATURN_PCM_RING_COUNT);
}

#endif
