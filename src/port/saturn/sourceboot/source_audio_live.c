#include "source_audio_live.h"

#include <stddef.h>
#include <string.h>

#include "../audio/saturn_pcm_transport.h"
#include "../audio/saturn_sound_cpu.h"

#if defined(TARGET_SATURN) && defined(SATURN_SOURCEBOOT)
#include "../platform/saturn_cart_code.h"
#define SOURCE_AUDIO_EXTERNAL_WORKSPACE 1
#define SOURCE_AUDIO_STATIC_ABI \
    __attribute__((section(".lwram_bss"), used))
#else
#define SOURCE_AUDIO_STATIC_ABI
#endif

typedef struct source_audio_live_workspace {
    sm64_saturn_pcm_transport_t transport;
    uint16_t bundle_generation;
    bool active;
} source_audio_live_workspace_t;

#if defined(SOURCE_AUDIO_EXTERNAL_WORKSPACE)
static source_audio_live_workspace_t *s_state SOURCE_AUDIO_STATIC_ABI;
#else
static source_audio_live_workspace_t s_host_state;
static source_audio_live_workspace_t *s_state = &s_host_state;
#endif

#define s_transport (s_state->transport)
#define s_bundle_generation (s_state->bundle_generation)
#define s_active (s_state->active)

size_t sm64_saturn_source_audio_live_workspace_bytes(void)
{
    return sizeof(source_audio_live_workspace_t);
}

bool sm64_saturn_source_audio_live_workspace_bind(void *workspace,
                                                  size_t workspace_bytes)
{
    if (workspace == NULL || workspace_bytes < sizeof(*s_state) ||
        ((uintptr_t)workspace % _Alignof(source_audio_live_workspace_t)) !=
            0U) {
        return false;
    }
    memset(workspace, 0, sizeof(source_audio_live_workspace_t));
    s_state = workspace;
    return true;
}

static bool source_audio_live_protocol_ready(const volatile uint8_t *sound_ram)
{
    return sound_ram != NULL &&
           sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_MAGIC_OFFSET) ==
               SM64_SATURN_PCM_PROTOCOL_MAGIC &&
           sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_VERSION_OFFSET) ==
               SM64_SATURN_PCM_PROTOCOL_VERSION &&
           sm64_saturn_pcm_get_be16(sound_ram,
                                    SM64_SATURN_PCM_STATUS_OFFSET) ==
               SM64_SATURN_PCM_STATUS_READY;
}

static bool source_audio_live_bundle_current(
    const volatile uint8_t *sound_ram, uint16_t generation)
{
    const uint16_t base = SM64_SATURN_PCM_SFX_BUNDLE_OFFSET;
    const uint16_t header_bytes = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(base + 6U));
    const uint16_t metadata_bytes = sm64_saturn_pcm_get_be16(
        sound_ram, (uint16_t)(base + 18U));
    const uint32_t pcm_bytes = sm64_saturn_pcm_get_be32(
        sound_ram, (uint16_t)(base + 20U));

    return generation != 0U &&
           sm64_saturn_pcm_get_be32(sound_ram, base) ==
               SM64_SATURN_PCM_SFX_BUNDLE_MAGIC &&
           sm64_saturn_pcm_get_be16(sound_ram,
                                    (uint16_t)(base + 4U)) ==
               SM64_SATURN_PCM_SFX_BUNDLE_VERSION &&
           header_bytes == SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES &&
           sm64_saturn_pcm_get_be16(sound_ram,
                                    (uint16_t)(base + 8U)) == generation &&
           metadata_bytes >= header_bytes &&
           metadata_bytes <= SM64_SATURN_PCM_SFX_BUNDLE_BYTES &&
           pcm_bytes != 0U &&
           pcm_bytes <= SM64_SATURN_PCM_SOUND_RAM_BYTES -
                            SM64_SATURN_PCM_BANK_OFFSET;
}

bool sm64_saturn_source_audio_live_activate(volatile uint8_t *sound_ram,
                                            uint16_t bundle_generation)
{
    if (s_state == NULL) {
        return false;
    }
    sm64_saturn_source_audio_live_deactivate();
    if (!source_audio_live_protocol_ready(sound_ram) ||
        !source_audio_live_bundle_current(sound_ram, bundle_generation)) {
        return false;
    }
    sm64_saturn_pcm_transport_init(&s_transport, sound_ram);
    s_bundle_generation = bundle_generation;
    s_active = true;
    return true;
}

void sm64_saturn_source_audio_live_deactivate(void)
{
    if (s_state == NULL) {
        return;
    }
    sm64_saturn_pcm_transport_init(&s_transport, NULL);
    s_bundle_generation = 0U;
    s_active = false;
}

bool sm64_saturn_source_audio_emit_event(
    const sm64_saturn_audio_event_t *event)
{
    if (s_state == NULL || !s_active || event == NULL) {
        return false;
    }
    if (sm64_saturn_audio_opcode_is_sfx(event->opcode)) {
        if ((event->opcode == SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH ||
             event->opcode == SM64_SATURN_AUDIO_OPCODE_STOP_HANDLE) &&
            event->words[3] != s_bundle_generation) {
            return false;
        }
        return sm64_saturn_audio_sfx_enqueue(&s_transport, event->opcode,
                                              event->words);
    }
    if (sm64_saturn_audio_opcode_is_control(event->opcode)) {
        return sm64_saturn_audio_control_enqueue(&s_transport, event->opcode,
                                                  event->words);
    }
    return false;
}

#if defined(__sh__)
#include <string.h>

#include <yaul.h>

enum {
    SOURCE_AUDIO_SOUND_RAM_ADDRESS = 0x25A00000UL,
    SOURCE_AUDIO_READY_VBLANKS = 120U,
    SOURCE_AUDIO_MASTER_VOLUME = 12U,
};

