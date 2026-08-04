/* Host contract for the Saturn SH-2 <-> 68K PCM mailbox ABI.
 *
 * This must stay byte-addressed: the peers are separately compiled and may
 * not exchange C structures or pointers. */
#include <assert.h>
#include <stdint.h>

#include "saturn_pcm_protocol.h"

int main(void)
{
    uint8_t sound_ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};

    assert(SM64_SATURN_PCM_MAILBOX_OFFSET == 0x4000U);
    assert(SM64_SATURN_PCM_RING_OFFSET == 0x4040U);
    assert(SM64_SATURN_PCM_RING_COUNT == 32U);
    assert(SM64_SATURN_PCM_COMMAND_BYTES == 16U);
    assert(SM64_SATURN_PCM_RING_BYTES == 512U);
    assert(SM64_SATURN_PCM_BANK_OFFSET == 0x8000U);
    assert(SM64_SATURN_PCM_OPCODE_PLAY == 1U);
    assert(SM64_SATURN_PCM_OPCODE_STOP_ALL == 2U);
    assert(SM64_SATURN_PCM_OPCODE_SET_MASTER == 3U);
    assert(SM64_SATURN_PCM_RING_OFFSET + SM64_SATURN_PCM_RING_BYTES <=
           SM64_SATURN_PCM_BANK_OFFSET);
    assert(SM64_SATURN_PCM_BANK_OFFSET < SM64_SATURN_PCM_SOUND_RAM_BYTES);

    sm64_saturn_pcm_put_be16(sound_ram, 0x4040U, 0x1234U);
    assert(sound_ram[0x4040U] == 0x12U);
    assert(sound_ram[0x4041U] == 0x34U);
    assert(sm64_saturn_pcm_get_be16(sound_ram, 0x4040U) == 0x1234U);

    assert(sm64_saturn_pcm_ring_next(31U) == 0U);
    assert(sm64_saturn_pcm_ring_next(0U) == 1U);
    return 0;
}
