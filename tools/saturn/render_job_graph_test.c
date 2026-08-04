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
    if (!expect(sm64_saturn_render_job_queue_published_job(
                    &queue, 19U, 1U) != NULL,
                "a current immutable lower descriptor must remain inspectable before DONE"))
        return 1;
    if (!expect(sm64_saturn_render_job_graph_claim_slave(&graph, 19U, &job) &&
                    job == 0U,
                "a slave may claim a ready producer")) return 1;
    if (!expect(sm64_saturn_render_job_graph_claim_master(&graph, 19U, &job) &&
                    job == 2U,
                "an independent job remains eligible before another chain retires")) return 1;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 19U, 2U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_MASTER),
                "independent work must retire independently")) return 1;
    if (!expect(!sm64_saturn_render_job_graph_claim_master(&graph, 19U, &job),
                "a dependent consumer must not claim before its producer is DONE")) return 1;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 19U, 0U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "cross-CPU producer must retire")) return 1;
    if (!expect(sm64_saturn_render_job_graph_claim_master(&graph, 19U, &job) &&
                    job == 1U,
                "a consumer may claim only after its producer retires")) return 1;
    {
        uint16_t admit = UINT16_MAX;
        if (!expect(sm64_saturn_render_job_graph_world_lower_admit_done(
                        &graph, 19U, job,
                        SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, &admit) &&
                        admit == 0U,
                    "a lower claim must prove its exact completed WORLD_ADMIT predecessor"))
            return 1;
    }
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
    if (!expect(sm64_saturn_render_job_graph_claim_slave(&graph, 19U, &job) &&
                    job == 2U,
                "an independent job remains eligible after another chain fails")) return 1;
    if (!expect(sm64_saturn_render_job_queue_complete(
                    &queue, 19U, 2U,
                    SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE),
                "independent job must still retire")) return 1;
    if (!expect(sm64_saturn_render_job_graph_propagate_failures(&graph, 19U),
                "failed producer must terminally quarantine dependent work")) return 1;
    if (!expect(sm64_saturn_render_job_queue_all_terminal(&queue, 19U),
                "failure propagation must leave a terminal generation")) return 1;

    {
        const uint8_t cycle[] = {1U << 1U, 1U << 0U, 0U};
        sm64_saturn_render_job_queue_init(&queue);
        sm64_saturn_render_job_graph_init(&graph, &queue);
        if (!expect(!sm64_saturn_render_job_graph_publish(
                        &graph, 19U, k_jobs, cycle, 3U),
                    "cyclic dependencies must fail before queue publication")) return 1;
    }
    {
        /* Mutate the scheduler path deliberately: a raw lower claim before
         * its admit predecessor is terminal must still fail the callback-side
         * proof, even though normal graph draining cannot make this claim. */
        uint16_t admit = UINT16_MAX;
        sm64_saturn_render_job_queue_init(&queue);
        sm64_saturn_render_job_graph_init(&graph, &queue);
        if (!expect(sm64_saturn_render_job_graph_publish(
                        &graph, 19U, k_jobs, deps, 3U) &&
                        sm64_saturn_render_job_queue_claim_index(
                            &queue, 19U, 1U,
                            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER) &&
                        !sm64_saturn_render_job_graph_world_lower_admit_done(
                            &graph, 19U, 1U,
                            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, &admit),
                    "a lower callback must reject an unready WORLD_ADMIT predecessor"))
            return 1;
    }
    {
        const uint8_t wrong_deps[] = {0U, 1U << 2U, 0U};
        uint16_t admit = UINT16_MAX;
        sm64_saturn_render_job_queue_init(&queue);
        sm64_saturn_render_job_graph_init(&graph, &queue);
        if (!expect(sm64_saturn_render_job_graph_publish(
                        &graph, 19U, k_jobs, wrong_deps, 3U) &&
                        sm64_saturn_render_job_queue_claim_index(
                            &queue, 19U, 2U,
                            SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE) &&
                        sm64_saturn_render_job_queue_complete(
                            &queue, 19U, 2U,
                            SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE) &&
                        sm64_saturn_render_job_queue_claim_index(
                            &queue, 19U, 1U,
                            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER) &&
                        !sm64_saturn_render_job_graph_world_lower_admit_done(
                            &graph, 19U, 1U,
                            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER, &admit),
                    "a lower callback must reject a completed non-WORLD_ADMIT predecessor"))
            return 1;
    }
    {
        const sm64_saturn_render_job_t chain[] = {
            k_jobs[0], k_jobs[1],
            {SM64_SATURN_RENDER_JOB_WORLD_LOWER,
             SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER, 19U, 12U, 2U,
             14U, 2U},
        };
        const uint8_t chain_deps[] = {0U, 1U << 0U, 1U << 1U};
        sm64_saturn_render_job_queue_init(&queue);
        sm64_saturn_render_job_graph_init(&graph, &queue);
        if (!expect(sm64_saturn_render_job_graph_publish(
                        &graph, 19U, chain, chain_deps, 3U) &&
                        sm64_saturn_render_job_graph_claim_master(
                            &graph, 19U, &job) && job == 0U &&
                        sm64_saturn_render_job_queue_fail(
                            &queue, 19U, 0U,
                            SM64_SATURN_RENDER_JOB_CLAIMED_MASTER) &&
                        sm64_saturn_render_job_graph_propagate_failures(
                            &graph, 19U) &&
                        sm64_saturn_render_job_queue_all_terminal(&queue, 19U),
                    "failure propagation must quarantine every reverse-chain dependent")) return 1;
    }
    puts("render job graph fixture: PASS");
    return 0;
}
