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
bool sm64_saturn_render_job_graph_validate_terrain_merge(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    const sm64_saturn_render_job_result_identity_t *identities,
    uint16_t count);

#endif
