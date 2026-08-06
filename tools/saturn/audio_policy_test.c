#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_audio_policy.h"

typedef struct event_log {
    sm64_saturn_audio_event_t events[64];
    uint16_t count;
} event_log_t;

static bool capture_event(void *context, const sm64_saturn_audio_event_t *event)
{
    event_log_t *log = (event_log_t *)context;
    assert(log->count < 64U);
    log->events[log->count++] = *event;
    return true;
}

static void init(sm64_saturn_audio_policy_t *policy, event_log_t *log)
{
    memset(log, 0, sizeof(*log));
    sm64_saturn_audio_policy_init(policy, capture_event, log);
}

static void test_six_entry_priority_queue_and_duplicates(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0121U, 7U));
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0522U, 9U));
    assert(policy.background_queue_size == 2U);
    assert(policy.background_queue[0].seq_id == 0x22U);
    assert(policy.background_queue[0].priority == 5U);
    assert(policy.background_queue[1].seq_id == 0x21U);
    assert(log.count == 2U);

    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0522U, 11U));
    assert(policy.background_queue_size == 2U);
    assert(log.count == 3U);
    assert(log.events[2].opcode == SM64_SATURN_AUDIO_OPCODE_SEQ_START);
    assert(log.events[2].words[2] == 11U);

    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0121U, 13U));
    assert(policy.background_queue_size == 2U);
    assert(log.count == 3U);

    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0623U, 0U));
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0724U, 0U));
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0825U, 0U));
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0926U, 0U));
    assert(policy.background_queue_size == SM64_SATURN_AUDIO_BACKGROUND_QUEUE_CAPACITY);
    assert(!sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0A27U, 0U));
    assert(policy.background_queue_size == SM64_SATURN_AUDIO_BACKGROUND_QUEUE_CAPACITY);
}

static void test_stop_fade_and_queue_resume(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0131U, 0U));
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0632U, 0U));
    assert(sm64_saturn_audio_policy_current_background(&policy) == 0x0632U);
    assert(sm64_saturn_audio_policy_fadeout_background(&policy, 0x32U, 44U));
    assert(log.events[log.count - 1U].opcode == SM64_SATURN_AUDIO_OPCODE_SEQ_FADE);
    assert(log.events[log.count - 1U].words[2] == 44U);

    assert(sm64_saturn_audio_policy_stop_background(&policy, 0x32U));
    assert(sm64_saturn_audio_policy_current_background(&policy) == 0x0131U);
    assert(log.events[log.count - 1U].opcode == SM64_SATURN_AUDIO_OPCODE_SEQ_START);
    assert(log.events[log.count - 1U].words[1] == 0x31U);
    assert(sm64_saturn_audio_policy_stop_background(&policy, 0x31U));
    assert(sm64_saturn_audio_policy_current_background(&policy) == UINT16_MAX);
    assert(log.events[log.count - 1U].opcode == SM64_SATURN_AUDIO_OPCODE_SEQ_FADE);
    assert(log.events[log.count - 1U].words[2] == 20U);
}

static void test_secondary_jingle_and_lower_constraints_resume(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0433U, 0U));
    assert(sm64_saturn_audio_policy_play_secondary(&policy, 0x13U, 60U, 90U, 20U));
    assert(policy.background_target_volume == 60U);
    assert(policy.environment_seq_id == 0x13U);
    assert(sm64_saturn_audio_policy_lower(&policy, 0U, 8U, 12U));
    assert(policy.lower_background_music);
    assert(sm64_saturn_audio_policy_effective_background_volume(&policy) == 40U);
    assert(sm64_saturn_audio_policy_unlower(&policy, 0U, 9U));
    assert(sm64_saturn_audio_policy_effective_background_volume(&policy) == 60U);

    assert(sm64_saturn_audio_policy_play_jingle(&policy, 0x1BU, 20U));
    assert(sm64_saturn_audio_policy_effective_background_volume(&policy) == 20U);
    assert(sm64_saturn_audio_policy_environment_complete(&policy));
    assert(policy.environment_seq_id == 0x13U);
    assert(sm64_saturn_audio_policy_effective_background_volume(&policy) == 60U);

    assert(sm64_saturn_audio_policy_stop_secondary(&policy, 30U));
    assert(policy.background_target_volume == SM64_SATURN_AUDIO_VOLUME_UNSET);
    assert(policy.environment_seq_id == SM64_SATURN_AUDIO_SEQUENCE_NONE);
}

