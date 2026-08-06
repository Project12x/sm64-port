#include "saturn_audio_policy.h"

#include <stddef.h>
#include <string.h>

#include "saturn_audio_spatial.h"

enum {
    SOUNDARGS_MASK_BANK_VALUE = 0xF0000000U,
    SOUNDARGS_MASK_SOUND_ID_VALUE = 0x00FF0000U,
    SOUNDARGS_MASK_PRIORITY_VALUE = 0x0000FF00U,
    SOUNDARGS_SHIFT_BANK_VALUE = 28U,
    SOUNDARGS_SHIFT_SOUND_ID_VALUE = 16U,
    SOUNDARGS_SHIFT_PRIORITY_VALUE = 8U,
    SOUND_LOWER_BACKGROUND_MUSIC_VALUE = 0x10U,
    SOUND_DISCRETE_VALUE = 0x80U,
    SEQ_EVENT_PIRANHA_PLANT_VALUE = 0x0BU,
    SEQ_EVENT_MERRY_GO_ROUND_VALUE = 0x13U,
};

static const uint8_t s_num_sounds_per_bank[SM64_SATURN_AUDIO_BANK_COUNT] = {
    0x70U, 0x30U, 0x40U, 0x80U, 0x20U,
    0x80U, 0x20U, 0x40U, 0x80U, 0x80U,
};

static bool emit_words(sm64_saturn_audio_policy_t *policy,
                       sm64_saturn_audio_opcode_t opcode,
                       const uint16_t words[7])
{
    sm64_saturn_audio_event_t event;
    if (policy == NULL) {
        return false;
    }
    event.opcode = opcode;
    memcpy(event.words, words, sizeof(event.words));
    return policy->emit == NULL || policy->emit(policy->emit_context, &event);
}

static bool emit_control(sm64_saturn_audio_policy_t *policy,
                         sm64_saturn_audio_opcode_t opcode, uint16_t word0,
                         uint16_t word1, uint16_t word2)
{
    const uint16_t words[7] = {word0, word1, word2, 0U, 0U, 0U, 0U};
    return emit_words(policy, opcode, words);
}

static uint8_t sound_bank(uint32_t sound_bits)
{
    return (uint8_t)((sound_bits & SOUNDARGS_MASK_BANK_VALUE) >>
                     SOUNDARGS_SHIFT_BANK_VALUE);
}

static uint8_t sound_id(uint32_t sound_bits)
{
    return (uint8_t)((sound_bits & SOUNDARGS_MASK_SOUND_ID_VALUE) >>
                     SOUNDARGS_SHIFT_SOUND_ID_VALUE);
}

static uint8_t sound_priority(uint32_t sound_bits)
{
    return (uint8_t)((sound_bits & SOUNDARGS_MASK_PRIORITY_VALUE) >>
                     SOUNDARGS_SHIFT_PRIORITY_VALUE);
}

bool sm64_saturn_audio_policy_sound_id_valid(uint32_t sound_bits)
{
    const uint8_t bank = sound_bank(sound_bits);
    return bank < SM64_SATURN_AUDIO_BANK_COUNT &&
           sound_id(sound_bits) < s_num_sounds_per_bank[bank];
}

static bool advance_environment_generation(sm64_saturn_audio_policy_t *policy)
{
    if (policy->environment_generation == UINT16_MAX) {
        return false;
    }
    policy->environment_generation++;
    return true;
}

static sm64_saturn_audio_sfx_state_t *find_sfx(
    sm64_saturn_audio_policy_t *policy, uint8_t bank, uint16_t source_token,
    uint16_t package_generation)
{
    uint16_t i;
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && sound_bank(state->sound_bits) == bank &&
            state->source_token == source_token &&
            state->package_generation == package_generation) {
            return state;
        }
    }
    return NULL;
}

static sm64_saturn_audio_sfx_state_t *allocate_sfx(
    sm64_saturn_audio_policy_t *policy, uint8_t bank)
{
    uint16_t i;
    uint16_t in_bank = 0U;
    sm64_saturn_audio_sfx_state_t *free_slot = NULL;
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && sound_bank(state->sound_bits) == bank) {
            in_bank++;
        } else if (!state->active && free_slot == NULL) {
            free_slot = state;
        }
    }
    return in_bank < SM64_SATURN_AUDIO_SFX_PER_BANK ? free_slot : NULL;
}

