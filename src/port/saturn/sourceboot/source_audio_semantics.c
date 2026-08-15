/* Saturn source-audio semantic adapter.
 *
 * Reuse mode: close adaptation within this repository of the inherited
 * src/audio/external.c policy paths (play_sound/process_sound_request,
 * background queue, fades, secondary music, jingles, bank masks and stops).
 * The public src/audio/external.h ABI is unchanged.  Only bounded semantic
 * records cross the SH-2/MC68000 seam; the raw f32 position identity remains
 * in s_spatial on the source SH-2.
 */
#include <stddef.h>
#include <string.h>
#include <ultra64.h>

#include "audio/external.h"
#include "dialog_ids.h"
#include "macros.h"
#include "seq_ids.h"
#include "sounds.h"
#include "game/area.h"
#include "level_table.h"
#include "port/saturn/audio/saturn_audio_policy.h"
#include "port/saturn/audio/saturn_audio_spatial.h"
#include "source_audio_semantics.h"

#if defined(TARGET_SATURN) && defined(SATURN_SOURCEBOOT)
#define SOURCE_AUDIO_EXTERNAL_WORKSPACE 1
#define SOURCE_AUDIO_STATIC_ABI \
    __attribute__((section(".lwram_bss"), used))
#else
#define SOURCE_AUDIO_STATIC_ABI
#endif

typedef struct source_audio_semantic_workspace {
    sm64_saturn_audio_policy_t policy;
    sm64_saturn_audio_spatial_table_t spatial;
    u8 moving_speed[SM64_SATURN_AUDIO_BANK_COUNT];
    u16 package_generation;
    u32 audio_frame_count;
    u8 pending_environment_seq_id;
    u16 pending_environment_generation;
    bool environment_completion_pending;
    u8 initialized;
} source_audio_semantic_workspace_t;

#if defined(SOURCE_AUDIO_EXTERNAL_WORKSPACE)
static source_audio_semantic_workspace_t *s_state SOURCE_AUDIO_STATIC_ABI;
#else
static source_audio_semantic_workspace_t s_host_state;
static source_audio_semantic_workspace_t *s_state = &s_host_state;
#endif

/* These three public ABI values are referenced by the source game objects.
 * Keep only this 20-byte ABI in static HWRAM; the large policy/spatial state
 * below is caller-owned main-pool memory. */
s32 gAudioErrorFlags SOURCE_AUDIO_STATIC_ABI;
f32 gGlobalSoundSource[3] SOURCE_AUDIO_STATIC_ABI;
u32 gAudioRandom SOURCE_AUDIO_STATIC_ABI;

#define s_policy (s_state->policy)
#define s_spatial (s_state->spatial)
#define s_moving_speed (s_state->moving_speed)
#define s_package_generation (s_state->package_generation)
#define s_audio_frame_count (s_state->audio_frame_count)
#define s_pending_environment_seq_id (s_state->pending_environment_seq_id)
#define s_pending_environment_generation (s_state->pending_environment_generation)
#define s_environment_completion_pending (s_state->environment_completion_pending)
#define s_initialized (s_state->initialized)