static void test_bank_masks_continuous_freshness_stop_and_getter(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t refresh = {
        0x10120001U, 7U, 2U, 255U, 64U, 4096U, 1U
    };
    uint8_t playing;
    uint8_t in_bank;
    uint8_t sound_id;

    init(&policy, &log);
    sm64_saturn_audio_policy_disable_banks(&policy, (uint16_t)(1U << 1));
    assert(!sm64_saturn_audio_policy_play_refresh(&policy, &refresh));
    sm64_saturn_audio_policy_enable_banks(&policy, (uint16_t)(1U << 1));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh));
    assert(policy.active_sfx_count == 1U);
    assert(log.events[log.count - 1U].opcode == SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH);

    sm64_saturn_audio_policy_get_playing(&policy, 1U, &playing, &in_bank, &sound_id);
    assert(playing == 1U);
    assert(in_bank == 1U);
    assert(sound_id == 0x12U);

    sm64_saturn_audio_policy_tick(&policy);
    sm64_saturn_audio_policy_tick(&policy);
    assert(policy.active_sfx_count == 1U);
    sm64_saturn_audio_policy_tick(&policy);
    assert(policy.active_sfx_count == 0U);
    assert(log.events[log.count - 1U].opcode == SM64_SATURN_AUDIO_OPCODE_STOP_HANDLE);

    refresh.sound_bits = 0x00138081U;
    refresh.source_token = 9U;
    refresh.freshness_generation = policy.freshness_generation;
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh));
    sm64_saturn_audio_policy_tick(&policy);
    sm64_saturn_audio_policy_tick(&policy);
    sm64_saturn_audio_policy_tick(&policy);
    assert(policy.active_sfx_count == 1U);
    for (uint8_t i = 0U; i < 9U; ++i) {
        sm64_saturn_audio_policy_tick(&policy);
    }
    assert(policy.active_sfx_count == 1U);
    assert(sm64_saturn_audio_policy_stop_source(&policy, 9U, 2U));
    assert(policy.active_sfx_count == 0U);
    assert(log.events[log.count - 1U].opcode == SM64_SATURN_AUDIO_OPCODE_STOP_SOURCE);
}

static void test_bank_priority_publishes_one_source_and_promotes_next(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t low = {
        0x30202001U, 3U, 1U, 180U, 64U, 4096U, 0U
    };
    sm64_saturn_audio_play_refresh_t high = {
        0x3021A001U, 4U, 1U, 120U, 64U, 4096U, 0U
    };
    uint8_t playing;
    uint8_t in_bank;
    uint8_t sound_id;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &low));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &high));
    sm64_saturn_audio_policy_get_playing(&policy, 3U, &playing, &in_bank,
                                         &sound_id);
    assert(playing == 1U);
    assert(in_bank == 2U);
    assert(sound_id == 0x21U);
    assert(log.count == 3U);
    assert(log.events[1].opcode == SM64_SATURN_AUDIO_OPCODE_STOP_HANDLE);
    assert(log.events[2].opcode == SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH);

    assert(sm64_saturn_audio_policy_play_refresh(&policy, &low));
    assert(log.count == 3U);
    assert(sm64_saturn_audio_policy_stop_handle(&policy, high.sound_bits,
                                                high.source_token,
                                                high.package_generation));
    sm64_saturn_audio_policy_get_playing(&policy, 3U, &playing, &in_bank,
                                         &sound_id);
    assert(playing == 1U);
    assert(in_bank == 1U);
    assert(sound_id == 0x20U);
    assert(log.events[log.count - 1U].opcode ==
           SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH);
}

static void test_same_source_priority_and_discrete_restart(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t refresh = {
        0x30128001U, 5U, 1U, 255U, 64U, 4096U, 0U
    };

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh));
    refresh.sound_bits = 0x30114001U;
    assert(!sm64_saturn_audio_policy_play_refresh(&policy, &refresh));
    assert(policy.sfx[0].sound_bits == 0x30128001U);
    refresh.sound_bits = 0x3013A081U;
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh));
    assert(policy.sfx[0].sound_bits == 0x3013A081U);
    assert(policy.sfx[0].restart_generation == 1U);
}

int main(void)
{
    test_six_entry_priority_queue_and_duplicates();
    test_stop_fade_and_queue_resume();
    test_secondary_jingle_and_lower_constraints_resume();
    test_bank_masks_continuous_freshness_stop_and_getter();
    test_same_source_priority_and_discrete_restart();
    test_bank_priority_publishes_one_source_and_promotes_next();
    return 0;
}
