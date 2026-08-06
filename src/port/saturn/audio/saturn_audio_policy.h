/* Source-authoritative SM64 audio policy for the Saturn SH-2 side.
 *
 * This is a close adaptation of the policy state machines in the same
 * repository's inherited src/audio/external.c.  It does
 * not interpret sequence bytecode or allocate SCSP voices; it publishes only
 * bounded, pointer-free semantic events for the MC68000 service.
 */
#ifndef SM64_SATURN_AUDIO_POLICY_H
#define SM64_SATURN_AUDIO_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_pcm_protocol.h"

enum {
    SM64_SATURN_AUDIO_BACKGROUND_QUEUE_CAPACITY = 6U,
    SM64_SATURN_AUDIO_BANK_COUNT = 10U,
    /* external.c has 40 entries, but indices 0 and 39 are list sentinels. */
    SM64_SATURN_AUDIO_SFX_PER_BANK = 38U,
    SM64_SATURN_AUDIO_SFX_CAPACITY =
        SM64_SATURN_AUDIO_BANK_COUNT * SM64_SATURN_AUDIO_SFX_PER_BANK,
    SM64_SATURN_AUDIO_REQUEST_CAPACITY = 256U,
    SM64_SATURN_AUDIO_SEQUENCE_NONE = 0xFFU,
    SM64_SATURN_AUDIO_VOLUME_UNSET = 0xFFU,
    SM64_SATURN_AUDIO_DISCRETE_FRESHNESS = 10U,
    SM64_SATURN_AUDIO_CONTINUOUS_GRACE = 2U,
};

typedef struct sm64_saturn_audio_event {
    sm64_saturn_audio_opcode_t opcode;
    uint16_t words[7];
} sm64_saturn_audio_event_t;

typedef bool (*sm64_saturn_audio_emit_fn)(
    void *context, const sm64_saturn_audio_event_t *event);

typedef struct sm64_saturn_audio_play_refresh {
    uint32_t sound_bits;
    uint16_t source_token;
    uint16_t package_generation;
    uint8_t volume;
    uint8_t pan;
    uint16_t pitch;
    uint16_t freshness_generation;
} sm64_saturn_audio_play_refresh_t;

typedef struct sm64_saturn_audio_queue_item {
    uint8_t seq_id;
    uint8_t priority;
} sm64_saturn_audio_queue_item_t;

typedef struct sm64_saturn_audio_sfx_state {
    uint32_t sound_bits;
    uint16_t source_token;
    uint16_t package_generation;
    uint16_t last_refresh_generation;
    uint16_t restart_generation;
    uint32_t priority_score;
    uint8_t volume;
    uint8_t pan;
    uint16_t pitch;
    bool active;
    bool published;
} sm64_saturn_audio_sfx_state_t;

typedef struct sm64_saturn_audio_pending_request {
    sm64_saturn_audio_play_refresh_t refresh;
    uint32_t priority_score;
} sm64_saturn_audio_pending_request_t;

typedef struct sm64_saturn_audio_policy {
    sm64_saturn_audio_emit_fn emit;
    void *emit_context;
    sm64_saturn_audio_queue_item_t
        background_queue[SM64_SATURN_AUDIO_BACKGROUND_QUEUE_CAPACITY];
    sm64_saturn_audio_sfx_state_t sfx[SM64_SATURN_AUDIO_SFX_CAPACITY];
    sm64_saturn_audio_pending_request_t
        pending_requests[SM64_SATURN_AUDIO_REQUEST_CAPACITY];
    uint16_t disabled_bank_mask;
    uint16_t lowering_bank_mask;
    uint16_t freshness_generation;
    uint16_t environment_generation;
    uint16_t active_sfx_count;
    uint16_t pending_request_count;
    uint8_t background_queue_size;
    uint8_t background_target_volume;
    uint8_t background_max_volume;
    uint8_t environment_seq_id;
    uint8_t secondary_seq_id;
    uint8_t secondary_volume;
    uint8_t environment_completion_guard;
    uint8_t sound_mode;
    bool lower_background_music;
    bool muted;
    bool global_fade_started;
} sm64_saturn_audio_policy_t;

void sm64_saturn_audio_policy_init(sm64_saturn_audio_policy_t *policy,
                                   sm64_saturn_audio_emit_fn emit,
                                   void *emit_context);
bool sm64_saturn_audio_policy_play_music(sm64_saturn_audio_policy_t *policy,
                                         uint8_t player, uint16_t seq_args,
                                         uint16_t fade_timer);
bool sm64_saturn_audio_policy_stop_background(
    sm64_saturn_audio_policy_t *policy, uint16_t seq_id);
