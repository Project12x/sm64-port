#include <ultra64.h>
#include "gfx_output_buffer.h"

#ifdef TARGET_SATURN
/* Saturn consumes the original Fast3D command list directly.  The N64 RSP
 * output buffer is not a render queue and must not reserve 127 KiB of the
 * fixed sourceboot work-RAM image.  The symbol stays present for shared
 * source declarations; TARGET_SATURN tasks deliberately leave it unused. */
u64 gGfxSPTaskOutputBuffer[1];
#elif defined(VERSION_EU)
// 0x17e00 bytes, aligned to a 0x200-byte boundary through sm64.ld. The alignment
// wastes 0x100 bytes of space.
u64 gGfxSPTaskOutputBuffer[0x2fc0];
#else
// 0x1f000 bytes, aligned to a 0x1000-byte boundary through sm64.ld. (This results
// in a bunch of unused space: ~0x100 in JP, ~0x300 in US.)
u64 gGfxSPTaskOutputBuffer[0x3e00];
#endif
