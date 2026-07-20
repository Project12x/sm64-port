/* The direct-Bob E2 bootstrap never selects a title-screen demo.  Preserve
 * the source DmaHandlerList ABI with an empty table until the normal menu
 * package is part of the Saturn level-script closure. */
#include "game/memory.h"

struct SaturnSourceDemoInputs {
    u32 numEntries;
    const void *addrPlaceholder;
    struct OffsetSizePair entries[1];
};

const struct SaturnSourceDemoInputs gDemoInputs = {
    0U,
    NULL,
    { { 0U, 0U } },
};
