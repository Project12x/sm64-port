/* src/port/saturn/gfx/saturn_hud.h */
#ifndef SM64_SATURN_HUD_H
#define SM64_SATURN_HUD_H

#include <stdint.h>

/* Everything render_hud() reads or computes that the VDP2 backend needs,
 * gathered into one fixed-width, pointer-free struct -- matching the same
 * "scalar copies only" discipline sm64_saturn_render_snapshot_t already
 * documents (saturn_render_snapshot.h:39-40). This type carries no
 * generation field of its own: it inherits render-snapshot/frame-bank
 * generation coherence for free by riding inside those existing structs
 * (see saturn_render_snapshot.h and saturn_vdp1_frame_bank.h). */
typedef struct sm64_saturn_hud_snapshot {
    int16_t lives;
    int16_t coins;
    int16_t stars;
    int16_t wedges;
    int16_t keys;
    int16_t flags;
    uint16_t timer;
    int16_t camera_status;
    int8_t power_meter_animation;
    int16_t power_meter_y;
    uint8_t cannon_active;
    uint8_t reserved0;
} sm64_saturn_hud_snapshot_t;

_Static_assert(sizeof(sm64_saturn_hud_snapshot_t) == 22U,
               "hud snapshot ABI must remain fixed-width");

#endif /* SM64_SATURN_HUD_H */
