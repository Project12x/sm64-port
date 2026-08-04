#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "saturn_render_job_queue.h"

typedef struct claimant_context {
    sm64_saturn_render_job_queue_t *queue;
    uint32_t generation;
    sm64_saturn_render_job_state_t owner;
    volatile LONG claims[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
} claimant_context_t;

typedef struct callback_context {
    uint16_t calls[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    uint16_t total;
} callback_context_t;

static const sm64_saturn_render_job_t k_jobs[] = {
    {.type = SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
     .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT,
     .snapshot_generation = 7U, .input_offset = 0U, .input_count = 12U,
     .output_offset = 0U, .output_capacity = 12U},
    {.type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
     .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
     .snapshot_generation = 7U, .input_offset = 12U, .input_count = 8U,
     .output_offset = 12U, .output_capacity = 8U},
    {.type = SM64_SATURN_RENDER_JOB_ACTOR_ADMIT,
     .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT,
     .snapshot_generation = 7U, .input_offset = 20U, .input_count = 6U,
     .output_offset = 20U, .output_capacity = 6U},
    {.type = SM64_SATURN_RENDER_JOB_ACTOR_LOWER,
     .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER,
     .snapshot_generation = 7U, .input_offset = 26U, .input_count = 4U,
     .output_offset = 26U, .output_capacity = 4U},
};

static DWORD WINAPI claim_until_empty(void *opaque)
{
    claimant_context_t *context = opaque;
    uint16_t index;
    while (context->owner == SM64_SATURN_RENDER_JOB_CLAIMED_MASTER
               ? sm64_saturn_render_job_queue_claim_master(
                     context->queue, context->generation, &index)
               : sm64_saturn_render_job_queue_claim_slave(
                     context->queue, context->generation, &index)) {
        if (InterlockedIncrement(&context->claims[index]) != 1L) return 1UL;
        if (!sm64_saturn_render_job_queue_complete(context->queue,
                                                    context->generation, index,
                                                    context->owner))
            return 1UL;
    }
    return 0UL;
}

static int expect(bool condition, const char *message)
{
    if (condition) return 1;
    fprintf(stderr, "%s\n", message);
    return 0;
}

static bool record_callback(const sm64_saturn_render_job_t *job,
                            sm64_saturn_render_job_state_t claimed_state,
                            void *opaque)
{
    callback_context_t *context = opaque;
    if (job == NULL || context == NULL ||
        job->callback_id == 0U ||
        job->callback_id > SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER ||
        (claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
         claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE))
        return false;
    context->calls[job->callback_id - 1U]++;
    context->total++;
    return true;
}

int main(void)
{
    sm64_saturn_render_job_queue_t queue;
    claimant_context_t master = {0};
    claimant_context_t slave = {0};
    uint16_t index;
    HANDLE master_thread;
    HANDLE slave_thread;
    DWORD master_result = 1UL;
    DWORD slave_result = 1UL;

    {
        /* Each job kind writes a physically distinct bounded payload bank, so
         * local offset zero is legal in all four namespaces. */
        sm64_saturn_render_job_t local_offsets[4];
        memcpy(local_offsets, k_jobs, sizeof(local_offsets));
        for (uint16_t job = 0U; job < 4U; job++)
            local_offsets[job].output_offset = 0U;
        sm64_saturn_render_job_queue_init(&queue);
        if (!expect(sm64_saturn_render_job_queue_publish(
                        &queue, 7U, local_offsets, 4U),
                    "different physical payload kinds may reuse local offsets"))
            return 1;
    }
    {
        sm64_saturn_render_job_t same_kind_overlap[2] = {
            k_jobs[1], k_jobs[1],
        };
        same_kind_overlap[1].input_offset = 40U;
        same_kind_overlap[1].output_offset = 8U;
        sm64_saturn_render_job_queue_init(&queue);
        if (!expect(!sm64_saturn_render_job_queue_publish(
                        &queue, 7U, same_kind_overlap, 2U),
                    "overlapping spans in one physical payload kind must fail"))
            return 1;
    }
    {
        sm64_saturn_render_job_t mismatched = k_jobs[0];
        mismatched.callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT;
        sm64_saturn_render_job_queue_init(&queue);
        if (!expect(!sm64_saturn_render_job_queue_publish(
                        &queue, 7U, &mismatched, 1U),
                    "type/callback payload-kind mismatch must fail closed"))
            return 1;
        mismatched = k_jobs[0];
        mismatched.type = 0U;
        if (!expect(!sm64_saturn_render_job_queue_publish(
                        &queue, 7U, &mismatched, 1U),
                    "unknown payload kind must fail closed"))
            return 1;
    }

    sm64_saturn_render_job_queue_init(&queue);
    if (!expect(sm64_saturn_render_job_queue_publish(&queue, 7U, k_jobs, 4U),
                "valid immutable jobs must publish")) return 1;
    if (!expect(!sm64_saturn_render_job_queue_publish(&queue, 8U, k_jobs,
                                                        4U),
                "live generation must fail closed")) return 1;
    if (!expect(!sm64_saturn_render_job_queue_claim_master(&queue, 6U, &index),
                "stale generation must not claim work")) return 1;

    master.queue = &queue;
    master.generation = 7U;
    master.owner = SM64_SATURN_RENDER_JOB_CLAIMED_MASTER;
    slave.queue = &queue;
    slave.generation = 7U;
    slave.owner = SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE;
    master_thread = CreateThread(NULL, 0U, claim_until_empty, &master, 0U, NULL);
    slave_thread = CreateThread(NULL, 0U, claim_until_empty, &slave, 0U, NULL);
    if (!expect(master_thread != NULL && slave_thread != NULL,
                "host claim threads must start")) return 1;
    WaitForSingleObject(master_thread, INFINITE);
    WaitForSingleObject(slave_thread, INFINITE);
    GetExitCodeThread(master_thread, &master_result);
    GetExitCodeThread(slave_thread, &slave_result);
    CloseHandle(master_thread);
    CloseHandle(slave_thread);
    if (!expect(master_result == 0UL && slave_result == 0UL,
                "race claimant must complete only its own claim")) return 1;
    for (uint16_t job = 0U; job < 4U; job++) {
        const LONG claims = master.claims[job] + slave.claims[job];
        if (!expect(claims == 1L, "every published job needs exactly one owner"))
            return 1;
        if (!expect(sm64_saturn_render_job_queue_job(&queue, 7U, job) ==
                        &queue.jobs[job],
                    "terminal merge order must retain publication order")) return 1;
    }
    if (!expect(sm64_saturn_render_job_queue_all_terminal(&queue, 7U),
                "queue must be terminal after both consumers finish")) return 1;
    if (!expect(!sm64_saturn_render_job_queue_reset_retired(&queue, 6U),
                "stale generation must not reset completed work")) return 1;
    if (!expect(sm64_saturn_render_job_queue_reset_retired(&queue, 7U),
                "retired terminal generation must reset")) return 1;

    sm64_saturn_render_job_queue_init(&queue);
    if (!expect(!sm64_saturn_render_job_queue_publish(
                    &queue, 7U, k_jobs,
                    (uint16_t)(SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY + 1U)),
                "queue-full publication must fail closed")) return 1;
    if (!expect(sm64_saturn_render_job_queue_publish(&queue, 7U, k_jobs, 2U),
                "small second queue must publish")) return 1;
    if (!expect(sm64_saturn_render_job_queue_claim_slave(&queue, 7U, &index),
                "slow slave must be able to reserve one job")) return 1;
    if (!expect(sm64_saturn_render_job_queue_claim_master(&queue, 7U, &index),
                "master must claim useful work while slave is occupied")) return 1;
    if (!expect(!sm64_saturn_render_job_queue_reset_retired(&queue, 7U),
                "reset before terminal retirement must fail closed")) return 1;

    /* The persistent polling consumer resolves descriptor callback IDs through
     * a static table. It claims until no READY work remains and never needs a
     * per-job function pointer or a second join path. */
    sm64_saturn_render_job_queue_init(&queue);
    callback_context_t callbacks = {0};
    sm64_saturn_render_job_t polling_jobs[4];
    memcpy(polling_jobs, k_jobs, sizeof(polling_jobs));
    for (uint16_t job = 0U; job < 4U; job++)
        polling_jobs[job].snapshot_generation = 11U;
    const sm64_saturn_render_job_callback_table_t callback_table = {
        .entries = {record_callback, record_callback, record_callback,
                    record_callback}};
    if (!expect(sm64_saturn_render_job_queue_publish(
                    &queue, 11U, polling_jobs, 4U),
                "polling queue generation must publish")) return 1;
    if (!expect(sm64_saturn_render_job_queue_drain_slave(
                    &queue, 11U, &callback_table, &callbacks) == 4U,
                "slave polling consumer must drain every ready job")) return 1;
    if (!expect(callbacks.total == 4U && callbacks.calls[0] == 1U &&
                    callbacks.calls[1] == 1U && callbacks.calls[2] == 1U &&
                    callbacks.calls[3] == 1U,
                "callback IDs must resolve through the static table")) return 1;
    if (!expect(sm64_saturn_render_job_queue_all_terminal(&queue, 11U),
                "polling consumer must terminally retire every callback")) return 1;

    puts("render job queue fixture: PASS");
    return 0;
}
