#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_audio_policy.h"
#include "saturn_pcm_protocol.h"
#include "source_audio_live.h"

static uint8_t s_sound_ram[SM64_SATURN_PCM_SOUND_RAM_BYTES];

static void put_bundle_header(uint16_t generation)
{
    const uint16_t base = SM64_SATURN_PCM_SFX_BUNDLE_OFFSET;

    sm64_saturn_pcm_put_be32(s_sound_ram, base,
                             SM64_SATURN_PCM_SFX_BUNDLE_MAGIC);
    sm64_saturn_pcm_put_be16(s_sound_ram, (uint16_t)(base + 4U),
                             SM64_SATURN_PCM_SFX_BUNDLE_VERSION);
    sm64_saturn_pcm_put_be16(s_sound_ram, (uint16_t)(base + 6U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(s_sound_ram, (uint16_t)(base + 8U), generation);
    sm64_saturn_pcm_put_be16(s_sound_ram, (uint16_t)(base + 10U), 1U);
    sm64_saturn_pcm_put_be16(s_sound_ram, (uint16_t)(base + 12U), 1U);
    sm64_saturn_pcm_put_be16(s_sound_ram, (uint16_t)(base + 14U),
                             SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES);
    sm64_saturn_pcm_put_be16(s_sound_ram, (uint16_t)(base + 16U), 40U);
    sm64_saturn_pcm_put_be16(s_sound_ram, (uint16_t)(base + 18U), 52U);
    sm64_saturn_pcm_put_be32(s_sound_ram, (uint16_t)(base + 20U), 32U);
}

static void publish_ready(uint16_t generation)
{
    memset(s_sound_ram, 0, sizeof(s_sound_ram));
    sm64_saturn_pcm_put_be16(s_sound_ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                             SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(s_sound_ram, SM64_SATURN_PCM_VERSION_OFFSET,
                             SM64_SATURN_PCM_PROTOCOL_VERSION);
    sm64_saturn_pcm_put_be16(s_sound_ram, SM64_SATURN_PCM_STATUS_OFFSET,
                             SM64_SATURN_PCM_STATUS_READY);
    put_bundle_header(generation);
}

static sm64_saturn_audio_event_t play_refresh(uint16_t generation)
{
    sm64_saturn_audio_event_t event;

    memset(&event, 0, sizeof(event));
    event.opcode = SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH;
    event.words[0] = 0x2400U;
    event.words[1] = 0x8080U;
    event.words[2] = 17U;
    event.words[3] = generation;
    event.words[4] = 0xFF40U;
    event.words[5] = 0x0100U;
    event.words[6] = 9U;
    return event;
}

static void test_live_bridge_enqueues_current_semantic_sfx(void)
{
    const sm64_saturn_audio_event_t event = play_refresh(1U);

    publish_ready(1U);
    assert(sm64_saturn_source_audio_live_activate(s_sound_ram, 1U));
    assert(sm64_saturn_source_audio_emit_event(&event));
    assert(sm64_saturn_pcm_get_be16(
               s_sound_ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET) == 1U);
    assert(sm64_saturn_pcm_get_be16(
               s_sound_ram, SM64_SATURN_PCM_SFX_RING_OFFSET) ==
           SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH);
    assert(sm64_saturn_pcm_get_be16(
               s_sound_ram, (uint16_t)(SM64_SATURN_PCM_SFX_RING_OFFSET +
                                        2U)) == event.words[0]);
    sm64_saturn_source_audio_live_deactivate();
}

static void test_live_bridge_rejects_stale_and_unready_events(void)
{
    sm64_saturn_audio_event_t event = play_refresh(2U);

    publish_ready(1U);
    assert(sm64_saturn_source_audio_live_activate(s_sound_ram, 1U));
    assert(!sm64_saturn_source_audio_emit_event(&event));
    assert(sm64_saturn_pcm_get_be16(
               s_sound_ram, SM64_SATURN_PCM_SFX_PRODUCER_OFFSET) == 0U);
    sm64_saturn_source_audio_live_deactivate();
    event.words[3] = 1U;
    assert(!sm64_saturn_source_audio_emit_event(&event));
}

static void test_live_bridge_enqueues_control_on_control_ring(void)
{
    sm64_saturn_audio_event_t event;

    publish_ready(1U);
    assert(sm64_saturn_source_audio_live_activate(s_sound_ram, 1U));
    memset(&event, 0, sizeof(event));
    event.opcode = SM64_SATURN_AUDIO_OPCODE_SET_MASTER;
    event.words[0] = 12U;
    assert(sm64_saturn_source_audio_emit_event(&event));
    assert(sm64_saturn_pcm_get_be16(
               s_sound_ram, SM64_SATURN_PCM_CONTROL_PRODUCER_OFFSET) == 1U);
    sm64_saturn_source_audio_live_deactivate();
}

int main(void)
{
    test_live_bridge_enqueues_current_semantic_sfx();
    test_live_bridge_rejects_stale_and_unready_events();
    test_live_bridge_enqueues_control_on_control_ring();
    return 0;
}
