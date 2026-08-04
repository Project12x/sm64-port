#include <assert.h>
#include <stdint.h>

static uint16_t s_writes[64];
static uint16_t s_write_count;

static void observe_write(uint16_t offset)
{
    assert(s_write_count < (uint16_t)(sizeof(s_writes) / sizeof(s_writes[0])));
    s_writes[s_write_count++] = offset;
}

#define SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER(offset) observe_write(offset)
#include "../../src/port/saturn/audio/saturn_pcm_transport.c"
#undef SM64_SATURN_PCM_PRODUCER_WRITE_OBSERVER

#define SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER(offset) observe_write(offset)
#include "../../src/port/saturn/audio68k/pcm_voice.c"
#undef SM64_SATURN_PCM_CONSUMER_WRITE_OBSERVER

int main(void)
{
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES] = {0};
    sm64_saturn_pcm_transport_t transport;
    sm64_saturn_pcm_voice_state_t state;
    const uint16_t play[7] = {0U, 15U, 0U, 0U, 0U, 0U, 0U};

    sm64_saturn_pcm_transport_init(&transport, ram);
    assert(sm64_saturn_pcm_enqueue(&transport,
                                   SM64_SATURN_PCM_OPCODE_PLAY, play));
    assert(s_write_count == 9U);
    assert(s_writes[s_write_count - 1U] == SM64_SATURN_PCM_PRODUCER_OFFSET);

    s_write_count = 0U;
    sm64_saturn_pcm_voice_state_init(&state);
    assert(sm64_saturn_pcm68k_consume(ram, &state) == 1U);
    assert(s_write_count > 1U);
    assert(s_writes[s_write_count - 1U] == SM64_SATURN_PCM_CONSUMER_OFFSET);
    return 0;
}