#define STUB_LEVEL(_0, _1, _2, volume, _4, _5, _6, _7, _8) volume,
#define DEFINE_LEVEL(_0, _1, _2, _3, _4, volume, _6, _7, _8, _9, _10) volume,
static const u16 s_level_acoustic_reach[LEVEL_COUNT] = {
    20000U,
#include "levels/level_defines.h"
};
#undef STUB_LEVEL
#undef DEFINE_LEVEL

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
bool sm64_saturn_source_audio_emit_event(
    const sm64_saturn_audio_event_t *event)
{
    (void)event;
    return true;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
bool sm64_saturn_source_audio_poll_sfx_completion(
    u32 *sound_bits, u16 *source_token, u16 *package_generation)
{
    (void)sound_bits;
    (void)source_token;
    (void)package_generation;
    return false;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
bool sm64_saturn_source_audio_poll_environment_completion(
    u8 *seq_id, u16 *environment_generation)
{
    (void)seq_id;
    (void)environment_generation;
    return false;
}

static bool source_emit(void *context,
                        const sm64_saturn_audio_event_t *event)
{
    (void)context;
    return sm64_saturn_source_audio_emit_event(event);
}

size_t sm64_saturn_source_audio_semantic_workspace_bytes(void)
{
    return sizeof(source_audio_semantic_workspace_t);
}

bool sm64_saturn_source_audio_semantic_workspace_bind(void *workspace,
                                                       size_t workspace_bytes)
{
    if (workspace == NULL ||
        workspace_bytes < sizeof(source_audio_semantic_workspace_t) ||
        ((uintptr_t)workspace % _Alignof(source_audio_semantic_workspace_t)) !=
            0U) {
        return false;
    }
    memset(workspace, 0, sizeof(source_audio_semantic_workspace_t));
    s_state = workspace;
    s_package_generation = 1U;
    return true;
}

void sm64_saturn_source_audio_semantics_reset(void)
{
    /* Explicitly park the module in its unbound fail-closed state.  On the
     * sourceboot target every static here lives in NOLOAD .lwram_bss, which
     * is never crt0-zeroed: until someone stores to them, s_state and the
     * public ABI globals hold whatever the RAM powered up with.  Sourceboot
     * calls this before any bind attempt so both a fresh boot and a failed
     * audio init end with a provably-NULL workspace pointer and zeroed ABI
     * globals.  Must not read or dereference s_state: its current contents
     * are untrusted. */
#if defined(SOURCE_AUDIO_EXTERNAL_WORKSPACE)
    s_state = NULL;
#else
    memset(&s_host_state, 0, sizeof(s_host_state));
    s_state = &s_host_state;
#endif
    gAudioErrorFlags = 0;
    memset(gGlobalSoundSource, 0, sizeof(gGlobalSoundSource));
    gAudioRandom = 0U;
}

static bool source_audio_initialize(void)
{
    u8 bank;
    if (s_state == NULL) {
        return false;
    }
    sm64_saturn_audio_policy_init(&s_policy, source_emit, NULL);
    sm64_saturn_audio_spatial_init(&s_spatial);
    gAudioErrorFlags = 0;
    memset(gGlobalSoundSource, 0, sizeof(gGlobalSoundSource));
    gAudioRandom = 0U;
    for (bank = 0U; bank < SM64_SATURN_AUDIO_BANK_COUNT; ++bank) {
        s_moving_speed[bank] = 32U;
    }
    s_package_generation = 1U;
    s_initialized = TRUE;
    return true;
}

static bool source_audio_ensure_initialized(void)
{
    if (s_state == NULL) {
        return false;
    }
    if (!s_initialized) {
        return source_audio_initialize();
    }
    return true;
}

static u16 source_level_acoustic_reach(void)
{
    const s16 level = gCurrLevelNum;
    if (level < LEVEL_NONE || level >= LEVEL_COUNT) {
        return s_level_acoustic_reach[LEVEL_NONE];
    }
    return s_level_acoustic_reach[level];
}

static bool token_is_active(u16 token)
{
    return sm64_saturn_audio_policy_token_is_active(
        &s_policy, token, s_package_generation);
}

static void release_inactive_sources(void)
{
    u16 i;
    for (i = 0U; i < SM64_SATURN_AUDIO_SOURCE_CAPACITY; ++i) {
        const f32 *identity = s_spatial.entries[i].identity;
        u16 token;
        if (!s_spatial.entries[i].active || identity == NULL) {
            continue;
        }
        token = sm64_saturn_audio_spatial_find(&s_spatial, identity);
        if (!token_is_active(token)) {
            (void)sm64_saturn_audio_spatial_release(&s_spatial, identity);
        }
    }
}

static void refresh_active_source_positions(void)
{
    u16 i;
    for (i = 0U; i < SM64_SATURN_AUDIO_SFX_CAPACITY; ++i) {
        const sm64_saturn_audio_sfx_state_t *state = &s_policy.sfx[i];
        const f32 *pos;
        sm64_saturn_audio_spatial_params_t params;
        u8 bank;
        if (!state->active) {
            continue;
        }
        pos = sm64_saturn_audio_spatial_resolve(
            &s_spatial, state->source_token, state->package_generation);
        if (pos == NULL) {
            continue;
        }
        bank = (u8)(((u32)state->sound_bits & SOUNDARGS_MASK_BANK) >>
                    SOUNDARGS_SHIFT_BANK);
        sm64_saturn_audio_spatial_quantize(
            state->sound_bits, bank, s_moving_speed[bank],
            source_level_acoustic_reach(), gAudioRandom,
            pos[0], pos[1], pos[2], &params);
        (void)sm64_saturn_audio_policy_update_spatial(
            &s_policy, state->sound_bits, state->source_token,
            state->package_generation, params.volume, params.pan,
            params.pitch, params.priority_score);
    }
    for (i = 0U; i < s_policy.pending_request_count; ++i) {
        sm64_saturn_audio_pending_request_t *pending =
            &s_policy.pending_requests[i];
        const f32 *pos = sm64_saturn_audio_spatial_resolve(
            &s_spatial, pending->refresh.source_token,
            pending->refresh.package_generation);
        sm64_saturn_audio_spatial_params_t params;
        u8 bank;
        if (pos == NULL) {
            continue;
        }
        bank = (u8)((pending->refresh.sound_bits & SOUNDARGS_MASK_BANK) >>
                    SOUNDARGS_SHIFT_BANK);
        if (bank >= SM64_SATURN_AUDIO_BANK_COUNT) {
            continue;
        }
        sm64_saturn_audio_spatial_quantize(
            pending->refresh.sound_bits, bank, s_moving_speed[bank],
            source_level_acoustic_reach(), gAudioRandom,
            pos[0], pos[1], pos[2], &params);
        (void)sm64_saturn_audio_policy_update_spatial(
            &s_policy, pending->refresh.sound_bits,
            pending->refresh.source_token,
            pending->refresh.package_generation, params.volume, params.pan,
            params.pitch, params.priority_score);
    }
}

static void consume_driver_completions(void)
{
    u8 i;
    for (i = 0U; i < 16U; ++i) {
        u32 sound_bits;
        u16 source_token;
        u16 package_generation;
        if (!sm64_saturn_source_audio_poll_sfx_completion(
                &sound_bits, &source_token, &package_generation)) {
            break;
        }
        (void)sm64_saturn_audio_policy_complete_handle(
            &s_policy, sound_bits, source_token, package_generation);
    }
    if (s_environment_completion_pending &&
        (s_pending_environment_seq_id != s_policy.environment_seq_id ||
         s_pending_environment_generation !=
             s_policy.environment_generation)) {
        s_environment_completion_pending = false;
    }
    if (!s_environment_completion_pending &&
        sm64_saturn_source_audio_poll_environment_completion(
            &s_pending_environment_seq_id,
            &s_pending_environment_generation)) {
        s_environment_completion_pending = true;
    }
    if (s_environment_completion_pending &&
        sm64_saturn_audio_policy_environment_complete_matching(
            &s_policy, s_pending_environment_seq_id,
            s_pending_environment_generation)) {
        s_environment_completion_pending = false;
    }
}

struct SPTask *create_next_audio_frame_task(void)
{
    if (!source_audio_ensure_initialized()) {
        return NULL;
    }
    s_audio_frame_count++;
    gAudioRandom = (gAudioRandom + s_audio_frame_count) * s_audio_frame_count;
    return NULL;
}

void play_sound(s32 soundBits, f32 *pos)
{
    sm64_saturn_audio_play_refresh_t refresh;
    sm64_saturn_audio_spatial_params_t params;
    u8 bank;

    if (!source_audio_ensure_initialized()) {
        return;
    }
    if (pos == NULL || soundBits == NO_SOUND) {
        return;
    }
    bank = (u8)(((u32)soundBits & SOUNDARGS_MASK_BANK) >>
                SOUNDARGS_SHIFT_BANK);
    if (bank >= SM64_SATURN_AUDIO_BANK_COUNT) {
        return;
    }
    if (!sm64_saturn_audio_policy_sound_id_valid((u32)soundBits)) {
        return;
    }
    refresh.source_token = sm64_saturn_audio_spatial_acquire(
        &s_spatial, pos, s_package_generation);
    if (refresh.source_token == 0U) {
        gAudioErrorFlags++;
        return;
    }
    sm64_saturn_audio_spatial_quantize(
        (u32)soundBits, bank, s_moving_speed[bank],
        source_level_acoustic_reach(),
        gAudioRandom, pos[0], pos[1], pos[2], &params);
    refresh.sound_bits = (u32)soundBits;
    refresh.package_generation = s_package_generation;
    refresh.volume = params.volume;
    refresh.pan = params.pan;
    refresh.pitch = params.pitch;
    refresh.freshness_generation = s_policy.freshness_generation;
    if (!sm64_saturn_audio_policy_play_refresh(&s_policy, &refresh,
                                                params.priority_score)) {
        gAudioErrorFlags++;
    }
}

void audio_signal_game_loop_tick(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    refresh_active_source_positions();
    sm64_saturn_audio_policy_tick(&s_policy);
    consume_driver_completions();
    release_inactive_sources();
}

void seq_player_fade_out(u8 player, u16 fadeDuration)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_fade_player(&s_policy, player, 0U,
                                               fadeDuration);
}

void fade_volume_scale(u8 player, u8 targetScale, u16 fadeDuration)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_fade_channels(
        &s_policy, player, targetScale, fadeDuration);
}

