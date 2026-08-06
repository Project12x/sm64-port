#include "saturn_geo_walk_storage.h"

#if UINTPTR_MAX == 0xFFFFFFFFU
_Static_assert(sizeof(sm64_saturn_geo_walk_frame_t) ==
                   SM64_SATURN_GEO_TRAVERSAL_FRAME_BYTES,
               "geo-walk frame ABI differs from generated SH-2 manifest");
#endif

sm64_saturn_geo_walk_frame_t sourceboot_geo_walk_frames
    [SM64_SATURN_GEO_TRAVERSAL_CAPACITY]
    __attribute__((section(".lwram_geo_traversal"), aligned(16), used));

const uint16_t sourceboot_geo_walk_frame_capacity =
    (uint16_t)SM64_SATURN_GEO_TRAVERSAL_CAPACITY;
