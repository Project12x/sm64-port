/* E2 audio transport placeholder.
 *
 * The inherited game and level code keep making their normal audio calls;
 * these target services deliberately have no SCSP side effect yet.  This is a
 * platform boundary, not an alternate game/audio policy.  SCSP transport will
 * replace these functions without changing the source call sites.
 */
#include <ultra64.h>

#include "macros.h"
#include "audio/external.h"

s32 gAudioErrorFlags;
f32 gGlobalSoundSource[3];
u32 gAudioRandom;

struct SPTask *create_next_audio_frame_task(void) { return NULL; }
void play_sound(UNUSED s32 soundBits, UNUSED f32 *pos) {}
void audio_signal_game_loop_tick(void) {}
void seq_player_fade_out(UNUSED u8 player, UNUSED u16 fadeDuration) {}
void fade_volume_scale(UNUSED u8 player, UNUSED u8 targetScale, UNUSED u16 fadeDuration) {}
void seq_player_lower_volume(UNUSED u8 player, UNUSED u16 fadeDuration, UNUSED u8 percentage) {}
void seq_player_unlower_volume(UNUSED u8 player, UNUSED u16 fadeDuration) {}
void set_audio_muted(UNUSED u8 muted) {}
void sound_init(void) {}
void get_currently_playing_sound(UNUSED u8 bank, u8 *numPlayingSounds,
                                 u8 *numSoundsInBank, u8 *soundId) {
    if (numPlayingSounds != NULL) *numPlayingSounds = 0;
    if (numSoundsInBank != NULL) *numSoundsInBank = 0;
    if (soundId != NULL) *soundId = 0;
}
void stop_sound(UNUSED u32 soundBits, UNUSED f32 *pos) {}
void stop_sounds_from_source(UNUSED f32 *pos) {}
void stop_sounds_in_continuous_banks(void) {}
void sound_banks_disable(UNUSED u8 player, UNUSED u16 bankMask) {}
void sound_banks_enable(UNUSED u8 player, UNUSED u16 bankMask) {}
void set_sound_moving_speed(UNUSED u8 bank, UNUSED u8 speed) {}
void play_dialog_sound(UNUSED u8 dialogID) {}
void play_music(UNUSED u8 player, UNUSED u16 seqArgs, UNUSED u16 fadeTimer) {}
void stop_background_music(UNUSED u16 seqId) {}
void fadeout_background_music(UNUSED u16 arg0, UNUSED u16 fadeOut) {}
void drop_queued_background_music(void) {}
u16 get_current_background_music(void) { return 0; }
void play_secondary_music(UNUSED u8 seqId, UNUSED u8 bgMusicVolume,
                          UNUSED u8 volume, UNUSED u16 fadeTimer) {}
void func_80321080(UNUSED u16 fadeTimer) {}
void func_803210D4(UNUSED u16 fadeOutTime) {}
void play_course_clear(void) {}
void play_peachs_jingle(void) {}
void play_puzzle_jingle(void) {}
void play_star_fanfare(void) {}
void play_power_star_jingle(UNUSED u8 arg0) {}
void play_race_fanfare(void) {}
void play_toads_jingle(void) {}
void sound_reset(UNUSED u8 presetId) {}
void audio_set_sound_mode(UNUSED u8 arg0) {}
void audio_init(void) {}

#if defined(VERSION_EU) || defined(VERSION_SH)
struct SPTask *unused_80321460(void) { return NULL; }
#endif
