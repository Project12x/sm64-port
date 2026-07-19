#include <stddef.h>
#include <stdint.h>

#include "sm64.h"
#include "game/memory.h"
#include "game/mario.h"
#include "game/object_list_processor.h"

/* The original surface loader allocates its pools through main_pool_alloc.
 * Keep that allocator boundary, but provide a fixed Saturn WRAM arena sized
 * for Castle Area 1's source collision (7,000 nodes + 2,300 surfaces). */
static uint8_t sCastleCollisionPool[256 * 1024] __attribute__((aligned(8)));
static size_t sCastleCollisionPoolUsed;

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
    const size_t aligned = (sCastleCollisionPoolUsed + 7U) & ~((size_t)7U);
    if (aligned + size > sizeof(sCastleCollisionPool)) {
        return NULL;
    }
    sCastleCollisionPoolUsed = aligned + size;
    return &sCastleCollisionPool[aligned];
}

void reset_red_coins_collected(void) {
}

void spawn_special_objects(s16 areaIndex, s16 **specialObjList) {
    (void)areaIndex;
    (void)specialObjList;
}
