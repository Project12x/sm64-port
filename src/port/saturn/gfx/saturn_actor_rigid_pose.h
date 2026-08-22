/* Rigid-part pose evaluator: the second evaluator lane.
 *
 * WHY A SECOND EVALUATOR
 * ----------------------
 * `sm64_saturn_actor_pose_evaluate_staged` produces *skinned* vertices: it
 * walks the joint hierarchy, then transforms all 424 of Mario's vertices by
 * the joint each one is bound to.  That shape is a plan-2a *recreation* built
 * for exactly one actor, and its cost grows with mesh density.
 *
 * Every other jointed actor in SM64 is a **rigid-part hierarchy**: each
 * `GEO_ANIMATED_PART` carries a whole display list on one matrix, and nothing
 * inside that display list deforms.  Its published pose is therefore an array
 * of *part transforms*, not an array of posed vertices -- O(parts), not
 * O(vertices), and it does not grow when the bake gets denser.
 *
 * Mario keeps the skinned path unchanged.  This is a second lane, not a
 * replacement.
 *
 * THE PUBLISHED LAYOUT, AND WHY IT IS 24 B PER PART
 * -------------------------------------------------
 * r2-pose-slot-pooling.md section 2 assumed 48 B per part (a 3x4 Q16.16
 * matrix) and section 8.2 left the decision open, noting that 12 B would
 * shrink the class plan.  The shipped answer is **24 B**: a 3x3 rotation in
 * Q1.15 plus an int16 translation triple.
 *
 *   - Q16.16 rotation is 15 bits of precision nobody can see.  A rotation
 *     entry lives in [-1, 1]; Q1.15 resolves it to 3.05e-5, and a rigid part
 *     is at most a few hundred actor units across, so the worst per-vertex
 *     placement error is ~0.01 units.  Phase plan 2b: pick what plays right.
 *   - The error does **not** accumulate.  Every part is recomposed from the
 *     animation channels from scratch each frame, so this is a fixed offset
 *     within one frame, which 2b calls invisible, and never the
 *     swimming/creeping class of defect, which 2b calls a defect at any size.
 *   - 12 B (angles + translation) was rejected: Euler angles do not compose,
 *     so a 12 B published form either forces the *consumer* to walk the
 *     hierarchy every frame, or forces the producer to decompose a composed
 *     rotation back to angles through an asin/atan2 per part.  Both spend
 *     more than the 12 B they save.
 *   - Composition is still done in the existing Q16.16 workspace with the
 *     already-proven `sm64_saturn_matrix_mul`; the quantisation happens once,
 *     at publish.  No new fixed-point code path exists.
 *
 * CONSUMER CONTRACT
 * -----------------
 * `rotation` is row-major and follows the same row-vector convention the
 * skinned evaluator's `matrix_apply` uses, so for a part-local vertex `v`:
 *
 *     out[c] = ((v[0]*rot[0*3+c] + v[1]*rot[1*3+c] + v[2]*rot[2*3+c]) >> 15)
 *              + translation[c]
 *
 * `translation` is already model space -- parent composition is done by the
 * producer, so a consumer never walks the hierarchy.
 */
#ifndef SM64_SATURN_ACTOR_RIGID_POSE_H
#define SM64_SATURN_ACTOR_RIGID_POSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "saturn_actor_bank.h"
#include "saturn_matrix.h"

/* One published part.  24 B, 4-byte aligned by construction. */
typedef struct sm64_saturn_rigid_part {
    int16_t rotation[9];    /* row-major 3x3, Q1.15 */
    int16_t translation[3]; /* model space, actor units */
} sm64_saturn_rigid_part_t;

#define SM64_SATURN_RIGID_PART_BYTES 24U

/* Flag bits in `sm64_saturn_rigid_pose_t.flags`. */
#define SM64_SATURN_RIGID_POSE_VALID 1U

/* The block a pose slot holds: a 16 B header followed by `part_count` parts.
 * `sm64_saturn_pose_pool_claim` is asked for exactly
 * SM64_SATURN_RIGID_POSE_BYTES(part_count). */
typedef struct sm64_saturn_rigid_pose {
    uint16_t animation_id;
    uint16_t frame;
    uint16_t frame_count;
    uint16_t part_count;
    int16_t root_translation[3];
    uint16_t flags;
    sm64_saturn_rigid_part_t part[]; /* part_count entries; see _BYTES */
} sm64_saturn_rigid_pose_t;

#define SM64_SATURN_RIGID_POSE_HEADER_BYTES 16U
#define SM64_SATURN_RIGID_POSE_BYTES(parts) \
    (SM64_SATURN_RIGID_POSE_HEADER_BYTES + \
     (uint32_t)(parts) * SM64_SATURN_RIGID_PART_BYTES)

_Static_assert(sizeof(sm64_saturn_rigid_part_t) == SM64_SATURN_RIGID_PART_BYTES,
               "published rigid part is not 24 B");
_Static_assert(sizeof(sm64_saturn_rigid_pose_t) ==
                   SM64_SATURN_RIGID_POSE_HEADER_BYTES,
               "published rigid pose header is not 16 B");

/* Ceiling on a single hierarchy.  24 parts is 592 B, which fits the largest
 * cold class (640 B).  A deeper actor is a class-plan data edit, not a
 * redesign -- see the class plan in saturn_actor_bridge.c. */
#define SM64_SATURN_RIGID_POSE_MAX_PARTS 24U

/* Q16.16 -> Q1.15 with round-half-up and a saturating clamp.  Exposed so the
 * host fixture can state its expectations in the published units rather than
 * re-deriving the conversion. */
int16_t sm64_saturn_rigid_q15_from_q16(int32_t value);

/* Compose a rigid-part hierarchy and publish it.
 *
 * `joints[i].parent_ordinal` must be < i (or -1 for a root): the bake emits
 * parents before children, and the check is enforced, not assumed.
 * `angles[i]` are the three s16 Euler samples for part i this frame.
 * `workspace` is `part_count` Q16.16 matrices of composition scratch; it is
 * consumed inside this call and is NOT part of the published pose, so one
 * workspace is shared with the skinned evaluator (there is exactly one
 * evaluator lane -- the master sim tick -- and the two calls are sequential).
 *
 * Fails closed: on any rejection `flags` is cleared, so a consumer sees an
 * invalid pose rather than a half-composed one. */
bool sm64_saturn_actor_rigid_pose_compose(
    const sm64_saturn_actor_joint_t *joints, uint16_t part_count,
    const int16_t (*angles)[3], const int16_t root_translation[3],
    sm64_saturn_mtx_t *workspace,
    sm64_saturn_rigid_pose_t *out, uint32_t out_bytes);

/* Bank-driven wrapper: sample the animation channels for `animation_id` at
 * `animation_frame` and compose.  `staged_joints` may be NULL, in which case
 * the joint table is read out of the bank (which lives in CART); passing it
 * is the same staging optimisation the skinned lane already takes. */
bool sm64_saturn_actor_rigid_pose_evaluate(
    const sm64_saturn_actor_bank_view_t *bank,
    int16_t animation_id, int16_t animation_frame,
    const sm64_saturn_actor_joint_t *staged_joints,
    sm64_saturn_mtx_t *workspace,
    sm64_saturn_rigid_pose_t *out, uint32_t out_bytes);

#endif
