/* Direct Saturn source-port entry for the original Bob-omb Battlefield script.
 *
 * This is intentionally only the platform's boot selection.  It does not
 * describe a scene, spawn Mario manually, or implement movement/camera logic:
 * `level_bob_entry` remains the inherited source script and reaches the
 * original `lvl_init_or_update` CALL/CALL_LOOP path.
 */
#include <ultra64.h>

#include "level_commands.h"
#include "level_table.h"
#include "game/level_update.h"

#include "levels/bob/header.h"

const LevelScript level_script_entry[] = {
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
    CALL(/* arg */ 0, /* func */ lvl_init_from_save_file),
    CALL(/* arg */ 0, /* func */ lvl_set_current_level),
    EXECUTE(/* seg */ 0x0E, /* script */ NULL, /* scriptEnd */ NULL,
            /* entry */ level_bob_entry),
    JUMP(/* target */ level_script_entry),
};
