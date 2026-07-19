#include <stddef.h>
#include <stdint.h>
#include <scu/map.h>

#include "sm64.h"
#include "game/memory.h"
#include "game/mario.h"
#include "game/object_list_processor.h"
#include "saturn_memory_arena.h"

/* The original surface loader allocates its pools through main_pool_alloc.
 * Keep that boundary, but put the cold 256 KiB Castle surface arena in the
 * upper quarter of Saturn low WRAM. The prior HWRAM array consumed a quarter
 * of the 1 MiB hot/code memory and made a ~7 KiB transform cache cross the
 * runtime boundary. Collision queries are sparse compared with rendering. */
#define CASTLE_COLLISION_POOL_SIZE (256U * 1024U)
#define CASTLE_COLLISION_POOL ((uint8_t *)LWRAM(0x000C0000U))
static sm64_saturn_memory_arena_t sCollisionArena;

s32 gSurfaceNodesAllocated;
s32 gSurfacesAllocated;
s32 gNumStaticSurfaceNodes;
s32 gNumStaticSurfaces;
s16 *gEnvironmentRegions;
s32 gEnvironmentLevels[20];
s16 gCCMEnteredSlide;
s16 gCheckingSurfaceCollisionsForCamera;
s16 gFindFloorIncludeSurfaceIntangible;
s32 gNumFindFloorMisses;
struct NumTimesCalled gNumCalls;
struct Object *gCurrentObject;
struct Object *gMarioObject;
struct MarioState *gMarioState;
u32 gTimeStopState;

void *main_pool_alloc(u32 size, u32 side) {
    (void)side;
    if (sCollisionArena.base == NULL) {
        sm64_saturn_memory_arena_init(&sCollisionArena,
                                      CASTLE_COLLISION_POOL,
                                      CASTLE_COLLISION_POOL_SIZE);
    }
    return sm64_saturn_memory_arena_alloc(&sCollisionArena, size, 8U);
}

void reset_red_coins_collected(void) {
}

void spawn_special_objects(s16 areaIndex, s16 **specialObjList) {
    (void)areaIndex;
    (void)specialObjList;
}

/* Area 1's first Saturn slice loads the original static collision bank only;
 * macro/special object records are deliberately not instantiated yet.  Keep
 * the source loader call intact and satisfy its optional object hooks with
 * no-op boundaries until the object behavior bank is ported. */
void spawn_macro_objects(s16 areaIndex, s16 *macroObjList) {
    (void)areaIndex;
    (void)macroObjList;
}

void spawn_macro_objects_hardcoded(s16 areaIndex, s16 *macroObjList) {
    (void)areaIndex;
    (void)macroObjList;
}