static bool recompute_lowering_bank(sm64_saturn_audio_policy_t *policy,
                                    uint8_t bank)
{
    uint16_t i;
    const uint8_t old_volume =
        sm64_saturn_audio_policy_effective_background_volume(policy);
    bool emitted = true;
    policy->lowering_bank_mask &= (uint16_t)~(uint16_t)(1U << bank);
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        const sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && state->published &&
            sound_bank(state->sound_bits) == bank &&
            (state->sound_bits & SOUND_LOWER_BACKGROUND_MUSIC_VALUE) != 0U) {
            policy->lowering_bank_mask |= (uint16_t)(1U << bank);
            break;
        }
    }
    if (old_volume !=
            sm64_saturn_audio_policy_effective_background_volume(policy) &&
        policy->background_queue_size != 0U) {
        emitted = emit_control(
            policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, 0U,
            sm64_saturn_audio_policy_effective_background_volume(policy),
            50U);
    }
    return emitted;
}

static void clear_sfx(sm64_saturn_audio_policy_t *policy,
                      sm64_saturn_audio_sfx_state_t *state)
{
    memset(state, 0, sizeof(*state));
    if (policy->active_sfx_count != 0U) {
        policy->active_sfx_count--;
    }
}

static uint32_t sfx_priority_score(
    const sm64_saturn_audio_sfx_state_t *state)
{
    return state->priority_score;
}

static bool emit_play_state(sm64_saturn_audio_policy_t *policy,
                            const sm64_saturn_audio_sfx_state_t *state)
{
    sm64_saturn_audio_play_refresh_t refresh;
    uint16_t words[7];
    refresh.sound_bits = state->sound_bits;
    refresh.source_token = state->source_token;
    refresh.package_generation = state->package_generation;
    refresh.volume = state->volume;
    refresh.pan = state->pan;
    refresh.pitch = state->pitch;
    refresh.freshness_generation = state->last_refresh_generation;
    sm64_saturn_audio_spatial_encode_play_refresh(&refresh, words);
    return emit_words(policy, SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, words);
}

static bool emit_stop_state(sm64_saturn_audio_policy_t *policy,
                            const sm64_saturn_audio_sfx_state_t *state)
{
    const uint16_t words[7] = {
        (uint16_t)(state->sound_bits >> 16), (uint16_t)state->sound_bits,
        state->source_token, state->package_generation, 0U, 0U, 0U};
    return emit_words(policy, SM64_SATURN_AUDIO_OPCODE_STOP_HANDLE, words);
}

static bool publish_bank(sm64_saturn_audio_policy_t *policy, uint8_t bank,
                         uint16_t refreshed_token)
{
    sm64_saturn_audio_sfx_state_t *current = NULL;
    sm64_saturn_audio_sfx_state_t *best = NULL;
    uint32_t best_score = UINT32_MAX;
    uint16_t i;
    bool emitted = true;

    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        uint32_t score;
        if (!state->active || sound_bank(state->sound_bits) != bank) {
            continue;
        }
        if (state->published) {
            current = state;
        }
        score = sfx_priority_score(state);
        if (best == NULL || score <= best_score) {
            best = state;
            best_score = score;
        }
    }
    if (current != NULL && current != best) {
        emitted = emit_stop_state(policy, current) && emitted;
        current->published = false;
        if ((current->sound_bits & SOUND_DISCRETE_VALUE) != 0U) {
            clear_sfx(policy, current);
        }
    }
    if (best != NULL &&
        (best != current || best->source_token == refreshed_token)) {
        best->published = true;
        emitted = emit_play_state(policy, best) && emitted;
    }
    emitted = recompute_lowering_bank(policy, bank) && emitted;
    return emitted;
}

