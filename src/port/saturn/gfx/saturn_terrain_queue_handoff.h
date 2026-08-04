/* Single-producer terrain handoff for the A5 render graph.
 *
 * One WORLD_ADMIT descriptor transforms the complete visible-position set.
 * Its actual claimant is therefore the sole payload producer; WORLD_LOWER
 * must rebuild its local owner map from that exact DONE claimant and must not
 * wait for a second transform publication.
 */
#ifndef SM64_SATURN_TERRAIN_QUEUE_HANDOFF_H
#define SM64_SATURN_TERRAIN_QUEUE_HANDOFF_H

#include <stdbool.h>
#include <stdint.h>

typedef struct sm64_saturn_terrain_queue_handoff {
    uint8_t producer_lane;
    uint8_t peer_transform_required;
} sm64_saturn_terrain_queue_handoff_t;

static inline bool sm64_saturn_terrain_queue_handoff_single_producer(
    uint8_t claimant_lane, sm64_saturn_terrain_queue_handoff_t *handoff)
{
    if (handoff == NULL || claimant_lane > 1U) return false;
    handoff->producer_lane = claimant_lane;
    handoff->peer_transform_required = 0U;
    return true;
}

#endif
