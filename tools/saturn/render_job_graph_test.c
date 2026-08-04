#include <stdio.h>

#include "saturn_render_job_graph.h"

static int expect(int condition, const char *message)
{
    if (condition) return 1;
    fprintf(stderr, "%s\n", message);
    return 0;
}

static const sm64_saturn_render_job_t k_jobs[] = {
    {SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
     SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT, 19U, 0U, 4U, 0U, 4U},
    {SM64_SATURN_RENDER_JOB_WORLD_LOWER,
     SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER, 19U, 4U, 4U, 4U, 8U},
    {SM64_SATURN_RENDER_JOB_ACTOR_ADMIT,
     SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT, 19U, 8U, 2U, 12U, 2U},
};

int main(void)
{
    sm64_saturn_render_job_queue_t queue;
    sm64_saturn_render_job_graph_t graph;
    const uint8_t deps[] = {0U, 1U << 0U, 0U};
    uint16_t job = 0U;
    const sm64_saturn_render_job_result_identity_t merge[] = {
        {1U, 0U}, {1U, 1U}, {1U, 2U}, {1U, 3U},
    };

    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_job_graph_init(&graph, &queue);
    if (!expect(sm64_saturn_render_job_graph_publish(
                    &graph, 19U, k_jobs, deps, 3U),
                "dependency graph must publish immutable descriptors")) return 1;
    if (!expect(sm64_saturn_render_job_graph_claim_slave(&graph, 19U, &job) &&
                    job == 0U,
                "a slave may claim a ready producer")) return 1;
    if (!expect(!sm64_saturn_render_job_graph_claim_master(&graph, 19U, &job),
                "a consumer must not claim before its producer is DONE")) return 1;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 19U, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "cross-CPU producer must retire")) return 1;
    if (!expect(sm64_saturn_render_job_graph_claim_master(&graph, 19U, &job) &&
                    job == 1U,
                "a consumer may claim only after its producer retires")) return 1;
    if (!expect(!sm64_saturn_render_job_graph_validate_terrain_merge(
                    &graph, 19U, merge, 4U),
                "terrain merge identities must reject a non-DONE consumer")) return 1;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 19U, 1U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER),
                "master consumer must retire")) return 1;
    if (!expect(sm64_saturn_render_job_graph_validate_terrain_merge(
                    &graph, 19U, merge, 4U),
                "terrain multi-result merge must retain descriptor/local order")) return 1;
    if (!expect(sm64_saturn_render_job_graph_claim_slave(&graph, 19U, &job) &&
                    job == 2U,
                "independent work remains eligible while another chain retires")) return 1;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 19U, 2U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "independent job must retire")) return 1;

    sm64_saturn_render_job_queue_init(&queue);
    sm64_saturn_render_job_graph_init(&graph, &queue);
    if (!expect(sm64_saturn_render_job_graph_publish(
                    &graph, 19U, k_jobs, deps, 3U),
                "failure graph must publish")) return 1;
    if (!expect(sm64_saturn_render_job_graph_claim_master(&graph, 19U, &job) &&
                    job == 0U,
                "master may claim a producer")) return 1;
    if (!expect(sm64_saturn_render_job_queue_fail(
                    &queue, 19U, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER),
                "producer failure must retire terminally")) return 1;
    if (!expect(!sm64_saturn_render_job_graph_claim_slave(&graph, 19U, &job),
                "failed producer must never expose its consumer")) return 1;
    if (!expect(sm64_saturn_render_job_graph_propagate_failures(&graph, 19U),
                "failed producer must terminally quarantine dependent work")) return 1;
    if (!expect(sm64_saturn_render_job_graph_claim_slave(&graph, 19U, &job) &&
                    job == 2U &&
                    sm64_saturn_render_job_queue_complete(
                        &queue, 19U, job,
                        SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "an independent chain must still retire after another fails")) return 1;
    if (!expect(sm64_saturn_render_job_queue_all_terminal(&queue, 19U),
                "failure propagation must leave a terminal generation")) return 1;
    puts("render job graph fixture: PASS");
    return 0;
}