void sm64_saturn_audio_policy_init(sm64_saturn_audio_policy_t *policy,
                                   sm64_saturn_audio_emit_fn emit,
                                   void *emit_context)
{
    if (policy == NULL) {
        return;
    }
    memset(policy, 0, sizeof(*policy));
    policy->emit = emit;
    policy->emit_context = emit_context;
    policy->background_target_volume = SM64_SATURN_AUDIO_VOLUME_UNSET;
    policy->background_max_volume = SM64_SATURN_AUDIO_VOLUME_UNSET;
    policy->environment_seq_id = SM64_SATURN_AUDIO_SEQUENCE_NONE;
    policy->secondary_seq_id = SM64_SATURN_AUDIO_SEQUENCE_NONE;
}

bool sm64_saturn_audio_policy_play_music(sm64_saturn_audio_policy_t *policy,
                                         uint8_t player, uint16_t seq_args,
                                         uint16_t fade_timer)
{
    const uint8_t seq_id = (uint8_t)seq_args;
    const uint8_t priority = (uint8_t)(seq_args >> 8);
    uint8_t i;
    uint8_t found_index = 0U;

    if (policy == NULL) {
        return false;
    }
    if (player != 0U) {
        if (player == 1U) {
            if (!advance_environment_generation(policy)) {
                return false;
            }
            policy->environment_seq_id = seq_id;
        }
        return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_START,
                            player, seq_id, fade_timer);
    }
    if (policy->background_queue_size ==
        SM64_SATURN_AUDIO_BACKGROUND_QUEUE_CAPACITY) {
        return false;
    }
    for (i = 0U; i < policy->background_queue_size; ++i) {
        if (policy->background_queue[i].seq_id == seq_id) {
            if (i == 0U) {
                return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_START,
                                    player, seq_id, fade_timer);
            }
            return true;
        }
    }
    for (i = 0U; i < policy->background_queue_size; ++i) {
        if (policy->background_queue[i].priority <= priority) {
            found_index = i;
            break;
        }
    }
    if (found_index == 0U) {
        if (!emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_START,
                          player, seq_id, fade_timer)) {
            return false;
        }
        policy->background_queue_size++;
    }
    for (i = (uint8_t)(policy->background_queue_size - 1U);
         i > found_index; --i) {
        policy->background_queue[i] = policy->background_queue[i - 1U];
    }
    policy->background_queue[found_index].priority = priority;
    policy->background_queue[found_index].seq_id = seq_id;
    return true;
}

bool sm64_saturn_audio_policy_stop_background(
    sm64_saturn_audio_policy_t *policy, uint16_t seq_id)
{
    uint8_t i;
    uint8_t found;
    bool emitted = true;
    if (policy == NULL || policy->background_queue_size == 0U) {
        return false;
    }
    found = policy->background_queue_size;
    for (i = 0U; i < policy->background_queue_size; ++i) {
        if (policy->background_queue[i].seq_id == (uint8_t)seq_id) {
            found = i;
            break;
        }
    }
    if (found == policy->background_queue_size) {
        return false;
    }
    policy->background_queue_size--;
    if (found == 0U) {
        if (policy->background_queue_size != 0U) {
            emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_START,
                                   0U, policy->background_queue[1].seq_id, 0U);
        } else {
            emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE,
                                   0U, 0U, 20U);
        }
    }
    for (i = found; i < policy->background_queue_size; ++i) {
        policy->background_queue[i] = policy->background_queue[i + 1U];
    }
    memset(&policy->background_queue[policy->background_queue_size], 0,
           sizeof(policy->background_queue[0]));
    return emitted;
}

bool sm64_saturn_audio_policy_fadeout_background(
    sm64_saturn_audio_policy_t *policy, uint16_t seq_id, uint16_t fade_timer)
{
    if (policy == NULL || policy->background_queue_size == 0U ||
        policy->background_queue[0].seq_id != (uint8_t)seq_id) {
        return false;
    }
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, 0U, 0U,
                        fade_timer);
}

void sm64_saturn_audio_policy_drop_queued(sm64_saturn_audio_policy_t *policy)
{
    if (policy != NULL && policy->background_queue_size != 0U) {
        policy->background_queue_size = 1U;
    }
}

