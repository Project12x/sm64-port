#include <stdint.h>
#include <stdio.h>

#include "saturn_terrain_queue_handoff.h"
#include "saturn_render_job_graph.h"

static uint8_t s_admit_owner;
static uint8_t s_lower_local_owner;

static bool admit_callback(const sm64_saturn_render_job_t *job,
                           sm64_saturn_render_job_state_t claim)
{
    sm64_saturn_terrain_queue_handoff_t handoff;
    const uint8_t lane = claim == SM64_SATURN_RENDER_JOB_CLAIMED_MASTER
        ? 0U : 1U;
    if (job == NULL || job->type != SM64_SATURN_RENDER_JOB_WORLD_ADMIT ||
        !sm64_saturn_terrain_queue_handoff_single_producer(lane, &handoff) ||
        handoff.peer_transform_required != 0U)
        return false;
    s_admit_owner = handoff.producer_lane;
    return true;
}

static bool lower_callback(const sm64_saturn_render_job_t *job)
{
    sm64_saturn_terrain_queue_handoff_t handoff;
    if (job == NULL || job->type != SM64_SATURN_RENDER_JOB_WORLD_LOWER ||
        !sm64_saturn_terrain_queue_handoff_single_producer(
            s_admit_owner, &handoff))
        return false;
    /* This assignment models WORLD_LOWER rebuilding its own cached owner map
     * from exact DONE admit metadata, overwriting a poisoned prior frame. */
    s_lower_local_owner = handoff.producer_lane;
    return true;
}

static bool run_generation(sm64_saturn_render_job_graph_t *graph,
                           uint32_t generation,
                           sm64_saturn_render_job_state_t admit_claim,
                           sm64_saturn_render_job_state_t lower_claim)
{
    const sm64_saturn_render_job_t jobs[] = {{
        .type = SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT,
        .snapshot_generation = generation,
        .input_count = 1U,
        .output_capacity = 1U,
    }, {
        .type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
        .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
        .snapshot_generation = generation,
        .input_count = 1U,
        .output_capacity = 1U,
    }};
    const uint8_t dependencies[] = {0U, 1U};
    if (!sm64_saturn_render_job_graph_publish(
            graph, generation, jobs, dependencies, 2U) ||
        !sm64_saturn_render_job_queue_claim_index(
            graph->queue, generation, 0U, admit_claim) ||
        !admit_callback(&graph->queue->jobs[0], admit_claim) ||
        !sm64_saturn_render_job_queue_complete(
            graph->queue, generation, 0U, admit_claim) ||
        !sm64_saturn_render_job_queue_claim_index(
            graph->queue, generation, 1U, lower_claim) ||
        !lower_callback(&graph->queue->jobs[1]) ||
        !sm64_saturn_render_job_queue_complete(
            graph->queue, generation, 1U, lower_claim) ||
        !sm64_saturn_render_job_queue_all_terminal(graph->queue, generation) ||
        !sm64_saturn_render_job_queue_reset_retired(graph->queue, generation))
        return false;
    return true;
}

int main(void)
{
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_job_graph_t graph;
    sm64_saturn_terrain_queue_handoff_t handoff;
    uint8_t poisoned_owner = 0U;
    if (!sm64_saturn_terrain_queue_handoff_single_producer(1U, &handoff) ||
        handoff.producer_lane != 1U || handoff.peer_transform_required != 0U)
        return 1;
    poisoned_owner = handoff.producer_lane;
    if (!sm64_saturn_terrain_queue_handoff_single_producer(0U, &handoff) ||
        handoff.producer_lane != 0U || handoff.peer_transform_required != 0U ||
        poisoned_owner == handoff.producer_lane ||
        sm64_saturn_terrain_queue_handoff_single_producer(2U, &handoff) ||
        sm64_saturn_terrain_queue_handoff_single_producer(0U, NULL))
        return 1;
    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_job_graph_init(&graph, &queue);
    s_lower_local_owner = 0U; /* poisoned master-cached prior generation */
    if (!run_generation(&graph, 11U,
            SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE,
            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER) ||
        s_lower_local_owner != 1U)
        return 1;
    s_lower_local_owner = 1U; /* poison in the opposite direction */
    if (!run_generation(&graph, 12U,
            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER,
            SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE) ||
        s_lower_local_owner != 0U)
        return 1;
    puts("terrain queue handoff fixture: PASS");
    return 0;
}