bool sm64_saturn_audio_policy_fadeout_background(
    sm64_saturn_audio_policy_t *policy, uint16_t seq_id, uint16_t fade_timer);
void sm64_saturn_audio_policy_drop_queued(sm64_saturn_audio_policy_t *policy);
uint16_t sm64_saturn_audio_policy_current_background(
    const sm64_saturn_audio_policy_t *policy);
bool sm64_saturn_audio_policy_play_secondary(
    sm64_saturn_audio_policy_t *policy, uint8_t seq_id,
    uint8_t background_volume, uint8_t volume, uint16_t fade_timer);
bool sm64_saturn_audio_policy_stop_secondary(
    sm64_saturn_audio_policy_t *policy, uint16_t fade_timer);
bool sm64_saturn_audio_policy_play_jingle(sm64_saturn_audio_policy_t *policy,
                                          uint8_t seq_id,
                                          uint8_t max_background_volume);
bool sm64_saturn_audio_policy_environment_complete(
    sm64_saturn_audio_policy_t *policy);
bool sm64_saturn_audio_policy_environment_complete_matching(
    sm64_saturn_audio_policy_t *policy, uint8_t seq_id,
    uint16_t environment_generation);
bool sm64_saturn_audio_policy_lower(sm64_saturn_audio_policy_t *policy,
                                    uint8_t player, uint16_t fade_timer,
                                    uint8_t percentage);
bool sm64_saturn_audio_policy_unlower(sm64_saturn_audio_policy_t *policy,
                                      uint8_t player, uint16_t fade_timer);
uint8_t sm64_saturn_audio_policy_effective_background_volume(
    const sm64_saturn_audio_policy_t *policy);
void sm64_saturn_audio_policy_disable_banks(sm64_saturn_audio_policy_t *policy,
                                            uint16_t bank_mask);
void sm64_saturn_audio_policy_enable_banks(sm64_saturn_audio_policy_t *policy,
                                           uint16_t bank_mask);
bool sm64_saturn_audio_policy_play_refresh(
    sm64_saturn_audio_policy_t *policy,
    const sm64_saturn_audio_play_refresh_t *refresh,
    uint32_t priority_score);
bool sm64_saturn_audio_policy_sound_id_valid(uint32_t sound_bits);
bool sm64_saturn_audio_policy_stop_handle(sm64_saturn_audio_policy_t *policy,
                                          uint32_t sound_bits,
                                          uint16_t source_token,
                                          uint16_t package_generation);
bool sm64_saturn_audio_policy_complete_handle(
    sm64_saturn_audio_policy_t *policy, uint32_t sound_bits,
    uint16_t source_token, uint16_t package_generation);
bool sm64_saturn_audio_policy_update_spatial(
    sm64_saturn_audio_policy_t *policy, uint16_t source_token,
    uint16_t package_generation, uint8_t volume, uint8_t pan,
    uint16_t pitch, uint32_t priority_score);
bool sm64_saturn_audio_policy_token_is_active(
    const sm64_saturn_audio_policy_t *policy, uint16_t source_token,
    uint16_t package_generation);
bool sm64_saturn_audio_policy_stop_source(sm64_saturn_audio_policy_t *policy,
                                          uint16_t source_token,
                                          uint16_t package_generation);
bool sm64_saturn_audio_policy_stop_bank(sm64_saturn_audio_policy_t *policy,
                                        uint8_t bank);
void sm64_saturn_audio_policy_tick(sm64_saturn_audio_policy_t *policy);
void sm64_saturn_audio_policy_get_playing(
    const sm64_saturn_audio_policy_t *policy, uint8_t bank,
    uint8_t *num_playing, uint8_t *num_in_bank, uint8_t *sound_id);
bool sm64_saturn_audio_policy_fade_player(sm64_saturn_audio_policy_t *policy,
                                          uint8_t player, uint8_t target,
                                          uint16_t fade_timer);
bool sm64_saturn_audio_policy_fade_channels(
    sm64_saturn_audio_policy_t *policy, uint8_t player, uint8_t target,
    uint16_t fade_timer);
bool sm64_saturn_audio_policy_fade_sfx_banks(
    sm64_saturn_audio_policy_t *policy, uint16_t bank_mask, uint8_t target,
    uint16_t fade_timer);
bool sm64_saturn_audio_policy_mute(sm64_saturn_audio_policy_t *policy,
                                   bool muted);
bool sm64_saturn_audio_policy_reset(sm64_saturn_audio_policy_t *policy,
                                    uint8_t preset_id);
bool sm64_saturn_audio_policy_set_sound_mode(sm64_saturn_audio_policy_t *policy,
                                             uint8_t mode);

#endif
