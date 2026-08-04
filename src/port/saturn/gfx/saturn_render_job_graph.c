#include "saturn_render_job_graph.h"

#include <string.h>

#if defined(__sh__)
#include <yaul.h>
#endif

static inline sm64_saturn_render_job_graph_t *graph_cache_through(
    sm64_saturn_render_job_graph_t *graph)
{
#if defined(__sh__)
    return graph == NULL ? NULL : (sm64_saturn_render_job_graph_t *)(
        CPU_CACHE_THROUGH | (uintptr_t)graph);
#else
    return graph;
#endif
}

static inline void graph_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static bool graph_current(const sm64_saturn_render_job_graph_t *graph,
                          uint32_t generation)
{
    graph = graph_cache_through((sm64_saturn_render_job_graph_t *)graph);
    return graph != NULL && graph->queue != NULL && generation != 0U &&
        graph->generation == generation && graph->count != 0U &&
        sm64_saturn_render_job_queue_generation(graph->queue) == generation;
}

static bool predecessors_done(const sm64_saturn_render_job_graph_t *graph,
                              uint32_t generation, uint16_t job_index)
{
    const uint8_t mask = graph->dependency_mask[job_index];
    for (uint16_t predecessor = 0U; predecessor < graph->count; predecessor++)
        if ((mask & (uint8_t)(1U << predecessor)) != 0U &&
            sm64_saturn_render_job_queue_job(graph->queue, generation,
                                              predecessor) == NULL)
            return false;
    return true;
}

static bool predecessor_failed(const sm64_saturn_render_job_graph_t *graph,
                               uint16_t job_index)
{
    const uint8_t mask = graph->dependency_mask[job_index];
    for (uint16_t predecessor = 0U; predecessor < graph->count; predecessor++) {
        if ((mask & (uint8_t)(1U << predecessor)) == 0U) continue;
        const sm64_saturn_render_job_state_t state =
            sm64_saturn_render_job_queue_state(graph->queue, graph->generation,
                                                predecessor);
        if (state == SM64_SATURN_RENDER_JOB_FAILED ||
            state == SM64_SATURN_RENDER_JOB_QUARANTINED) return true;
    }
    return false;
}

static bool dependency_graph_acyclic(const uint8_t *dependency_mask,
                                     uint16_t count)
{
    uint8_t retired = 0U;
    const uint8_t valid_mask = (uint8_t)((1U << count) - 1U);
    for (uint16_t pass = 0U; pass < count; pass++) {
        bool progressed = false;
        for (uint16_t index = 0U; index < count; index++) {
            const uint8_t bit = (uint8_t)(1U << index);
            if ((retired & bit) != 0U) continue;
            if ((dependency_mask[index] & (uint8_t)~retired & valid_mask) != 0U)
                continue;
            retired |= bit;
            progressed = true;
        }
        if (!progressed) break;
    }
    return retired == valid_mask;
}

void sm64_saturn_render_job_graph_init(sm64_saturn_render_job_graph_t *graph,
                                       sm64_saturn_render_job_queue_t *queue)
{
    graph = graph_cache_through(graph);
    if (graph == NULL) return;
    memset(graph, 0, sizeof(*graph));
    graph->queue = queue;
}

bool sm64_saturn_render_job_graph_publish(
    sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    const sm64_saturn_render_job_t *jobs, const uint8_t *dependency_mask,
    uint16_t count)
{
    graph = graph_cache_through(graph);
    if (graph == NULL || graph->queue == NULL || jobs == NULL ||
        dependency_mask == NULL || count == 0U ||
        count > SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY) return false;
    const uint8_t valid_mask = (uint8_t)((1U << count) - 1U);
    for (uint16_t index = 0U; index < count; index++)
        if ((dependency_mask[index] & (uint8_t)~valid_mask) != 0U ||
            (dependency_mask[index] & (uint8_t)(1U << index)) != 0U)
            return false;
    if (!dependency_graph_acyclic(dependency_mask, count)) return false;
    memcpy(graph->dependency_mask, dependency_mask, count);
    graph->generation = generation;
    graph->count = count;
    graph_fence();
    if (!sm64_saturn_render_job_queue_publish(graph->queue, generation, jobs,
                                              count)) {
        graph->generation = 0U;
        graph->count = 0U;
        return false;
    }
    return true;
}

static bool graph_claim(sm64_saturn_render_job_graph_t *graph,
                        uint32_t generation,
                        sm64_saturn_render_job_state_t claimant,
                        uint16_t *job_index)
{
    graph = graph_cache_through(graph);
    if (job_index != NULL) *job_index = 0U;
    if (!graph_current(graph, generation) || job_index == NULL) return false;
    for (uint16_t index = 0U; index < graph->count; index++)
        if (predecessors_done(graph, generation, index) &&
            sm64_saturn_render_job_queue_claim_index(graph->queue, generation,
                                                      index, claimant)) {
            *job_index = index;
            return true;
        }
    return false;
}

bool sm64_saturn_render_job_graph_claim_master(
    sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t *job_index)
{
    graph = graph_cache_through(graph);
    return graph_claim(graph, generation,
                       SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, job_index);
}

bool sm64_saturn_render_job_graph_claim_slave(
    sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    uint16_t *job_index)
{
    return graph_claim(graph, generation,
                       SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE, job_index);
}

bool sm64_saturn_render_job_graph_propagate_failures(
    sm64_saturn_render_job_graph_t *graph, uint32_t generation)
{
    bool changed = false;
    if (!graph_current(graph, generation)) return false;
    bool pass_changed;
    do {
        pass_changed = false;
        for (uint16_t index = 0U; index < graph->count; index++)
            if (predecessor_failed(graph, index) &&
                sm64_saturn_render_job_queue_quarantine_ready(graph->queue,
                                                               generation, index)) {
                changed = true;
                pass_changed = true;
            }
    } while (pass_changed);
    return changed;
}

bool sm64_saturn_render_job_graph_validate_terrain_merge(
    const sm64_saturn_render_job_graph_t *graph, uint32_t generation,
    const sm64_saturn_render_job_result_identity_t *identities,
    uint16_t count)
{
    graph = graph_cache_through((sm64_saturn_render_job_graph_t *)graph);
    if (!graph_current(graph, generation) || identities == NULL || count == 0U)
        return false;
    for (uint16_t index = 0U; index < count; index++) {
        const sm64_saturn_render_job_result_identity_t identity = identities[index];
        const sm64_saturn_render_job_t *const job =
            sm64_saturn_render_job_queue_job(graph->queue, generation,
                                              identity.job_index);
        if (job == NULL || job->type != SM64_SATURN_RENDER_JOB_WORLD_LOWER ||
            identity.output_index >= job->output_capacity) return false;
        if (index > 0U &&
            (identities[index - 1U].job_index > identity.job_index ||
             (identities[index - 1U].job_index == identity.job_index &&
              identities[index - 1U].output_index >= identity.output_index)))
            return false;
    }
    return true;
}
