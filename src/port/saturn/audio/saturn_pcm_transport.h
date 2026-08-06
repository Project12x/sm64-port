#ifndef SM64_SATURN_PCM_TRANSPORT_H
#define SM64_SATURN_PCM_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_pcm_protocol.h"

typedef struct sm64_saturn_audio_ticket {
    uint16_t source_ring;
    uint16_t cursor;
    uint16_t opcode;
} sm64_saturn_audio_ticket_t;

typedef struct sm64_saturn_audio_completion {
    sm64_saturn_audio_ticket_t ticket;
    uint16_t status;
    uint32_t generation;
    uint16_t detail;
    uint16_t service_tick;
} sm64_saturn_audio_completion_t;

typedef struct sm64_saturn_audio_status_snapshot {
    uint32_t active_generation;
    uint32_t prepared_generation;
    uint16_t completion_saturated;
    uint16_t completion_protocol_faults;
    uint16_t last_completion_status;
    uint16_t last_completion_detail;
    uint16_t service_tick;
    uint16_t active_voice_count;
    uint16_t abi_flags;
} sm64_saturn_audio_status_snapshot_t;

_Static_assert(sizeof(sm64_saturn_audio_ticket_t) == 6U,
               "audio ticket must remain pointer-free");
_Static_assert(sizeof(sm64_saturn_audio_completion_t) == 16U,
               "host completion must remain pointer-free and bounded");

typedef struct sm64_saturn_pcm_transport {
    volatile uint8_t *sound_ram;
    uint32_t control_enqueued;
    uint32_t sfx_enqueued;
    uint32_t control_saturated;
    uint32_t sfx_saturated;
    uint32_t protocol_faults;
    uint32_t completions_drained;
    uint32_t completion_protocol_faults;
    uint32_t ticket_busy;
    uint16_t control_high_water;
    uint16_t sfx_high_water;
    bool control_ticket_pending[SM64_SATURN_PCM_CONTROL_RING_COUNT * 2U];
    bool sfx_ticket_pending[SM64_SATURN_PCM_SFX_RING_COUNT * 2U];
} sm64_saturn_pcm_transport_t;

void sm64_saturn_pcm_transport_init(sm64_saturn_pcm_transport_t *transport,
                                    volatile uint8_t *sound_ram);

bool sm64_saturn_audio_control_enqueue(sm64_saturn_pcm_transport_t *transport,
                                       sm64_saturn_audio_opcode_t opcode,
                                       const uint16_t words[7]);
bool sm64_saturn_audio_sfx_enqueue(sm64_saturn_pcm_transport_t *transport,
                                   sm64_saturn_audio_opcode_t opcode,
                                   const uint16_t words[7]);
bool sm64_saturn_audio_control_enqueue_ticket(
    sm64_saturn_pcm_transport_t *transport,
    sm64_saturn_audio_opcode_t opcode, const uint16_t words[7],
    sm64_saturn_audio_ticket_t *ticket);
bool sm64_saturn_audio_sfx_enqueue_ticket(
    sm64_saturn_pcm_transport_t *transport,
    sm64_saturn_audio_opcode_t opcode, const uint16_t words[7],
    sm64_saturn_audio_ticket_t *ticket);
bool sm64_saturn_audio_play_refresh_enqueue_ticket(
    sm64_saturn_pcm_transport_t *transport, const uint16_t words[7],
    uint32_t package_generation, sm64_saturn_audio_ticket_t *ticket);
bool sm64_saturn_audio_completion_poll(
    sm64_saturn_pcm_transport_t *transport,
    sm64_saturn_audio_completion_t *completion);
bool sm64_saturn_audio_completion_matches_ticket(
    const sm64_saturn_audio_completion_t *completion,
    const sm64_saturn_audio_ticket_t *ticket);
bool sm64_saturn_audio_completion_is_acceptance(
    const sm64_saturn_audio_completion_t *completion);
bool sm64_saturn_audio_completion_is_required(
    uint16_t source_ring, sm64_saturn_audio_opcode_t opcode,
    uint16_t status);
bool sm64_saturn_audio_status_snapshot(
    sm64_saturn_pcm_transport_t *transport,
    sm64_saturn_audio_status_snapshot_t *snapshot);

#endif