uint16_t sm64_saturn_audio_policy_current_background(
    const sm64_saturn_audio_policy_t *policy)
{
    if (policy == NULL || policy->background_queue_size == 0U) {
        return UINT16_MAX;
    }
    return (uint16_t)(((uint16_t)policy->background_queue[0].priority << 8) |
                      policy->background_queue[0].seq_id);
}

bool sm64_saturn_audio_policy_play_secondary(
    sm64_saturn_audio_policy_t *policy, uint8_t seq_id,
    uint8_t background_volume, uint8_t volume, uint16_t fade_timer)
{
    bool emitted;
    const bool first_secondary =
        policy != NULL &&
        policy->background_target_volume == SM64_SATURN_AUDIO_VOLUME_UNSET;
    if (policy == NULL || policy->background_queue_size == 0U ||
        policy->background_queue[0].seq_id == 2U) {
        return false;
    }
    if (first_secondary) {
        if (!advance_environment_generation(policy)) {
            return false;
        }
        policy->background_target_volume = background_volume;
        policy->secondary_seq_id = seq_id;
        policy->secondary_volume = volume;
        policy->environment_seq_id = seq_id;
        emitted = emit_control(
            policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, 0U,
            sm64_saturn_audio_policy_effective_background_volume(policy),
            fade_timer);
        emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_START, 1U,
                               seq_id, (uint16_t)(fade_timer >> 1)) && emitted;
        if (volume < 0x80U) {
            emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE,
                                   1U, volume, fade_timer) && emitted;
        }
        return emitted;
    }
    if (volume != SM64_SATURN_AUDIO_VOLUME_UNSET) {
        policy->background_target_volume = background_volume;
        policy->secondary_volume = volume;
        emitted = emit_control(
            policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, 0U,
            sm64_saturn_audio_policy_effective_background_volume(policy),
            fade_timer);
        emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE,
                               1U, volume, fade_timer) && emitted;
        return emitted;
    }
    return true;
}

bool sm64_saturn_audio_policy_stop_secondary(
    sm64_saturn_audio_policy_t *policy, uint16_t fade_timer)
{
    bool emitted;
    if (policy == NULL ||
        policy->background_target_volume == SM64_SATURN_AUDIO_VOLUME_UNSET) {
        return false;
    }
    policy->background_target_volume = SM64_SATURN_AUDIO_VOLUME_UNSET;
    policy->secondary_seq_id = SM64_SATURN_AUDIO_SEQUENCE_NONE;
    policy->secondary_volume = SM64_SATURN_AUDIO_VOLUME_UNSET;
    policy->environment_seq_id = SM64_SATURN_AUDIO_SEQUENCE_NONE;
    emitted = emit_control(
        policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, 0U,
        sm64_saturn_audio_policy_effective_background_volume(policy),
        fade_timer);
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, 1U, 0U,
                        fade_timer) && emitted;
}

bool sm64_saturn_audio_policy_play_jingle(sm64_saturn_audio_policy_t *policy,
                                          uint8_t seq_id,
                                          uint8_t max_background_volume)
{
    bool emitted;
    if (policy == NULL) {
        return false;
    }
    if (!advance_environment_generation(policy)) {
        return false;
    }
    policy->environment_seq_id = seq_id;
    policy->background_max_volume = max_background_volume;
    policy->environment_completion_guard = 2U;
    emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_START, 1U,
                           seq_id, 0U);
    return emit_control(
               policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, 0U,
               sm64_saturn_audio_policy_effective_background_volume(policy),
               50U) && emitted;
}

bool sm64_saturn_audio_policy_environment_complete(
    sm64_saturn_audio_policy_t *policy)
{
    if (policy == NULL) {
        return false;
    }
    return sm64_saturn_audio_policy_environment_complete_matching(
        policy, policy->environment_seq_id, policy->environment_generation);
}

