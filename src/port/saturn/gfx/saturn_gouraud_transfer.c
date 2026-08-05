#include "saturn_gouraud_transfer.h"

bool sm64_saturn_gouraud_transfer_submit(
    const sm64_saturn_gouraud_bank_t *bank,
    saturn_dma_queue_sequence_t *sequence, bool *retried)
{
    if (bank == NULL || sequence == NULL || retried == NULL)
        return false;
    *sequence = SATURN_DMA_QUEUE_SEQUENCE_INVALID;
    *retried = false;
    const size_t bytes = sm64_saturn_gouraud_bank_used_bytes(bank);
    if (bytes == 0U)
        return true;
    *sequence = saturn_dma_queue_submit(
        (void *)bank->vram_base, bank->staging, bytes, SATURN_DMA_QUEUE_SCU);
    if (*sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID)
        return true;
    *retried = true;
    saturn_dma_queue_drain();
    *sequence = saturn_dma_queue_submit(
        (void *)bank->vram_base, bank->staging, bytes, SATURN_DMA_QUEUE_SCU);
    return *sequence != SATURN_DMA_QUEUE_SEQUENCE_INVALID;
}
