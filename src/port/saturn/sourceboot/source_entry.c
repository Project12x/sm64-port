/* Direct Saturn source-port entry for the original Bob-omb Battlefield script.
 *
 * This is intentionally only the platform's boot selection.  It does not
 * describe a scene, spawn Mario manually, or implement movement/camera logic:
 * `level_bob_entry` remains the inherited source script and reaches the
 * original `lvl_init_or_update` CALL/CALL_LOOP path.
 */
#include <ultra64.h>

#include "macros.h"
#include "level_commands.h"
#include "level_table.h"
#include "game/level_update.h"
#include "game/save_file.h"

#include "levels/bob/header.h"

/* Model geo/DL symbols for the registration block below. Retail declares
 * these in levels/scripts.c, which this target never executes -- see the
 * block comment on the registration sequence. group0 carries mario_geo;
 * common0/common1 carry the effect, coin, star, and cap models. */
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

const LevelScript level_script_entry[] = {
    /* MODEL REGISTRATION -- restores the stage retail performs in
     * level_main_scripts_entry (levels/scripts.c:67-115) before any level
     * script runs. This target never executes that script (see the
     * lvl_init_from_save_file comment below, which documents the same
     * gap for a different consequence), so gLoadedGraphNodes stayed
     * entirely empty: every MARIO()/OBJECT() command read a NULL model
     * pointer into spawnInfo->unk18, which became a NULL sharedChild, and
     * geo_process_object (src/game/rendering_graph_node.c:1127) skipped
     * the whole subtree. Mario has never had geometry submitted.
     *
     * Placed BEFORE INIT_LEVEL(), not after: INIT_LEVEL()
     * (level_cmd_init_level, src/engine/level_script.c:331-338) calls
     * main_pool_push_state() (level_script.c:336), which pushes a
     * save-point onto the main pool's allocation stack. Nothing in this
     * script ever calls CLEAR_LEVEL() to pop that frame back off -- the
     * trailing JUMP(level_script_entry) below re-runs this whole script
     * (and therefore INIT_LEVEL() again) on every return from
     * EXECUTE(level_bob_entry), i.e. on every ordinary level-exit/warp
     * cycle through level_bob_entry's own CLEAR_LEVEL()/EXIT() pair, with
     * no CLEAR_LEVEL() of this script's own frame ever happening in
     * between. Anything allocated from the main pool after INIT_LEVEL()
     * would therefore live inside a stack frame that becomes permanently
     * unreachable the next time this script runs, leaking main-pool bytes
     * on every such cycle.
     *
     * This reorder closes the leak for the FIRST pass through this array
     * only -- the boot-time registration this task exists to prove out.
     * It does NOT close it for the second and later level-exit/reentry
     * cycles: JUMP(level_script_entry) restarts this entire array from
     * the top, so on cycle 2 the registration block above runs again
     * while cycle 1's own INIT_LEVEL() push is STILL on the stack (it was
     * never popped -- see above), and the re-registration lands inside
     * that same unpopped frame regardless of its position relative to
     * THIS cycle's INIT_LEVEL() call. Retail avoids this because its
     * registration prologue (levels/scripts.c:67-115) is structurally
     * outside the loop that revisits level scripts -- a separate
     * LOOP_BEGIN()/JUMP_LINK(script_exec_level_table) construct further
     * down the same array never re-enters the registration section.
     * Matching retail's command ORDER inside a self-looping array is not
     * the same as matching retail's script TOPOLOGY; closing this fully
     * needs the registration hoisted into a true one-time prologue ahead
     * of a separate looping body, which this task does not attempt --
     * verified live (2026-07-25) that this deviation is real, not
     * theoretical, by tracing the actual push/pop call sequence across
     * one full level_bob_entry EXECUTE/CLEAR_LEVEL/EXIT round-trip.
     *
     * Not fixed here because: (a) this plan's own test methodology is a
     * single continuous boot with no level-exit/reentry, so the residual
     * leak is never exercised by anything this plan measures or ships;
     * (b) lvl_init_or_update (src/game/level_update.c:1234-1247) is real,
     * unmodified retail transition logic, not stubbed out, so reachability
     * once warp/star mechanics work end-to-end on this target cannot be
     * ruled out; and (c) restructuring this script's topology is a larger,
     * separate change with its own risk to the boot sequence every task
     * in this plan depends on. Tracked as follow-up work, not silently
     * accepted.
     *
     * FREE_LEVEL_POOL is shrink-to-fit, not destroy
     * (src/engine/level_script.c:363-369 resizes the pool to usedSpace),
     * so these registrations survive into BOB's own ALLOC_LEVEL_POOL at
     * levels/bob/script.c:67 -- exactly as they do in retail.
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
    FREE_LEVEL_POOL(),
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
     * camera_course_processing's real AREA_BOB case (camera.c:6630).
     *
     * Verified safe before wiring this in: every save-file accessor
     * init_mario_from_save_file()/lvl_init_from_save_file() touch
     * (save_file_exists, save_file_get_total_star_count,
     * save_file_get_cap_pos, save_file_move_cap_to_default_location) only
     * reads/writes gSaveBuffer (src/buffers/buffers.c:34, a plain
     * zero-initialized static struct, no pointer indirection) and never
     * touches the EEPROM read/write path (gated separately behind
     * gEepromProbe, which this call chain never reaches) -- so this is
     * safe on a target with no real save-file hardware. The remaining
     * side effects (disable_warp_checkpoint, select_mario_cam_mode,
     * set_yoshi_as_not_dead) are all single static-flag writes. */
    SET_REG(/* value */ LEVEL_BOB),
    /* Before lvl_init_from_save_file so its gNeverEnteredCastle read
     * (!save_file_exists) already sees the marked file -- see the adapter's
     * comment above for the full intro-skip rationale. */
    CALL(/* arg */ 0, /* func */ sourceboot_mark_save_file_exists),
    CALL(/* arg */ 0, /* func */ lvl_init_from_save_file),
    CALL(/* arg */ 0, /* func */ lvl_set_current_level),
    EXECUTE(/* seg */ 0x0E, /* script */ NULL, /* scriptEnd */ NULL,
            /* entry */ level_bob_entry),
    JUMP(/* target */ level_script_entry),
};