bool sm64_saturn_audio_policy_environment_complete_matching(
    sm64_saturn_audio_policy_t *policy, uint8_t seq_id,
    uint16_t environment_generation)
{
    bool emitted;
    if (policy == NULL ||
        policy->environment_seq_id != seq_id ||
        policy->environment_generation != environment_generation ||
        policy->environment_completion_guard != 0U ||
        policy->background_max_volume == SM64_SATURN_AUDIO_VOLUME_UNSET) {
        return false;
    }
    policy->background_max_volume = SM64_SATURN_AUDIO_VOLUME_UNSET;
    emitted = emit_control(
        policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, 0U,
        sm64_saturn_audio_policy_effective_background_volume(policy), 50U);
    if (policy->secondary_seq_id == SEQ_EVENT_PIRANHA_PLANT_VALUE ||
        policy->secondary_seq_id == SEQ_EVENT_MERRY_GO_ROUND_VALUE) {
        policy->environment_seq_id = policy->secondary_seq_id;
        emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_START, 1U,
                               policy->secondary_seq_id, 1U) && emitted;
        if (policy->secondary_volume != SM64_SATURN_AUDIO_VOLUME_UNSET) {
            emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE,
                                   1U, policy->secondary_volume, 1U) && emitted;
        }
        return emitted;
    }
    policy->environment_seq_id = SM64_SATURN_AUDIO_SEQUENCE_NONE;
    return emitted;
}

bool sm64_saturn_audio_policy_lower(sm64_saturn_audio_policy_t *policy,
                                    uint8_t player, uint16_t fade_timer,
                                    uint8_t percentage)
{
    if (policy == NULL) {
        return false;
    }
    if (player == 0U) {
        policy->lower_background_music = true;
        percentage =
            sm64_saturn_audio_policy_effective_background_volume(policy);
    }
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, player,
                        percentage, fade_timer);
}

bool sm64_saturn_audio_policy_unlower(sm64_saturn_audio_policy_t *policy,
                                      uint8_t player, uint16_t fade_timer)
{
    if (policy == NULL) {
        return false;
    }
    policy->lower_background_music = false;
    if (player == 0U) {
        return emit_control(
            policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, player,
            sm64_saturn_audio_policy_effective_background_volume(policy),
            fade_timer);
    }
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, player,
                        SM64_SATURN_AUDIO_VOLUME_UNSET, fade_timer);
}

uint8_t sm64_saturn_audio_policy_effective_background_volume(
    const sm64_saturn_audio_policy_t *policy)
{
    uint8_t target;
    if (policy == NULL) {
        return 0U;
    }
    target = policy->background_target_volume;
    if (policy->background_max_volume != SM64_SATURN_AUDIO_VOLUME_UNSET &&
        (target == SM64_SATURN_AUDIO_VOLUME_UNSET ||
         target > policy->background_max_volume)) {
        target = policy->background_max_volume;
    }
    if (policy->lower_background_music &&
        (target == SM64_SATURN_AUDIO_VOLUME_UNSET || target > 40U)) {
        target = 40U;
    }
    if (policy->lowering_bank_mask != 0U &&
        (target == SM64_SATURN_AUDIO_VOLUME_UNSET || target > 20U)) {
        target = 20U;
    }
    return target;
}

void sm64_saturn_audio_policy_disable_banks(sm64_saturn_audio_policy_t *policy,
                                            uint16_t bank_mask)
{
    if (policy != NULL) {
        policy->disabled_bank_mask |= bank_mask;
        (void)emit_control(policy, SM64_SATURN_AUDIO_OPCODE_BANK_MASK,
                           policy->disabled_bank_mask, 0U, 0U);
    }
}

void sm64_saturn_audio_policy_enable_banks(sm64_saturn_audio_policy_t *policy,
                                           uint16_t bank_mask)
{
    if (policy != NULL) {
        policy->disabled_bank_mask &= (uint16_t)~bank_mask;
        (void)emit_control(policy, SM64_SATURN_AUDIO_OPCODE_BANK_MASK,
                           policy->disabled_bank_mask, 0U, 0U);
    }
}

