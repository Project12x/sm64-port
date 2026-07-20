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
    EXECUTE(/* seg */ 0x0E, /* script */ NULL, /* scriptEnd */ NULL,
            /* entry */ level_bob_entry),
    JUMP(/* target */ level_script_entry),
};
