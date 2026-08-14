/* src/port/saturn/gfx/saturn_hud_publish.h */
#ifndef SM64_SATURN_HUD_PUBLISH_H
#define SM64_SATURN_HUD_PUBLISH_H

#include "saturn_hud.h"
#include "saturn_hud_layout.h"

/* Publishes a bounded complete visible HUD grid.  The sourceboot generic-BOB
 * target reserves its final LWRAM region for the slave stack, so this keeps
 * no persistent dirty-cell history: at most 280 blank PND writes followed by
 * 24 current glyph writes at the existing presentation boundary.  The atlas
 * writer remains the only VDP2-facing operation; this API stays Yaul-free and
 * host-testable. */
void sm64_saturn_hud_publish(const sm64_saturn_hud_snapshot_t *snapshot);

#endif /* SM64_SATURN_HUD_PUBLISH_H */