void seq_player_lower_volume(u8 player, u16 fadeDuration, u8 percentage)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_lower(&s_policy, player, fadeDuration,
                                         percentage);
}

void seq_player_unlower_volume(u8 player, u16 fadeDuration)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_unlower(&s_policy, player, fadeDuration);
}

void set_audio_muted(u8 muted)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_mute(&s_policy, muted != 0U);
}

void sound_init(void)
{
    (void)source_audio_initialize();
}

void get_currently_playing_sound(u8 bank, u8 *numPlayingSounds,
                                 u8 *numSoundsInBank, u8 *soundId)
{
    if (!source_audio_ensure_initialized()) {
        if (numPlayingSounds != NULL) {
            *numPlayingSounds = 0U;
        }
        if (numSoundsInBank != NULL) {
            *numSoundsInBank = 0U;
        }
        if (soundId != NULL) {
            *soundId = 0U;
        }
        return;
    }
    sm64_saturn_audio_policy_get_playing(&s_policy, bank, numPlayingSounds,
                                         numSoundsInBank, soundId);
}

void stop_sound(u32 soundBits, f32 *pos)
{
    u16 token;
    if (!source_audio_ensure_initialized()) {
        return;
    }
    token = sm64_saturn_audio_spatial_find(&s_spatial, pos);
    if (token != 0U && sm64_saturn_audio_policy_stop_handle(
                           &s_policy, soundBits, token,
                           s_package_generation) &&
        !token_is_active(token)) {
        (void)sm64_saturn_audio_spatial_release(&s_spatial, pos);
    }
}

