#ifndef SM64_SATURN_DEMO_RENDER_H
#define SM64_SATURN_DEMO_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_fast3d_frontend.h"
#include "saturn_actor_bridge.h"
#include "saturn_actor_batch.h"
#include "saturn_gouraud_bank.h"
#include "saturn_render_snapshot.h"
#include "saturn_render_lifecycle.h"
#include "saturn_vdp1_backend.h"

/* Bounded IR renderer used by sourceboot's opt-in demo profile.  It consumes
 * the generated BOB scene and the authoritative Lakitu camera, while the
 * interpreted Fast3D frontend remains available for differential builds. */
/* Promote the generated bank out of cart_rodata before the frame loop. */
void sm64_saturn_demo_render_init(void);

/* Boot-time generic actor validation and frame-time actor preparation reuse
 * the Mario transform context only while no Mario job owns it. */
void *sm64_saturn_demo_render_actor_workspace(uint32_t *byte_count);

/* Call after every authoritative source tick. This tracks a real area exit
 * (`active == false`) and same-ID re-entry, resetting hysteretic LOD state at
 * the source scene boundary rather than at renderer startup alone. */
void sm64_saturn_demo_render_scene_observe(bool active, int16_t level,
                                           int16_t area);

bool sm64_saturn_demo_render_observe_lifecycle(
    sm64_saturn_render_lifecycle_observer_t observer, void *context);

typedef enum sm64_saturn_demo_render_status {
    SM64_SATURN_DEMO_RENDER_PENDING = 0,
    SM64_SATURN_DEMO_RENDER_COMPLETE,
    SM64_SATURN_DEMO_RENDER_FAILED
} sm64_saturn_demo_render_status_t;

/* Publish immutable generation-bound work and return after slave notify.
 * Snapshot, pose, backend storage, and Gouraud storage remain caller-owned
 * and immutable until poll_frame() returns COMPLETE or FAILED. */
bool sm64_saturn_demo_render_start_frame(
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_snapshot_t *scene_snapshot,
    sm64_saturn_actor_runtime_storage_t *actor_runtime,
    uint32_t generation);

/* Finalize only after positive slave retirement. The successful call owns
 * the one master drain, terminal validation/merge, Gouraud reservation, VDP1
 * lowering, and retired-generation reset. FAILED never serially replays. */
sm64_saturn_demo_render_status_t sm64_saturn_demo_render_poll_frame(
    sm64_saturn_fast3d_profile_t *profile, uint32_t generation);

#endif
