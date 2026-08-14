#ifndef SM64_SATURN_SOURCE_AUDIO_LIVE_H
#define SM64_SATURN_SOURCE_AUDIO_LIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../audio/saturn_audio_policy.h"

/* The source policy owns semantic events.  This bridge owns only the bounded
 * mailbox publication for the currently sealed sound-RAM SFX package. */
bool sm64_saturn_source_audio_live_activate(volatile uint8_t *sound_ram,
                                            uint16_t bundle_generation);
void sm64_saturn_source_audio_live_deactivate(void);
bool sm64_saturn_source_audio_emit_event(
    const sm64_saturn_audio_event_t *event);
size_t sm64_saturn_source_audio_live_workspace_bytes(void);
bool sm64_saturn_source_audio_live_workspace_bind(void *workspace,
                                                  size_t workspace_bytes);
bool sm64_saturn_source_audio_live_boot(const uint8_t *driver,
                                        uint32_t driver_bytes,
                                        const uint8_t *metadata,
                                        uint32_t metadata_bytes,
                                        const uint8_t *pcm,
                                        uint32_t pcm_bytes,
                                        uint16_t bundle_generation);

#endif