static bool admit_play_refresh(
    sm64_saturn_audio_policy_t *policy,
    const sm64_saturn_audio_play_refresh_t *refresh,
    uint32_t priority_score)
{
    uint8_t bank;
    sm64_saturn_audio_sfx_state_t *state;
    bool restart = false;

    if (policy == NULL || refresh == NULL || refresh->source_token == 0U) {
        return false;
    }
    bank = sound_bank(refresh->sound_bits);
    if (bank >= SM64_SATURN_AUDIO_BANK_COUNT ||
        !sm64_saturn_audio_policy_sound_id_valid(refresh->sound_bits) ||
        (policy->disabled_bank_mask & (uint16_t)(1U << bank)) != 0U) {
        return false;
    }
    state = find_sfx(policy, bank, refresh->source_token,
                     refresh->package_generation);
    if (state != NULL) {
        if (sound_priority(state->sound_bits) >
            sound_priority(refresh->sound_bits)) {
            return false;
        }
        if ((state->sound_bits & SOUND_DISCRETE_VALUE) != 0U ||
            sound_id(state->sound_bits) != sound_id(refresh->sound_bits)) {
            restart = true;
        }
    } else {
        state = allocate_sfx(policy, bank);
        if (state == NULL) {
            return false;
        }
        memset(state, 0, sizeof(*state));
        state->active = true;
        policy->active_sfx_count++;
    }
    if (restart) {
        state->restart_generation++;
    }
    state->sound_bits = refresh->sound_bits;
    state->source_token = refresh->source_token;
    state->package_generation = refresh->package_generation;
    state->last_refresh_generation = policy->freshness_generation;
    state->priority_score = priority_score;
    state->volume = refresh->volume;
    state->pan = refresh->pan;
    state->pitch = refresh->pitch;
    return true;
}

bool sm64_saturn_audio_policy_play_refresh(
    sm64_saturn_audio_policy_t *policy,
    const sm64_saturn_audio_play_refresh_t *refresh,
    uint32_t priority_score)
{
    sm64_saturn_audio_pending_request_t *pending;
    if (policy == NULL || refresh == NULL || refresh->source_token == 0U ||
        policy->pending_request_count == SM64_SATURN_AUDIO_REQUEST_CAPACITY) {
        return false;
    }
    pending = &policy->pending_requests[policy->pending_request_count++];
    pending->refresh = *refresh;
    pending->priority_score = priority_score;
    return true;
}

bool sm64_saturn_audio_policy_stop_handle(sm64_saturn_audio_policy_t *policy,
                                          uint32_t sound_bits,
                                          uint16_t source_token,
                                          uint16_t package_generation)
{
    uint16_t i;
    if (policy == NULL || source_token == 0U) {
        return false;
    }
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && state->source_token == source_token &&
            state->package_generation == package_generation &&
            sound_id(state->sound_bits) == sound_id(sound_bits) &&
            sound_bank(state->sound_bits) == sound_bank(sound_bits)) {
            const bool published = state->published;
            bool emitted = true;
            if (published) {
                emitted = emit_stop_state(policy, state);
            }
            clear_sfx(policy, state);
            return publish_bank(policy, sound_bank(sound_bits), 0U) && emitted;
        }
    }
    return false;
}

bool sm64_saturn_audio_policy_complete_handle(
    sm64_saturn_audio_policy_t *policy, uint32_t sound_bits,
    uint16_t source_token, uint16_t package_generation)
{
    uint16_t i;
    if (policy == NULL || source_token == 0U) {
        return false;
    }
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && state->published &&
            state->source_token == source_token &&
            state->package_generation == package_generation &&
            sound_id(state->sound_bits) == sound_id(sound_bits) &&
            sound_bank(state->sound_bits) == sound_bank(sound_bits)) {
            const uint8_t bank = sound_bank(state->sound_bits);
            clear_sfx(policy, state);
            return publish_bank(policy, bank, 0U);
        }
    }
    return false;
}