static bool source_audio_live_assets_valid(const uint8_t *driver,
                                           uint32_t driver_bytes,
                                           const uint8_t *metadata,
                                           uint32_t metadata_bytes,
                                           const uint8_t *pcm,
                                           uint32_t pcm_bytes,
                                           uint16_t generation)
{
    return driver != NULL && metadata != NULL && pcm != NULL &&
           driver_bytes != 0U &&
           driver_bytes <= SM64_SATURN_PCM_DRIVER_END &&
           metadata_bytes >= SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES &&
           metadata_bytes <= SM64_SATURN_PCM_SFX_BUNDLE_BYTES &&
           pcm_bytes != 0U &&
           pcm_bytes <= SM64_SATURN_PCM_SOUND_RAM_BYTES -
                            SM64_SATURN_PCM_BANK_OFFSET &&
           sm64_saturn_pcm_get_be32(metadata, 0U) ==
               SM64_SATURN_PCM_SFX_BUNDLE_MAGIC &&
           sm64_saturn_pcm_get_be16(metadata, 4U) ==
               SM64_SATURN_PCM_SFX_BUNDLE_VERSION &&
           sm64_saturn_pcm_get_be16(metadata, 6U) ==
               SM64_SATURN_PCM_SFX_BUNDLE_HEADER_BYTES &&
           sm64_saturn_pcm_get_be16(metadata, 8U) == generation &&
           sm64_saturn_pcm_get_be16(metadata, 18U) == metadata_bytes &&
           sm64_saturn_pcm_get_be32(metadata, 20U) == pcm_bytes;
}

static void source_audio_live_wait_vblank(void)
{
    vdp2_sync();
    vdp2_sync_wait();
}

SM64_SATURN_CART_COLD
bool sm64_saturn_source_audio_live_boot(const uint8_t *driver,
                                        uint32_t driver_bytes,
                                        const uint8_t *metadata,
                                        uint32_t metadata_bytes,
                                        const uint8_t *pcm,
                                        uint32_t pcm_bytes,
                                        uint16_t bundle_generation)
{
    volatile uint8_t *const sound_ram =
        (volatile uint8_t *)(uintptr_t)SOURCE_AUDIO_SOUND_RAM_ADDRESS;
    sm64_saturn_sound_cpu_yaul_result_t command_result = {0};
    uint16_t heartbeat;
    uint16_t attempt;
    const uint16_t master_words[7] = {SOURCE_AUDIO_MASTER_VOLUME, 0U, 0U,
                                      0U, 0U, 0U, 0U};

    sm64_saturn_source_audio_live_deactivate();
    if (!source_audio_live_assets_valid(driver, driver_bytes, metadata,
                                        metadata_bytes, pcm, pcm_bytes,
                                        bundle_generation) ||
        sm64_saturn_sound_cpu_yaul_command(
            &command_result, SM64_SATURN_SOUND_CPU_COMMAND_OFF) !=
            SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED) {
        return false;
    }
    /* Keep the SCSP CPU stopped while replacing its full RAM image.  The
     * standalone PoneSound sample briefly starts it before clearing, but in
     * sourceboot that lets arbitrary SCSP RAM execute alongside live VDP/SCU
     * state and can corrupt presentation. */
    if (!sm64_saturn_sound_cpu_yaul_set_512k(NULL)) {
        return false;
    }
    memset((void *)sound_ram, 0, SM64_SATURN_PCM_SOUND_RAM_BYTES);
    memcpy((void *)sound_ram, driver, driver_bytes);
    memcpy((void *)(sound_ram + SM64_SATURN_PCM_SFX_BUNDLE_OFFSET), metadata,
           metadata_bytes);
    memcpy((void *)(sound_ram + SM64_SATURN_PCM_BANK_OFFSET), pcm, pcm_bytes);
    heartbeat = sm64_saturn_pcm_get_be16(sound_ram,
                                         SM64_SATURN_PCM_HEARTBEAT_OFFSET);
    if (sm64_saturn_sound_cpu_yaul_command(
            &command_result, SM64_SATURN_SOUND_CPU_COMMAND_ON) !=
        SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED) {
        return false;
    }
    for (attempt = 0U; attempt < SOURCE_AUDIO_READY_VBLANKS; ++attempt) {
        source_audio_live_wait_vblank();
        if (source_audio_live_protocol_ready(sound_ram) &&
            sm64_saturn_pcm_get_be16(sound_ram,
                                     SM64_SATURN_PCM_HEARTBEAT_OFFSET) !=
                heartbeat &&
            sm64_saturn_source_audio_live_activate(sound_ram,
                                                    bundle_generation)) {
            return sm64_saturn_audio_control_enqueue(
                &s_transport, SM64_SATURN_AUDIO_OPCODE_SET_MASTER,
                master_words);
        }
    }
    (void)sm64_saturn_sound_cpu_yaul_command(
        &command_result, SM64_SATURN_SOUND_CPU_COMMAND_OFF);
    sm64_saturn_source_audio_live_deactivate();
    return false;
}
#else
bool sm64_saturn_source_audio_live_boot(const uint8_t *driver,
                                        uint32_t driver_bytes,
                                        const uint8_t *metadata,
                                        uint32_t metadata_bytes,
                                        const uint8_t *pcm,
                                        uint32_t pcm_bytes,
                                        uint16_t bundle_generation)
{
    (void)driver;
    (void)driver_bytes;
    (void)metadata;
    (void)metadata_bytes;
    (void)pcm;
    (void)pcm_bytes;
    (void)bundle_generation;
    return false;
}
#endif
