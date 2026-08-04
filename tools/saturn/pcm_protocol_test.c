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
    assert(SM64_SATURN_PCM_MAGIC_OFFSET == 0x4000U);
    assert(SM64_SATURN_PCM_VERSION_OFFSET == 0x4002U);
    assert(SM64_SATURN_PCM_STATUS_OFFSET == 0x4004U);
    assert(SM64_SATURN_PCM_HEARTBEAT_OFFSET == 0x4006U);
    assert(SM64_SATURN_PCM_PRODUCER_OFFSET == 0x4008U);
    assert(SM64_SATURN_PCM_CONSUMER_OFFSET == 0x400AU);
    assert(SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET == 0x400CU);
    assert(SM64_SATURN_PCM_VOICES_STARTED_OFFSET == 0x400EU);
    assert(SM64_SATURN_PCM_UNKNOWN_OPCODES_OFFSET == 0x4010U);
    assert(SM64_SATURN_PCM_LAST_OPCODE_OFFSET == 0x4012U);
    assert(SM64_SATURN_PCM_ACTIVE_SLOT_OFFSET == 0x4014U);
    assert(SM64_SATURN_PCM_INVALID_SAMPLES_OFFSET == 0x4016U);
    assert(SM64_SATURN_PCM_PROTOCOL_FAULTS_OFFSET == 0x4018U);
    assert(SM64_SATURN_PCM_PROTOCOL_MAGIC == 0x5036U);
    assert(SM64_SATURN_PCM_PROTOCOL_VERSION == 1U);
    assert(SM64_SATURN_PCM_STATUS_BOOTING == 1U);
    assert(SM64_SATURN_PCM_STATUS_READY == 2U);
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
