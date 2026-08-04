/* Bounded generated-actor admission and painter-order preparation. */
#ifndef SM64_SATURN_ACTOR_MESHLETS_H
#define SM64_SATURN_ACTOR_MESHLETS_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_fast3d_frontend.h"
#include "saturn_render_snapshot.h"

typedef struct sm64_saturn_actor_draw_ref {
    uint16_t meshlet_id;
    uint16_t primitive_id;
    uint32_t sort_key;
} sm64_saturn_actor_draw_ref_t;

typedef struct sm64_saturn_actor_meshlet_output {
    sm64_saturn_actor_draw_ref_t *opaque;
    sm64_saturn_actor_draw_ref_t *translucent;
    uint16_t opaque_count;
    uint16_t translucent_count;
} sm64_saturn_actor_meshlet_output_t;

/* The generated Mario tables are only an input bank: this API contains no
 * actor-specific pointer, VDP, worker, or allocator state, so future actor
 * bank implementations can use the same bounded output contract. */
bool sm64_saturn_actor_meshlets_prepare(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats);

#endif
