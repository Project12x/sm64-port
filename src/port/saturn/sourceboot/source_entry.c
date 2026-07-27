/* Direct Saturn source-port entry for the original Bob-omb Battlefield script.
 *
 * This is intentionally only the platform's boot selection.  It does not
 * describe a scene, spawn Mario manually, or implement movement/camera logic:
 * `level_bob_entry` remains the inherited source script and reaches the
 * original `lvl_init_or_update` CALL/CALL_LOOP path.
 */
#include <ultra64.h>

#include "macros.h"
#include "sm64.h"
#include "level_commands.h"
#include "level_table.h"
#include "game/level_update.h"
#include "game/save_file.h"

#include "levels/bob/header.h"

/* Model geo/DL symbols for the registration block below. Retail declares
 * these in levels/scripts.c, which this target never executes -- see the
 * block comment on the registration sequence. group0 carries mario_geo;
 * common0/common1 carry the effect, coin, star, and cap models. sm64.h
 * supplies LAYER_ALPHA for the display-list model entries below (retail
 * pulls it in the same way, levels/scripts.c:2). */
#include "actors/common0.h"
#include "actors/common1.h"
#include "actors/group0.h"
#include "model_ids.h"

/* Retail only ever stages the Peach-letter intro cutscene in castle
 * grounds; its own skip mechanism is the save file: init_level
 * (src/game/level_update.c:1198) spawns Mario in ACT_IDLE when
 * save_file_exists(gCurrSaveFileNum - 1) is true and only falls back to
 * ACT_INTRO_CUTSCENE for a never-saved file. A no-save boot straight
 * into BOB stages that cutscene against the wrong world (castle pipe
 * model/coordinates) and then blocks on its letter dialog
 * (live-confirmed 2026-07-22: c->cutscene=0x8e, gDialogID=DIALOG_020,
 * cameraEvent pinned at CAM_EVENT_START_INTRO). Mark file 1 as existing
 * through the unmodified upstream setter -- save_file_set_flags
 * (src/game/save_file.c:466) unconditionally ORs SAVE_FLAG_FILE_EXISTS
 * into gSaveBuffer, a plain zero-initialized static struct; the EEPROM
 * write path stays untouched (gated behind gEepromProbe elsewhere).
 *
 * This adapter exists because the level-script CALL mechanism invokes
 * targets as s32 (*)(s16, s32) and stores the return value back into the
 * interpreter register (src/engine/level_script.c:239); calling the
 * void(u32) setter directly through that pointer type would be a
 * function-pointer-type mismatch. Returning `value` unchanged preserves
 * the register mid-chain, the same contract lvl_init_from_save_file
 * honors (level_update.c:1283). */
static s32 sourceboot_mark_save_file_exists(UNUSED s16 arg, s32 value) {
    save_file_set_flags(SAVE_FLAG_FILE_EXISTS);
    return value;
}

/* Two-array split, mirroring retail's real topology (levels/scripts.c):
 * `level_script_entry` is a one-time prologue that runs the model
 * registration exactly once, ever, then hands off permanently to
 * `sSourcebootLevelLoop` below. `sSourcebootLevelLoop` is the looping body:
 * it is the only thing JUMP()ed back into on a level-exit/reentry cycle, and
 * it never re-enters `level_script_entry`. This forward declaration lets
 * `level_script_entry`'s JUMP() below reference the loop array before its
 * definition, the same forward-declare-then-define pattern retail's own
 * levels/scripts.c uses for script_L1/script_L2/goto_mario_head_regular/
 * goto_mario_head_dizzy/script_L5. */
static const LevelScript sSourcebootLevelLoop[];