void stop_sounds_from_source(f32 *pos)
{
    u16 token;
    if (!source_audio_ensure_initialized()) {
        return;
    }
    token = sm64_saturn_audio_spatial_find(&s_spatial, pos);
    if (token != 0U) {
        (void)sm64_saturn_audio_policy_stop_source(
            &s_policy, token, s_package_generation);
        if (!token_is_active(token)) {
            (void)sm64_saturn_audio_spatial_release(&s_spatial, pos);
        }
    }
}

void stop_sounds_in_continuous_banks(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_stop_bank(&s_policy, SOUND_BANK_MOVING);
    (void)sm64_saturn_audio_policy_stop_bank(&s_policy, SOUND_BANK_ENV);
    (void)sm64_saturn_audio_policy_stop_bank(&s_policy, SOUND_BANK_AIR);
    release_inactive_sources();
}

void sound_banks_disable(UNUSED u8 player, u16 bankMask)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    sm64_saturn_audio_policy_disable_banks(&s_policy, bankMask);
}

void sound_banks_enable(UNUSED u8 player, u16 bankMask)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    sm64_saturn_audio_policy_enable_banks(&s_policy, bankMask);
}

void set_sound_moving_speed(u8 bank, u8 speed)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    if (bank < SM64_SATURN_AUDIO_BANK_COUNT) {
        s_moving_speed[bank] = speed;
    }
}

