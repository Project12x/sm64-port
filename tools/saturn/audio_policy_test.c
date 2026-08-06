#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_audio_policy.h"

/* Pinned source-trace oracle: these transitions were transcribed from
 * src/audio/external.c at Task 9 base db7c9569, specifically request batching
 * and selection (842-1168), lowering (967-974, 1389-1393, 2076-2118), and
 * music/ENV policy (2383-2704).  The production policy is never used to
 * calculate the expected state/event sequences below. */

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
    assert(!sm64_saturn_audio_policy_environment_complete(&policy));
    sm64_saturn_audio_policy_tick(&policy);
    assert(!sm64_saturn_audio_policy_environment_complete(&policy));
    sm64_saturn_audio_policy_tick(&policy);
    assert(sm64_saturn_audio_policy_environment_complete(&policy));
    assert(policy.environment_seq_id == 0x13U);
    assert(sm64_saturn_audio_policy_effective_background_volume(&policy) == 60U);

    assert(sm64_saturn_audio_policy_stop_secondary(&policy, 30U));
    assert(policy.background_target_volume == SM64_SATURN_AUDIO_VOLUME_UNSET);
    assert(policy.environment_seq_id == SM64_SATURN_AUDIO_SEQUENCE_NONE);
    assert(sm64_saturn_audio_policy_effective_background_volume(&policy) ==
           SM64_SATURN_AUDIO_VOLUME_UNSET);
    assert(log.events[log.count - 2U].words[1] ==
           SM64_SATURN_AUDIO_VOLUME_UNSET);
    assert(sm64_saturn_audio_policy_lower(&policy, 0U, 1U, 12U));
    assert(policy.lower_background_music);
    assert(sm64_saturn_audio_policy_unlower(&policy, 1U, 1U));
    assert(!policy.lower_background_music);
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
    assert(!sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 100U));
    sm64_saturn_audio_policy_enable_banks(&policy, (uint16_t)(1U << 1));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 100U));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 100U));
    assert(policy.active_sfx_count == 1U);
    sm64_saturn_audio_policy_tick(&policy);
    assert(log.events[log.count - 1U].opcode == SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH);

    sm64_saturn_audio_policy_get_playing(&policy, 1U, &playing, &in_bank, &sound_id);
    assert(playing == 1U);
    assert(in_bank == 1U);
    assert(sound_id == 0x12U);

    sm64_saturn_audio_policy_tick(&policy);
    assert(policy.active_sfx_count == 1U);
    sm64_saturn_audio_policy_tick(&policy);
    assert(policy.active_sfx_count == 0U);
    assert(log.events[log.count - 1U].opcode == SM64_SATURN_AUDIO_OPCODE_STOP_HANDLE);

    refresh.sound_bits = 0x00138081U;
    refresh.source_token = 9U;
    refresh.freshness_generation = policy.freshness_generation;
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 100U));
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

static void test_discrete_waiting_expires_and_published_completion_promotes(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t high = {
        0x3020A081U, 3U, 1U, 255U, 64U, 4096U, 0U
    };
    sm64_saturn_audio_play_refresh_t waiting = {
        0x30218081U, 4U, 1U, 255U, 64U, 4096U, 0U
    };
    uint8_t i;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &high, 100U));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &waiting, 1000U));
    assert(log.count == 0U);
    assert(policy.active_sfx_count == 2U);
    assert(!policy.sfx[0].published);
    assert(!policy.sfx[1].published);
    sm64_saturn_audio_policy_tick(&policy);
    assert(log.count == 1U);
    assert(policy.sfx[0].published);
    assert(!policy.sfx[1].published);
    for (i = 1U; i < SM64_SATURN_AUDIO_DISCRETE_FRESHNESS; ++i) {
        sm64_saturn_audio_policy_tick(&policy);
    }
    assert(policy.active_sfx_count == 2U);
    sm64_saturn_audio_policy_tick(&policy);
    assert(policy.active_sfx_count == 1U);
    assert(!sm64_saturn_audio_policy_complete_handle(
        &policy, high.sound_bits, high.source_token,
        (uint16_t)(high.package_generation + 1U)));
    assert(sm64_saturn_audio_policy_complete_handle(
        &policy, high.sound_bits, high.source_token,
        high.package_generation));
    assert(policy.active_sfx_count == 0U);

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &high, 100U));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &waiting, 1000U));
    sm64_saturn_audio_policy_tick(&policy);
    assert(sm64_saturn_audio_policy_complete_handle(
        &policy, high.sound_bits, high.source_token,
        high.package_generation));
    assert(policy.active_sfx_count == 1U);
    assert(policy.sfx[1].published);
}