const LevelScript level_script_entry[] = {
    /* ONE-TIME MODEL REGISTRATION -- mirrors the stage retail performs in
     * level_main_scripts_entry (levels/scripts.c:67-115) before any level
     * script runs, adapted for this target's simplified boot chain (a single
     * hardcoded level, not a real level table). This target never executes
     * level_main_scripts_entry, so without this block gLoadedGraphNodes
     * stayed entirely empty: every MARIO()/OBJECT() command read a NULL
     * model pointer into spawnInfo->unk18, which became a NULL sharedChild,
     * and geo_process_object (src/game/rendering_graph_node.c:1127) skipped
     * the whole subtree. Mario would have no geometry submitted.
     *
     * This array runs from the top exactly once, for the life of the
     * program: nothing anywhere JUMPs back into `level_script_entry`. Fixed
     * 2026-07-25 (was previously a single self-looping array that combined
     * this registration block with INIT_LEVEL() and a trailing
     * JUMP(level_script_entry) back to its own top): every level-exit/
     * reentry cycle used to re-run this entire registration block, each pass
     * permanently consuming another full copy of it (measured at ~39 KiB per
     * cycle against ~124 KiB of headroom after one pass -- exhausted in
     * roughly three cycles), on top of a separate, compounding leak in the
     * main pool's push/pop stack (see sSourcebootLevelLoop's own comment
     * below for that half). Splitting the registration into its own
     * never-revisited array closes this half structurally: there is no path
     * back into this array at all, at any point, so it cannot re-run
     * regardless of how many level-exit/reentry cycles occur.
     *
     * FREE_LEVEL_POOL is shrink-to-fit, not destroy
     * (src/engine/level_script.c:363-369 resizes the pool to usedSpace), so
     * these registrations survive into BOB's own ALLOC_LEVEL_POOL at
     * levels/bob/script.c:67 -- exactly as they do in retail, and now
     * exactly once per boot, exactly as retail's own registration block only
     * ever runs once.
     *
     * No LOAD_MIO0/LOAD_RAW segment commands are needed or wanted: this
     * build defines NO_SEGMENTED_MEMORY, under which segmented_to_virtual
     * is the identity function (src/game/memory.c:138-140) and
     * level_cmd_load_model_from_geo passes the geo pointer straight
     * through (src/engine/level_script.c:427). The segment commands would
     * call load_segment_decompress -> dma_read against N64 ROM addresses
     * that do not exist on this target. */
    ALLOC_LEVEL_POOL(),
    LOAD_MODEL_FROM_GEO(MODEL_MARIO,                   mario_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SMOKE,                   smoke_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SPARKLES,                sparkles_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BUBBLE,                  bubble_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SMALL_WATER_SPLASH,      small_water_splash_geo),
    LOAD_MODEL_FROM_GEO(MODEL_IDLE_WATER_WAVE,         idle_water_wave_geo),
    LOAD_MODEL_FROM_GEO(MODEL_WATER_SPLASH,            water_splash_geo),
    LOAD_MODEL_FROM_GEO(MODEL_WAVE_TRAIL,              wave_trail_geo),
    LOAD_MODEL_FROM_GEO(MODEL_YELLOW_COIN,             yellow_coin_geo),
    LOAD_MODEL_FROM_GEO(MODEL_STAR,                    star_geo),
    LOAD_MODEL_FROM_GEO(MODEL_TRANSPARENT_STAR,        transparent_star_geo),
    LOAD_MODEL_FROM_GEO(MODEL_WOODEN_SIGNPOST,         wooden_signpost_geo),
    LOAD_MODEL_FROM_DL( MODEL_WHITE_PARTICLE_SMALL,    white_particle_small_dl,     LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO(MODEL_RED_FLAME,               red_flame_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BLUE_FLAME,              blue_flame_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BURN_SMOKE,              burn_smoke_geo),
    LOAD_MODEL_FROM_GEO(MODEL_LEAVES,                  leaves_geo),
    LOAD_MODEL_FROM_GEO(MODEL_PURPLE_MARBLE,           purple_marble_geo),
    LOAD_MODEL_FROM_GEO(MODEL_FISH,                    fish_geo),
    LOAD_MODEL_FROM_GEO(MODEL_FISH_SHADOW,             fish_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_SPARKLES_ANIMATION,      sparkles_animation_geo),
    LOAD_MODEL_FROM_DL( MODEL_SAND_DUST,               sand_seg3_dl_0302BCD0,       LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO(MODEL_BUTTERFLY,               butterfly_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BURN_SMOKE_UNUSED,       burn_smoke_geo),
    LOAD_MODEL_FROM_DL( MODEL_PEBBLE,                  pebble_seg3_dl_0301CB00,     LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO(MODEL_MIST,                    mist_geo),
    LOAD_MODEL_FROM_GEO(MODEL_WHITE_PUFF,              white_puff_geo),
    LOAD_MODEL_FROM_DL( MODEL_WHITE_PARTICLE_DL,       white_particle_dl,           LAYER_ALPHA),
    LOAD_MODEL_FROM_GEO(MODEL_WHITE_PARTICLE,          white_particle_geo),
    LOAD_MODEL_FROM_GEO(MODEL_YELLOW_COIN_NO_SHADOW,   yellow_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BLUE_COIN,               blue_coin_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BLUE_COIN_NO_SHADOW,     blue_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_WINGED_METAL_CAP, marios_winged_metal_cap_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_METAL_CAP,        marios_metal_cap_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_WING_CAP,         marios_wing_cap_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_CAP,              marios_cap_geo),
    LOAD_MODEL_FROM_GEO(MODEL_MARIOS_CAP,              marios_cap_geo), // repeated upstream
    LOAD_MODEL_FROM_GEO(MODEL_BOWSER_KEY_CUTSCENE,     bowser_key_cutscene_geo),
    LOAD_MODEL_FROM_GEO(MODEL_BOWSER_KEY,              bowser_key_geo),
    LOAD_MODEL_FROM_GEO(MODEL_RED_FLAME_SHADOW,        red_flame_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_1UP,                     mushroom_1up_geo),
    LOAD_MODEL_FROM_GEO(MODEL_RED_COIN,                red_coin_geo),
    LOAD_MODEL_FROM_GEO(MODEL_RED_COIN_NO_SHADOW,      red_coin_no_shadow_geo),
    LOAD_MODEL_FROM_GEO(MODEL_NUMBER,                  number_geo),
    LOAD_MODEL_FROM_GEO(MODEL_EXPLOSION,               explosion_geo),
    LOAD_MODEL_FROM_GEO(MODEL_DIRT_ANIMATION,          dirt_animation_geo),
    LOAD_MODEL_FROM_GEO(MODEL_CARTOON_STAR,            cartoon_star_geo),
    FREE_LEVEL_POOL(),
    /* Hand off to the looping body. This deliberately targets
     * sSourcebootLevelLoop, never level_script_entry itself -- see that
     * array's own comment for why that distinction is the fix. */
    JUMP(/* target */ sSourcebootLevelLoop),
};

static const LevelScript sSourcebootLevelLoop[] = {
    /* LOOPING BODY -- everything that repeats on a level-exit/reentry cycle.
     * Fixed 2026-07-25: this array (and CLEAR_LEVEL() specifically) did not
     * exist before; the single combined level_script_entry array called
     * INIT_LEVEL() but never CLEAR_LEVEL()d it, so main_pool_push_state()'s
     * save-point (src/game/memory.c:218) was pushed again on every
     * JUMP(level_script_entry) loop-back with nothing ever popping it. The
     * CLEAR_LEVEL() near the bottom of this array is the missing
     * counterpart: it pops exactly the frame this array's own INIT_LEVEL()
     * pushed, immediately before the JUMP back to this array's own top -- so
     * main-pool depth returns to the same level at the start of every pass,
     * indefinitely, instead of growing by one frame per cycle.
     *
     * Nesting during one full pass (outermost to innermost, traced against
     * the actual push/pop call sites -- level_cmd_init_level/
     * level_cmd_clear_level, src/engine/level_script.c:332-348, and
     * level_cmd_load_and_execute/level_cmd_exit, level_script.c:95-104 and
     * :119-125):
     *   INIT_LEVEL() (this array)   -> push frame A
     *   EXECUTE(level_bob_entry)    -> push frame B (level_cmd_load_and_execute
     *                                   pushes before jumping into bob's script)
     *     levels/bob/script.c's own INIT_LEVEL()/ALLOC_LEVEL_POOL()/
     *     FREE_LEVEL_POOL()/CLEAR_LEVEL() push and pop their own frame C,
     *     entirely inside frame B, independent of this array's frames
     *   levels/bob/script.c's EXIT() -> pops frame B, returns control here
     *   CLEAR_LEVEL() (this array)  -> pops frame A
     *   JUMP(sSourcebootLevelLoop)  -> repeat from the top; net main-pool
     *                                   depth unchanged from this pass's start
     *
     * Verified by code tracing only, not by a live second-cycle capture:
     * this project's test methodology to date is a single continuous boot
     * with no level exit
     * (docs/saturn/evidence/reports/e2-sourceboot-mario-freeroam-2026-07-25.json
     * is the established baseline), so EXECUTE(level_bob_entry) has never
     * actually returned control to this array in a live capture --
     * warp/star mechanics may not be wired up on this target yet. But
     * lvl_init_or_update (src/game/level_update.c:1234-1247) is real,
     * unmodified retail transition logic, not stubbed out, so a second pass
     * through this loop remains a reachable path once those mechanics work
     * end-to-end; this fix is what makes that path safe when it becomes
     * reachable, not just theoretically closed. */
    INIT_LEVEL(),
    /* Act number, first: retail's star-select screen writes gCurrActNum
     * through this same script mechanism before a course loads; E2 boots
     * straight into the level, so nothing ever set it and it stayed at its
     * BSS zero. That is not benign: level_cmd_place_object
     * (src/engine/level_script.c:470) computes the act mask as
     * `1 << (gCurrActNum - 1)`, which with gCurrActNum == 0 is `1 << -1`
     * -- undefined behavior that yields 0 on this SH-2 build -- so every
     * OBJECT_WITH_ACTS entry whose act mask is not the special all-acts
     * 0x1F was silently skipped (King Bob-omb, the act-1 Bob-omb buddies,
     * and the rest of BOB's act-gated population never spawned). Measured
     * live before this fix: gObjectCounter = 0x50 (80) objects/frame.
     * GET_OR_SET(OP_SET, VAR_CURR_ACT_NUM) is the interpreter's own
     * gCurrActNum store (level_script.c:760); act 1 is star 1, the same
     * default a fresh file's star select would offer. Placed before the
     * SET_REG(LEVEL_BOB) sequence below because GET_OR_SET consumes
     * sRegister. */
    SET_REG(/* value */ 1),
    GET_OR_SET(/* op */ OP_SET, /* var */ VAR_CURR_ACT_NUM),
    /* The retail entry reaches a menu before selecting a level.  E2 needs a
     * bounded source-gameplay bootstrap, so select Bob here through the same
     * source level-state functions before executing its unmodified script.
     *
     * The retail master script (levels/scripts.c's level_main_scripts_entry)
     * calls lvl_init_from_save_file() exactly once, before any level is
     * ever selected -- unconditionally, using whatever gCurrSaveFileNum
     * currently holds (its own static initializer defaults it to 1,
     * src/game/area.c:52; the real menu/file-select flow that would
     * normally choose a file has not run yet at that same point in the
     * retail sequence either, so this matches upstream timing exactly, not
     * a special case invented for E2). This entry never reaches that call
     * naturally, because it never executes level_main_scripts_entry -- E2
     * goes straight from the platform's own boot into level_bob_entry.
     * Without it, gMarioState->animList (and numCoins/numStars/numKeys/
     * numLives/health/spawnInfo/statusForCamera/marioBodyState/controller,
     * all set by init_mario_from_save_file()) stay at their cold-boot
     * zero/NULL BSS values. That is latent until Mario's own animation
     * system is first exercised -- confirmed live (2026-07-22
     * crash-root-cause session): the intro cutscene's "jump out of pipe
     * and land" step calls set_mario_animation() for the first time, which
     * calls load_patchable_table() on the NULL animList, reads a garbage
     * struct DmaTable* from address 0, and feeds a garbage ~1.2 GB size
     * into a memcpy that runs the SH-2 into unmapped memory and crashes it
     * permanently (confirmed via live register tracing: PC=0x2, all
     * interrupts masked, fully reproducible).
     *
     * CALL ORDER IS LOAD-BEARING (corrected 2026-07-22, second pass, after
     * the systematic init-gap audit): lvl_init_from_save_file must run
     * BEFORE lvl_set_current_level, matching retail's global order (the
     * master script calls it before any per-level SET_REG/
     * lvl_set_current_level stub ever runs). Both functions are chained
     * through the interpreter's register: CALL passes sRegister as the
     * second parameter and stores the return value back into sRegister
     * (src/engine/level_script.c:239). lvl_init_from_save_file returns its
     * levelNum argument unchanged (src/game/level_update.c:1283), so
     * SET_REG(LEVEL_BOB) survives through it into lvl_set_current_level.
     * The first version of this fix had the two calls REVERSED, which fed
     * lvl_set_current_level's boolean return (1 for BOB's course) into
     * lvl_init_from_save_file's levelNum -- silently re-setting
     * gCurrLevelNum to 1 (LEVEL_UNKNOWN_1) for the whole session
     * (gCurrLevelArea read 0x11 live instead of BOB's 0x91), which
     * disabled every gCurrLevelArea-keyed camera behavior including
     * camera_course_processing's real AREA_BOB case (camera.c:6630). */
    SET_REG(/* value */ LEVEL_BOB),
    /* Before lvl_init_from_save_file so its gNeverEnteredCastle read
     * (!save_file_exists) already sees the marked file -- see the adapter's
     * comment above for the full intro-skip rationale. */
    CALL(/* arg */ 0, /* func */ sourceboot_mark_save_file_exists),
    CALL(/* arg */ 0, /* func */ lvl_init_from_save_file),
    CALL(/* arg */ 0, /* func */ lvl_set_current_level),
    EXECUTE(/* seg */ 0x0E, /* script */ NULL, /* scriptEnd */ NULL,
            /* entry */ level_bob_entry),
    /* Balances this pass's own INIT_LEVEL() above -- see this array's
     * top-of-block comment for the full push/pop nesting trace. */
    CLEAR_LEVEL(),
    JUMP(/* target */ sSourcebootLevelLoop),
};
