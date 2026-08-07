#ifndef SM64_SATURN_VDP2_FRAME_H
#define SM64_SATURN_VDP2_FRAME_H

#include <stdint.h>

#include "saturn_fast3d_frontend.h"
#include "saturn_vdp2_camera_snapshot.h"

/* VDP2 owns only sky, HUD text, and layer composition.  These values follow
 * Yaul's NBG register numbering but keep the frame policy host-testable. */
#define SM64_SATURN_VDP2_FRAME_NBG0_MASK (1U << 0)
#define SM64_SATURN_VDP2_FRAME_NBG1_MASK (1U << 1)
#define SM64_SATURN_VDP2_FRAME_NBG3_MASK (1U << 3)
#define SM64_SATURN_VDP2_FRAME_DISPLAY_MASK \
    (SM64_SATURN_VDP2_FRAME_NBG0_MASK | SM64_SATURN_VDP2_FRAME_NBG1_MASK | \
     SM64_SATURN_VDP2_FRAME_NBG3_MASK)
#define SM64_SATURN_VDP2_FRAME_HUD_TICK_DIVISOR 30U
#define SM64_SATURN_VDP2_FRAME_SOURCE_TICKS_PER_SECOND 30U
#define SM64_SATURN_VDP2_FRAME_HUD_TEXT_CAPACITY 224U

/* One terminal VDP1/VDP2 composition pins the displayed/rendered bank
 * generation and names the current authoritative simulation generation. The
 * VDP2 module receives only this metadata and the immutable camera snapshot:
 * it remains independent of geometry, actors, VDP1 command data, and live
 * game state. */
typedef struct sm64_saturn_vdp2_generation_state {
    uint32_t displayed_generation;
    uint32_t rendered_generation;
    uint32_t simulation_generation;
} sm64_saturn_vdp2_generation_state_t;

typedef struct sm64_saturn_vdp2_frame {
    int32_t sky_scroll_x;
    int32_t sky_scroll_y;
    uint32_t display_mask;
    uint32_t last_hud_source_tick;
    uint32_t last_hud_displayed_generation;
    uint32_t last_hud_rendered_generation;
    uint32_t last_hud_simulation_generation;
    uint32_t fps_anchor_source_tick;
    uint32_t fps_presented_frames;
    uint32_t total_fps;
    uint32_t commits;
    uint8_t hud_dirty;
    uint8_t prepared;
    char hud_text[SM64_SATURN_VDP2_FRAME_HUD_TEXT_CAPACITY];
} sm64_saturn_vdp2_frame_t;

typedef struct sm64_saturn_vdp2_frame_backend {
    void (*sky_scroll_set)(int32_t x, int32_t y, void *work);
    void (*hud_write)(const char *text, void *work);
    void (*layers_set)(uint32_t display_mask, uint8_t vdp1_priority,
                       void *work);
    void (*vblank_commit)(void *work);
    void *work;
} sm64_saturn_vdp2_frame_backend_t;

void sm64_saturn_vdp2_frame_init(sm64_saturn_vdp2_frame_t *frame);
void sm64_saturn_vdp2_frame_begin(
    sm64_saturn_vdp2_frame_t *frame,
    const sm64_saturn_vdp2_camera_snapshot_t *snapshot,
    const sm64_saturn_fast3d_profile_t *profile,
    const sm64_saturn_vdp2_generation_state_t *generations,
    uint32_t source_tick);
void sm64_saturn_vdp2_frame_commit(
    sm64_saturn_vdp2_frame_t *frame,
    const sm64_saturn_vdp2_frame_backend_t *backend);

#endif
