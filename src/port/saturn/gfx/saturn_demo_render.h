#ifndef SM64_SATURN_DEMO_RENDER_H
#define SM64_SATURN_DEMO_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_fast3d_frontend.h"
#include "saturn_actor_bridge.h"
#include "saturn_gouraud_bank.h"
#include "saturn_vdp1_backend.h"

/* Bounded IR renderer used by sourceboot's opt-in demo profile.  It consumes
 * the generated BOB scene and the authoritative Lakitu camera, while the
 * interpreted Fast3D frontend remains available for differential builds. */
/* Promote the generated bank out of cart_rodata before the frame loop. */
void sm64_saturn_demo_render_init(void);

/* Call after every authoritative source tick. This tracks a real area exit
 * (`active == false`) and same-ID re-entry, resetting hysteretic LOD state at
 * the source scene boundary rather than at renderer startup alone. */
void sm64_saturn_demo_render_scene_observe(bool active, int16_t level,
                                           int16_t area);

void sm64_saturn_demo_render_frame(
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose);

#endif