bool sm64_saturn_audio_policy_update_spatial(
    sm64_saturn_audio_policy_t *policy, uint32_t sound_bits,
    uint16_t source_token, uint16_t package_generation, uint8_t volume,
    uint8_t pan, uint16_t pitch, uint32_t priority_score)
{
    uint16_t i;
    bool found = false;
    if (policy == NULL || source_token == 0U) {
        return false;
    }
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && state->source_token == source_token &&
            state->package_generation == package_generation &&
            state->sound_bits == sound_bits) {
            state->volume = volume;
            state->pan = pan;
            state->pitch = pitch;
            state->priority_score = priority_score;
            found = true;
        }
    }
    for (i = 0U; i < policy->pending_request_count; ++i) {
        sm64_saturn_audio_pending_request_t *pending =
            &policy->pending_requests[i];
        if (pending->refresh.source_token == source_token &&
            pending->refresh.package_generation == package_generation &&
            pending->refresh.sound_bits == sound_bits) {
            pending->refresh.volume = volume;
            pending->refresh.pan = pan;
            pending->refresh.pitch = pitch;
            pending->priority_score = priority_score;
            found = true;
        }
    }
    return found;
}

bool sm64_saturn_audio_policy_token_is_active(
    const sm64_saturn_audio_policy_t *policy, uint16_t source_token,
    uint16_t package_generation)
{
    uint16_t i;
    if (policy == NULL || source_token == 0U) {
        return false;
    }
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        const sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && state->source_token == source_token &&
            state->package_generation == package_generation) {
            return true;
        }
    }
    for (i = 0U; i < policy->pending_request_count; ++i) {
        const sm64_saturn_audio_pending_request_t *pending =
            &policy->pending_requests[i];
        if (pending->refresh.source_token == source_token &&
            pending->refresh.package_generation == package_generation) {
            return true;
        }
    }
    return false;
}

bool sm64_saturn_audio_policy_stop_source(sm64_saturn_audio_policy_t *policy,
                                          uint16_t source_token,
                                          uint16_t package_generation)
{
    uint16_t i;
    uint16_t affected_banks = 0U;
    bool found = false;
    bool emitted;
    if (policy == NULL || source_token == 0U) {
        return false;
    }
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && state->source_token == source_token &&
            state->package_generation == package_generation) {
            affected_banks |= (uint16_t)(1U << sound_bank(state->sound_bits));
            clear_sfx(policy, state);
            found = true;
        }
    }
    if (found) {
        emitted = emit_control(policy, SM64_SATURN_AUDIO_OPCODE_STOP_SOURCE,
                               source_token, package_generation, 0U);
        for (i = 0U; i < SM64_SATURN_AUDIO_BANK_COUNT; ++i) {
            if ((affected_banks & (uint16_t)(1U << i)) != 0U) {
                emitted = publish_bank(policy, (uint8_t)i, 0U) && emitted;
            }
        }
        return emitted;
    }
    return false;
}

bool sm64_saturn_audio_policy_stop_bank(sm64_saturn_audio_policy_t *policy,
                                        uint8_t bank)
{
    uint16_t i;
    bool found = false;
    if (policy == NULL || bank >= SM64_SATURN_AUDIO_BANK_COUNT) {
        return false;
    }
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        if (state->active && sound_bank(state->sound_bits) == bank) {
            clear_sfx(policy, state);
            found = true;
        }
    }
    if (found) {
        const uint16_t words[7] = {bank, 0U, 0U, 0U, 0U, 0U, 0U};
        const bool emitted =
            emit_words(policy, SM64_SATURN_AUDIO_OPCODE_STOP_BANK, words);
        return recompute_lowering_bank(policy, bank) && emitted;
    }
    return false;
}

