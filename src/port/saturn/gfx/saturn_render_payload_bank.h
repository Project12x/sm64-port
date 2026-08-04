/* Physical result storage selected by a descriptor's recorded claimant. */
#ifndef SM64_SATURN_RENDER_PAYLOAD_BANK_H
#define SM64_SATURN_RENDER_PAYLOAD_BANK_H

#include <stddef.h>

#include "saturn_render_job_bridge.h"

typedef struct sm64_saturn_render_payload_bank {
    uint8_t *lane[2];
    uint16_t item_size;
    uint16_t capacity;
} sm64_saturn_render_payload_bank_t;

void sm64_saturn_render_payload_bank_init(
    sm64_saturn_render_payload_bank_t *bank, void *master, void *slave,
    uint16_t item_size, uint16_t capacity);

void *sm64_saturn_render_payload_bank_write(
    sm64_saturn_render_payload_bank_t *bank,
    const sm64_saturn_render_job_execution_t *execution);

const void *sm64_saturn_render_payload_bank_read(
    const sm64_saturn_render_payload_bank_t *payload,
    const sm64_saturn_render_job_queue_t *queue,
    const sm64_saturn_render_output_bank_t *terrain,
    const sm64_saturn_render_output_bank_t *actor, uint16_t job_index,
    uint8_t reader_lane);

#endif