static void test_sound_id_catalog_bounds(void)
{
    static const uint8_t sizes[SM64_SATURN_AUDIO_BANK_COUNT] = {
        0x70U, 0x30U, 0x40U, 0x80U, 0x20U,
        0x80U, 0x20U, 0x40U, 0x80U, 0x80U,
    };
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t refresh = {
        0U, 1U, 1U, 255U, 64U, 4096U, 0U
    };
    uint8_t bank;

    init(&policy, &log);
    for (bank = 0U; bank < SM64_SATURN_AUDIO_BANK_COUNT; ++bank) {
        refresh.source_token = (uint16_t)(bank + 1U);
        refresh.sound_bits = ((uint32_t)bank << 28) |
                             ((uint32_t)(sizes[bank] - 1U) << 16) |
                             0x00008081U;
        assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 1U));
        assert(sm64_saturn_audio_policy_stop_source(
            &policy, refresh.source_token, refresh.package_generation));
        refresh.sound_bits = ((uint32_t)bank << 28) |
                             ((uint32_t)sizes[bank] << 16) |
                             0x00008081U;
        assert(!sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 1U));
    }
}

static void test_bank_pool_matches_inherited_38_usable_nodes(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t refresh = {
        0x30002001U, 1U, 1U, 255U, 64U, 4096U, 0U
    };
    uint16_t i;

    init(&policy, &log);
    for (i = 0U; i < 38U; ++i) {
        refresh.source_token = (uint16_t)(i + 1U);
        refresh.sound_bits = 0x30002001U | ((uint32_t)i << 16);
        assert(sm64_saturn_audio_policy_play_refresh(
            &policy, &refresh, (uint32_t)(100U + i)));
    }
    refresh.source_token = 39U;
    refresh.sound_bits = 0x30262001U;
    assert(!sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 200U));
    assert(policy.active_sfx_count == 38U);
}

static void test_lowering_only_tracks_published_sound_and_emits_fades(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t high = {
        0x3020A081U, 3U, 1U, 255U, 64U, 4096U, 0U
    };
    sm64_saturn_audio_play_refresh_t low_lowering = {
        0x30218091U, 4U, 1U, 255U, 64U, 4096U, 0U
    };
    uint16_t before;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0433U, 0U));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &high, 100U));
    sm64_saturn_audio_policy_tick(&policy);
    before = log.count;
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &low_lowering,
                                                  1000U));
    assert(policy.lowering_bank_mask == 0U);
    assert(log.count == before);
    assert(sm64_saturn_audio_policy_complete_handle(
        &policy, high.sound_bits, high.source_token,
        high.package_generation));
    assert(policy.lowering_bank_mask == (uint16_t)(1U << 3));
    assert(log.events[log.count - 1U].opcode ==
           SM64_SATURN_AUDIO_OPCODE_SEQ_FADE);
    assert(log.events[log.count - 1U].words[1] == 20U);
    assert(log.events[log.count - 1U].words[2] == 50U);
    assert(sm64_saturn_audio_policy_complete_handle(
        &policy, low_lowering.sound_bits, low_lowering.source_token,
        low_lowering.package_generation));
    assert(policy.lowering_bank_mask == 0U);
    assert(log.events[log.count - 1U].opcode ==
           SM64_SATURN_AUDIO_OPCODE_SEQ_FADE);
    assert(log.events[log.count - 1U].words[2] == 50U);

    assert(sm64_saturn_audio_policy_play_refresh(&policy, &low_lowering,
                                                  100U));
    sm64_saturn_audio_policy_tick(&policy);
    assert(policy.lowering_bank_mask == (uint16_t)(1U << 3));
    assert(sm64_saturn_audio_policy_stop_bank(&policy, 3U));
    assert(policy.lowering_bank_mask == 0U);
    assert(log.events[log.count - 1U].opcode ==
           SM64_SATURN_AUDIO_OPCODE_SEQ_FADE);
    assert(log.events[log.count - 1U].words[2] == 50U);
}

