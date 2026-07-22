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
    /* The retail entry reaches a menu before selecting a level.  E2 needs a
     * bounded source-gameplay bootstrap, so select Bob here through the same
     * source level-state function before executing its unmodified script. */
    SET_REG(/* value */ LEVEL_BOB),
    CALL(/* arg */ 0, /* func */ lvl_set_current_level),
    /* The retail master script (levels/scripts.c's level_main_scripts_entry)
     * calls lvl_init_from_save_file() exactly once, immediately after its
     * initial model loading and before any level-specific script ever runs
     * -- unconditionally, using whatever gCurrSaveFileNum currently holds
     * (its own static initializer defaults it to 1, src/game/area.c:52;
     * the real menu/file-select flow that would normally choose a file has
     * not run yet at this same point in the retail sequence either, so this
     * matches upstream timing exactly, not a special case invented for E2).
     * This entry never reaches that call at all, because it never executes
     * level_main_scripts_entry -- E2 goes straight from the platform's own
     * boot into level_bob_entry. Without it, gMarioState->animList (and
     * numCoins/numStars/numKeys/numLives/health/spawnInfo/statusForCamera/
     * marioBodyState/controller, all set by init_mario_from_save_file())
     * stay at their cold-boot zero/NULL BSS values. That is latent until
     * Mario's own animation system is first exercised -- confirmed live
     * (2026-07-22 crash-root-cause session): the intro cutscene's
     * "jump out of pipe and land" step calls set_mario_animation() for the
     * first time, which calls load_patchable_table() on the NULL animList,
     * reads a garbage struct DmaTable* from address 0, and feeds a garbage
     * ~1.2 GB size into a memcpy that runs the SH-2 into unmapped memory
     * and crashes it permanently (confirmed via live register tracing:
     * PC=0x2, all interrupts masked, fully reproducible).
     *
     * Called via the same level-script CALL mechanism (not a raw C call)
     * so the interpreter supplies levelNum exactly as it does for
     * lvl_set_current_level above (CALL passes the interpreter's sRegister,
     * still LEVEL_BOB from the SET_REG above, as lvl_init_from_save_file's
     * second parameter -- src/engine/level_script.c:239) -- this is the
     * literal, unmodified upstream call, not a reimplementation.
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
    CALL(/* arg */ 0, /* func */ lvl_init_from_save_file),
    EXECUTE(/* seg */ 0x0E, /* script */ NULL, /* scriptEnd */ NULL,
            /* entry */ level_bob_entry),
    JUMP(/* target */ level_script_entry),
};