enum {
    DIALOG_SPEAKER_UKIKI,
    DIALOG_SPEAKER_TUXIE,
    DIALOG_SPEAKER_BOWSER_INTRO,
    DIALOG_SPEAKER_KOOPA,
    DIALOG_SPEAKER_KING_BOBOMB,
    DIALOG_SPEAKER_BOO,
    DIALOG_SPEAKER_BOBOMB_BUDDY,
    DIALOG_SPEAKER_BOWSER,
    DIALOG_SPEAKER_GRUNT,
    DIALOG_SPEAKER_WIGGLER,
    DIALOG_SPEAKER_YOSHI,
    DIALOG_SPEAKER_NONE = 0xFF,
};

#define N DIALOG_SPEAKER_NONE
static const u8 s_dialog_speaker[DIALOG_COUNT] = {
    N,6,6,6,6,3,3,3,N,3, N,N,N,N,N,N,N,4,N,N,
    N,2,2,2,2,2,2,2,2,2, N,N,N,N,N,N,N,1,N,N,
    N,3,N,N,N,N,N,6,N,N, N,N,N,N,N,1,1,1,1,1,
    N,N,N,N,N,N,N,7,N,N, N,N,N,N,N,N,N,N,N,0,
    0,N,N,N,N,5,N,N,N,N, 7,N,7,7,N,N,N,N,5,5,
    0,0,N,N,N,6,6,5,5,N, N,N,N,N,8,8,4,8,8,N,
    N,N,N,N,N,N,N,N,4,N, N,N,1,N,N,N,N,N,N,N,
    N,N,N,N,N,N,N,N,N,N, 9,9,9,N,N,N,N,N,N,N,
    N,10,N,N,N,N,N,N,9,N
};
#undef N

static const s32 s_dialog_voice[] = {
    SOUND_OBJ_UKIKI_CHATTER_LONG,
    SOUND_OBJ_BIG_PENGUIN_YELL,
    SOUND_OBJ_BOWSER_INTRO_LAUGH,
    SOUND_OBJ_KOOPA_TALK,
    SOUND_OBJ_KING_BOBOMB_TALK,
    SOUND_OBJ_BOO_LAUGH_LONG,
    SOUND_OBJ_BOBOMB_BUDDY_TALK,
    SOUND_OBJ_BOWSER_LAUGH,
    SOUND_OBJ2_BOSS_DIALOG_GRUNT,
    SOUND_OBJ_WIGGLER_TALK,
    SOUND_GENERAL_YOSHI_TALK,
};

void play_dialog_sound(u8 dialogID)
{
    u8 speaker;
    if (!source_audio_ensure_initialized()) {
        return;
    }
    if (dialogID >= DIALOG_COUNT) {
        dialogID = 0U;
    }
    speaker = s_dialog_speaker[dialogID];
    if (speaker != DIALOG_SPEAKER_NONE) {
        play_sound(s_dialog_voice[speaker], gGlobalSoundSource);
        if (speaker == DIALOG_SPEAKER_BOWSER_INTRO) {
            play_music(SEQ_PLAYER_ENV, SEQ_EVENT_KOOPA_MESSAGE, 0U);
        }
    }
    if (dialogID == DIALOG_010 || dialogID == DIALOG_011 ||
        dialogID == DIALOG_012) {
        play_puzzle_jingle();
    }
}

