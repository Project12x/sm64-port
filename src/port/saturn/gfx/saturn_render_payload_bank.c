#include "saturn_render_payload_bank.h"
#include "port/saturn/platform/saturn_cart_code.h"

static bool span_valid(const sm64_saturn_render_payload_bank_t *bank,
                       uint16_t offset, uint16_t capacity)
{
    return bank != NULL && bank->item_size != 0U && offset <= bank->capacity &&
        capacity <= (uint16_t)(bank->capacity - offset);
}

SM64_SATURN_CART_COLD
void sm64_saturn_render_payload_bank_init(
    sm64_saturn_render_payload_bank_t *bank, void *master, void *slave,
    uint16_t item_size, uint16_t capacity)
{
    if (bank == NULL) return;
    *bank = (sm64_saturn_render_payload_bank_t){
        .lane = {(uint8_t *)master, (uint8_t *)slave},
        .item_size = item_size,
        .capacity = capacity,
    };
}

void *sm64_saturn_render_payload_bank_write(
    sm64_saturn_render_payload_bank_t *bank,
    const sm64_saturn_render_job_execution_t *execution)
{
    if (bank == NULL || execution == NULL || execution->writer_lane > 1U ||
        bank->lane[execution->writer_lane] == NULL ||
        !span_valid(bank, execution->output_offset, execution->output_capacity))
        return NULL;
    return bank->lane[execution->writer_lane] +
        (size_t)execution->output_offset * bank->item_size;
}

const void *sm64_saturn_render_payload_bank_read(
    const sm64_saturn_render_payload_bank_t *payload,
    const sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_output_bank_t *terrain,
    const sm64_saturn_render_output_bank_t *actor, uint16_t job_index,
    uint8_t reader_lane)
{
    if (payload == NULL || queue == NULL || reader_lane > 1U) return NULL;
    const sm64_saturn_render_job_t *const job =
        sm64_saturn_render_job_queue_done_job(queue, job_index);
    if (job == NULL ||
        !span_valid(payload, job->output_offset, job->output_capacity))
        return NULL;
    const sm64_saturn_render_output_bank_t *const metadata =
        sm64_saturn_render_output_bank_kind_for_job(job) ==
                SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN ? terrain : actor;
    uint8_t writer_lane = UINT8_MAX;
    if (metadata == NULL ||
        !sm64_saturn_render_output_bank_owner_lane(
            metadata, queue, job_index, &writer_lane) ||
        writer_lane > 1U || payload->lane[writer_lane] == NULL)
        return NULL;
    const void *const cached = payload->lane[writer_lane] +
        (size_t)job->output_offset * payload->item_size;
    return sm64_saturn_render_job_bridge_read_output(
        queue, terrain, actor, job_index, reader_lane, cached);
}
