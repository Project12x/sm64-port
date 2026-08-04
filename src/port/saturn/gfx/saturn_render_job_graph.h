/* Dependency and result-identity layer for the A5 render queue.
 *
 * Descriptor records stay fixed-width and pointer-free.  Dependency masks are
 * renderer-local graph metadata, published alongside one queue generation;
 * no callback may claim a consumer until all named producers are DONE.
 */
#ifndef SM64_SATURN_RENDER_JOB_GRAPH_H
#define SM64_SATURN_RENDER_JOB_GRAPH_H

#include "saturn_render_job_queue.h"

typedef struct sm64_saturn_render_job_result_identity {
    uint16_t job_index;
    uint16_t output_index;
} sm64_saturn_render_job_result_identity_t;

typedef struct sm64_saturn_render_job_graph {
    sm64_saturn_render_job_queue_t *queue;
    uint8_t dependency_mask[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    uint32_t generation;
    uint16_t count;
} sm64_saturn_render_job_graph_t;

void sm64_saturn_render_job_graph_init(sm64_saturn_render_job_graph_t *graph,
                                       sm64_saturn_render_job_queue_t *queue);
bool sm64_saturn_render_job_graph_publish(
    sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    const sm64_saturn_render_job_t *jobs, const uint8_t *dependency_mask,
    uint16_t count);
bool sm64_saturn_render_job_graph_claim_master(
    sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t *job_index);
bool sm64_saturn_render_job_graph_claim_slave(
    sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t *job_index);
bool sm64_saturn_render_job_graph_propagate_failures(
    sm64_saturn_render_job_graph_t *graph, uint32_t generation);
/* A terrain lower callback must name exactly one immutable WORLD_ADMIT
 * predecessor and that descriptor must already be terminal DONE.  This is a
 * callback-side proof in addition to scheduler eligibility, so a malformed
 * graph or a stale/unready predecessor fails closed before payload reads. */
bool sm64_saturn_render_job_graph_world_lower_admit_done(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t lower_job_index,
    sm64_saturn_render_job_state_t claimed_state, uint16_t *admit_job_index);
/* Enumerate every immutable WORLD_LOWER in a current generation and require
 * each exact descriptor to be terminal DONE. This is the executable
 * completeness gate used before renderer metadata/payload assembly. */
bool sm64_saturn_render_job_graph_collect_done_world_lower(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t *job_indices, uint16_t capacity, uint16_t *count);
bool sm64_saturn_render_job_graph_validate_terrain_merge(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    const sm64_saturn_render_job_result_identity_t *identities,
    uint16_t count);
/* Actor transform/classify mirrors the terrain producer/consumer contract.
 * ACTOR_LOWER must name exactly one completed ACTOR_ADMIT descriptor before
 * it may read the descriptor-owned projected-vertex payload. */
bool sm64_saturn_render_job_graph_actor_lower_admit_done(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t lower_job_index,
    sm64_saturn_render_job_state_t claimed_state, uint16_t *admit_job_index);
bool sm64_saturn_render_job_graph_actor_done_lower_admit_done(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t lower_job_index, uint16_t *admit_job_index);
bool sm64_saturn_render_job_graph_collect_done_actor_lower(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t *job_indices, uint16_t capacity, uint16_t *count);
bool sm64_saturn_render_job_graph_validate_actor_merge(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    const sm64_saturn_render_job_result_identity_t *identities,
    uint16_t count);

#endif
