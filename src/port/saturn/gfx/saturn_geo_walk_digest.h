/* S1.10 equivalence oracle: a streaming digest of everything the N64 geo walk
 * publishes during one tick.
 *
 * WHY A STREAM AND NOT A SNAPSHOT. Most of the walk's observable output does
 * not survive the walk. `throwMatrix` publishes a POINTER into `gMatStack`,
 * whose slot is reused by the next sibling; the animation globals are
 * saved/restored around held objects; the frustum-cull verdict is consumed
 * immediately. Digesting state at end-of-walk would therefore miss nearly
 * everything that matters and would certify a replacement that got the
 * intermediate stream wrong. Folding at each publication point captures the
 * values AND their order, which is what equivalence actually means here.
 *
 * WHAT THIS IS FOR. S1.10 replaces the geo walk with a Saturn-native one. The
 * walk cannot simply be deleted -- W1 measured that at +18.18% and produced a
 * build that can never ship, because the walk feeds animation, camera, the
 * actor observer, positional audio and a gameplay-visible cull decision. A
 * replacement is only safe if it reproduces that published stream exactly.
 * This digest is how that claim gets checked tick by tick instead of asserted.
 *
 * NOT A CRYPTOGRAPHIC HASH. FNV-1a 64 is used because this runs per tick on a
 * 28.6 MHz SH-2 and only needs to detect accidental divergence, not resist an
 * adversary. `saturn_sha256` remains the tool for build identity.
 */
#ifndef SM64_SATURN_GEO_WALK_DIGEST_H
#define SM64_SATURN_GEO_WALK_DIGEST_H

#include <stdbool.h>
#include <stdint.h>

/* Event tags. The tag is folded before the payload so that two events with
 * identical bytes but different meaning cannot collide, and so that a
 * replacement which publishes the right values through the wrong event is
 * still caught. Values are frozen: appending is fine, renumbering is not,
 * because recorded digests from earlier runs would silently stop matching. */
enum {
    SM64_SATURN_GEO_WALK_EVENT_NONE = 0U,
    SM64_SATURN_GEO_WALK_EVENT_OBJECT_OBSERVED = 1U,
    SM64_SATURN_GEO_WALK_EVENT_OBJECT_REJECTED = 2U,
    SM64_SATURN_GEO_WALK_EVENT_CULL_VERDICT = 3U,
    SM64_SATURN_GEO_WALK_EVENT_THROW_MATRIX = 4U,
    SM64_SATURN_GEO_WALK_EVENT_CAMERA_MATRIX = 5U,
    SM64_SATURN_GEO_WALK_EVENT_ANIMATION_GLOBALS = 6U,
    SM64_SATURN_GEO_WALK_EVENT_SWITCH_SELECTED = 7U,
    SM64_SATURN_GEO_WALK_EVENT_SHADOW_STATE = 8U,
    SM64_SATURN_GEO_WALK_EVENT_GENERATED_LIST = 9U,
    SM64_SATURN_GEO_WALK_EVENT_OBSERVER_TOTALS = 10U
};

typedef struct sm64_saturn_geo_walk_digest {
    uint64_t state;
    uint32_t event_count;
    uint32_t generation;
    uint8_t active;
    uint8_t reserved[3];
} sm64_saturn_geo_walk_digest_t;

/* Begin a tick. Folds the generation so that two ticks publishing identical
 * streams still produce different digests -- otherwise a replacement that
 * froze the walk would match a stationary reference run. */
void sm64_saturn_geo_walk_digest_begin(sm64_saturn_geo_walk_digest_t *digest,
                                       uint32_t generation);

/* Fold one publication event. `bytes` may be NULL only when size is 0. */
void sm64_saturn_geo_walk_digest_fold(sm64_saturn_geo_walk_digest_t *digest,
                                      uint32_t tag, const void *bytes,
                                      uint32_t size);

/* Convenience for the common scalar events. */
void sm64_saturn_geo_walk_digest_fold_u32(
    sm64_saturn_geo_walk_digest_t *digest, uint32_t tag, uint32_t value);

/* Fold a Q16 4x4 matrix in a fixed row-major order, independent of how the
 * caller stores it, so the digest does not accidentally encode layout. */
void sm64_saturn_geo_walk_digest_fold_matrix_q16(
    sm64_saturn_geo_walk_digest_t *digest, uint32_t tag,
    const int32_t matrix[4][4]);

/* Close the tick. Folds the event count, so a truncated stream cannot match a
 * complete one that happens to hash the same prefix. Returns the digest. */
uint64_t sm64_saturn_geo_walk_digest_finish(
    sm64_saturn_geo_walk_digest_t *digest);

/* Process-wide instance the walk folds into, so publication sites do not have
 * to thread a pointer through the traversal. Returns NULL when the digest is
 * not armed, which is the shipped configuration. */
sm64_saturn_geo_walk_digest_t *sm64_saturn_geo_walk_digest_bound(void);
void sm64_saturn_geo_walk_digest_bind(sm64_saturn_geo_walk_digest_t *digest);

#endif /* SM64_SATURN_GEO_WALK_DIGEST_H */