void play_music(u8 player, u16 seqArgs, u16 fadeTimer)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_play_music(&s_policy, player, seqArgs,
                                               fadeTimer);
}

void stop_background_music(u16 seqId)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_stop_background(&s_policy, seqId);
}

void fadeout_background_music(u16 seqId, u16 fadeOut)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_fadeout_background(&s_policy, seqId,
                                                       fadeOut);
}

void drop_queued_background_music(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    sm64_saturn_audio_policy_drop_queued(&s_policy);
}

u16 get_current_background_music(void)
{
    if (!source_audio_ensure_initialized()) {
        return 0U;
    }
    return sm64_saturn_audio_policy_current_background(&s_policy);
}

void play_secondary_music(u8 seqId, u8 bgMusicVolume, u8 volume,
                          u16 fadeTimer)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_play_secondary(
        &s_policy, seqId, bgMusicVolume, volume, fadeTimer);
}

void func_80321080(u16 fadeTimer)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_stop_secondary(&s_policy, fadeTimer);
}

void func_803210D4(u16 fadeOutTime)
{
    const u16 non_menu_banks =
        (u16)(0x03FFU & ~(u16)(1U << SOUND_BANK_MENU));
    if (!source_audio_ensure_initialized()) {
        return;
    }
    if (s_policy.global_fade_started) {
        return;
    }
    (void)sm64_saturn_audio_policy_fade_player(&s_policy, SEQ_PLAYER_LEVEL,
                                               0U, fadeOutTime);
    (void)sm64_saturn_audio_policy_fade_player(&s_policy, SEQ_PLAYER_ENV,
                                               0U, fadeOutTime);
    (void)sm64_saturn_audio_policy_fade_sfx_banks(
        &s_policy, non_menu_banks, 0U, (u16)(fadeOutTime / 16U));
    s_policy.global_fade_started = true;
}

void play_course_clear(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_play_jingle(
        &s_policy, SEQ_EVENT_CUTSCENE_COLLECT_STAR, 0U);
}

void play_peachs_jingle(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_play_jingle(
        &s_policy, SEQ_EVENT_PEACH_MESSAGE, 0U);
}

void play_puzzle_jingle(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_play_jingle(
        &s_policy, SEQ_EVENT_SOLVE_PUZZLE, 20U);
}

void play_star_fanfare(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_play_jingle(
        &s_policy, SEQ_EVENT_HIGH_SCORE, 20U);
}

void play_power_star_jingle(u8 arg0)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    if (arg0 == 0U) {
        s_policy.background_target_volume = 0U;
    }
    (void)sm64_saturn_audio_policy_play_jingle(
        &s_policy, SEQ_EVENT_CUTSCENE_STAR_SPAWN, 20U);
}

void play_race_fanfare(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_play_jingle(
        &s_policy, SEQ_EVENT_RACE, 20U);
}

void play_toads_jingle(void)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_play_jingle(
        &s_policy, SEQ_EVENT_TOAD_MESSAGE, 20U);
}

void sound_reset(u8 presetId)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_reset(&s_policy, presetId < 8U ? presetId
                                                                 : 0U);
    sm64_saturn_audio_spatial_init(&s_spatial);
}

void audio_set_sound_mode(u8 mode)
{
    if (!source_audio_ensure_initialized()) {
        return;
    }
    (void)sm64_saturn_audio_policy_set_sound_mode(&s_policy, mode);
}

void audio_init(void)
{
    (void)source_audio_initialize();
}

#if defined(VERSION_EU) || defined(VERSION_SH)
struct SPTask *unused_80321460(void)
{
    return NULL;
}
#endif