void sm64_saturn_audio_policy_tick(sm64_saturn_audio_policy_t *policy)
{
    uint16_t i;
    uint16_t pending_count;
    uint8_t bank;
    if (policy == NULL) {
        return;
    }
    if (policy->environment_completion_guard != 0U) {
        policy->environment_completion_guard--;
    }
    pending_count = policy->pending_request_count;
    policy->pending_request_count = 0U;
    for (i = 0U; i < pending_count; ++i) {
        const sm64_saturn_audio_pending_request_t *pending =
            &policy->pending_requests[i];
        (void)admit_play_refresh(policy, &pending->refresh,
                                 pending->priority_score);
    }
    policy->freshness_generation++;
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
        uint16_t age;
        if (!state->active) {
            continue;
        }
        age = (uint16_t)(policy->freshness_generation -
                         state->last_refresh_generation);
        if ((state->sound_bits & SOUND_DISCRETE_VALUE) != 0U &&
            state->published) {
            continue;
        }
        if (((state->sound_bits & SOUND_DISCRETE_VALUE) != 0U &&
             age > SM64_SATURN_AUDIO_DISCRETE_FRESHNESS) ||
            ((state->sound_bits & SOUND_DISCRETE_VALUE) == 0U &&
             age > SM64_SATURN_AUDIO_CONTINUOUS_GRACE)) {
            if (state->published) {
                (void)emit_stop_state(policy, state);
            }
            clear_sfx(policy, state);
        }
    }
    for (bank = 0U; bank < SM64_SATURN_AUDIO_BANK_COUNT; ++bank) {
        uint16_t refresh_token = 0U;
        for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
            const sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
            if (state->active && state->published &&
                sound_bank(state->sound_bits) == bank) {
                refresh_token = state->source_token;
                break;
            }
        }
        (void)publish_bank(policy, bank, refresh_token);
    }
}

void sm64_saturn_audio_policy_get_playing(
    const sm64_saturn_audio_policy_t *policy, uint8_t bank,
    uint8_t *num_playing, uint8_t *num_in_bank, uint8_t *out_sound_id)
{
    uint16_t i;
    uint8_t count = 0U;
    uint8_t playing = 0U;
    uint8_t first = 0xFFU;
    if (policy != NULL && bank < SM64_SATURN_AUDIO_BANK_COUNT) {
        for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
            const sm64_saturn_audio_sfx_state_t *state = &policy->sfx[i];
            if (state->active && sound_bank(state->sound_bits) == bank) {
                if (first == 0xFFU) {
                    if (state->published) {
                        first = sound_id(state->sound_bits);
                    }
                }
                if (count != 0xFFU) {
                    count++;
                }
                if (state->published) {
                    playing = 1U;
                }
            }
        }
    }
    if (num_playing != NULL) {
        *num_playing = playing;
    }
    if (num_in_bank != NULL) {
        *num_in_bank = count;
    }
    if (out_sound_id != NULL) {
        *out_sound_id = first;
    }
}

bool sm64_saturn_audio_policy_fade_player(sm64_saturn_audio_policy_t *policy,
                                          uint8_t player, uint8_t target,
                                          uint16_t fade_timer)
{
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_FADE, player,
                        target, fade_timer);
}

bool sm64_saturn_audio_policy_fade_channels(
    sm64_saturn_audio_policy_t *policy, uint8_t player, uint8_t target,
    uint16_t fade_timer)
{
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_CHANNEL_FADE,
                        player, target, fade_timer);
}

bool sm64_saturn_audio_policy_fade_sfx_banks(
    sm64_saturn_audio_policy_t *policy, uint16_t bank_mask, uint8_t target,
    uint16_t fade_timer)
{
    const uint16_t words[7] = {
        2U, target, fade_timer, bank_mask, 0U, 0U, 0U,
    };
    return emit_words(policy, SM64_SATURN_AUDIO_OPCODE_SEQ_CHANNEL_FADE,
                      words);
}

bool sm64_saturn_audio_policy_mute(sm64_saturn_audio_policy_t *policy,
                                   bool muted)
{
    if (policy == NULL) {
        return false;
    }
    policy->muted = muted;
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_MUTE,
                        muted ? 1U : 0U, 0U, 0U);
}

bool sm64_saturn_audio_policy_reset(sm64_saturn_audio_policy_t *policy,
                                    uint8_t preset_id)
{
    sm64_saturn_audio_emit_fn emit;
    void *context;
    if (policy == NULL) {
        return false;
    }
    emit = policy->emit;
    context = policy->emit_context;
    sm64_saturn_audio_policy_init(policy, emit, context);
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_RESET, preset_id, 0U,
                        0U);
}

bool sm64_saturn_audio_policy_set_sound_mode(sm64_saturn_audio_policy_t *policy,
                                             uint8_t mode)
{
    if (policy == NULL) {
        return false;
    }
    policy->sound_mode = mode;
    return emit_control(policy, SM64_SATURN_AUDIO_OPCODE_SOUND_MODE, mode, 0U,
                        0U);
}