static void test_secondary_jingle_and_global_fade_publish_bounded_actions(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    uint16_t start;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_music(&policy, 0U, 0x0433U, 0U));
    start = log.count;
    assert(sm64_saturn_audio_policy_play_secondary(&policy, 0x13U, 60U,
                                                    90U, 20U));
    assert(log.count == (uint16_t)(start + 3U));
    assert(log.events[start].opcode == SM64_SATURN_AUDIO_OPCODE_SEQ_FADE);
    assert(log.events[start].words[0] == 0U);
    assert(log.events[start].words[1] == 60U);
    assert(log.events[start + 1U].opcode == SM64_SATURN_AUDIO_OPCODE_SEQ_START);
    assert(log.events[start + 2U].opcode == SM64_SATURN_AUDIO_OPCODE_SEQ_FADE);
    assert(log.events[start + 2U].words[0] == 1U);
    assert(log.events[start + 2U].words[1] == 90U);

    start = log.count;
    {
        const uint16_t generation = policy.environment_generation;
        assert(sm64_saturn_audio_policy_play_secondary(
            &policy, 0x0BU, 20U, SM64_SATURN_AUDIO_VOLUME_UNSET, 30U));
        assert(log.count == start);
        assert(policy.background_target_volume == 60U);
        assert(policy.secondary_seq_id == 0x13U);
        assert(policy.environment_generation == generation);
    }

    start = log.count;
    assert(sm64_saturn_audio_policy_play_jingle(&policy, 0x1BU, 20U));
    assert(log.count == (uint16_t)(start + 2U));
    assert(log.events[start + 1U].words[0] == 0U);
    assert(log.events[start + 1U].words[1] == 20U);
    start = log.count;
    assert(!sm64_saturn_audio_policy_environment_complete_matching(
        &policy, 0x1BU, (uint16_t)(policy.environment_generation - 1U)));
    assert(!sm64_saturn_audio_policy_environment_complete_matching(
        &policy, 0x1BU, policy.environment_generation));
    sm64_saturn_audio_policy_tick(&policy);
    assert(!sm64_saturn_audio_policy_environment_complete_matching(
        &policy, 0x1BU, policy.environment_generation));
    sm64_saturn_audio_policy_tick(&policy);
    assert(sm64_saturn_audio_policy_environment_complete_matching(
        &policy, 0x1BU, policy.environment_generation));
    assert(log.count == (uint16_t)(start + 3U));
    assert(log.events[start].words[0] == 0U);
    assert(log.events[start].words[1] == 60U);
    assert(log.events[start + 1U].opcode == SM64_SATURN_AUDIO_OPCODE_SEQ_START);
    assert(log.events[start + 2U].words[1] == 90U);

    start = log.count;
    assert(sm64_saturn_audio_policy_fade_sfx_banks(
        &policy, (uint16_t)(0x03FFU & ~(1U << 7)), 0U, 12U));
    assert(log.count == (uint16_t)(start + 1U));
    assert(log.events[start].opcode ==
           SM64_SATURN_AUDIO_OPCODE_SEQ_CHANNEL_FADE);
    assert(log.events[start].words[3] ==
           (uint16_t)(0x03FFU & ~(1U << 7)));
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
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &low, 1000U));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &high, 100U));
    sm64_saturn_audio_policy_tick(&policy);
    sm64_saturn_audio_policy_get_playing(&policy, 3U, &playing, &in_bank,
                                         &sound_id);
    assert(playing == 1U);
    assert(in_bank == 2U);
    assert(sound_id == 0x21U);
    assert(log.count == 1U);
    assert(log.events[0].opcode == SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH);

    assert(sm64_saturn_audio_policy_play_refresh(&policy, &low, 1000U));
    assert(log.count == 1U);
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

