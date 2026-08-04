#include "saturn_render_job_bridge.h"

static sm64_saturn_render_output_bank_t *bridge_bank_for_job(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_output_bank_t *terrain,
    sm64_saturn_render_output_bank_t *actor)
{
    switch (sm64_saturn_render_output_bank_kind_for_job(job)) {
    case SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN: return terrain;
    case SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR: return actor;
    default: return NULL;
    }
}

bool sm64_saturn_render_job_bridge_begin_output(
    sm64_saturn_render_job_queue_t *queue,
    sm64_saturn_render_output_bank_t *terrain,
    sm64_saturn_render_output_bank_t *actor, uint16_t job_index,
    sm64_saturn_render_job_execution_t *execution)
{
    if (execution != NULL) *execution = (sm64_saturn_render_job_execution_t){0};
    if (queue == NULL || execution == NULL || job_index >= queue->count)
        return false;
    const sm64_saturn_render_job_t *const job = &queue->jobs[job_index];
    sm64_saturn_render_output_bank_t *const bank =
        bridge_bank_for_job(job, terrain, actor);
    if (bank == NULL || !sm64_saturn_render_output_bank_publish(
                            bank, queue, job_index))
        return false;
    uint8_t writer_lane = UINT8_MAX;
    if (!sm64_saturn_render_output_bank_owner_lane(bank, queue, job_index,
                                                    &writer_lane))
        return false;
    execution->job_index = job_index;
    execution->output_offset = job->output_offset;
    execution->output_capacity = job->output_capacity;
    execution->writer_lane = writer_lane;
    return true;
}

const void *sm64_saturn_render_job_bridge_read_output(
    const sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_output_bank_t *terrain,
    const sm64_saturn_render_output_bank_t *actor, uint16_t job_index,
    uint8_t reader_lane, const void *cached)
{
    if (queue == NULL || cached == NULL)
        return NULL;
    const sm64_saturn_render_job_t *const job =
        sm64_saturn_render_job_queue_done_job(queue, job_index);
    if (job == NULL) return NULL;
    const sm64_saturn_render_output_bank_t *const bank = bridge_bank_for_job(
        job, (sm64_saturn_render_output_bank_t *)terrain,
        (sm64_saturn_render_output_bank_t *)actor);
    if (bank == NULL) return NULL;
    return sm64_saturn_render_output_bank_read_range(
        bank, queue, job_index, reader_lane, cached);
}
