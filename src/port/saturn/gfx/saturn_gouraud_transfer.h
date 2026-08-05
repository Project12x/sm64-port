#ifndef SM64_SATURN_GOURAUD_TRANSFER_H
#define SM64_SATURN_GOURAUD_TRANSFER_H

#include <stdbool.h>

#include "saturn_gouraud_bank.h"
#include "../gpl/slavedriver_dma_queue.h"

/* Compatibility helper for boot/standalone clients. The accepted sourceboot
 * frame pipeline submits command + Gouraud descriptors atomically through
 * saturn_vdp1_frame_bank and must not call this one-sided retry path.
 * Submit the used Gouraud prefix, draining and retrying at most once. A zero
 * prefix succeeds with an invalid sequence. False means no transfer exists
 * after the retry, so the caller must not upload or publish the command list. */
bool sm64_saturn_gouraud_transfer_submit(
    const sm64_saturn_gouraud_bank_t *bank,
    saturn_dma_queue_sequence_t *sequence, bool *retried);

#endif