static void test_frame_spatial_update_can_change_selected_source(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t first = {
        0x30202001U, 3U, 1U, 180U, 64U, 4096U, 0U
    };
    sm64_saturn_audio_play_refresh_t second = {
        0x30212001U, 4U, 1U, 120U, 64U, 4096U, 0U
    };
    uint8_t playing;
    uint8_t in_bank;
    uint8_t sound_id;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &first, 100U));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &second, 1000U));
    sm64_saturn_audio_policy_tick(&policy);
    assert(sm64_saturn_audio_policy_update_spatial(
        &policy, first.source_token, first.package_generation,
        first.volume, first.pan, first.pitch, 2000U));
    assert(sm64_saturn_audio_policy_update_spatial(
        &policy, second.source_token, second.package_generation,
        second.volume, second.pan, second.pitch, 50U));
    sm64_saturn_audio_policy_tick(&policy);
    sm64_saturn_audio_policy_get_playing(&policy, 3U, &playing, &in_bank,
                                         &sound_id);
    assert(playing == 1U);
    assert(in_bank == 2U);
    assert(sound_id == 0x21U);
}

static void test_equal_priority_prefers_later_used_list_entry(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t first = {
        0x30202001U, 3U, 1U, 180U, 64U, 4096U, 0U
    };
    sm64_saturn_audio_play_refresh_t later = {
        0x30212001U, 4U, 1U, 120U, 64U, 4096U, 0U
    };
    uint8_t playing;
    uint8_t in_bank;
    uint8_t sound_id;

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &first, 100U));
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &later, 100U));
    sm64_saturn_audio_policy_tick(&policy);
    sm64_saturn_audio_policy_get_playing(&policy, 3U, &playing, &in_bank,
                                         &sound_id);
    assert(sound_id == 0x21U);
}

static void test_same_source_priority_and_discrete_restart(void)
{
    sm64_saturn_audio_policy_t policy;
    event_log_t log;
    sm64_saturn_audio_play_refresh_t refresh = {
        0x30128001U, 5U, 1U, 255U, 64U, 4096U, 0U
    };

    init(&policy, &log);
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 100U));
    refresh.sound_bits = 0x30114001U;
    assert(!sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 100U));
    assert(policy.sfx[0].sound_bits == 0x30128001U);
    refresh.sound_bits = 0x3013A081U;
    assert(sm64_saturn_audio_policy_play_refresh(&policy, &refresh, 100U));
    assert(policy.sfx[0].sound_bits == 0x3013A081U);
    assert(policy.sfx[0].restart_generation == 1U);
}

int main(void)
{
    test_six_entry_priority_queue_and_duplicates();
    test_stop_fade_and_queue_resume();
    test_secondary_jingle_and_lower_constraints_resume();
    test_bank_masks_continuous_freshness_stop_and_getter();
    test_discrete_waiting_expires_and_published_completion_promotes();
    test_sound_id_catalog_bounds();
    test_bank_pool_matches_inherited_38_usable_nodes();
    test_lowering_only_tracks_published_sound_and_emits_fades();
    test_secondary_jingle_and_global_fade_publish_bounded_actions();
    test_same_source_priority_and_discrete_restart();
    test_bank_priority_publishes_one_source_and_promotes_next();
    test_frame_spatial_update_can_change_selected_source();
    test_equal_priority_prefers_later_used_list_entry();
    return 0;
}
